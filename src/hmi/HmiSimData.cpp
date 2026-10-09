// =============================================================================
//  hmi/HmiSimData.cpp - 1.11.15 : la remanence de simulation (voir le .hpp)
// =============================================================================
#include "HmiSimData.hpp"

#include "HmiEnums.hpp"
#include "HmiStore.hpp"
#include "HmiTypeRegistry.hpp"   // 1.11.19 (refonte, lot 6) : la regle de conversion
#include "HmiTypes.hpp"
#include "HmiZones.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <limits>
#include <map>
#include <set>

namespace hmi::simdata {
namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
// Le type declare, pour comparer : sans blancs, sans casse, LREAL = REAL.
std::string typeKey(std::string_view s) {
    std::string out;
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return out == "LREAL" ? std::string("REAL") : out;
}
bool plainInteger(sim::Type t) {
    switch (t) {
        case sim::Type::Byte: case sim::Type::Word: case sim::Type::DWord:
        case sim::Type::Int:  case sim::Type::DInt: case sim::Type::UInt: case sim::Type::UDInt: return true;
        default: return false;
    }
}
std::pair<long long, long long> rangeOf(sim::Type t) {
    switch (t) {
        case sim::Type::Byte:  return {0, 255};
        case sim::Type::Word:  case sim::Type::UInt: return {0, 65535};
        case sim::Type::DWord: case sim::Type::UDInt: return {0, 4294967295LL};
        case sim::Type::Int:   return {-32768, 32767};
        case sim::Type::DInt:  return {-2147483648LL, 2147483647LL};
        default:               return {std::numeric_limits<long long>::min(), std::numeric_limits<long long>::max()};
    }
}
std::string typeName(sim::Type t) { return std::string(sim::toString(t)); }

std::string str(const Record& r, const char* key) {
    const auto* v = r.get(key);
    return v ? *v : std::string{};
}
long long num(const Record& r, const char* key, long long fallback) {
    const auto* v = r.get(key);
    if (!v || v->empty()) return fallback;
    long long n = 0;
    const auto res = std::from_chars(v->data(), v->data() + v->size(), n);
    return res.ec == std::errc{} && res.ptr == v->data() + v->size() ? n : fallback;
}
std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

} // namespace

std::string nowStamp() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::string valueText(const sim::Value& v) {
    switch (v.type()) {
        case sim::Type::Bool: return v.isTruthy() ? "TRUE" : "FALSE";
        case sim::Type::Real: {
            char buf[64];
            const auto r = std::to_chars(buf, buf + sizeof buf, v.asReal());
            return r.ec == std::errc{} ? std::string(buf, r.ptr) : std::string("0");
        }
        case sim::Type::String: return v.asString();
        case sim::Type::Unknown: return {};
        default: return std::to_string(v.asInteger());
    }
}
std::optional<sim::Value> valueFrom(sim::Type t, const std::string& text) {
    switch (t) {
        case sim::Type::Unknown: return std::nullopt;
        case sim::Type::Bool:
            if (text == "TRUE" || text == "1") return sim::Value::boolean(true);
            if (text == "FALSE" || text == "0") return sim::Value::boolean(false);
            return std::nullopt;
        case sim::Type::String: return sim::Value::text(text);
        case sim::Type::Real: {
            double d = 0;
            const auto r = std::from_chars(text.data(), text.data() + text.size(), d);
            if (r.ec != std::errc{} || r.ptr != text.data() + text.size()) return std::nullopt;
            return sim::Value::real(d);
        }
        default: {
            long long n = 0;
            const auto r = std::from_chars(text.data(), text.data() + text.size(), n);
            if (r.ec != std::errc{} || r.ptr != text.data() + text.size()) return std::nullopt;
            if (t == sim::Type::Time) return sim::Value::time(n);
            return sim::Value::integer(t, n);
        }
    }
}
// 1.11.19 (refonte, lot 6) : la regle du registre (typereg::conversion), et pour une VALEUR
// entiere, sa place : elle passe dans un autre entier, meme plus petit, si elle y tient.
std::optional<sim::Value> convert(const sim::Value& from, sim::Type to) {
    const sim::Type t = from.type();
    if (t == sim::Type::Unknown || to == sim::Type::Unknown) return std::nullopt;
    if (t == to) return from;
    const std::string a(sim::toString(t)), b(sim::toString(to));
    if (typereg::isInteger(a) && typereg::isInteger(b)) {
        if (!typereg::valueFits(from.asInteger(), b)) return std::nullopt;   // elle n'y tient pas : rien de tronque en cachette
        return sim::Value::integer(to, from.asInteger());
    }
    if (typereg::isInteger(a) && to == sim::Type::Real && typereg::conversion(a, b).lenient())
        return sim::Value::real(static_cast<double>(from.asInteger()));
    return std::nullopt;                                       // BOOL, TIME, STRING, REAL -> entier : incompatibles
}

