// =============================================================================
//  hmi/HmiSymbolTypes.cpp - 1.51.0 : les types IHM d'un symbole. Voir l'en-tete.
// =============================================================================
#include "HmiSymbolTypes.hpp"

#include "HmiEnums.hpp"
#include "HmiMethods.hpp"
#include "HmiNames.hpp"
#include "HmiOperators.hpp"
#include "HmiScript.hpp"      // forEachCode
#include "HmiSymbols.hpp"
#include "HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace hmi::symtypes {

namespace {

bool same(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identPart(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
std::string trimmed(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}
bool fail(std::string* why, std::string w) {
    if (why) *why = std::move(w);
    return false;
}

// Les mots d'un texte de type (ARRAY[1..3] OF T_Etat, LIST OF T_Etat, T_Mesure<T_Etat>) : chaque nom court (ni apres un
// point, ni devant un point, ni apres #) passe par `f` (rend son remplacement, ou vide : tel quel).
template <class F>
std::string rewriteWords(std::string_view type, F&& f) {
    std::string out;
    out.reserve(type.size() + 8);
    for (std::size_t i = 0; i < type.size();) {
        if (!identStart(type[i]) || (i > 0 && (identPart(type[i - 1]) || type[i - 1] == '.' || type[i - 1] == '#'))) {
            out += type[i++];
            continue;
        }
        std::size_t j = i;
        while (j < type.size() && identPart(type[j])) ++j;
        const std::string_view word = type.substr(i, j - i);
        const bool head = j < type.size() && type[j] == '.' && j + 1 < type.size() && identStart(type[j + 1]);
        std::string with = head ? std::string{} : f(word);
        out += with.empty() ? std::string(word) : with;
        i = j;
    }
    return out;
}

// Le symbole d'un type porte ; nul : introuvable (ou pas un symbole).
const View* symbolView(const Project& p, Id id) {
    const View* v = p.view(id);
    return v && isSymbolView(*v) ? v : nullptr;
}

// Un nom court du symbole que le projet lit autrement (un homonyme de la racine, d'un autre symbole) : son nom complet.
// Vide : rien a ecrire (le nom court designe deja ce type, ou ce n'est pas un type du symbole).
std::string neededFull(const Project& p, const View& sym, std::string_view word) {
    const HmiType* own = find(p, sym.id, word);
    if (!own) return {};
    if (p.hmiTypeByName(word) == own) return {};
    return qualifiedName(sym.name, own->name);
}

// Un texte de type du symbole, ses noms courts ambigus ecrits en entier ; vrai : change.
bool fixType(const Project& p, const View& sym, std::string& text) {
    if (text.empty()) return false;
    std::string next = rewriteWords(text, [&](std::string_view w) { return neededFull(p, sym, w); });
    if (next == text) return false;
    text = std::move(next);
    return true;
}

std::size_t fixDecls(const Project& p, const View& sym, std::vector<Declaration>& decls) {
    std::size_t n = 0;
    for (auto& d : decls) n += fixType(p, sym, d.type) ? 1u : 0u;
    return n;
}
std::size_t fixOperators(const Project& p, const View& sym, std::vector<HmiOperator>& ops) {
    std::size_t n = 0;
    for (auto& o : ops) {
        n += fixType(p, sym, o.left) ? 1u : 0u;
        n += fixType(p, sym, o.right) ? 1u : 0u;
        n += fixType(p, sym, o.result) ? 1u : 0u;
        n += fixDecls(p, sym, o.decls);
    }
    return n;
}
std::size_t fixFunctions(const Project& p, const View& sym, std::vector<HmiFunction>& fns) {
    std::size_t n = 0;
    for (auto& f : fns) {
        n += fixType(p, sym, f.returnType) ? 1u : 0u;
        n += fixDecls(p, sym, f.decls);
    }
    return n;
}
// Les textes de type d'une vue de symbole (ses parametres, ses fonctions, ses scripts, ses operateurs).
std::size_t fixView(const Project& p, const View& sym, View& v) {
    std::size_t n = 0;
    for (auto& prm : v.params) n += fixType(p, sym, prm.type) ? 1u : 0u;
    n += fixFunctions(p, sym, v.functions);
    for (auto& sc : v.scripts) n += fixDecls(p, sym, sc.decls);
    n += fixOperators(p, sym, v.operators);
    return n;
}
std::size_t fixOwned(const Project& p, const View& sym, HmiType& t) {
    std::size_t n = 0;
    for (auto& m : t.members) n += fixType(p, sym, m.type) ? 1u : 0u;
    n += fixType(p, sym, t.base) ? 1u : 0u;
    for (auto& i : t.interfaces) n += fixType(p, sym, i) ? 1u : 0u;
    n += fixFunctions(p, sym, t.methods);
    // Ses operateurs le nomment par son nom court (TO_STRING(T_Mode) : la regle des operateurs d'un type) : pas reecrits.
    if (t.kind == HmiTypeKind::Callback) n += fixType(p, sym, t.signature) ? 1u : 0u;
    return n;
}

// Le symbole `symbol` et ses types, seuls, dans un petit projet (renommer dedans, puis rendre au projet).
Project subProject(const Project& p, Id symbol) {
    Project sub;
    sub.config = p.config;
    sub.nextId = p.nextId;
    if (const View* v = p.view(symbol)) sub.views.push_back(*v);
    for (const auto& t : p.programs.types)
        if (t.symbol == symbol) sub.programs.types.push_back(t);
    return sub;
}
void putBack(Project& p, const Project& sub, Id symbol) {
    for (const auto& v : sub.views)
        if (View* mine = p.view(v.id)) *mine = v;
    for (const auto& t : sub.programs.types)
        if (HmiType* mine = p.hmiType(t.id)) {
            *mine = t;
            mine->symbol = symbol;
        }
}

// Dans un texte (code ou type) : le nom complet `fromFull` (et fromFull#x) devient `toFull` ; un nom plus long qui le
// prolonge (fromFull.Membre) aussi. Les chaines et les commentaires d'un code ne bougent pas.
std::string renameFull(std::string_view text, std::string_view fromFull, std::string_view toFull, bool code) {
    if (text.find('.') == std::string_view::npos) return std::string(text);
    std::string out;
    out.reserve(text.size() + 8);
    char quote = 0;
    bool line = false, block = false;
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (code) {
            if (line) { out += c; ++i; if (c == '\n') line = false; continue; }
            if (block) { out += c; ++i; if (c == ')' && out.size() >= 2 && out[out.size() - 2] == '*') block = false; continue; }
            if (quote) { out += c; ++i; if (c == quote) quote = 0; continue; }
            if (c == '\'' || c == '"') { quote = c; out += c; ++i; continue; }
            if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') { line = true; out += c; ++i; continue; }
            if (c == '(' && i + 1 < text.size() && text[i + 1] == '*') { block = true; out += "(*"; i += 2; continue; }
        }
        if (!identStart(c) || (i > 0 && (identPart(text[i - 1]) || text[i - 1] == '.' || text[i - 1] == '#'))) {
            out += c;
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < text.size() && (identPart(text[j]) || (text[j] == '.' && j + 1 < text.size() && identStart(text[j + 1])))) ++j;
        const std::string_view word = text.substr(i, j - i);
        if (same(word.substr(0, std::min(word.size(), fromFull.size())), fromFull)
            && (word.size() == fromFull.size() || word[fromFull.size()] == '.')) {
            out += toFull;
            out += word.substr(fromFull.size());
        } else {
            out += word;
        }
        i = j;
    }
    return out;
}

} // namespace

// ---- lire ---------------------------------------------------------------------------------------

std::vector<const HmiType*> typesOf(const Project& p, Id symbol) {
    std::vector<const HmiType*> out;
    if (symbol == kNoId) return out;
    for (const auto& t : p.programs.types)
        if (t.symbol == symbol) out.push_back(&t);
    return out;
}

std::vector<Id> typeIdsOf(const Project& p, Id symbol) {
    std::vector<Id> out;
    for (const auto* t : typesOf(p, symbol)) out.push_back(t->id);
    return out;
}

bool hasTypes(const Project& p, Id symbol) {
    if (symbol == kNoId) return false;
    return std::any_of(p.programs.types.begin(), p.programs.types.end(), [&](const HmiType& t) { return t.symbol == symbol; });
}

const View* ownerOf(const Project& p, const HmiType& t) { return owned(t) ? symbolView(p, t.symbol) : nullptr; }

const HmiType* find(const Project& p, Id symbol, std::string_view name) {
    if (symbol == kNoId || name.empty()) return nullptr;
    for (const auto& t : p.programs.types)
        if (t.symbol == symbol && same(t.name, name)) return &t;
    return nullptr;
}

std::string scopeOf(const Project& p, const View& v) {
    if (!isSymbolView(v) || !hasTypes(p, v.id)) return {};
    return v.name;
}

bool symbolScope(const Project& p, std::string_view ns) {
    if (ns.empty()) return false;
    const View* v = p.viewByName(ns);
    if (!v || !isSymbolView(*v)) return false;
    bool any = false;
    for (const auto& t : p.programs.types) {
        if (!same(t.ns, ns)) continue;
        if (t.symbol != v->id) return false;
        any = true;
    }
    for (const auto& f : p.programs.functions) if (same(f.ns, ns)) return false;
    for (const auto& s : p.programs.scripts) if (same(s.ns, ns)) return false;
    for (const auto& l : p.libraries) if (same(l.name, ns)) return false;
    return any;
}

bool projectSpacesUsed(const Project& p) {
    if (!p.libraries.empty()) return true;
    for (const auto& t : p.programs.types) if (!t.ns.empty() && !owned(t)) return true;
    for (const auto& f : p.programs.functions) if (!f.ns.empty()) return true;
    for (const auto& s : p.programs.scripts) if (!s.ns.empty()) return true;
    const auto it = p.listFolders.find("espaces");
    return it != p.listFolders.end() && !it->second.empty();
}

// ---- les noms -----------------------------------------------------------------------------------

bool validName(const Project& p, Id symbol, std::string_view raw, Id self, std::string* why) {
    const std::string name = trimmed(raw);
    if (!isIdentifier(name)) return fail(why, "nom invalide : lettres, chiffres et _");
    if (types::isElementary(name)) return fail(why, name + " est un type \xC3\xA9l\xC3\xA9mentaire");
    if (const HmiType* o = find(p, symbol, name); o && o->id != self)
        return fail(why, "le symbole a d\xC3\xA9j\xC3\xA0 un type " + o->name);
    return true;
}

std::string uniqueName(const Project& p, Id symbol, std::string_view wanted) {
    std::string base = trimmed(wanted);
    if (base.empty() || !isIdentifier(base)) base = "T_Type";
    if (!find(p, symbol, base) && !types::isElementary(base)) return base;
    for (int i = 2; i < 100000; ++i)
        if (std::string n = base + "_" + std::to_string(i); !find(p, symbol, n)) return n;
    return base;
}

std::string qualifiedOf(const Project& p, const HmiType& t) {
    if (const View* v = ownerOf(p, t)) return qualifiedName(v->name, t.name);
    return fullName(t);
}

// ---- l'edition ----------------------------------------------------------------------------------

Id addType(Project& p, Id symbol, std::string_view raw, HmiTypeKind kind, std::string* why) {
    const View* sym = symbolView(p, symbol);
    if (!sym) {
        (void)fail(why, "symbole introuvable");
        return kNoId;
    }
    const std::string name = trimmed(raw);
    if (!validName(p, symbol, name, kNoId, why)) return kNoId;
    HmiType t;
    if (kind == HmiTypeKind::Enumeration) {
        t = makeEnumeration(p, name);
        t.name = name;                                      // le nom voulu (un homonyme du projet est permis)
        t.operators.clear();
        t.operators.push_back(enumToStringOperator(t));
        t.operators.push_back(enumFromStringOperator(t));
        renumberOperators(p, t.operators);
    } else {
        t.id = p.allocate();
        t.name = name;
        t.kind = kind;
        if (kind == HmiTypeKind::Structure) {
            t.members = {{"Valeur", "REAL"}, {"Marche", "BOOL"}};
        } else if (kind == HmiTypeKind::Interface) {
            HmiFunction sig;
            sig.id = p.allocate();
            sig.name = "Demarrer";
            t.methods.push_back(std::move(sig));
        } else if (kind == HmiTypeKind::Callback) {
            t.signature = "CALLBACK (x : REAL) : BOOL";
        }
    }
    t.ns = sym->name;
    t.symbol = symbol;
    const Id made = t.id;
    p.programs.types.push_back(std::move(t));
    return made;
}

bool adopt(Project& p, Id type, Id symbol) {
    const View* sym = symbolView(p, symbol);
    HmiType* t = p.hmiType(type);
    if (!sym || !t) return false;
    t->symbol = symbol;
    t->ns = sym->name;
    t->folder.clear();
    return true;
}

Id duplicateType(Project& p, Id type) {
    const HmiType* t = p.hmiType(type);
    if (!t) return kNoId;
    const HmiType copy = *t;
    std::string name = uniqueName(p, copy.symbol, copy.name + "_copie");
    HmiType x = copy;
    x.id = p.allocate();
    x.name = name;
    copyOperators(p, x.operators, copy.name, name);
    copyEnumerationLiterals(x, copy.name);
    renumberMethods(p, x.methods);
    for (auto& m : x.methods)
        for (auto& d : m.decls) d.id = p.allocate();
    const Id made = x.id;
    p.programs.types.push_back(std::move(x));
    return made;
}

std::size_t copyTypes(Project& p, Id from, Id to) {
    const View* src = p.view(from);
    const View* dst = symbolView(p, to);
    if (!src || !dst || from == to) return 0;
    const std::string fromName = src->name, toName = dst->name;
    std::vector<HmiType> copies;
    for (const auto* t : typesOf(p, from)) copies.push_back(*t);
    if (copies.empty()) return 0;
    for (auto& x : copies) {
        x.id = p.allocate();
        for (auto& op : x.operators) op.id = p.allocate();
        renumberMethods(p, x.methods);
        for (auto& m : x.methods)
            for (auto& d : m.decls) d.id = p.allocate();
        x.symbol = to;
        x.ns = fromName;                                    // renameScopeIn le passe au nouveau symbole
        p.programs.types.push_back(std::move(x));
    }
    (void)renameScopeIn(p, to, fromName, toName);
    for (auto& t : p.programs.types)
        if (t.symbol == to) t.ns = toName;
    return copies.size();
}

std::size_t removeTypes(Project& p, Id symbol) {
    if (symbol == kNoId) return 0;
    auto& all = p.programs.types;
    const auto before = all.size();
    all.erase(std::remove_if(all.begin(), all.end(), [&](const HmiType& t) { return t.symbol == symbol; }), all.end());
    return before - all.size();
}

std::size_t renameScope(Project& p, std::string_view from, std::string_view to) {
    if (from.empty() || to.empty() || same(from, to)) return 0;
    const View* v = p.viewByName(to);
    if (!v || !hasTypes(p, v->id)) return 0;
    // L'espace de ses types est encore l'ancien nom : normalize le fait suivre (et chaque texte qui les nomme en entier).
    for (auto& t : p.programs.types)
        if (t.symbol == v->id) t.ns = std::string(from);
    return normalize(p);
}

std::size_t renameScopeIn(Project& p, Id symbol, std::string_view from, std::string_view to) {
    if (from.empty() || to.empty() || same(from, to)) return 0;
    Project sub = subProject(p, symbol);
    for (auto& t : sub.programs.types) t.symbol = kNoId;
    std::size_t n = 0;
    // Les textes de type et le code du symbole et de ses types : from.T -> to.T.
    const auto type = [&](std::string& s) {
        std::string next = renameFull(s, from, to, false);
        if (next != s) { s = std::move(next); ++n; }
    };
    for (auto& t : sub.programs.types) {
        if (same(t.ns, from)) { t.ns = std::string(to); ++n; }
        type(t.base);
        for (auto& i : t.interfaces) type(i);
        for (auto& m : t.members) type(m.type);
        for (auto& m : t.methods) type(m.returnType);
        for (auto& o : t.operators) { type(o.left); type(o.right); type(o.result); }
        type(t.signature);
    }
    for (auto& v : sub.views) {
        for (auto& prm : v.params) type(prm.type);
        for (auto& f : v.functions) type(f.returnType);
        for (auto& o : v.operators) { type(o.left); type(o.right); type(o.result); }
    }
    forEachDeclarations(sub, [&](std::vector<Declaration>& list) { for (auto& d : list) type(d.type); });
    forEachCode(sub, [&](std::string& text, CodeForm, const CodeSite&) {
        if (text.empty()) return;
        std::string next = renameFull(text, from, to, true);
        if (next != text) { text = std::move(next); ++n; }
    });
    putBack(p, sub, symbol);
    return n;
}

std::size_t renameType(Project& p, Id type, std::string_view raw) {
    const HmiType* t = p.hmiType(type);
    const std::string name = trimmed(raw);
    if (!t || !owned(*t) || name.empty() || t->name == name) return 0;
    const View* sym = symbolView(p, t->symbol);
    if (!sym) return 0;
    const Id symbol = sym->id;
    const std::string fromFull = qualifiedName(sym->name, t->name), toFull = qualifiedName(sym->name, name);
    std::size_t n = 0;
    // 1) Partout : le nom complet (MonSymbole.T_Etat, MonSymbole.T_Etat#x) - le seul qui le vise hors du symbole.
    const auto typeText = [&](std::string& s) {
        std::string next = renameFull(s, fromFull, toFull, false);
        if (next != s) { s = std::move(next); ++n; }
    };
    for (auto& x : p.programs.types) {
        typeText(x.base);
        for (auto& i : x.interfaces) typeText(i);
        for (auto& m : x.members) typeText(m.type);
        for (auto& m : x.methods) typeText(m.returnType);
        for (auto& o : x.operators) { typeText(o.left); typeText(o.right); typeText(o.result); }
    }
    for (auto& v : p.programs.variables) typeText(v.type);
    for (auto& f : p.programs.functions) typeText(f.returnType);
    for (auto& v : p.views) {
        for (auto& prm : v.params) typeText(prm.type);
        for (auto& f : v.functions) typeText(f.returnType);
        for (auto& o : v.operators) { typeText(o.left); typeText(o.right); typeText(o.result); }
    }
    forEachDeclarations(p, [&](std::vector<Declaration>& list) { for (auto& d : list) typeText(d.type); });
    forEachCode(p, [&](std::string& text, CodeForm, const CodeSite&) {
        if (text.empty()) return;
        std::string next = renameFull(text, fromFull, toFull, true);
        if (next != text) { text = std::move(next); ++n; }
    });
    // 2) Dans le symbole et ses types : le renommage du projet (court et complet), sur un petit projet ou ils sont seuls.
    Project sub = subProject(p, symbol);
    for (auto& x : sub.programs.types) x.symbol = kNoId;
    n += renameTypeEverywhere(sub, type, name);
    putBack(p, sub, symbol);
    return n;
}

std::size_t normalize(Project& p, bool dropOrphans) {
    std::size_t n = 0;
    bool any = false;
    auto& all = p.programs.types;
    for (auto it = all.begin(); it != all.end();) {
        if (!owned(*it)) { ++it; continue; }
        const View* v = symbolView(p, it->symbol);
        if (!v) {
            ++n;
            if (dropOrphans) { it = all.erase(it); continue; }
            it->symbol = kNoId;                               // un type de l'espace, rien ne se perd
            ++it;
            continue;
        }
        any = true;
        if (!it->folder.empty()) { it->folder.clear(); ++n; }   // la liste du symbole n'a pas de dossiers
        ++it;
    }
    if (!any) return n;
    // UN SYMBOLE RENOMME (par l'editeur, Renommer, l'Assistant...) : l'espace de ses types le suit, et chaque texte qui les
    // nommait en entier (Ancien.T_Etat). Dans tout le projet quand l'ancien nom n'est plus a personne ; sinon (un
    // symbole importe sous un autre nom, rendu par la corbeille a cote de son homonyme) dans le symbole seul.
    std::map<Id, std::string> moved;
    for (const auto& t : all)
        if (owned(t))
            if (const View* v = symbolView(p, t.symbol); v && t.ns != v->name && !moved.count(t.symbol)) moved[t.symbol] = t.ns;
    for (const auto& [id, was] : moved) {
        const View* v = symbolView(p, id);
        if (!v) continue;
        const std::string now = v->name;
        const bool freeName = !was.empty() && !same(was, now) && !p.viewByName(was)
                           && std::none_of(all.begin(), all.end(), [&](const HmiType& t) { return t.symbol != id && same(t.ns, was); })
                           && std::none_of(p.programs.functions.begin(), p.programs.functions.end(), [&](const HmiFunction& f) { return same(f.ns, was); })
                           && std::none_of(p.programs.scripts.begin(), p.programs.scripts.end(), [&](const Script& sc) { return same(sc.ns, was); });
        if (freeName) {
            for (auto& t : all)
                if (t.symbol == id) t.ns = was;
            n += names::renameNamespace(p, was, now);
        } else if (!was.empty() && !same(was, now)) {
            n += renameScopeIn(p, id, was, now);
        }
        for (auto& t : all)
            if (t.symbol == id && t.ns != now) { t.ns = now; ++n; }
    }
    std::set<Id> symbols;
    for (const auto& t : all) if (owned(t)) symbols.insert(t.symbol);
    for (const Id id : symbols) {
        View* v = p.view(id);
        if (!v) continue;
        View sym;                                             // son identite seule, lue pendant qu'on ecrit dans *v
        sym.id = v->id;
        sym.name = v->name;
        sym.role = v->role;
        n += fixView(p, sym, *v);
    }
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (!owned(all[i])) continue;
        const View* v = p.view(all[i].symbol);
        if (!v) continue;
        HmiType t = all[i];
        if (const std::size_t k = fixOwned(p, *v, t)) {
            n += k;
            all[i] = std::move(t);
        }
    }
    return n;
}

