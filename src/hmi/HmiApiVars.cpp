// =============================================================================
//  hmi/HmiApiVars.cpp - 1.11.1 : les variables de l'automate sous `API.`
// -----------------------------------------------------------------------------
//  L'acces vient du plan d'adressage de hmi::comm (buildPlan) : le meme que la
//  liaison Modbus utilise. Les membres et les types viennent de
//  project::members (l'arbre de l'onglet Variables), les tailles de domain.
// =============================================================================
#include "HmiApiVars.hpp"

#include "HmiComm.hpp"
#include "HmiModel.hpp"
#include "HmiObjectAlarms.hpp"
#include "../domain/ProjectModel.hpp"
#include "../project/MemberTree.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>

namespace hmi::apivars {

namespace {

using domain::Index;
using domain::kNoIndex;
using domain::VariableScope;

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

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

bool startsWithNoCase(std::string_view text, std::string_view prefix) {
    if (prefix.size() > text.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(text[i])) != std::toupper(static_cast<unsigned char>(prefix[i]))) return false;
    return true;
}

bool integer(std::string_view text, std::int64_t& out) {
    const auto t = trim(text);
    if (t.empty()) return false;
    std::size_t i = (t[0] == '-' || t[0] == '+') ? 1 : 0;
    if (i >= t.size()) return false;
    for (std::size_t k = i; k < t.size(); ++k)
        if (!std::isdigit(static_cast<unsigned char>(t[k]))) return false;
    out = std::strtoll(t.c_str(), nullptr, 10);
    return true;
}

// La distance d'edition, sans casse.
std::size_t distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diag = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t up = row[j];
            const bool same = std::toupper(static_cast<unsigned char>(a[i - 1])) == std::toupper(static_cast<unsigned char>(b[j - 1]));
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (same ? 0 : 1)});
            diag = up;
        }
    }
    return row[b.size()];
}

// Le nom le plus proche : d'abord un nom qui commence par celui-ci (Etat ->
// Etat_Armoire), puis un nom qui le contient, puis une faute de frappe.
std::string closestOf(std::string_view name, const std::vector<std::string>& pool) {
    if (name.empty() || pool.empty()) return {};
    const std::string u = upper(name);
    std::string best;
    for (const auto& c : pool)
        if (startsWithNoCase(c, name) && (best.empty() || c.size() < best.size())) best = c;
    if (!best.empty()) return best;
    for (const auto& c : pool)
        if (u.size() >= 3 && upper(c).find(u) != std::string::npos && (best.empty() || c.size() < best.size())) best = c;
    if (!best.empty()) return best;
    std::size_t bestD = std::max<std::size_t>(1, name.size() / 3) + 1;
    for (const auto& c : pool) {
        const auto d = distance(name, c);
        if (d < bestD) {
            bestD = d;
            best = c;
        }
    }
    return best;
}

// Un morceau de chemin : un nom (.Membre) ou des indices ([3], [i, 2]).
struct Seg {
    bool                     index{false};
    std::string              member;
    std::vector<std::string> idx;
};

// "Armoires[0].ana.PT1.mes" -> Armoires, [0], ana, PT1, mes. Faux : illisible.
bool parsePath(std::string_view s, std::vector<Seg>& out) {
    out.clear();
    std::size_t i = 0;
    const auto skip = [&] {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    };
    const auto ident = [&](std::string& into) {
        skip();
        const std::size_t a = i;
        while (i < s.size() && identChar(s[i])) ++i;
        into = std::string(s.substr(a, i - a));
        return !into.empty();
    };
    Seg first;
    if (!ident(first.member)) return false;
    out.push_back(std::move(first));
    for (;;) {
        skip();
        if (i >= s.size()) return true;
        if (s[i] == '.') {
            ++i;
            Seg m;
            if (!ident(m.member)) return false;
            out.push_back(std::move(m));
            continue;
        }
        if (s[i] == '[') {
            ++i;
            Seg g;
            g.index = true;
            int depth = 0;
            std::string cur;
            while (i < s.size()) {
                const char c = s[i];
                if (c == '[' || c == '(') ++depth;
                if ((c == ']' || c == ')') && depth > 0) {
                    --depth;
                    cur += c;
                    ++i;
                    continue;
                }
                if (c == ']' && depth == 0) break;
                if (c == ',' && depth == 0) {
                    g.idx.push_back(trim(cur));
                    cur.clear();
                    ++i;
                    continue;
                }
                cur += c;
                ++i;
            }
            if (i >= s.size()) return false;
            ++i;   // ]
            g.idx.push_back(trim(cur));
            out.push_back(std::move(g));
            continue;
        }
        return false;
    }
}

std::string joinIdx(const std::vector<std::int64_t>& v, const char* sep) {
    std::string out;
    for (std::size_t k = 0; k < v.size(); ++k) out += (k ? sep : "") + std::to_string(v[k]);
    return out;
}

Scope scopeOf(VariableScope s) {
    switch (s) {
        case VariableScope::Global:   return Scope::Global;
        case VariableScope::Constant: return Scope::Constant;
        case VariableScope::Public:   return Scope::Public;
        case VariableScope::Local:    return Scope::Private;
        case VariableScope::Input:    return Scope::Input;
        case VariableScope::Output:   return Scope::Output;
        case VariableScope::InOut:    return Scope::InOut;
        case VariableScope::DerivedMember: return Scope::Field;
    }
    return Scope::Global;
}

