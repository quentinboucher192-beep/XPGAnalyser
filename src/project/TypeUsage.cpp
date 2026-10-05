// project/TypeUsage.cpp - qui se sert de quoi (lot API 5).
#include "TypeUsage.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace project::usage {

using namespace domain;

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

void addUnique(std::vector<std::string>& v, std::string s) {
    if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(std::move(s));
}

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Chaque identifiant « racine » du code (pas un membre apres un point), en
// minuscules ; commentaires (* *), // et chaines '...' "..." sautes.
template <typename F>
void forEachRoot(std::string_view s, F&& f) {
    std::size_t i = 0;
    const auto n = s.size();
    char prev = 0;     // le dernier caractere significatif (hors blancs)
    while (i < n) {
        const char c = s[i];
        if (c == '(' && i + 1 < n && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            i = end == std::string_view::npos ? n : end + 2;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            i = end == std::string_view::npos ? n : end + 1;
            continue;
        }
        if (c == '\'' || c == '"') {
            const auto end = s.find(c, i + 1);
            i = end == std::string_view::npos ? n : end + 1;
            prev = c;
            continue;
        }
        if (isIdentStart(c)) {
            std::size_t j = i + 1;
            while (j < n && isIdentChar(s[j])) ++j;
            if (prev != '.') f(s.substr(i, j - i));
            prev = 'a';
            i = j;
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(c))) prev = c;
        ++i;
    }
}

const std::set<std::string>& standardBlocks() {
    static const std::set<std::string> k{"ton", "tof", "tp", "ctu", "ctd", "ctud", "r_trig", "f_trig", "sr", "rs", "rtc",
                                         "ton_ms", "tof_ms", "tp_ms", "blink", "fbd_ton", "pid", "ramp", "lag", "sah"};
    return k;
}

} // namespace

std::string baseTypeOf(const Project& p, const Variable& v) {
    if (v.type.elementType) return std::string(p.strings.text(v.type.elementType));
    std::string t(p.strings.text(v.type.name));
    std::string u = t;
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const auto of = u.find(" OF ");
    if (u.rfind("ARRAY", 0) == 0 && of != std::string::npos) {
        t = t.substr(of + 4);
        while (!t.empty() && t.front() == ' ') t.erase(t.begin());
        while (!t.empty() && t.back() == ' ') t.pop_back();
    }
    return t;
}

// ------------------------------------------------------------------ Where ----
Where::Where(const Project& p) : project_(&p) {
    for (Index s = 0; s < p.sections.size(); ++s)
        forEachRoot(p.sections[s].body, [&](std::string_view id) {
            auto& v = map_[lower(id)];
            if (v.empty() || v.back() != s) v.push_back(s);
        });
}

const std::vector<Index>& Where::sectionsOf(std::string_view name) const {
    static const std::vector<Index> kNone;
    const auto root = lower(name.substr(0, std::min(name.find('.'), name.find('['))));
    const auto it = map_.find(root);
    return it == map_.end() ? kNone : it->second;
}

std::string Where::sectionNames(std::string_view name, std::size_t max) const {
    const auto& v = sectionsOf(name);
    std::string out;
    std::size_t shown = 0;
    for (const auto s : v) {
        if (shown == max) {
            out += " +" + std::to_string(v.size() - max);
            break;
        }
        out += (out.empty() ? "" : ", ") + std::string(project_->strings.text(project_->sections[s].name));
        ++shown;
    }
    return out;
}

// ------------------------------------------------------------ les types ----
std::string TypeUse::summary() const {
    std::string out;
    const auto add = [&](const std::string& s) { out += (out.empty() ? "" : " \xC2\xB7 ") + s; };
    if (globals) add(std::to_string(globals) + (globals == 1 ? " variable" : " variables"));
    if (!units.empty()) add(std::to_string(units.size()) + (units.size() == 1 ? " unit\xC3\xA9" : " unit\xC3\xA9s"));
    if (dfbs.size() == 1) add(dfbs.front());
    else if (dfbs.size() > 1) add(std::to_string(dfbs.size()) + " DFB");
    if (types.size() == 1) add("dans " + types.front());
    else if (types.size() > 1) add("dans " + types.front() + " +" + std::to_string(types.size() - 1));
    return out.empty() ? std::string("pas utilis\xC3\xA9") : out;
}

