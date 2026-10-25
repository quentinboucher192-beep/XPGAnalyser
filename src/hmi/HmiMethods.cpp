// =============================================================================
//  hmi/HmiMethods.cpp - les methodes, l'heritage et les interfaces (1.13.0, lot J1)
//  Voir HmiMethods.hpp. Calque sur HmiOperators.cpp : une methode devient une
//  FUNCTION interne (methodFunctionText) que l'interpreteur appelle (resolveMethod).
// =============================================================================
#include "HmiMethods.hpp"

#include "HmiCalc.hpp"       // hmi::closest : le nom connu le plus proche (veux-tu dire ?)
#include "HmiDecl.hpp"       // 1.11.18 : les declarations du modele, reconstruites (decl::codeOf)
#include "HmiEnums.hpp"      // typeKindLabel
#include "HmiMarkers.hpp"    // 1.11.1 : le corps sans les $ des reperes
#include "HmiNames.hpp"      // 1.13.0 (lot J8) : une methode voit les noms de l'espace de son type
#include "HmiScript.hpp"
#include "HmiTypes.hpp"
#include "HmiTypeRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>

namespace hmi {
namespace {

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
bool sameName(std::string_view a, std::string_view b) { return upperOf(trimmed(a)) == upperOf(trimmed(b)); }
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identPart(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Le code sans ses commentaires ni ses chaines (blancs, longueurs gardees).
std::string codeOnly(std::string_view s) {
    std::string out(s);
    std::size_t i = 0;
    const auto blank = [&](std::size_t from, std::size_t to) {
        for (std::size_t k = from; k < to && k < out.size(); ++k)
            if (out[k] != '\n') out[k] = ' ';
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            std::size_t k = i + 1;
            while (k < s.size() && s[k] != c) k += s[k] == '$' ? 2 : 1;
            blank(i, k + 1);
            i = k + 1;
        } else if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            const std::size_t stop = end == std::string_view::npos ? s.size() : end + 2;
            blank(i, stop);
            i = stop;
        } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            const std::size_t stop = end == std::string_view::npos ? s.size() : end;
            blank(i, stop);
            i = stop;
        } else {
            ++i;
        }
    }
    return out;
}

// Un identifiant ST valide (pour le nom interne : ses caracteres non valides, '_').
std::string sanitize(std::string_view s) {
    std::string out;
    for (char c : s) out += identPart(c) ? c : '_';
    return out;
}

// Le texte d'un type pour l'en-tete (un generique ecrit tel quel).
std::string displayType(std::string_view t) { return trimmed(t); }

} // namespace

// ------------------------------------------------------------ noms et texte ---
std::string methodFunctionName(std::string_view typeName, const HmiFunction& m) {
    return "METHODE_" + sanitize(upperOf(typeName)) + "_" + sanitize(upperOf(m.name));
}

std::string methodSignature(std::string_view typeName, const HmiFunction& m) {
    std::string s = std::string(trimmed(typeName)) + "." + m.name + "(";
    bool first = true;
    for (const auto& d : m.decls) {
        if (d.kind != DeclKind::Parameter) continue;
        s += (first ? "" : "; ") + d.name + " : " + d.type;
        first = false;
    }
    s += ")";
    if (!m.returnType.empty()) s += " : " + m.returnType;
    return s;
}

