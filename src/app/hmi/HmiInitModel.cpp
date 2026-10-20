// =============================================================================
//  app/hmi/HmiInitModel.cpp - 1.12.16 (T4) : la valeur initiale, sans ecran
// =============================================================================
#include "HmiInitModel.hpp"

#include "../../hmi/HmiEnums.hpp"
#include "../../hmi/HmiRuntime.hpp"     // 1.12.19 : isVariablePath
#include "../../hmi/HmiSymbols.hpp"     // 1.12.19 : hmi::cellValue (la regle des cases)
#include "../../hmi/HmiNatives.hpp"
#include "../../hmi/HmiTypeForms.hpp"
#include "../../hmi/HmiTypeRegistry.hpp"
#include "../../hmi/HmiTypes.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace app::initvalue {

namespace tf = hmi::typeform;

namespace {

std::string trim(std::string_view s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(a, b - a + 1));
}

std::string upper(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}

bool numberText(const std::string& t, double& v) {
    const std::string s = trim(t);
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end && *end == '\0';
}

// Les elements de premier niveau separes par des virgules : "INT, STRING", "0..4, 1..3".
std::vector<std::string> splitTop(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (const char c : s) {
        if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') --depth;
        if (c == ',' && depth == 0) {
            out.push_back(trim(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

// "1..4" -> {1, 4} ; faux : illisible (ou plus de 10 000 cases).
bool bounds(const std::string& text, long long& lo, long long& hi) {
    const auto dots = text.find("..");
    if (dots == std::string::npos) return false;
    char* end = nullptr;
    const std::string a = trim(text.substr(0, dots)), b = trim(text.substr(dots + 2));
    lo = std::strtoll(a.c_str(), &end, 10);
    if (a.empty() || !end || *end) return false;
    hi = std::strtoll(b.c_str(), &end, 10);
    if (b.empty() || !end || *end) return false;
    return hi >= lo && hi - lo < 10000;
}

bool textType(std::string_view type) {
    const std::string u = upper(trim(type));
    return u == "STRING" || u == "WSTRING" || u.rfind("STRING[", 0) == 0 || u.rfind("STRING(", 0) == 0 || u.rfind("WSTRING[", 0) == 0
        || u == "CHAR" || u == "WCHAR";
}

std::string unquoted(const std::string& t) {
    const std::string s = trim(t);
    if (s.size() >= 2 && (s.front() == '\'' || s.front() == '"') && s.back() == s.front()) {
        std::string in = s.substr(1, s.size() - 2), out;
        for (std::size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '$' && i + 1 < in.size() && (in[i + 1] == '\'' || in[i + 1] == '$')) {
                out += in[i + 1];
                ++i;
                continue;
            }
            out += in[i];
        }
        return out;
    }
    return s;
}

std::string quoted(const std::string& t) {
    std::string out = "'";
    for (const char c : t) {
        if (c == '\'' || c == '$') out += '$';
        out += c;
    }
    return out + "'";
}

// La valeur d'une case telle que le litteral l'ecrit. 1.12.19 (la regle des cases) : lue comme toute
// case de valeur (hmi::cellValue) - un texte sans apostrophes (Azote -> 'Azote' ; 'Azote' tape reste
// 'Azote', avant '$'Azote$''), 2,5 -> 2.5, VRAI -> TRUE, 5s -> T#5s, =formule ; illisible : telle quelle
// (problems() dit pourquoi) ; vide : rien.
std::string written(std::string_view type, const std::string& cell) {
    const std::string t = trim(cell);
    if (t.empty()) return {};
    const auto c = hmi::cellValue(nullptr, type, t);
    return c.ok() ? c.text : t;
}
// Et l'inverse : ce que la case montre d'une valeur ecrite - une valeur fixe telle qu'on la tape (un
// texte sans apostrophes), une formule avec son = (un nom dans une case de texte aussi : sans le =, il
// se relirait comme un texte).
std::string shown(std::string_view type, const std::string& value) {
    const std::string t = trim(value);
    if (t.empty() || hmi::isLiteralArgument(t)) return textType(type) ? unquoted(t) : t;
    if (!textType(type) && hmi::isVariablePath(t)) return t;
    return "=" + t;
}

std::string memberList(const std::vector<Field>& fields, const std::vector<std::string>& cells, bool all) {
    std::string out;
    for (std::size_t k = 0; k < fields.size(); ++k) {
        std::string v = k < cells.size() ? written(fields[k].type, cells[k]) : std::string{};
        if (v.empty()) {
            if (!all) continue;
            v = fields[k].zero;
        }
        out += (out.empty() ? "" : ", ") + fields[k].name + " := " + v;
    }
    return out;
}

// Les membres d'une structure lus d'un litteral "(Nom := 'A', Niveau := 2.5)" : nom -> valeur.
std::map<std::string, std::string> membersRead(const std::string& literal) {
    std::map<std::string, std::string> out;
    std::vector<std::string> items;
    if (!tf::literalItems(literal, items)) return out;
    for (const auto& it : items) {
        const auto at = it.find(":=");
        if (at == std::string::npos) continue;
        out[upper(trim(it.substr(0, at)))] = trim(it.substr(at + 2));
    }
    return out;
}

std::vector<Field> fieldsOf(const hmi::Project& p, std::string_view type) {
    std::vector<Field> out;
    for (const auto& m : hmi::types::membersOf(p, type)) {
        Field f;
        f.name = m.name;
        f.type = m.type;
        f.zero = m.initial.empty() ? zeroOf(p, m.type) : m.initial;
        out.push_back(std::move(f));
    }
    return out;
}

bool sameValue(const std::string& a, const std::string& b) {
    double x = 0, y = 0;
    if (numberText(a, x) && numberText(b, y)) return x == y;
    return unquoted(a) == unquoted(b);
}

} // namespace

Shape shapeOfType(const hmi::Project& p, std::string_view type);   // shapeOf sans la valeur du type de l'element

std::string_view kindKey(Kind k) noexcept {
    switch (k) {
        case Kind::Map:    return "map";
        case Kind::List:   return "liste";
        case Kind::Struct: return "structure";
        case Kind::Array:  return "tableau";
        case Kind::Matrix: return "tableau2d";
        case Kind::Set:    return "ensemble";
        case Kind::Ring:   return "tampon";
        case Kind::Enum:   return "enumeration";
        case Kind::Time:   return "duree";
        case Kind::Tuple:  return "tuple";
        case Kind::Scalar: return "simple";
    }
    return "simple";
}

std::string zeroOf(const hmi::Project& p, std::string_view type) {
    const std::string u = upper(trim(type));
    if (textType(u)) return u == "CHAR" || u == "WCHAR" ? "' '" : "''";
    if (u == "BOOL") return "FALSE";
    if (u == "TIME") return "T#0s";
    if (u == "TOD" || u == "TIME_OF_DAY") return "TOD#00:00:00";
    if (u == "DATE") return "D#1970-01-01";
    if (u == "DT" || u == "DATE_AND_TIME") return "DT#1970-01-01-00:00:00";
    if (u == "REAL" || u == "LREAL") return "0.0";
    const auto sh = tf::decompose(type);
    if (sh.form == tf::Form::Tuple) {
        std::vector<std::string> items{zeroOf(p, sh.element)};
        for (const auto& t : splitTop(sh.parameter)) items.push_back(zeroOf(p, t));
        return tf::literalOf(items, true);
    }
    if (sh.form != tf::Form::Simple) return "[]";
    if (const auto* e = hmi::findEnumeration(p, trim(type)); e && !e->values.empty()) return e->name + "#" + e->values.front().name;
    if (const auto fields = fieldsOf(p, type); !fields.empty()) return "(" + memberList(fields, {}, true) + ")";
    return "0";
}

Shape shapeOf(const hmi::Project& p, std::string_view type) {
    Shape s = shapeOfType(p, type);
    if (s.kind != Kind::Struct && s.kind != Kind::Tuple && s.kind != Kind::Enum && s.kind != Kind::Time && !s.element.empty()) {
        s.zero = s.structElements ? "(" + memberList(s.fields, {}, true) + ")" : zeroOf(p, s.element);
    }
    return s;
}

Shape shapeOfType(const hmi::Project& p, std::string_view type) {
    Shape s;
    s.type = trim(type);
    const auto sh = tf::decompose(type);
    s.element = sh.element;
    const auto structElement = [&](const std::string& el) {
        s.fields = fieldsOf(p, el);
        s.structElements = !s.fields.empty();
        if (s.structElements) s.elementName = trim(el);
    };
    switch (sh.form) {
        case tf::Form::Map: {
            s.kind = Kind::Map;
            s.key = trim(sh.parameter).empty() ? std::string("STRING") : trim(sh.parameter);
            s.textKeys = textType(s.key);
            return s;
        }
        case tf::Form::List:
        case tf::Form::Vector:
            s.kind = Kind::List;
            s.container = sh.form == tf::Form::Vector ? "VECTOR" : "LIST";
            structElement(sh.element);
            return s;
        case tf::Form::Set:
            s.kind = Kind::Set;
            return s;
        case tf::Form::Ring: {
            s.kind = Kind::Ring;
            char* end = nullptr;
            const std::string n = trim(sh.parameter);
            const long long cap = std::strtoll(n.c_str(), &end, 10);
            s.capacity = cap > 0 ? static_cast<std::size_t>(cap) : 0;
            return s;
        }
        case tf::Form::Tuple: {
            s.kind = Kind::Tuple;
            std::vector<std::string> types{sh.element};
            for (const auto& t : splitTop(sh.parameter)) types.push_back(t);
            for (std::size_t k = 0; k < types.size(); ++k) s.fields.push_back({"Item" + std::to_string(k + 1), types[k], zeroOf(p, types[k])});
            return s;
        }
        case tf::Form::Array:
        case tf::Form::Array2D: {
            const auto dims = splitTop(sh.parameter);
            if (sh.form == tf::Form::Array2D && dims.size() == 2 && bounds(dims[0], s.low, s.high) && bounds(dims[1], s.low2, s.high2)) {
                s.kind = Kind::Matrix;
                return s;
            }
            if (!dims.empty() && bounds(dims[0], s.low, s.high)) {
                s.kind = Kind::Array;
                structElement(sh.element);
                return s;
            }
            s.kind = Kind::List;                                  // des bornes illisibles : comme une liste
            return s;
        }
        case tf::Form::Simple:
            break;
    }
    const std::string u = upper(s.type);
    if (u == "TIME") {
        s.kind = Kind::Time;
        return s;
    }
    if (const auto* e = hmi::findEnumeration(p, s.type)) {
        s.kind = Kind::Enum;
        s.enumName = e->name;
        for (const auto& v : e->values) s.options.push_back({v.name, v.value, v.text.empty() ? v.name : v.text});
        return s;
    }
    if (auto fields = fieldsOf(p, s.type); !fields.empty()) {
        s.kind = Kind::Struct;
        s.fields = std::move(fields);
        return s;
    }
    s.kind = Kind::Scalar;
    s.element = s.type;
    return s;
}

namespace {

// Une duree ecrite (T#1m30s250ms, TIME#2h) : heures, minutes, secondes, millisecondes ; faux : illisible.
bool timeParts(const std::string& text, long long out[4]) {
    std::string s = upper(trim(text));
    for (auto& v : std::array<long long*, 4>{&out[0], &out[1], &out[2], &out[3]}) *v = 0;
    if (s.rfind("TIME#", 0) == 0) s = s.substr(5);
    else if (s.rfind("T#", 0) == 0) s = s.substr(2);
    else return false;
    std::size_t i = 0;
    bool any = false;
    while (i < s.size()) {
        std::size_t j = i;
        while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
        if (j == i) return false;
        const long long n = std::strtoll(s.substr(i, j - i).c_str(), nullptr, 10);
        std::size_t k = j;
        while (k < s.size() && std::isalpha(static_cast<unsigned char>(s[k]))) ++k;
        const std::string unit = s.substr(j, k - j);
        if (unit == "H") out[0] += n;
        else if (unit == "M") out[1] += n;
        else if (unit == "S") out[2] += n;
        else if (unit == "MS") out[3] += n;
        else if (unit == "D") out[0] += n * 24;
        else return false;
        any = true;
        i = k;
    }
    return any;
}

} // namespace

Raw load(const hmi::Project& p, std::string_view type, std::string_view value) {
    Raw r;
    r.shape = shapeOf(p, type);
    const Shape& s = r.shape;
    const std::string v = trim(value);
    std::vector<std::string> items;
    const bool literal = !v.empty() && tf::literalItems(v, items);
    const auto fieldCells = [&](const std::string& lit) {
        std::vector<std::string> cells;
        const auto read = membersRead(lit);
        for (const auto& f : s.fields) {
            const auto it = read.find(upper(f.name));
            cells.push_back(it == read.end() ? std::string{} : shown(f.type, it->second));
        }
        return cells;
    };
    switch (s.kind) {
        case Kind::Map:
            for (const auto& it : items) {
                const auto at = it.find(":=");
                if (at == std::string::npos) continue;
                r.rows.push_back({s.textKeys ? unquoted(it.substr(0, at)) : trim(it.substr(0, at)), {shown(s.element, it.substr(at + 2))}});
            }
            break;
        case Kind::List:
        case Kind::Set:
        case Kind::Ring:
            for (const auto& it : items) r.rows.push_back({{}, s.structElements ? fieldCells(it) : std::vector<std::string>{shown(s.element, it)}});
            break;
        case Kind::Array: {
            const auto n = static_cast<std::size_t>(s.high - s.low + 1);
            for (std::size_t k = 0; k < n; ++k) {
                const std::string it = k < items.size() ? items[k] : std::string{};
                r.rows.push_back({{}, s.structElements ? fieldCells(it) : std::vector<std::string>{shown(s.element, it)}});
            }
            break;
        }
        case Kind::Matrix: {
            const auto n1 = static_cast<std::size_t>(s.high - s.low + 1), n2 = static_cast<std::size_t>(s.high2 - s.low2 + 1);
            for (std::size_t i = 0; i < n1; ++i) {
                Row row;
                for (std::size_t j = 0; j < n2; ++j) {
                    const std::size_t k = i * n2 + j;
                    row.cells.push_back(k < items.size() ? shown(s.element, items[k]) : std::string{});
                }
                r.rows.push_back(std::move(row));
            }
            break;
        }
        case Kind::Struct: {
            const auto cells = literal ? fieldCells(v) : std::vector<std::string>(s.fields.size());
            for (std::size_t k = 0; k < s.fields.size(); ++k) r.rows.push_back({{}, {k < cells.size() ? cells[k] : std::string{}}});
            break;
        }
        case Kind::Tuple:
            for (std::size_t k = 0; k < s.fields.size(); ++k)
                r.rows.push_back({{}, {k < items.size() ? shown(s.fields[k].type, items[k]) : std::string{}}});
            break;
        case Kind::Enum: {
            std::string name = v;
            if (const auto hash = name.find('#'); hash != std::string::npos) name = name.substr(hash + 1);
            for (std::size_t k = 0; k < s.options.size(); ++k)
                if (upper(s.options[k].name) == upper(trim(name)) || (!name.empty() && std::to_string(s.options[k].number) == trim(name)))
                    r.choice = static_cast<int>(k);
            if (!v.empty() && r.choice < 0) r.freeText = true;
            break;
        }
        case Kind::Time: {
            long long parts[4]{};
            Row row;
            if (v.empty() || timeParts(v, parts)) {
                for (const long long x : parts) row.cells.push_back(v.empty() ? std::string{} : std::to_string(x));
            } else {
                r.freeText = true;
                row.cells = {v, {}, {}, {}};
            }
            r.rows.push_back(std::move(row));
            break;
        }
        case Kind::Scalar:
            r.rows.push_back({{}, {shown(s.element, v)}});
            break;
    }
    // Une valeur qui ne se lit pas comme un litteral de ce type (un nom, un calcul) : telle quelle.
    if (!v.empty() && !literal && s.kind != Kind::Scalar && s.kind != Kind::Enum && s.kind != Kind::Time) {
        r.freeText = true;
        r.rows.clear();
        r.rows.push_back({{}, {v}});
    }
    return r;
}

std::string literal(const Raw& r) {
    const Shape& s = r.shape;
    if (r.freeText) return r.rows.empty() || r.rows.front().cells.empty() ? std::string{} : trim(r.rows.front().cells.front());
    std::vector<std::string> items;
    const auto element = [&](const Row& row) {
        if (s.structElements) {
            const std::string m = memberList(s.fields, row.cells, false);
            return m.empty() ? std::string{} : "(" + m + ")";
        }
        return row.cells.empty() ? std::string{} : written(s.element, row.cells.front());
    };
    switch (s.kind) {
        case Kind::Map: {
            std::vector<std::pair<std::string, std::string>> entries;     // la derniere gagne
            for (const auto& row : r.rows) {
                const std::string k = trim(row.key);
                if (k.empty()) continue;
                std::string val = row.cells.empty() ? std::string{} : written(s.element, row.cells.front());
                if (val.empty()) val = s.zero;
                const auto at = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return sameValue(e.first, k); });
                if (at != entries.end()) at->second = val;
                else entries.emplace_back(k, val);
            }
            std::sort(entries.begin(), entries.end(), [&](const auto& a, const auto& b) {
                double x = 0, y = 0;
                if (!s.textKeys && numberText(a.first, x) && numberText(b.first, y)) return x < y;
                return a.first < b.first;
            });
            for (const auto& [k, val] : entries) items.push_back((s.textKeys ? quoted(k) : k) + " := " + val);
            return tf::literalOf(items, false);
        }
        case Kind::List:
        case Kind::Set:
        case Kind::Ring:
            for (const auto& row : r.rows)
                if (auto e = element(row); !e.empty()) items.push_back(std::move(e));
            if (s.kind == Kind::Ring && s.capacity > 0 && items.size() > s.capacity)
                items.erase(items.begin(), items.end() - static_cast<std::ptrdiff_t>(s.capacity));
            if (s.kind == Kind::Set) {
                std::stable_sort(items.begin(), items.end(), [](const std::string& a, const std::string& b) {
                    double x = 0, y = 0;
                    const bool na = numberText(a, x), nb = numberText(b, y);
                    if (na && nb) return x < y;
                    if (na != nb) return na;
                    return unquoted(a) < unquoted(b);
                });
                items.erase(std::unique(items.begin(), items.end(), sameValue), items.end());
            }
            return tf::literalOf(items, false);
        case Kind::Array:
        case Kind::Matrix: {
            std::vector<std::string> flat;
            for (const auto& row : r.rows) {
                if (s.kind == Kind::Array) flat.push_back(element(row));
                else for (const auto& c : row.cells) flat.push_back(written(s.element, c));
            }
            std::size_t last = 0;
            for (std::size_t k = 0; k < flat.size(); ++k)
                if (!flat[k].empty()) last = k + 1;
            const std::string zero = s.zero;
            for (std::size_t k = 0; k < last; ++k) items.push_back(flat[k].empty() ? zero : flat[k]);
            return tf::literalOf(items, false);
        }
        case Kind::Struct: {
            std::vector<std::string> cells;
            for (const auto& row : r.rows) cells.push_back(row.cells.empty() ? std::string{} : row.cells.front());
            std::string m = memberList(s.fields, cells, false);
            if (m.empty() && !s.fields.empty()) m = s.fields.front().name + " := " + s.fields.front().zero;   // tout du type : le premier membre dit
            return "(" + m + ")";
        }
        case Kind::Tuple:
            for (std::size_t k = 0; k < s.fields.size(); ++k) {
                const std::string c = k < r.rows.size() && !r.rows[k].cells.empty() ? written(s.fields[k].type, r.rows[k].cells.front()) : std::string{};
                items.push_back(c.empty() ? s.fields[k].zero : c);
            }
            return tf::literalOf(items, true);
        case Kind::Enum:
            return r.choice >= 0 && static_cast<std::size_t>(r.choice) < s.options.size() ? s.enumName + "#" + s.options[static_cast<std::size_t>(r.choice)].name
                                                                                         : std::string{};
        case Kind::Time: {
            if (r.rows.empty()) return {};
            static const char* kUnit[] = {"h", "m", "s", "ms"};
            std::string out = "T#";
            bool any = false, written_ = false;
            for (std::size_t k = 0; k < 4 && k < r.rows.front().cells.size(); ++k) {
                const std::string c = trim(r.rows.front().cells[k]);
                if (c.empty()) continue;
                written_ = true;
                double x = 0;
                if (!numberText(c, x) || x == 0) continue;
                out += c + kUnit[k];
                any = true;
            }
            if (!written_) return {};
            return any ? out : std::string("T#0s");
        }
        case Kind::Scalar:
            return r.rows.empty() || r.rows.front().cells.empty() ? std::string{} : written(s.element, r.rows.front().cells.front());
    }
    return {};
}

std::vector<Problem> problems(const hmi::Project& p, const Raw& r) {
    std::vector<Problem> out;
    const Shape& s = r.shape;
    const auto add = [&](bool error, std::string text, int row = -1) {
        for (const auto& q : out)
            if (q.text == text) return;
        out.push_back({error, std::move(text), row});
    };
    const auto fits = [&](const std::string& type, const std::string& text, const std::string& where, int row) {
        const std::string w = written(type, text);
        if (w.empty()) return;
        std::string why;
        if (!tf::valueFits(p, type, w, &why)) add(true, where + trim(text) + " n'est pas un " + type + (why.empty() ? std::string{} : " (" + why + ")"), row);
    };
    if (r.freeText) {
        add(false, "une valeur qui n'est pas un litt\xC3\xA9ral de " + s.type + " : elle est gard\xC3\xA9" "e telle quelle (l'\xC3\xA9" "diteur du type la remplace)");
        return out;
    }
    switch (s.kind) {
        case Kind::Map: {
            std::set<std::string> keys;
            for (std::size_t k = 0; k < r.rows.size(); ++k) {
                const auto& row = r.rows[k];
                const std::string key = trim(row.key);
                if (key.empty()) {
                    if (!row.cells.empty() && !trim(row.cells.front()).empty()) add(false, "une ligne sans cl\xC3\xA9 ne compte pas", static_cast<int>(k));
                    continue;
                }
                if (!keys.insert(s.textKeys ? key : upper(key)).second)
                    add(true, "la cl\xC3\xA9 " + (s.textKeys ? quoted(key) : key) + " est \xC3\xA9" "crite deux fois : la derni\xC3\xA8re gagne", static_cast<int>(k));
                if (!s.textKeys) fits(s.key, key, "cl\xC3\xA9 ", static_cast<int>(k));
                if (!row.cells.empty()) fits(s.element, row.cells.front(), (s.textKeys ? quoted(key) : key) + " : ", static_cast<int>(k));
            }
            break;
        }
        case Kind::List:
        case Kind::Set:
        case Kind::Ring:
        case Kind::Array:
            for (std::size_t k = 0; k < r.rows.size(); ++k) {
                const std::string at = rowLabel(r, k) + " : ";
                if (s.structElements) {
                    for (std::size_t c = 0; c < s.fields.size() && c < r.rows[k].cells.size(); ++c)
                        fits(s.fields[c].type, r.rows[k].cells[c], rowLabel(r, k) + "." + s.fields[c].name + " : ", static_cast<int>(k));
                } else if (!r.rows[k].cells.empty()) {
                    fits(s.element, r.rows[k].cells.front(), at, static_cast<int>(k));
                }
            }
            if (s.kind == Kind::Set) {
                std::size_t dups = 0;
                for (std::size_t k = 0; k < r.rows.size(); ++k)
                    if (rowMark(r, k) == "doublon") ++dups;
                if (dups) add(false, std::to_string(dups) + (dups > 1 ? " doublons : gard\xC3\xA9s" : " doublon : gard\xC3\xA9") + " une seule fois");
            }
            if (s.kind == Kind::Ring && s.capacity > 0) {
                std::size_t n = 0;
                for (const auto& row : r.rows)
                    if (!row.cells.empty() && !trim(row.cells.front()).empty()) ++n;
                if (n > s.capacity)
                    add(false, std::to_string(n - s.capacity) + " valeur" + (n - s.capacity > 1 ? "s" : "") + " au-del\xC3\xA0 de la capacit\xC3\xA9 ("
                                   + std::to_string(s.capacity) + ") : les plus anciennes partent");
            }
            break;
        case Kind::Matrix:
            for (std::size_t k = 0; k < r.rows.size(); ++k)
                for (std::size_t c = 0; c < r.rows[k].cells.size(); ++c)
                    fits(s.element, r.rows[k].cells[c], "[" + std::to_string(s.low + static_cast<long long>(k)) + ", " + std::to_string(s.low2 + static_cast<long long>(c)) + "] : ",
                         static_cast<int>(k));
            break;
        case Kind::Struct:
        case Kind::Tuple:
            for (std::size_t k = 0; k < r.rows.size() && k < s.fields.size(); ++k)
                if (!r.rows[k].cells.empty()) fits(s.fields[k].type, r.rows[k].cells.front(), rowLabel(r, k) + " : ", static_cast<int>(k));
            break;
        case Kind::Enum:
            if (r.choice < 0) add(false, "aucune valeur choisie : la premi\xC3\xA8re (" + (s.options.empty() ? std::string("?") : s.options.front().name) + ") au d\xC3\xA9marrage");
            break;
        case Kind::Time:
            if (!r.rows.empty()) {
                static const char* kPart[] = {"heures", "minutes", "secondes", "millisecondes"};
                for (std::size_t k = 0; k < 4 && k < r.rows.front().cells.size(); ++k) {
                    const std::string c = trim(r.rows.front().cells[k]);
                    double x = 0;
                    if (!c.empty() && (!numberText(c, x) || x < 0 || x != static_cast<double>(static_cast<long long>(x))))
                        add(true, std::string(kPart[k]) + " : " + c + " n'est pas un nombre entier positif", 0);
                }
            }
            break;
        case Kind::Scalar:
            if (!r.rows.empty() && !r.rows.front().cells.empty()) fits(s.element, r.rows.front().cells.front(), {}, 0);
            break;
    }
    return out;
}

std::string rowMark(const Raw& r, std::size_t row) {
    const Shape& s = r.shape;
    if (row >= r.rows.size() || r.rows[row].cells.empty()) return {};
    const std::string v = trim(r.rows[row].cells.front());
    if (v.empty()) return {};
    if (s.kind == Kind::Set)
        for (std::size_t k = 0; k < row; ++k)
            if (!r.rows[k].cells.empty() && sameValue(written(s.element, r.rows[k].cells.front()), written(s.element, v))) return "doublon";
    if (s.kind == Kind::Ring && s.capacity > 0) {
        std::size_t after = 0;
        for (std::size_t k = row + 1; k < r.rows.size(); ++k)
            if (!r.rows[k].cells.empty() && !trim(r.rows[k].cells.front()).empty()) ++after;
        if (after >= s.capacity) return "partira";
    }
    return {};
}

std::string rowLabel(const Raw& r, std::size_t row) {
    const Shape& s = r.shape;
    switch (s.kind) {
        case Kind::Map:    return row < r.rows.size() ? (s.textKeys ? quoted(r.rows[row].key) : r.rows[row].key) : std::string{};
        case Kind::Array:  return "[" + std::to_string(s.low + static_cast<long long>(row)) + "]";
        case Kind::Matrix: return "[" + std::to_string(s.low + static_cast<long long>(row)) + ", ..]";
        case Kind::Struct: return row < s.fields.size() ? "." + s.fields[row].name : std::string{};
        case Kind::Tuple:  return row < s.fields.size() ? s.fields[row].name : std::string{};
        case Kind::List:   return "[" + std::to_string(row) + "]";
        case Kind::Set:
        case Kind::Ring:   return std::to_string(row + 1);
        default:           return {};
    }
}

std::vector<std::string> cellLabels(const Raw& r) {
    const Shape& s = r.shape;
    if (r.freeText) return {"Valeur"};
    switch (s.kind) {
        case Kind::Map: return {"Valeur (" + s.element + ")"};
        case Kind::Matrix: {
            std::vector<std::string> out;
            for (long long j = s.low2; j <= s.high2; ++j) out.push_back("[.., " + std::to_string(j) + "]");
            return out;
        }
        case Kind::Time: return {"heures", "minutes", "secondes", "millisecondes"};
        case Kind::Struct:
        case Kind::Tuple: return {"Valeur"};
        default:
            if (s.structElements) {
                std::vector<std::string> out;
                for (const auto& f : s.fields) out.push_back(f.name + " (" + f.type + ")");
                return out;
            }
            return {"Valeur (" + s.element + ")"};
    }
}

std::string placeholder(const Raw& r, std::size_t row, std::size_t cell) {
    const Shape& s = r.shape;
    std::string zero;
    switch (s.kind) {
        case Kind::Struct:
        case Kind::Tuple: zero = row < s.fields.size() ? s.fields[row].zero : std::string{}; break;
        case Kind::Time:  zero = "0"; break;
        default:
            if (s.structElements) zero = cell < s.fields.size() ? s.fields[cell].zero : std::string{};
            else zero = s.zero;
    }
    if (zero.empty()) return {};
    // 1.12.19 (la regle des cases) : l'invite d'une case vide, partout la meme - « 12 · valeur par defaut ».
    const std::string field = s.kind == Kind::Struct || s.kind == Kind::Tuple ? (row < s.fields.size() ? s.fields[row].type : std::string{})
                            : s.structElements ? (cell < s.fields.size() ? s.fields[cell].type : std::string{})
                            : s.element;
    std::string z = shown(field, zero);
    if (z.empty()) z = "vide";
    return z + " \xC2\xB7 valeur par d\xC3\xA9" "faut";
}

bool resizable(const Raw& r) noexcept {
    const Kind k = r.shape.kind;
    return !r.freeText && (k == Kind::Map || k == Kind::List || k == Kind::Set || k == Kind::Ring);
}

bool addRow(Raw& r) {
    if (!resizable(r)) return false;
    Row row;
    row.cells.assign(r.shape.structElements ? r.shape.fields.size() : 1, std::string{});
    r.rows.push_back(std::move(row));
    return true;
}

bool removeRow(Raw& r, std::size_t row) {
    if (!resizable(r) || row >= r.rows.size()) return false;
    r.rows.erase(r.rows.begin() + static_cast<std::ptrdiff_t>(row));
    return true;
}

bool moveRow(Raw& r, std::size_t row, int delta) {
    if (r.freeText || (r.shape.kind != Kind::List && r.shape.kind != Kind::Ring) || row >= r.rows.size()) return false;
    const long long to = static_cast<long long>(row) + delta;
    if (to < 0 || to >= static_cast<long long>(r.rows.size())) return false;
    std::swap(r.rows[row], r.rows[static_cast<std::size_t>(to)]);
    return true;
}

bool sortByKey(Raw& r) {
    if (r.freeText || r.shape.kind != Kind::Map) return false;
    const bool text = r.shape.textKeys;
    std::stable_sort(r.rows.begin(), r.rows.end(), [text](const Row& a, const Row& b) {
        double x = 0, y = 0;
        if (!text && numberText(a.key, x) && numberText(b.key, y)) return x < y;
        return trim(a.key) < trim(b.key);
    });
    return true;
}

bool fillZero(Raw& r) {
    if (r.freeText || (r.shape.kind != Kind::Array && r.shape.kind != Kind::Matrix)) return false;
    const std::string zero = r.shape.zero;
    for (auto& row : r.rows)
        for (std::size_t c = 0; c < row.cells.size(); ++c)
            row.cells[c] = r.shape.structElements ? std::string{} : shown(r.shape.element, zero);
    return true;
}

bool sequence(Raw& r, double start, double step) {
    if (r.freeText || r.shape.kind != Kind::Array || r.shape.structElements) return false;
    const std::string u = upper(r.shape.element);
    const bool real = u == "REAL" || u == "LREAL";
    if (!real && !hmi::typereg::isInteger(u)) return false;
    for (std::size_t k = 0; k < r.rows.size(); ++k) {
        const double v = start + step * static_cast<double>(k);
        std::string t;
        if (real) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%g", v);
            t = buf;
            if (t.find_first_of(".eE") == std::string::npos) t += ".0";
        } else {
            t = std::to_string(static_cast<long long>(v));
        }
        r.rows[k].cells.assign(1, t);
    }
    return true;
}

