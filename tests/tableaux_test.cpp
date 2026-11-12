// =============================================================================
//  tests/tableaux_test.cpp - 1.50.0 : les tableaux de tableaux et les tableaux
//  a plusieurs dimensions, partout ou un type s'ecrit ou s'emploie
// -----------------------------------------------------------------------------
//  ARRAY[0..3] OF ARRAY[0..15] OF BOOL (un tableau de tableaux) et
//  ARRAY[0..3, 0..15] OF BOOL (deux dimensions) : les deux formes, acceptees
//  par une seule lecture (hmi::types::parseSpec, le registre des types, le
//  moteur), et lues a[i][j] comme a[i, j].
//
//  TAB_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiDecl.hpp"
#include "../src/hmi/HmiExpr.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiScriptCheck.hpp"
#include "../src/hmi/HmiTypeForms.hpp"
#include "../src/hmi/HmiTypeRegistry.hpp"
#include "../src/hmi/HmiTypes.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("TAB_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

const char* const kNested = "ARRAY[0..3] OF ARRAY[0..15] OF BOOL";
const char* const kTwoD = "ARRAY[0..3, 0..15] OF BOOL";

std::string shown(const sim::Value& v) { return v.type() == sim::Type::String ? v.asString() : v.display(); }

Variable& addVar(Project& p, const std::string& n, const std::string& t, const std::string& init = "") {
    Variable v;
    v.id = p.allocate();
    v.name = n;
    v.type = t;
    v.initial = init;
    p.programs.variables.push_back(v);
    return p.programs.variables.back();
}

std::string errorsOf(const std::vector<ScriptDiagnostic>& d) {
    std::string out;
    for (const auto& x : d)
        if (x.severity == ScriptDiagnostic::Severity::Error) out += (out.empty() ? "" : " | ") + std::to_string(x.line) + ": " + x.message;
    return out;
}

// Un script "Essai" appele une fois ; ce qu'il laisse dans `readVar` (ou "#"), et son erreur.
std::string run(Project& p, const std::string& body, std::string* err = nullptr, const std::string& readVar = "R") {
    Script s;
    s.id = p.allocate();
    s.name = "Essai";
    s.event = "Appel";
    s.body = body;
    p.programs.scripts.push_back(s);
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.start(0.0);
    rt.tick(0.1);
    std::string e;
    const bool ok = rt.callScript("Essai", 0.2, &e);
    if (err) *err = ok ? std::string{} : e;
    sim::Value out;
    return rt.environment().read(readVar, out) ? shown(out) : std::string("#");
}

void sonde() {
    std::printf("-- sonde\n");
    for (const char* t : {kNested, kTwoD}) {
        std::string why;
        types::Spec s;
        const bool ps = types::parseSpec(t, s, &why);
        std::printf("parseSpec(%s) = %d dims=%d element=%s why=%s\n", t, ps, s.dims, s.element.c_str(), why.c_str());
        Project p;
        why.clear();
        std::printf("  validType=%d (%s)\n", types::validType(p, t, &why), why.c_str());
        why.clear();
        std::printf("  validMemberType=%d (%s)\n", types::validMemberType(p, t, &why), why.c_str());
        const auto f = types::flatten(p, "X", t);
        std::printf("  flatten ok=%d leaves=%zu first=%s last=%s err=%s\n", f.ok(), f.leaves.size(),
                    f.leaves.empty() ? "" : f.leaves.front().path.c_str(), f.leaves.empty() ? "" : f.leaves.back().path.c_str(), f.error.c_str());
        const auto reg = typereg::Registry::build(p);
        for (unsigned u : {typereg::UseVariable, typereg::UseDeclaration, typereg::UseParameter, typereg::UseReturn}) {
            const auto r = reg->resolve(t, u);
            std::printf("  resolve(use %u) ok=%d text=%s why=%s\n", u, r.ok, r.text.c_str(), r.why.c_str());
        }
        const auto sh = typeform::decompose(t);
        std::printf("  decompose form=%d element=%s param=%s compose=%s\n", static_cast<int>(sh.form), sh.element.c_str(), sh.parameter.c_str(),
                    typeform::compose(sh).c_str());
        std::printf("  summary=%s words=%lld normalized=%s\n", types::summary(p, t).c_str(), types::weightOf(p, t).words, types::normalized(t).c_str());
        addVar(p, "X", t);
        addVar(p, "R", "BOOL", "FALSE");
        std::printf("  typeOfPath X[1]=%s X[1][2]=%s X[1,2]=%s\n", types::typeOfPath(p, "X[1]").c_str(), types::typeOfPath(p, "X[1][2]").c_str(),
                    types::typeOfPath(p, "X[1,2]").c_str());
        for (const char* code : {"X[1][2] := TRUE;", "X[1, 2] := TRUE;", "R := X[1][2];", "R := X[1, 2];"}) {
            std::string pp;
            for (const auto& q : types::pathProblems(p, code)) pp += q.message + " ; ";
            scriptcheck::Scope sc;
            sc.project = &p;
            std::string fs;
            for (const auto& q : scriptcheck::check(sc, code)) fs += q.message + " ; ";
            Project rp = p;
            std::string err;
            const std::string got = run(rp, std::string(code) + (std::string(code).rfind("R :=", 0) == 0 ? "" : "\nR := X[1][2];"), &err);
            std::printf("  code %-18s pathProblems=[%s] check=[%s] run R=%s err=%s\n", code, pp.c_str(), fs.c_str(), got.c_str(), err.c_str());
        }
        // Les locales
        for (const char* code : {"a[1][2] := TRUE; R := a[1][2];", "a[1, 2] := TRUE; R := a[1, 2];"}) {
            const std::string body = std::string("VAR\n  a : ") + t + ";\nEND_VAR\n" + code;
            const TypeKnown known = [&p](std::string_view x) { return types::knownTypeName(p, x); };
            scriptcheck::Scope sc;
            sc.project = &p;
            std::string fs;
            for (const auto& q : scriptcheck::check(sc, body)) fs += q.message + " ; ";
            Project rp = p;
            std::string err;
            const std::string got = run(rp, body, &err);
            std::printf("  locale %-32s checkScript=[%s] check=[%s] run R=%s err=%s\n", code, errorsOf(checkScript(ScriptLang::ST, body, "S", known)).c_str(),
                        fs.c_str(), got.c_str(), err.c_str());
        }
        // fx
        {
            Project rp = p;
            Script s;
            s.id = rp.allocate();
            s.name = "Essai";
            s.event = "Appel";
            s.body = "X[1][2] := TRUE;";
            rp.programs.scripts.push_back(s);
            View v = makeView(rp, "Vue_Essai");
            rp.views.push_back(v);
            rp.config.startView = rp.views.back().id;
            Runtime rt;
            rt.bind(&rp, nullptr);
            rt.start(0.0);
            rt.tick(0.1);
            std::string e;
            rt.callScript("Essai", 0.2, &e);
            for (const char* fx : {"X[1][2]", "X[1, 2]", "NOT X[1][2]"}) {
                const auto ex = Expression::compile(fx);
                const auto r = ex.evaluate(rt.environment());
                std::printf("  fx %-12s valid=%d -> %s\n", fx, ex.valid(), r ? shown(*r).c_str() : r.error().message().c_str());
            }
        }
    }
}

} // namespace

int main() {
    std::printf("tableaux : ARRAY OF ARRAY et ARRAY[a, b], partout\n");
    if (std::getenv("TAB_SONDE")) sonde();
    std::printf("%d controles, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