// Le corps reecrit : membres prefixes de SELF., THIS -> SELF, le nom de la methode
// (le retour) -> le nom interne, SUPER.M(...) -> METHODE_<Parent>_M(SELF[, ...]).
// `selfType` : le type de SELF (ses membres) ; `definingOwner` : le type qui definit la methode
// (son parent sert a SUPER).
static std::string rewriteBody(const Project& p, std::string_view ownerType, const HmiFunction& m,
                               std::string_view internal) {
    const std::string body = markers::strip(m.body);
    const std::string code = codeOnly(body);

    std::set<std::string> memberSet;
    for (const auto& mm : allMembers(p, ownerType)) memberSet.insert(upperOf(mm.name));
    std::set<std::string> shadow;                                   // parametres et locales : ils cachent un membre
    for (const auto& d : m.decls) shadow.insert(upperOf(d.name));
    const std::string ret = upperOf(m.name);

    // Le type parent (pour SUPER) : celui du type qui DEFINIT la methode.
    const HmiType* self = p.hmiTypeByName(ownerType);
    const std::string parentName = self ? self->base : std::string{};

    std::string out;
    out.reserve(body.size() + 32);
    std::size_t i = 0;
    while (i < body.size()) {
        // dans un commentaire ou une chaine : tel quel
        if (code[i] == ' ' && body[i] != ' ') { out += body[i++]; continue; }
        if (!identStart(body[i]) || (i > 0 && (identPart(body[i - 1]) || body[i - 1] == '.' || body[i - 1] == '#'))) {
            out += body[i++];
            continue;
        }
        const std::size_t start = i;
        while (i < body.size() && identPart(body[i])) ++i;
        const std::string word = body.substr(start, i - start);
        const std::string U = upperOf(word);
        // un appel ? (le mot suivi d'une parenthese)
        std::size_t k = i;
        while (k < body.size() && (body[k] == ' ' || body[k] == '\t')) ++k;
        const bool call = k < body.size() && body[k] == '(';

        if (U == "THIS" && !call) { out += "SELF"; continue; }
        if (U == "SUPER" && !parentName.empty()) {
            // SUPER.Methode(...) -> METHODE_<ParentQuiLaDefinit>_Methode(SELF[, ...])
            std::size_t d = i;
            while (d < body.size() && (body[d] == ' ' || body[d] == '\t')) ++d;
            if (d < body.size() && body[d] == '.') {
                std::size_t n0 = d + 1;
                while (n0 < body.size() && (body[n0] == ' ' || body[n0] == '\t')) ++n0;
                std::size_t n1 = n0;
                while (n1 < body.size() && identPart(body[n1])) ++n1;
                std::size_t paren = n1;
                while (paren < body.size() && (body[paren] == ' ' || body[paren] == '\t')) ++paren;
                if (n1 > n0 && paren < body.size() && body[paren] == '(') {
                    const std::string mname = body.substr(n0, n1 - n0);
                    const HmiType* owner = nullptr;
                    const HmiFunction* pm = effectiveMethod(p, parentName, mname, &owner);
                    if (pm && owner) {
                        out += methodFunctionName(fullName(*owner), *pm) + "(SELF";   // 1.13.0 (lot J8) : Gaz.T_Pompe
                        std::size_t after = paren + 1;
                        while (after < body.size() && (body[after] == ' ' || body[after] == '\t')) ++after;
                        out += (after < body.size() && body[after] == ')') ? "" : ", ";
                        i = paren + 1;
                        continue;
                    }
                }
            }
            out += word;
            continue;
        }
        if (U == ret && !call) { out += internal; continue; }
        if (memberSet.count(U) && !shadow.count(U) && !call) { out += "SELF." + word; continue; }
        out += word;
    }
    return out;
}

std::string methodFunctionText(const Project& p, std::string_view ownerType, const HmiFunction& m) {
    const std::string internal = methodFunctionName(ownerType, m);
    HmiFunction m2 = m;
    m2.body = rewriteBody(p, ownerType, m, internal);
    std::string head = "FUNCTION " + internal + (m.returnType.empty() ? std::string{} : " : " + displayType(m.returnType));
    // SELF, en VAR_IN_OUT : une methode qui ecrit un membre le modifie pour de bon. SELF a le type qui
    // DEFINIT la methode ; un sous-type passe la ou ce parent est attendu (montee du VAR_IN_OUT).
    std::string self = "VAR_IN_OUT\n    SELF : " + std::string(trimmed(ownerType)) + ";\nEND_VAR\n";
    std::string body = decl::codeOf(m2);
    if (!body.empty() && body.back() != '\n') body += '\n';
    return head + "\n" + self + body + "END_FUNCTION\n";
}

// ---------------------------------------------------------------- heritage ---
std::vector<const HmiType*> baseChain(const Project& p, std::string_view typeName) {
    std::vector<const HmiType*> chain;
    std::set<std::string> seen;
    const HmiType* ty = p.hmiTypeByName(typeName);
    if (!ty) return chain;
    seen.insert(upperOf(ty->name));
    std::string base = ty->base;
    while (!base.empty()) {
        const HmiType* b = p.hmiTypeByName(base);
        if (!b || !seen.insert(upperOf(b->name)).second) break;       // inconnu ou cycle : on s'arrete
        chain.push_back(b);
        base = b->base;
    }
    return chain;
}

std::vector<TypeMember> allMembers(const Project& p, std::string_view typeName) {
    return types::membersOf(p, typeName);                             // suit la chaine de base (parent d'abord)
}

