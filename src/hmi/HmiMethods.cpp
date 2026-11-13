// =============================================================================
//  hmi/HmiMethods.cpp - les methodes, l'heritage et les interfaces (1.13.0, lot J1)
//  Voir HmiMethods.hpp. Calque sur HmiOperators.cpp : une methode devient une
//  FUNCTION interne (methodFunctionText) que l'interpreteur appelle (resolveMethod).
// =============================================================================
#include "HmiMethods.hpp"

#include "HmiCalc.hpp"       // hmi::closest : le nom connu le plus proche (veux-tu dire ?)
#include "HmiDecl.hpp"       // 1.11.18 : les declarations du modele, reconstruites (decl::codeOf)
#include "HmiDeclEdit.hpp"   // 1.50.1 : renommer un parametre d'une redefinition (la machinerie des declarations)
#include "HmiFunctionTests.hpp"   // 1.50.1 : les cas d'une redefinition suivent ses parametres
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

// 1.13.0 : ce qu'est chaque caractere du texte - 0 : du code, 's' : une chaine (ses guillemets compris), 'c' : un
// commentaire. codeOnly blanchit les deux pareil : une apostrophe d'un commentaire ("// s'il faut") n'ouvre pas une chaine.
std::string kindsOf(std::string_view s) {
    std::string out(s.size(), '\0');
    std::size_t i = 0;
    const auto mark = [&](std::size_t from, std::size_t to, char k) {
        for (std::size_t m = from; m < to && m < out.size(); ++m) out[m] = k;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            std::size_t k = i + 1;
            while (k < s.size() && s[k] != c) k += s[k] == '$' ? 2 : 1;
            mark(i, k + 1, 's');
            i = k + 1;
        } else if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            const std::size_t stop = end == std::string_view::npos ? s.size() : end + 2;
            mark(i, stop, 'c');
            i = stop;
        } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            const std::size_t stop = end == std::string_view::npos ? s.size() : end;
            mark(i, stop, 'c');
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

namespace {
// 1.50.1 : le suffixe d'une surcharge - une autre methode du meme nom dans le type, apres la premiere : "__<id>" ; la
// premiere (et une methode seule de son nom) : rien.
std::string overloadSuffix(const HmiType& owner, const HmiFunction& m) {
    bool first = true;
    for (const auto& x : owner.methods) {
        if (!sameName(x.name, m.name)) continue;
        if (x.id == m.id) return first ? std::string{} : "__" + std::to_string(m.id);
        first = false;
    }
    return {};
}
// Le nom interne de `m` portee par le type de nom `ownerType` (une surcharge : son suffixe) ; type inconnu : le nom d'avant.
std::string internalNameOf(const Project& p, std::string_view ownerType, const HmiFunction& m) {
    const HmiType* t = p.hmiTypeByName(trimmed(ownerType));
    return methodFunctionName(ownerType, m) + (t ? overloadSuffix(*t, m) : std::string{});
}
} // namespace