bool setCell(Raw& r, std::size_t row, std::size_t cell, std::string text) {
    if (row >= r.rows.size()) return false;
    auto& cells = r.rows[row].cells;
    if (cell >= cells.size()) {
        const std::size_t want = r.shape.kind == Kind::Time ? 4 : r.shape.structElements ? r.shape.fields.size() : 1;
        if (cell >= want && !(r.shape.kind == Kind::Matrix)) return false;
        cells.resize(std::max(cell + 1, want));
    }
    cells[cell] = std::move(text);
    return true;
}

bool setKey(Raw& r, std::size_t row, std::string text) {
    if (r.shape.kind != Kind::Map || row >= r.rows.size()) return false;
    r.rows[row].key = r.shape.textKeys ? unquoted(text) : trim(text);
    return true;
}

bool choose(Raw& r, int option) {
    if (r.shape.kind != Kind::Enum || option < -1 || option >= static_cast<int>(r.shape.options.size())) return false;
    r.choice = option;
    r.freeText = false;
    return true;
}

bool leaveFreeText(Raw& r) {
    if (!r.freeText) return false;
    Raw fresh = load(hmi::Project{}, r.shape.type, {});
    fresh.shape = r.shape;
    fresh.rows.clear();
    switch (r.shape.kind) {
        case Kind::Array:
            for (long long k = r.shape.low; k <= r.shape.high; ++k) fresh.rows.push_back({{}, std::vector<std::string>(r.shape.structElements ? r.shape.fields.size() : 1)});
            break;
        case Kind::Matrix:
            for (long long k = r.shape.low; k <= r.shape.high; ++k)
                fresh.rows.push_back({{}, std::vector<std::string>(static_cast<std::size_t>(r.shape.high2 - r.shape.low2 + 1))});
            break;
        case Kind::Struct:
        case Kind::Tuple:
            for (std::size_t k = 0; k < r.shape.fields.size(); ++k) fresh.rows.push_back({{}, {std::string{}}});
            break;
        case Kind::Time: fresh.rows.push_back({{}, std::vector<std::string>(4)}); break;
        case Kind::Scalar: fresh.rows.push_back({{}, {std::string{}}}); break;
        default: break;
    }
    fresh.freeText = false;
    r = std::move(fresh);
    return true;
}