TypeUse usesOfType(const Project& p, std::string_view typeName) {
    TypeUse u;
    const auto want = lower(typeName);
    for (const auto& v : p.variables) {
        if (lower(baseTypeOf(p, v)) != want) continue;
        if (v.scope == VariableScope::Global) {
            ++u.globals;
        } else if (v.scope == VariableScope::DerivedMember) {
            if (v.owner < p.derivedTypes.size()) addUnique(u.types, std::string(p.strings.text(p.derivedTypes[v.owner].name)));
        } else if (v.owner < p.pous.size()) {
            const auto& pou = p.pous[v.owner];
            if (pou.kind == PouKind::FunctionBlockType) addUnique(u.dfbs, std::string(p.strings.text(pou.name)));
            else addUnique(u.units, std::string(p.strings.text(pou.name)));
        }
    }
    return u;
}

// ------------------------------------------------------------- les DFB ----
std::vector<Instance> instancesOf(const Project& p, std::string_view blockName, const Where& where) {
    std::vector<Instance> out;
    const auto want = lower(blockName);
    for (Index i = 0; i < p.variables.size(); ++i) {
        const auto& v = p.variables[i];
        if (v.scope == VariableScope::DerivedMember || lower(baseTypeOf(p, v)) != want) continue;
        Instance in;
        in.variable = i;
        in.name = std::string(p.strings.text(v.name));
        in.owner = ownerName(p, v);
        in.calledIn = where.sectionNames(in.name);
        out.push_back(std::move(in));
    }
    return out;
}

Interface interfaceOf(const Project& p, Index pou) {
    Interface f;
    if (pou >= p.pous.size()) return f;
    const auto sort = [&](Index v) {
        if (v >= p.variables.size()) return;
        switch (p.variables[v].scope) {
            case VariableScope::Input:  f.inputs.push_back(v); break;
            case VariableScope::Output: f.outputs.push_back(v); break;
            case VariableScope::InOut:  f.inouts.push_back(v); break;
            case VariableScope::Public: f.publics.push_back(v); break;
            default:                    f.privates.push_back(v); break;
        }
    };
    for (const auto v : p.pous[pou].parameters) sort(v);
    for (const auto v : p.pous[pou].locals) sort(v);
    return f;
}

// --------------------------------------------------------- les variables ----
Genre genreOf(const Project& p, const Variable& v) {
    const auto base = lower(baseTypeOf(p, v));
    for (const auto& pou : p.pous)
        if (pou.kind == PouKind::FunctionBlockType && lower(p.strings.text(pou.name)) == base)
            return pou.userDefined ? Genre::DfbInstance : Genre::StandardBlock;
    if (standardBlocks().count(base) || v.type.klass == TypeClass::FunctionBlock) return Genre::StandardBlock;
    for (const auto& dt : p.derivedTypes)
        if (lower(p.strings.text(dt.name)) == base) return Genre::DdtInstance;
    if (v.located || v.address.valid()) return Genre::Located;
    return Genre::Other;
}

std::string_view genreLabel(Genre g) noexcept {
    switch (g) {
        case Genre::DfbInstance:   return "Instances de DFB";
        case Genre::StandardBlock: return "Temporisations et blocs standard";
        case Genre::DdtInstance:   return "Instances de types d\xC3\xA9riv\xC3\xA9s";
        case Genre::Located:       return "Situ\xC3\xA9" "es (%MW, %I, %M)";
        default:                   return "Les autres";
    }
}

std::string ownerName(const Project& p, const Variable& v) {
    if (v.scope == VariableScope::Global) return {};
    if (v.scope == VariableScope::DerivedMember)
        return v.owner < p.derivedTypes.size() ? std::string(p.strings.text(p.derivedTypes[v.owner].name)) : std::string{};
    return v.owner < p.pous.size() ? std::string(p.strings.text(p.pous[v.owner].name)) : std::string{};
}

// ---------------------------------------------------- les sous-routines ----
std::vector<std::string> callersOf(const Project& p, Index section) {
    std::vector<std::string> out;
    if (section >= p.sections.size()) return out;
    const auto want = lower(p.strings.text(p.sections[section].name));
    for (Index s = 0; s < p.sections.size(); ++s) {
        if (s == section) continue;
        bool hit = false;
        forEachRoot(p.sections[s].body, [&](std::string_view id) { hit = hit || lower(id) == want; });
        if (hit) addUnique(out, std::string(p.strings.text(p.sections[s].name)));
    }
    return out;
}

} // namespace project::usage
