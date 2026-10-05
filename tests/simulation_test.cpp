// =============================================================================
//  tests/simulation_test.cpp — running the program instead of reading it
// -----------------------------------------------------------------------------
//  A simulator earns trust by being wrong in the ways the PLC is wrong. So the
//  assertions here are less about "does it compute 2+2" and more about:
//
//    * PLC arithmetic: an INT wraps at 32767, it does not promote;
//    * timers behave across scans, including PT changing mid-count;
//    * forcing overrides the program, and the program cannot fight it;
//    * bad code is reported with its line rather than crashing or hanging;
//    * anything the simulator does not support says so instead of being skipped.
// =============================================================================
#include "../src/project/EditCommands.hpp"
#include "../src/project/ProjectStore.hpp"
#include "../src/sim/Runtime.hpp"

#include <cassert>
#include <cstdio>
#include <cmath>
#include <string>
#include <tuple>

using namespace sim;
using namespace project;
using namespace domain;

namespace {

// A project with one MAST section whose body is `code`, plus whatever globals
// the caller declares.
struct Fixture {
    std::shared_ptr<Project> project;
    core::CommandStack       stack;

    Fixture() : project(ProjectStore::createEmpty("Sim", "BMXP342020")) {}

    void global(const std::string& name, const std::string& type,
                const std::string& address = {}) {
        AddVariableCommand::Spec s;
        s.name = name; s.type = type; s.address = address;
        s.scope = VariableScope::Global;
        assert(stack.push(std::make_unique<AddVariableCommand>(project, s)).has_value());
    }
    void section(const std::string& name, const std::string& code) {
        assert(stack.push(std::make_unique<AddSectionCommand>(
                   project, name, "MAST", PouLanguage::ST)).has_value());
        assert(stack.push(std::make_unique<SetSectionBodyCommand>(
                   project, static_cast<Index>(project->sections.size() - 1), code)).has_value());
    }
};

std::int64_t intOf(Runtime& rt, const char* name) {
    Value v;
    assert(rt.get(name, v));
    return v.asInteger();
}
bool boolOf(Runtime& rt, const char* name) {
    Value v;
    assert(rt.get(name, v));
    return v.isTruthy();
}

} // namespace

