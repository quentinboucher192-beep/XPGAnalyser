#include "Runtime.hpp"

#include "../domain/ExecutionOrder.hpp"

#include "../project/BlockLibrary.hpp"
#include "../project/MemberTree.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <cstdio>

namespace sim {
namespace {

// The project indexes its types by interned name; this wraps the two steps into
// the question actually being asked.
const domain::DerivedType* derivedTypeNamed(const domain::Project& p, std::string_view name) {
    const auto id = const_cast<domain::Project&>(p).strings.intern(name);
    const auto it = p.typeByName.find(id);
    return it == p.typeByName.end() ? nullptr : &p.derivedTypes[it->second];
}

// Lot API 7 : chaque case d'un tableau a plusieurs dimensions, dans l'ordre
// (la derniere dimension varie la plus vite) : "[0,0]", "[0,1]"... - le nom que
// l'interpreteur construit pour Grille[i, j].
template <typename Fn>
void forEachCell(const project::members::ArrayShape& shape, Fn&& fn) {
    if (!shape.valid()) return;
    std::vector<std::int64_t> idx;
    for (const auto& d : shape.dims) {
        if (d.second < d.first) return;
        idx.push_back(d.first);
    }
    for (;;) {
        fn(project::members::indexSuffix(idx));
        std::size_t k = idx.size();
        for (;;) {
            if (k == 0) return;
            --k;
            if (idx[k] < shape.dims[k].second) {
                ++idx[k];
                for (std::size_t j = k + 1; j < idx.size(); ++j) idx[j] = shape.dims[j].first;
                break;
            }
        }
    }
}

std::string lowerOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// The argument named `name`, or the one at `position` when the call used
// positional arguments. ST allows both, sometimes in the same call.
const Value* argument(const std::vector<std::pair<std::string, Value>>& arguments,
                      std::string_view name, std::size_t position) {
    for (const auto& [argName, value] : arguments)
        if (!argName.empty() && upperOf(argName) == upperOf(name)) return &value;
    std::size_t positional = 0;
    for (const auto& [argName, value] : arguments) {
        if (!argName.empty()) continue;
        if (positional++ == position) return &value;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  LES VALEURS INITIALES.
//
//  Toute case partait de la valeur par defaut de son type - FALSE, 0 - quelle
//  que soit la valeur initiale declaree. Or l'automate, lui, l'applique. Deux
//  consequences, invisibles jusqu'a ce qu'on regarde ce que calculent les
//  blocs de libs/ :
//    - un DFB dont une variable privee vaut 15 a l'origine (LAST, le dernier
//      indice) la trouvait a 0, et ne traitait que le PREMIER equipement de son
//      tableau ;
//    - un DDT dont le champ Enable vaut TRUE a l'origine le trouvait a FALSE :
//      dans l'aide, l'exemple de la pompe tournait... et la pompe jamais.
//
//  Le parcours suit exactement celui de declareFrom (tableaux, structures,
//  instances de DFB), sans rien declarer : il rend les couples (case, texte de
//  la valeur initiale), appliques par prepare() ET par reset() - un Arret
//  suivi d'une Marche doit repartir de l'etat de mise sous tension, pas de
//  zeros.
// ---------------------------------------------------------------------------
using Initials = std::vector<std::pair<std::string, std::string>>;

void collectInitials(const domain::Project& p, const domain::Variable& v,
                     const std::string& prefix, Initials& out, int depth) {
    if (depth > 16) return;                     // un DDT qui se contiendrait
    const auto name = prefix + std::string(p.strings.text(v.name));
    const auto typeName = std::string(p.strings.text(v.type.name));

    if (v.type.klass == domain::TypeClass::Array || v.type.arrayHigh >= v.type.arrayLow) {
        // Lot API 7 : plusieurs dimensions - le texte du type fait foi.
        if (const auto shape = project::members::parseArray(typeName); shape.dims.size() > 1) {
            if (shape.count() > 100000) return;
            if (const auto* ddt = derivedTypeNamed(p, shape.element))
                forEachCell(shape, [&](const std::string& suffix) {
                    for (auto fi : ddt->fields) collectInitials(p, p.variables[fi], name + suffix + ".", out, depth + 1);
                });
            return;
        }
        if (v.type.arrayHigh - v.type.arrayLow + 1 > 100000) return;
        const auto element = std::string(p.strings.text(v.type.elementType));
        if (const auto* ddt = derivedTypeNamed(p, element)) {
            for (auto i = v.type.arrayLow; i <= v.type.arrayHigh; ++i) {
                const auto slot = name + "[" + std::to_string(i) + "]";
                for (auto fi : ddt->fields) collectInitials(p, p.variables[fi], slot + ".", out, depth + 1);
            }
        }
        // Un tableau de scalaires initialise en bloc ("[1, 2, 3]") n'est pas
        // lu : aucun fichier de la bibliotheque ne s'en sert.
        return;
    }
    if (const auto* ddt = derivedTypeNamed(p, typeName)) {
        for (auto fi : ddt->fields) collectInitials(p, p.variables[fi], name + ".", out, depth + 1);
        return;
    }
    if (project::BlockLibrary::shared().find(typeName)) return;   // TON, CTU : leur etat
    if (v.type.fbTypeIndex != domain::kNoIndex && v.type.fbTypeIndex < p.pous.size()) {
        const auto& pou = p.pous[v.type.fbTypeIndex];
        for (auto pi : pou.parameters) collectInitials(p, p.variables[pi], name + ".", out, depth + 1);
        for (auto li : pou.locals)     collectInitials(p, p.variables[li], name + ".", out, depth + 1);
        return;
    }
    const auto text = std::string(p.strings.text(v.initValue));
    if (!text.empty()) out.emplace_back(name, text);
}

Initials allInitials(const domain::Project& p) {
    Initials out;
    for (const auto& v : p.variables) {
        if (v.scope == domain::VariableScope::DerivedMember) continue;
        if (v.scope == domain::VariableScope::Global || v.scope == domain::VariableScope::Constant) {
            collectInitials(p, v, {}, out, 0);
            continue;
        }
        if (v.owner != domain::kNoIndex && v.owner < p.pous.size())
            collectInitials(p, v, std::string(p.strings.text(p.pous[v.owner].name)) + ".", out, 0);
    }
    return out;
}

// Un litteral de declaration, rendu dans le type de la case : TRUE, 15, -1,
// 16#FF, 2#1010, 1.5, 1.0E3, T#5S, 'texte'. Rend false quand le texte ne se
// lit pas : la case garde alors sa valeur par defaut, plutot qu'une valeur
// inventee.
bool literalValue(std::string_view raw, Type type, Value& out) {
    std::string t;
    for (const char c : raw) if (c != '_') t += c;
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    if (t.empty()) return false;
    // Un prefixe de type ("INT#5", "REAL#1.5") n'apprend rien ici.
    if (const auto hash = t.find('#'); hash != std::string::npos) {
        const auto head = upperOf(t.substr(0, hash));
        if (head != "16" && head != "8" && head != "2" && head != "T" && head != "TIME")
            t = t.substr(hash + 1);
    }
    const auto up = upperOf(t);

    if (type == Type::String) {
        if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') t = t.substr(1, t.size() - 2);
        out = Value::text(t);
        return true;
    }
    if (type == Type::Bool) {
        if (up == "TRUE" || up == "1")  { out = Value::boolean(true);  return true; }
        if (up == "FALSE" || up == "0") { out = Value::boolean(false); return true; }
        return false;
    }
    if (type == Type::Time) {
        // T#1H2M3S4MS, T#5S, T#250MS.
        const auto hash = up.find('#');
        if (hash == std::string::npos) return false;
        std::int64_t ms = 0;
        std::size_t i = hash + 1;
        while (i < up.size()) {
            std::size_t j = i;
            while (j < up.size() && (std::isdigit(static_cast<unsigned char>(up[j])) || up[j] == '.')) ++j;
            if (j == i) return false;
            const double n = std::atof(up.substr(i, j - i).c_str());
            std::size_t k = j;
            while (k < up.size() && std::isalpha(static_cast<unsigned char>(up[k]))) ++k;
            const auto unit = up.substr(j, k - j);
            if (unit == "D") ms += static_cast<std::int64_t>(n * 86400000.0);
            else if (unit == "H") ms += static_cast<std::int64_t>(n * 3600000.0);
            else if (unit == "M") ms += static_cast<std::int64_t>(n * 60000.0);
            else if (unit == "S") ms += static_cast<std::int64_t>(n * 1000.0);
            else if (unit == "MS") ms += static_cast<std::int64_t>(n);
            else return false;
            i = k;
        }
        out = Value::time(ms);
        return true;
    }
    if (type == Type::Real) {
        char* end = nullptr;
        const double d = std::strtod(t.c_str(), &end);
        if (end == t.c_str()) return false;
        out = Value::real(d);
        return true;
    }
    if (isInteger(type)) {
        int base = 10;
        std::string digits = up;
        bool negative = false;
        if (!digits.empty() && (digits[0] == '-' || digits[0] == '+')) {
            negative = digits[0] == '-';
            digits = digits.substr(1);
        }
        if (const auto hash = digits.find('#'); hash != std::string::npos) {
            base = std::atoi(digits.substr(0, hash).c_str());
            digits = digits.substr(hash + 1);
        }
        if (digits.empty() || (base != 2 && base != 8 && base != 10 && base != 16)) return false;
        char* end = nullptr;
        const auto n = std::strtoll(digits.c_str(), &end, base);
        if (end == digits.c_str() || *end != '\0') return false;
        out = Value::integer(type, negative ? -n : n);
        return true;
    }
    return false;
}

// 1.11.1 (decision 127) : la limite de temps d'un cycle, un reglage d'essai
// invisible du client. XPG_SIM_LIMITE_CYCLE_MS = des millisecondes (0 : pas de
// limite de temps, comme setScanLimits) ; absente, vide, illisible ou au-dela
// d'un jour : `fallback` (1 500 ms), comme avant. Les sessions et les captures
// la posent quand la machine est chargee ; setScanLimits() passe devant.
std::int64_t scanMillisecondsFromEnvironment(std::int64_t fallback) {
    const char* v = std::getenv("XPG_SIM_LIMITE_CYCLE_MS");
    if (v == nullptr || *v == '\0') return fallback;
    for (const char* c = v; *c != '\0'; ++c)
        if (*c < '0' || *c > '9') return fallback;
    char* end = nullptr;
    const long long n = std::strtoll(v, &end, 10);
    if (end == v || *end != '\0' || n < 0 || n > 86400000LL) return fallback;
    return static_cast<std::int64_t>(n);
}

} // namespace

Runtime::Runtime(std::shared_ptr<const domain::Project> project)
    : project_(std::move(project)) {
    maxScanMsSetting_ = scanMillisecondsFromEnvironment(maxScanMsSetting_);
}

// ------------------------------------------------------------- declaring ---
void Runtime::declare(const std::string& name, Type type, bool dynamic) {
    if (name.empty()) return;
    Slot fresh{Value::defaultOf(type), false, {}, false};
    fresh.dynamic = dynamic;                    // lot API 8 : creee par le programme
    slots_.emplace(name, std::move(fresh));
    clearResolveCache();
    canonical_.emplace(lowerOf(name), name);

    // Record the member under every aggregate it belongs to, so that copying a
    // whole array copies the fields of its elements too.
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] != '.' && name[i] != '[') continue;
        if (name[i] == '[' && i == 0) continue;
        aggregates_[lowerOf(name.substr(0, i))].push_back(name.substr(i));
    }
}

