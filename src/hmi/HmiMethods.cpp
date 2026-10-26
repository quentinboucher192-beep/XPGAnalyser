// =============================================================================
//  hmi/HmiMethods.cpp - les methodes, l'heritage et les interfaces (1.13.0, lot J1)
//  Voir HmiMethods.hpp. Calque sur HmiOperators.cpp : une methode devient une
//  FUNCTION interne (methodFunctionText) que l'interpreteur appelle (resolveMethod).
// =============================================================================
#include "HmiMethods.hpp"

#include "HmiCalc.hpp"       // hmi::closest : le nom connu le plus proche (veux-tu dire ?)
#include "HmiDecl.hpp"       // 1.11.18 : les declarations du modele, reconstruites (decl::codeOf)
#include "HmiEnums.hpp"      // typeKindLabel, renameEnumType
#include "HmiEquipmentLink.hpp" // 1.13.0 : eqlink::renameType (un type renomme : sa liaison Equipement)
#include "HmiMarkers.hpp"    // 1.11.1 : le corps sans les $ des reperes
#include "HmiNames.hpp"      // 1.13.0 (lot J8) : une methode voit les noms de l'espace de son type
#include "HmiOperators.hpp"  // 1.13.0 : renameTypeInOperators
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
// 1.13.0 : une autre methode du type appelee par son nom nu (Journal()) -> SELF.Journal() (le dispatch suit le
// type reel de SELF) ; un argument nomme ou un champ de litteral (F(Consigne := x), (Debit := 1.0)) garde son nom ;
// dans un texte, les trous {Debit:0.0} d'un message (LEVER, IHM_LOG, IHM_JOURNAL) lisent SELF.Debit.
static std::string rewriteBody(const Project& p, std::string_view ownerType, const HmiFunction& m,
                               std::string_view internal) {
    const std::string body = markers::strip(m.body);
    const std::string code = codeOnly(body);

    std::set<std::string> memberSet;
    for (const auto& mm : allMembers(p, ownerType)) memberSet.insert(upperOf(mm.name));
    std::set<std::string> shadow;                                   // parametres et locales : ils cachent un membre
    for (const auto& d : m.decls) shadow.insert(upperOf(d.name));
    // les locales ecrites dans le corps (VAR ... END_VAR) cachent aussi un membre
    for (const auto& d : decl::extract(body).decls) shadow.insert(upperOf(d.name));
    const std::string ret = upperOf(m.name);
    const auto method = [&](std::string_view w) { return !shadow.count(upperOf(w)) && effectiveMethod(p, ownerType, w) != nullptr; };

    // Les identifiants d'une expression d'un trou {...} : un membre (ou un appel de methode) prend SELF.
    const auto holeExpr = [&](std::string_view x) {
        std::string r;
        for (std::size_t j = 0; j < x.size();) {
            if (!identStart(x[j]) || (j > 0 && (identPart(x[j - 1]) || x[j - 1] == '.' || x[j - 1] == '#'))) { r += x[j++]; continue; }
            std::size_t e = j;
            while (e < x.size() && identPart(x[e])) ++e;
            const std::string w(x.substr(j, e - j));
            std::size_t q = e;
            while (q < x.size() && (x[q] == ' ' || x[q] == '\t')) ++q;
            const bool isCall = q < x.size() && x[q] == '(';
            const std::string U = upperOf(w);
            if ((!isCall && memberSet.count(U) && !shadow.count(U)) || (isCall && method(w))) r += "SELF.";
            r += w;
            j = e;
        }
        return r;
    };
    // Un texte entre apostrophes (ou guillemets) : ses trous {expr[:format]} relus ; le reste tel quel.
    const auto rewriteText = [&](std::string_view t) {
        std::string r;
        for (std::size_t j = 0; j < t.size();) {
            if (t[j] == '$' && j + 1 < t.size()) { r += t.substr(j, 2); j += 2; continue; }       // $' $$ $N : un echappement
            if (t[j] == '{' && j + 1 < t.size() && t[j + 1] == '{') { r += "{{"; j += 2; continue; }   // une accolade ecrite
            if (t[j] != '{') { r += t[j++]; continue; }
            const std::size_t close = t.find('}', j + 1);
            if (close == std::string_view::npos) { r += t.substr(j); break; }
            const std::string_view inside = t.substr(j + 1, close - j - 1);
            std::size_t cut = inside.size();                      // le format : apres le dernier ':' (pas un :=)
            if (const auto colon = inside.rfind(':'); colon != std::string_view::npos && (colon + 1 >= inside.size() || inside[colon + 1] != '='))
                cut = colon;
            r += '{';
            r += holeExpr(inside.substr(0, cut));
            r += inside.substr(cut);
            r += '}';
            j = close + 1;
        }
        return r;
    };

    // Le type parent (pour SUPER) : celui du type qui DEFINIT la methode (par son nom complet : 1.13.0).
    const HmiType* self = p.hmiTypeByName(ownerType);
    const HmiType* parent = self ? types::baseOf(p, *self) : nullptr;
    const std::string parentName = parent ? fullName(*parent) : std::string{};

    std::string out;
    out.reserve(body.size() + 32);
    std::size_t i = 0;
    while (i < body.size()) {
        // une chaine : ses trous {Membre} lisent SELF.Membre (le reste tel quel)
        if (code[i] == ' ' && (body[i] == '\'' || body[i] == '"')) {
            std::size_t k = i + 1;
            while (k < body.size() && body[k] != body[i]) k += body[k] == '$' ? 2 : 1;
            const std::size_t close = std::min(k, body.size());   // l'apostrophe fermante (ou la fin)
            out += body[i];
            out += rewriteText(std::string_view(body).substr(i + 1, close - (i + 1)));
            if (close < body.size()) out += body[close];
            i = close < body.size() ? close + 1 : body.size();
            continue;
        }
        // dans un commentaire : tel quel
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
        // un argument nomme, un champ d'un litteral : (Consigne := x), Ecrire(a, Consigne := x) - son nom reste
        const bool named = [&] {
            if (k + 1 >= body.size() || body[k] != ':' || body[k + 1] != '=') return false;
            std::size_t b = start;
            while (b > 0 && std::isspace(static_cast<unsigned char>(code[b - 1]))) --b;
            return b > 0 && (code[b - 1] == '(' || code[b - 1] == ',');
        }();
        if (memberSet.count(U) && !shadow.count(U) && !call && !named) { out += "SELF." + word; continue; }
        // 1.13.0 : une autre methode du type, appelee par son nom nu (Journal()) : SELF.Journal() - SELF est l'objet reel
        // (VAR_IN_OUT), l'appel garde le dispatch virtuel. Une methode passe avant une fonction de meme nom.
        if (call && method(word)) { out += "SELF." + word; continue; }
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
    // 1.13.0 : par le NOM COMPLET - Gaz.T_Vanne EXTENDS Base.T_Vanne n'est pas un cycle ; le parent se lit depuis
    // l'espace du type (types::baseOf).
    seen.insert(upperOf(fullName(*ty)));
    for (const HmiType* b = types::baseOf(p, *ty); b; b = types::baseOf(p, *b)) {
        if (!seen.insert(upperOf(fullName(*b))).second) break;       // cycle : on s'arrete
        chain.push_back(b);
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
    // 1.13.0 : un parent connu se compare par identite (Base.T_Vanne n'est pas le T_Vanne de la racine).
    for (const HmiType* b : baseChain(p, sub))
        if (target ? b == target : (sameName(b->name, base) || sameName(fullName(*b), base))) return true;
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
            if (wanted ? types::typeFrom(p, t.ns, i, &t) == wanted : sameName(i, iface)) return true;
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
        const HmiType* iface = types::typeFrom(p, type.ns, iname, &type);     // 1.13.0 : lue depuis l'espace du type
        if (!iface || iface->kind != HmiTypeKind::Interface) {
            out.push_back(type.name + " d\xC3\xA9" "clare " + iname + " mais ce n'est pas une interface");
            continue;
        }
        for (const auto& sig : iface->methods) {
            const HmiFunction* mine = effectiveMethod(p, fullName(type), sig.name);   // 1.13.0 : nom complet
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
    const HmiType* b = types::baseOf(p, t);
    if (!b) {
        std::vector<std::string> pool;
        for (const auto& x : p.programs.types)
            if (x.kind == HmiTypeKind::Structure) pool.push_back(x.name);
        const std::string guess = calc::closest(t.base, pool);
        return t.name + " h\xC3\xA9rite de " + t.base + " : type inconnu" + (guess.empty() ? std::string{} : " - veux-tu dire " + guess + " ?");
    }
    if (b->kind != HmiTypeKind::Structure)
        return t.name + " ne peut h\xC3\xA9riter que d'une structure (" + t.base + " est " + std::string(typeKindLabel(b->kind)) + ")";
    // cycle d'heritage (1.13.0 : par les noms complets, chaque parent lu depuis l'espace de son enfant)
    std::set<std::string> seen{upperOf(fullName(t))};
    std::string chain = fullName(t);
    for (const HmiType* c = b; c; c = types::baseOf(p, *c)) {
        chain += " -> " + fullName(*c);
        if (!seen.insert(upperOf(fullName(*c))).second)
            return t.name + " h\xC3\xA9rite de lui-m\xC3\xAAme : " + chain;
    }
    return {};
}

std::string methodProblem(const Project& p, const HmiType& owner, const HmiFunction& m) {
    const std::string here = owner.name + "." + m.name;
    if (!isIdentifier(m.name)) return owner.name + " : nom de m\xC3\xA9thode invalide '" + m.name + "'";
    // une methode privee ne peut pas etre virtuelle (rien a redefinir de l'exterieur)
    if (m.visibility == Visibility::Private && m.isVirtual)
        return here + " : une m\xC3\xA9thode priv\xC3\xA9" "e ne peut pas \xC3\xAAtre virtuelle (rien \xC3\xA0 red\xC3\xA9" "finir de l'ext\xC3\xA9rieur)";
    // 1.13.0 : le retour et les parametres se lisent comme ceux d'une fonction IHM (une locale) : un type simple, un type
    // IHM, une reference (REF_TO I_Demarrable), ANY, POINTER TO... - pas comme un membre (une place Modbus) ; une
    // interface seule y est refusee (REF_TO I_x).
    const auto knownName = [&p](std::string_view t) { return types::knownTypeName(p, t); };
    const auto typeProblem = [&](const std::string& t) -> std::string {
        if (const std::string iface = types::interfaceAsData(p, t, true); !iface.empty()) return iface;
        if (localTypeSupported(t) || richLocalType(t, knownName)) return {};
        std::string why;
        if (types::validType(p, t, &why)) return {};
        return why.empty() ? "type illisible \xC2\xAB " + t + " \xC2\xBB" : why;
    };
    // retour inconnu ?
    if (!m.returnType.empty())
        if (const std::string why = typeProblem(m.returnType); !why.empty()) return here + " : type de retour " + why;
    // parametres illisibles ?
    for (const auto& d : m.decls)
        if (d.kind == DeclKind::Parameter)
            if (const std::string why = typeProblem(d.type); !why.empty()) return here + " : param\xC3\xA8tre " + d.name + " : " + why;
    // OVERRIDE : la meme methode doit exister VIRTUAL chez un parent, meme signature.
    bool overridesParent = false;
    const HmiType* def = nullptr;
    for (const HmiType* b : baseChain(p, fullName(owner)))      // 1.13.0 : nom complet (Gaz.T_Vanne)
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
namespace {
// Un texte de type (INT, T_Pompe, ARRAY[1..3] OF Gaz.T_Pompe, LIST OF T_Pompe, T_Mesure<T_Pompe>) ou `before` (court) ou
// `beforeFull` (complet) devient `after` / `afterFull`. Rend vrai si le texte change.
bool renameTypeText(std::string& type, std::string_view before, std::string_view beforeFull, std::string_view after,
                    std::string_view afterFull) {
    std::string out;
    out.reserve(type.size() + 8);
    bool changed = false;
    for (std::size_t i = 0; i < type.size();) {
        if (!identStart(type[i]) || (i > 0 && (identPart(type[i - 1]) || type[i - 1] == '#'))) { out += type[i++]; continue; }
        std::size_t j = i;
        // un nom complet (Gaz.T_Vanne) se lit d'un bloc ; T_MODE#Auto : le mot avant # seulement
        while (j < type.size() && (identPart(type[j]) || (type[j] == '.' && j + 1 < type.size() && identStart(type[j + 1])))) ++j;
        const std::string word = type.substr(i, j - i);
        if (!beforeFull.empty() && sameName(word, beforeFull)) { out += afterFull; changed = true; }
        else if (sameName(word, before)) { out += after; changed = true; }
        else out += word;
        i = j;
    }
    if (changed) type = std::move(out);
    return changed;
}
// DYNAMIC_CAST<T_Pompe>, STATIC_CAST<Gaz.T_Pompe> dans du code : le type entre chevrons suit.
std::string renameTypeInCasts(const std::string& code, std::string_view before, std::string_view beforeFull, std::string_view after,
                              std::string_view afterFull) {
    if (code.find('<') == std::string::npos) return code;
    const std::string blank = codeOnly(code);
    std::string out;
    std::size_t copied = 0;
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (blank[i] != '<' || i < 11) continue;
        std::size_t h = i;
        while (h > 0 && (code[h - 1] == ' ' || code[h - 1] == '\t')) --h;
        std::size_t w = h;
        while (w > 0 && identPart(code[w - 1])) --w;
        const std::string head = upperOf(std::string_view(code).substr(w, h - w));
        if (head != "DYNAMIC_CAST" && head != "STATIC_CAST") continue;
        const auto close = code.find('>', i + 1);
        if (close == std::string::npos) continue;
        std::string inner = code.substr(i + 1, close - i - 1);
        if (!renameTypeText(inner, before, beforeFull, after, afterFull)) continue;
        out.append(code, copied, i + 1 - copied);
        out += inner;
        copied = close;
    }
    if (copied == 0) return code;
    out.append(code, copied, std::string::npos);
    return out;
}
} // namespace

std::size_t renameTypeEverywhere(Project& p, Id id, std::string_view newName) {
    HmiType* t = p.hmiType(id);
    const std::string name = trimmed(newName);
    if (!t || name.empty() || t->name == name) return 0;
    const std::string before = t->name;
    const std::string beforeFull = fullName(*t), afterFull = qualifiedName(t->ns, name);
    // Qui en herite, qui l'implemente : par IDENTITE (lu depuis l'espace de chacun), avant de renommer - un homonyme
    // d'un autre espace (Util.T_Vanne) ne suit pas.
    std::vector<Id> extenders;
    std::vector<std::pair<Id, std::size_t>> implementers;
    for (const auto& x : p.programs.types) {
        if (types::baseOf(p, x) == t) extenders.push_back(x.id);
        for (std::size_t k = 0; k < x.interfaces.size(); ++k)
            if (types::typeFrom(p, x.ns, x.interfaces[k], &x) == t) implementers.emplace_back(x.id, k);
    }
    std::size_t changed = 1;
    const auto type = [&](std::string& text) { if (renameTypeText(text, before, beforeFull, name, afterFull)) ++changed; };
    t->name = name;
    for (auto& x : p.programs.types) {
        for (auto& m : x.members) type(m.type);
        for (auto& m : x.methods) {                     // les signatures des methodes (retour, parametres, locales)
            type(m.returnType);
            for (auto& d : m.decls) type(d.type);
        }
    }
    for (const Id xid : extenders)
        if (HmiType* x = p.hmiType(xid)) {
            x->base = x->base.find('.') != std::string::npos ? afterFull : name;
            ++changed;
        }
    for (const auto& [xid, k] : implementers)
        if (HmiType* x = p.hmiType(xid); x && k < x->interfaces.size()) {
            x->interfaces[k] = x->interfaces[k].find('.') != std::string::npos ? afterFull : name;
            ++changed;
        }
    for (auto& v : p.programs.variables) type(v.type);
    for (auto& f : p.programs.functions) type(f.returnType);
    forEachDeclarations(p, [&](std::vector<Declaration>& list) { for (auto& d : list) type(d.type); });
    for (auto& vw : p.views) {
        for (auto& prm : vw.params) type(prm.type);      // 1.12.11 (lot G) : sa liaison Equipement en depend
        for (auto& fn : vw.functions) type(fn.returnType);
    }
    changed += renameTypeInOperators(p, before, name);   // 1.10 (chantier S2) : les operateurs suivent
    changed += renameEnumType(p, before, name);          // 1.10 (chantier E) : T_ANCIEN#x -> T_NOUVEAU#x
    changed += eqlink::renameType(p, before, name);      // 1.12.11 (lot G) : sa liaison Equipement
    // DYNAMIC_CAST<T_Pompe> dans le code (corps des methodes compris).
    forEachCode(p, [&](std::string& text, CodeForm form, const CodeSite&) {
        if (form != CodeForm::Code || text.empty()) return;
        std::string next = renameTypeInCasts(text, before, beforeFull, name, afterFull);
        if (next != text) { text = std::move(next); ++changed; }
    });
    return changed;
}

void renumberMethods(Project& pr, std::vector<HmiFunction>& methods) {
    for (auto& m : methods) m.id = pr.allocate();
}

} // namespace hmi