std::string serialize(const Snapshot& s) {
    std::string out(kHeader);
    out += "\n# XPGAnalyser : les donnees de la simulation IHM (remanence de simulation). Ecrit par l'application.\n";
    out += "prise date=" + quote(s.date) + " session=" + std::to_string(s.session) + "\n";
    for (const auto& c : s.cells)
        out += "case variable=" + std::to_string(c.variable) + " nom=" + quote(c.name) + " declare=" + quote(c.declared) + " chemin=" + quote(c.path)
             + " type=" + typeName(c.value.type()) + " valeur=" + quote(valueText(c.value)) + "\n";
    std::size_t twinLines = 0;
    for (const auto& t : s.twins) {
        // Une memoire toute a zero n'a aucune plage : une ligne vide la represente
        // (au retour, la memoire de l'esclave est remise a zero, comme a la prise).
        if (t.memory.empty()) {
            out += "jumeau equipement=" + std::to_string(t.equipment) + " nom=" + quote(t.name) + " table=" + std::string(zones::tableKey(MemTable::Holding))
                 + " debut=0 valeurs=\"\"\n";
            ++twinLines;
            continue;
        }
        for (const auto& m : t.memory) {
            std::string values;
            for (std::size_t i = 0; i < m.values.size(); ++i) values += (i ? "," : "") + std::to_string(m.values[i]);
            out += "jumeau equipement=" + std::to_string(t.equipment) + " nom=" + quote(t.name) + " table=" + std::string(zones::tableKey(m.table))
                 + " debut=" + std::to_string(m.first) + " valeurs=" + quote(values) + "\n";
            ++twinLines;
        }
    }
    out += "fin cases=" + std::to_string(s.cells.size()) + " jumeaux=" + std::to_string(twinLines) + "\n";
    return out;
}