void Runtime::declareFrom(const domain::Project& p, const domain::Variable& v,
                          const std::string& prefix) {
    const auto name = prefix + std::string(p.strings.text(v.name));
    const auto typeName = std::string(p.strings.text(v.type.name));

    // An array becomes one slot per element, because the interpreter addresses
    // them by the textual name it built: table[3]. Bounds come from the
    // declaration, so an index outside them finds no slot and is reported.
    //
    // The emptiness test is `high >= low`, not `either bound is non-zero`:
    // TypeRef defaults arrayHigh to -1 to mean "not an array", so the obvious
    // test declared every scalar in the project to be a zero-length array and
    // silently gave the simulator no variables at all.
    if (v.type.klass == domain::TypeClass::Array || v.type.arrayHigh >= v.type.arrayLow) {
        constexpr std::int64_t kMaxElements = 100000;
        // Lot API 7 : UN TABLEAU A PLUSIEURS DIMENSIONS. Le TypeRef de l'import
        // ne garde que la premiere ; le texte du type les a toutes. Une case par
        // n-uplet d'indices, sous le nom que l'interpreteur construit pour
        // Grille[i, j] : "Grille[2,5]".
        if (const auto shape = project::members::parseArray(typeName); shape.dims.size() > 1) {
            if (shape.count() > kMaxElements) {
                prepareDiagnostics_.push_back(Diagnostic{
                    Diagnostic::Severity::Warning,
                    "'" + name + "' declares more than " + std::to_string(kMaxElements) + " elements; it is not simulated",
                    0, {}});
                return;
            }
            const auto* ddt = derivedTypeNamed(p, shape.element);
            const auto cellType = typeFromName(shape.element);
            forEachCell(shape, [&](const std::string& suffix) {
                if (ddt) {
                    for (auto fi : ddt->fields) declareFrom(p, p.variables[fi], name + suffix + ".");
                } else {
                    declare(name + suffix, cellType);
                }
            });
            return;
        }
        const auto element = std::string(p.strings.text(v.type.elementType));
        const auto elementType = typeFromName(element);

        // A declaration like ARRAY[0..999999] would otherwise create a million
        // slots and look like a hang. Refuse it and say so.
        if (v.type.arrayHigh - v.type.arrayLow + 1 > kMaxElements) {
            prepareDiagnostics_.push_back(Diagnostic{
                Diagnostic::Severity::Warning,
                "'" + name + "' declares more than " + std::to_string(kMaxElements)
                    + " elements; it is not simulated",
                0, {}});
            return;
        }
        for (auto i = v.type.arrayLow; i <= v.type.arrayHigh; ++i) {
            const auto slot = name + "[" + std::to_string(i) + "]";
            if (const auto* ddt = derivedTypeNamed(p, element)) {
                // Recursive, not one level: a field may itself be a structure,
                // an array or a block instance. Armoires[0].ana.PT1.ech_basse is
                // four levels deep in the reference project.
                for (auto fi : ddt->fields) declareFrom(p, p.variables[fi], slot + ".");
            } else {
                declare(slot, elementType);
            }
        }
        return;
    }

    // A structure becomes one slot per field.
    if (const auto* ddt = derivedTypeNamed(p, typeName)) {
        for (auto fi : ddt->fields)
            declareFrom(p, p.variables[fi], name + ".");
        return;
    }

    // A function-block instance: its pins become members, and it gets state.
    if (const auto* block = project::BlockLibrary::shared().find(typeName)) {
        instanceTypes_[name] = upperOf(typeName);
        instanceLower_[lowerOf(name)] = name;
        blocks_[name] = BlockState{upperOf(typeName), 0, false, false, 0};
        for (const auto& pin : block->parameters)
            declare(name + "." + pin.name, typeFromName(pin.type));
        return;
    }
    if (v.type.fbTypeIndex != domain::kNoIndex && v.type.fbTypeIndex < p.pous.size()) {
        const auto& pou = p.pous[v.type.fbTypeIndex];
        instanceTypes_[name] = typeName;
        instanceLower_[lowerOf(name)] = name;
        for (auto pi : pou.parameters)
            declareFrom(p, p.variables[pi], name + ".");
        for (auto li : pou.locals)
            declareFrom(p, p.variables[li], name + ".");
        return;
    }

    declare(name, typeFromName(typeName));
}

// 1.10 : un texte ST qui range le resultat de FIND (ou FIND_INT) dans une
// variable puis compare cette variable a -1 (`= -1`, `<> -1`) ou la teste
// `< 0` : il attend -1 quand rien n'est trouve. Hors commentaires ; sans
// distinction de casse.
static bool findExpectsMinusOne(std::string_view body) {
    std::string text;
    text.reserve(body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {           // les commentaires (* *) blanchis
        if (body[i] == '(' && i + 1 < body.size() && body[i + 1] == '*') {
            const auto end = body.find("*)", i + 2);
            const auto stop = end == std::string_view::npos ? body.size() : end + 2;
            text.append(stop - i, ' ');
            i = stop - 1;
            continue;
        }
        text.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(body[i]))));
    }
    auto isIdent = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'; };
    std::vector<std::string> targets;
    for (std::size_t at = text.find("find"); at != std::string::npos; at = text.find("find", at + 4)) {
        if (at > 0 && isIdent(text[at - 1])) continue;
        std::size_t k = at + 4;
        if (text.compare(k, 4, "_int") == 0) k += 4;
        while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
        if (k >= text.size() || text[k] != '(') continue;
        std::size_t b = at;                                     // en arriere : "<variable> :="
        while (b > 0 && std::isspace(static_cast<unsigned char>(text[b - 1]))) --b;
        if (b < 2 || text[b - 1] != '=' || text[b - 2] != ':') continue;
        b -= 2;
        while (b > 0 && std::isspace(static_cast<unsigned char>(text[b - 1]))) --b;
        std::size_t e = b;
        while (b > 0 && isIdent(text[b - 1])) --b;
        if (e > b) targets.push_back(text.substr(b, e - b));
    }
    for (const auto& v : targets) {
        for (std::size_t at = text.find(v); at != std::string::npos; at = text.find(v, at + 1)) {
            if ((at > 0 && isIdent(text[at - 1])) || (at + v.size() < text.size() && isIdent(text[at + v.size()]))) continue;
            std::size_t k = at + v.size();
            while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
            if (k >= text.size()) continue;
            bool lessThanZero = false;
            if (text[k] == '=' ) ++k;                               // "= -1"
            else if (text.compare(k, 2, "<>") == 0) k += 2;          // "<> -1"
            else if (text[k] == '<' && (k + 1 >= text.size() || text[k + 1] != '=')) { ++k; lessThanZero = true; }   // "< 0"
            else continue;
            while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
            if (lessThanZero) {
                if (k < text.size() && text[k] == '0' && (k + 1 >= text.size() || !std::isdigit(static_cast<unsigned char>(text[k + 1])))) return true;
                continue;
            }
            if (k >= text.size() || text[k] != '-') continue;
            ++k;
            while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
            if (k < text.size() && text[k] == '1' && (k + 1 >= text.size() || !std::isdigit(static_cast<unsigned char>(text[k + 1])))) return true;
        }
    }
    return false;
}

