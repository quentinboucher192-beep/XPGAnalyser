// =============================================================================
//  app/ai/AiAutomate.cpp - l'automate relie -> l'IHM (1.16.0) : voir AiAutomate.hpp
// =============================================================================
#include "AiAutomate.hpp"

#include "../../hmi/HmiTypeRegistry.hpp"
#include "../../hmi/HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>

namespace app::ai {

namespace {

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

bool contains(std::string_view text, std::string_view piece) {
    return piece.empty() || upper(text).find(upper(trim(piece))) != std::string::npos;
}

// "API.Niveau" -> "Niveau" (le nom tel que l'automate le declare).
std::string bareName(std::string_view n) {
    if (n.size() > 4 && upper(n.substr(0, 4)) == "API.") n.remove_prefix(4);
    return std::string(n);
}

// Un type elementaire de l'automate, dans les types de l'IHM (comme HmiStandalone, Converter::elementary) ; vide : aucun.
std::string elementary(std::string_view name) {
    std::string u = upper(trim(name));
    if (u.rfind("STRING", 0) == 0 && u.find('[') != std::string::npos) u = "STRING";   // STRING[32]
    if (u == "EBOOL") u = "BOOL";
    if (u == "TOD") u = "TIME_OF_DAY";
    if (u == "DT") u = "DATE_AND_TIME";
    return !u.empty() && hmi::types::isElementary(u) ? u : std::string{};
}

// Le nom de l'element d'un type ("ARRAY[0..3] OF T_Vanne" -> "T_Vanne" ; "T_Vanne" -> lui-meme).
std::string elementOf(std::string_view type) {
    const std::string t = trim(type);
    const std::string u = upper(t);
    if (u.rfind("ARRAY", 0) == 0)
        if (const auto of = u.rfind(" OF "); of != std::string::npos) return elementOf(t.substr(of + 4));
    return t;
}

const std::string* ddtNamed(const std::vector<std::string>& ddts, std::string_view name) {
    for (const auto& d : ddts)
        if (upper(d) == upper(trim(name))) return &d;
    return nullptr;
}

std::vector<std::string> ddtNames(const std::vector<Env::PlcType>& types) {
    std::vector<std::string> out;
    for (const auto& t : types) out.push_back(t.name);
    return out;
}

// Les DDT de l'automate a creer, dans l'ordre des dependances : `wanted` (des noms, sans casse) et ce qu'ils
// emploient, sans ceux que l'IHM a deja ; un cycle n'est suivi qu'une fois.
std::vector<Operation> orderedTypes(const hmi::Project& project, const std::vector<Env::PlcType>& all, const std::set<std::string>& wanted) {
    std::vector<Operation> out;
    const auto names = ddtNames(all);
    std::set<std::string> done, doing;
    std::function<void(const Env::PlcType&)> visit = [&](const Env::PlcType& t) {
        const std::string key = upper(t.name);
        if (done.count(key) || doing.count(key) || project.hmiTypeByName(t.name)) return;
        doing.insert(key);
        for (const auto& m : t.members) {
            const std::string element = elementOf(m.type);
            if (!elementary(element).empty()) continue;
            for (const auto& dep : all)
                if (upper(dep.name) == upper(element)) visit(dep);
        }
        doing.erase(key);
        done.insert(key);
        Operation op;
        op.action = Operation::Action::Type;
        op.name = t.name;
        op.props.emplace_back("description", t.comment.empty() ? "Le DDT " + t.name + " de l'automate reli\xC3\xA9" : t.comment);
        for (const auto& m : t.members) {
            const std::string type = hmiTypeOfPlc(m.type, names, project);
            if (type.empty()) continue;   // un membre sans equivalent (une instance de bloc) : ecarte
            op.members.push_back({m.name, type, m.description});
        }
        if (!op.members.empty()) out.push_back(std::move(op));
    };
    for (const auto& t : all)
        if (wanted.count(upper(t.name))) visit(t);
    return out;
}

} // namespace

std::string hmiTypeOfPlc(std::string_view plcType, const std::vector<std::string>& ddts, const hmi::Project& project, std::string* why) {
    const std::string t = trim(plcType);
    const std::string u = upper(t);
    if (u.rfind("ARRAY", 0) == 0) {
        const auto open = t.find('['), close = t.find(']'), of = u.rfind(" OF ");
        if (open == std::string::npos || close == std::string::npos || of == std::string::npos || close > of) {
            if (why) *why = "tableau illisible (" + t + ")";
            return {};
        }
        std::string dims;
        for (const char c : t.substr(open + 1, close - open - 1))
            if (!std::isspace(static_cast<unsigned char>(c))) dims += c;
        std::string bounds;
        for (std::size_t i = 0; i < dims.size(); ++i) bounds += dims[i] == ',' ? std::string(", ") : std::string(1, dims[i]);
        const std::string element = hmiTypeOfPlc(t.substr(of + 4), ddts, project, why);
        if (element.empty()) return {};
        return hmi::types::normalized("ARRAY[" + bounds + "] OF " + element);
    }
    if (const std::string e = elementary(t); !e.empty()) return e;
    if (const std::string* d = ddtNamed(ddts, t)) return *d;
    if (const auto* ht = project.hmiTypeByName(t)) return ht->name;
    // Un type que l'IHM connait, mais pas pour une variable (TOD, DATE, DT : ni place Modbus ni variable IHM).
    std::string u2 = u == "TOD" ? std::string("TIME_OF_DAY") : u == "DT" ? std::string("DATE_AND_TIME") : u;
    if (hmi::typereg::baseRegistry().byName(u2)) {
        if (why) *why = "un " + u2 + " ne se range pas dans une variable IHM";
        return {};
    }
    if (why) *why = "le type " + t + " n'a pas d'\xC3\xA9quivalent dans l'IHM (une instance de bloc, un type d'\xC3\xA9quipement)";
    return {};
}

std::vector<Operation> variablesFromPlc(const hmi::Project& project, const Env& env, std::string_view filter) {
    if (!env.plcVariables) return {};
    const auto types = env.plcTypes ? env.plcTypes() : std::vector<Env::PlcType>{};
    const auto names = ddtNames(types);
    Operation op;
    op.action = Operation::Action::Variables;
    std::set<std::string> seen;
    for (const auto& [full, type] : env.plcVariables()) {
        const std::string name = bareName(full);
        if (!contains(name, filter) || !hmi::isIdentifier(name) || project.variable(name) || !seen.insert(upper(name)).second) continue;
        const std::string hmiType = hmiTypeOfPlc(type, names, project);
        if (hmiType.empty()) continue;   // une instance de bloc (TON, un DFB) : pas une variable IHM
        std::string description = env.plcComment ? trim(env.plcComment(name)) : std::string{};
        if (description.empty()) description = "La variable API." + name + " de l'automate reli\xC3\xA9";
        op.variables.push_back({name, hmiType, description, {}});
    }
    if (op.variables.empty()) return {};
    return {std::move(op)};
}

std::vector<Operation> typesFromPlc(const hmi::Project& project, const Env& env, std::string_view filter) {
    if (!env.plcTypes) return {};
    const auto all = env.plcTypes();
    std::set<std::string> wanted;
    for (const auto& t : all)
        if (contains(t.name, filter)) wanted.insert(upper(t.name));
    return orderedTypes(project, all, wanted);
}

std::vector<Operation> fromPlc(const hmi::Project& project, const Env& env, std::string_view filter) {
    const auto all = env.plcTypes ? env.plcTypes() : std::vector<Env::PlcType>{};
    auto vars = variablesFromPlc(project, env, filter);
    std::set<std::string> wanted;
    for (const auto& t : all)
        if (contains(t.name, filter)) wanted.insert(upper(t.name));
    for (const auto& op : vars)
        for (const auto& v : op.variables) wanted.insert(upper(elementOf(v.type)));
    auto out = orderedTypes(project, all, wanted);
    for (auto& op : vars) out.push_back(std::move(op));
    return out;
}

} // namespace app::ai