// ---- le code ----------------------------------------------------------------------------
std::string header(std::string_view name, std::string_view type) { return "FUNCTION Init_" + std::string(name) + " : " + std::string(type); }

std::string toCode(std::string_view literal) {
    const std::string l = trim(literal);
    return "(* le brut, en code : \xC3\xA0 ajuster *)\nRETURN " + (l.empty() ? std::string("...") : l) + ";\n";
}

namespace {
// Un nom lisible du type `type` (le premier) ; vide : aucun.
std::string readableOf(const std::vector<Name>& names, std::string_view type) {
    for (const auto& n : names)
        if (upper(trim(n.type)) == upper(trim(type))) return n.name;
    return {};
}
std::string sample(const hmi::Project& p, std::string_view type, int k) {
    const std::string u = upper(trim(type));
    if (textType(u)) return quoted(std::string(1, static_cast<char>('A' + k)));
    if (u == "BOOL") return k % 2 ? "TRUE" : "FALSE";
    if (u == "REAL" || u == "LREAL") return std::to_string(10 * (k + 1)) + ".0";
    if (u == "TIME") return "T#" + std::to_string(k + 1) + "s";
    if (hmi::typereg::isInteger(u)) return std::to_string(k + 1);
    return zeroOf(p, type);
}
} // namespace