bool parse(std::string_view text, Snapshot& out, std::string* why) {
    out = Snapshot{};
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        out = Snapshot{};
        return false;
    };
    std::size_t at = 0, lineNo = 0, twinLines = 0;
    bool header = false, end = false;
    while (at < text.size()) {
        const auto nl = text.find('\n', at);
        std::string_view line = text.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at);
        at = nl == std::string_view::npos ? text.size() : nl + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        if (!header) {
            if (line != kHeader) return fail("ce n'est pas un instantan\xC3\xA9 de simulation");
            header = true;
            continue;
        }
        if (end) return fail("des lignes apr\xC3\xA8s la fin (ligne " + std::to_string(lineNo) + ")");
        Record r;
        std::string error;
        if (!parseRecord(line, r, error)) return fail("ligne " + std::to_string(lineNo) + " illisible : " + error);
        if (r.word == "prise") {
            out.date = str(r, "date");
            out.session = static_cast<int>(num(r, "session", 0));
        } else if (r.word == "case") {
            Cell c;
            c.variable = static_cast<Id>(num(r, "variable", static_cast<long long>(kNoId)));
            c.name = str(r, "nom");
            c.declared = str(r, "declare");
            c.path = str(r, "chemin");
            const auto type = sim::typeFromName(str(r, "type"));
            auto v = valueFrom(type, str(r, "valeur"));
            if (c.variable == kNoId || !v) return fail("case illisible (ligne " + std::to_string(lineNo) + ")");
            c.value = std::move(*v);
            out.cells.push_back(std::move(c));
        } else if (r.word == "jumeau") {
            const Id eq = static_cast<Id>(num(r, "equipement", static_cast<long long>(kNoId)));
            const auto table = zones::tableFrom(str(r, "table"));
            const long long first = num(r, "debut", -1);
            if (eq == kNoId || !table || first < 0 || first > 65535) return fail("jumeau illisible (ligne " + std::to_string(lineNo) + ")");
            SavedMemory m;
            m.table = *table;
            m.first = static_cast<std::uint32_t>(first);
            const std::string values = str(r, "valeurs");
            std::string cur;
            for (std::size_t i = 0; i <= values.size(); ++i) {
                if (i == values.size() || values[i] == ',') {
                    if (!cur.empty()) {
                        long long n = -1;
                        const auto res = std::from_chars(cur.data(), cur.data() + cur.size(), n);
                        if (res.ec != std::errc{} || n < 0 || n > 65535) return fail("jumeau illisible (ligne " + std::to_string(lineNo) + ")");
                        m.values.push_back(static_cast<std::uint16_t>(n));
                    }
                    cur.clear();
                } else {
                    cur.push_back(values[i]);
                }
            }
            auto it = std::find_if(out.twins.begin(), out.twins.end(), [eq](const TwinMemory& t) { return t.equipment == eq; });
            if (it == out.twins.end()) {
                out.twins.push_back(TwinMemory{eq, str(r, "nom"), {}});
                it = std::prev(out.twins.end());
            }
            if (!m.values.empty()) it->memory.push_back(std::move(m));   // une ligne vide : la memoire toute a zero
            ++twinLines;
        } else if (r.word == "fin") {
            if (num(r, "cases", -1) != static_cast<long long>(out.cells.size()) || num(r, "jumeaux", -1) != static_cast<long long>(twinLines))
                return fail("instantan\xC3\xA9 incomplet : la fin ne compte pas les m\xC3\xAAmes lignes");
            end = true;
        }
        // un autre mot : d'une version plus recente, ignore
    }
    if (!header) return fail("fichier vide");
    if (!end) return fail("instantan\xC3\xA9 coup\xC3\xA9 (pas de ligne de fin)");
    return true;
}

std::vector<Cell> captureVariables(const Project& p, const Reader& read, const std::function<bool(const Variable&)>& keep) {
    std::vector<Cell> cells;
    for (const auto& var : p.programs.variables) {
        if (!var.equipment.empty()) continue;            // liee : la memoire de son esclave simule
        if (keep && !keep(var)) continue;
        const auto put = [&](const std::string& rel) {
            const sim::Value* v = read(var.name + rel);
            if (!v || v->type() == sim::Type::Unknown) return;
            cells.push_back(Cell{var.id, var.name, var.type, rel, *v});
        };
        if (!types::isComposite(var.type) || findEnumeration(p, var.type)) {
            put({});
            continue;
        }
        std::string error;
        const auto leaves = types::leafVariables(p, var, nullptr, nullptr, &error);
        if (!error.empty()) continue;
        const std::string root = upper(var.name);
        for (const auto& leaf : leaves) {
            const std::string up = upper(leaf.name);
            put(up.rfind(root, 0) == 0 ? leaf.name.substr(var.name.size()) : leaf.name);
        }
    }
    return cells;
}