std::size_t normalizeView(const Project& p, View& v) {
    if (!isSymbolView(v) || !hasTypes(p, v.id)) return 0;
    View sym;
    sym.id = v.id;
    sym.role = v.role;
    sym.name = v.name;
    if (const View* cur = p.view(v.id)) sym.name = cur->name;   // l'espace de ses types : le nom qu'ils portent
    return fixView(p, sym, v);
}

// ---- le moteur et les controles -----------------------------------------------------------------

std::string qualify(const Project& p, const View& symbol, std::string_view code, bool function, const std::vector<std::string>& hidden) {
    if (code.empty()) return std::string(code);
    const std::string scope = scopeOf(p, symbol);
    if (scope.empty()) return std::string(code);
    const std::string named = names::qualify(p, code, scope, hidden, function);
    // Les litteraux d'enumeration (T_Mode#Marche) : names::qualify laisse un litteral type tel quel ; ceux du symbole
    // prennent son nom complet (S_Pompe.T_Mode#Marche) - la portee la plus proche, comme les autres noms.
    if (named.find('#') == std::string::npos) return named;
    std::string out;
    out.reserve(named.size() + 16);
    char quote = 0;
    bool line = false, block = false;
    for (std::size_t i = 0; i < named.size();) {
        const char c = named[i];
        if (line) { out += c; ++i; if (c == '\n') line = false; continue; }
        if (block) { out += c; ++i; if (c == ')' && out.size() >= 2 && out[out.size() - 2] == '*') block = false; continue; }
        if (quote) { out += c; ++i; if (c == quote) quote = 0; continue; }
        if (c == '\'' || c == '"') { quote = c; out += c; ++i; continue; }
        if (c == '/' && i + 1 < named.size() && named[i + 1] == '/') { line = true; out += c; ++i; continue; }
        if (c == '(' && i + 1 < named.size() && named[i + 1] == '*') { block = true; out += "(*"; i += 2; continue; }
        if (!identStart(c) || (i > 0 && (identPart(named[i - 1]) || named[i - 1] == '.' || named[i - 1] == '#'))) {
            out += c;
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < named.size() && identPart(named[j])) ++j;
        const std::string_view word = std::string_view(named).substr(i, j - i);
        const HmiType* own = j + 1 < named.size() && named[j] == '#' && identStart(named[j + 1]) ? find(p, symbol.id, word) : nullptr;
        out += own && own->kind == HmiTypeKind::Enumeration ? qualifiedName(scope, own->name) : std::string(word);
        i = j;
    }
    return out;
}

