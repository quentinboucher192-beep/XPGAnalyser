// =============================================================================
//  project/MastImport.cpp - lot 7 : importer un .XPG dans le projet ouvert
//  (voir l'en-tete : le plan, la fusion, la commande).
// =============================================================================
#include "MastImport.hpp"

#include "ApiCommands.hpp"      // taskKindLabel, defaultAnimationTableOwner
#include "MemberTree.hpp"
#include "../import/ProjectImporter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string_view>

namespace project::mast {

using namespace domain;
namespace m = project::members;

namespace {

// "->" en typographie : une fleche.
constexpr const char* kArrow = " \xE2\x86\x92 ";
// Le point median des details : "INT \xC2\xB7 %MW12".
constexpr const char* kDot = " \xC2\xB7 ";

std::string lowerOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Deux ecritures du meme type : "ARRAY[0..1] OF armoire" et "array[0..1] of ARMOIRE".
std::string typeKey(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string addressKey(std::string_view raw) { return lowerOf(trimmed(raw)); }

// La racine d'un chemin : "Armoires" pour "Armoires[0].ana.PT1".
std::string_view rootOf(std::string_view path) {
    std::size_t end = 0;
    while (end < path.size() && path[end] != '.' && path[end] != '[') ++end;
    return path.substr(0, end);
}

bool allDigits(std::string_view s) {
    if (s.empty()) return false;
    for (const char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

std::string text(const Project& p, SymbolId id) { return std::string(p.strings.text(id)); }
std::string nameOf(const Project& p, const Variable& v) { return text(p, v.name); }
std::string typeOf(const Project& p, const Variable& v) { return text(p, v.type.name); }

// Les variables globales d'un projet, par leur nom en minuscules (la premiere
// l'emporte : Control Expert n'en accepte pas deux).
using Globals = std::map<std::string, Index>;
Globals globalsOf(const Project& p) {
    Globals out;
    for (Index i = 0; i < p.variables.size(); ++i)
        if (p.variables[i].scope == VariableScope::Global) out.emplace(lowerOf(p.strings.text(p.variables[i].name)), i);
    return out;
}

// L'adresse (en minuscules) -> le nom de la globale qui y vit.
std::map<std::string, std::string> addressesOf(const Project& p) {
    std::map<std::string, std::string> out;
    for (const auto& v : p.variables)
        if (v.scope == VariableScope::Global && !trimmed(v.address.raw).empty())
            out.emplace(addressKey(v.address.raw), nameOf(p, v));
    return out;
}

// Le type des elements d'un tableau (d'un tableau de tableaux...) ; le type
// lui-meme sinon.
std::string elementOf(std::string_view type) {
    std::string t = trimmed(type);
    for (int depth = 0; depth < 8; ++depth) {
        const auto shape = m::parseArray(t);
        if (!shape.valid()) break;
        t = trimmed(shape.element);
    }
    return t;
}

bool isProjectDfb(const Project& p, std::string_view type) {
    const auto key = lowerOf(trimmed(type));
    return std::any_of(p.pous.begin(), p.pous.end(), [&](const Pou& pou) {
        return pou.kind == PouKind::FunctionBlockType && lowerOf(p.strings.text(pou.name)) == key;
    });
}

// Une instance de DDT ou de bloc (un tableau d'instances compris) ?
bool isInstance(const Project& p, std::string_view type) {
    const auto n = m::natureOf(p, elementOf(type));
    return n == m::Nature::Structure || n == m::Nature::Block;
}

// Le sous-groupe d'une instance : son type.
std::string instanceSub(const Project& p, std::string_view type) {
    const auto e = elementOf(type);
    switch (m::natureOf(p, e)) {
        case m::Nature::Structure: return e + " (DDT)";
        case m::Nature::Block:     return e + (isProjectDfb(p, e) ? " (DFB)" : " (bloc standard)");
        default:                   return e;
    }
}

// Le genre d'une variable elementaire (le sous-groupe des variables globales).
struct Genre { int rank; const char* label; };
Genre genreOf(std::string_view type) {
    const auto t = typeKey(type);
    if (t.rfind("ARRAY", 0) == 0) return {6, "Tableaux"};
    if (t == "BOOL" || t == "EBOOL") return {0, "Bool\xC3\xA9" "ens"};
    static const char* ints[] = {"INT", "UINT", "DINT", "UDINT", "SINT", "USINT", "LINT", "ULINT"};
    for (const char* k : ints)
        if (t == k) return {1, "Entiers"};
    static const char* words[] = {"BYTE", "WORD", "DWORD", "LWORD"};
    for (const char* k : words)
        if (t == k) return {2, "Mots et octets"};
    if (t == "REAL" || t == "LREAL") return {3, "R\xC3\xA9" "els"};
    static const char* times[] = {"TIME", "DATE", "TOD", "DT", "TIME_OF_DAY", "DATE_AND_TIME"};
    for (const char* k : times)
        if (t == k) return {4, "Temps et dates"};
    if (t.rfind("STRING", 0) == 0) return {5, "Cha\xC3\xAEnes"};
    return {7, "Autres"};
}

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n > 1 ? many : one);
}

// Au plus `max` morceaux, puis "..." et le reste compte.
std::string joinParts(const std::vector<std::string>& parts, std::size_t max = 5) {
    std::string out;
    for (std::size_t i = 0; i < parts.size() && i < max; ++i) out += (i ? ", " : "") + parts[i];
    if (parts.size() > max) out += ", \xE2\x80\xA6 (+" + std::to_string(parts.size() - max) + ")";
    return out;
}

// ------------------------------------------------------ suivre un chemin ----
// Le type atteint en suivant `rest` (".ana.PT1.mes", "[0].ana", "[2,5]") depuis
// `type`. Faux : un morceau manque (`why` dit lequel). Un type inconnu du
// projet et de la bibliotheque est suivi sans rien dire.
bool follow(const Project& p, std::string type, std::string_view rest, std::string* why) {
    for (int guard = 0; !rest.empty() && guard < 64; ++guard) {
        if (rest.front() == '[') {
            const auto close = rest.find(']');
            if (close == std::string_view::npos) return true;        // mal forme : rien a en dire
            const auto shape = m::parseArray(type);
            if (!shape.valid()) {
                if (m::natureOf(p, type) == m::Nature::Unknown) return true;
                if (why) *why = type + " n'est pas un tableau";
                return false;
            }
            // Les indices ecrits en chiffres : dans les bornes (une expression,
            // un nom : rien a verifier).
            std::vector<std::string> parts;
            std::string cur;
            for (const char c : rest.substr(1, close - 1)) {
                if (c == ',') { parts.push_back(trimmed(cur)); cur.clear(); }
                else cur += c;
            }
            parts.push_back(trimmed(cur));
            if (parts.size() == shape.dims.size()) {
                for (std::size_t k = 0; k < parts.size(); ++k) {
                    const auto& s = parts[k];
                    const bool negative = !s.empty() && s.front() == '-';
                    if (!allDigits(negative ? std::string_view(s).substr(1) : std::string_view(s))) continue;
                    const auto value = static_cast<std::int64_t>(std::strtoll(s.c_str(), nullptr, 10));
                    const auto [lo, hi] = shape.dims[k];
                    if (value < lo || value > hi) {
                        if (why) *why = "l'indice " + s + " sort de [" + std::to_string(lo) + ".." + std::to_string(hi) + "]";
                        return false;
                    }
                }
            }
            type = trimmed(shape.element);
            rest = rest.substr(close + 1);
            continue;
        }
        if (rest.front() == '.') {
            std::size_t end = 1;
            while (end < rest.size() && rest[end] != '.' && rest[end] != '[') ++end;
            const std::string name(rest.substr(1, end - 1));
            rest = rest.substr(end);
            if (name.empty()) return true;
            if (allDigits(name)) { type = "BOOL"; continue; }     // un bit d'un mot : Mot.3
            const auto nature = m::natureOf(p, type);
            if (nature == m::Nature::Unknown) return true;
            if (nature == m::Nature::Elementary || nature == m::Nature::Array) {
                if (why) *why = type + " n'a pas de champ " + name;
                return false;
            }
            const auto kids = m::children(p, m::root("x", type));
            const auto key = lowerOf(name);
            const m::Node* hit = nullptr;
            for (const auto& k : kids)
                if (k.label.size() > 1 && lowerOf(std::string_view(k.label).substr(1)) == key) { hit = &k; break; }
            if (!hit) {
                if (kids.empty()) return true;                       // un bloc sans ses broches connues
                if (why) *why = std::string(nature == m::Nature::Structure ? "pas de champ " : "pas de membre ") + name + " dans " + type;
                return false;
            }
            type = hit->type;
            continue;
        }
        return true;                                                 // autre chose : pas a nous d'en juger
    }
    return true;
}

// OU UN NOM SE CHERCHE. L'IHM ne lit que les globales ; une ligne de table
// d'animation lit aussi les variables de l'unite de programme qui porte la
// table ("Gc_SurpressionA.debug.stepfocus") - une variable de l'unite passe
// devant une globale du meme nom.
struct Scope {
    const Project* p{nullptr};
    const Globals* globals{nullptr};
    Globals        locals;
    [[nodiscard]] Index find(std::string_view root) const {
        const auto key = lowerOf(root);
        if (const auto it = locals.find(key); it != locals.end()) return it->second;
        if (globals)
            if (const auto it = globals->find(key); it != globals->end()) return it->second;
        return kNoIndex;
    }
};

// Les variables d'une unite de programme (ses parametres, ses locales).
Globals unitVariables(const Project& p, std::string_view unit) {
    Globals out;
    if (unit.empty()) return out;
    const auto key = lowerOf(unit);
    for (const auto& pou : p.pous) {
        if (pou.kind != PouKind::ProgramUnit || lowerOf(p.strings.text(pou.name)) != key) continue;
        for (const auto v : pou.parameters)
            if (v < p.variables.size()) out.emplace(lowerOf(p.strings.text(p.variables[v].name)), v);
        for (const auto v : pou.locals)
            if (v < p.variables.size()) out.emplace(lowerOf(p.strings.text(p.variables[v].name)), v);
        break;
    }
    return out;
}

// Un chemin : sa racine (une variable de la portee), puis ses membres. Une
// adresse directe (%M12, %MW100) existe toujours : c'est la memoire de l'automate.
bool resolve(const Scope& s, std::string_view path, std::string* why, Index* var = nullptr) {
    const std::string whole = trimmed(path);
    if (!whole.empty() && whole.front() == '%') return true;
    const auto root = rootOf(whole);            // une vue sur `whole`, qui vit jusqu'au bout
    if (root.empty()) {
        if (why) *why = "chemin vide";
        return false;
    }
    const auto index = s.find(root);
    if (index == kNoIndex || index >= s.p->variables.size()) {
        if (why) *why = "pas de variable " + std::string(root);
        return false;
    }
    if (var) *var = index;
    return follow(*s.p, typeOf(*s.p, s.p->variables[index]), std::string_view(whole).substr(root.size()), why);
}

bool resolve(const Project& p, const Globals& g, std::string_view path, std::string* why, Index* var = nullptr) {
    return resolve(Scope{&p, &g, {}}, path, why, var);
}

// Ce qui change d'une globale a l'autre (type, adresse) ; "" : rien.
std::string variableChange(const Project& a, const Variable& va, const Project& b, const Variable& vb) {
    std::vector<std::string> parts;
    const auto ta = typeOf(a, va), tb = typeOf(b, vb);
    if (typeKey(ta) != typeKey(tb)) parts.push_back(ta + kArrow + tb);
    const auto aa = trimmed(va.address.raw), ab = trimmed(vb.address.raw);
    if (addressKey(aa) != addressKey(ab))
        parts.push_back((aa.empty() ? std::string("non localis\xC3\xA9" "e") : aa) + kArrow
                        + (ab.empty() ? std::string("non localis\xC3\xA9" "e") : ab));
    return joinParts(parts);
}

// La meme variable des deux cotes : ce qui la change ; "" : identique (ou absente).
std::string rootChange(const Scope& a, const Scope& b, std::string_view root) {
    const auto ia = a.find(root), ib = b.find(root);
    if (ia == kNoIndex || ib == kNoIndex || ia >= a.p->variables.size() || ib >= b.p->variables.size()) return {};
    return variableChange(*a.p, a.p->variables[ia], *b.p, b.p->variables[ib]);
}

std::string rootChange(const Project& a, const Globals& ga, const Project& b, const Globals& gb, std::string_view root) {
    return rootChange(Scope{&a, &ga, {}}, Scope{&b, &gb, {}}, root);
}

// --------------------------------------------------- comparer les types ----
struct Member { std::string name, type, scope; };

const char* scopeWord(VariableScope s) {
    switch (s) {
        case VariableScope::Input:  return "entr\xC3\xA9" "e";
        case VariableScope::Output: return "sortie";
        case VariableScope::InOut:  return "entr\xC3\xA9" "e-sortie";
        case VariableScope::Public: return "publique";
        case VariableScope::Local:  return "priv\xC3\xA9" "e";
        default:                    return "";
    }
}

std::vector<Member> fieldsOf(const Project& p, const DerivedType& d) {
    std::vector<Member> out;
    for (const auto f : d.fields)
        if (f < p.variables.size()) out.push_back({nameOf(p, p.variables[f]), typeOf(p, p.variables[f]), {}});
    return out;
}

std::vector<Member> interfaceOf(const Project& p, const Pou& pou) {
    std::vector<Member> out;
    for (const auto v : pou.parameters)
        if (v < p.variables.size()) out.push_back({nameOf(p, p.variables[v]), typeOf(p, p.variables[v]), scopeWord(p.variables[v].scope)});
    for (const auto v : pou.locals)
        if (v < p.variables.size()) out.push_back({nameOf(p, p.variables[v]), typeOf(p, p.variables[v]), scopeWord(p.variables[v].scope)});
    return out;
}

// "+champ, -autre, seuil : INT -> REAL" ; "" : les memes.
std::string membersDiff(const std::vector<Member>& before, const std::vector<Member>& after) {
    std::map<std::string, const Member*> a, b;
    for (const auto& x : before) a.emplace(lowerOf(x.name), &x);
    for (const auto& x : after) b.emplace(lowerOf(x.name), &x);
    std::vector<std::string> parts;
    for (const auto& x : after)
        if (!a.count(lowerOf(x.name))) parts.push_back("+" + x.name);
    for (const auto& x : before)
        if (!b.count(lowerOf(x.name))) parts.push_back("-" + x.name);
    for (const auto& x : before) {
        const auto it = b.find(lowerOf(x.name));
        if (it == b.end()) continue;
        if (typeKey(x.type) != typeKey(it->second->type)) parts.push_back(x.name + " : " + x.type + kArrow + it->second->type);
        else if (x.scope != it->second->scope) parts.push_back(x.name + " : " + x.scope + kArrow + it->second->scope);
    }
    if (parts.empty() && before.size() == after.size()) {
        for (std::size_t i = 0; i < before.size(); ++i)
            if (lowerOf(before[i].name) != lowerOf(after[i].name)) {
                parts.emplace_back("l'ordre change");
                break;
            }
    }
    return joinParts(parts);
}

// Le code d'un bloc ou d'une unite : ses sections, par nom.
std::map<std::string, const Section*> codeOf(const Project& p, const Pou& pou) {
    std::map<std::string, const Section*> out;
    for (const auto s : pou.sections)
        if (s < p.sections.size()) out.emplace(lowerOf(p.strings.text(p.sections[s].name)), &p.sections[s]);
    return out;
}

bool sameSection(const Project& a, const Section& sa, const Project& b, const Section& sb) {
    return sa.body == sb.body && sa.language == sb.language && sa.isSubroutine == sb.isSubroutine
        && lowerOf(a.strings.text(sa.task)) == lowerOf(b.strings.text(sb.task))
        && trimmed(a.strings.text(sa.activationCondition)) == trimmed(b.strings.text(sb.activationCondition));
}

// "code : 2 sections modifiees, 1 ajoutee" ; "" : le meme.
std::string codeDiff(const Project& a, const Pou& pa, const Project& b, const Pou& pb) {
    const auto ca = codeOf(a, pa), cb = codeOf(b, pb);
    std::size_t added = 0, removed = 0, modified = 0;
    for (const auto& [name, s] : cb) {
        const auto it = ca.find(name);
        if (it == ca.end()) ++added;
        else if (!sameSection(a, *it->second, b, *s)) ++modified;
    }
    for (const auto& [name, s] : ca)
        if (!cb.count(name)) ++removed;
    std::vector<std::string> parts;
    if (modified) parts.push_back(plural(modified, "section modifi\xC3\xA9" "e", "sections modifi\xC3\xA9" "es"));
    if (added) parts.push_back(plural(added, "ajout\xC3\xA9" "e", "ajout\xC3\xA9" "es"));
    if (removed) parts.push_back(plural(removed, "retir\xC3\xA9" "e", "retir\xC3\xA9" "es"));
    return parts.empty() ? std::string{} : "code : " + joinParts(parts);
}

std::size_t lineCount(const std::string& body) {
    if (body.empty()) return 0;
    return static_cast<std::size_t>(std::count(body.begin(), body.end(), '\n')) + (body.back() == '\n' ? 0u : 1u);
}

// Une section "de tache" (pas le corps d'un DFB ni d'une unite).
bool taskSection(const Project& p, const Section& s) {
    return s.owner >= p.pous.size() || p.pous[s.owner].kind == PouKind::Section || p.pous[s.owner].kind == PouKind::Program;
}

std::string sectionSub(const Project& p, const Section& s) {
    if (s.isSubroutine) return "Sous-routines";
    const auto task = text(p, s.task);
    return task.empty() ? std::string("Sections") : "Sections de " + task;
}

const AnimationTable* tableNamed(const Project& p, std::string_view name) {
    const auto key = lowerOf(name);
    for (const auto& t : p.animationTables)
        if (lowerOf(p.strings.text(t.name)) == key) return &t;
    return nullptr;
}

bool unitExists(const Project& p, std::string_view name) {
    const auto key = lowerOf(name);
    return std::any_of(p.pous.begin(), p.pous.end(), [&](const Pou& pou) {
        return pou.kind == PouKind::ProgramUnit && lowerOf(p.strings.text(pou.name)) == key;
    });
}

// ------------------------------------ les tables d'animation, option cochee ----
//  Les tables du projet restent (leurs lignes IHM aussi) ; les lignes d'une
//  variable disparue partent ; une table du meme nom dans le .XPG ajoute ses
//  lignes qui manquent ; les tables du .XPG seulement s'ajoutent. Le plan et
//  la fusion lisent la MEME liste : ils ne peuvent pas se contredire.
struct TableLine { std::string text; bool hmi{false}; Status status{Status::Kept}; std::string detail; };
struct TablePlan { std::string name, owner; std::vector<TableLine> lines; bool fromXpg{false}; };

std::vector<TablePlan> keptTables(const Project& cur, const Globals& gc, const Project& imp, const Globals& gi) {
    std::vector<TablePlan> out;
    const std::string fallbackOwner = defaultAnimationTableOwner(imp);
    for (const auto& t : cur.animationTables) {
        TablePlan tp;
        tp.name = text(cur, t.name);
        tp.owner = text(cur, t.owner);
        // Les lignes se lisent dans l'unite qui porte la table, des deux cotes.
        const Scope before{&cur, &gc, unitVariables(cur, tp.owner)};
        const Scope after{&imp, &gi, unitVariables(imp, tp.owner)};
        const auto* x = tableNamed(imp, tp.name);
        if (!tp.owner.empty() && !unitExists(imp, tp.owner)) tp.owner = x ? text(imp, x->owner) : fallbackOwner;
        std::set<std::string> present;
        for (const auto& e : t.entries) {
            TableLine line;
            line.text = text(cur, e.name);
            line.hmi = e.hmi;
            if (e.hmi) {
                line.detail = "ligne IHM";
            } else {
                std::string why;
                if (resolve(after, line.text, &why)) {
                    present.insert(lowerOf(line.text));
                    line.detail = rootChange(before, after, rootOf(line.text));
                    if (!line.detail.empty()) line.status = Status::Changed;
                } else {
                    line.status = Status::Removed;
                    line.detail = "la ligne part : " + why;
                }
            }
            tp.lines.push_back(std::move(line));
        }
        if (x)
            for (const auto& e : x->entries) {
                if (e.hmi) continue;
                const auto s = text(imp, e.name);
                if (!present.insert(lowerOf(s)).second) continue;
                tp.lines.push_back({s, false, Status::Added, "ligne de la table du .XPG"});
            }
        out.push_back(std::move(tp));
    }
    for (const auto& x : imp.animationTables) {
        if (tableNamed(cur, imp.strings.text(x.name))) continue;
        TablePlan tp;
        tp.name = text(imp, x.name);
        tp.owner = text(imp, x.owner);
        tp.fromXpg = true;
        for (const auto& e : x.entries) tp.lines.push_back({text(imp, e.name), e.hmi, Status::Added, {}});
        out.push_back(std::move(tp));
    }
    return out;
}

// La documentation d'une globale identique, prise dans le projet.
bool identical(const Project& a, const Variable& va, const Project& b, const Variable& vb) {
    return lowerOf(a.strings.text(va.name)) == lowerOf(b.strings.text(vb.name))
        && typeKey(typeOf(a, va)) == typeKey(typeOf(b, vb))
        && addressKey(va.address.raw) == addressKey(vb.address.raw);
}

std::string hardwareSummary(const HardwareConfig& hw) {
    std::size_t modules = 0;
    for (const auto& r : hw.racks) modules += r.modules.size();
    if (hw.racks.empty()) return "aucun rack (importe le .XHW pour les voir)";
    return plural(hw.racks.size(), "rack", "racks") + ", " + plural(modules, "module", "modules");
}

std::string cpuKey(std::string_view s) {
    std::string out;
    for (const char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

// ================================================================ libelles ====
const char* statusLabel(Status s) noexcept {
    switch (s) {
        case Status::Kept:     return "Gard\xC3\xA9";
        case Status::Changed:  return "Chang\xC3\xA9";
        case Status::Removed:  return "Supprim\xC3\xA9";
        case Status::Added:    return "Nouveau";
        case Status::LinkLost: return "Liens IHM perdus";
    }
    return "";
}

const char* groupLabel(Group g) noexcept {
    switch (g) {
        case Group::Variables: return "Variables globales";
        case Group::Instances: return "Instances de DFB/DDT";
        case Group::Types:     return "Types (DDT et DFB)";
        case Group::Program:   return "Sections et unit\xC3\xA9s";
        case Group::Tables:    return "Tables d'animation";
        case Group::Hmi:       return "IHM";
        case Group::Hardware:  return "Configuration mat\xC3\xA9rielle";
    }
    return "";
}

std::vector<const Item*> Plan::itemsOf(Status s) const {
    std::vector<const Item*> out;
    for (const auto& it : items)
        if (it.status == s) out.push_back(&it);
    return out;
}

// ==================================================================== plan ====
Plan makePlan(const Project& cur, const Project& imp, const std::vector<HmiRef>& hmi, const Options& options, std::string source) {
    Plan plan;
    plan.source = std::move(source);
    plan.options = options;
    const auto gc = globalsOf(cur), gi = globalsOf(imp);
    plan.variablesBefore = gc.size();
    plan.variablesAfter = gi.size();

    // Chaque groupe a sa liste ; elles sont mises bout a bout a la fin, dans
    // l'ordre des groupes (l'arbre d'un onglet les montre dans cet ordre).
    std::array<std::vector<Item>, kGroupCount> byGroup;
    const auto add = [&](Status s, Group g, std::string sub, std::string name, std::string detail) {
        byGroup[static_cast<std::size_t>(g)].push_back(Item{s, g, std::move(sub), std::move(name), std::move(detail)});
    };

    // ---- les types, d'abord : une instance dit si le sien a change ------------
    std::map<std::string, std::string> changedTypes;      // nom en minuscules -> ce qui change
    {
        std::map<std::string, const DerivedType*> a, b;
        for (const auto& d : cur.derivedTypes) a.emplace(lowerOf(cur.strings.text(d.name)), &d);
        for (const auto& d : imp.derivedTypes) b.emplace(lowerOf(imp.strings.text(d.name)), &d);
        const std::string sub = "Types d\xC3\xA9riv\xC3\xA9s (DDT)";
        for (const auto& [key, d] : a) {
            const auto name = text(cur, d->name);
            const auto it = b.find(key);
            if (it == b.end()) {
                add(Status::Removed, Group::Types, sub, name, plural(d->fields.size(), "champ", "champs"));
                continue;
            }
            const auto diff = membersDiff(fieldsOf(cur, *d), fieldsOf(imp, *it->second));
            if (diff.empty()) add(Status::Kept, Group::Types, sub, name, plural(it->second->fields.size(), "champ", "champs"));
            else {
                add(Status::Changed, Group::Types, sub, name, diff);
                changedTypes[key] = diff;
            }
        }
        for (const auto& [key, d] : b)
            if (!a.count(key)) add(Status::Added, Group::Types, sub, text(imp, d->name), plural(d->fields.size(), "champ", "champs"));
        plan.ddtAfter = b.size();
    }
    {
        std::map<std::string, const Pou*> a, b;
        for (const auto& pou : cur.pous)
            if (pou.kind == PouKind::FunctionBlockType) a.emplace(lowerOf(cur.strings.text(pou.name)), &pou);
        for (const auto& pou : imp.pous)
            if (pou.kind == PouKind::FunctionBlockType) b.emplace(lowerOf(imp.strings.text(pou.name)), &pou);
        const std::string sub = "Blocs DFB";
        const auto size = [](const Pou& pou) {
            return plural(pou.parameters.size() + pou.locals.size(), "variable", "variables") + ", "
                 + plural(pou.sections.size(), "section", "sections");
        };
        for (const auto& [key, pou] : a) {
            const auto name = text(cur, pou->name);
            const auto it = b.find(key);
            if (it == b.end()) {
                add(Status::Removed, Group::Types, sub, name, size(*pou));
                continue;
            }
            std::vector<std::string> parts;
            if (auto d = membersDiff(interfaceOf(cur, *pou), interfaceOf(imp, *it->second)); !d.empty()) parts.push_back("interface : " + d);
            if (auto d = codeDiff(cur, *pou, imp, *it->second); !d.empty()) parts.push_back(d);
            if (parts.empty()) add(Status::Kept, Group::Types, sub, name, size(*it->second));
            else {
                std::string detail;
                for (const auto& part : parts) detail += (detail.empty() ? "" : " ; ") + part;
                add(Status::Changed, Group::Types, sub, name, detail);
                changedTypes[key] = detail;
            }
        }
        for (const auto& [key, pou] : b)
            if (!a.count(key)) add(Status::Added, Group::Types, sub, text(imp, pou->name), size(*pou));
        plan.dfbAfter = b.size();
    }

    // ---- les variables globales et les instances --------------------------------
    {
        struct Sorted { Item item; int rank; };
        std::vector<Sorted> vars, instances;
        const auto push = [&](const Project& p, const Variable& v, Status s, std::string detail) {
            const auto type = typeOf(p, v);
            Item it{s, Group::Variables, {}, nameOf(p, v), std::move(detail)};
            if (isInstance(p, type)) {
                it.group = Group::Instances;
                it.sub = instanceSub(p, type);
                instances.push_back({std::move(it), 0});
            } else {
                const auto g = genreOf(type);
                it.sub = g.label;
                vars.push_back({std::move(it), g.rank});
            }
        };
        const auto describe = [](const Project& p, const Variable& v) {
            const auto address = trimmed(v.address.raw);
            return typeOf(p, v) + (address.empty() ? std::string{} : kDot + address);
        };
        for (const auto& [key, ic] : gc) {
            const auto& vc = cur.variables[ic];
            const auto it = gi.find(key);
            if (it == gi.end()) {
                push(cur, vc, Status::Removed, describe(cur, vc));
                continue;
            }
            const auto& vi = imp.variables[it->second];
            if (!identical(cur, vc, imp, vi)) {
                push(imp, vi, Status::Changed, variableChange(cur, vc, imp, vi));
                continue;
            }
            std::vector<std::string> notes;
            const auto type = typeOf(imp, vi);
            if (isInstance(imp, type))
                if (const auto t = changedTypes.find(lowerOf(elementOf(type))); t != changedTypes.end())
                    notes.push_back("le type " + elementOf(type) + " change (" + t->second + ")");
            const auto ccur = trimmed(cur.strings.text(vc.comment)), cimp = trimmed(imp.strings.text(vi.comment));
            if (ccur != cimp) notes.emplace_back(options.keepIdentical ? "commentaire du projet gard\xC3\xA9" : "commentaire du .XPG");
            const auto icur = trimmed(cur.strings.text(vc.initValue)), iimp = trimmed(imp.strings.text(vi.initValue));
            if (icur != iimp)
                notes.push_back(options.keepIdentical
                                    ? "valeur initiale du projet gard\xC3\xA9" "e (" + (icur.empty() ? std::string("aucune") : icur) + ")"
                                    : "valeur initiale du .XPG (" + (iimp.empty() ? std::string("aucune") : iimp) + ")");
            std::string detail = describe(imp, vi);
            for (const auto& n : notes) detail += " \xE2\x80\x94 " + n;
            push(imp, vi, Status::Kept, std::move(detail));
        }
        for (const auto& [key, ii] : gi)
            if (!gc.count(key)) push(imp, imp.variables[ii], Status::Added, describe(imp, imp.variables[ii]));
        std::stable_sort(vars.begin(), vars.end(), [](const Sorted& x, const Sorted& y) { return x.rank < y.rank; });
        std::stable_sort(instances.begin(), instances.end(),
                         [](const Sorted& x, const Sorted& y) { return lowerOf(x.item.sub) < lowerOf(y.item.sub); });
        for (auto& s : vars) byGroup[static_cast<std::size_t>(Group::Variables)].push_back(std::move(s.item));
        for (auto& s : instances) byGroup[static_cast<std::size_t>(Group::Instances)].push_back(std::move(s.item));
    }

    // ---- le programme : sections des taches, unites, taches ------------------------
    {
        std::map<std::string, const Section*> a, b;
        for (const auto& s : cur.sections)
            if (taskSection(cur, s)) a.emplace(lowerOf(cur.strings.text(s.name)), &s);
        for (const auto& s : imp.sections)
            if (taskSection(imp, s)) b.emplace(lowerOf(imp.strings.text(s.name)), &s);
        plan.sectionsBefore = a.size();
        plan.sectionsAfter = b.size();
        // Dans l'ordre du .XPG (les taches, puis l'ordre d'execution), puis ce qui part.
        std::vector<const Section*> order;
        for (const auto& s : imp.sections)
            if (taskSection(imp, s)) order.push_back(&s);
        for (const auto* s : order) {
            const auto name = text(imp, s->name);
            const auto it = a.find(lowerOf(name));
            const auto lines = lineCount(s->body);
            if (it == a.end()) {
                add(Status::Added, Group::Program, sectionSub(imp, *s), name, plural(lines, "ligne", "lignes"));
                continue;
            }
            const auto* old = it->second;
            if (sameSection(cur, *old, imp, *s)) {
                add(Status::Kept, Group::Program, sectionSub(imp, *s), name, plural(lines, "ligne", "lignes"));
                continue;
            }
            std::vector<std::string> parts;
            if (old->body != s->body) parts.push_back("code : " + std::to_string(lineCount(old->body)) + kArrow + plural(lines, "ligne", "lignes"));
            if (old->language != s->language)
                parts.push_back(std::string(toString(old->language)) + kArrow + std::string(toString(s->language)));
            if (lowerOf(cur.strings.text(old->task)) != lowerOf(imp.strings.text(s->task)))
                parts.push_back("t\xC3\xA2" "che " + text(cur, old->task) + kArrow + text(imp, s->task));
            if (old->isSubroutine != s->isSubroutine) parts.emplace_back(s->isSubroutine ? "devient une sous-routine" : "n'est plus une sous-routine");
            if (trimmed(cur.strings.text(old->activationCondition)) != trimmed(imp.strings.text(s->activationCondition)))
                parts.emplace_back("condition d'activation");
            add(Status::Changed, Group::Program, sectionSub(imp, *s), name, joinParts(parts));
        }
        for (const auto& s : cur.sections)
            if (taskSection(cur, s) && !b.count(lowerOf(cur.strings.text(s.name))))
                add(Status::Removed, Group::Program, sectionSub(cur, s), text(cur, s.name), plural(lineCount(s.body), "ligne", "lignes"));
    }
    {
        std::map<std::string, const Pou*> a, b;
        for (const auto& pou : cur.pous)
            if (pou.kind == PouKind::ProgramUnit) a.emplace(lowerOf(cur.strings.text(pou.name)), &pou);
        for (const auto& pou : imp.pous)
            if (pou.kind == PouKind::ProgramUnit) b.emplace(lowerOf(imp.strings.text(pou.name)), &pou);
        plan.unitsAfter = b.size();
        const std::string sub = "Unit\xC3\xA9s de programme";
        for (const auto& pou : imp.pous) {
            if (pou.kind != PouKind::ProgramUnit) continue;
            const auto name = text(imp, pou.name);
            const auto it = a.find(lowerOf(name));
            const auto size = plural(pou.sections.size(), "section", "sections");
            if (it == a.end()) {
                add(Status::Added, Group::Program, sub, name, size);
                continue;
            }
            std::vector<std::string> parts;
            if (auto d = membersDiff(interfaceOf(cur, *it->second), interfaceOf(imp, pou)); !d.empty()) parts.push_back("interface : " + d);
            if (auto d = codeDiff(cur, *it->second, imp, pou); !d.empty()) parts.push_back(d);
            if (lowerOf(cur.strings.text(it->second->task)) != lowerOf(imp.strings.text(pou.task)))
                parts.push_back("t\xC3\xA2" "che " + text(cur, it->second->task) + kArrow + text(imp, pou.task));
            if (parts.empty()) add(Status::Kept, Group::Program, sub, name, size);
            else {
                std::string detail;
                for (const auto& part : parts) detail += (detail.empty() ? "" : " ; ") + part;
                add(Status::Changed, Group::Program, sub, name, detail);
            }
        }
        for (const auto& [key, pou] : a)
            if (!b.count(key)) add(Status::Removed, Group::Program, sub, text(cur, pou->name), plural(pou->sections.size(), "section", "sections"));
    }
    {
        std::map<std::string, const Task*> a;
        for (const auto& t : cur.tasks) a.emplace(lowerOf(cur.strings.text(t.name)), &t);
        std::set<std::string> seen;
        plan.tasksAfter = imp.tasks.size();
        const std::string sub = "T\xC3\xA2" "ches";
        for (const auto& t : imp.tasks) {
            const auto name = text(imp, t.name);
            seen.insert(lowerOf(name));
            const auto it = a.find(lowerOf(name));
            if (it == a.end()) {
                add(Status::Added, Group::Program, sub, name, taskKindLabel(t));
                continue;
            }
            std::vector<std::string> parts;
            if (taskKindLabel(*it->second) != taskKindLabel(t)) parts.push_back(taskKindLabel(*it->second) + kArrow + taskKindLabel(t));
            if (it->second->watchdog != t.watchdog)
                parts.push_back("chien de garde " + std::to_string(it->second->watchdog) + kArrow + std::to_string(t.watchdog) + " ms");
            // L'ordre d'execution : les sections communes, dans le meme ordre ?
            // (Une section ajoutee ou retiree se voit deja a sa ligne.)
            const auto orderOf = [](const Project& p, const Task& task, const std::set<std::string>& keep) {
                std::vector<std::string> names;
                for (const auto s : task.sections)
                    if (s < p.sections.size())
                        if (auto n = lowerOf(p.strings.text(p.sections[s].name)); keep.count(n)) names.push_back(std::move(n));
                return names;
            };
            std::set<std::string> before, after, both;
            for (const auto s : it->second->sections)
                if (s < cur.sections.size()) before.insert(lowerOf(cur.strings.text(cur.sections[s].name)));
            for (const auto s : t.sections)
                if (s < imp.sections.size()) after.insert(lowerOf(imp.strings.text(imp.sections[s].name)));
            std::set_intersection(before.begin(), before.end(), after.begin(), after.end(), std::inserter(both, both.end()));
            if (orderOf(cur, *it->second, both) != orderOf(imp, t, both)) parts.emplace_back("ordre d'ex\xC3\xA9" "cution des sections");
            if (parts.empty()) add(Status::Kept, Group::Program, sub, name, taskKindLabel(t));
            else add(Status::Changed, Group::Program, sub, name, joinParts(parts));
        }
        for (const auto& t : cur.tasks)
            if (!seen.count(lowerOf(cur.strings.text(t.name)))) add(Status::Removed, Group::Program, sub, text(cur, t.name), taskKindLabel(t));
    }

    // ---- les tables d'animation ------------------------------------------------------
    if (options.keepIdentical) {
        for (const auto& tp : keptTables(cur, gc, imp, gi)) {
            if (tp.fromXpg) {
                add(Status::Added, Group::Tables, {}, tp.name, plural(tp.lines.size(), "ligne", "lignes") + " (table du .XPG)");
                continue;
            }
            if (tp.lines.empty()) add(Status::Kept, Group::Tables, {}, tp.name, "table vide, gard\xC3\xA9" "e");
            for (const auto& l : tp.lines) add(l.status, Group::Tables, tp.name, l.text, l.detail);
        }
    } else {
        for (const auto& t : cur.animationTables) {
            const auto name = text(cur, t.name);
            std::size_t hmiLines = 0;
            for (const auto& e : t.entries) hmiLines += e.hmi ? 1u : 0u;
            const auto* x = tableNamed(imp, name);
            if (!x) {
                add(Status::Removed, Group::Tables, {}, name,
                    plural(t.entries.size(), "ligne", "lignes") + (hmiLines ? " (dont " + std::to_string(hmiLines) + " IHM)" : std::string{}));
                continue;
            }
            std::vector<std::string> ours, theirs;
            for (const auto& e : t.entries)
                if (!e.hmi) ours.push_back(lowerOf(cur.strings.text(e.name)));
            for (const auto& e : x->entries)
                if (!e.hmi) theirs.push_back(lowerOf(imp.strings.text(e.name)));
            if (ours == theirs && hmiLines == 0) {
                add(Status::Kept, Group::Tables, {}, name, plural(x->entries.size(), "ligne", "lignes"));
                continue;
            }
            add(Status::Changed, Group::Tables, {}, name,
                "remplac\xC3\xA9" "e par celle du .XPG (" + std::to_string(t.entries.size()) + kArrow + plural(x->entries.size(), "ligne", "lignes")
                    + (hmiLines ? ", les " + std::to_string(hmiLines) + " lignes IHM partent)" : std::string(")")));
        }
        for (const auto& x : imp.animationTables)
            if (!tableNamed(cur, imp.strings.text(x.name)))
                add(Status::Added, Group::Tables, {}, text(imp, x.name), plural(x.entries.size(), "ligne", "lignes") + " (table du .XPG)");
    }

    // ---- l'IHM : ce qu'elle lit de l'automate -------------------------------------------
    {
        const auto ac = addressesOf(cur), ai = addressesOf(imp);
        // L'adresse d'une variable IHM liee -> la globale qu'elle lit ; un nom (ou
        // un chemin) dans le champ de l'adresse : sa racine.
        const auto plcAt = [](const Project& p, const Globals& g, const std::map<std::string, std::string>& byAddress,
                              const std::string& address, std::string* why) -> std::string {
            if (const auto it = byAddress.find(addressKey(address)); it != byAddress.end()) return it->second;
            const auto a = trimmed(address);
            if (a.empty() || !(std::isalpha(static_cast<unsigned char>(a.front())) || a.front() == '_')) return {};
            Index v = kNoIndex;
            if (!resolve(p, g, a, why, &v) || v >= p.variables.size()) return {};
            return nameOf(p, p.variables[v]);
        };
        std::set<std::string> seen;
        for (const auto& ref : hmi) {
            const auto key = ref.where + '\n' + ref.what + '\n' + lowerOf(ref.path) + '\n' + lowerOf(ref.address);
            if (!seen.insert(key).second) continue;
            if (!trimmed(ref.address).empty()) {
                std::string whyOld, whyNew;
                const auto before = plcAt(cur, gc, ac, ref.address, &whyOld);
                const auto after = plcAt(imp, gi, ai, ref.address, &whyNew);
                const auto address = trimmed(ref.address);
                if (before.empty() && after.empty()) continue;           // pas un lien vers l'automate
                if (after.empty()) {
                    add(Status::LinkLost, Group::Hmi, ref.where, ref.what,
                        address + " : " + before + " n'est plus dans le .XPG" + (whyNew.empty() ? std::string{} : " (" + whyNew + ")"));
                } else if (before.empty()) {
                    add(Status::Added, Group::Hmi, ref.where, ref.what, address + " : lit maintenant " + after);
                } else if (lowerOf(before) != lowerOf(after)) {
                    add(Status::Changed, Group::Hmi, ref.where, ref.what, address + " : " + before + kArrow + after);
                } else if (const auto change = rootChange(cur, gc, imp, gi, before); !change.empty()) {
                    add(Status::Changed, Group::Hmi, ref.where, ref.what, address + " : " + before + " (" + change + ")");
                } else {
                    add(Status::Kept, Group::Hmi, ref.where, ref.what, address + " : " + before);
                }
                continue;
            }
            const auto path = trimmed(ref.path);
            if (path.empty()) continue;
            std::string whyOld, whyNew;
            const bool before = resolve(cur, gc, path, &whyOld);
            const bool after = resolve(imp, gi, path, &whyNew);
            // Deja introuvable avant l'import, et toujours : pas un lien vers
            // l'automate (une variable IHM, une faute de frappe) - rien a en dire.
            if (!before && !after) continue;
            if (!after) add(Status::LinkLost, Group::Hmi, ref.where, ref.what, path + " : " + whyNew);
            else if (!before) add(Status::Added, Group::Hmi, ref.where, ref.what, path + " : retrouv\xC3\xA9" "e dans le .XPG");
            else if (const auto change = rootChange(cur, gc, imp, gi, rootOf(path)); !change.empty())
                add(Status::Changed, Group::Hmi, ref.where, ref.what, path + " (" + std::string(rootOf(path)) + " : " + change + ")");
            else add(Status::Kept, Group::Hmi, ref.where, ref.what, path);
        }
    }

    // ---- la configuration materielle ------------------------------------------------------
    {
        const auto& hc = cur.hardware;
        const auto& hi = imp.hardware;
        if (!hi.racks.empty()) {
            plan.hardwareReplaced = true;
            add(Status::Changed, Group::Hardware, {}, "Racks et modules",
                "ceux du .XHW import\xC3\xA9 : " + hardwareSummary(hi) + " (le projet : " + hardwareSummary(hc) + ")");
            if (!hc.cpuReference.empty() && !hi.cpuReference.empty() && cpuKey(hc.cpuReference) != cpuKey(hi.cpuReference))
                add(Status::Changed, Group::Hardware, {}, "Processeur", hc.cpuReference + kArrow + hi.cpuReference);
        } else {
            add(Status::Kept, Group::Hardware, {}, "Racks et modules", "ceux du projet : " + hardwareSummary(hc));
            if (!hc.cpuReference.empty() && !hi.cpuReference.empty() && cpuKey(hc.cpuReference) != cpuKey(hi.cpuReference))
                add(Status::Kept, Group::Hardware, {}, "Processeur",
                    hc.cpuReference + " gard\xC3\xA9 (le .XPG est \xC3\xA9" "crit pour " + hi.cpuReference + ")");
        }
    }

    for (auto& list : byGroup)
        for (auto& it : list) {
            ++plan.counts[static_cast<std::size_t>(it.status)];
            plan.items.push_back(std::move(it));
        }
    return plan;
}

// =================================================================== fusion ====
Project merge(const Project& cur, const Project& imp, const Options& options) {
    Project out = imp;                        // le cote API du .XPG (sa copie refait l'index des noms)
    // L'en-tete : celui du programme importe, mais le projet reste le meme.
    if (!cur.header.sourceFile.empty()) out.header.sourceFile = cur.header.sourceFile;
    if (!cur.header.projectName.empty()) out.header.projectName = cur.header.projectName;
    // Le materiel : celui du projet, sauf si un .XHW est venu avec le .XPG
    // (le .XPG seul ne porte que l'identite du processeur).
    if (imp.hardware.racks.empty()) {
        HardwareConfig hw = cur.hardware;
        if (hw.family.empty()) hw.family = imp.hardware.family;
        if (hw.cpuReference.empty()) hw.cpuReference = imp.hardware.cpuReference;
        if (hw.cpuFirmware.empty()) hw.cpuFirmware = imp.hardware.cpuFirmware;
        if (hw.resourceName.empty()) hw.resourceName = imp.hardware.resourceName;
        out.hardware = std::move(hw);
    }
    // Les reglages du projet, que Control Expert ne connait pas.
    out.memoryWindows = cur.memoryWindows;
    out.icon = cur.icon;
    out.codeIcons = cur.codeIcons;      // 1.8.0 : par nom ; un element disparu garde la sienne, sans effet

    if (options.keepIdentical) {
        // Une globale identique (nom, type, adresse) garde sa declaration du
        // projet : ce qu'on y avait documente ne se perd pas.
        const auto gc = globalsOf(cur);
        for (auto& v : out.variables) {
            if (v.scope != VariableScope::Global) continue;
            const auto it = gc.find(lowerOf(out.strings.text(v.name)));
            if (it == gc.end()) continue;
            const auto& old = cur.variables[it->second];
            if (!identical(cur, old, out, v)) continue;
            v.comment = out.strings.intern(cur.strings.text(old.comment));
            v.initValue = out.strings.intern(cur.strings.text(old.initValue));
            v.instanceElements.clear();
            for (const auto& e : old.instanceElements)
                v.instanceElements.push_back(InstanceElement{e.name, out.strings.intern(cur.strings.text(e.comment))});
        }
        // Les tables d'animation du projet, sans les lignes des variables disparues.
        const auto gi = globalsOf(imp);
        std::vector<AnimationTable> tables;
        for (const auto& tp : keptTables(cur, gc, imp, gi)) {
            AnimationTable t;
            t.name = out.strings.intern(tp.name);
            t.owner = tp.owner.empty() ? SymbolId{0} : out.strings.intern(tp.owner);
            for (const auto& l : tp.lines)
                if (l.status != Status::Removed) t.entries.push_back(AnimationEntry{out.strings.intern(l.text), l.hmi});
            tables.push_back(std::move(t));
        }
        out.animationTables = std::move(tables);
    }

    // Le .XPG annonce que les racks manquent ; ceux du projet (ou du .XHW) sont la.
    if (!out.hardware.racks.empty())
        std::erase_if(out.partialDataNotices, [](const std::string& n) { return n.find("Rack and module layout") != std::string::npos; });
    out.linkTypes();
    return out;
}

// ================================================================== lecture ====
int sniffFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return -1;
    std::string head(1024, '\0');
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, in.gcount())));
    switch (importer::ProjectImporter::sniff(head, path)) {
        case importer::SourceFormat::Xpg: return 1;
        case importer::SourceFormat::Xhw: return 2;
        default:                          return 0;
    }
}

core::Result<std::shared_ptr<Project>> read(const std::vector<std::string>& paths) {
    if (paths.empty()) return core::fail(core::ErrorCode::InvalidArgument, "aucun fichier");
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const auto name = std::filesystem::path(paths[i]).filename().string();
        const int kind = sniffFile(paths[i]);
        if (kind < 0) return core::fail(core::ErrorCode::FileNotFound, name + " : introuvable ou illisible", paths[i]);
        if (i == 0 && kind != 1)
            return core::fail(core::ErrorCode::XmlUnexpectedRoot, name + " n'est pas un programme export\xC3\xA9 de Control Expert (.XPG)", paths[i]);
        if (i > 0 && kind != 2)
            return core::fail(core::ErrorCode::XmlUnexpectedRoot, name + " n'est pas une configuration mat\xC3\xA9rielle (.XHW)", paths[i]);
    }
    // UN BUS A SOI : celui de l'application recevrait ImportFinished, et App
    // remplacerait tout le projet (l'IHM, le dossier, l'historique) par ce fichier.
    core::EventBus bus;
    importer::ProjectImporter importer(bus);
    auto result = importer.importFiles(paths);
    if (!result) return core::Err<core::Error>(result.error());
    if (!result->project) return core::fail(core::ErrorCode::IncompleteProject, "le fichier ne donne aucun programme", paths.front());
    return result->project;
}

bool pathExists(const Project& p, std::string_view path, std::string* why) {
    return resolve(p, globalsOf(p), path, why);
}

// ================================================================= commande ====
ImportMastCommand::ImportMastCommand(std::shared_ptr<Project> project, std::shared_ptr<const Project> imported,
                                     Options options, std::string source)
    : project_(std::move(project)), imported_(std::move(imported)), options_(options), source_(std::move(source)) {}

core::Status ImportMastCommand::execute() {
    if (!project_ || !imported_) return core::fail(core::ErrorCode::InvalidArgument, "pas de projet");
    // Le projet est modifie EN PLACE : l'application et les vues tiennent ce
    // meme objet. L'etat d'avant est garde entier (Ctrl+Z le rend tel quel).
    auto next = merge(*project_, *imported_, options_);
    before_ = *project_;
    *project_ = std::move(next);
    return core::ok();
}

core::Status ImportMastCommand::undo() {
    if (!project_ || !before_) return core::ok();
    *project_ = std::move(*before_);
    before_.reset();
    return core::ok();
}

std::string ImportMastCommand::label() const { return "Importer " + source_ + " (nouveau MAST)"; }

} // namespace project::mast