std::vector<Template> templates(const hmi::Project& p, std::string_view name, std::string_view type, const std::vector<Name>& readable) {
    std::vector<Template> out;
    const Shape s = shapeOf(p, type);
    const std::string t(trim(type));
    const auto add = [&](std::string label, std::string calc, std::string locals = {}) { out.push_back({std::move(label), std::move(calc), std::move(locals)}); };
    switch (s.kind) {
        case Kind::Map: {
            const std::string k1 = s.textKeys ? "'bas'" : "1", k2 = s.textKeys ? "'haut'" : "2";
            add("\xC3\x89" "crire les entr\xC3\xA9" "es",
                "m[" + k1 + "] := " + sample(p, s.element, 0) + ";\nm[" + k2 + "] := " + sample(p, s.element, 1) + ";\nRETURN m;\n",
                "m : " + t);
            if (s.textKeys)
                add("Une entr\xC3\xA9" "e par nom",
                    "FOR i := 0 TO 2 DO\n    m[noms[i]] := " + sample(p, s.element, 0) + ";\nEND_FOR;\nRETURN m;\n",
                    "m : " + t + "\nnoms : ARRAY[0..2] OF STRING := ['F1', 'F2', 'F3']");
            if (const std::string other = readableOf(readable, t); !other.empty())
                add("Partir d'une autre constante", "m := " + other + ";            (* une copie *)\nRETURN m;\n", "m : " + t);
            break;
        }
        case Kind::List:
        case Kind::Set:
        case Kind::Ring: {
            const std::string adder = s.kind == Kind::Set ? "SET_ADD" : s.kind == Kind::Ring ? "RING_ADD" : "LIST_ADD";
            if (s.structElements) {
                std::string fill;
                for (const auto& f : s.fields) {
                    const std::string u = upper(f.type);
                    if (textType(u)) fill += "    e." + f.name + " := CONCAT('" + f.name + " ', TO_STRING(i));\n";
                    else if (u == "REAL" || u == "LREAL") fill += "    e." + f.name + " := 10.0 * i;\n";
                    else if (hmi::typereg::isInteger(u)) fill += "    e." + f.name + " := i;\n";
                }
                add("Quatre \xC3\xA9l\xC3\xA9ments num\xC3\xA9rot\xC3\xA9s", "FOR i := 1 TO 4 DO\n" + fill + "    " + adder + "(l, e);\nEND_FOR;\nRETURN l;\n",
                    "l : " + t + "\ne : " + s.elementName);
                add("\xC3\x89" "crire les \xC3\xA9l\xC3\xA9ments", "RETURN [(" + memberList(s.fields, {}, true) + ")];\n");
            } else {
                add("Ajouter un \xC3\xA0 un", adder + "(l, " + sample(p, s.element, 0) + ");\n" + adder + "(l, " + sample(p, s.element, 1) + ");\nRETURN l;\n", "l : " + t);
                add("Le litt\xC3\xA9ral", "RETURN [" + sample(p, s.element, 0) + ", " + sample(p, s.element, 1) + "];\n");
                if (s.kind == Kind::Ring && s.capacity > 0)
                    add("Plus que la capacit\xC3\xA9", "FOR i := 1 TO " + std::to_string(s.capacity + 2) + " DO\n    " + adder + "(l, " +
                                                (upper(s.element) == "REAL" ? std::string("i * 1.5") : std::string("i")) + ");\nEND_FOR;\nRETURN l;   (* les "
                                                + std::to_string(s.capacity) + " derni\xC3\xA8res restent *)\n",
                        "l : " + t);
            }
            break;
        }
        case Kind::Array: {
            const std::string u = upper(s.element);
            const long long n = s.high - s.low + 1;
            if (!s.structElements && (u == "REAL" || u == "LREAL" || hmi::typereg::isInteger(u))) {
                const std::string step = u == "REAL" || u == "LREAL" ? "25.0" : "1";
                add("Une suite (IOTA)", "IOTA(t, " + std::string(u == "REAL" || u == "LREAL" ? "0.0" : "0") + ", " + step + ");\nRETURN t;\n", "t : " + t);
                if (const std::string other = readableOf(readable, s.element); !other.empty())
                    add("Jusqu'\xC3\xA0 " + other, "IOTA(t, 0.0, " + other + " / " + std::to_string(std::max<long long>(1, n - 1)) + ".0);\nRETURN t;\n", "t : " + t);
            }
            add("Case par case", "FOR i := " + std::to_string(s.low) + " TO " + std::to_string(s.high) + " DO\n    t[i] := " +
                                     (s.structElements ? std::string("t[i]") : sample(p, s.element, 0)) + ";\nEND_FOR;\nRETURN t;\n",
                "t : " + t);
            break;
        }
        case Kind::Matrix:
            add("Ligne par ligne", "FOR i := " + std::to_string(s.low) + " TO " + std::to_string(s.high) + " DO\n    FOR j := " + std::to_string(s.low2) + " TO " +
                                       std::to_string(s.high2) + " DO\n        t[i, j] := " + sample(p, s.element, 0) + ";\n    END_FOR;\nEND_FOR;\nRETURN t;\n",
                "t : " + t);
            break;
        case Kind::Struct: {
            std::string fill;
            for (const auto& f : s.fields) fill += "r." + f.name + " := " + f.zero + ";\n";
            add("Remplir membre par membre", fill + "RETURN r;\n", "r : " + t);
            add("Le litt\xC3\xA9ral", "RETURN (" + memberList(s.fields, {}, true) + ");\n");
            break;
        }
        case Kind::Enum:
            if (s.options.size() >= 2)
                add("Selon un r\xC3\xA9glage", "IF " + (readableOf(readable, "BOOL").empty() ? std::string("TRUE") : readableOf(readable, "BOOL")) + " THEN\n    RETURN " +
                                            s.enumName + "#" + s.options[1].name + ";\nEND_IF\nRETURN " + s.enumName + "#" + s.options[0].name + ";\n");
            else if (!s.options.empty())
                add("La valeur", "RETURN " + s.enumName + "#" + s.options[0].name + ";\n");
            break;
        case Kind::Time:
            add("Depuis des secondes", "RETURN TO_TIME(90 * 1000);   (* des millisecondes *)\n");
            add("Le litt\xC3\xA9ral", "RETURN T#1m30s;\n");
            break;
        case Kind::Tuple: {
            std::vector<std::string> items;
            for (std::size_t k = 0; k < s.fields.size(); ++k) items.push_back(sample(p, s.fields[k].type, static_cast<int>(k)));
            add("Le litt\xC3\xA9ral", "RETURN " + tf::literalOf(items, true) + ";\n");
            break;
        }
        case Kind::Scalar: {
            const std::string other = readableOf(readable, s.element);
            add(other.empty() ? "Un calcul" : "D'un autre r\xC3\xA9glage", "RETURN " + (other.empty() ? sample(p, s.element, 0) : other) + ";\n");
            break;
        }
    }
    (void)name;
    return out;
}

