// Les derniers verdicts des tests unitaires (1.13.0, lot J4b) : voir HmiTestRuns.hpp.
#include "HmiTestRuns.hpp"

#include "../../core/Command.hpp"   // 1.13.0 (D3b) : la pile de l'application - le projet a-t-il change ?

#include <cctype>
#include <deque>
#include <functional>
#include <map>
#include <set>

namespace app {

namespace {
using hmi::Id;
using hmi::Verdict;
std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// 1.13.0 (seconde passe) : les noms qu'un code cite (en majuscules) - hors chaines et commentaires. Un chemin
// Ligne.Pression donne LIGNE et PRESSION : il suffit que la racine y soit.
void namesIn(std::string_view code, std::set<std::string>& out) {
    const std::size_t n = code.size();
    std::size_t i = 0;
    const auto identStart = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    const auto identChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    while (i < n) {
        const char c = code[i];
        if (c == '\'' || c == '"') {                                   // une chaine
            const char q = c;
            ++i;
            while (i < n && code[i] != q) i += code[i] == '$' ? 2 : 1;
            ++i;
        } else if (c == '(' && i + 1 < n && code[i + 1] == '*') {      // (* ... *)
            const auto end = code.find("*)", i + 2);
            i = end == std::string_view::npos ? n : end + 2;
        } else if (c == '/' && i + 1 < n && code[i + 1] == '*') {      // /* ... */
            const auto end = code.find("*/", i + 2);
            i = end == std::string_view::npos ? n : end + 2;
        } else if (c == '/' && i + 1 < n && code[i + 1] == '/') {      // // ...
            const auto end = code.find('\n', i);
            i = end == std::string_view::npos ? n : end;
        } else if (identStart(c)) {
            const std::size_t s = i;
            while (i < n && identChar(code[i])) ++i;
            out.insert(upperOf(code.substr(s, i - s)));
        } else {
            ++i;
        }
    }
}

void addDecls(std::string& s, const std::vector<hmi::Declaration>& decls) {
    for (const auto& d : decls)
        s += d.name + '\x1e' + d.type + '\x1e' + d.value + '\x1e' + std::to_string(static_cast<int>(d.kind)) + std::to_string(static_cast<int>(d.mode))
           + '\x1e' + d.unit + '\x1e' + d.calc + '\x1f';
}
} // namespace

namespace testruns {
namespace {
struct Entry {
    hmi::FunctionTestResult result;
    std::size_t             print{0};
    std::size_t             context{0};
    const void*             owner{nullptr};     // le projet ou le cas a ete joue
};
std::map<Id, Entry>& entries() {
    static std::map<Id, Entry> e;
    return e;
}
std::uint64_t& rev() {
    static std::uint64_t r = 0;
    return r;
}
int& batchDepth() {
    static int d = 0;
    return d;
}
bool& batchPending() {
    static bool p = false;
    return p;
}
void bump() {
    ++rev();
    if (batchDepth() > 0) {
        batchPending() = true;          // une campagne : dit une fois, a la fin
        return;
    }
    changed()->emit();
}
} // namespace

std::size_t fingerprint(const hmi::HmiFunction& f, const hmi::FunctionTest& t) {
    std::string s = f.name + '\x1f' + f.returnType + '\x1f' + f.returnUnit + '\x1f' + f.body + '\x1f';
    for (const auto& d : f.decls)
        s += d.name + '\x1e' + d.type + '\x1e' + d.value + '\x1e' + std::to_string(static_cast<int>(d.kind)) + std::to_string(static_cast<int>(d.mode))
           + '\x1e' + d.unit + '\x1f';
    for (const auto& g : f.generics) s += g.name + ':' + g.constraint + '\x1f';
    s += '\x1d' + t.name + '\x1f' + t.expected + '\x1f' + t.tolerance + '\x1f' + t.errorCode + '\x1f' + (t.enabled ? "1" : "0") + '\x1f';
    for (const auto& a : t.arguments) s += a + '\x1e';
    for (const auto& g : t.givens) s += g.target + '=' + g.value + '\x1e';
    for (const auto& c : t.checks) s += c.target + '=' + c.expected + '~' + c.tolerance + '\x1e';
    return std::hash<std::string>{}(s);
}

// 1.13.0 (seconde passe, D3b) : CE QUE LA FONCTION LIT, de proche en proche, par les noms que citent son code, ses
// declarations (valeurs et types) et ses cas : les fonctions IHM de ce nom (du projet, de toute vue, popup ou symbole -
// une instance appelle celles de son symbole -, les methodes des types, les fonctions des bibliotheques reliees), les
// redefinitions d'une instance, les variables IHM, les types IHM (membres, valeurs, base, operateurs ; ceux des
// bibliotheques aussi), les constantes de sa vue et d'une vue nommee. Un nom pris pour un autre (deux fonctions
// Calculer) compte les deux : au pire, un cas repart " a jouer " pour rien.
namespace {
struct ContextIndex {
    std::multimap<std::string, const hmi::HmiFunction*>      functions;
    std::multimap<std::string, const hmi::FunctionOverride*> overrides;
    std::multimap<std::string, const hmi::Variable*>         variables;
    std::multimap<std::string, const hmi::HmiType*>          types;
    std::multimap<std::string, const hmi::View*>             views;
    std::map<Id, const hmi::View*>                           ownerOf;     // une fonction de vue -> sa vue
};

ContextIndex contextIndexOf(const hmi::Project& p) {
    ContextIndex ix;
    const auto addTypes = [&ix](const std::vector<hmi::HmiType>& types) {
        for (const auto& ty : types) {
            ix.types.emplace(upperOf(ty.name), &ty);
            for (const auto& m : ty.methods) ix.functions.emplace(upperOf(m.name), &m);
        }
    };
    for (const auto& g : p.programs.functions) ix.functions.emplace(upperOf(g.name), &g);
    for (const auto& v : p.programs.variables) ix.variables.emplace(upperOf(v.name), &v);
    addTypes(p.programs.types);
    for (const auto& v : p.views) {
        ix.views.emplace(upperOf(v.name), &v);
        for (const auto& g : v.functions) {
            ix.functions.emplace(upperOf(g.name), &g);
            ix.ownerOf.emplace(g.id, &v);
        }
        for (const auto& o : v.objects)
            for (const auto& r : o.functionOverrides) ix.overrides.emplace(upperOf(r.function), &r);
    }
    if (const auto& libs = p.libraryCache.loaded)
        for (const auto& lib : *libs) {
            for (const auto& g : lib.content.functions) ix.functions.emplace(upperOf(g.name), &g);
            addTypes(lib.content.types);
        }
    return ix;
}

std::size_t& computed() {
    static std::size_t n = 0;
    return n;
}

std::size_t contextWith(const ContextIndex& ix, const hmi::HmiFunction& f) {
    ++computed();
    std::set<std::string> names;
    std::deque<std::string> pending;
    const auto cite = [&](std::string_view code) {
        std::set<std::string> found;
        namesIn(code, found);
        for (auto& n : found)
            if (names.insert(n).second) pending.push_back(n);
    };
    const auto citeDecls = [&](const std::vector<hmi::Declaration>& decls) {
        for (const auto& d : decls) {
            cite(d.value);
            cite(d.type);
            cite(d.calc);
        }
    };
    const auto scan = [&](const hmi::HmiFunction& g) {
        cite(g.body);
        cite(g.returnType);
        citeDecls(g.decls);
    };
    scan(f);
    for (const auto& t : f.tests) {
        for (const auto& a : t.arguments) cite(a);
        cite(t.expected);
        for (const auto& g : t.givens) { cite(g.target); cite(g.value); }
        for (const auto& c : t.checks) { cite(c.target); cite(c.expected); }
    }
    std::string s;
    std::set<const void*> seen;
    std::set<Id> seenFunctions{f.id};        // par identifiant : la fonction elle-meme (ou sa copie) n'en est pas
    const auto first = [&seen](const void* x) { return seen.insert(x).second; };
    // Les constantes de sa vue (une fonction de vue les lit par leur nom).
    if (const auto it = ix.ownerOf.find(f.id); it != ix.ownerOf.end()) {
        s += '\x1a' + it->second->name + '\x1f';
        addDecls(s, it->second->constants);
        citeDecls(it->second->constants);
    }
    while (!pending.empty()) {
        const std::string n = pending.front();
        pending.pop_front();
        for (auto [a, b] = ix.functions.equal_range(n); a != b; ++a) {
            const hmi::HmiFunction& g = *a->second;
            if (!seenFunctions.insert(g.id).second) continue;
            s += '\x1d' + g.ns + '.' + g.name + '\x1f' + g.returnType + '\x1f' + g.returnUnit + '\x1f' + g.body + '\x1f';
            addDecls(s, g.decls);
            for (const auto& gp : g.generics) s += gp.name + ':' + gp.constraint + '\x1f';
            scan(g);
        }
        for (auto [a, b] = ix.overrides.equal_range(n); a != b; ++a) {
            const hmi::FunctionOverride& r = *a->second;
            if (!first(&r)) continue;
            s += '\x19' + r.function + '\x1f' + r.body + '\x1f';
            addDecls(s, r.decls);
            cite(r.body);
            citeDecls(r.decls);
        }
        for (auto [a, b] = ix.variables.equal_range(n); a != b; ++a) {
            const hmi::Variable& v = *a->second;
            if (!first(&v)) continue;
            s += '\x1c' + v.name + '\x1e' + v.type + '\x1e' + v.initial + '\x1e' + v.unit + '\x1f';
            cite(v.type);
            cite(v.initial);
        }
        for (auto [a, b] = ix.types.equal_range(n); a != b; ++a) {
            const hmi::HmiType& ty = *a->second;
            if (!first(&ty)) continue;
            s += '\x1b' + ty.ns + '.' + ty.name + '\x1e' + ty.base + '\x1e' + ty.signature + '\x1f';
            cite(ty.base);
            cite(ty.signature);
            for (const auto& m : ty.members) {
                s += m.name + '\x1e' + m.type + '\x1e' + m.initial + '\x1e' + m.unit + '\x1f';
                cite(m.type);
                cite(m.initial);
            }
            for (const auto& e : ty.values) s += e.name + '=' + std::to_string(e.value) + '\x1f';
            for (const auto& o : ty.operators) {          // appeles sans leur nom (a + b, TO_T_X)
                s += o.op + '\x1e' + o.left + '\x1e' + o.right + '\x1e' + o.result + '\x1e' + o.body + '\x1f';
                addDecls(s, o.decls);
                cite(o.body);
                citeDecls(o.decls);
            }
        }
        for (auto [a, b] = ix.views.equal_range(n); a != b; ++a) {      // Vue_Four.SEUIL : ses constantes
            const hmi::View& v = *a->second;
            if (!first(&v)) continue;
            s += '\x1a' + v.name + '\x1f';
            addDecls(s, v.constants);
            citeDecls(v.constants);
        }
    }
    return std::hash<std::string>{}(s);
}

// LE CACHE DES CONTEXTES : ceux de toutes les fonctions a cas du projet, refaits d'un coup quand le projet a pu changer
// - une commande faite, annulee ou refaite (la pile de l'application : CommandGroupScope), un autre projet, un oubli
// (forget), d'autres bibliotheques lues. Sans pile enregistree (un essai), rien n'est garde : chaque lecture recalcule.
struct ContextCache {
    const void*               project{nullptr};
    const core::CommandStack* stack{nullptr};
    std::uint64_t             revision{0};
    std::uint64_t             epoch{0};
    const void*               libraries{nullptr};
    bool                      valid{false};
    std::map<Id, std::size_t> byFunction;
};
ContextCache& contextCache() {
    static ContextCache c;
    return c;
}
std::uint64_t& epoch() {
    static std::uint64_t e = 0;
    return e;
}
} // namespace

std::size_t context(const hmi::Project& p, const hmi::HmiFunction& f) {
    const core::CommandStack* stack = core::CommandGroupScope::stack();
    if (!stack) return contextWith(contextIndexOf(p), f);
    auto& c = contextCache();
    const void* libs = p.libraryCache.loaded.get();
    if (!c.valid || c.project != &p || c.stack != stack || c.revision != stack->revision() || c.epoch != epoch() || c.libraries != libs) {
        c.byFunction.clear();
        const ContextIndex ix = contextIndexOf(p);
        for (const auto& g : p.programs.functions)
            if (!g.tests.empty()) c.byFunction[g.id] = contextWith(ix, g);
        for (const auto& v : p.views)
            for (const auto& g : v.functions)
                if (!g.tests.empty()) c.byFunction[g.id] = contextWith(ix, g);
        c.project = &p;
        c.stack = stack;
        c.revision = stack->revision();
        c.epoch = epoch();
        c.libraries = libs;
        c.valid = true;
    }
    if (const auto it = c.byFunction.find(f.id); it != c.byFunction.end()) return it->second;
    return contextWith(contextIndexOf(p), f);         // une fonction hors du projet (ou sans cas encore)
}

std::size_t contextsComputed() noexcept { return computed(); }

void record(const hmi::Project& p, const hmi::HmiFunction& f, const hmi::FunctionTestResult& r) {
    for (const auto& t : f.tests)
        if (t.id == r.test) {
            entries()[t.id] = Entry{r, fingerprint(f, t), context(p, f), &p};
            bump();
            return;
        }
}

void record(const hmi::Project& p, const hmi::HmiFunction& f, const hmi::FunctionTestReport& rep) {
    const std::size_t ctx = context(p, f);
    for (const auto& r : rep.results)
        for (const auto& t : f.tests)
            if (t.id == r.test) entries()[t.id] = Entry{r, fingerprint(f, t), ctx, &p};
    bump();
}

namespace {
const hmi::FunctionTestResult* lastWith(const hmi::Project& p, const hmi::HmiFunction& f, const hmi::FunctionTest& t, std::size_t ctx) {
    const auto it = entries().find(t.id);
    if (it == entries().end() || it->second.owner != &p || it->second.print != fingerprint(f, t) || it->second.context != ctx) return nullptr;
    return &it->second.result;
}
} // namespace

const hmi::FunctionTestResult* last(const hmi::Project& p, const hmi::HmiFunction& f, const hmi::FunctionTest& t) {
    return lastWith(p, f, t, context(p, f));
}

std::string Tally::text() const {
    if (cases == 0) return "aucun cas";
    std::string s = std::to_string(cases) + " cas \xC2\xB7 " + plural(passed, "r\xC3\xA9ussi", "r\xC3\xA9ussis");
    if (failed) s += ", " + plural(failed, "\xC3\xA9" "chec", "\xC3\xA9" "checs");
    if (errors) s += ", " + plural(errors, "erreur", "erreurs");
    if (skipped) s += ", " + plural(skipped, "inactif", "inactifs");
    if (pending) s += ", " + std::to_string(pending) + " \xC3\xA0 jouer";
    return s;
}

Tally& Tally::operator+=(const Tally& o) {
    cases += o.cases;
    passed += o.passed;
    failed += o.failed;
    errors += o.errors;
    pending += o.pending;
    skipped += o.skipped;
    return *this;
}

Tally tally(const hmi::Project& p, const hmi::HmiFunction& f) {
    Tally t;
    if (f.tests.empty()) return t;
    const std::size_t ctx = context(p, f);          // une fois pour tous ses cas
    for (const auto& c : f.tests) {
        ++t.cases;
        const auto* r = lastWith(p, f, c, ctx);
        const Verdict v = !c.enabled ? Verdict::Skipped : r ? r->verdict : Verdict::Pending;
        switch (v) {
            case Verdict::Passed: ++t.passed; break;
            case Verdict::Failed: ++t.failed; break;
            case Verdict::Error: ++t.errors; break;
            case Verdict::Skipped: ++t.skipped; break;
            default: ++t.pending; break;
        }
    }
    return t;
}

Tally tally(const hmi::Project& p) {
    Tally t;
    for (const auto& f : p.programs.functions) t += tally(p, f);
    for (const auto& v : p.views)
        for (const auto& f : v.functions) t += tally(p, f);
    return t;
}

void forget() {
    entries().clear();
    ++epoch();                  // les contextes gardes aussi (l'adresse d'un projet ferme peut resservir)
    bump();
}

std::uint64_t revision() noexcept { return rev(); }

const core::SignalPtr<>& changed() {
    static const core::SignalPtr<> s = core::Signal<>::create();
    return s;
}

Batch::Batch() { ++batchDepth(); }
Batch::~Batch() {
    if (--batchDepth() > 0 || !batchPending()) return;
    batchPending() = false;
    changed()->emit();
}
} // namespace testruns

} // namespace app