const HmiFunction* ownMethod(const HmiType& t, std::string_view name) {
    for (const auto& m : t.methods)
        if (sameName(m.name, name)) return &m;
    return nullptr;
}

std::vector<OwnedMethod> allMethods(const Project& p, std::string_view typeName) {
    const HmiType* ty = p.hmiTypeByName(typeName);
    if (!ty) return {};
    // Du parent le plus lointain vers le type : une OVERRIDE remplace la version heritee.
    std::vector<const HmiType*> order = baseChain(p, typeName);       // [parent, grand-parent...]
    std::reverse(order.begin(), order.end());                         // [grand-parent... parent]
    order.push_back(ty);
    std::vector<OwnedMethod> out;
    for (const HmiType* t : order)
        for (const auto& m : t->methods) {
            bool replaced = false;
            for (auto& o : out)
                if (sameName(o.method->name, m.name)) { o = {t, &m}; replaced = true; break; }
            if (!replaced) out.push_back({t, &m});
        }
    return out;
}

const HmiFunction* effectiveMethod(const Project& p, std::string_view typeName, std::string_view name,
                                   const HmiType** owner) {
    for (const auto& o : allMethods(p, typeName))
        if (sameName(o.method->name, name)) {
            if (owner) *owner = o.owner;
            return o.method;
        }
    return nullptr;
}

// ------------------------------------------------------------- predicats ----
bool isA(const Project& p, std::string_view sub, std::string_view base) {
    if (sameName(sub, base)) return true;
    // 1.13.0 (lot J8) : un meme type ecrit court ou complet (T_Pompe, Gaz.T_Pompe).
    const HmiType* target = p.hmiTypeByName(trimmed(base));
    if (const HmiType* self = p.hmiTypeByName(trimmed(sub)); self && self == target) return true;
    for (const HmiType* b : baseChain(p, sub))
        if (sameName(b->name, base) || sameName(fullName(*b), base) || (target && b == target)) return true;
    return false;
}

bool isInterface(const Project& p, std::string_view name) {
    const HmiType* t = p.hmiTypeByName(name);
    return t && t->kind == HmiTypeKind::Interface;
}

bool implementsInterface(const Project& p, std::string_view type, std::string_view iface) {
    const HmiType* ty = p.hmiTypeByName(type);
    if (!ty) return false;
    const HmiType* wanted = p.hmiTypeByName(trimmed(iface));     // 1.13.0 (lot J8) : court ou complet
    const auto has = [&](const HmiType& t) {
        for (const auto& i : t.interfaces)
            if (sameName(i, iface) || (wanted && p.hmiTypeByName(trimmed(i)) == wanted)) return true;
        return false;
    };
    if (has(*ty)) return true;
    for (const HmiType* b : baseChain(p, type))
        if (has(*b)) return true;
    return false;
}

// ------------------------------------------------------------- interfaces ---
bool sameMethodSignature(const HmiFunction& a, const HmiFunction& b) {
    if (!sameName(a.returnType, b.returnType)) return false;
    std::vector<const Declaration*> pa, pb;
    for (const auto& d : a.decls) if (d.kind == DeclKind::Parameter) pa.push_back(&d);
    for (const auto& d : b.decls) if (d.kind == DeclKind::Parameter) pb.push_back(&d);
    if (pa.size() != pb.size()) return false;
    for (std::size_t i = 0; i < pa.size(); ++i)
        if (!sameName(pa[i]->type, pb[i]->type) || pa[i]->mode != pb[i]->mode) return false;
    return true;
}

std::vector<std::string> interfaceMisses(const Project& p, const HmiType& type) {
    std::vector<std::string> out;
    for (const auto& iname : type.interfaces) {
        const HmiType* iface = p.hmiTypeByName(iname);
        if (!iface || iface->kind != HmiTypeKind::Interface) {
            out.push_back(type.name + " d\xC3\xA9" "clare " + iname + " mais ce n'est pas une interface");
            continue;
        }
        for (const auto& sig : iface->methods) {
            const HmiFunction* mine = effectiveMethod(p, type.name, sig.name);
            if (!mine)
                out.push_back(type.name + " d\xC3\xA9" "clare " + iname + " mais n'a pas " + methodSignature(iname, sig).substr(iname.size() + 1));
            else if (!sameMethodSignature(*mine, sig))
                out.push_back(type.name + "." + sig.name + " ne respecte pas la signature de " + iname + " : "
                              + methodSignature(iname, sig).substr(iname.size() + 1));
        }
    }
    return out;
}