core::Result<void> Runtime::prepare(std::string_view task) {
    if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "no project");
    const auto& p = *project_;

    slots_.clear();
    clearResolveCache();
    blocks_.clear();
    instanceTypes_.clear();
    instanceLower_.clear();
    blockBodies_.clear();
    callDepth_ = 0;
    aggregates_.clear();
    programs_.clear();
    prepareDiagnostics_.clear();
    unknownCalls_.clear();
    // Lot API 8 : les numeros des sections, les entrees, le cycle en cours.
    task_ = std::string(task);
    tagNames_.assign(1, std::string{});
    tagPrograms_.assign(1, nullptr);
    entryNames_.clear();
    locatedAt_.clear();
    scanOpen_ = false;
    nextRunnable_ = 0;
    currentRunnable_ = static_cast<std::size_t>(-1);
    frames_.clear();
    trace_ = {};
    scanHit_.reset();
    pendingHit_.reset();
    lastTimes_.clear();
    unitLinks_.clear();          // 1.10.2 : les parametres des unites (voir plus bas)
    paramAliases_.clear();

    // Direct addresses are not declared anywhere: a program simply writes %M12
    // or %MW1174. The runtime provides the whole address space, generously, and
    // reports separately when an access is outside what the PLC is configured
    // for - which is a finding about the program, not about the simulator.
    memoryWarnings_.clear();

    for (const auto& v : p.variables) {
        if (v.scope == domain::VariableScope::DerivedMember) continue;   // reached through its type
        if (v.scope == domain::VariableScope::Global || v.scope == domain::VariableScope::Constant) {
            declareFrom(p, v, {});
            // A located variable is reachable by its address as well as by its
            // name, which is how ST that writes %Q0.1 directly still works.
            if (v.located && !v.address.raw.empty()) {
                const auto name = std::string(p.strings.text(v.name));
                if (auto it = slots_.find(name); it != slots_.end())
                    slots_.emplace(v.address.raw, it->second);
                locatedAt_.emplace(normaliseAddress(v.address.raw), name);   // lot API 8 : qui a ecrit %MW100
            }
            continue;
        }
        // A POU's own declarations, qualified by the POU so two units may each
        // have a "counter" without colliding.
        if (v.owner != domain::kNoIndex && v.owner < p.pous.size())
            declareFrom(p, v, std::string(p.strings.text(p.pous[v.owner].name)) + ".");
    }

    // Les valeurs initiales declarees : voir allInitials().
    for (const auto& [name, text] : allInitials(p))
        if (auto* slot = find(name)) {
            Value v;
            if (literalValue(text, slot->value.type(), v)) slot->value = v;
        }

    // Les sections de la tache, DANS SON ORDRE D'EXECUTION.
    //
    //  Ce bloc prenait les sections de la tache dans l'ordre, puis AJOUTAIT A LA
    //  FIN celles des unites de programme. Une unite placee en tete par
    //  l'utilisateur tournait donc en queue, et la simulation executait un
    //  programme qui n'etait pas le sien - silencieusement, ce qui est le pire
    //  des deux. `domain::executionOrder` mele les deux sources et trie par
    //  `Section::order`, qui est la seule verite.
    const auto taskId = const_cast<domain::Project&>(p).strings.intern(task);
    // Lot API 8 : par ENTREE de la tache (une section, ou une unite en bloc) -
    // les memes sections dans le meme ordre (executionEntries regroupe
    // executionOrder) ; le pas a pas avance d'une entree.
    const auto taskEntries = domain::executionEntries(p, taskId);

    auto scopeOf = [&](domain::Index si) -> std::string {
        if (si >= p.sections.size()) return {};
        const auto owner = p.sections[si].owner;
        if (owner == domain::kNoIndex || owner >= p.pous.size()) return {};
        const auto& pou = p.pous[owner];
        if (pou.kind != domain::PouKind::ProgramUnit
            && pou.kind != domain::PouKind::FunctionBlockType) return {};
        return std::string(p.strings.text(pou.name)) + ".";
    };

    // 1.10 : FIND, QUAND RIEN N'EST TROUVE. L'IEC 61131-3 dit 0, et c'est ce que
    // rend le simulateur. Mais un programme qui compare le resultat de FIND a -1
    // attend -1 : c'est le cas de BUILDING (le constructeur des grafcets de
    // DFB_GRAFCETENGINE, `IF Pos = -1 THEN`) dans tous les projets des armoires.
    // Avec 0, sa boucle de lecture du texte ne finit jamais : le premier cycle
    // s'arretait sur "the scan ran for more than its time limit" (BUILDING, ligne
    // 16), et l'API ne tournait pas. Le programme dit ce qu'il attend : on le suit,
    // et on le dit (une information de la preparation).
    findNotFound_ = 0;
    for (const auto& section : p.sections) {
        if (section.language != domain::PouLanguage::ST || !findExpectsMinusOne(section.body)) continue;
        findNotFound_ = -1;
        prepareDiagnostics_.push_back(Diagnostic{
            Diagnostic::Severity::Info,
            "FIND: this program compares its result with -1 when nothing is found; the simulation returns -1 "
            "in that case (IEC 61131-3 says 0)",
            0, std::string(p.strings.text(section.name))});
        break;
    }

    // The bodies of the project's own function blocks, parsed once. They are not
    // scheduled by the task: they run when an instance is called.
    for (const auto& pou : p.pous) {
        if (pou.kind != domain::PouKind::FunctionBlockType) continue;
        const auto typeName = std::string(p.strings.text(pou.name));
        for (auto si : pou.sections) {
            if (si >= p.sections.size()) continue;
            const auto& body = p.sections[si];
            const auto bodyName = typeName + "." + std::string(p.strings.text(body.name));
            if (body.language != domain::PouLanguage::ST) {
                prepareDiagnostics_.push_back(Diagnostic{
                    Diagnostic::Severity::Warning,
                    "the body of DFB '" + typeName + "' is "
                        + std::string(domain::toString(body.language))
                        + "; instances of it will store their inputs but do nothing",
                    0, bodyName});
                continue;
            }
            auto program = parse(body.body, bodyName);
            if (!program) {
                prepareDiagnostics_.push_back(Diagnostic{
                    Diagnostic::Severity::Error, program.error().message(), 0, bodyName});
                continue;
            }
            // Lot API 8 : son numero (qui a ecrit, points d'arret).
            setProgramTag(**program, static_cast<std::uint32_t>(tagNames_.size()));
            tagNames_.push_back(bodyName);
            tagPrograms_.push_back(program->get());
            blockBodies_[typeName].push_back(*program);
        }
    }

    for (const auto& entry : taskEntries) {
        const auto entryIndex = static_cast<std::uint32_t>(entryNames_.size());
        bool runs = false;
        for (auto si : entry.sections) {
            if (si >= p.sections.size()) continue;
            const auto& section = p.sections[si];
            const auto name = std::string(p.strings.text(section.name));

            if (section.language != domain::PouLanguage::ST) {
                prepareDiagnostics_.push_back(Diagnostic{
                    Diagnostic::Severity::Warning,
                    "section '" + name + "' is " + std::string(domain::toString(section.language))
                        + "; this simulator runs Structured Text only, so it will not be executed",
                    0, name});
                continue;
            }
            auto program = parse(section.body, name);
            if (!program) {
                prepareDiagnostics_.push_back(Diagnostic{
                    Diagnostic::Severity::Error, program.error().message(), 0, name});
                continue;
            }
            // Lot API 8 : le nom d'une section d'unite porte son unite
            // ("Logigrammes_A.Matrice") - trois unites ont leur "Init", et
            // MAST a le sien : qui a ecrit, un point d'arret ne s'y trompent pas.
            const auto scope = scopeOf(si);
            setProgramTag(**program, static_cast<std::uint32_t>(tagNames_.size()));
            tagNames_.push_back(scope + name);
            tagPrograms_.push_back(program->get());
            Runnable runnable{name, scope, *program, entryIndex, {}, {}, true};
            // 1.10.2 : LA CONDITION D'ACTIVATION ("configuree") : lue a chaque
            // cycle dans la portee de la section ; fausse, la section ne tourne pas.
            if (section.activationCondition != 0) {
                auto text = std::string(p.strings.text(section.activationCondition));
                text.erase(0, text.find_first_not_of(" \t\r\n"));
                text.erase(text.find_last_not_of(" \t\r\n;") + 1);
                if (!text.empty()) {
                    if (auto condition = parseExpression(text)) {
                        runnable.condition = *condition;
                        runnable.conditionText = text;
                    } else {
                        prepareDiagnostics_.push_back(Diagnostic{
                            Diagnostic::Severity::Warning,
                            "the activation condition of section '" + name + "' (" + text
                                + ") cannot be read; the section runs every scan",
                            0, scope + name});
                    }
                }
            }
            programs_.push_back(std::move(runnable));
            runs = true;
        }
        // Une entree dont rien ne tourne (du LD, une section qui ne se lit pas)
        // n'est pas un pas : le pas a pas l'enjambe.
        if (!runs) continue;
        // 1.10.2 : les parametres de l'unite relies a une globale.
        // Une unite sans rang (order 0) tourne section par section : chacune
        // est une entree, et ses parametres sont copies autour de chacune.
        unitLinks_.resize(entryIndex + 1);
        if (entry.pou < p.pous.size() && p.pous[entry.pou].kind == domain::PouKind::ProgramUnit)
            linkUnitParameters(p, entry.pou, unitLinks_[entryIndex]);
        if (entry.unit && entry.pou < p.pous.size()) entryNames_.emplace_back(p.strings.text(p.pous[entry.pou].name));
        else entryNames_.push_back(programs_.back().section);
    }
    unitLinks_.resize(entryNames_.size());
    // Les noms deja resolus (valeurs initiales) l'ont ete sans les IN_OUT relies.
    if (!paramAliases_.empty()) clearResolveCache();
    pendingTimes_.assign(programs_.size(), {});
    // Lot API 8 : les points d'arret poses avant la preparation.
    if (!breakpoints_.empty()) {
        resolveBreakpoints();
    } else {
        breakInfo_.clear();
        anyArmed_ = false;
    }

    if (programs_.empty())
        return core::fail(core::ErrorCode::IncompleteProject,
                          "no Structured Text section of task '" + std::string(task)
                              + "' could be prepared");
    return {};
}

void Runtime::reset() {
    for (auto& [name, slot] : slots_) {
        slot.value = Value::defaultOf(slot.value.type());
        slot.written = false;
        slot.writerTag = slot.writerLine = 0;       // lot API 8 : qui a ecrit - personne encore
        slot.writerScan = 0;
    }
    // Lot API 8 : le cycle en cours (pas a pas), les passages, les temps.
    scanOpen_ = false;
    nextRunnable_ = 0;
    currentRunnable_ = static_cast<std::size_t>(-1);
    currentScan_ = 0;
    frames_.clear();
    trace_ = {};
    scanHit_.reset();
    pendingHit_.reset();
    lastTimes_.clear();
    for (auto& bp : breakpoints_) bp.hits = 0;
    for (auto& info : breakInfo_) info.trueNow = info.trueBefore = false;   // "devient vraie" : de zero
    // Repartir de l'etat de mise sous tension, valeurs initiales comprises.
    // 1.10.2 : sans celles d'un IN_OUT relie (c'est la globale : elle a les siennes).
    if (project_)
        for (const auto& [name, text] : allInitials(*project_))
            if (auto* slot = paramAliases_.empty() || !findParamAlias(name) ? find(name) : nullptr) {
                Value v;
                if (literalValue(text, slot->value.type(), v)) slot->value = v;
            }
    for (auto& [name, state] : blocks_) state = BlockState{state.type, 0, false, false, 0};
    history_.clear();
    clockMs_ = 0;
    scans_   = 0;
    halted_  = false;
}