// ---- le type, explique ------------------------------------------------------------------
std::string cppOf(const hmi::Project& p, std::string_view type) {
    const Shape s = shapeOf(p, type);
    const std::string u = upper(s.element);
    const auto scalar = [&](const std::string& t) -> std::string {
        const std::string x = upper(trim(t));
        static const std::map<std::string, std::string> kMap = {
            {"BOOL", "bool"},           {"SINT", "std::int8_t"},   {"USINT", "std::uint8_t"}, {"BYTE", "std::uint8_t"},
            {"INT", "std::int16_t"},    {"UINT", "std::uint16_t"}, {"WORD", "std::uint16_t"}, {"DINT", "std::int32_t"},
            {"UDINT", "std::uint32_t"}, {"DWORD", "std::uint32_t"}, {"LINT", "std::int64_t"}, {"ULINT", "std::uint64_t"},
            {"LWORD", "std::uint64_t"}, {"REAL", "float"},         {"LREAL", "double"},       {"STRING", "std::string"},
            {"WSTRING", "std::wstring"}, {"CHAR", "char"},          {"WCHAR", "wchar_t"},      {"TIME", "std::chrono::milliseconds"},
            {"DATE", "std::chrono::sys_days"}, {"TOD", "std::chrono::milliseconds (depuis minuit)"},
            {"TIME_OF_DAY", "std::chrono::milliseconds (depuis minuit)"}, {"DT", "std::chrono::system_clock::time_point"},
            {"DATE_AND_TIME", "std::chrono::system_clock::time_point"}};
        if (const auto it = kMap.find(x); it != kMap.end()) return it->second;
        if (x.rfind("STRING", 0) == 0) return "std::string";
        return trim(t);
    };
    const auto inner = [&](const std::string& t) {
        const Shape i = shapeOf(p, t);
        return i.kind == Kind::Scalar || i.kind == Kind::Time ? scalar(t) : cppOf(p, t);
    };
    switch (s.kind) {
        case Kind::Map:    return "std::map<" + scalar(s.key) + ", " + inner(s.element) + ">";
        case Kind::List:   return s.container == "VECTOR" ? "std::vector<" + inner(s.element) + ">" : "std::list<" + inner(s.element) + ">   // ou std::vector";
        case Kind::Set:    return "std::set<" + inner(s.element) + ">";
        case Kind::Ring:   return "std::deque<" + inner(s.element) + ">   // born\xC3\xA9" "e \xC3\xA0 " + std::to_string(s.capacity);
        case Kind::Array:  return "std::array<" + inner(s.element) + ", " + std::to_string(s.high - s.low + 1) + ">";
        case Kind::Matrix:
            return "std::array<std::array<" + inner(s.element) + ", " + std::to_string(s.high2 - s.low2 + 1) + ">, " + std::to_string(s.high - s.low + 1) + ">";
        case Kind::Struct: {
            std::string m;
            for (std::size_t k = 0; k < s.fields.size() && k < 2; ++k) m += " " + inner(s.fields[k].type) + " " + s.fields[k].name + ";";
            return "struct " + s.type + " {" + m + (s.fields.size() > 2 ? " \xE2\x80\xA6 };" : " };");
        }
        case Kind::Enum: {
            std::string v;
            for (std::size_t k = 0; k < s.options.size() && k < 3; ++k)
                v += (k ? ", " : " ") + s.options[k].name + " = " + std::to_string(s.options[k].number);
            return "enum class " + s.enumName + " {" + v + (s.options.size() > 3 ? ", \xE2\x80\xA6 };" : " };");
        }
        case Kind::Tuple: {
            std::string v;
            for (const auto& f : s.fields) v += (v.empty() ? "" : ", ") + inner(f.type);
            return "std::tuple<" + v + ">";
        }
        case Kind::Time:   return "std::chrono::milliseconds";
        case Kind::Scalar: return scalar(s.element);
    }
    (void)u;
    return {};
}

