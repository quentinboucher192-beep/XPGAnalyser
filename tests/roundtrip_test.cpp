// =============================================================================
//  tests/roundtrip_test.cpp — import, write, re-import, compare
// -----------------------------------------------------------------------------
//  The writer is the risky half of this tool. Reading a file wrongly costs a
//  diagnostic; writing one wrongly costs an engineer an afternoon in front of a
//  machine that will not import. So the writer is not trusted, it is measured:
//  the real export goes in, comes back out, is read again, and the two models
//  are compared entity by entity.
//
//  A second pass then checks that writing the *re-imported* model produces a
//  byte-identical document. Convergence after one round is what says the writer
//  and the reader agree; drift on the second pass would mean one of them is
//  losing or inventing something.
// =============================================================================
#include "../src/export/XpgWriter.hpp"
#include "../src/import/ProjectImporter.hpp"

#include <cassert>
#include <cstdio>
#include <map>
#include <string>

using namespace importer;
using namespace domain;

namespace {

struct Shape {
    std::size_t variables{}, globals{}, ddts{}, ddtFields{}, fbTypes{}, programUnits{};
    std::size_t sections{}, tasks{}, animationTables{}, lines{}, statements{};
    std::map<std::string, std::string> variableTypes;   // name -> type
    // Keyed by owner *and* name: this project has several sections called
    // "Init", one per program unit, and a name-only key silently compared two
    // different bodies with each other.
    std::map<std::string, std::size_t> sectionBodies;
};

Shape shapeOf(const Project& p) {
    Shape s;
    s.variables = p.variables.size();
    for (const auto& v : p.variables) {
        if (v.scope == VariableScope::Global) ++s.globals;
        if (v.scope == VariableScope::DerivedMember) ++s.ddtFields;
        s.variableTypes[std::string(p.strings.text(v.name))] =
            std::string(p.strings.text(v.type.name));
    }
    s.ddts = p.derivedTypes.size();
    for (const auto& pou : p.pous) {
        if (pou.kind == PouKind::FunctionBlockType) ++s.fbTypes;
        if (pou.kind == PouKind::ProgramUnit)       ++s.programUnits;
    }
    s.sections = p.sections.size();
    for (const auto& sec : p.sections) {
        s.lines += sec.lineCount;
        s.statements += sec.statementCount;
        const std::string owner = (sec.owner != kNoIndex && sec.owner < p.pous.size())
                                      ? std::string(p.strings.text(p.pous[sec.owner].name))
                                      : std::string("<task>");
        s.sectionBodies[owner + "::" + std::string(p.strings.text(sec.name))] = sec.body.size();
    }
    s.tasks = p.tasks.size();
    s.animationTables = p.animationTables.size();
    return s;
}

std::shared_ptr<const Project> importText(const std::string& path) {
    core::EventBus bus;
    ProjectImporter imp(bus);
    auto res = imp.importFile(path);
    if (!res) {
        std::fprintf(stderr, "import failed: %s\n", res.error().message().c_str());
        return nullptr;
    }
    return res->project;
}

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : "tests/fixtures/MAST.XPG";
    const std::string out1   = "/tmp/roundtrip-pass1.XPG";
    const std::string out2   = "/tmp/roundtrip-pass2.XPG";

    // --- pass 1: read the real export --------------------------------------
    auto original = importText(source);
    assert(original && "the reference export must import");
    const auto before = shapeOf(*original);
    std::printf("original : %zu variables, %zu DDT, %zu DFB, %zu units, %zu sections, %zu lines\n",
                before.variables, before.ddts, before.fbTypes, before.programUnits,
                before.sections, before.lines);

    // --- write it back ------------------------------------------------------
    exporter::XpgWriteOptions opts;
    opts.dateTime = "date_and_time#2026-1-1-0:00:00";   // fixed, so passes compare
    auto written = exporter::writeXpg(*original, opts);
    assert(written && "writing must succeed");
    assert(exporter::writeXpgFile(*original, out1, opts).has_value());
    std::printf("written  : %zu bytes\n", written->size());