int main() {
    // --- 1. arithmetic is the PLC's, not C's --------------------------------
    {
        Fixture f;
        f.global("counter", "INT");
        f.global("big", "DINT");
        f.global("ratio", "REAL");
        f.section("Cycle",
                  "counter := counter + 1;\n"
                  "big := 100000 * 3;\n"
                  "ratio := 7.0 / 2.0;\n");
        Runtime rt(f.project);
        assert(rt.prepare().has_value());

        for (int i = 0; i < 5; ++i) (void)rt.step();
        assert(intOf(rt, "counter") == 5);
        assert(intOf(rt, "big") == 300000);
        Value ratio;
        assert(rt.get("ratio", ratio) && std::abs(ratio.asReal() - 3.5) < 1e-9);

        // An INT wraps at 16 bits. A simulator that quietly kept counting would
        // hide precisely the overflow worth finding.
        assert(rt.set("counter", Value::integer(Type::Int, 32767)));
        (void)rt.step();
        std::printf("INT 32767 + 1 = %lld\n", static_cast<long long>(intOf(rt, "counter")));
        assert(intOf(rt, "counter") == -32768);
    }

    // --- 2. control flow, and the value of TIME literals ---------------------
    {
        Fixture f;
        f.global("mode", "INT");
        f.global("out", "INT");
        f.global("delay", "TIME");
        f.section("Cycle",
                  "delay := T#1s500ms;\n"
                  "CASE mode OF\n"
                  "  0: out := 10;\n"
                  "  1..3: out := 20;\n"
                  "ELSE\n"
                  "  out := 99;\n"
                  "END_CASE;\n");
        Runtime rt(f.project);
        assert(rt.prepare().has_value());

        (void)rt.step();
        assert(intOf(rt, "out") == 10);
        assert(intOf(rt, "delay") == 1500 && "T#1s500ms is 1500 ms");

        assert(rt.set("mode", Value::integer(Type::Int, 2)));
        (void)rt.step();
        assert(intOf(rt, "out") == 20 && "a range label matches");

        assert(rt.set("mode", Value::integer(Type::Int, 7)));
        (void)rt.step();
        assert(intOf(rt, "out") == 99);
    }

    // --- 3. a timer across scans, and PT changing mid-count -----------------
    {
        Fixture f;
        f.global("run", "BOOL");
        f.global("Timer", "TON");
        f.global("done", "BOOL");
        f.global("preset", "TIME");
        f.section("Cycle",
                  "Timer(IN := run, PT := preset);\n"
                  "done := Timer.Q;\n");
        Runtime rt(f.project);
        assert(rt.prepare().has_value());
        assert(rt.known("Timer.Q") && "an instance's pins are addressable");

        assert(rt.set("preset", Value::time(100)));
        assert(rt.set("run", Value::boolean(true)));

        for (int i = 0; i < 4; ++i) (void)rt.step(20);      // 0, 20, 40, 60 ms
        assert(!boolOf(rt, "done") && "60 ms of a 100 ms timer");
        (void)rt.step(20);                                   // 80
        (void)rt.step(20);                                   // 100
        std::printf("TON elapsed %lld ms, Q = %s\n",
                    static_cast<long long>(intOf(rt, "Timer.ET")),
                    boolOf(rt, "done") ? "TRUE" : "FALSE");
        assert(boolOf(rt, "done") && "the timer reaches its preset");

        // Raising PT while it counts moves the target; the timer keeps counting
        // rather than restarting.
        assert(rt.set("preset", Value::time(200)));
        (void)rt.step(20);
        assert(!boolOf(rt, "done") && "the target moved past where we are");
        for (int i = 0; i < 5; ++i) (void)rt.step(20);
        assert(boolOf(rt, "done") && "and it carries on from where it was");

        // Dropping IN resets it.
        assert(rt.set("run", Value::boolean(false)));
        (void)rt.step(20);
        assert(!boolOf(rt, "done") && intOf(rt, "Timer.ET") == 0);
    }

    // --- 4. forcing wins over the program ------------------------------------
    {
        Fixture f;
        f.global("start", "EBOOL", "%I0.3");
        f.global("motor", "EBOOL", "%Q0.1");
        f.global("cycles", "DINT");
        f.section("Cycle",
                  "IF start THEN\n"
                  "  motor := TRUE;\n"
                  "  cycles := cycles + 1;\n"
                  "ELSE\n"
                  "  motor := FALSE;\n"
                  "END_IF;\n");
        Runtime rt(f.project);
        assert(rt.prepare().has_value());
        assert(rt.known("%I0.3") && "a located variable answers to its address too");

        (void)rt.step();
        assert(!boolOf(rt, "motor"));

        // An input has nowhere else to come from: forcing is the whole point.
        assert(rt.force("start", Value::boolean(true)));
        (void)rt.step();
        assert(boolOf(rt, "motor") && intOf(rt, "cycles") == 1);

        // An output may be written by the program - and forced against it.
        assert(rt.force("motor", Value::boolean(false)));
        (void)rt.step();
        assert(!boolOf(rt, "motor") && "the program wrote TRUE; the force wins");
        assert(intOf(rt, "cycles") == 2 && "the rest of the logic still ran");

        assert(rt.forcedNames().size() == 2);
        rt.unforce("motor");
        (void)rt.step();
        assert(boolOf(rt, "motor") && "releasing the force gives the program control back");
    }

    // --- 5. bad code is reported, not crashed into ---------------------------
    {
        Fixture f;
        f.global("a", "DINT");
        f.global("b", "DINT");
        f.section("Cycle", "a := 10 / b;\n");
        Runtime rt(f.project);
        assert(rt.prepare().has_value());

        const auto report = rt.step();
        assert(report.halted && "a division by zero must stop the scan");
        assert(!report.diagnostics.empty());
        std::printf("caught: %s (line %u)\n", report.diagnostics.front().message.c_str(),
                    report.diagnostics.front().line);
        assert(report.diagnostics.front().message.find("division by zero") != std::string::npos);
        assert(report.diagnostics.front().line == 1);

        assert(rt.set("b", Value::integer(Type::DInt, 5)));
        const auto ok = rt.step();
        assert(!ok.halted && intOf(rt, "a") == 2);
    }

    // --- 6. a loop that never ends is stopped ---------------------------------
    {
        Fixture f;
        f.global("i", "DINT");
        f.section("Cycle", "WHILE TRUE DO\n  i := i + 1;\nEND_WHILE;\n");
        Runtime rt(f.project);
        assert(rt.prepare().has_value());

        RunLimits{};   // the defaults are what the runtime uses
        const auto report = rt.step();
        assert(report.halted && "the application must not hang on a runaway loop");
        assert(!report.diagnostics.empty());
        std::printf("caught: %s\n", report.diagnostics.front().message.c_str());
        assert(report.diagnostics.front().message.find("not terminating") != std::string::npos
               || report.diagnostics.front().message.find("without its condition") != std::string::npos);
    }

    // --- 7. what is not supported says so -------------------------------------
    {
        Fixture f;
        f.global("x", "DINT");
        assert(f.stack.push(std::make_unique<AddSectionCommand>(
                   f.project, "Ladder", "MAST", PouLanguage::LD)).has_value());
        f.section("Cycle", "x := 1;\n");

        Runtime rt(f.project);
        assert(rt.prepare().has_value());
        bool warned = false;
        for (const auto& d : rt.preparationDiagnostics())
            if (d.message.find("Structured Text only") != std::string::npos) warned = true;
        assert(warned && "an LD section must be declared unrunnable, not silently skipped");
        (void)rt.step();
        assert(intOf(rt, "x") == 1 && "the ST section still runs");
    }

    // --- 8. conversions and the string functions the project uses ------------
    {
        Fixture f;
        f.global("ms", "UDINT");
        f.global("t", "TIME");
        f.global("r", "REAL");
        f.global("n", "INT");
        f.global("label", "STRING");
        f.section("Cycle",
                  "t := T#2s;\n"
                  "ms := TIME_TO_UDINT(t);\n"
                  "r := INT_TO_REAL(7);\n"
                  "n := LEN_INT('abcdef');\n"
                  "label := CONCAT_STR('ab', 'cd');\n");
        Runtime rt(f.project);
        assert(rt.prepare().has_value());
        (void)rt.step();

        assert(intOf(rt, "ms") == 2000);
        Value r;
        assert(rt.get("r", r) && std::abs(r.asReal() - 7.0) < 1e-9);
        assert(intOf(rt, "n") == 6);
        Value label;
        assert(rt.get("label", label) && label.asString() == "abcd");
    }

    // --- 9. trends, for the graph window --------------------------------------
    {
        Fixture f;
        f.global("ramp", "DINT");
        f.section("Cycle", "ramp := ramp + 2;\n");
        Runtime rt(f.project);
        assert(rt.prepare().has_value());
        rt.watch("ramp");

        for (int i = 0; i < 50; ++i) (void)rt.step(10);
        const auto* history = rt.history("ramp");
        assert(history && history->size() == 50);
        assert(history->front().value == 2.0 && history->back().value == 100.0);
        assert(history->back().clockMs == 500);
        std::printf("trend: %zu samples, last %g at %lld ms\n",
                    history->size(), history->back().value,
                    static_cast<long long>(history->back().clockMs));
    }

    // --- 10. a project's own function block actually runs --------------------
    {
        Fixture f;
        f.global("rawA", "INT");
        f.global("rawB", "INT");
        f.global("scaledA", "REAL");
        f.global("scaledB", "REAL");

        // A DFB with an interface and a body of its own.
        assert(f.stack.push(std::make_unique<AddFunctionBlockCommand>(
                   f.project, "SCALER", "1.00")).has_value());
        Index dfb = 0;
        for (Index i = 0; i < f.project->pous.size(); ++i)
            if (f.project->pous[i].kind == PouKind::FunctionBlockType) dfb = i;

        for (auto [name, type, scope] :
             std::initializer_list<std::tuple<const char*, const char*, VariableScope>>{
                 {"raw", "INT", VariableScope::Input},
                 {"gain", "REAL", VariableScope::Input},
                 {"scaled", "REAL", VariableScope::Output},
                 {"calls", "DINT", VariableScope::Local}}) {
            AddVariableCommand::Spec s;
            s.name = name; s.type = type; s.scope = scope; s.owner = dfb;
            assert(f.stack.push(std::make_unique<AddVariableCommand>(f.project, s)).has_value());
        }
        assert(f.stack.push(std::make_unique<AddSectionCommand>(
                   f.project, "Body", "", PouLanguage::ST, dfb)).has_value());
        assert(f.stack.push(std::make_unique<SetSectionBodyCommand>(
                   f.project, static_cast<Index>(f.project->sections.size() - 1),
                   "calls := calls + 1;\n"
                   "scaled := INT_TO_REAL(raw) * gain;\n")).has_value());

        // Two instances of it, so the test proves they do not share state.
        f.global("A", "SCALER");
        f.global("B", "SCALER");
        f.section("Cycle",
                  "A(raw := rawA, gain := 2.0);\n"
                  "scaledA := A.scaled;\n"
                  "B(raw := rawB, gain := 10.0);\n"
                  "scaledB := B.scaled;\n");

        Runtime rt(f.project);
        assert(rt.prepare().has_value());
        assert(rt.known("A.scaled") && rt.known("B.calls"));

        assert(rt.set("rawA", Value::integer(Type::Int, 3)));
        assert(rt.set("rawB", Value::integer(Type::Int, 7)));
        const auto report = rt.step();
        for (const auto& d : report.diagnostics)
            std::printf("  sim diag: %s\n", d.message.c_str());
        assert(!report.halted && "the DFB body must run, not stop the scan");

        Value a, b;
        assert(rt.get("scaledA", a) && rt.get("scaledB", b));
        std::printf("DFB: A %g, B %g\n", a.asReal(), b.asReal());
        assert(std::abs(a.asReal() - 6.0) < 1e-9 && "3 * 2.0");
        assert(std::abs(b.asReal() - 70.0) < 1e-9 && "7 * 10.0");

        // Each instance keeps its own locals: the counter must be 1 in both, not
        // 2 in a shared one.
        assert(intOf(rt, "A.calls") == 1 && intOf(rt, "B.calls") == 1);
        (void)rt.step();
        assert(intOf(rt, "A.calls") == 2 && intOf(rt, "B.calls") == 2);

        // And the instance's own name resolves inside its body without being
        // qualified, while the caller sees it qualified.
        assert(intOf(rt, "A.calls") != intOf(rt, "rawA"));
    }

    // --- 10b. names resolve without regard to case, as ST does ---------------
    {
        Fixture f;
        f.global("Counter", "DINT");
        f.global("Flag", "BOOL");
        f.section("Cycle", "counter := COUNTER + 1;\nflAg := TRUE;\n");
        Runtime rt(f.project);
        assert(rt.prepare().has_value());
        (void)rt.step();
        // The declarations say Counter and Flag; the code says counter, COUNTER
        // and flAg. Structured Text does not distinguish them and neither may
        // the simulator - this was stopping a fifth of the reference project.
        assert(intOf(rt, "Counter") == 1);
        assert(boolOf(rt, "Flag"));
        assert(intOf(rt, "cOuNtEr") == 1 && "and reading is case-insensitive too");
    }

    // --- 11. a block with no body says so instead of pretending ---------------
    {
        Fixture f;
        f.global("x", "DINT");
        assert(f.stack.push(std::make_unique<AddFunctionBlockCommand>(
                   f.project, "EMPTY_FB", "1.00")).has_value());
        Index dfb = 0;
        for (Index i = 0; i < f.project->pous.size(); ++i)
            if (f.project->pous[i].kind == PouKind::FunctionBlockType) dfb = i;
        {
            AddVariableCommand::Spec s;
            s.name = "in1"; s.type = "DINT"; s.scope = VariableScope::Input; s.owner = dfb;
            assert(f.stack.push(std::make_unique<AddVariableCommand>(f.project, s)).has_value());
        }
        f.global("E", "EMPTY_FB");
        f.section("Cycle", "E(in1 := 42);\nx := E.in1;\n");

        Runtime rt(f.project);
        assert(rt.prepare().has_value());
        const auto report = rt.step();
        assert(!report.halted && "no body is not an error");
        assert(intOf(rt, "x") == 42 && "the input was still stored");
        bool warned = false;
        for (const auto& d : report.diagnostics)
            if (d.message.find("no Structured Text body") != std::string::npos) warned = true;
        assert(warned && "and the simulator says the body did not run");
    }

    // --- 12. a forcing configuration survives a round trip -------------------
    {
        Fixture f;
        f.global("gStart", "BOOL", "%I0.3");
        f.global("gLevel", "INT");
        f.global("gGain", "REAL");
        f.global("gDelay", "TIME");
        f.section("Cycle", "gLevel := gLevel;\n");
        Runtime rt(f.project);
        assert(rt.prepare().has_value());

        assert(rt.force("gStart", Value::boolean(true)));
        assert(rt.force("gLevel", Value::integer(Type::Int, 42)));
        assert(rt.force("gGain", Value::real(1.5)));
        assert(rt.force("gDelay", Value::time(500)));

        const auto text = rt.exportForcing();
        std::printf("forcing file:\n%s", text.c_str());
        assert(text.find("gStart = TRUE") != std::string::npos);
        assert(text.find("gLevel = 42") != std::string::npos);

        // A fresh runtime, the same scenario.
        Runtime other(f.project);
        assert(other.prepare().has_value());
        assert(other.forcedNames().empty());
        const auto applied = other.importForcing(text);
        assert(applied == 4);
        assert(other.forcedNames().size() == 4);
        assert(boolOf(other, "gStart"));
        assert(intOf(other, "gLevel") == 42);
        assert(intOf(other, "gDelay") == 500 && "T#500ms comes back as 500 ms");

        // A name that no longer exists is reported, never skipped in silence: a
        // scenario that half-applies without saying so is worse than one that
        // refuses.
        std::vector<std::string> unknown;
        const auto partial = other.importForcing("gLevel = 7\nvanished = TRUE\n", &unknown);
        assert(partial == 1);
        assert(unknown.size() == 1 && unknown.front() == "vanished");
        assert(intOf(other, "gLevel") == 7);
    }

    std::printf("\nsimulation_test: all assertions passed\n");
    return 0;
}