namespace {
// "%MW0[3].5" or "counter.2": a numeric member is a bit of the thing before it.
// Returns the owning name and the bit index, or false when the member is a
// field name rather than a number.
bool splitBitAccess(std::string_view name, std::string& owner, int& bit) {
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size()) return false;
    const auto digits = name.substr(dot + 1);
    if (!std::all_of(digits.begin(), digits.end(),
                     [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return false;
    owner = std::string(name.substr(0, dot));
    bit   = std::atoi(std::string(digits).c_str());
    return true;
}
} // namespace

// A direct address may be written indexed: %MW0[57] means word 57, and their
// program uses it heavily to walk a block of memory. Rewriting it here means the
// rest of the runtime only ever sees flat addresses.
std::string Runtime::normaliseAddress(std::string_view name) {
    if (name.empty() || name.front() != '%') return std::string(name);
    const auto open = name.find('[');
    if (open == std::string_view::npos) return std::string(name);
    const auto close = name.find(']', open);
    if (close == std::string_view::npos) return std::string(name);

    // Split the prefix into letters and the base number: "%MW" + "0".
    std::size_t digitsAt = 1;
    while (digitsAt < open && !std::isdigit(static_cast<unsigned char>(name[digitsAt]))) ++digitsAt;
    const auto prefix = name.substr(0, digitsAt);
    const auto base   = std::atoll(std::string(name.substr(digitsAt, open - digitsAt)).c_str());
    const auto offset = std::atoll(std::string(name.substr(open + 1, close - open - 1)).c_str());

    std::string out = std::string(prefix) + std::to_string(base + offset);
    out += name.substr(close + 1);          // whatever followed, e.g. ".0"
    return out;
}

// ------------------------------------------------------------------ slots ---
namespace {
// One resolution rule, used by both overloads so they can never disagree:
// scope first, then the plain name, then the flattened address - each tried
// exactly as written and then case-insensitively.
template <class Map, class Canonical>
auto resolveKey(const Map& slots, const Canonical& canonical, const std::string& scope,
                std::string_view name) -> decltype(slots.find(std::string{})) {
    auto tryKey = [&](const std::string& key) {
        auto it = slots.find(key);
        if (it != slots.end()) return it;
        auto alias = canonical.find(lowerOf(key));
        return alias == canonical.end() ? slots.end() : slots.find(alias->second);
    };
    if (!scope.empty() && (name.empty() || name.front() != '%')) {
        auto it = tryKey(scope + std::string(name));
        if (it != slots.end()) return it;
    }
    auto it = tryKey(std::string(name));
    if (it != slots.end()) return it;
    if (!name.empty() && name.front() == '%') return tryKey(Runtime::normaliseAddress(name));
    return slots.end();
}
} // namespace

Runtime::Slot* Runtime::find(std::string_view name) {
    // The const overload does the work; casting back is safe because this
    // overload is only reached through a non-const Runtime.
    return const_cast<Slot*>(static_cast<const Runtime*>(this)->find(name));
}
const Runtime::Slot* Runtime::find(std::string_view name) const {
    if (!cacheCurrent_ || cacheScope_ != scopePrefix_) {
        cacheScope_ = scopePrefix_;
        cacheCurrent_ = &resolveCache_[scopePrefix_];
    }
    if (const auto hit = cacheCurrent_->find(name); hit != cacheCurrent_->end()) return hit->second;
    // 1.10.2 : un parametre IN_OUT d'unite EST sa globale - avant sa propre case.
    const Slot* slot = paramAliases_.empty() ? nullptr : findParamAlias(name);
    if (!slot) {
        auto it = resolveKey(slots_, canonical_, scopePrefix_, name);
        slot = it == slots_.end() ? nullptr : &it->second;
    }
    // Seulement ce qui est trouve : un nom inconnu peut le devenir (une adresse
    // %MW que le programme touche pour la premiere fois).
    if (slot) cacheCurrent_->emplace(std::string(name), slot);
    return slot;
}

// 1.10.2 : "Unite.param", "Unite.param[3].etat" (ou "param..." dans la portee
// de l'unite) quand param est un IN_OUT relie : la case de sa globale. Une seule
// recherche dans la table : la cle est "unite.param", en minuscules.
const Runtime::Slot* Runtime::findParamAlias(std::string_view name) const {
    if (name.empty() || name.front() == '%') return nullptr;
    auto tryFull = [&](const std::string& full) -> const Slot* {
        const auto dot = full.find('.');
        if (dot == std::string::npos || dot == 0) return nullptr;
        std::size_t end = dot + 1;
        while (end < full.size() && full[end] != '.' && full[end] != '[') ++end;
        const auto alias = paramAliases_.find(lowerOf(std::string_view(full).substr(0, end)));
        if (alias == paramAliases_.end()) return nullptr;
        auto it = resolveKey(slots_, canonical_, std::string{}, alias->second + full.substr(end));
        return it == slots_.end() ? nullptr : &it->second;
    };
    if (!scopePrefix_.empty())
        if (const auto* s = tryFull(scopePrefix_ + std::string(name))) return s;
    return tryFull(std::string(name));
}

// 1.10.2 : les parametres de l'unite `pou` relies a une globale. Les cases du
// parametre existent (declareFrom) ; celles de la globale aussi : on les apparie
// une a une, par le meme suffixe ("[0].etat"), une fois pour toutes.
void Runtime::linkUnitParameters(const domain::Project& p, domain::Index pou, UnitLinks& links) {
    if (pou >= p.pous.size()) return;
    const auto unit = std::string(p.strings.text(p.pous[pou].name));
    for (auto vi : p.pous[pou].parameters) {
        if (vi >= p.variables.size()) continue;
        const auto& v = p.variables[vi];
        if (v.scope != domain::VariableScope::Input && v.scope != domain::VariableScope::Output
            && v.scope != domain::VariableScope::InOut)
            continue;
        std::string effective;
        for (const auto& [key, value] : v.attributes)
            if (key == "EffectiveParameter") effective = value;
        effective.erase(0, effective.find_first_not_of(" \t"));
        effective.erase(effective.find_last_not_of(" \t") + 1);
        if (effective.empty()) continue;                         // non relie : sa propre variable
        const auto param = unit + "." + std::string(p.strings.text(v.name));
        if (effective.front() == '%') (void)ensureAddress(effective);

        std::vector<std::string> suffixes;
        if (auto agg = aggregates_.find(lowerOf(param)); agg != aggregates_.end()) suffixes = agg->second;
        else suffixes.emplace_back();
        std::size_t paired = 0;
        for (const auto& suffix : suffixes) {
            auto own = slots_.find(param + suffix);
            if (own == slots_.end()) {
                auto low = canonical_.find(lowerOf(param + suffix));
                if (low == canonical_.end() || (own = slots_.find(low->second)) == slots_.end()) continue;
            }
            const auto found = resolveKey(slots_, canonical_, std::string{}, effective + suffix);
            if (found == slots_.end()) continue;
            auto* global = const_cast<Slot*>(&found->second);     // resolveKey lit une table const
            auto* mine = &own->second;
            if (global == mine) continue;
            ++paired;
            if (v.scope == domain::VariableScope::Input) links.in.push_back(ParamCopy{global, mine});
            else if (v.scope == domain::VariableScope::Output) links.out.push_back(ParamCopy{mine, global});
            else links.mirror.push_back(ParamCopy{global, mine});
        }
        if (paired == 0) {
            prepareDiagnostics_.push_back(Diagnostic{
                Diagnostic::Severity::Warning,
                "parameter '" + param + "' is linked to '" + effective
                    + "', which the simulator does not know; it keeps its own value",
                0, unit});
            continue;
        }
        if (v.scope == domain::VariableScope::InOut) paramAliases_[lowerOf(param)] = effective;
    }
}

// 1.10.2 : la copie d'une entree (avant) ou d'une sortie (apres). La valeur lue
// est celle que le programme lirait (le forcage s'il y en a un) ; une globale
// forcee garde son forcage, et dessous ce que l'unite y met (comme write()).
void Runtime::copyParameters(const std::vector<ParamCopy>& copies, bool noteWriter) {
    for (const auto& c : copies) {
        const auto& v = c.from->forced ? c.from->forcedValue : c.from->value;
        c.to->value.assignFrom(v);
        if (noteWriter && c.from->written) {
            // Qui a ecrit la globale : la ligne de l'unite qui a ecrit le parametre.
            c.to->written = true;
            c.to->writerTag = c.from->writerTag;
            c.to->writerLine = c.from->writerLine;
            c.to->writerScan = c.from->writerScan;
        }
    }
}

// Touching an address beyond what the PLC is configured for is not a simulator
// problem, it is a finding: the program would not run on the machine. Reported
// once per address so a loop does not produce ten thousand identical lines.
void Runtime::checkAgainstConfiguredMemory(const std::string& name) {
    if (!project_ || !project_->hardware.memory.declared) return;
    if (name.size() < 2 || name.front() != '%') return;

    const auto& mem = project_->hardware.memory;
    std::uint32_t limit = 0;
    std::size_t digitsAt = 0;
    if (name.compare(0, 3, "%MW") == 0) { limit = mem.internalWords; digitsAt = 3; }
    else if (name.compare(0, 2, "%M") == 0
             && std::isdigit(static_cast<unsigned char>(name[2]))) { limit = mem.internalBits; digitsAt = 2; }
    else return;
    if (limit == 0) return;

    const auto index = std::atoll(name.c_str() + digitsAt);
    if (index < limit) return;
    if (std::find(memoryWarnings_.begin(), memoryWarnings_.end(), name) != memoryWarnings_.end()) return;
    memoryWarnings_.push_back(name);
    scanDiagnostics_.push_back(Diagnostic{
        Diagnostic::Severity::Warning,
        name + " is outside the configured data memory (" + std::to_string(limit)
            + (digitsAt == 3 ? " words" : " bits") + "); the PLC would reject this",
        0, {}});
}

std::vector<std::string> Runtime::names() const {
    std::vector<std::string> out;
    out.reserve(slots_.size());
    for (const auto& [name, slot] : slots_) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

bool Runtime::known(std::string_view name) const { return find(name) != nullptr; }

bool Runtime::get(std::string_view name, Value& out) const {
    if (const auto* slot = find(name)) {
        out = slot->forced ? slot->forcedValue : slot->value;
        return true;
    }
    // A bit of a word: %MW0[3].5. Reading it is a shift and a mask rather than
    // a slot of its own, so a word and its bits can never disagree.
    std::string owner;
    int bit = 0;
    if (splitBitAccess(name, owner, bit)) {
        const auto* word = find(owner);
        if (word && bit >= 0 && bit < 64) {
            const auto value = (word->forced ? word->forcedValue : word->value).asInteger();
            out = Value::boolean(((value >> bit) & 1) != 0);
            return true;
        }
    }
    return false;
}

bool Runtime::set(std::string_view name, const Value& v) {
    auto* slot = find(name);
    if (!slot) return false;
    slot->value.assignFrom(v);
    noteWrite(*slot);        // lot API 8 : hors du programme (une table d'animation, un essai)
    return true;
}

// Lot API 8 : QUI A ECRIT - la section (son numero), la ligne et le cycle du
// moment, poses sur la case a chaque ecriture. Trois mots : le suivi coute ce
// que coute une ecriture de plus en memoire deja chaude.
void Runtime::noteWrite(Slot& slot) noexcept {
    slot.written = true;
    slot.writerTag = trace_.tag;
    slot.writerLine = trace_.line;
    slot.writerScan = scanOpen_ ? currentScan_ : scans_;
}

bool Runtime::force(std::string_view name, const Value& v) {
    auto* slot = find(name);
    if (!slot) return false;
    slot->forced = true;
    slot->forcedValue = Value::defaultOf(slot->value.type());
    slot->forcedValue.assignFrom(v);
    return true;
}

bool Runtime::unforce(std::string_view name) {
    auto* slot = find(name);
    if (!slot) return false;
    slot->forced = false;
    return true;
}

void Runtime::unforceAll() { for (auto& [name, slot] : slots_) slot.forced = false; }

bool Runtime::isForced(std::string_view name) const {
    const auto* slot = find(name);
    return slot && slot->forced;
}

std::vector<std::string> Runtime::forcedNames() const {
    std::vector<std::string> out;
    for (const auto& [name, slot] : slots_) if (slot.forced) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

std::string Runtime::exportForcing() const {
    std::ostringstream out;
    out << "# Forcing configuration for the XpgAnalyzer simulator.\n"
           "# One variable per line: name = value. TRUE/FALSE, 42, 1.5, T#500ms.\n"
           "# Names that no longer exist are reported on import rather than\n"
           "# silently dropped: a scenario that half-applies is worse than one\n"
           "# that says what it could not do.\n";
    for (const auto& name : forcedNames()) {
        Value v;
        if (!get(name, v)) continue;
        out << name << " = " << v.display() << '\n';
    }
    return out.str();
}

std::size_t Runtime::importForcing(std::string_view text, std::vector<std::string>* unknown) {
    std::size_t applied = 0;
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto nl = text.find('\n', from);
        auto line = text.substr(from, (nl == std::string_view::npos ? text.size() : nl) - from);
        from = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;

        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r' || line.back() == '\t'))
            line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;

        const auto eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        auto name = line.substr(0, eq);
        auto text2 = line.substr(eq + 1);
        while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
        while (!text2.empty() && text2.front() == ' ') text2.remove_prefix(1);

        if (!known(name)) {
            if (unknown) unknown->emplace_back(name);
            continue;
        }
        std::string upper(text2);
        for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        Value v;
        if (upper == "TRUE")        v = Value::boolean(true);
        else if (upper == "FALSE")  v = Value::boolean(false);
        else if (upper.rfind("T#", 0) == 0)
            v = Value::time(std::atoll(std::string(text2.substr(2)).c_str()));
        else if (text2.find('.') != std::string_view::npos)
            v = Value::real(std::atof(std::string(text2).c_str()));
        else if (!text2.empty() && text2.front() == '\'')
            v = Value::text(std::string(text2.substr(1, text2.size() - 2)));
        else
            v = Value::integer(Type::DInt, std::atoll(std::string(text2).c_str()));

        if (force(name, v)) ++applied;
    }
    return applied;
}

void Runtime::watch(std::string_view name) { history_[std::string(name)]; }
void Runtime::unwatch(std::string_view name) { history_.erase(std::string(name)); }

const std::deque<Sample>* Runtime::history(std::string_view name) const {
    auto it = history_.find(std::string(name));
    return it == history_.end() ? nullptr : &it->second;
}

// A direct address is created the first time the program names it. The type
// comes from the prefix, which is how Control Expert reads them too: %M and %I
// are bits, %MW and %IW are words, %MD is a double word, %MF a real.
Runtime::Slot* Runtime::ensureAddress(std::string_view name) {
    if (name.empty() || name.front() != '%') return nullptr;
    if (auto* existing = find(name)) return existing;

    const auto flat = normaliseAddress(name);
    if (auto* existing = find(flat)) return existing;

    // A trailing ".n" on a bit-addressable word is a bit, not an address of its
    // own; it is handled by the bit-access path, not created here.
    std::string letters;
    for (std::size_t i = 1; i < flat.size() && std::isalpha(static_cast<unsigned char>(flat[i])); ++i)
        letters.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(flat[i]))));