// Ce que project::members dit d'un membre ("champ", "entree", "publique"...).
Scope scopeOfWhat(std::string_view what) {
    if (what == "champ") return Scope::Field;
    if (what == "sortie") return Scope::Output;
    if (what == "publique") return Scope::Public;
    if (what == "entr\xC3\xA9" "e") return Scope::Input;
    if (what == "entr\xC3\xA9" "e-sortie") return Scope::InOut;
    if (what == "priv\xC3\xA9" "e") return Scope::Private;
    return Scope::Element;
}

// D18 (decision 129, le texte d'API-M) : le remede Simulateur d'abord, mot pour
// mot celui de la marque de la liaison (kRemedy, HmiRuntimeLot14.cpp, API-M 9e1a4e1).
const char* const kHowTo = " \xE2\x80\x94 pour la voir calcul\xC3\xA9" "e : passe la Communication en Simulateur ; pour la lire sur l'automate : "
                           "donne-lui une adresse dans le programme de l'automate (Control Expert, puis Fichier > Importer) "
                           "ou une ligne dans Configuration > Communication";

} // namespace

// ------------------------------------------------------------- le prefixe ---
bool isApiPath(std::string_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (text.size() - i < 3 || !startsWithNoCase(text.substr(i), kRoot)) return false;
    i += 3;
    if (i < text.size() && identChar(text[i])) return false;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    return i < text.size() && text[i] == '.';
}

std::string stripApi(std::string_view text) {
    if (!isApiPath(text)) return std::string(text);
    const auto dot = text.find('.');
    std::size_t i = dot + 1;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    return std::string(text.substr(i));
}

std::string withApi(std::string_view plcName) {
    if (isApiPath(plcName)) return std::string(plcName);
    return std::string(kRoot) + "." + trim(plcName);
}

std::string withoutAddressAdvice(std::string_view why) {
    std::string out(why);
    static constexpr std::string_view kAdvice = " : donne-lui une adresse (";
    const auto at = out.find(kAdvice);
    if (at == std::string::npos) return out;
    const auto close = out.find(')', at + kAdvice.size());
    out.erase(at, close == std::string::npos ? std::string::npos : close + 1 - at);
    return out;
}

std::string_view accessLabel(Access a) noexcept {
    switch (a) {
        case Access::ReadWrite: return "Lecture / \xC3\xA9" "criture";
        case Access::ReadOnly:  return "Lecture seule";
        case Access::NoAddress: return "Sans adresse \xE2\x80\x94 simulation seulement";
    }
    return {};
}

std::string_view accessShort(Access a) noexcept {
    switch (a) {
        case Access::ReadWrite: return "L/\xC3\x89";
        case Access::ReadOnly:  return "L";
        case Access::NoAddress: return "sans adresse";
    }
    return {};
}

std::string_view scopeLabel(Scope s) noexcept {
    switch (s) {
        case Scope::Global:   return "Globale";
        case Scope::Constant: return "Constante";
        case Scope::Public:   return "Publique";
        case Scope::Private:  return "Priv\xC3\xA9" "e";
        case Scope::Input:    return "Entr\xC3\xA9" "e";
        case Scope::Output:   return "Sortie";
        case Scope::InOut:    return "Entr\xC3\xA9" "e / sortie";
        case Scope::Field:    return "Champ";
        case Scope::Pin:      return "Broche";
        case Scope::Element:  return "\xC3\x89" "l\xC3\xA9" "ment";
    }
    return {};
}

std::string_view groupLabel(GroupKind g) noexcept {
    switch (g) {
        case GroupKind::Globals: return "Globales";
        case GroupKind::Public:  return "Publiques";
        case GroupKind::Private: return "Priv\xC3\xA9" "es";
        case GroupKind::InOut:   return "E/S";
        default:                 return {};
    }
}

// ---------------------------------------------------------------- le modele ---
struct Model::Data {
    std::shared_ptr<const domain::Project> plc;
    std::vector<Node> roots;
    std::map<std::string, Index, std::less<>> units;        // NOM -> pou
    std::vector<Index> unitOrder;
    std::map<std::string, Index, std::less<>> globals;      // NOM -> variable
    std::vector<Index> globalOrder;
    std::map<Index, std::map<std::string, Index, std::less<>>> unitVars;   // pou -> NOM -> variable
    comm::Plan plan;
    bool writes{true};
    bool modbus{false};
    std::optional<Communication> link;   // la liaison du projet IHM du plan (vide : sans projet IHM)
    std::map<std::string, std::vector<Use>, std::less<>> uses;   // keyOf(nom de l'automate) -> emplois

    [[nodiscard]] std::string text(domain::SymbolId id) const { return std::string(plc->strings.text(id)); }

    [[nodiscard]] std::uint32_t usesCount(std::string_view plcName) const {
        const std::string k = comm::keyOf(plcName);
        if (k.empty()) return 0;
        std::uint32_t n = 0;
        for (auto it = uses.lower_bound(k); it != uses.end(); ++it) {
            const auto& key = it->first;
            if (key.compare(0, k.size(), k) != 0) break;
            if (key.size() == k.size() || key[k.size()] == '.' || key[k.size()] == '[') n += static_cast<std::uint32_t>(it->second.size());
        }
        return n;
    }