std::string methodFunctionName(const HmiType& owner, const HmiFunction& m) {
    return methodFunctionName(fullName(owner), m) + overloadSuffix(owner, m);
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
// dans un texte, les trous {Debit:0.0} d'un message (LEVER, IHM_LOG, IHM_JOURNAL) lisent SELF.Debit - seulement
// la : JSON_LIRE('{"Debit":1}'), CONCAT('{Debit}', ...) restent tels quels. Un commentaire est recopie tel quel (ses
// apostrophes n'ouvrent pas de chaine).
// `bound` (facultatif) : les mots nus que le corps lit sur SELF ([debut, fin) dans le corps sans reperes) - un membre lu
// comme valeur, une methode appelee, son propre nom (le retour) : methodSelfWords.
static std::string rewriteBody(const Project& p, std::string_view ownerType, const HmiFunction& m,
                               std::string_view internal, std::vector<std::pair<std::size_t, std::size_t>>* bound = nullptr) {
    const std::string body = markers::strip(m.body);
    const std::string code = codeOnly(body);
    const std::string kind = kindsOf(body);
    // L'appel dont une chaine est un argument direct : LEVER('E_X', 'msg {Debit}') -> "LEVER" ; aucun : vide.
    const auto enclosingCall = [&](std::size_t at) -> std::string {
        int depth = 0;
        for (std::size_t k = at; k-- > 0;) {
            const char ch = code[k];
            if (ch == ')' || ch == ']') ++depth;
            else if (ch == '(' || ch == '[') {
                if (depth == 0) {
                    if (ch == '[') return {};
                    std::size_t e = k;
                    while (e > 0 && (code[e - 1] == ' ' || code[e - 1] == '\t')) --e;
                    std::size_t b = e;
                    while (b > 0 && identPart(code[b - 1])) --b;
                    return upperOf(std::string_view(code).substr(b, e - b));
                }
                --depth;
            } else if (ch == ';' && depth == 0) {
                return {};
            }
        }
        return {};
    };
    const auto messageCall = [](std::string_view u) { return u == "LEVER" || u == "RELEVER" || u == "IHM_LOG" || u == "IHM_JOURNAL"; };

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
        // dans un commentaire : tel quel (une apostrophe n'y ouvre pas de chaine)
        if (kind[i] == 'c') { out += body[i++]; continue; }
        // une chaine : celle d'un message (LEVER, IHM_LOG, IHM_JOURNAL) - ses trous {Membre} lisent SELF.Membre ; une
        // autre (JSON_LIRE('{"Debit":1}'), un texte ecrit) : telle quelle.
        if (kind[i] == 's') {
            std::size_t k = i + 1;
            while (k < body.size() && body[k] != body[i]) k += body[k] == '$' ? 2 : 1;
            const std::size_t close = std::min(k, body.size());   // l'apostrophe fermante (ou la fin)
            const std::string_view inner = std::string_view(body).substr(i + 1, close - (i + 1));
            out += body[i];
            out += messageCall(enclosingCall(i)) ? rewriteText(inner) : std::string(inner);
            if (close < body.size()) out += body[close];
            i = close < body.size() ? close + 1 : body.size();
            continue;
        }
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
                    // 1.50.1 : une methode du parent SURCHARGEE (jamais virtuelle : un sous-type ne la redefinit pas) -
                    // SELF.M(...) : le moteur choisit la surcharge par ses arguments, et c'est bien celle du parent.
                    if (pm && owner && ownMethods(*owner, mname).size() > 1) {
                        out += "SELF." + mname + "(";
                        i = paren + 1;
                        continue;
                    }
                    if (pm && owner) {
                        out += methodFunctionName(*owner, *pm) + "(SELF";   // 1.13.0 (lot J8) : Gaz.T_Pompe ; 1.50.1 : son nom interne
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
        if (U == ret && !call) {
            out += internal;
            if (bound) bound->emplace_back(start, i);
            continue;
        }
        // un argument nomme, un champ d'un litteral : (Consigne := x), Ecrire(a, Consigne := x) - son nom reste
        const bool named = [&] {
            if (k + 1 >= body.size() || body[k] != ':' || body[k + 1] != '=') return false;
            std::size_t b = start;
            while (b > 0 && std::isspace(static_cast<unsigned char>(code[b - 1]))) --b;
            return b > 0 && (code[b - 1] == '(' || code[b - 1] == ',');
        }();
        if (memberSet.count(U) && !shadow.count(U) && !call && !named) {
            out += "SELF." + word;
            if (bound) bound->emplace_back(start, i);
            continue;
        }
        // 1.13.0 : une autre methode du type, appelee par son nom nu (Journal()) : SELF.Journal() - SELF est l'objet reel
        // (VAR_IN_OUT), l'appel garde le dispatch virtuel. Une methode passe avant une fonction de meme nom.
        if (call && method(word)) {
            out += "SELF." + word;
            if (bound) bound->emplace_back(start, i);
            continue;
        }
        out += word;
    }
    return out;
}

std::string methodFunctionText(const Project& p, std::string_view ownerType, const HmiFunction& m) {
    const std::string internal = internalNameOf(p, ownerType, m);
    HmiFunction m2 = m;
    m2.body = rewriteBody(p, ownerType, m, internal);
    std::string head = "FUNCTION " + internal + (m.returnType.empty() ? std::string{} : " : " + displayType(m.returnType));
    // 1.50.0 : une methode generique (ses propres T, comme une fonction de symbole) porte son TEMPLATE <...> devant, sur la
    // meme ligne (functionText) ; sans generique, le texte d'avant.
    if (!m.generics.empty()) head = "TEMPLATE <" + genericsText(m.generics) + "> " + head;
    // SELF, en VAR_IN_OUT : une methode qui ecrit un membre le modifie pour de bon. SELF a le type qui
    // DEFINIT la methode ; un sous-type passe la ou ce parent est attendu (montee du VAR_IN_OUT).
    // 1.13.0 (J1b) : l'en-tete et SELF sur la LIGNE 1, comme functionText - la ligne N du corps est la ligne N de
    // l'editeur (les messages de Compiler, les points d'arret du debogueur).
    std::string self = " VAR_IN_OUT SELF : " + std::string(trimmed(ownerType)) + "; END_VAR ";
    return head + self + decl::codeOf(m2) + "\nEND_FUNCTION\n";
}

std::string methodInternalText(const Project& p, std::string_view ownerType, const HmiFunction& m, std::string_view text) {
    HmiFunction copy = m;
    copy.body = std::string(text);
    return rewriteBody(p, ownerType, copy, internalNameOf(p, ownerType, m));
}

std::vector<std::pair<std::size_t, std::size_t>> methodSelfWords(const Project& p, std::string_view ownerType, const HmiFunction& m,
                                                                 std::string_view body) {
    HmiFunction copy = m;
    copy.body = std::string(body);
    std::vector<std::pair<std::size_t, std::size_t>> bound;
    (void)rewriteBody(p, ownerType, copy, internalNameOf(p, ownerType, m), &bound);
    // rewriteBody lit le corps sans les $ des reperes : ses positions ramenees au texte tel qu'il est ecrit.
    const auto kept = markers::stripKeep(body);
    if (kept.removed.empty()) return bound;
    const auto back = [&](std::size_t at) {
        for (const std::size_t r : kept.removed) {
            if (r > at) break;
            ++at;
        }
        return at;
    };
    for (auto& [a, b] : bound) {
        const std::size_t length = b - a;
        a = back(a);
        b = a + length;
    }
    return bound;
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
            // 1.50.1 : les surcharges d'un meme type restent cote a cote ; une methode d'un sous-type remplace celle du
            // parent de meme nom ET de meme signature (une redefinition) - a defaut (une faute que Generer dit), la
            // premiere de ce nom, comme avant.
            OwnedMethod* target = nullptr;
            for (auto& o : out)
                if (o.owner != t && sameName(o.method->name, m.name) && sameMethodSignature(*o.method, m)) { target = &o; break; }
            if (!target)
                for (auto& o : out)
                    if (o.owner != t && sameName(o.method->name, m.name)) { target = &o; break; }
            if (target) *target = {t, &m};
            else out.push_back({t, &m});
        }
    return out;
}