    Type type = Type::Unknown;
    if (letters == "M" || letters == "I" || letters == "Q" || letters == "S") type = Type::Bool;
    else if (letters == "MW" || letters == "IW" || letters == "QW"
             || letters == "SW" || letters == "KW") type = Type::Word;
    else if (letters == "MD" || letters == "KD" || letters == "ID" || letters == "QD"
             || letters == "SD")
        type = Type::DWord;   // %SD18 : le compteur de 100 ms de l'automate
    else if (letters == "MF" || letters == "KF") type = Type::Real;
    if (type == Type::Unknown) return nullptr;

    declare(flat, type, /*dynamic*/ true);
    checkAgainstConfiguredMemory(flat);
    return find(flat);
}

// ------------------------------------------------------------ Environment ---
bool Runtime::read(std::string_view name, Value& out) {
    if (!name.empty() && name.front() == '%') {
        // A bit of a word must not create a slot of its own; get() resolves it
        // against the word, which ensureAddress has just created if needed.
        std::string owner;
        int bit = 0;
        if (splitBitAccess(normaliseAddress(name), owner, bit) && owner.size() > 1
            && owner.front() == '%')
            (void)ensureAddress(owner);
        else
            (void)ensureAddress(name);
    }
    return get(name, out);
}

bool Runtime::write(std::string_view name, const Value& v) {
    if (!name.empty() && name.front() == '%') {
        std::string owner;
        int bit = 0;
        if (splitBitAccess(normaliseAddress(name), owner, bit) && owner.size() > 1
            && owner.front() == '%')
            (void)ensureAddress(owner);
        else
            (void)ensureAddress(name);
    }
    auto* slot = find(name);
    if (!slot) {
        // Setting a bit of a word writes the word, so the two stay consistent.
        std::string owner;
        int bit = 0;
        if (splitBitAccess(name, owner, bit)) {
            if (auto* word = find(owner); word && bit >= 0 && bit < 64) {
                noteWrite(*word);                   // lot API 8 : (et written)
                // Lot API 8 : forcee, le mot garde en dessous ce que le programme
                // y ecrit (programValue : "le programme dirait") ; on lit le forcage.
                auto bits = word->value.asInteger();
                bits = v.isTruthy() ? (bits | (std::int64_t{1} << bit))
                                    : (bits & ~(std::int64_t{1} << bit));
                word->value.assignFrom(Value::integer(word->value.type(), bits));
                return true;
            }
        }
    }
    if (!slot) {
        // UN ELEMENT DE TABLEAU QUI N'EXISTE PAS : l'indice est hors des bornes.
        // Sur l'automate, c'est une faute (%S20, ou une case voisine ecrasee).
        // Le creer en silence, comme une variable de boucle, cachait le bug :
        // BUILDING ecrivait Sources[3] pour une transition a quatre sources, et
        // rien ne le disait. L'ecriture est refusee, le cycle s'arrete et dit
        // pourquoi. Une variable de boucle n'a jamais de crochet.
        if (name.find('[') != std::string_view::npos) return false;
        // The loop variable of a FOR is not declared anywhere; creating it on
        // first write is what makes `FOR i := ...` work without a declaration,
        // and matches what Control Expert tolerates.
        declare(std::string(name), v.type(), /*dynamic*/ true);
        slot = find(name);
        if (!slot) return false;
    }
    noteWrite(*slot);                               // lot API 8 : qui a ecrit (et written)
    if (!name.empty() && name.front() == '%') checkAgainstConfiguredMemory(normaliseAddress(name));
    // A forced variable ignores the program, exactly as on the PLC. The write is
    // accepted so the scan continues; it simply has no effect.
    // Lot API 8 : LE PROGRAMME DIRAIT. La case garde en dessous ce que le
    // programme y ecrit (Slot.value) ; tout ce qui lit (read, get) rend le
    // forcage (forcedValue) : pour le programme, rien ne change. Relachee, elle
    // reprend la derniere valeur du programme (et plus celle d'avant le forcage).
    slot->value.assignFrom(v);
    return true;
}

bool Runtime::exists(std::string_view name) { return known(name); }

void Runtime::report(Diagnostic d) {
    // Only an error stops the scan. A warning - a DFB body that is not executed,
    // an address outside the configured memory - is information, and halting on
    // it would make the simulator useless on any real project.
    const bool isError = d.severity == Diagnostic::Severity::Error;
    scanDiagnostics_.push_back(std::move(d));
    if (isError) halted_ = true;
}

// Lot API 7 : une fonction inconnue. Continuer (rendre 0) si on l'a choisi, et
// la noter - une fois par nom, avec le nombre d'appels et le premier endroit.
bool Runtime::tolerateUnknownCall(std::string_view name, std::uint32_t line) {
    if (!continueOnUnknown_ || probing_ > 0) return false;   // lot API 8 : une condition n'est pas le programme
    const auto key = upperOf(name);
    for (auto& u : unknownCalls_)
        if (upperOf(u.name) == key) { ++u.calls; return true; }
    unknownCalls_.push_back(UnknownCall{std::string(name), 1, currentSection_, line});
    return true;
}

// -------------------------------------------------------- standard blocks ---
bool Runtime::runStandardBlock(const std::string& instance, const std::string& type,
                               const std::vector<std::pair<std::string, Value>>& arguments) {
    auto& state = blocks_[instance];
    state.type = type;

    auto pin = [&](std::string_view name, std::size_t position) -> const Value* {
        return argument(arguments, name, position);
    };
    auto store = [&](std::string_view member, const Value& v) {
        if (auto* slot = find(instance + "." + std::string(member))) {
            slot->value.assignFrom(v);
            noteWrite(*slot);                       // lot API 8 : la ligne de l'appel
        }
    };
    auto member = [&](std::string_view name) {
        Value v;
        (void)get(instance + "." + std::string(name), v);
        return v;
    };

    if (type == "TON" || type == "TOF" || type == "TP") {
        if (const auto* in = pin("IN", 0)) store("IN", *in);
        if (const auto* pt = pin("PT", 1)) store("PT", *pt);
        const bool in = member("IN").isTruthy();
        // PT is read every scan, so changing it mid-count simply moves the
        // target and the timer keeps counting - which is the behaviour asked for.
        const auto preset = member("PT").asInteger();

        if (type == "TON") {
            if (in) {
                if (!state.running) { state.running = true; state.elapsed = 0; }
                else                 state.elapsed += deltaMs_;
            } else { state.running = false; state.elapsed = 0; }
            store("ET", Value::time(std::min(state.elapsed, preset)));
            store("Q",  Value::boolean(in && state.elapsed >= preset));
        } else if (type == "TOF") {
            if (in) { state.running = false; state.elapsed = 0; }
            else if (!state.running) { state.running = true; state.elapsed = 0; }
            else state.elapsed += deltaMs_;
            store("ET", Value::time(std::min(state.elapsed, preset)));
            store("Q",  Value::boolean(in || (state.running && state.elapsed < preset)));
        } else {   // TP
            if (in && !state.running && state.elapsed == 0) { state.running = true; state.elapsed = 0; }
            else if (state.running) state.elapsed += deltaMs_;
            if (state.running && state.elapsed >= preset) { state.running = false; }
            if (!in && !state.running) state.elapsed = 0;
            store("ET", Value::time(std::min(state.elapsed, preset)));
            store("Q",  Value::boolean(state.running));
        }
        return true;
    }

    if (type == "R_TRIG" || type == "F_TRIG") {
        if (const auto* clk = pin("CLK", 0)) store("CLK", *clk);
        const bool clk = member("CLK").isTruthy();
        const bool edge = type == "R_TRIG" ? (clk && !state.previousClock)
                                           : (!clk && state.previousClock);
        state.previousClock = clk;
        store("Q", Value::boolean(edge));
        return true;
    }

    if (type == "CTU" || type == "CTD") {
        const auto* clock = pin(type == "CTU" ? "CU" : "CD", 0);
        if (clock) store(type == "CTU" ? "CU" : "CD", *clock);
        if (const auto* r = pin(type == "CTU" ? "R" : "LD", 1)) store(type == "CTU" ? "R" : "LD", *r);
        if (const auto* pv = pin("PV", 2)) store("PV", *pv);

        const bool tick  = member(type == "CTU" ? "CU" : "CD").isTruthy();
        const bool reset = member(type == "CTU" ? "R" : "LD").isTruthy();
        const auto preset = member("PV").asInteger();

        if (reset) state.counter = type == "CTU" ? 0 : preset;
        else if (tick && !state.previousClock) state.counter += (type == "CTU" ? 1 : -1);
        state.previousClock = tick;

        store("CV", Value::integer(Type::Int, state.counter));
        store("Q",  Value::boolean(type == "CTU" ? state.counter >= preset : state.counter <= 0));
        return true;
    }

    if (type == "SR" || type == "RS") {
        const bool set   = pin(type == "SR" ? "SET1" : "SET", 0)
                               ? pin(type == "SR" ? "SET1" : "SET", 0)->isTruthy() : false;
        const bool reset = pin(type == "SR" ? "RESET" : "RESET1", 1)
                               ? pin(type == "SR" ? "RESET" : "RESET1", 1)->isTruthy() : false;
        bool q = member("Q1").isTruthy();
        if (type == "SR") { if (reset) q = false; if (set) q = true; }
        else              { if (set) q = true;   if (reset) q = false; }
        store("Q1", Value::boolean(q));
        return true;
    }
    return false;
}

