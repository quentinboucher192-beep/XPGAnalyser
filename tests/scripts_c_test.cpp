// =============================================================================
//  tests/scripts_c_test.cpp - 1.50.0 : les scripts C et C++ compiles et executes
// -----------------------------------------------------------------------------
//  SCRC_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("SCRC_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

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

std::string run(Project& p, ScriptLang lang, const std::string& body, std::string* err = nullptr, const std::string& readVar = "R") {
    Script s;
    s.id = p.allocate();
    s.name = "Essai";
    s.lang = lang;
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
    struct T { const char* type; const char* body; };
    const T cases[] = {
        {"BOOL", "R := 1;"},
        {"BOOL", "VAR_TEMP b : BOOL; END_VAR b := 1; R := b;"},
        {"DINT", "R := TO_DINT(2.7);"},
        {"DINT", "R := TO_DINT(-2.7);"},
        {"DINT", "R := TRUNC(-2.7);"},
        {"DINT", "R := 7 / 2;"},
        {"REAL", "R := 7 / 2;"},
        {"REAL", "VAR_TEMP x : REAL; END_VAR x := 7; R := x / 2;"},
        {"DINT", "FUNCTION F(VAR_IN_OUT a : DINT) a := a + 5; END_FUNCTION VAR_TEMP z : DINT; END_VAR z := 1; F(z); R := z;"},
        {"DINT", "FUNCTION G() R := 42; END_FUNCTION G();"},
        {"DINT", "FUNCTION H() : DINT RETURN 41; END_FUNCTION R := H() + 1;"},
        {"DINT", "VAR_TEMP t : ARRAY[0..2] OF DINT; s : DINT; END_VAR t[0] := 1; t[1] := 2; t[2] := 3; FOR EACH v IN t DO s := s + v; END_FOR; R := s;"},
        {"DINT", "R := TRUE ? 3 : 4;"},
        {"DINT", "VAR g : DINT := 10; END_VAR FUNCTION K() : DINT RETURN g + 1; END_FUNCTION R := K();"},
        {"BOOL", "R := TO_BOOL(5);"},
        {"BOOL", "IF 5 THEN R := TRUE; END_IF;"},
        {"DINT", "VAR_TEMP i : DINT; END_VAR WHILE i < 10 DO i := i + 1; IF i < 5 THEN CONTINUE; END_IF; R := R + 1; END_WHILE;"},
        {"STRING", "R := 'a' + TO_STRING(5) + 'b';"},
        {"DINT", "R := 16#FF + 8#17 + 2#101;"},
        {"REAL", "R := 1.0E3;"},
        {"DINT", "IHM_JOURNAL('x {R}'); R := 3;"},
        {"DINT", "VAR_TEMP i : DINT; END_VAR i := 3; i += 2; i -= 1; i *= 3; i /= 2; R := i;"},
        {"DINT", "R := SHL(1, 4) + SHR(64, 2);"},
        {"DINT", "R := 17 MOD 5;"},
        {"DINT", "R := 5 AND 3;"},
        {"DINT", "R := NOT 0;"},
        {"DINT", "VAR_TEMP m : ARRAY[0..2] OF ARRAY[0..3] OF DINT; END_VAR m[1][2] := 7; R := m[1][2];"},
        {"DINT", "VAR_TEMP m : ARRAY[0..2, 0..3] OF DINT; END_VAR m[1, 2] := 7; R := m[1][2];"},
        {"DINT", "VAR_TEMP m : ARRAY[0..2] OF ARRAY[0..3] OF DINT; END_VAR m[1, 2] := 7; R := m[1][2];"},
        {"DINT", "FUNCTION F(x : DINT) : DINT VAR k : DINT; END_VAR k := x * 2; RETURN k; END_FUNCTION R := F(4);"},
        {"LREAL", "R := SQRT(16.0) + ABS(-2) + EXPT(2.0, 3);"},
        {"DINT", "VAR_TEMP time_c : DINT; END_VAR time_c := 4; R := time_c;"},
        {"DINT", "VAR_TEMP x : SINT; END_VAR x := 65; R := x;"},
        {"DINT", "R := MIN(3, 2) + MAX(1, 9);"},
    };
    for (const auto& c : cases) {
        Project p;
        addVar(p, "R", c.type, "");
        std::string err;
        const std::string got = run(p, ScriptLang::ST, c.body, &err);
        std::printf("%-6s %-110s -> %s %s\n", c.type, c.body, got.c_str(), err.c_str());
    }
}

} // namespace

int main() {
    std::printf("scripts C et C++ : compiles et executes\n");
    if (std::getenv("SCRC_SONDE")) sonde();
    std::printf("%d controles, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
