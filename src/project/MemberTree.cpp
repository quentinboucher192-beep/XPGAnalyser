// =============================================================================
//  project/MemberTree.cpp - ce qu'une ligne depliee montre dessous (lot API 7)
// =============================================================================
#include "MemberTree.hpp"

#include "BlockLibrary.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>

namespace project::members {

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// Un entier signe, et rien d'autre (une constante nommee n'est pas lue).
bool integer(std::string_view text, std::int64_t& out) {
    const auto t = trim(text);
    if (t.empty()) return false;
    std::size_t i = 0;
    if (t[0] == '-' || t[0] == '+') i = 1;
    if (i >= t.size()) return false;
    for (std::size_t k = i; k < t.size(); ++k)
        if (!std::isdigit(static_cast<unsigned char>(t[k]))) return false;
    out = std::strtoll(t.c_str(), nullptr, 10);
    return true;
}

std::string joinIndices(const std::vector<std::int64_t>& v, const char* sep) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += std::to_string(v[i]);
    }
    return out;
}

const char* const kElement = "\xC3\xA9l\xC3\xA9ment";
const char* const kEllipsis = "\xE2\x80\xA6";                 // ...
const char* const kSpacedDots = " \xE2\x80\xA6 ";             // " ... "

const domain::DerivedType* derivedNamed(const domain::Project& p, std::string_view name) {
    const auto l = lower(trim(name));
    for (const auto& dt : p.derivedTypes)
        if (lower(p.strings.text(dt.name)) == l) return &dt;
    return nullptr;
}

const domain::Pou* dfbNamed(const domain::Project& p, std::string_view name) {
    const auto l = lower(trim(name));
    for (const auto& pou : p.pous)
        if (pou.kind == domain::PouKind::FunctionBlockType && lower(p.strings.text(pou.name)) == l) return &pou;
    return nullptr;
}

const char* scopeWord(domain::VariableScope s) {
    switch (s) {
        case domain::VariableScope::Input:  return "entr\xC3\xA9" "e";
        case domain::VariableScope::Output: return "sortie";
        case domain::VariableScope::InOut:  return "entr\xC3\xA9" "e-sortie";
        case domain::VariableScope::Public: return "publique";
        default:                            return "priv\xC3\xA9" "e";
    }
}

// Une dimension d'un tableau, a partir de `fixed` et sur [lo, hi] : des paquets
// si elle est trop longue, sinon ses indices - une case quand c'est la
// derniere dimension, une ligne a deplier sinon.
std::vector<Node> level(const std::string& path, const std::string& type, const ArrayShape& shape,
                        const std::vector<std::int64_t>& fixed, std::size_t dim, std::int64_t lo, std::int64_t hi) {
    std::vector<Node> out;
    if (dim >= shape.dims.size() || hi < lo) return out;
    const std::int64_t count = hi - lo + 1;
    const std::string head = fixed.empty() ? std::string{} : joinIndices(fixed, ",") + ",";
    const std::string headShown = fixed.empty() ? std::string{} : joinIndices(fixed, ", ") + ", ";
    if (count > kChunk) {
        std::int64_t step = kChunk;
        while (count > step * kChunk && step < (std::int64_t{1} << 50)) step *= kChunk;
        for (std::int64_t a = lo; a <= hi; a += step) {
            const std::int64_t b = std::min(hi, a + step - 1);
            Node g;
            g.path = path;
            g.type = type;
            g.real = false;
            g.fixed = fixed;
            g.dim = dim;
            g.first = a;
            g.last = b;
            g.key = path + "[" + head + std::to_string(a) + ".." + std::to_string(b) + "]";
            g.label = "[" + headShown + std::to_string(a) + kSpacedDots + std::to_string(b) + "]";
            g.what = "paquet";
            out.push_back(std::move(g));
            if (b == hi) break;
        }
        return out;
    }
    const bool lastDim = dim + 1 == shape.dims.size();
    out.reserve(static_cast<std::size_t>(count));
    for (std::int64_t i = lo; i <= hi; ++i) {
        auto idx = fixed;
        idx.push_back(i);
        Node n;
        if (lastDim) {
            n.path = path + indexSuffix(idx);
            n.type = shape.element;
            n.key = n.path;
            n.label = "[" + joinIndices(idx, ", ") + "]";
            n.what = kElement;
        } else {
            n.path = path;
            n.type = type;
            n.real = false;
            n.fixed = idx;
            n.dim = dim + 1;
            n.first = shape.dims[dim + 1].first;
            n.last = shape.dims[dim + 1].second;
            n.key = path + "[" + joinIndices(idx, ",") + ",*]";
            n.label = "[" + joinIndices(idx, ", ") + ", " + kEllipsis + "]";
            n.what = "ligne";
        }
        out.push_back(std::move(n));
    }
    return out;
}

} // namespace