// --------------------------------------------------------------- functions ---
bool Runtime::runFunction(const std::string& name,
                          const std::vector<std::pair<std::string, Value>>& arguments,
                          Value& result) {
    const auto upper = upperOf(name);
    auto arg = [&](std::size_t i) -> Value {
        const auto* v = argument(arguments, "", i);
        if (v) return *v;
        return i < arguments.size() ? arguments[i].second : Value{};
    };

    // 1.10.2 (SIM) : STRING_TO_ASCII / ASCII_TO_STRING sur UNE case (un INT, un
    // WORD...). Le tableau entier - le cas ordinaire - est traite dans call()
    // (l'interpreteur y passe son nom) ; ici, le meme rangement : le 1er
    // caractere dans l'octet de poids faible, arret au premier octet nul.
    if (upper == "STRING_TO_ASCII" && arguments.size() == 1) {
        const auto s = arg(0).asString();
        std::int64_t packed = 0;
        for (std::size_t b = 0; b < 2 && b < s.size(); ++b)
            packed |= static_cast<std::int64_t>(static_cast<unsigned char>(s[b])) << (8 * b);
        result = Value::integer(Type::Int, packed);
        return true;
    }
    if (upper == "ASCII_TO_STRING" && arguments.size() == 1) {
        const auto in = arg(0);
        if (in.type() == Type::String) { result = in; return true; }
        const int bytes = std::max(1, bitWidth(in.type()) / 8);
        const auto bits = static_cast<std::uint64_t>(in.asInteger());
        std::string out;
        for (int b = 0; b < bytes; ++b) {
            const auto c = static_cast<char>((bits >> (8 * b)) & 0xFFu);
            if (c == '\0') break;
            out.push_back(c);
        }
        result = Value::text(std::move(out));
        return true;
    }

    // Conversions: X_TO_Y. Dispatching on the name rather than listing every
    // pair keeps this honest - the library file decides which exist, and this
    // only has to know how to perform one.
    if (const auto at = upper.find("_TO_"); at != std::string::npos && arguments.size() == 1) {
        // 1.11.25 : les types que le moteur n'a pas prennent leur type de calcul (celui de
        // typereg::simTypeOf) : LREAL un REAL, LINT un DINT, ULINT un UDINT, LWORD un DWORD ;
        // SINT et USINT un INT et un UINT ramenes a 8 bits. INT_TO_LREAL, REAL_TO_SINT...
        // n'existaient pas (fonction ou bloc inconnu du simulateur).
        const auto targetName = upper.substr(at + 4);
        auto target = typeFromName(targetName);
        int eightBits = 0;                                    // 1 : SINT, 2 : USINT
        if (target == Type::Unknown) {
            if (targetName == "LREAL") target = Type::Real;
            else if (targetName == "LINT") target = Type::DInt;
            else if (targetName == "ULINT") target = Type::UDInt;
            else if (targetName == "LWORD") target = Type::DWord;
            else if (targetName == "SINT") { target = Type::Int; eightBits = 1; }
            else if (targetName == "USINT") { target = Type::UInt; eightBits = 2; }
        }
        const auto narrow = [eightBits](std::int64_t v) -> std::int64_t {
            if (eightBits == 1) return static_cast<std::int8_t>(static_cast<std::uint8_t>(v & 0xFF));
            if (eightBits == 2) return v & 0xFF;
            return v;
        };
        const auto& in = arguments.front().second;
        // UNE CHAINE SE LIT. STRING_TO_INT('3') rendait 0 : asInteger() d'une
        // chaine ne la lit pas. Control Expert la lit en decimal, espaces de
        // tete ignores, signe permis.
        if (in.type() == Type::String && target != Type::String && target != Type::Unknown) {
            const auto text = in.asString();
            if (target == Type::Real) {
                result = Value::real(std::strtod(text.c_str(), nullptr));
            } else if (target == Type::Bool) {
                result = Value::boolean(std::strtoll(text.c_str(), nullptr, 10) != 0);
            } else {
                result = Value::integer(target, narrow(static_cast<std::int64_t>(
                                                    std::strtoll(text.c_str(), nullptr, 10))));
            }
            return true;
        }
        if (target == Type::String) { result = Value::text(in.display()); return true; }
        if (target == Type::Real)   { result = Value::real(in.asReal());  return true; }
        if (target != Type::Unknown) {
            // A REAL converted to an integer rounds, as IEC requires, rather
            // than truncating toward zero the way a C cast would.
            const auto raw = in.type() == Type::Real
                                 ? static_cast<std::int64_t>(std::llround(in.asReal()))
                                 : in.asInteger();
            result = Value::integer(target, narrow(raw));
            return true;
        }
    }

    if (upper == "ABS")  { const auto v = arg(0);
                           result = v.type() == Type::Real ? Value::real(std::fabs(v.asReal()))
                                                           : Value::integer(v.type(), std::llabs(v.asInteger()));
                           return true; }
    if (upper == "SQRT") { result = Value::real(std::sqrt(arg(0).asReal())); return true; }
    if (upper == "LN")   { result = Value::real(std::log(arg(0).asReal())); return true; }
    if (upper == "LOG")  { result = Value::real(std::log10(arg(0).asReal())); return true; }
    if (upper == "EXP")  { result = Value::real(std::exp(arg(0).asReal())); return true; }
    if (upper == "SIN")  { result = Value::real(std::sin(arg(0).asReal())); return true; }
    if (upper == "COS")  { result = Value::real(std::cos(arg(0).asReal())); return true; }
    if (upper == "TAN")  { result = Value::real(std::tan(arg(0).asReal())); return true; }
    // 1.11.25 : acceptees par Compiler (et par l'IHM) depuis longtemps, mais inconnues en marche
    // ("fonction ou bloc inconnu du simulateur") - ASIN, ACOS, ATAN, EXPT, TRUNC, ROUND, NEG.
    if (upper == "ASIN") { result = Value::real(std::asin(arg(0).asReal())); return true; }
    if (upper == "ACOS") { result = Value::real(std::acos(arg(0).asReal())); return true; }
    if (upper == "ATAN") { result = Value::real(std::atan(arg(0).asReal())); return true; }
    if (upper == "EXPT") { result = Value::real(std::pow(arg(0).asReal(), arg(1).asReal())); return true; }
    if (upper == "TRUNC") {   // vers zero : TRUNC(-2.7) = -2 ; un DINT
        result = Value::integer(Type::DInt, static_cast<std::int64_t>(std::trunc(arg(0).asReal())));
        return true;
    }
    if (upper == "ROUND") {   // au plus proche (2.5 -> 3) ; ROUND(x, n) : a n decimales ; un REAL
        const double x = arg(0).asReal();
        const auto n = arguments.size() > 1 ? std::clamp<std::int64_t>(arg(1).asInteger(), -15, 15) : 0;
        const double f = std::pow(10.0, static_cast<double>(n));
        result = Value::real(std::round(x * f) / f);
        return true;
    }
    if (upper == "NEG") {
        const auto v = arg(0);
        result = v.type() == Type::Real ? Value::real(-v.asReal()) : Value::integer(v.type(), -v.asInteger());
        return true;
    }

    // 1.11.25 : toutes les valeurs (MIN(3, 2, 1) rendait 2 : seules les deux premieres comptaient).
    if (upper == "MIN" || upper == "MAX") {
        if (arguments.empty()) return false;
        auto best = arg(0);
        for (std::size_t k = 1; k < arguments.size(); ++k) {
            const auto v = arg(k);
            if (upper == "MIN" ? v.compare(best) < 0 : v.compare(best) > 0) best = v;
        }
        result = best;
        return true;
    }
    if (upper == "LIMIT") {
        const auto mn = arg(0), in = arg(1), mx = arg(2);
        result = in.compare(mn) < 0 ? mn : (in.compare(mx) > 0 ? mx : in);
        return true;
    }
    if (upper == "SEL")  { result = arg(0).isTruthy() ? arg(2) : arg(1); return true; }
    if (upper == "MUX")  {
        const auto k = static_cast<std::size_t>(arg(0).asInteger());
        result = k + 1 < arguments.size() ? arg(k + 1) : arg(1);
        return true;
    }

    if (upper == "SHL" || upper == "SHR" || upper == "ROL" || upper == "ROR") {
        const auto in = arg(0);
        const auto n  = static_cast<int>(arg(1).asInteger());
        const auto width = bitWidth(in.type());
        const auto mask = width >= 64 ? ~0ULL : ((1ULL << width) - 1);
        auto bits = static_cast<std::uint64_t>(in.asInteger()) & mask;
        if (upper == "SHL") bits = (bits << n) & mask;
        else if (upper == "SHR") bits = (bits >> n) & mask;
        else if (width > 0 && width < 64) {
            const int shift = n % width;
            bits = upper == "ROL" ? ((bits << shift) | (bits >> (width - shift))) & mask
                                  : ((bits >> shift) | (bits << (width - shift))) & mask;
        }
        result = Value::integer(in.type(), static_cast<std::int64_t>(bits));
        return true;
    }

    // Strings, with Control Expert's typed names.
    if (upper == "LEN_INT" || upper == "LEN") {
        result = Value::integer(Type::Int, static_cast<std::int64_t>(arg(0).asString().size()));
        return true;
    }
    if (upper == "LEFT_INT" || upper == "LEFT") {
        const auto s = arg(0).asString();
        const auto n = static_cast<std::size_t>(std::max<std::int64_t>(0, arg(1).asInteger()));
        result = Value::text(s.substr(0, std::min(n, s.size())));
        return true;
    }
    if (upper == "RIGHT_INT" || upper == "RIGHT") {
        const auto s = arg(0).asString();
        const auto n = static_cast<std::size_t>(std::max<std::int64_t>(0, arg(1).asInteger()));
        result = Value::text(n >= s.size() ? s : s.substr(s.size() - n));
        return true;
    }
    if (upper == "MID_INT" || upper == "MID") {
        const auto s = arg(0).asString();
        const auto n = static_cast<std::size_t>(std::max<std::int64_t>(0, arg(1).asInteger()));
        const auto p = static_cast<std::size_t>(std::max<std::int64_t>(1, arg(2).asInteger()));
        result = Value::text(p - 1 >= s.size() ? std::string{} : s.substr(p - 1, n));
        return true;
    }
    if (upper == "CONCAT_STR" || upper == "CONCAT") {
        std::string out;
        for (const auto& [argName, value] : arguments) out += value.asString();
        result = Value::text(out);
        return true;
    }
    // 1.11.25 : INSERT, DELETE, REPLACE (les positions commencent a 1, comme MID) - acceptees
    // par Compiler, inconnues en marche.
    if (upper == "INSERT_INT" || upper == "INSERT") {   // IN2 dans IN1, apres le caractere P
        const auto s = arg(0).asString(), add = arg(1).asString();
        const auto p = static_cast<std::size_t>(std::clamp<std::int64_t>(arg(2).asInteger(), 0, static_cast<std::int64_t>(s.size())));
        result = Value::text(s.substr(0, p) + add + s.substr(p));
        return true;
    }
    if (upper == "DELETE_INT" || upper == "DELETE") {   // L caracteres a partir de P
        auto s = arg(0).asString();
        const auto n = static_cast<std::size_t>(std::max<std::int64_t>(0, arg(1).asInteger()));
        const auto p = static_cast<std::size_t>(std::max<std::int64_t>(1, arg(2).asInteger()));
        if (p - 1 < s.size()) s.erase(p - 1, n);
        result = Value::text(std::move(s));
        return true;
    }
    if (upper == "REPLACE_INT" || upper == "REPLACE") {   // L caracteres de IN1 a partir de P, remplaces par IN2
        auto s = arg(0).asString();
        const auto with = arg(1).asString();
        const auto n = static_cast<std::size_t>(std::max<std::int64_t>(0, arg(2).asInteger()));
        const auto p = static_cast<std::size_t>(std::max<std::int64_t>(1, arg(3).asInteger()));
        if (p - 1 <= s.size()) s.replace(p - 1, n, with);
        result = Value::text(std::move(s));
        return true;
    }
    if (upper == "FIND_INT" || upper == "FIND") {
        const auto at = arg(0).asString().find(arg(1).asString());
        result = Value::integer(Type::Int,
                                at == std::string::npos ? findNotFound_ : static_cast<std::int64_t>(at + 1));
        return true;
    }
    return false;
}