namespace {
std::string kindPhrase(const hmi::Project& p, std::string_view type) {
    const Shape s = shapeOf(p, type);
    const std::string u = upper(s.element);
    switch (s.kind) {
        case Kind::Map:    return "un dictionnaire";
        case Kind::List:   return s.container == "VECTOR" ? "un vecteur, dans l'ordre" : "une liste, dans l'ordre";
        case Kind::Set:    return "un ensemble (chaque valeur une seule fois)";
        case Kind::Ring:   return "un tampon : les " + std::to_string(s.capacity) + " derni\xC3\xA8res valeurs";
        case Kind::Array:  return "un tableau de " + std::to_string(s.high - s.low + 1) + " cases, indices " + std::to_string(s.low) + " \xC3\xA0 " + std::to_string(s.high);
        case Kind::Matrix: return "un tableau \xC3\xA0 deux dimensions";
        case Kind::Struct: return "une structure IHM";
        case Kind::Enum:   return "une \xC3\xA9num\xC3\xA9ration IHM";
        case Kind::Time:   return "une dur\xC3\xA9" "e";
        case Kind::Tuple:  return "des valeurs de types fixes, dans l'ordre";
        case Kind::Scalar: break;
    }
    if (u == "BOOL") return "vrai ou faux";
    if (u == "REAL") return "un r\xC3\xA9" "el 32 bits (environ 7 chiffres significatifs)";
    if (u == "LREAL") return "un r\xC3\xA9" "el 64 bits (environ 15 chiffres significatifs)";
    if (textType(u)) return "un texte";
    if (hmi::typereg::isInteger(u)) return "un entier";
    return "une valeur";
}

void treeOf(const hmi::Project& p, std::string_view type, int depth, std::vector<TreeLine>& out, int guard = 0) {
    if (guard > 6) return;
    const Shape s = shapeOf(p, type);
    switch (s.kind) {
        case Kind::Map:
            out.push_back({depth, "MAP", "un dictionnaire"});
            out.push_back({depth + 1, "[" + s.key + "]", "la cl\xC3\xA9 : " + kindPhrase(p, s.key) + ", unique"});
            out.push_back({depth + 1, "OF " + s.element, "la valeur : " + kindPhrase(p, s.element)});
            if (shapeOf(p, s.element).kind != Kind::Scalar) treeOf(p, s.element, depth + 2, out, guard + 1);
            return;
        case Kind::List:
        case Kind::Set:
        case Kind::Ring: {
            const std::string head = s.kind == Kind::Set ? "SET" : s.kind == Kind::Ring ? "RING[" + std::to_string(s.capacity) + "]" : s.container;
            out.push_back({depth, head, kindPhrase(p, type)});
            out.push_back({depth + 1, "OF " + s.element, "chaque \xC3\xA9l\xC3\xA9ment : " + kindPhrase(p, s.element)});
            if (shapeOf(p, s.element).kind != Kind::Scalar) treeOf(p, s.element, depth + 2, out, guard + 1);
            return;
        }
        case Kind::Array:
        case Kind::Matrix: {
            const auto sh = tf::decompose(type);
            out.push_back({depth, "ARRAY[" + sh.parameter + "]", kindPhrase(p, type)});
            out.push_back({depth + 1, "OF " + s.element, "chaque case : " + kindPhrase(p, s.element)});
            if (shapeOf(p, s.element).kind != Kind::Scalar) treeOf(p, s.element, depth + 2, out, guard + 1);
            return;
        }
        case Kind::Struct:
            out.push_back({depth, s.type, "une structure IHM"});
            for (const auto& f : s.fields) {
                out.push_back({depth + 1, "." + f.name, f.type});
                if (shapeOf(p, f.type).kind != Kind::Scalar && shapeOf(p, f.type).kind != Kind::Time) treeOf(p, f.type, depth + 2, out, guard + 1);
            }
            return;
        case Kind::Enum:
            out.push_back({depth, s.enumName, "une \xC3\xA9num\xC3\xA9ration IHM"});
            for (const auto& o : s.options) out.push_back({depth + 1, "#" + o.name, std::to_string(o.number) + " \xC2\xB7 " + o.text});
            return;
        case Kind::Tuple:
            out.push_back({depth, "TUPLE", "des valeurs de types fixes, dans l'ordre"});
            for (const auto& f : s.fields) out.push_back({depth + 1, f.name, f.type});
            return;
        case Kind::Time:
            out.push_back({depth, "TIME", "une dur\xC3\xA9" "e"});
            return;
        case Kind::Scalar:
            out.push_back({depth, s.type, kindPhrase(p, type)});
            return;
    }
}
} // namespace

