// =============================================================================
//  tests/hardware_test.cpp — the .XHW hardware export
// -----------------------------------------------------------------------------
//  Runs the two exports together, exactly as the application does: the program
//  export first (CPU identity, no layout), then the hardware export (racks,
//  modules, memory sizing). The interesting assertions are the ones about where
//  a number came from - the file or the catalog.
// =============================================================================
#include "../src/import/ProjectImporter.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using namespace importer;
using namespace domain;

int main(int argc, char** argv) {
    const std::string xpg = argc > 1 ? argv[1] : "tests/fixtures/MAST.XPG";
    const std::string xhw = argc > 2 ? argv[2] : "tests/fixtures/CONFIG.XHW";

    core::EventBus bus;
    ProjectImporter imp(bus);
    auto res = imp.importFiles({xpg, xhw});
    if (!res) { std::fprintf(stderr, "import failed: %s\n", res.error().message().c_str()); return 1; }
    bus.drain();

    const auto& p = *res->project;
    const auto& hw = p.hardware;

    std::printf("CPU        : %s %s  OS %s\n",
                hw.family.c_str(), hw.cpuReference.c_str(), hw.cpuFirmware.c_str());
    std::printf("bus        : %s   supply: %s\n", hw.busName.c_str(), hw.powerSupply.c_str());
    std::printf("memory cfg : %%M %u bits, %%MW %u words, %%KW %u words%s\n",
                hw.memory.internalBits, hw.memory.internalWords, hw.memory.constantWords,
                hw.memory.initialiseWords ? ", %MW zeroed on cold start" : "");
    std::printf("racks      : %zu, modules %u, %u inputs / %u outputs\n\n",
                hw.racks.size(), hw.totalModules(),
                hw.totalInputPoints(), hw.totalOutputPoints());

    for (const auto& rack : hw.racks) {
        std::printf("rack %u  %s  (%s, %u slots)\n", rack.number, rack.reference.c_str(),
                    rack.description.c_str(), rack.slotCount);
        for (const auto& m : rack.modules) {
            char points[48] = "-";
            if (m.points())
                std::snprintf(points, sizeof points, "%u in / %u out%s",
                              m.inputPoints, m.outputPoints,
                              m.pointsFromCatalog ? " (catalog)" : "");
            std::printf("   slot %2d  %-14s %-16s v%-6s %-22s %2zu ch  %s\n",
                        m.slot, m.reference.c_str(),
                        std::string(toString(m.kind)).c_str(), m.firmware.c_str(),
                        m.description.empty() ? "(not in catalog)" : m.description.c_str(),
                        m.channels.size(), points);
        }
    }

    // ---- structure -------------------------------------------------------
    assert(hw.racks.size() == 3);
    assert(!hw.inferred && "importing a .XHW must clear the inferred flag");
    assert(hw.racks[0].reference == "BMXXBP0800");
    assert(hw.racks[0].slotCount == 8 && "slot count comes from the catalog");
    assert(hw.racks[1].reference == "BMEXBP1200");
    assert(hw.racks[2].slotCount == 12);

    // Rack 0: power supply plus eight modules, supply sorted first at slot -1.
    assert(hw.racks[0].modules.size() == 9);
    assert(hw.racks[0].modules.front().kind == ModuleKind::PowerSupply);
    assert(hw.racks[0].modules.front().slot == -1);
    assert(hw.powerSupply == "BMXCPS2000");

    // ---- point counts derived from the file, not looked up -----------------
    auto find = [&](const char* ref) -> const Module* {
        for (const auto& r : hw.racks)
            for (const auto& m : r.modules)
                if (m.reference == ref) return &m;
        return nullptr;
    };

    const auto* ddi32 = find("BMXDDI3202K");
    assert(ddi32 && ddi32->inputPoints == 32 && ddi32->outputPoints == 0);
    assert(!ddi32->pointsFromCatalog && "4 groups of 8 are measurable from the file");

    const auto* ddi64 = find("BMXDDI6402K");
    assert(ddi64 && ddi64->inputPoints == 64);
    const auto* ddo64 = find("BMXDDO6402K");
    assert(ddo64 && ddo64->outputPoints == 64 && ddo64->kind == ModuleKind::DiscreteOutput);
    const auto* dra = find("BMXDRA1605");
    assert(dra && dra->outputPoints == 16);

    // ---- the one case the file cannot settle -------------------------------
    const auto* ddm = find("BMXDDM16022");
    assert(ddm && ddm->kind == ModuleKind::DiscreteMixed);
    assert(ddm->pointsFromCatalog && "a single group per direction has no stride to measure");
    // 8 in / 8 out per the Control Expert catalogue ("Dig 8E 24 Vdc 8S Source Tr").
    // An earlier seed said 16/8, inferred from the reference; the export could
    // not disprove it, which is why this assertion is written against the vendor
    // catalogue and not against the part number.
    assert(ddm->inputPoints == 8 && ddm->outputPoints == 8);

    // ---- analog channels are declared singly -------------------------------
    const auto* ami8 = find("BMXAMI0800");
    assert(ami8 && ami8->channels.size() == 8 && ami8->kind == ModuleKind::AnalogInput);
    const auto* ami4 = find("BMXAMI0410");
    assert(ami4 && ami4->channels.size() == 4);

    // ---- a module with no configModule at all ------------------------------
    const auto* nrp = find("BMXNRP0201");
    assert(nrp && nrp->channels.empty() && nrp->kind == ModuleKind::Communication);
    assert(nrp->knownReference && !nrp->description.empty());

    // ---- CPU and memory ----------------------------------------------------
    assert(hw.cpuReference == "BMXP342020");
    assert(hw.family == "Modicon M340");
    assert(hw.memory.declared);
    assert(hw.memory.internalWords == 1024);
    assert(hw.memory.constantWords == 256);
    assert(hw.memory.internalBits == 512);
    assert(hw.busName == "XBusMicro");

    // The two exports disagree about the OS version; that must be reported.
    bool sawVersionWarning = false;
    for (const auto& d : res->diagnostics)
        if (d.message.find("OS version differs") != std::string::npos) sawVersionWarning = true;
    std::printf("\ndiagnostics: %zu\n", res->diagnostics.size());
    for (const auto& d : res->diagnostics)
        if (d.level != ParseDiagnostic::Level::Info)
            std::printf("  %s\n", d.message.c_str());
    assert(sawVersionWarning && "03.30 in the program export vs 03.50 in the hardware export");

    // The .XPG's "rack layout missing" notice must be gone once the .XHW lands.
    for (const auto& n : p.partialDataNotices)
        assert(n.find("Rack and module layout") == std::string::npos);

    std::printf("\nhardware_test: all assertions passed\n");
    return 0;
}