bool Runtime::assignAggregate(std::string_view target, std::string_view source) {
    auto resolve = [&](std::string_view name) -> const std::vector<std::string>* {
        if (!scopePrefix_.empty()) {
            auto local = aggregates_.find(lowerOf(scopePrefix_ + std::string(name)));
            if (local != aggregates_.end()) return &local->second;
        }
        auto it = aggregates_.find(lowerOf(name));
        return it == aggregates_.end() ? nullptr : &it->second;
    };
    auto qualifiedName = [&](std::string_view name) {
        if (!scopePrefix_.empty()
            && aggregates_.count(lowerOf(scopePrefix_ + std::string(name))) != 0)
            return scopePrefix_ + std::string(name);
        return std::string(name);
    };

    const auto* members = resolve(source);
    if (!members || members->empty()) return false;

    const auto from = qualifiedName(source);
    const auto to   = qualifiedName(target);
    std::size_t copied = 0;
    for (const auto& suffix : *members) {
        Value v;
        if (!get(from + suffix, v)) continue;
        if (write(to + suffix, v)) ++copied;
    }
    return copied > 0;
}

// A DFB instance: inputs in, body executed with the instance as its scope,
// outputs left where the caller can read them as members. This is what makes
// `Sensor(raw := %IW0.2.0); value := Sensor.scaled;` mean anything.
bool Runtime::runProjectBlock(const std::string& instance, const std::string& type,
                              const std::vector<std::pair<std::string, Value>>& arguments) {
    auto body = blockBodies_.find(type);

    // Inputs are written whether or not there is a body to run, so an
    // un-executable block still shows what it was given.
    std::size_t positional = 0;
    for (const auto& [argName, value] : arguments) {
        if (!argName.empty()) {
            if (auto* slot = find(instance + "." + argName)) {
                slot->value.assignFrom(value);
                noteWrite(*slot);                   // lot API 8 : la ligne de l'appel
            }
            continue;
        }
        // Positional arguments bind to the inputs in declaration order, which is
        // the order the pins were declared into the slot table.
        (void)positional++;
    }

    if (body == blockBodies_.end() || body->second.empty()) return false;

    if (callDepth_ >= kMaxCallDepth) {
        report(Diagnostic{Diagnostic::Severity::Error,
                          "'" + instance + "' nests function-block calls more than "
                              + std::to_string(kMaxCallDepth)
                              + " deep; a block probably instantiates itself",
                          0, type});
        return true;
    }

    // The instance becomes the scope, so the block's own declarations resolve to
    // its own copy of them and two instances never share state.
    const auto savedScope = scopePrefix_;
    scopePrefix_ = instance + ".";
    ++callDepth_;
    for (const auto& program : body->second) {
        // Lot API 8 : la pile d'un point d'arret - tenue seulement s'il y en a un.
        const bool framed = anyArmed_;
        if (framed) frames_.push_back(Frame{&instance, &type, programTag(*program)});
        const auto r = execute(*program, *this, scanLimits_);
        if (framed) frames_.pop_back();
        // RETURN rend la main a l'appelant : les sections suivantes du DFB ne
        // s'executent pas, comme dans Control Expert.
        if (!r.completed || r.returned) break;
    }
    --callDepth_;
    scopePrefix_ = savedScope;
    return true;
}

bool Runtime::call(std::string_view name, std::string_view instance,
                   const std::vector<std::pair<std::string, Value>>& arguments, Value& result) {
    std::string target(name);

    // 1.10.2 (SIM) : STRING_TO_ASCII / ASCII_TO_STRING SUR UN TABLEAU. Le tableau
    // n'a pas de valeur a lui : l'interpreteur passe son NOM dans `instance`
    // (different du nom de la fonction, qui est sinon ce qu'il y met) :
    //   tableau := STRING_TO_ASCII(chaine)  -> call("STRING_TO_ASCII", "tableau", {chaine})
    //   ASCII_TO_STRING(tableau)            -> call("ASCII_TO_STRING", "tableau", {})
    // Le rangement est celui d'une STRING dans des %MW d'un M580 : les cases dans
    // l'ordre des indices, le 1er caractere dans l'octet de poids faible de la 1re
    // case, autant de caracteres que la case a d'octets (INT, WORD : 2 ; BYTE : 1).
    // STRING_TO_ASCII : ce qui depasse le tableau est perdu, le reste vaut 0 (la
    // fin de la chaine) ; chaque case passe par write() (forcee, elle le reste).
    // ASCII_TO_STRING : les octets dans le meme ordre, jusqu'au premier nul.
    // Faux : pas un tableau de nombres a une dimension (l'appel ordinaire suit).
    if (!instance.empty() && instance != name) {
        const auto fn = upperOf(name);
        if (fn == "STRING_TO_ASCII" || fn == "ASCII_TO_STRING") {
            const std::vector<std::string>* members = nullptr;
            std::string base;
            if (!scopePrefix_.empty()) {
                auto local = aggregates_.find(lowerOf(scopePrefix_ + std::string(instance)));
                if (local != aggregates_.end()) { members = &local->second; base = scopePrefix_ + std::string(instance); }
            }
            if (!members) {
                auto it = aggregates_.find(lowerOf(instance));
                if (it == aggregates_.end()) return false;
                members = &it->second;
                base = std::string(instance);
            }
            std::vector<std::pair<long long, Slot*>> cells;   // (indice, case)
            for (const auto& suffix : *members) {
                if (suffix.size() < 3 || suffix.front() != '[' || suffix.back() != ']') return false;
                const auto digits = suffix.substr(1, suffix.size() - 2);
                char* end = nullptr;
                const auto index = std::strtoll(digits.c_str(), &end, 10);
                if (end != digits.c_str() + digits.size()) return false;   // [i,j], [i].membre
                auto* slot = find(base + suffix);
                if (!slot || !isInteger(slot->value.type())) return false;
                cells.emplace_back(index, slot);
            }
            if (cells.empty()) return false;
            std::sort(cells.begin(), cells.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });

            if (fn == "STRING_TO_ASCII") {
                if (arguments.size() != 1) return false;
                const auto& in = arguments.front().second;
                const auto text = in.type() == Type::String ? in.asString() : in.display();
                std::size_t at = 0;
                for (const auto& [index, slot] : cells) {
                    const auto type = slot->value.type();
                    const int bytes = std::max(1, bitWidth(type) / 8);
                    std::uint64_t packed = 0;
                    for (int b = 0; b < bytes; ++b, ++at)
                        if (at < text.size())
                            packed |= static_cast<std::uint64_t>(static_cast<unsigned char>(text[at])) << (8 * b);
                    (void)write(base + "[" + std::to_string(index) + "]",
                                Value::integer(type, static_cast<std::int64_t>(packed)));
                }
                result = Value::integer(Type::Int, 0);
                return true;
            }
            std::string text;
            for (const auto& [index, slot] : cells) {
                Value v;
                if (!get(base + "[" + std::to_string(index) + "]", v)) v = slot->value;   // le forcage d'abord
                const int bytes = std::max(1, bitWidth(v.type()) / 8);
                const auto bits = static_cast<std::uint64_t>(v.asInteger());
                bool ended = false;
                for (int b = 0; b < bytes && !ended; ++b) {
                    const auto c = static_cast<char>((bits >> (8 * b)) & 0xFFu);
                    if (c == '\0') ended = true;
                    else text.push_back(c);
                }
                if (ended) break;
            }
            result = Value::text(std::move(text));
            return true;
        }
    }

    // An instance call: the name is a declared variable of a block type.
    //
    // Lot API 7 : L'INSTANCE D'UNE UNITE (ou d'un bloc). Dans une section de
    // Logigrammes_A, `Builder(...)` appelle Logigrammes_A.Builder : la portee
    // d'abord, comme pour une lecture, puis le nom tel quel, puis sans la casse.
    // Sans cela, les grafcets des unites ne tournaient jamais.
    auto it = instanceTypes_.end();
    if (!scopePrefix_.empty()) {
        it = instanceTypes_.find(scopePrefix_ + target);
        if (it == instanceTypes_.end())
            if (const auto low = instanceLower_.find(lowerOf(scopePrefix_ + target)); low != instanceLower_.end())
                it = instanceTypes_.find(low->second);
    }
    if (it == instanceTypes_.end()) it = instanceTypes_.find(target);
    if (it == instanceTypes_.end())
        if (const auto low = instanceLower_.find(lowerOf(target)); low != instanceLower_.end()) it = instanceTypes_.find(low->second);
    if (it != instanceTypes_.end()) target = it->first;
    if (it != instanceTypes_.end()) {
        // Lot API 8 : une condition de point d'arret ne fait pas tourner de bloc.
        if (probing_ > 0) return false;
        if (runStandardBlock(target, it->second, arguments)) {
            result = Value::boolean(true);
            return true;
        }
        if (runProjectBlock(target, it->second, arguments)) {
            result = Value::boolean(true);
            return true;
        }
        // No body to run: the inputs were still stored. Reported once per
        // instance rather than once per scan, or a project with 34 instances
        // produces 34 identical lines every 20 ms.
        if (std::find(memoryWarnings_.begin(), memoryWarnings_.end(), target)
            == memoryWarnings_.end()) {
            memoryWarnings_.push_back(target);
            report(Diagnostic{Diagnostic::Severity::Warning,
                              "'" + target + "' is an instance of '" + it->second
                                  + "', which has no Structured Text body to execute; its inputs "
                                    "are stored and its outputs hold their last value",
                              0, {}});
        }
        result = Value::boolean(false);
        return true;
    }

    return runFunction(target, arguments, result);
}