// -------------------------------------------- l'interface avec l'interpreteur ---
bool resolveMethod(const Project& p, std::string_view selfType, std::string_view method, MethodCall& out) {
    const HmiType* owner = nullptr;
    const HmiFunction* m = effectiveMethod(p, selfType, method, &owner);
    if (!m || !owner) return false;
    // SELF a le type qui DEFINIT la methode (owner) ; l'objet reel (un sous-type) passe la ou owner est
    // attendu (montee du VAR_IN_OUT). Une seule fonction par (owner, methode) : un SUPER la retrouve.
    // 1.13.0 (lot J8) : le nom complet du type (Gaz.T_Pompe) - SELF et le nom interne ; une methode d'un type d'un
    // espace voit les noms de cet espace sans prefixe (son corps qualifie avant le moteur).
    const std::string ownerName = fullName(*owner);
    out.selfType = ownerName;
    out.isVirtual = m->isVirtual;
    out.function = methodFunctionText(p, ownerName, *m);
    if (!owner->ns.empty() || names::mentionsUsing(m->body)) out.function = names::qualify(p, out.function, owner->ns, {"SELF"}, true);
    out.owner = "type " + ownerName + ", m\xC3\xA9thode " + m->name;
    out.key = "methode|" + upperOf(ownerName) + "|" + upperOf(m->name) + "|"
            + std::to_string(std::hash<std::string>{}(out.function));
    return true;
}

bool resolveMethodInternal(const Project& p, std::string_view internalName, MethodCall& out) {
    // 1.13.0 (lot J8) : les types du projet et ceux des bibliotheques reliees, par leur nom complet.
    const auto tryType = [&](const HmiType& t) {
        const std::string tn = fullName(t);
        for (const auto& m : t.methods)
            if (methodFunctionName(tn, m) == internalName) {
                out.selfType = tn;
                out.isVirtual = m.isVirtual;
                out.function = methodFunctionText(p, tn, m);
                if (!t.ns.empty() || names::mentionsUsing(m.body)) out.function = names::qualify(p, out.function, t.ns, {"SELF"}, true);
                out.owner = "type " + tn + ", m\xC3\xA9thode " + m.name;
                out.key = "methode|" + upperOf(tn) + "|" + upperOf(m.name) + "|"
                        + std::to_string(std::hash<std::string>{}(out.function));
                return true;
            }
        return false;
    };
    for (const auto& t : p.programs.types)
        if (tryType(t)) return true;
    bool found = false;
    p.forEachLibraryType([&](const HmiType& t, const LoadedLibrary&) { if (!found) found = tryType(t); });
    return found;
}

// --------------------------------------------------------------- l'edition ---
std::vector<OperatorName> methodNames(const Project& p, std::string_view typeName, const HmiFunction& m) {
    (void)p;
    std::vector<OperatorName> out;
    out.push_back({"SELF", std::string(trimmed(typeName)), "l'instance de " + std::string(trimmed(typeName)) + " (THIS vaut SELF)"});
    if (!m.returnType.empty())
        out.push_back({m.name, m.returnType, "le r\xC3\xA9sultat (" + m.returnType + ") : " + m.name + " := \xE2\x80\xA6"});
    for (const auto& d : m.decls)
        if (d.kind == DeclKind::Parameter) out.push_back({d.name, d.type, "un param\xC3\xA8tre"});
    return out;
}

std::string methodLegend(const Project& p, std::string_view typeName, const HmiFunction& m) {
    (void)p;
    std::string s = "SELF : l'instance de " + std::string(trimmed(typeName)) + " (THIS vaut SELF) . ses membres se lisent par leur nom ou SELF.X";
    if (!m.returnType.empty()) s += " . rends par " + m.name + " := \xE2\x80\xA6";
    return s;
}

std::string methodTemplate(const Project& p, std::string_view typeName, const HmiFunction& m) {
    std::string s = "(* " + methodLegend(p, typeName, m) + " *)\n";
    if (!m.returnType.empty()) s += m.name + " := ;\n";
    return s;
}

std::string overrideTemplate(const Project& p, std::string_view typeName, const HmiFunction& parent) {
    (void)p;
    (void)typeName;
    std::string s = "(* redefinit " + parent.name + " ; SUPER." + parent.name + "(\xE2\x80\xA6) rappelle le corps du parent *)\n";
    if (!parent.returnType.empty()) s += parent.name + " := SUPER." + parent.name + "();\n";
    else s += "SUPER." + parent.name + "();\n";
    return s;
}