    Access access(std::string_view plcName, std::string* why, std::string* reference) const {
        std::optional<comm::Point> pt = plan.resolve(plcName);
        if (!pt) {
            const auto k = comm::keyOf(plcName);
            for (const auto& p : plan.points())
                if (p.array && comm::keyOf(p.name) == k) {
                    pt = p;
                    break;
                }
        }
        if (pt) {
            if (reference) *reference = pt->address;
            if (!writes) {
                if (why) *why = pt->address + " : la liaison est en lecture seule (Configuration > Communication) ; l'IHM la lit, elle ne l'\xC3\xA9" "crit pas";
                return Access::ReadOnly;
            }
            if (!pt->writable) {
                if (why) {
                    if (pt->area == comm::Area::DiscreteInputs || pt->area == comm::Area::InputRegisters)
                        *why = pt->address + " : une entr\xC3\xA9" "e, elle se lit seulement (" + pt->modbusText() + ")";
                    else
                        *why = pt->address + " : la table des adresses la met en lecture seule (Configuration > Communication)";
                }
                return Access::ReadOnly;
            }
            if (why) {
                if (pt->origin == "table")
                    *why = "la table des adresses lui donne " + pt->address + " : l'IHM la lit et l'\xC3\xA9" "crit (" + pt->modbusText() + ")";
                else
                    *why = (pt->origin == "programme" ? "localis\xC3\xA9" "e en " : "") + pt->address + " : l'IHM la lit et l'\xC3\xA9" "crit (" + pt->modbusText() + ")";
            }
            return Access::ReadWrite;
        }
        if (why) {
            std::string w = plan.whyNot(plcName);
            if (w.empty()) {
                const std::string name = trim(plcName);
                const auto cut = name.find_first_of(".[");
                const std::string root = name.substr(0, cut);
                if (cut != std::string::npos && name[cut] == '.' && units.count(upper(root)))
                    w = "variable de l'unit\xC3\xA9 " + root + " : sur l'automate, l'IHM ne lit que les globales localis\xC3\xA9" "es et la table des adresses";
                else if (cut != std::string::npos)
                    w = "pas de place pour ce membre (ni adresse, ni ligne de la table des adresses)";
                else
                    w = "non localis\xC3\xA9" "e";
            }
            *why = withoutAddressAdvice(w) + kHowTo;   // 1.11.2 (R1111-15) : l'adresse, une fois (kHowTo)
        }
        return Access::NoAddress;
    }

    // La taille d'un type ecrit (un membre, un element), en bits ; 0 : inconnue.
    [[nodiscard]] std::uint64_t bitsOf(std::string_view type) const {
        const std::string t = trim(type);
        if (const auto b = domain::elementaryBits(t); b > 0) return b;
        if (const auto shape = project::members::parseArray(t); shape.valid()) {
            const auto each = bitsOf(shape.element);
            const auto n = shape.count();
            return each && n > 0 ? each * static_cast<std::uint64_t>(n) : 0;
        }
        const std::string u = upper(t);
        for (Index i = 0; i < plc->derivedTypes.size(); ++i)
            if (upper(plc->strings.text(plc->derivedTypes[i].name)) == u) return domain::derivedTypeSizeInBits(*plc, i);
        return 0;
    }

    // Un noeud reel : ses colonnes.
    void fill(Node& n) const {
        n.access = access(n.plcName, &n.accessWhy, &n.reference);
        n.uses = usesCount(n.plcName);
        n.children = project::members::hasChildren(*plc, project::members::root(n.plcName, n.type));
    }

    Node variable(Index vi, const std::string& unit) const {
        const auto& v = plc->variables[vi];
        Node n;
        n.kind = NodeKind::Variable;
        n.name = text(v.name);
        n.plcName = unit.empty() ? n.name : unit + "." + n.name;
        n.path = std::string(kRoot) + "." + n.plcName;
        n.key = n.path;
        n.type = trim(text(v.type.name));
        n.scope = scopeOf(v.scope);
        n.unit = unit;
        n.scopeText = std::string(scopeLabel(n.scope)) + (unit.empty() ? std::string{} : " \xC2\xB7 " + unit);
        n.constant = v.scope == VariableScope::Constant;
        n.comment = text(v.comment);
        n.bits = domain::typeSizeInBits(*plc, v.type);
        n.decl = vi;
        fill(n);
        if (n.reference.empty() && v.located) n.reference = v.address.raw;
        return n;
    }

    // Les elements [from, to] (rangs a plat, en ordre de lignes) d'un tableau.
    void elements(const Node& array, const project::members::ArrayShape& shape, std::int64_t from, std::int64_t to,
                  std::vector<Node>& out) const {
        const std::int64_t total = shape.count();
        const std::int64_t stop = std::min(to, total - 1);
        for (std::int64_t k = from; k <= stop; ++k) {
            std::vector<std::int64_t> idx(shape.dims.size());
            std::int64_t rest = k;
            for (std::size_t d = shape.dims.size(); d-- > 0;) {
                const std::int64_t len = shape.dims[d].second - shape.dims[d].first + 1;
                idx[d] = shape.dims[d].first + rest % len;
                rest /= len;
            }
            Node e;
            e.kind = NodeKind::Element;
            e.name = "[" + joinIdx(idx, ", ") + "]";
            e.plcName = array.plcName + "[" + joinIdx(idx, ",") + "]";
            e.path = std::string(kRoot) + "." + e.plcName;
            e.key = e.path;
            e.type = shape.element;
            e.scope = Scope::Element;
            e.scopeText = std::string(scopeLabel(Scope::Element));
            e.unit = array.unit;
            e.constant = array.constant;
            e.bits = bitsOf(shape.element);
            e.decl = array.decl;
            if (array.decl < plc->variables.size()) {
                const std::string suffix = "[" + joinIdx(idx, ",") + "]";
                for (const auto& ie : plc->variables[array.decl].instanceElements)
                    if (ie.name == suffix) e.comment = text(ie.comment);
            }
            fill(e);
            out.push_back(std::move(e));
        }
        if (stop < total - 1) {
            Node more;
            more.kind = NodeKind::More;
            more.first = stop + 1;
            more.last = total - 1;
            more.count = static_cast<std::size_t>(more.last - more.first + 1);
            more.name = "\xE2\x80\xA6 et " + std::to_string(more.count) + " autre" + (more.count > 1 ? "s" : "");
            more.plcName = array.plcName;
            more.type = array.type;
            more.unit = array.unit;
            more.constant = array.constant;
            more.decl = array.decl;
            more.key = array.key + "#plus" + std::to_string(more.first);
            more.children = true;
            out.push_back(std::move(more));
        }
    }