std::vector<const HmiFunction*> ownMethods(const HmiType& t, std::string_view name) {
    std::vector<const HmiFunction*> out;
    for (const auto& m : t.methods)
        if (sameName(m.name, name)) out.push_back(&m);
    return out;
}

std::vector<OwnedMethod> effectiveMethods(const Project& p, std::string_view typeName, std::string_view name) {
    std::vector<OwnedMethod> out;
    for (const auto& o : allMethods(p, typeName))
        if (sameName(o.method->name, name)) out.push_back(o);
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
            // 1.50.1 : des surcharges - celle qui a la signature de l'interface (s'il y en a une).
            for (const auto& om : effectiveMethods(p, fullName(type), sig.name))
                if (sameMethodSignature(*om.method, sig)) {
                    mine = om.method;
                    break;
                }
            if (!mine)
                out.push_back(type.name + " d\xC3\xA9" "clare " + iname + " mais n'a pas " + methodSignature(iname, sig).substr(iname.size() + 1));
            else if (!sameMethodSignature(*mine, sig))
                out.push_back(type.name + "." + sig.name + " ne respecte pas la signature de " + iname + " : "
                              + methodSignature(iname, sig).substr(iname.size() + 1));
            // 1.13.0 : une methode privee ne remplit pas un contrat - l'appel par l'interface (e^.Demarrer()) viendrait
            // d'ailleurs que du type, et le moteur le refuserait en marche.
            else if (mine->visibility == Visibility::Private)
                out.push_back(mine->name + " est priv\xC3\xA9" "e : elle ne remplit pas l'interface " + iname);
        }
    }
    return out;
}

const HmiFunction* methodById(const Project& p, Id id, const HmiType** owner) {
    if (id == kNoId) return nullptr;
    for (const auto& t : p.programs.types)
        for (const auto& m : t.methods)
            if (m.id == id) {
                if (owner) *owner = &t;
                return &m;
            }
    return nullptr;
}

// ---------------------------------------------- 1.50.1 : les surcharges -------
namespace {
// `type` nomme-t-il (un mot entier, sans casse) un de ces generiques (T, LIST OF T, T_Mesure<T>) ?
bool mentionsGenericName(std::string_view type, const std::vector<GenericParam>& generics) {
    if (generics.empty()) return false;
    for (std::size_t i = 0; i < type.size();) {
        if (!identStart(type[i])) { ++i; continue; }
        std::size_t j = i;
        while (j < type.size() && identPart(type[j])) ++j;
        const std::string_view w = type.substr(i, j - i);
        for (const auto& g : generics)
            if (sameName(g.name, w)) return true;
        i = j;
    }
    return false;
}
} // namespace

overload::Signature methodOverloadSignature(const HmiType& owner, const HmiFunction& m) {
    overload::Signature sig = overload::signatureOf(m);     // ses generiques : pas de type compare
    sig.name = owner.name + "." + m.name;
    // Les generiques du type (T_Mesure<T>) : un type T se voit a l'appel, pas ici.
    for (auto& prm : sig.params)
        if (mentionsGenericName(prm.type, owner.generics)) prm.type.clear();
    if (mentionsGenericName(sig.result, owner.generics)) sig.result.clear();
    return sig;
}

bool chooseMethod(const Project& p, std::string_view typeName, std::string_view name, const std::vector<overload::Arg>& args,
                  OwnedMethod& chosen, std::string& why, bool* uncertain) {
    if (uncertain) *uncertain = false;
    const auto ms = effectiveMethods(p, typeName, name);
    if (ms.empty()) {
        why = std::string(name) + " n'est pas une m\xC3\xA9thode de " + std::string(typeName);
        return false;
    }
    std::vector<overload::Signature> sigs;
    sigs.reserve(ms.size());
    for (const auto& om : ms) sigs.push_back(methodOverloadSignature(*om.owner, *om.method));
    const auto c = overload::choose(sigs, args);
    if (c.chosen >= 0) {
        chosen = ms[static_cast<std::size_t>(c.chosen)];
        return true;
    }
    if (uncertain) *uncertain = c.uncertain;
    why = c.why;
    return false;
}

std::string methodOverloadProblem(const HmiType& owner, const HmiFunction& m) {
    const auto same = ownMethods(owner, m.name);
    if (same.size() < 2) return {};
    const std::string here = owner.name + "." + m.name;
    if (owner.kind == HmiTypeKind::Interface)
        return here + " : une interface n'a qu'une signature par nom (pas de surcharge) - renomme l'une des deux";
    if (m.isVirtual)
        return here + " est virtuelle et surcharg\xC3\xA9" "e : une m\xC3\xA9thode virtuelle ne se surcharge pas (ses red\xC3\xA9" "finitions la "
               "d\xC3\xA9signent par son nom) - d\xC3\xA9" "coche Virtuelle, ou renomme la surcharge";
    const auto mine = methodOverloadSignature(owner, m);
    for (const HmiFunction* o : same) {
        if (o->id == m.id) continue;
        const auto other = methodOverloadSignature(owner, *o);
        if (overload::sameShape(mine, other))
            return here + " : deux surcharges ont la m\xC3\xAAme forme " + m.name + mine.shape().substr(mine.name.size())
                   + " - un appel ne saurait laquelle prendre ; change les param\xC3\xA8tres de l'une (le retour seul ne les distingue pas)";
    }
    return {};
}