    // --- pass 2: read our own output ---------------------------------------
    auto reimported = importText(out1);
    assert(reimported && "the file we wrote must import through our own reader");
    const auto after = shapeOf(*reimported);
    std::printf("re-read  : %zu variables, %zu DDT, %zu DFB, %zu units, %zu sections, %zu lines\n",
                after.variables, after.ddts, after.fbTypes, after.programUnits,
                after.sections, after.lines);

    // --- nothing may be lost ------------------------------------------------
    assert(after.ddts == before.ddts && "every derived type must survive");
    assert(after.ddtFields == before.ddtFields && "every DDT member must survive");
    assert(after.fbTypes == before.fbTypes && "every DFB type must survive");
    assert(after.programUnits == before.programUnits);
    assert(after.sections == before.sections && "every section must survive");
    assert(after.globals == before.globals && "the whole dictionary must survive");
    std::fflush(stdout); if (after.variables != before.variables) std::fprintf(stderr, "variables %zu -> %zu (globals %zu -> %zu, ddt fields %zu -> %zu)\n", before.variables, after.variables, before.globals, after.globals, before.ddtFields, after.ddtFields);
    assert(after.variables == before.variables);
    assert(after.tasks == before.tasks);
    assert(after.animationTables == before.animationTables);

    // Line counts prove the bodies came back intact, not merely present.
    assert(after.lines == before.lines && "not one line of code may be lost");
    assert(after.statements == before.statements);

    // Every symbol keeps its exact type spelling, including "ARRAY[0..27] OF X".
    assert(after.variableTypes.size() == before.variableTypes.size());
    for (const auto& [name, type] : before.variableTypes) {
        const auto it = after.variableTypes.find(name);
        assert(it != after.variableTypes.end() && "a symbol disappeared");
        assert(it->second == type && "a symbol changed type");
    }
    for (const auto& [name, length] : before.sectionBodies) {
        const auto it = after.sectionBodies.find(name);
        assert(it != after.sectionBodies.end() && "a section disappeared");
        assert(it->second == length && "a section body changed length");
    }

    // --- the header the CPU is identified by --------------------------------
    assert(reimported->hardware.cpuReference == original->hardware.cpuReference);
    assert(reimported->hardware.cpuFirmware  == original->hardware.cpuFirmware);
    assert(reimported->header.projectName    == original->header.projectName);

    // --- flags that are not checksums must be passed through untouched ------
    assert(written->find("UseNewTplSignAlgo") != std::string::npos
           && "a flag Control Expert set must come back");
    assert(written->find("UseNewTplSignAlgo\" value=\"TRUE\"") != std::string::npos);

    // --- checksums are written as zero, on instruction ----------------------
    assert(written->find("TypeSignatureCheckSumString") != std::string::npos);
    {
        std::size_t zeroed = 0, at = 0;
        while ((at = written->find("TypeSignatureCheckSumString", at)) != std::string::npos) {
            const auto value = written->find("value=\"", at);
            assert(value != std::string::npos);
            if (written->compare(value + 7, 2, "0\"") == 0) ++zeroed;
            at = value;
        }
        // Both DDTs and DFB types carry a signature checksum, so the count is
        // the sum of the two, not the DDT count alone.
        std::printf("checksums: %zu written as 0 (%zu DDT + %zu DFB)\n",
                    zeroed, before.ddts, before.fbTypes);
        assert(zeroed == before.ddts + before.fbTypes
               && "Control Expert recomputes these on import");
    }

    // --- convergence: writing the re-import must reproduce the same bytes ---
    auto second = exporter::writeXpg(*reimported, opts);
    assert(second);
    assert(exporter::writeXpgFile(*reimported, out2, opts).has_value());
    std::printf("pass 2   : %zu bytes, %s\n", second->size(),
                *second == *written ? "identical" : "DIFFERENT");
    assert(*second == *written
           && "a second write must be byte-identical, or reader and writer disagree");

    // --- XML escaping must survive text that contains markup ----------------
    {
        const auto escaped = exporter::escapeXml("a < b && c > \"d\" 'e'");
        assert(escaped == "a &lt; b &amp;&amp; c &gt; &quot;d&quot; &apos;e&apos;");
    }

    std::printf("\nroundtrip_test: all assertions passed\n");
    return 0;
}