    std::vector<Node> members(const Node& n) const {
        std::vector<Node> out;
        if (const auto shape = project::members::parseArray(n.type); shape.valid()) {
            elements(n, shape, 0, kFirstElements - 1, out);
            return out;
        }
        const auto parent = project::members::root(n.plcName, n.type);
        for (const auto& c : project::members::children(*plc, parent)) {
            if (!c.real) continue;
            Node m;
            m.kind = NodeKind::Member;
            m.name = c.label.size() > 1 && c.label.front() == '.' ? c.label.substr(1) : c.label;
            m.plcName = c.path;
            m.path = std::string(kRoot) + "." + m.plcName;
            m.key = m.path;
            m.type = trim(c.type);
            m.scope = scopeOfWhat(c.what);
            const auto decl = project::members::declarationOf(*plc, parent, n.decl, c);
            if (decl == kNoIndex && m.scope != Scope::Field) m.scope = m.scope == Scope::Element ? Scope::Element : Scope::Pin;
            m.scopeText = std::string(scopeLabel(m.scope));
            m.unit = n.unit;
            m.constant = n.constant;
            m.decl = decl;
            m.comment = project::members::commentOf(*plc, decl, parent, c);
            m.bits = bitsOf(m.type);
            fill(m);
            out.push_back(std::move(m));
        }
        return out;
    }
};

Model::Model() = default;
Model::~Model() = default;
Model::Model(const Model&) = default;
Model& Model::operator=(const Model&) = default;
Model::Model(Model&&) noexcept = default;
Model& Model::operator=(Model&&) noexcept = default;

namespace {

// Les emplois : chaque chemin de l'automate que le projet IHM ecrit (API.… ou nu).
void collectUses(const Project& p, const std::function<bool(std::string_view)>& isGlobal,
                 std::map<std::string, std::vector<Use>, std::less<>>& uses) {
    const auto take = [&](std::string_view source, Use where) {
        if (source.empty()) return;
        for (const auto& path : comm::plcPaths(source)) {
            std::string name;
            if (isApiPath(path)) name = stripApi(path);
            else {
                const auto root = std::string_view(path).substr(0, path.find_first_of(".["));
                if (!isGlobal(root)) continue;
                name = path;
            }
            const auto key = comm::keyOf(name);
            if (key.empty()) continue;
            Use u = where;
            u.written = path;
            uses[key].push_back(std::move(u));
        }
    };
    const auto takeCode = [&](std::string_view code, Use where) {
        int line = 1;
        std::size_t from = 0;
        while (from <= code.size()) {
            const auto nl = code.find('\n', from);
            const auto text = code.substr(from, (nl == std::string_view::npos ? code.size() : nl) - from);
            Use u = where;
            u.line = line;
            u.where = where.where + ", ligne " + std::to_string(line);
            take(text, u);
            if (nl == std::string_view::npos) break;
            from = nl + 1;
            ++line;
        }
    };
    const auto takeActions = [&](const std::vector<Action>& actions, const Use& base) {
        for (const auto& a : actions) {
            Use u = base;
            u.where = base.where + " (action)";
            take(a.watch, u);
            take(a.guard, u);
            take(a.target, u);
            if (a.operation == Operation::RunScript) takeCode(a.value, u);
            else take(a.value, u);
        }
    };
    for (const auto& v : p.views) {
        for (const auto& o : v.objects) {
            Use base;
            base.view = v.id;
            base.object = o.id;
            for (const auto& pr : o.props) {
                Use u = base;
                u.property = pr.key;
                u.where = v.name + "/" + o.name + "." + pr.key;
                if (!pr.expr.empty()) take(pr.expr, u);
                else if (pr.value.find('{') != std::string::npos) take(pr.value, u);
                else if ((pr.key == "variable" || pr.key == "xVariable" || pr.key == "feedback") && !pr.value.empty()) take(pr.value, u);
            }
            base.where = v.name + "/" + o.name;
            takeActions(o.actions, base);
        }
        Use vb;
        vb.view = v.id;
        vb.where = v.name;
        takeActions(v.actions, vb);
        for (const auto& sc : v.scripts) {
            Use u;
            u.view = v.id;
            u.script = sc.id;
            u.property = sc.name;
            u.where = "script " + v.name + "/" + sc.name;
            takeCode(sc.body, u);
        }
    }
    for (const auto& sc : p.programs.scripts) {
        Use u;
        u.script = sc.id;
        u.property = sc.name;
        u.where = "script " + sc.name;
        takeCode(sc.body, u);
    }
    for (const auto& f : p.programs.functions) {
        Use u;
        u.script = f.id;
        u.property = f.name;
        u.where = "fonction " + f.name;
        takeCode(f.body, u);
    }
    for (const auto& a : p.alarms) {
        Use u;
        u.item = a.id;
        u.where = "alarme " + a.name;
        take(a.condition, u);
        take(a.message, u);
    }
    for (const auto& r : p.recipes)
        for (const auto& f : r.fields) {
            Use u;
            u.item = r.id;
            u.where = "recette " + r.name + "/" + f.name;
            take(f.variable, u);
        }
    for (const auto& e : p.history.archived) {
        Use u;
        u.where = "historique (archiv\xC3\xA9" "e)";
        take(e, u);
    }
    for (const auto& usr : p.security.users)
        if (!usr.expression.empty()) {
            Use u;
            u.item = usr.id;
            u.where = "utilisateur " + usr.login;
            take(usr.expression, u);
        }
}

} // namespace

