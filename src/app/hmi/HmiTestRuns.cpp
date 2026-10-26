// Les derniers verdicts des tests unitaires (1.13.0, lot J4b) : voir HmiTestRuns.hpp.
#include "HmiTestRuns.hpp"

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

// 1.13.0 (seconde passe) : CE QUE LA FONCTION LIT, de proche en proche - les fonctions IHM qu'elle appelle (du projet,
// de sa vue), les variables IHM qu'elle nomme (et leurs types IHM), les constantes de sa vue. Les noms cites par ses
// cas (poses, verifs, attendus calcules) comptent aussi.
std::size_t context(const hmi::Project& p, const hmi::HmiFunction& f) {
    const hmi::View* owner = nullptr;
    for (const auto& v : p.views)
        for (const auto& g : v.functions)
            if (g.id == f.id) owner = &v;
    std::set<std::string> names;
    const auto scan = [&](const hmi::HmiFunction& g) {
        namesIn(g.body, names);
        for (const auto& d : g.decls) namesIn(d.value, names);
    };
    scan(f);
    for (const auto& t : f.tests) {
        for (const auto& a : t.arguments) namesIn(a, names);
        namesIn(t.expected, names);
        for (const auto& g : t.givens) { namesIn(g.target, names); namesIn(g.value, names); }
        for (const auto& c : t.checks) { namesIn(c.target, names); namesIn(c.expected, names); }
    }
    std::string s;
    // Les fonctions appelees, de proche en proche (chacune une fois).
    std::set<Id> seen{f.id};
    std::deque<const hmi::HmiFunction*> todo;
    const auto reach = [&] {
        bool more = false;
        const auto visit = [&](const hmi::HmiFunction& g) {
            if (seen.count(g.id) || !names.count(upperOf(g.name))) return;
            seen.insert(g.id);
            todo.push_back(&g);
            more = true;
        };
        for (const auto& g : p.programs.functions) visit(g);
        if (owner)
            for (const auto& g : owner->functions) visit(g);
        return more;
    };
    while (reach())
        while (!todo.empty()) {
            const auto* g = todo.front();
            todo.pop_front();
            scan(*g);
            s += g->name + '\x1f' + g->returnType + '\x1f' + g->returnUnit + '\x1f' + g->body + '\x1f';
            addDecls(s, g->decls);
            for (const auto& gp : g->generics) s += gp.name + ':' + gp.constraint + '\x1f';
        }
    // Les variables IHM nommees, et les types IHM de proche en proche (leurs membres, leurs valeurs).
    std::set<std::string> typeNames;
    for (const auto& v : p.programs.variables)
        if (names.count(upperOf(v.name))) {
            s += '\x1c' + v.name + '\x1e' + v.type + '\x1e' + v.initial + '\x1e' + v.unit + '\x1f';
            typeNames.insert(upperOf(v.type));
        }
    std::set<std::string> typesDone;
    for (bool more = true; more;) {
        more = false;
        for (const auto& ty : p.programs.types) {
            const std::string u = upperOf(ty.name);
            if (!typeNames.count(u) || typesDone.count(u)) continue;
            typesDone.insert(u);
            more = true;
            s += '\x1b' + ty.name + '\x1e' + ty.base + '\x1f';
            if (!ty.base.empty()) typeNames.insert(upperOf(ty.base));
            for (const auto& m : ty.members) {
                s += m.name + '\x1e' + m.type + '\x1e' + m.initial + '\x1e' + m.unit + '\x1f';
                typeNames.insert(upperOf(m.type));
            }
            for (const auto& e : ty.values) s += e.name + '=' + std::to_string(e.value) + '\x1f';
        }
    }
    // Les constantes de sa vue (une fonction de vue les lit par leur nom).
    if (owner) addDecls(s, owner->constants);
    return std::hash<std::string>{}(s);
}

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