Report restoreVariables(const Project& p, const std::vector<Cell>& cells, const Slot& slot) {
    Report r;
    std::map<Id, const Variable*> byId;
    for (const auto& v : p.programs.variables) byId[v.id] = &v;
    std::vector<Id> order;
    std::map<Id, std::vector<const Cell*>> groups;
    for (const auto& c : cells) {
        if (isDeclarationCell(c)) continue;      // 1.11.18 (lot 5) : une declaration Persistante - le moteur la rend
        auto& g = groups[c.variable];
        if (g.empty()) order.push_back(c.variable);
        g.push_back(&c);
    }
    std::set<Id> seen;
    for (const Id id : order) {
        const auto& cs = groups[id];
        const Cell& first = *cs.front();
        const auto it = byId.find(id);
        if (it == byId.end()) {
            ++r.removed;
            r.notes.push_back(first.name + " : supprim\xC3\xA9" "e du projet \xE2\x80\x94 ignor\xC3\xA9" "e");
            continue;
        }
        const Variable& var = *it->second;
        seen.insert(id);
        if (!var.equipment.empty()) {
            r.notes.push_back(var.name + " : li\xC3\xA9" "e \xC3\xA0 " + var.equipment + " \xE2\x80\x94 sa valeur vient de l'\xC3\xA9quipement");
            continue;
        }
        const HmiType* enumNow = findEnumeration(p, var.type);
        const bool structuredNow = types::isComposite(var.type) && !enumNow;
        const bool wasSimple = cs.size() == 1 && first.path.empty();
        if (typeKey(var.type) != typeKey(first.declared)) {
            // Un type simple qui devient un autre type simple : la conversion le dira ;
            // une structure, un tableau, une enumeration qui change de type : refuses.
            const bool simpleBoth = wasSimple && !structuredNow && !enumNow && !types::isComposite(first.declared);
            if (!simpleBoth) {
                r.incompatible += static_cast<int>(cs.size());
                r.warnings.push_back(var.name + " : son type a chang\xC3\xA9 (" + first.declared + " devient " + var.type
                                     + ") \xE2\x80\x94 incompatible, valeur initiale");
                continue;
            }
        }
        for (const Cell* c : cs) {
            sim::Value* s = slot(var.name + c->path);
            if (!s) {
                ++r.dropped;               // un membre, une case qui n'existe plus
                continue;
            }
            if (enumNow) {
                const auto n = c->value.asInteger();
                const bool known = std::any_of(enumNow->values.begin(), enumNow->values.end(), [n](const HmiEnumValue& e) { return e.value == n; });
                if (!known) {
                    ++r.incompatible;
                    r.warnings.push_back(var.name + " : la valeur " + std::to_string(n) + " n'est plus une valeur de " + var.type + " \xE2\x80\x94 valeur initiale");
                    continue;
                }
            }
            const auto v = convert(c->value, s->type());
            if (!v) {
                ++r.incompatible;
                r.warnings.push_back(var.name + c->path + " : " + typeName(c->value.type()) + " devient " + typeName(s->type())
                                     + " \xE2\x80\x94 incompatible, valeur initiale (\xC3\xA9tait " + valueText(c->value) + ")");
                continue;
            }
            if (c->value.type() != s->type()) {
                ++r.converted;
                r.notes.push_back(var.name + c->path + " : " + typeName(c->value.type()) + " devient " + typeName(s->type()) + " \xE2\x80\x94 valeur convertie");
            }
            s->assignFrom(*v);
            ++r.restored;
        }
    }
    for (const auto& v : p.programs.variables)
        if (v.equipment.empty() && !seen.count(v.id) && !groups.count(v.id)) ++r.fresh;
    return r;
}

std::string Report::summary() const {
    std::vector<std::string> parts;
    if (restored > 0 || (removed == 0 && incompatible == 0 && twins == 0))
        parts.push_back(plural(restored, "valeur rendue", "valeurs rendues") + (converted > 0 ? " (dont " + plural(converted, "convertie", "converties") + ")" : std::string{}));
    if (twins > 0) parts.push_back(plural(twins, "esclave simul\xC3\xA9 rendu", "esclaves simul\xC3\xA9s rendus"));
    if (removed > 0) parts.push_back(plural(removed, "variable supprim\xC3\xA9" "e ignor\xC3\xA9" "e", "variables supprim\xC3\xA9" "es ignor\xC3\xA9" "es"));
    if (dropped > 0) parts.push_back(plural(dropped, "case disparue ignor\xC3\xA9" "e", "cases disparues ignor\xC3\xA9" "es"));
    if (incompatible > 0) parts.push_back(plural(incompatible, "valeur incompatible (valeur initiale)", "valeurs incompatibles (valeur initiale)"));
    if (fresh > 0) parts.push_back(plural(fresh, "nouvelle variable (valeur initiale)", "nouvelles variables (valeur initiale)"));
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? " \xC2\xB7 " : "") + parts[i];
    return out;
}

} // namespace hmi::simdata