std::int64_t ArrayShape::count() const noexcept {
    std::int64_t n = 1;
    constexpr std::int64_t kCap = 1000000000000000LL;
    for (const auto& [lo, hi] : dims) {
        const std::int64_t d = hi >= lo ? hi - lo + 1 : 0;
        if (d == 0) return 0;
        if (n > kCap / d) return kCap;
        n *= d;
    }
    return dims.empty() ? 0 : n;
}

ArrayShape parseArray(std::string_view type) {
    ArrayShape shape;
    const auto t = trim(type);
    const auto u = upper(t);
    if (u.rfind("ARRAY", 0) != 0) return shape;
    const auto lb = u.find('[');
    const auto rb = u.find(']', lb == std::string::npos ? 0 : lb);
    if (lb == std::string::npos || rb == std::string::npos) return shape;
    const auto of = u.find(" OF ", rb);
    if (of == std::string::npos) return shape;
    const std::string inside = t.substr(lb + 1, rb - lb - 1);
    std::size_t from = 0;
    while (from <= inside.size()) {
        const auto comma = inside.find(',', from);
        const auto part = inside.substr(from, (comma == std::string::npos ? inside.size() : comma) - from);
        const auto dots = part.find("..");
        std::int64_t lo = 0, hi = 0;
        if (dots == std::string::npos || !integer(part.substr(0, dots), lo) || !integer(part.substr(dots + 2), hi)) return ArrayShape{};
        shape.dims.emplace_back(lo, hi);
        if (comma == std::string::npos) break;
        from = comma + 1;
    }
    shape.element = trim(t.substr(of + 4));
    if (shape.element.empty()) return ArrayShape{};
    return shape;
}

std::string indexSuffix(const std::vector<std::int64_t>& indices) {
    return "[" + joinIndices(indices, ",") + "]";
}

bool isElementary(std::string_view type) {
    static const char* const kTypes[] = {"BOOL", "EBOOL", "INT", "UINT", "DINT", "UDINT", "WORD", "DWORD", "BYTE", "REAL",
                                         "LREAL", "TIME", "DATE", "TOD", "DT", "SINT", "USINT", "LINT", "ULINT", "LWORD",
                                         "TIME_OF_DAY", "DATE_AND_TIME", "ANY"};
    const auto u = upper(trim(type));
    if (u.empty()) return true;
    if (u.rfind("STRING", 0) == 0 || u.rfind("WSTRING", 0) == 0) return true;
    return std::any_of(std::begin(kTypes), std::end(kTypes), [&](const char* k) { return u == k; });
}

Node root(std::string path, std::string type) {
    Node n;
    n.key = path;
    n.label = path;
    n.path = std::move(path);
    n.type = std::move(type);
    return n;
}

std::string groupType(const Node& n) {
    if (n.real) return n.type;
    if (n.what == "paquet") return "paquet de " + std::to_string(n.size());
    const auto shape = parseArray(n.type);
    if (!shape.valid() || n.dim >= shape.dims.size()) return n.type;
    std::string dims;
    for (std::size_t d = n.dim; d < shape.dims.size(); ++d) {
        if (!dims.empty()) dims += ", ";
        dims += std::to_string(shape.dims[d].first) + ".." + std::to_string(shape.dims[d].second);
    }
    return "ARRAY[" + dims + "] OF " + shape.element;
}