Explanation explain(const hmi::Project& p, std::string_view name, std::string_view type, std::string_view owner, const std::vector<Name>& readable) {
    Explanation e;
    const Shape s = shapeOf(p, type);
    treeOf(p, type, 0, e.tree);
    const std::string n(name);
    const std::string q = owner.empty() ? n : std::string(owner) + "." + n;
    std::vector<std::string> natives;
    switch (s.kind) {
        case Kind::Map:
            e.plain = "\xC3\x80 chaque cl\xC3\xA9 (" + kindPhrase(p, s.key) + ", unique) sa valeur (" + kindPhrase(p, s.element) +
                      "). Les cl\xC3\xA9s sont rang\xC3\xA9" "es dans l'ordre : l'ordre d'\xC3\xA9" "criture ne compte pas.";
            e.read = n + "[" + (s.textKeys ? std::string("'bas'") : std::string("1")) + "] lit une valeur. Une cl\xC3\xA9 absente arr\xC3\xAAte le script : MAP_HAS(" + n +
                     ", ...) d'abord, ou MAP_GET(" + n + ", ..., d\xC3\xA9" "faut). FOR EACH k, v IN " + n + " les parcourt.";
            natives = {"MAP_HAS", "MAP_GET", "MAP_SET", "MAP_SIZE", "MAP_KEYS", "MAP_VALUES"};
            break;
        case Kind::List:
            e.plain = "Des " + s.element + " dans l'ordre, de 0 \xC3\xA0 Count - 1. Elle grandit et r\xC3\xA9tr\xC3\xA9" "cit en marche." +
                      (s.structElements ? " Un membre laiss\xC3\xA9 vide garde la valeur du type." : "");
            e.read = n + "[0]" + (s.structElements && !s.fields.empty() ? "." + s.fields.front().name : std::string{}) + " ; " + n + ".Count ; " + n +
                     ".First ; FOR EACH x IN " + n + " DO \xE2\x80\xA6 END_FOR.";
            natives = {"LIST_ADD", "LIST_REMOVE_AT", "LIST_FIND_IF", "LIST_SORT", "LIST_COUNT", "LIST_SUM"};
            break;
        case Kind::Set:
            e.plain = "Chaque valeur une seule fois, rang\xC3\xA9" "e : un doublon dispara\xC3\xAEt, l'ordre d'\xC3\xA9" "criture ne compte pas.";
            e.read = "SET_HAS(" + n + ", x) ; " + n + ".Count ; FOR EACH x IN " + n + " (dans l'ordre).";
            natives = {"SET_ADD", "SET_HAS", "SET_REMOVE", "SET_UNION", "SET_INTERSECTION"};
            break;
        case Kind::Ring:
            e.plain = "Au plus " + std::to_string(s.capacity) + " valeurs ; plein, la plus ancienne part quand une nouvelle arrive. \xC3\x89" "crire plus de " +
                      std::to_string(s.capacity) + " valeurs n'en garde que les " + std::to_string(s.capacity) + " derni\xC3\xA8res.";
            e.read = n + ".Count ; " + n + ".Capacity (aussi MaxSize) ; " + n + ".Full ; LIST_AVG(" + n + ").";
            natives = {"RING_ADD", "LIST_AVG", "LIST_MIN", "LIST_MAX"};
            break;
        case Kind::Array:
            e.plain = std::to_string(s.high - s.low + 1) + " cases, toujours " + std::to_string(s.high - s.low + 1) +
                      " : leur nombre ne change pas en marche. Une case laiss\xC3\xA9" "e vide vaut " + zeroOf(p, s.element) + ".";
            e.read = n + "[" + std::to_string(s.low) + "] ; " + n + ".Length (" + std::to_string(s.high - s.low + 1) + ") ; " + n + ".Low (" +
                     std::to_string(s.low) + "), " + n + ".High (" + std::to_string(s.high) + ").";
            natives = {"IOTA", "LIST_SUM", "LIST_AVG", "LOWER_BOUND", "UPPER_BOUND"};
            break;
        case Kind::Matrix:
            e.plain = "Un tableau de " + std::to_string(s.high - s.low + 1) + " lignes et " + std::to_string(s.high2 - s.low2 + 1) +
                      " colonnes : sa taille ne change pas en marche.";
            e.read = n + "[" + std::to_string(s.low) + ", " + std::to_string(s.low2) + "] ; deux boucles FOR, ligne puis colonne.";
            natives = {"LIST_SUM", "LIST_MAX", "LIST_MIN"};
            break;
        case Kind::Struct:
            e.plain = "Une structure : un champ par membre. Un membre laiss\xC3\xA9 vide garde la valeur du type (\xC3\xA9" "crite en gris).";
            e.read = s.fields.empty() ? n : n + "." + s.fields.front().name + " ; une copie : r := " + n + ".";
            natives = {"TO_STRING", "CONCAT"};
            break;
        case Kind::Enum:
            e.plain = "Une valeur parmi " + std::to_string(s.options.size()) + ", \xC3\xA9" "crite par son nom. Son nombre sert pour un \xC3\xA9quipement ; son texte pour l'\xC3\xA9" "cran.";
            e.read = s.options.empty() ? n : "IF " + n + " = " + s.enumName + "#" + s.options.front().name + " THEN \xE2\x80\xA6 ; TO_STRING(" + n + ") donne son texte.";
            natives = {"TO_STRING", "TO_DINT", "SEL"};
            break;
        case Kind::Time:
            e.plain = "Une dur\xC3\xA9" "e, \xC3\xA9" "crite T#\xE2\x80\xA6 : des heures, minutes, secondes et millisecondes. Elle s'additionne \xC3\xA0 une autre dur\xC3\xA9" "e, se compare, se divise.";
            e.read = "IF IHM_TEMPS() - Debut > " + n + " THEN \xE2\x80\xA6 ; TO_DINT(" + n + ") en millisecondes.";
            natives = {"TO_TIME", "TO_DINT", "IHM_TEMPS"};
            break;
        case Kind::Tuple:
            e.plain = std::to_string(s.fields.size()) + " valeurs li\xC3\xA9" "es, chacune son type. Elles se lisent par leur rang : Item1, Item2\xE2\x80\xA6";
            e.read = n + ".Item1 ; " + n + ".Item2 ; TUPLE_TO_STRING(" + n + ").";
            natives = {"TUPLE_GET", "TUPLE_SET", "TUPLE_TO_STRING"};
            break;
        case Kind::Scalar: {
            const std::string u = upper(s.element);
            if (u == "REAL" || u == "LREAL") {
                e.plain = "Un nombre \xC3\xA0 virgule. \xC3\x89" "cris 85.0 (le point d\xC3\xA9" "cimal), ou 8.5E1.";
                natives = {"LIMIT", "MIN", "MAX", "ROUND", "CEIL", "FLOOR"};
            } else if (u == "BOOL") {
                e.plain = "Vrai ou faux : TRUE ou FALSE.";
                natives = {"SEL", "XOR"};
            } else if (textType(u)) {
                e.plain = "Un texte : \xC3\xA9" "cris-le tel quel ici (le litt\xC3\xA9ral l'entoure d'apostrophes).";
                natives = {"CONCAT", "LEN", "LEFT", "FIND", "FORMAT", "TRIM"};
            } else if (hmi::typereg::isInteger(u)) {
                e.plain = "Un entier (" + u + ") : \xC3\xA9" "cris 42, 16#2A ou 2#101010.";
                natives = {"LIMIT", "MIN", "MAX", "ABS"};
            } else {
                e.plain = "Une valeur de type " + s.element + ".";
            }
            e.read = "IF " + n + " > \xE2\x80\xA6 THEN \xE2\x80\xA6 ;" + (owner.empty() ? std::string{} : " ailleurs : " + q + ".");
            break;
        }
    }
    if (!owner.empty() && s.kind != Kind::Scalar) e.read += " Ailleurs : " + q + ".";
    e.cpp = cppOf(p, type);
    for (const auto& nm : natives)
        if (hmi::natives::function(nm) || nm == "IHM_TEMPS") e.natives.push_back(nm);
    std::string names;
    for (std::size_t k = 0; k < readable.size() && k < 3; ++k) names += (names.empty() ? "" : ", ") + readable[k].name;
    e.code = "Une fonction qui rend un " + s.type + ". Elle lit les constantes et les variables d\xC3\xA9j\xC3\xA0 pr\xC3\xAAtes" +
             (names.empty() ? std::string{} : " (" + names + ")") + " ; un mauvais type rendu est dit \xC3\xA0 sa ligne, avant d'appliquer.";
    return e;
}

} // namespace app::initvalue
