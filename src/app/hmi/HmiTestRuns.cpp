// Les derniers verdicts des tests unitaires (1.13.0, lot J4b) : voir HmiTestRuns.hpp.
#include "HmiTestRuns.hpp"

#include <functional>
#include <map>

namespace app {

namespace {
using hmi::Id;
using hmi::Verdict;
std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }
} // namespace

namespace testruns {
namespace {
struct Entry {
    hmi::FunctionTestResult result;
    std::size_t             print{0};
};
std::map<Id, Entry>& entries() {
    static std::map<Id, Entry> e;
    return e;
}
std::uint64_t& rev() {
    static std::uint64_t r = 0;
    return r;
}
void bump() {
    ++rev();
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

void record(const hmi::HmiFunction& f, const hmi::FunctionTestResult& r) {
    for (const auto& t : f.tests)
        if (t.id == r.test) {
            entries()[t.id] = Entry{r, fingerprint(f, t)};
            bump();
            return;
        }
}

void record(const hmi::HmiFunction& f, const hmi::FunctionTestReport& rep) {
    for (const auto& r : rep.results)
        for (const auto& t : f.tests)
            if (t.id == r.test) entries()[t.id] = Entry{r, fingerprint(f, t)};
    bump();
}

const hmi::FunctionTestResult* last(const hmi::HmiFunction& f, const hmi::FunctionTest& t) {
    const auto it = entries().find(t.id);
    if (it == entries().end() || it->second.print != fingerprint(f, t)) return nullptr;
    return &it->second.result;
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

Tally tally(const hmi::HmiFunction& f) {
    Tally t;
    for (const auto& c : f.tests) {
        ++t.cases;
        const auto* r = last(f, c);
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
    for (const auto& f : p.programs.functions) t += tally(f);
    for (const auto& v : p.views)
        for (const auto& f : v.functions) t += tally(f);
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
} // namespace testruns

} // namespace app