// -------------------------------------------- l'interface avec l'interpreteur ---
bool resolveMethod(const Project& p, std::string_view selfType, std::string_view method, MethodCall& out) {
    const HmiType* owner = nullptr;
    const HmiFunction* m = nullptr;
    // 1.50.1 : "Regler#615" - la surcharge choisie (chooseMethod), par son identifiant.
    if (const auto hash = method.find('#'); hash != std::string_view::npos) {
        const std::string id(method.substr(hash + 1));
        for (const auto& om : effectiveMethods(p, selfType, method.substr(0, hash)))
            if (std::to_string(om.method->id) == id) {
                m = om.method;
                owner = om.owner;
            }
    } else {
        m = effectiveMethod(p, selfType, method, &owner);
    }
    if (!m || !owner) return false;
    // SELF a le type qui DEFINIT la methode (owner) ; l'objet reel (un sous-type) passe la ou owner est
    // attendu (montee du VAR_IN_OUT). Une seule fonction par (owner, methode) : un SUPER la retrouve.
    // 1.13.0 (lot J8) : le nom complet du type (Gaz.T_Pompe) - SELF et le nom interne ; une methode d'un type d'un
    // espace voit les noms de cet espace sans prefixe (son corps qualifie avant le moteur).
    const std::string ownerName = fullName(*owner);
    out.selfType = ownerName;
    out.isVirtual = m->isVirtual;
    out.method = m->id;                                               // 1.13.0 : ses points d'arret (le debogueur)
    out.function = methodFunctionText(p, ownerName, *m);
    if (!owner->ns.empty() || names::mentionsUsing(m->body)) out.function = names::qualify(p, out.function, owner->ns, {"SELF"}, true);
    out.owner = "type " + ownerName + ", m\xC3\xA9thode " + m->name;
    out.key = "methode|" + upperOf(ownerName) + "|" + upperOf(m->name) + "|" + std::to_string(m->id) + "|"
            + std::to_string(std::hash<std::string>{}(out.function));
    // 1.13.0 : une methode privee - le moteur ne l'appelle que d'une methode de son type (le message de Generer).
    out.privateWhy = m->visibility == Visibility::Private
                         ? m->name + " est une m\xC3\xA9thode priv\xC3\xA9" "e de " + owner->name + " : seules ses m\xC3\xA9thodes l'appellent"
                         : std::string{};
    return true;
}