// ------------------------------------------------------- Generer (le modele) ---
std::string extendsProblem(const Project& p, const HmiType& t) {
    if (t.base.empty()) return {};
    if (t.kind != HmiTypeKind::Structure)
        return std::string(typeKindLabel(t.kind)) + " " + t.name + " ne peut pas h\xC3\xA9riter (EXTENDS n'existe qu'entre structures)";
    const HmiType* b = p.hmiTypeByName(t.base);
    if (!b) {
        std::vector<std::string> pool;
        for (const auto& x : p.programs.types)
            if (x.kind == HmiTypeKind::Structure) pool.push_back(x.name);
        const std::string guess = calc::closest(t.base, pool);
        return t.name + " h\xC3\xA9rite de " + t.base + " : type inconnu" + (guess.empty() ? std::string{} : " - veux-tu dire " + guess + " ?");
    }
    if (b->kind != HmiTypeKind::Structure)
        return t.name + " ne peut h\xC3\xA9riter que d'une structure (" + t.base + " est " + std::string(typeKindLabel(b->kind)) + ")";
    // cycle d'heritage
    std::set<std::string> seen{upperOf(t.name)};
    std::string chain = t.name;
    std::string cur = t.base;
    while (!cur.empty()) {
        chain += " -> " + cur;
        if (!seen.insert(upperOf(cur)).second)
            return t.name + " h\xC3\xA9rite de lui-m\xC3\xAAme : " + chain;
        const HmiType* c = p.hmiTypeByName(cur);
        cur = c ? c->base : std::string{};
    }
    return {};
}

std::string methodProblem(const Project& p, const HmiType& owner, const HmiFunction& m) {
    const std::string here = owner.name + "." + m.name;
    if (!isIdentifier(m.name)) return owner.name + " : nom de m\xC3\xA9thode invalide '" + m.name + "'";
    // une methode privee ne peut pas etre virtuelle (rien a redefinir de l'exterieur)
    if (m.visibility == Visibility::Private && m.isVirtual)
        return here + " : une m\xC3\xA9thode priv\xC3\xA9" "e ne peut pas \xC3\xAAtre virtuelle (rien \xC3\xA0 red\xC3\xA9" "finir de l'ext\xC3\xA9rieur)";
    // retour inconnu ?
    if (!m.returnType.empty()) {
        std::string why;
        if (!types::validType(p, m.returnType, &why)) return here + " : type de retour " + why;
    }
    // parametres illisibles ?
    for (const auto& d : m.decls)
        if (d.kind == DeclKind::Parameter) {
            std::string why;
            if (!types::validType(p, d.type, &why)) return here + " : param\xC3\xA8tre " + d.name + " : " + why;
        }
    // OVERRIDE : la meme methode doit exister VIRTUAL chez un parent, meme signature.
    bool overridesParent = false;
    const HmiType* def = nullptr;
    for (const HmiType* b : baseChain(p, owner.name))
        if (const HmiFunction* pm = ownMethod(*b, m.name)) { overridesParent = true; def = b;
            if (!pm->isVirtual)
                return here + " red\xC3\xA9" "finit " + b->name + "." + m.name + " qui n'est pas virtuelle (VIRTUAL) : rien \xC3\xA0 red\xC3\xA9" "finir";
            if (!sameMethodSignature(*pm, m))
                return here + " red\xC3\xA9" "finit " + b->name + "." + m.name + " mais change sa signature (attendu " + methodSignature(b->name, *pm).substr(b->name.size() + 1) + ")";
            break;
        }
    (void)overridesParent; (void)def;
    return {};
}

// ------------------------------------------- renommage et duplication (UI) ---
std::size_t renameTypeInMethods(Project& pr, std::string_view from, std::string_view to) {
    std::size_t changed = 0;
    for (auto& t : pr.programs.types) {
        if (sameName(t.base, from)) { t.base = std::string(to); ++changed; }
        for (auto& i : t.interfaces)
            if (sameName(i, from)) { i = std::string(to); ++changed; }
    }
    return changed;
}

void renumberMethods(Project& pr, std::vector<HmiFunction>& methods) {
    for (auto& m : methods) m.id = pr.allocate();
}

} // namespace hmi