Model Model::build(std::shared_ptr<const domain::Project> plc, const Project* hmi, const BuildOptions& options) {
    Model m;
    auto d = std::make_shared<Data>();
    d->plc = std::move(plc);
    const Communication link = hmi ? hmi->comm : Communication{};
    d->writes = link.writes;
    d->modbus = link.modbus();
    if (hmi) d->link = link;
    if (!d->plc) {
        m.d_ = std::move(d);
        return m;
    }
    const auto& p = *d->plc;
    d->plan = comm::buildPlan(&p, link);
    for (Index i = 0; i < p.pous.size(); ++i)
        if (p.pous[i].kind == domain::PouKind::ProgramUnit) {
            d->units.emplace(upper(p.strings.text(p.pous[i].name)), i);
            d->unitOrder.push_back(i);
        }
    for (Index i = 0; i < p.variables.size(); ++i) {
        const auto& v = p.variables[i];
        if (v.scope == VariableScope::Global || v.scope == VariableScope::Constant) {
            d->globals.emplace(upper(p.strings.text(v.name)), i);
            d->globalOrder.push_back(i);
            continue;
        }
        if (v.scope == VariableScope::DerivedMember || v.owner >= p.pous.size()) continue;
        if (p.pous[v.owner].kind != domain::PouKind::ProgramUnit) continue;
        d->unitVars[v.owner].emplace(upper(p.strings.text(v.name)), i);
    }
    if (hmi && options.uses) {
        const auto* data = d.get();
        collectUses(*hmi, [data](std::string_view root) { return data->globals.count(upper(root)) != 0; }, d->uses);
    }
    // Les racines : "Globales", puis chaque unite.
    if (!d->globalOrder.empty()) {
        Node g;
        g.kind = NodeKind::Group;
        g.group = GroupKind::Globals;
        g.key = "#globales";
        g.name = std::string(groupLabel(GroupKind::Globals));
        g.count = d->globalOrder.size();
        g.children = true;
        d->roots.push_back(std::move(g));
    }
    for (const auto u : d->unitOrder) {
        Node g;
        g.kind = NodeKind::Group;
        g.group = GroupKind::Unit;
        g.name = std::string(p.strings.text(p.pous[u].name));
        g.unit = g.name;
        g.key = "#unite:" + g.name;
        g.path = std::string(kRoot) + "." + g.name;
        g.plcName = g.name;
        g.scopeText = "Unit\xC3\xA9 de programme";
        g.decl = u;
        const auto it = d->unitVars.find(u);
        g.count = it == d->unitVars.end() ? 0 : it->second.size();
        g.children = g.count > 0;
        g.uses = d->usesCount(g.name);
        d->roots.push_back(std::move(g));
    }
    m.d_ = std::move(d);
    return m;
}

Model Model::build(const domain::Project& plc, const Project* hmi, const BuildOptions& options) {
    auto copy = std::make_shared<domain::Project>();
    copy->header = plc.header;
    copy->hardware = plc.hardware;
    copy->strings = plc.strings;
    copy->variables = plc.variables;
    copy->derivedTypes = plc.derivedTypes;
    copy->pous = plc.pous;
    copy->libraries = plc.libraries;
    copy->buildIndices();
    return build(std::shared_ptr<const domain::Project>(std::move(copy)), hmi, options);
}

Model Model::withLink(const Project& hmi) const {
    if (!d_ || !d_->plc || (d_->link && *d_->link == hmi.comm)) return *this;
    BuildOptions o;
    o.uses = false;
    return build(d_->plc, &hmi, o);
}

bool Model::empty() const noexcept { return !d_ || !d_->plc; }
bool Model::realPlc() const noexcept { return d_ && d_->modbus; }

const std::vector<Node>& Model::roots() const noexcept {
    static const std::vector<Node> kNone;
    return d_ ? d_->roots : kNone;
}

std::vector<Node> Model::children(const Node& n) const {
    std::vector<Node> out;
    if (empty()) return out;
    const auto& d = *d_;
    const auto& p = *d.plc;
    switch (n.kind) {
        case NodeKind::Group: {
            if (n.group == GroupKind::Globals) {
                for (const auto vi : d.globalOrder) out.push_back(d.variable(vi, {}));
                return out;
            }
            const Index u = n.decl;
            if (u >= p.pous.size()) return out;
            const std::string unit(p.strings.text(p.pous[u].name));
            const auto scopes = [&](GroupKind g, VariableScope s) {
                switch (g) {
                    case GroupKind::Public:  return s == VariableScope::Public;
                    case GroupKind::Private: return s == VariableScope::Local;
                    case GroupKind::InOut:   return s == VariableScope::Input || s == VariableScope::Output || s == VariableScope::InOut;
                    default:                 return false;
                }
            };
            std::vector<Index> vars;
            for (const auto vi : p.pous[u].parameters) vars.push_back(vi);
            for (const auto vi : p.pous[u].locals) vars.push_back(vi);
            if (n.group == GroupKind::Unit) {
                for (const auto g : {GroupKind::Public, GroupKind::Private, GroupKind::InOut}) {
                    std::size_t count = 0;
                    for (const auto vi : vars)
                        if (vi < p.variables.size() && scopes(g, p.variables[vi].scope)) ++count;
                    if (!count) continue;
                    Node s;
                    s.kind = NodeKind::Group;
                    s.group = g;
                    s.name = std::string(groupLabel(g));
                    s.unit = unit;
                    s.key = n.key + (g == GroupKind::Public ? "#pub" : g == GroupKind::Private ? "#priv" : "#es");
                    s.decl = u;
                    s.count = count;
                    s.children = true;
                    out.push_back(std::move(s));
                }
                return out;
            }
            for (const auto vi : vars)
                if (vi < p.variables.size() && scopes(n.group, p.variables[vi].scope)) out.push_back(d.variable(vi, unit));
            return out;
        }
        case NodeKind::More: {
            const auto shape = project::members::parseArray(n.type);
            if (!shape.valid()) return out;
            Node array;
            array.plcName = n.plcName;
            array.key = n.key.substr(0, n.key.rfind("#plus"));
            array.unit = n.unit;
            array.constant = n.constant;
            array.decl = n.decl;
            array.type = n.type;
            d.elements(array, shape, n.first, n.first + kMoreBatch - 1, out);
            return out;
        }
        default:
            if (!n.children) return out;
            return d.members(n);
    }
}

