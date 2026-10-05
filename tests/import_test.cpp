// End-to-end: read a real Control Expert export, build the domain model, run the
// analyzer, and print what the dashboard would show.
#include "../src/import/ProjectImporter.hpp"

#include <cassert>
#include <cstdio>
#include <iostream>

using namespace importer;
using namespace domain;

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "/mnt/project/MAST.XPG";

    core::EventBus  bus;
    int progressEvents = 0, finished = 0;
    auto s1 = bus.subscribe<ImportProgress>([&](const ImportProgress&) { ++progressEvents; });
    auto s2 = bus.subscribe<ImportParsed>([&](const ImportParsed& e) {
        ++finished;
        std::printf("parsed %s: %zu variables, %zu sections in %.1f ms\n",
                    e.path.c_str(), e.variables, e.sections, e.ms);
    });

    ProjectImporter importer(bus);
    auto res = importer.importFile(path);
    if (!res) {
        std::cerr << "import failed: " << res.error().message() << "\n";
        return 1;
    }
    bus.drain();
    assert(finished == 1 && progressEvents > 0);

    const auto& p = *res->project;
    const auto& r = res->report;

    std::printf("\n--- header --------------------------------------------------\n");
    std::printf("product          : %s\n", p.header.product.c_str());
    std::printf("project          : %s  v%s  (DTD %s)\n",
                p.header.projectName.c_str(), p.header.projectVersion.c_str(),
                p.header.dtdVersion.c_str());
    std::printf("resource         : %s\n", p.hardware.resourceName.c_str());
    std::printf("CPU              : %s %s  firmware %s%s\n",
                p.hardware.family.c_str(), p.hardware.cpuReference.c_str(),
                p.hardware.cpuFirmware.c_str(), p.hardware.inferred ? "  (inferred)" : "");

    std::printf("\n--- dashboard counters --------------------------------------\n");
    std::printf("variables        : %u  (global %u, local/param %u, DDT members %u)\n",
                r.totalVariables, r.globalVariables, r.localVariables, r.derivedTypeFields);
    std::printf("located I/O      : %u\n", r.locatedVariables);
    std::printf("derived types    : %u\n", r.derivedTypes);
    std::printf("DFB types        : %u   instances: %u\n", r.functionBlockTypes, r.dfbInstances);
    std::printf("program units    : %u\n", r.programUnits);
    std::printf("sections         : %u   tasks: %u   animation tables: %u\n",
                r.sections, r.tasks, r.animationTables);
    std::printf("lines of code    : %u   statements: %u\n", r.linesOfCode, r.statements);
    std::printf("doc coverage     : %.1f %%\n", 100.0 * r.documentationCoverage());
    std::printf("parse+analyze    : %.1f ms\n", res->totalMilliseconds);

    std::printf("\n--- by language ---------------------------------------------\n");
    for (const auto& l : r.byLanguage)
        std::printf("  %-4s %4u sections %7u lines\n",
                    std::string(toString(l.language)).c_str(), l.sections, l.lines);

    std::printf("\n--- most declared types -------------------------------------\n");
    for (std::size_t i = 0; i < r.topTypes.size() && i < 6; ++i)
        std::printf("  %-28s %4u\n", r.topTypes[i].first.c_str(), r.topTypes[i].second);

    std::printf("\n--- memory footprint ----------------------------------------\n");
    std::printf("declared total   : %.1f KB\n", r.totalBytes() / 1024.0);
    std::printf("  global         : %.1f KB\n", r.memory.globalBits / 8192.0);
    std::printf("  local          : %.1f KB\n", r.memory.localBits / 8192.0);
    std::printf("  parameters     : %.1f KB\n", r.memory.parameterBits / 8192.0);
    std::printf("  located I/O    : %.1f KB\n", r.memory.locatedBits / 8192.0);
    std::printf("  DFB instances  : %.1f KB\n", r.memory.dfbInstanceBits / 8192.0);
    std::printf("  DDT definitions: %.1f KB\n", r.memory.derivedTypeBits / 8192.0);
    std::printf("unresolved types : %llu declarations\n",
                static_cast<unsigned long long>(r.memory.unresolvedTypes));

    std::printf("\n--- types by memory (top 10 of %zu) -------------------------\n",
                r.typeUsage.size());
    std::printf("  %-28s %6s %8s %10s\n", "Type", "count", "bytes", "total");
    for (std::size_t i = 0; i < r.typeUsage.size() && i < 10; ++i) {
        const auto& t = r.typeUsage[i];
        std::printf("  %-28s %6u %8.1f %10.0f\n",
                    t.name.c_str(), t.declarations, t.bytesEach(), t.bytesTotal());
    }

    std::printf("\n--- most instantiated DFBs ----------------------------------\n");
    for (std::size_t i = 0; i < r.mostUsedDfbs.size() && i < 6; ++i)
        std::printf("  %-28s %4u\n", r.mostUsedDfbs[i].first.c_str(), r.mostUsedDfbs[i].second);

    std::printf("\n--- findings ------------------------------------------------\n");
    std::printf("  unused globals      : %u\n", r.count(FindingKind::UnusedGlobalVariable));
    std::printf("  unused locals       : %u\n", r.count(FindingKind::UnusedLocalVariable));
    std::printf("  unused DDTs         : %u\n", r.count(FindingKind::UnusedDerivedType));
    std::printf("  unused DFB types    : %u\n", r.count(FindingKind::UnusedFunctionBlockType));
    std::printf("  empty sections      : %u\n", r.count(FindingKind::EmptySection));
    std::printf("  long sections       : %u\n", r.count(FindingKind::LongSection));
    std::printf("  duplicate addresses : %u\n", r.count(FindingKind::DuplicateAddress));
    std::printf("  undocumented I/O    : %u\n", r.count(FindingKind::MissingComment));
    std::printf("  total findings      : %zu\n", r.findings.size());

    std::printf("\n--- partial data notices ------------------------------------\n");
    for (const auto& n : p.partialDataNotices) std::printf("  ! %s\n", n.c_str());

    // ---- invariants -------------------------------------------------------
    // Fixture-specific expectations only apply to the reference export.
    const bool referenceFixture = path.find("MAST.XPG") != std::string::npos;
    assert(p.header.company == "Schneider Automation");
    if (referenceFixture) {
    assert(r.totalVariables > 800);
    assert(r.sections > 60);
    assert(r.derivedTypes == 28);
    assert(r.functionBlockTypes == 8);
    assert(p.hardware.family == "Modicon M340");
    assert(p.hardware.cpuReference == "BMX P34 2020");
    assert(p.hardware.cpuFirmware == "03.30");
    assert(!p.partialDataNotices.empty() && "must declare that hardware layout is missing");
    }

    // Address parsing must sort numerically, not lexicographically.
    assert(Address::parse("%MW9") < Address::parse("%MW10"));

    // Type sizing must agree with the Control Expert conventions.
    assert(domain::elementaryBits("BOOL") == 1);
    assert(domain::elementaryBits("INT") == 16);
    assert(domain::elementaryBits("REAL") == 32);
    assert(domain::elementaryBits("DINT") == 32);
    assert(domain::elementaryBits("string[32]") == (32 + 2) * 8);
    assert(domain::elementaryBits("DFB_GRAFCETENGINE") == 0);   // not elementary
    assert(!r.typeUsage.empty());
    assert(r.memory.totalBits > 0);
    // Sorted heaviest first.
    for (std::size_t i = 1; i < r.typeUsage.size(); ++i)
        assert(r.typeUsage[i - 1].bitsTotal >= r.typeUsage[i].bitsTotal);
    assert(Address::parse("%I0.3").bit.value() == 3);
    assert(!Address::parse("nonsense").valid());

    std::printf("\nimport_test: all assertions passed\n");
    return 0;
}