// -------------------------------------------------------------------- scan ---
ScanReport Runtime::step(std::int64_t deltaMilliseconds) {
    // Lot API 8 : un cycle commence au pas a pas se finit ici (son horloge a
    // deja avance) ; sinon, un cycle entier.
    if (!scanOpen_) beginScan(deltaMilliseconds);
    armDeadline();
    bool halted = false;
    while (!halted && nextRunnable_ < programs_.size()) runRunnable(nextRunnable_++, halted);
    return finishScan(halted);
}

// Lot API 8 : UNE ENTREE DE MAST (une section, ou une unite en bloc), puis la
// main revient. Le cycle se finit apres la derniere (ou sur une halte).
ScanReport Runtime::stepEntry(std::int64_t deltaMilliseconds) {
    if (!scanOpen_) beginScan(deltaMilliseconds);
    armDeadline();
    bool halted = false;
    const auto statementsBefore = scanTopStatements_;
    const auto sectionsBefore = scanSectionsRun_;
    if (nextRunnable_ < programs_.size()) {
        const auto entry = programs_[nextRunnable_].entry;
        while (!halted && nextRunnable_ < programs_.size() && programs_[nextRunnable_].entry == entry)
            runRunnable(nextRunnable_++, halted);
    }
    if (halted || nextRunnable_ >= programs_.size()) return finishScan(halted);

    ScanReport out;
    scopePrefix_.clear();
    currentRunnable_ = static_cast<std::size_t>(-1);
    out.scan = static_cast<std::uint32_t>(currentScan_);
    out.clockMs = clockMs_;
    out.statements = scanTopStatements_ - statementsBefore;
    out.sectionsRun = scanSectionsRun_ - sectionsBefore;
    out.diagnostics.assign(scanDiagnostics_.begin() + static_cast<std::ptrdiff_t>(diagReported_), scanDiagnostics_.end());
    diagReported_ = scanDiagnostics_.size();
    out.completed = false;
    // Un point d'arret passe dans cette entree : dit tout de suite (le pas a pas
    // est deja en pause) ; un autre, plus loin dans le cycle, le sera a son tour.
    if (scanHit_) {
        pendingHit_ = std::move(scanHit_);
        scanHit_.reset();
    }
    return out;
}

void Runtime::beginScan(std::int64_t deltaMilliseconds) {
    scanDiagnostics_.clear();
    diagReported_ = 0;
    halted_ = false;
    deltaMs_ = std::max<std::int64_t>(1, deltaMilliseconds);
    clockMs_ += deltaMs_;
    currentScan_ = static_cast<std::uint64_t>(scans_) + 1;
    // Lot API 7 : un seul budget pour le cycle, blocs compris - 2 millions
    // d'instructions ou 1,5 s, ce qui arrive en premier.
    scanStatements_ = 0;
    scanTopStatements_ = 0;
    scanSectionsRun_ = 0;
    scanLimits_.scanStatements = &scanStatements_;
    scanLimits_.maxScanStatements = maxScanStatementsSetting_;
    scanLimits_.trace = &trace_;                 // lot API 8 : qui a ecrit
    trace_ = {};
    nextRunnable_ = 0;
    scanHit_.reset();
    if (pendingTimes_.size() != programs_.size()) pendingTimes_.assign(programs_.size(), {});
    scanOpen_ = true;
}

// L'heure limite vaut pour ce qui s'execute maintenant : au pas a pas, le temps
// passe en pause entre deux entrees ne compte pas.
void Runtime::armDeadline() {
    scanLimits_.hasDeadline = maxScanMsSetting_ > 0;
    scanLimits_.deadlineMicros = monotonicMicros() + (scanLimits_.hasDeadline ? maxScanMsSetting_ * 1000 : 0);
}

void Runtime::runRunnable(std::size_t index, bool& halted) {
    // 1.10.2 : une unite reliee - ses entrees avant sa premiere section, ses
    // sorties apres sa derniere (une halte au milieu : rien ne sort).
    const auto entry = programs_[index].entry;
    const UnitLinks* links = entry < unitLinks_.size() ? &unitLinks_[entry] : nullptr;
    if (links && (index == 0 || programs_[index - 1].entry != entry)) copyParameters(links->in, false);
    runSection(index, halted);
    if (links && !halted && (index + 1 >= programs_.size() || programs_[index + 1].entry != entry)) {
        copyParameters(links->out, true);
        copyParameters(links->mirror, false);
    }
}

void Runtime::runSection(std::size_t index, bool& halted) {
    auto& runnable = programs_[index];
    if (!runnable.program) return;
    scopePrefix_ = runnable.scope;
    currentSection_ = runnable.section;
    currentRunnable_ = index;
    // 1.10.2 : la condition d'activation, lue dans la portee de la section. Une
    // condition qui ne se calcule pas (une variable inconnue) : la section tourne.
    if (runnable.condition) {
        const auto truth = evaluate(*runnable.condition, *this);
        runnable.active = !truth || truth->isTruthy();
        if (!runnable.active) {
            // 1.11 : "inactive" va avec les temps de CE cycle (micros a -1) : apres une
            // halte, la page lit les deux dans le meme cycle complet (sectionTimes).
            pendingTimes_[index] = {0, -1};
            return;
        }
    }
    // Lot API 8 : ou passe le temps - les instructions de toute la section,
    // blocs appeles compris (le compteur partage du cycle), et sa duree.
    const auto t0 = monotonicMicros();
    const auto s0 = scanStatements_;
    const auto r = execute(*runnable.program, *this, scanLimits_);
    pendingTimes_[index] = {scanStatements_ - s0, monotonicMicros() - t0};
    scanTopStatements_ += r.statements;
    ++scanSectionsRun_;
    if (!r.completed) halted = true;
}

ScanReport Runtime::finishScan(bool halted) {
    ScanReport out;
    scopePrefix_.clear();
    currentRunnable_ = static_cast<std::size_t>(-1);
    frames_.clear();

    // Trend samples, taken after the scan so a graph shows the state the outputs
    // were left in rather than a value from the middle of the logic.
    for (auto& [name, samples] : history_) {
        Value v;
        if (!get(name, v)) continue;
        samples.push_back(Sample{clockMs_, v.asReal()});
        while (samples.size() > historyDepth_) samples.pop_front();
    }

    // Lot API 8 : le cycle ne compte qu'une fois fini.
    scans_ = static_cast<std::uint32_t>(currentScan_);
    out.clockMs = clockMs_;
    out.scan = scans_;
    out.statements = scanTopStatements_;
    out.sectionsRun = scanSectionsRun_;
    out.halted = halted;
    out.diagnostics.assign(scanDiagnostics_.begin() + static_cast<std::ptrdiff_t>(diagReported_), scanDiagnostics_.end());
    diagReported_ = scanDiagnostics_.size();
    if (!halted) lastTimes_ = pendingTimes_;      // un cycle complet
    scanOpen_ = false;
    nextRunnable_ = 0;
    if (!breakInfo_.empty()) rollConditionTruth();   // lot API 8 : "devient vraie"
    if (scanHit_) {
        pendingHit_ = std::move(scanHit_);
        scanHit_.reset();
    }
    return out;
}

// Lot API 7 : voir Runtime.hpp. Les noeuds d'une unordered_map ne bougent pas
// quand elle grandit : l'adresse de chaque valeur reste la meme.
void Runtime::forEachSlot(const std::function<void(const std::string& name, const Value& value)>& fn) const {
    if (!fn) return;
    for (const auto& [name, slot] : slots_) fn(name, slot.value);
}

// Lot API 8 : QUI A ECRIT. Le nom tel que l'onglet Automate le montre - complet
// (Unite.variable, Armoires[0].ana.PT1.mes), sans la casse -, une adresse
// indexee (%MW0[57]) ou un bit de mot (%MW100.3 : le mot). Une adresse situee
// renvoie a la variable qui y est : c'est elle que le programme ecrit.
bool Runtime::lastWrite(std::string_view path, WriteSite& out) const {
    while (!path.empty() && std::isspace(static_cast<unsigned char>(path.front()))) path.remove_prefix(1);
    while (!path.empty() && std::isspace(static_cast<unsigned char>(path.back()))) path.remove_suffix(1);
    if (path.empty()) return false;
    const std::string noScope;
    std::string word;
    auto it = resolveKey(slots_, canonical_, noScope, path);
    if (it == slots_.end()) {
        int bit = 0;
        if (splitBitAccess(path, word, bit)) it = resolveKey(slots_, canonical_, noScope, word);
    }
    const Slot* slot = it == slots_.end() ? nullptr : &it->second;
    if ((!slot || !slot->written) && path.front() == '%') {
        auto at = locatedAt_.find(normaliseAddress(path));
        if (at == locatedAt_.end() && !word.empty()) at = locatedAt_.find(normaliseAddress(word));
        if (at != locatedAt_.end())
            if (const auto v = slots_.find(at->second); v != slots_.end() && v->second.written) slot = &v->second;
    }
    if (!slot || !slot->written) return false;
    out.section = slot->writerTag < tagNames_.size() ? tagNames_[slot->writerTag] : std::string{};
    out.line = slot->writerLine;
    out.scan = slot->writerScan;
    return true;
}

// Lot API 8 : LE PROGRAMME DIRAIT - la case elle-meme (Slot.value), pas son
// forcage. Les memes chemins que lastWrite (le nom complet, sans la casse ; un
// bit de mot : le bit du mot).
bool Runtime::programValue(std::string_view path, Value& out) const {
    while (!path.empty() && std::isspace(static_cast<unsigned char>(path.front()))) path.remove_prefix(1);
    while (!path.empty() && std::isspace(static_cast<unsigned char>(path.back()))) path.remove_suffix(1);
    if (path.empty()) return false;
    const std::string noScope;
    if (const auto it = resolveKey(slots_, canonical_, noScope, path); it != slots_.end()) {
        out = it->second.value;
        return true;
    }
    std::string word;
    int bit = 0;
    if (splitBitAccess(path, word, bit) && bit >= 0 && bit < 64) {
        if (const auto it = resolveKey(slots_, canonical_, noScope, word); it != slots_.end()) {
            out = Value::boolean(((it->second.value.asInteger() >> bit) & 1) != 0);
            return true;
        }
    }
    return false;
}

} // namespace sim