std::vector<Node> children(const domain::Project& p, const Node& n) {
    std::vector<Node> out;
    if (!n.real) {
        const auto shape = parseArray(n.type);
        return level(n.path, n.type, shape, n.fixed, n.dim, n.first, n.last);
    }
    if (isElementary(n.type)) return out;
    if (const auto shape = parseArray(n.type); shape.valid())
        return level(n.path, n.type, shape, {}, 0, shape.dims[0].first, shape.dims[0].second);

    const auto member = [&](std::string_view name, std::string type, const char* what) {
        Node c;
        c.path = n.path + "." + std::string(name);
        c.key = c.path;
        c.label = "." + std::string(name);
        c.type = std::move(type);
        c.what = what;
        out.push_back(std::move(c));
    };
    if (const auto* dt = derivedNamed(p, n.type)) {
        for (const auto f : dt->fields)
            if (f < p.variables.size())
                member(p.strings.text(p.variables[f].name), std::string(p.strings.text(p.variables[f].type.name)), "champ");
        return out;
    }
    if (const auto* pou = dfbNamed(p, n.type)) {
        for (const auto v : pou->parameters)
            if (v < p.variables.size())
                member(p.strings.text(p.variables[v].name), std::string(p.strings.text(p.variables[v].type.name)), scopeWord(p.variables[v].scope));
        // Les publiques d'abord (on les lit de dehors), puis les privees.
        for (int pass = 0; pass < 2; ++pass)
            for (const auto v : pou->locals) {
                if (v >= p.variables.size()) continue;
                const bool isPublic = p.variables[v].scope == domain::VariableScope::Public;
                if ((pass == 0) != isPublic) continue;
                member(p.strings.text(p.variables[v].name), std::string(p.strings.text(p.variables[v].type.name)), scopeWord(p.variables[v].scope));
            }
        return out;
    }
    if (const auto* block = BlockLibrary::shared().find(trim(n.type))) {
        for (const auto& pin : block->parameters)
            member(pin.name, pin.type,
                   pin.direction == BlockParameter::Direction::In    ? "entr\xC3\xA9" "e"
                   : pin.direction == BlockParameter::Direction::Out ? "sortie"
                                                                     : "entr\xC3\xA9" "e-sortie");
    }
    return out;
}

bool hasChildren(const domain::Project& p, const Node& n) {
    if (!n.real) return n.size() > 0;
    if (isElementary(n.type)) return false;
    if (const auto shape = parseArray(n.type); shape.valid()) return shape.count() > 0;
    if (const auto* dt = derivedNamed(p, n.type)) return !dt->fields.empty();
    if (const auto* pou = dfbNamed(p, n.type)) return !pou->parameters.empty() || !pou->locals.empty();
    if (const auto* block = BlockLibrary::shared().find(trim(n.type))) return !block->parameters.empty();
    return false;
}

// ---- lot API 7 (l'arbre, l'onglet Variables) : ce qu'une ligne montre --------
Nature natureOf(const domain::Project& p, std::string_view type) {
    if (isElementary(type)) return Nature::Elementary;
    if (parseArray(type).valid()) return Nature::Array;
    if (derivedNamed(p, type)) return Nature::Structure;
    if (dfbNamed(p, type) || BlockLibrary::shared().find(trim(type))) return Nature::Block;
    return Nature::Unknown;
}

domain::Index declarationOf(const domain::Project& p, const Node& parent, domain::Index parentDecl, const Node& child) {
    // Un element, un paquet, une ligne (leur libelle commence par "[") : ils
    // vivent dans le tableau, que la declaration du parent decrit deja.
    if (child.label.empty() || child.label.front() != '.') return parentDecl;
    const auto name = lower(std::string_view(child.label).substr(1));
    const auto named = [&](domain::Index v) {
        return v < p.variables.size() && lower(p.strings.text(p.variables[v].name)) == name;
    };
    if (const auto* dt = derivedNamed(p, parent.type)) {
        for (const auto f : dt->fields)
            if (named(f)) return f;
        return domain::kNoIndex;
    }
    if (const auto* pou = dfbNamed(p, parent.type)) {
        for (const auto v : pou->parameters)
            if (named(v)) return v;
        for (const auto v : pou->locals)
            if (named(v)) return v;
    }
    return domain::kNoIndex;           // une broche d'un bloc de la bibliotheque
}

std::string commentOf(const domain::Project& p, domain::Index decl, const Node& parent, const Node& child) {
    if (!child.real || decl >= p.variables.size()) return {};
    const auto& v = p.variables[decl];
    if (!child.label.empty() && child.label.front() == '.') return std::string(p.strings.text(v.comment));
    // Un element : ce que la declaration du tableau dit de lui, sous le nom que
    // Control Expert lui donne ("[2]", "[2,5]") - le suffixe de son chemin. Le
    // parent est le tableau, ou le paquet / la ligne qui le range (meme chemin).
    if (child.path.size() <= parent.path.size() || child.path.compare(0, parent.path.size(), parent.path) != 0) return {};
    const auto suffix = std::string_view(child.path).substr(parent.path.size());
    for (const auto& e : v.instanceElements)
        if (e.name == suffix) return std::string(p.strings.text(e.comment));
    return {};
}

} // namespace project::members