bool Model::node(std::string_view path, Node& out) const {
    const auto r = resolve(path);
    if (!r.ok || empty()) return false;
    const auto& d = *d_;
    std::vector<Seg> segs;
    if (!parsePath(r.plcName, segs)) return false;
    // Une variable declaree, puis ses membres et ses elements, un a un.
    std::size_t at = 1;
    Node cur;
    const auto u = d.units.find(upper(segs[0].member));
    if (u != d.units.end() && segs.size() >= 2 && !segs[1].index) {
        const auto& vars = d.unitVars.at(u->second);
        cur = d.variable(vars.at(upper(segs[1].member)), std::string(d.plc->strings.text(d.plc->pous[u->second].name)));
        at = 2;
    } else {
        cur = d.variable(d.globals.at(upper(segs[0].member)), {});
    }
    for (; at < segs.size(); ++at) {
        const auto& sg = segs[at];
        if (sg.index) {
            const auto shape = project::members::parseArray(cur.type);
            std::vector<std::int64_t> idx;
            for (const auto& t : sg.idx) {
                std::int64_t v = 0;
                if (!integer(t, v)) return false;
                idx.push_back(v);
            }
            if (!shape.valid() || idx.size() != shape.dims.size()) return false;
            std::int64_t k = 0;
            for (std::size_t dd = 0; dd < idx.size(); ++dd)
                k = k * (shape.dims[dd].second - shape.dims[dd].first + 1) + (idx[dd] - shape.dims[dd].first);
            std::vector<Node> one;
            d.elements(cur, shape, k, k, one);
            if (one.empty() || one.front().kind != NodeKind::Element) return false;
            cur = std::move(one.front());
            continue;
        }
        bool found = false;
        for (auto& m : d.members(cur))
            if (upper(m.name) == upper(sg.member)) {
                cur = std::move(m);
                found = true;
                break;
            }
        if (!found) return false;
    }
    out = std::move(cur);
    return true;
}

std::vector<std::string> Model::search(std::string_view text, std::size_t limit) const {
    std::vector<std::string> out;
    if (empty() || trim(text).empty()) return out;
    const std::string q = upper(trim(text));
    std::size_t visited = 0;
    constexpr std::size_t kMaxVisited = 200000;
    const auto matches = [&](const Node& n) {
        return upper(n.name).find(q) != std::string::npos || upper(n.type).find(q) != std::string::npos
            || (!n.reference.empty() && upper(n.reference).find(q) != std::string::npos);
    };
    std::function<void(const Node&, int)> walk = [&](const Node& n, int depth) {
        if (out.size() >= limit || visited >= kMaxVisited) return;
        ++visited;
        if (n.real() && matches(n)) out.push_back(n.path);
        if (!n.children || depth > 12 || n.kind == NodeKind::More) return;
        for (const auto& c : children(n)) walk(c, depth + 1);
    };
    for (const auto& r : roots()) walk(r, 0);
    return out;
}

Access Model::accessOf(std::string_view plcName, std::string* why, std::string* reference) const {
    if (empty()) {
        if (why) *why = "le programme de l'automate n'est pas charg\xC3\xA9";
        return Access::NoAddress;
    }
    return d_->access(stripApi(plcName), why, reference);
}