std::string typeIn(const Project& p, const View& symbol, std::string_view type) {
    if (type.empty() || !hasTypes(p, symbol.id)) return std::string(type);
    return rewriteWords(type, [&](std::string_view w) {
        const HmiType* own = find(p, symbol.id, w);
        return own ? qualifiedName(symbol.name, own->name) : std::string{};
    });
}

std::string shortIn(const Project& p, const View& symbol, std::string_view type) {
    if (type.empty() || !hasTypes(p, symbol.id)) return std::string(type);
    std::string out;
    const std::string head = symbol.name + ".";
    for (std::size_t i = 0; i < type.size();) {
        const bool start = i == 0 || !(identPart(type[i - 1]) || type[i - 1] == '.');
        if (start && type.size() - i > head.size() && same(type.substr(i, head.size()), head)) {
            std::size_t j = i + head.size();
            while (j < type.size() && identPart(type[j])) ++j;
            const std::string_view word = type.substr(i + head.size(), j - i - head.size());
            if (const HmiType* own = find(p, symbol.id, word); own && (j >= type.size() || type[j] != '.')) {
                out += own->name;
                i = j;
                continue;
            }
        }
        out += type[i++];
    }
    return out;
}

std::vector<std::string> shortNames(const Project& p, Id symbol) {
    std::vector<std::string> out;
    for (const auto* t : typesOf(p, symbol)) out.push_back(t->name);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        std::string x = a, y = b;
        for (auto& c : x) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        for (auto& c : y) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return x < y;
    });
    return out;
}

} // namespace hmi::symtypes