bool resolveMethodInternal(const Project& p, std::string_view internalName, MethodCall& out) {
    // 1.13.0 (lot J8) : les types du projet et ceux des bibliotheques reliees, par leur nom complet.
    const auto tryType = [&](const HmiType& t) {
        const std::string tn = fullName(t);
        for (const auto& m : t.methods)
            if (methodFunctionName(t, m) == internalName) {             // 1.50.1 : une surcharge, son suffixe
                out.selfType = tn;
                out.isVirtual = m.isVirtual;
                out.method = m.id;                                    // 1.13.0 : ses points d'arret (le debogueur)
                out.function = methodFunctionText(p, tn, m);
                if (!t.ns.empty() || names::mentionsUsing(m.body)) out.function = names::qualify(p, out.function, t.ns, {"SELF"}, true);
                out.owner = "type " + tn + ", m\xC3\xA9thode " + m.name;
                out.key = "methode|" + upperOf(tn) + "|" + upperOf(m.name) + "|" + std::to_string(m.id) + "|"
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
    // 1.13.0 (J1b) : les parametres passes tels quels au parent, dans l'ordre (SUPER.Demarrer(Consigne)).
    std::string args;
    for (const auto& d : parent.decls)
        if (d.kind == DeclKind::Parameter) args += (args.empty() ? "" : ", ") + d.name;
    if (!parent.returnType.empty()) s += parent.name + " := SUPER." + parent.name + "(" + args + ");\n";
    else s += "SUPER." + parent.name + "(" + args + ");\n";
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
        // 1.50.0 : un generique de la methode (ses T, comme une fonction de symbole) ou du type (T_Mesure<T>) : permis.
        for (const auto* gens : {&m.generics, &owner.generics})
            for (const auto& g : *gens)
                if (sameName(g.name, trimmed(t))) return {};
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
            // 1.13.0 : un appel par le parent (p^.Demarrer() sur une T_Pompe) arriverait, par le dispatch, dans la
            // redefinition privee - que le moteur refuserait en marche.
            if (m.visibility == Visibility::Private && pm->visibility != Visibility::Private)
                return here + " : une red\xC3\xA9" "finition ne peut pas \xC3\xAAtre plus priv\xC3\xA9" "e que la m\xC3\xA9thode qu'elle red\xC3\xA9" "finit ("
                       + b->name + "." + m.name + " est publique)";
            if (!sameMethodSignature(*pm, m))
                return here + " red\xC3\xA9" "finit " + b->name + "." + m.name + " mais change sa signature (attendu " + methodSignature(b->name, *pm).substr(b->name.size() + 1) + ")";
            break;
        }
    (void)overridesParent; (void)def;
    // 1.50.1 : les surcharges - la meme forme qu'une autre de son nom, une virtuelle surchargee, une interface.
    if (std::string w = methodOverloadProblem(owner, m); !w.empty()) return w;
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

// ------------------------------------------------ l'ecran des objets (J1b) ---
std::string methodParamsText(const HmiFunction& m) {
    std::string out;
    for (const auto& d : m.decls) {
        if (d.kind != DeclKind::Parameter) continue;
        if (!out.empty()) out += "; ";
        if (d.mode == PassMode::InOut) out += "VAR_IN_OUT ";
        else if (d.mode == PassMode::Out) out += "VAR_OUTPUT ";
        out += d.name + " : " + d.type;
    }
    return out;
}

bool setMethodParams(HmiFunction& m, std::string_view text, const std::function<Id()>& allocate, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    // Les morceaux : un ; ou une , de premier niveau (ARRAY[1..3, 1..2] OF REAL, MAP[STRING] OF INT restent entiers).
    std::vector<std::string> parts;
    {
        std::string cur;
        int depth = 0;
        for (const char c : text) {
            if (c == '[' || c == '(' || c == '<') ++depth;
            if ((c == ']' || c == ')' || c == '>') && depth > 0) --depth;
            if ((c == ';' || c == ',') && depth == 0) {
                parts.push_back(cur);
                cur.clear();
                continue;
            }
            cur += c;
        }
        parts.push_back(cur);
    }
    std::vector<Declaration> params;
    std::set<std::string> seen;
    for (auto& raw : parts) {
        std::string part(trimmed(raw));
        if (part.empty()) continue;
        PassMode mode = PassMode::In;
        const std::string up = upperOf(part);
        for (const auto& [word, pm] : {std::pair<const char*, PassMode>{"VAR_IN_OUT ", PassMode::InOut}, {"VAR_OUTPUT ", PassMode::Out},
                                       {"VAR_INPUT ", PassMode::In}})
            if (up.rfind(word, 0) == 0) {
                mode = pm;
                part = std::string(trimmed(std::string_view(part).substr(std::string_view(word).size())));
                break;
            }
        const auto colon = part.find(':');
        if (colon == std::string::npos) return fail("\xC2\xAB " + part + " \xC2\xBB : \xC3\xA9" "cris nom : type (Entree : REAL)");
        const std::string name(trimmed(std::string_view(part).substr(0, colon)));
        const std::string type(trimmed(std::string_view(part).substr(colon + 1)));
        if (!isIdentifier(name)) return fail("nom de param\xC3\xA8tre invalide \xC2\xAB " + name + " \xC2\xBB");
        if (type.empty()) return fail("le param\xC3\xA8tre " + name + " n'a pas de type");
        if (!seen.insert(upperOf(name)).second) return fail("param\xC3\xA8tre en double : " + name);
        if (sameName(name, m.name)) return fail(name + " est le nom de la m\xC3\xA9thode (son retour)");
        Declaration d;
        for (const auto& old : m.decls)
            if (old.kind == DeclKind::Parameter && sameName(old.name, name)) d = old;   // son id, sa description
        if (d.id == kNoId && allocate) d.id = allocate();
        d.kind = DeclKind::Parameter;
        d.name = name;
        d.type = type;
        d.mode = mode;
        params.push_back(std::move(d));
    }
    // Les parametres d'abord (dans l'ordre ecrit), puis les locales et constantes de toujours.
    std::vector<Declaration> next = std::move(params);
    for (const auto& d : m.decls)
        if (d.kind != DeclKind::Parameter) next.push_back(d);
    m.decls = std::move(next);
    return true;
}

namespace {
// Dans `body` (code seulement : ni chaine ni commentaire), chaque mot `from` (sans casse) qui convient a `pick` devient
// `to`. `pick(prefix, call)` : `prefix` le mot devant le point ("SELF", "SUPER"... ; vide : un nom nu), `call` suivi d'une
// parenthese. Rend vrai si le texte change.
bool renameWordIn(std::string& body, std::string_view from, std::string_view to,
                  const std::function<bool(std::string_view prefix, bool call)>& pick) {
    const std::string kinds = kindsOf(body);
    std::string out;
    out.reserve(body.size() + 8);
    bool changed = false;
    std::size_t i = 0;
    while (i < body.size()) {
        if (kinds[i] != 0 || !identStart(body[i]) || (i > 0 && identPart(body[i - 1]))) {
            out += body[i++];
            continue;
        }
        std::size_t j = i;
        while (j < body.size() && identPart(body[j])) ++j;
        const std::string_view word(body.data() + i, j - i);
        if (!sameName(word, from)) {
            out.append(word);
            i = j;
            continue;
        }
        // Le mot devant le point (SELF.X, SUPER.X, Pompe1.X) ; vide : un nom nu. Un # devant : un litteral (T#X).
        std::string prefix;
        std::size_t b = i;
        while (b > 0 && (body[b - 1] == ' ' || body[b - 1] == '\t')) --b;
        bool skip = b > 0 && body[b - 1] == '#';
        if (!skip && b > 0 && body[b - 1] == '.') {
            std::size_t e = b - 1;
            while (e > 0 && (body[e - 1] == ' ' || body[e - 1] == '\t')) --e;
            std::size_t s0 = e;
            while (s0 > 0 && identPart(body[s0 - 1])) --s0;
            prefix = body.substr(s0, e - s0);
            if (prefix.empty()) skip = true;           // a[1].X, F().X : pas un nom simple devant
        }
        std::size_t k = j;
        while (k < body.size() && (body[k] == ' ' || body[k] == '\t')) ++k;
        const bool call = k < body.size() && body[k] == '(';
        if (!skip && pick(prefix, call)) {
            out.append(to);
            changed = true;
        } else {
            out.append(word);
        }
        i = j;
    }
    if (changed) body = std::move(out);
    return changed;
}
} // namespace

std::size_t renameMethodEverywhere(Project& p, Id method, std::string_view rawName) {
    const std::string name(trimmed(rawName));
    const HmiType* ownerC = nullptr;
    const HmiFunction* mc = methodById(p, method, &ownerC);
    if (!mc || !ownerC || name.empty() || mc->name == name) return 0;
    const std::string old = mc->name;
    const Id ownerId = ownerC->id;
    // Le type et ses sous-types (par identite : un homonyme d'un autre espace ne suit pas).
    std::set<Id> family{ownerId};
    for (const auto& t : p.programs.types) {
        if (t.id == ownerId) continue;
        for (const HmiType* b : baseChain(p, fullName(t)))
            if (b->id == ownerId) {
                family.insert(t.id);
                break;
            }
    }
    std::size_t changed = 0;
    for (auto& t : p.programs.types) {
        if (!family.count(t.id)) continue;
        for (auto& m : t.methods) {
            // La methode elle-meme et ses redefinitions : le nom, et dans le corps le nom nu (le retour, un appel).
            // 1.50.1 : ses surcharges (le meme nom dans son type) avec elle - un appel X(...) les vise toutes.
            const bool self = m.id == method || sameName(m.name, old);
            if (self) {
                m.name = name;
                ++changed;
            }
            const bool body = renameWordIn(m.body, old, name, [&](std::string_view prefix, bool call) {
                if (prefix.empty()) return self || call;                 // EnDefaut := ... (soi) ; Journal() (une soeur)
                return sameName(prefix, "SELF") || sameName(prefix, "THIS") || sameName(prefix, "SUPER");
            });
            changed += body ? 1 : 0;
        }
    }
    return changed;
}

// ---------------------------- 1.50.1 : la signature d'une virtuelle, suivie ---
std::vector<OwnedMethod> overridesOf(const Project& p, Id method) {
    std::vector<OwnedMethod> out;
    const HmiType* owner = nullptr;
    const HmiFunction* m = methodById(p, method, &owner);
    if (!m || !owner) return out;
    for (const auto& t : p.programs.types) {
        if (t.id == owner->id) continue;
        bool below = false;
        for (const HmiType* b : baseChain(p, fullName(t)))
            if (b->id == owner->id) {
                below = true;
                break;
            }
        if (!below) continue;
        for (const auto& x : t.methods)
            if (sameName(x.name, m->name)) {
                out.push_back({&t, &x});
                break;
            }
    }
    return out;
}

namespace {
std::vector<const Declaration*> paramsOf(const HmiFunction& f) {
    std::vector<const Declaration*> out;
    for (const auto& d : f.decls)
        if (d.kind == DeclKind::Parameter) out.push_back(&d);
    return out;
}

// Les arguments d'un appel, de `open` (la parenthese ouvrante) a sa fermante : [debut, fin) de chacun dans `body`,
// coupes aux virgules de premier niveau (ni dans une chaine, ni dans un commentaire, ni entre () ou []). `close` : la
// fermante. Faux : pas de fermante.
bool callArguments(const std::string& body, const std::string& kinds, std::size_t open, std::vector<std::pair<std::size_t, std::size_t>>& args,
                   std::size_t& close) {
    int depth = 0;
    std::size_t start = open + 1;
    for (std::size_t k = open + 1; k < body.size(); ++k) {
        if (kinds[k] != 0) continue;
        const char c = body[k];
        if (c == '(' || c == '[') ++depth;
        else if ((c == ')' || c == ']') && depth > 0) --depth;
        else if (c == ')' && depth == 0) {
            if (body.find_first_not_of(" \t\r\n", start) < k || !args.empty()) args.emplace_back(start, k);
            close = k;
            return true;
        } else if (c == ',' && depth == 0) {
            args.emplace_back(start, k);
            start = k + 1;
        }
    }
    return false;
}

// SUPER.M(...) dans le corps d'une redefinition : ses arguments suivent la nouvelle signature de M. Par position (autant
// d'arguments que de parametres d'avant) : chacun suit son parametre, un parametre ajoute passe par son nom (la
// redefinition l'a aussi), un retire s'en va. Par nom (a := x) : le nom suit, un retire s'en va, un ajoute s'ajoute.
// `oldP`, `newP` : les parametres d'avant et d'apres de M ; `from[j]` : l'indice dans oldP du parametre j de newP (-1 :
// ajoute). Rend vrai si le corps change.
bool followSuperCalls(std::string& body, std::string_view name, const std::vector<Declaration>& oldP, const std::vector<Declaration>& newP,
                      const std::vector<int>& from) {
    bool changed = false;
    for (std::size_t at = 0;;) {
        const std::string kinds = kindsOf(body);
        // le prochain SUPER (un mot entier, dans le code)
        std::size_t i = at;
        bool found = false;
        std::size_t open = 0;
        for (; i + 5 <= body.size(); ++i) {
            if (kinds[i] != 0 || !identStart(body[i]) || (i > 0 && (identPart(body[i - 1]) || body[i - 1] == '.'))) continue;
            std::size_t j = i;
            while (j < body.size() && identPart(body[j])) ++j;
            if (!sameName(std::string_view(body).substr(i, j - i), "SUPER")) { i = j; continue; }
            std::size_t d = j;
            while (d < body.size() && (body[d] == ' ' || body[d] == '\t')) ++d;
            if (d >= body.size() || body[d] != '.') { i = j; continue; }
            std::size_t n0 = d + 1;
            while (n0 < body.size() && (body[n0] == ' ' || body[n0] == '\t')) ++n0;
            std::size_t n1 = n0;
            while (n1 < body.size() && identPart(body[n1])) ++n1;
            std::size_t paren = n1;
            while (paren < body.size() && (body[paren] == ' ' || body[paren] == '\t')) ++paren;
            if (n1 > n0 && paren < body.size() && body[paren] == '(' && sameName(std::string_view(body).substr(n0, n1 - n0), name)) {
                found = true;
                open = paren;
                break;
            }
            i = j;
        }
        if (!found) break;
        std::vector<std::pair<std::size_t, std::size_t>> args;
        std::size_t close = 0;
        if (!callArguments(body, kinds, open, args, close)) break;
        std::vector<std::string> texts;
        for (const auto& [a, b] : args) texts.push_back(trimmed(std::string_view(body).substr(a, b - a)));
        // un argument nomme : "Nom := x" ou "Nom => y"
        const auto namedOf = [](const std::string& t, std::string& n, std::string& rest) {
            std::size_t k = 0;
            while (k < t.size() && identPart(t[k])) ++k;
            if (k == 0 || !identStart(t[0])) return false;
            std::size_t q = k;
            while (q < t.size() && (t[q] == ' ' || t[q] == '\t')) ++q;
            if (q + 1 < t.size() && ((t[q] == ':' && t[q + 1] == '=') || (t[q] == '=' && t[q + 1] == '>'))) {
                n = t.substr(0, k);
                rest = t.substr(k);
                return true;
            }
            return false;
        };
        bool anyNamed = false, allNamed = !texts.empty();
        for (const auto& t : texts) {
            std::string n, r;
            const bool named = namedOf(t, n, r);
            anyNamed = anyNamed || named;
            allNamed = allNamed && named;
        }
        std::vector<std::string> next;
        bool rewrite = false;
        if (!anyNamed && texts.size() == oldP.size()) {
            for (std::size_t j = 0; j < newP.size(); ++j)
                next.push_back(from[j] >= 0 ? texts[static_cast<std::size_t>(from[j])] : newP[j].name);
            rewrite = true;
        } else if (allNamed) {
            for (const auto& t : texts) {
                std::string n, r;
                (void)namedOf(t, n, r);
                int k = -1;
                for (std::size_t q = 0; q < oldP.size(); ++q)
                    if (sameName(oldP[q].name, n)) k = static_cast<int>(q);
                if (k < 0) {                                   // un nom inconnu : tel quel
                    next.push_back(t);
                    continue;
                }
                int j = -1;
                for (std::size_t q = 0; q < from.size(); ++q)
                    if (from[q] == k) j = static_cast<int>(q);
                if (j >= 0) next.push_back(newP[static_cast<std::size_t>(j)].name + r);   // retire (j < 0) : s'en va
            }
            for (std::size_t j = 0; j < newP.size(); ++j)
                if (from[j] < 0) next.push_back(newP[j].name + (newP[j].mode == PassMode::Out ? " => " : " := ") + newP[j].name);
            rewrite = true;
        }
        if (!rewrite) {
            at = close + 1;
            continue;
        }
        std::string inside;
        for (const auto& t : next) inside += (inside.empty() ? "" : ", ") + t;
        const std::string before = body.substr(open + 1, close - open - 1);
        if (trimmed(before) != inside) {
            body.replace(open + 1, close - open - 1, inside);
            changed = true;
        }
        at = open + 1 + inside.size() + 1;
    }
    return changed;
}
} // namespace

bool sameMethodHead(const HmiFunction& a, const HmiFunction& b) {
    if (a.returnType != b.returnType || a.returnUnit != b.returnUnit) return false;
    const auto pa = paramsOf(a), pb = paramsOf(b);
    if (pa.size() != pb.size()) return false;
    for (std::size_t k = 0; k < pa.size(); ++k) {
        const Declaration& x = *pa[k];
        const Declaration& y = *pb[k];
        if (x.name != y.name || x.type != y.type || x.mode != y.mode || x.value != y.value || x.unit != y.unit) return false;
    }
    return true;
}

bool followOverrides(Project& p, Id method, const HmiFunction& before, std::vector<std::string>* touched, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    const HmiType* ownerC = nullptr;
    const HmiFunction* baseC = methodById(p, method, &ownerC);
    if (!baseC || !ownerC) return fail("m\xC3\xA9thode introuvable");
    const HmiFunction now = *baseC;
    std::vector<Declaration> oldP, newP;
    for (const auto* d : paramsOf(before)) oldP.push_back(*d);
    for (const auto* d : paramsOf(now)) newP.push_back(*d);
    // Chaque parametre d'apres : son indice d'avant (par son identifiant ; a defaut son nom) ; -1 : ajoute.
    std::vector<int> from(newP.size(), -1);
    for (std::size_t j = 0; j < newP.size(); ++j)
        for (std::size_t k = 0; k < oldP.size(); ++k)
            if ((newP[j].id != kNoId && oldP[k].id == newP[j].id) || (newP[j].id == kNoId && sameName(oldP[k].name, newP[j].name))) {
                from[j] = static_cast<int>(k);
                break;
            }
    std::vector<std::pair<Id, Id>> targets;               // (type, redefinition)
    for (const auto& om : overridesOf(p, method)) targets.emplace_back(om.owner->id, om.method->id);
    namespace de = decledit;
    for (const auto& [typeId, overId] : targets) {
        HmiType* t = p.hmiType(typeId);
        if (!t) continue;
        const auto findOver = [&]() -> HmiFunction* {
            for (auto& x : t->methods)
                if (x.id == overId) return &x;
            return nullptr;
        };
        HmiFunction* o = findOver();
        if (!o) continue;
        const std::string label = t->name + "." + o->name;
        de::Place at;
        at.kind = de::Place::Kind::TypeMethod;
        at.type = typeId;
        at.id = overId;
        // Ses parametres d'avant, un pour un avec ceux de la virtuelle (la meme signature) : par position.
        std::vector<Id> mine;
        for (const auto* d : paramsOf(*o)) mine.push_back(d->id);
        const auto rowOf = [&](Id declId) -> std::size_t {
            HmiFunction* f = findOver();
            if (!f) return static_cast<std::size_t>(-1);
            const auto rows = de::rowsOf(f->decls, de::Tab::Parameters);
            for (std::size_t r = 0; r < rows.size(); ++r)
                if (f->decls[rows[r]].id == declId) return r;
            return static_cast<std::size_t>(-1);
        };
        // (1) Les renommes : un parametre de meme nom que celui de la virtuelle prend son nouveau nom - par la
        // machinerie des declarations (son corps suit) ; en deux temps (un nom de passage), un echange de noms passe.
        std::vector<std::pair<Id, std::string>> renames;
        for (std::size_t j = 0; j < newP.size(); ++j) {
            const int k = from[j];
            if (k < 0 || static_cast<std::size_t>(k) >= mine.size()) continue;
            const Declaration* d = nullptr;
            for (const auto& x : findOver()->decls)
                if (x.id == mine[static_cast<std::size_t>(k)]) d = &x;
            if (d && sameName(d->name, oldP[static_cast<std::size_t>(k)].name) && d->name != newP[j].name)
                renames.emplace_back(d->id, newP[j].name);
        }
        for (int phase = 0; phase < 2; ++phase)
            for (const auto& [declId, target] : renames) {
                std::string w;
                const std::string text = phase == 0 ? "Suivi_" + std::to_string(declId) : target;
                if (!de::set(p, at, de::Tab::Parameters, rowOf(declId), de::Column::Name, text, &w, nullptr))
                    return fail(label + " : le param\xC3\xA8tre " + target + " - " + w);
            }
        o = findOver();
        if (!o) continue;
        const auto namesBefore = testParameterNames(*o);   // apres les renommages : ses cas suivent par leur nom
        // (2) La liste d'apres : un parametre garde (son identifiant, sa documentation) prend le type, le mode, le
        // defaut et l'unite de la virtuelle ; un ajoute est une copie (un identifiant neuf) ; un retire s'en va.
        std::vector<Declaration> next;
        for (std::size_t j = 0; j < newP.size(); ++j) {
            const int k = from[j];
            Declaration d;
            bool kept = false;
            if (k >= 0 && static_cast<std::size_t>(k) < mine.size())
                for (const auto& x : o->decls)
                    if (x.id == mine[static_cast<std::size_t>(k)]) {
                        d = x;
                        kept = true;
                    }
            if (!kept) {
                d = newP[j];
                d.id = p.allocate();
                // le nom ajoute ne doit rien cacher dans la redefinition (une locale, une constante, SELF...)
                const de::Code c = de::locate(p, at);
                std::string w;
                if (c.valid() && !de::nameAllowed(c, d.name, static_cast<std::size_t>(-1), &w))
                    return fail(label + " : le param\xC3\xA8tre ajout\xC3\xA9 " + d.name + " - " + w);
            } else {
                d.type = newP[j].type;
                d.mode = newP[j].mode;
                d.value = newP[j].value;
                d.unit = newP[j].unit;
            }
            d.kind = DeclKind::Parameter;
            next.push_back(std::move(d));
        }
        for (const auto& x : o->decls)
            if (x.kind != DeclKind::Parameter) next.push_back(x);
        o->decls = std::move(next);
        o->returnType = now.returnType;
        o->returnUnit = now.returnUnit;
        (void)followSuperCalls(o->body, now.name, oldP, newP, from);
        (void)remapTestArguments(*o, namesBefore, testParameterNames(*o));
        if (touched) touched->push_back(label);
    }
    return true;
}

std::vector<std::string> implementersOf(const Project& p, const HmiType& iface) {
    std::vector<std::string> out;
    if (iface.kind != HmiTypeKind::Interface) return out;
    const std::string full = fullName(iface);
    for (const auto& t : p.programs.types)
        if (t.kind == HmiTypeKind::Structure && implementsInterface(p, fullName(t), full)) out.push_back(fullName(t));
    return out;
}

std::string interfaceDeclaration(const HmiType& t) {
    std::string s = "TYPE " + t.name + " INTERFACE\n";
    for (const auto& m : t.methods) {
        s += "    METHOD " + m.name;
        const std::string params = methodParamsText(m);
        if (!params.empty()) s += " (" + params + ")";
        if (!m.returnType.empty()) s += " : " + m.returnType;
        s += "\n";
    }
    s += "END_TYPE";
    return s;
}

std::vector<std::string> baseChoices(const Project& p, const HmiType& t) {
    std::vector<std::string> out{"(aucun)"};
    for (const auto& c : p.programs.types) {
        if (c.id == t.id || c.kind != HmiTypeKind::Structure) continue;
        bool descendant = false;                     // c descend de t : le choisir ferait un cycle
        for (const HmiType* b : baseChain(p, fullName(c)))
            if (b->id == t.id) descendant = true;
        if (!descendant) out.push_back(fullName(c));
    }
    return out;
}

} // namespace hmi