Resolved Model::resolve(std::string_view path) const {
    Resolved r;
    const bool api = isApiPath(path);
    const std::string rest = api ? stripApi(path) : trim(path);
    std::vector<Seg> segs;
    if (rest.empty() || !parsePath(rest, segs) || segs.front().index) {
        r.error = std::string(kRoot) + ". : chemin incomplet (API.<variable>, API.<Unit\xC3\xA9>.<variable>)";
        return r;
    }
    if (empty()) {
        r.error = "le programme de l'automate n'est pas charg\xC3\xA9 (Fichier > Importer)";
        return r;
    }
    const auto& d = *d_;
    const auto& p = *d.plc;
    std::string plcName, probe, type, where;
    Index decl = kNoIndex;
    std::size_t at = 1;
    const std::string root = segs[0].member;
    const auto unit = d.units.find(upper(root));
    if (unit != d.units.end() && (segs.size() == 1 || !segs[1].index)) {
        const std::string unitName(p.strings.text(p.pous[unit->second].name));
        r.unit = unitName;
        if (segs.size() == 1) {
            r.unitOnly = true;
            r.error = std::string(kRoot) + "." + unitName + " est une unit\xC3\xA9 de programme : choisis une de ses variables (" + std::string(kRoot) + "."
                    + unitName + ".\xE2\x80\xA6)";
            return r;
        }
        const auto vit = d.unitVars.find(unit->second);
        const auto& member = segs[1].member;
        const bool none = vit == d.unitVars.end();
        const auto found = none ? std::map<std::string, Index, std::less<>>::const_iterator{} : vit->second.find(upper(member));
        if (none || found == vit->second.end()) {
            std::vector<std::string> pool;
            if (!none)
                for (const auto& [k, vi] : vit->second) pool.emplace_back(p.strings.text(p.variables[vi].name));
            r.missing = member;
            r.suggestion = closestOf(member, pool);
            r.error = unitName + " n'a pas de variable " + member;
            if (!r.suggestion.empty()) {
                r.error += " ; veux-tu dire " + r.suggestion + " ?";
                r.suggestedPath = std::string(kRoot) + "." + unitName + "." + r.suggestion;
            }
            return r;
        }
        decl = found->second;
        const auto& v = p.variables[decl];
        plcName = unitName + "." + std::string(p.strings.text(v.name));
        r.scope = scopeOf(v.scope);
        at = 2;
    } else if (const auto g = d.globals.find(upper(root)); g != d.globals.end()) {
        decl = g->second;
        const auto& v = p.variables[decl];
        plcName = std::string(p.strings.text(v.name));
        r.scope = scopeOf(v.scope);
        r.constant = v.scope == VariableScope::Constant;
    } else {
        std::vector<std::string> pool;
        for (const auto vi : d.globalOrder) pool.emplace_back(p.strings.text(p.variables[vi].name));
        for (const auto u : d.unitOrder) pool.emplace_back(p.strings.text(p.pous[u].name));
        r.missing = root;
        r.suggestion = closestOf(root, pool);
        r.error = root + " n'est ni une variable globale ni une unit\xC3\xA9 de programme de l'automate";
        if (!r.suggestion.empty()) {
            r.error += " ; veux-tu dire " + r.suggestion + " ?";
            r.suggestedPath = std::string(kRoot) + "." + r.suggestion;
        }
        return r;
    }
    type = trim(p.strings.text(p.variables[decl].type.name));
    probe = plcName;
    where = std::string(kRoot) + "." + plcName;
    bool computed = false;
    for (; at < segs.size(); ++at) {
        const auto& sg = segs[at];
        if (sg.index) {
            const auto shape = project::members::parseArray(type);
            if (!shape.valid()) {
                r.error = where + " n'est pas un tableau (" + (type.empty() ? std::string("?") : type) + ") : retire l'indice";
                return r;
            }
            if (sg.idx.size() != shape.dims.size()) {
                r.error = where + " a " + std::to_string(shape.dims.size()) + " dimension" + (shape.dims.size() > 1 ? "s" : "") + ", pas "
                        + std::to_string(sg.idx.size());
                return r;
            }
            std::string shown, probed;
            for (std::size_t k = 0; k < sg.idx.size(); ++k) {
                std::int64_t v = 0;
                const bool constant = integer(sg.idx[k], v);
                const auto [lo, hi] = shape.dims[k];
                if (constant && (v < lo || v > hi)) {
                    r.error = "indice " + std::to_string(v) + " hors des bornes de " + where + " (" + std::to_string(lo) + ".." + std::to_string(hi) + ")";
                    return r;
                }
                if (!constant) computed = true;
                shown += (k ? "," : "") + (constant ? std::to_string(v) : sg.idx[k]);
                probed += (k ? "," : "") + std::to_string(constant ? v : lo);
            }
            plcName += "[" + shown + "]";
            probe += "[" + probed + "]";
            where += "[" + shown + "]";
            type = trim(shape.element);
            r.scope = Scope::Element;
            continue;
        }
        const auto& m = sg.member;
        if (!m.empty() && std::isdigit(static_cast<unsigned char>(m[0]))) {   // un bit de mot : Mot.3
            plcName += "." + m;
            probe += "." + m;
            where += "." + m;
            type = "BOOL";
            continue;
        }
        const auto parent = project::members::root(probe, type);
        const auto kids = project::members::children(p, parent);
        const project::members::Node* hit = nullptr;
        std::vector<std::string> pool;
        for (const auto& c : kids) {
            if (!c.real || c.label.empty() || c.label.front() != '.') continue;
            const std::string name = c.label.substr(1);
            pool.push_back(name);
            if (!hit && upper(name) == upper(m)) hit = &c;
        }
        if (!hit) {
            r.missing = m;
            if (kids.empty() && project::members::isElementary(type)) {
                r.error = where + " est un " + type + " de l'automate : il n'a pas de membre " + m;
                return r;
            }
            r.suggestion = closestOf(m, pool);
            r.error = where + " (" + type + ") n'a pas de membre " + m;
            if (!r.suggestion.empty()) {
                r.error += " ; veux-tu dire " + r.suggestion + " ?";
                r.suggestedPath = where + "." + r.suggestion;
            }
            return r;
        }
        const std::string name = hit->label.substr(1);
        plcName += "." + name;
        probe += "." + name;
        where += "." + name;
        r.scope = scopeOfWhat(hit->what);
        type = trim(hit->type);
    }
    r.ok = true;
    r.plcName = plcName;
    r.path = std::string(kRoot) + "." + plcName;
    r.type = type;
    r.access = d.access(probe, &r.accessWhy, &r.reference);
    if (computed) r.reference.clear();
    return r;
}

std::vector<Proposal> Model::propose(std::string_view typed) const {
    std::vector<Proposal> out;
    const std::string t = trim(typed);
    if (t.empty()) return out;
    if (t.find('.') == std::string::npos) {
        if (t.size() <= kRoot.size() && startsWithNoCase(kRoot, t)) {
            Proposal pr;
            pr.kind = Proposal::Kind::Root;
            pr.text = std::string(kRoot);
            pr.path = std::string(kRoot);
            pr.type = "variables de l'automate";
            out.push_back(std::move(pr));
        }
        return out;
    }
    if (!isApiPath(t) || empty()) return out;
    const auto& d = *d_;
    const auto& p = *d.plc;
    const std::string rest = stripApi(t);
    // Le dernier point hors des crochets : ce qui est avant est le chemin, apres le debut du nom.
    std::size_t dot = std::string::npos;
    int depth = 0;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '[') ++depth;
        else if (rest[i] == ']') --depth;
        else if (rest[i] == '.' && depth == 0) dot = i;
    }
    const std::string base = dot == std::string::npos ? std::string{} : trim(rest.substr(0, dot));
    const std::string prefix = trim(dot == std::string::npos ? rest : rest.substr(dot + 1));
    const auto keep = [&](std::string_view name) { return prefix.empty() || startsWithNoCase(name, prefix); };
    const auto variable = [&](Index vi, const std::string& unit, const char* group) {
        const auto& v = p.variables[vi];
        Proposal pr;
        pr.kind = Proposal::Kind::Variable;
        pr.text = std::string(p.strings.text(v.name));
        const std::string plcName = unit.empty() ? pr.text : unit + "." + pr.text;
        pr.path = std::string(kRoot) + "." + plcName;
        pr.type = trim(p.strings.text(v.type.name));
        pr.group = group;
        pr.access = d.access(plcName, nullptr, nullptr);
        // 1.11.2 (API-V, retouche annoncee) : une constante se lit, ne s'ecrit pas (Node::writable,
        // le cadenas de l'arbre) : « L », pas « L/E », meme quand la table des adresses la nomme.
        if (v.scope == VariableScope::Constant && pr.access == Access::ReadWrite) pr.access = Access::ReadOnly;
        pr.accessText = std::string(accessShort(pr.access));
        pr.comment = std::string(p.strings.text(v.comment));
        out.push_back(std::move(pr));
    };
    if (base.empty()) {
        for (const auto u : d.unitOrder) {
            const std::string name(p.strings.text(p.pous[u].name));
            if (!keep(name)) continue;
            Proposal pr;
            pr.kind = Proposal::Kind::Unit;
            pr.text = name;
            pr.path = std::string(kRoot) + "." + name;
            pr.type = "unit\xC3\xA9 de programme";
            pr.group = "Unit\xC3\xA9s";
            out.push_back(std::move(pr));
        }
        for (const auto vi : d.globalOrder)
            if (keep(p.strings.text(p.variables[vi].name))) variable(vi, {}, "Globales");
        return out;
    }
    if (const auto u = d.units.find(upper(base)); u != d.units.end()) {
        const std::string unit(p.strings.text(p.pous[u->second].name));
        const auto& pou = p.pous[u->second];
        std::vector<Index> vars(pou.parameters.begin(), pou.parameters.end());
        vars.insert(vars.end(), pou.locals.begin(), pou.locals.end());
        const struct {
            GroupKind g;
            const char* label;
        } groups[] = {{GroupKind::Public, "Publiques"}, {GroupKind::Private, "Priv\xC3\xA9" "es"}, {GroupKind::InOut, "E/S"}};
        for (const auto& g : groups)
            for (const auto vi : vars) {
                if (vi >= p.variables.size()) continue;
                const auto s = p.variables[vi].scope;
                const bool in = g.g == GroupKind::Public    ? s == VariableScope::Public
                              : g.g == GroupKind::Private   ? s == VariableScope::Local
                                                            : (s == VariableScope::Input || s == VariableScope::Output || s == VariableScope::InOut);
                if (in && keep(p.strings.text(p.variables[vi].name))) variable(vi, unit, g.label);
            }
        return out;
    }
    const auto r = resolve(std::string(kRoot) + "." + base);
    if (!r.ok || project::members::parseArray(r.type).valid()) return out;
    for (const auto& c : project::members::children(p, project::members::root(r.plcName, r.type))) {
        if (!c.real || c.label.empty() || c.label.front() != '.') continue;
        const std::string name = c.label.substr(1);
        if (!keep(name)) continue;
        Proposal pr;
        pr.kind = Proposal::Kind::Member;
        pr.text = name;
        pr.path = std::string(kRoot) + "." + c.path;
        pr.type = trim(c.type);
        pr.group = "Membres";
        pr.access = d.access(c.path, nullptr, nullptr);
        if (r.constant && pr.access == Access::ReadWrite) pr.access = Access::ReadOnly;   // 1.11.2 : le membre d'une constante
        pr.accessText = std::string(accessShort(pr.access));
        out.push_back(std::move(pr));
    }
    return out;
}

std::vector<std::string> Model::unitNames() const {
    std::vector<std::string> out;
    if (empty()) return out;
    for (const auto u : d_->unitOrder) out.emplace_back(d_->plc->strings.text(d_->plc->pous[u].name));
    return out;
}

bool Model::isUnit(std::string_view name) const { return !empty() && d_->units.count(upper(trim(name))) != 0; }

std::vector<std::string> Model::globalNames() const {
    std::vector<std::string> out;
    if (empty()) return out;
    for (const auto vi : d_->globalOrder) out.emplace_back(d_->plc->strings.text(d_->plc->variables[vi].name));
    return out;
}

std::vector<Use> Model::usesOf(std::string_view path) const {
    std::vector<Use> out;
    if (!d_) return out;
    const std::string k = comm::keyOf(stripApi(path));
    if (k.empty()) return out;
    for (auto it = d_->uses.lower_bound(k); it != d_->uses.end(); ++it) {
        const auto& key = it->first;
        if (key.compare(0, k.size(), k) != 0) break;
        if (key.size() == k.size() || key[k.size()] == '.' || key[k.size()] == '[') out.insert(out.end(), it->second.begin(), it->second.end());
    }
    return out;
}

} // namespace hmi::apivars
