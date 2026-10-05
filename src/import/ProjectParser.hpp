// =============================================================================
//  import/ProjectParser.hpp — parser strategy + concrete Control Expert parsers
// -----------------------------------------------------------------------------
//  WHAT A .MAST / .XPG ACTUALLY CONTAINS  (verified against the supplied file)
//  --------------------------------------------------------------------------
//  Control Expert exports a project as a family of XML documents, not one file:
//
//    .XPG  program export   — tasks, sections, DDTs, DFB types, the data block
//    .XDB  data export      — the full variable dictionary with comments
//    .XHW  hardware export  — racks, slots, module references, firmware
//    .XEF / .ZEF            — whole-project export (contains all of the above)
//    .STU                   — the binary working file; not an exchange format
//
//  A section export is conventionally named after its task, which is why the
//  brief calls the entry point "MAST": the file is MAST.XPG. It carries the
//  program *and* the declarations, but of the hardware it carries only the
//  resource identity:
//
//      <resource resName="Micro Basic" resIdent="BMX P34 2020 03.30">
//
//  That is enough to identify the CPU (BMX P34 2020, Modicon M340 family, OS
//  03.30) but not the rack layout or the I/O modules. The importer therefore
//  reports hardware as *inferred* and asks for the .XHW/.XEF when the user opens
//  the PLC configuration view, instead of inventing a rack.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "../domain/ProjectModel.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace importer {

enum class SourceFormat : std::uint8_t { Unknown, Xpg, Xdb, Xhw, Xef, Xsy, Zef, Stu };

struct ParseOptions {
    bool keepSourceBodies{true};    // false halves memory when only metrics are wanted
    bool parseAnimationTables{true};
    bool strict{false};             // strict => unknown elements are an error
    std::size_t maxBytes{256ull * 1024 * 1024};
};

struct ParseProgress {
    std::string stage;              // "declarations", "sections", "cross-reference"
    float       fraction{0.f};
    std::size_t bytesRead{0};
};
using ProgressFn = std::function<void(const ParseProgress&)>;

struct ParseDiagnostic {
    enum class Level : std::uint8_t { Info, Warning, Error };
    Level       level{Level::Info};
    std::string message;
    std::size_t line{0};
    std::string source;
};

struct ParseOutcome {
    std::size_t                  elementsVisited{0};
    std::size_t                  bytesRead{0};
    double                       milliseconds{0.0};
    std::vector<ParseDiagnostic> diagnostics;
};

// ---------------------------------------------------------------------------
// Strategy. Every concrete parser fills part of the same Project, so a project
// assembled from four files goes through the same merge path as one file.
// ---------------------------------------------------------------------------
class IProjectParser {
public:
    virtual ~IProjectParser() = default;
    [[nodiscard]] virtual SourceFormat format() const noexcept = 0;
    [[nodiscard]] virtual bool canParse(std::string_view firstKilobyte) const noexcept = 0;
    virtual core::Result<ParseOutcome> parse(std::string_view buffer,
                                             domain::Project& into,
                                             const ParseOptions& opts,
                                             const ProgressFn& progress) = 0;
};
using ParserPtr = std::unique_ptr<IProjectParser>;

// --------------------------------------------------------------- XPG parser ---
class XpgParser final : public IProjectParser {
public:
    [[nodiscard]] SourceFormat format() const noexcept override { return SourceFormat::Xpg; }
    [[nodiscard]] bool canParse(std::string_view firstKilobyte) const noexcept override;
    core::Result<ParseOutcome> parse(std::string_view buffer,
                                     domain::Project& into,
                                     const ParseOptions& opts,
                                     const ProgressFn& progress) override;
};

// Declared here, implemented against their own schemas; they exist so that the
// importer's contract is complete and the UI can state precisely what is missing.
class XdbParser final : public IProjectParser {
public:
    [[nodiscard]] SourceFormat format() const noexcept override { return SourceFormat::Xdb; }
    [[nodiscard]] bool canParse(std::string_view) const noexcept override;
    core::Result<ParseOutcome> parse(std::string_view, domain::Project&,
                                     const ParseOptions&, const ProgressFn&) override;
};

class XhwParser final : public IProjectParser {
public:
    [[nodiscard]] SourceFormat format() const noexcept override { return SourceFormat::Xhw; }
    [[nodiscard]] bool canParse(std::string_view) const noexcept override;
    core::Result<ParseOutcome> parse(std::string_view, domain::Project&,
                                     const ParseOptions&, const ProgressFn&) override;
};

// ------------------------------------------------------------------ catalog ---
// Maps a resIdent string to a family / CPU / firmware triple. Data-driven from
// resources/catalog/plc_catalog.json so that supporting a new range is a data
// change, not a code change.
// ---------------------------------------------------------------------------
//  SCOPE OF THE CATALOG, since it is a fair question.
//
//  For *reading* an export it is almost unnecessary: the .XHW states every
//  reference, every family, every slot and, through <channelATS>, enough to
//  derive the point counts. The catalog earns its place in three narrow spots:
//
//    1. a module with a single channel group per direction (BMXDDM16022 here)
//       gives no stride to measure, so its split must be looked up;
//    2. rack slot counts are implied by the backplane reference, not stated;
//    3. human-readable descriptions.
//
//  For *generating* a configuration - building template applications - the
//  requirement is an order of magnitude larger: allowed slots, power budget per
//  rack, addressing rules, module compatibility per CPU range. That is a
//  vendor-data problem, not a parsing one, which is why this table is loaded
//  from a text file rather than compiled in: extending it must not require a
//  rebuild, and today it covers only the M340 references in hand.
// ---------------------------------------------------------------------------
// "ARRAY[0..27] OF armoire", "STRING[32]", "INT" -> a resolved TypeRef.
// Shared with the editor so a type typed into a dialog is understood the same
// way as one read from an export; two parsers for one syntax is two chances to
// disagree.
[[nodiscard]] domain::TypeRef classifyTypeName(std::string_view raw, domain::StringPool&);

class HardwareCatalog {
public:
    // Limits published by Schneider for the processor. They are what makes the
    // difference between drawing a configuration and being told it will not
    // build - and they are per CPU, not per range, so they belong here.
    struct Cpu {
        std::string reference;      // "BMX P34 2020"
        std::string family;         // "Modicon M340"
        std::string range;          // "BMX"
        std::uint16_t maxRacks{0};
        std::uint16_t slotsPerRack{0};
        std::vector<std::string> embeddedPorts;   // "Ethernet TCP/IP", "Modbus serial"

        std::uint16_t maxDiscretePoints{0};   // whole configuration
        std::uint16_t maxAnalogChannels{0};
        std::uint16_t maxSpecificChannels{0}; // counting, motion, serial...
        std::uint32_t programMemoryKb{0};
        std::uint32_t dataMemoryKb{0};
    };

    struct ModuleEntry {
        std::string   reference;     // "BMXDDI3202K", as exported: no spaces
        std::string   family;        // "Modicon M340"
        std::string   description;
        std::uint16_t inputPoints{0};
        std::uint16_t outputPoints{0};
        std::uint16_t slots{0};      // racks only
    };

    static core::Result<HardwareCatalog> loadFromFile(const std::string& path);
    static HardwareCatalog builtinFallback();

    // "BMX P34 2020 03.30" -> {cpu, firmware "03.30"}
    [[nodiscard]] core::Result<std::pair<Cpu, std::string>> resolve(std::string_view resIdent) const;

    // Reference lookup ignoring spaces and case, because the two exports spell
    // the same CPU "BMX P34 2020" and "BMXP342020".
    [[nodiscard]] const ModuleEntry* findModule(std::string_view reference) const;
    // Entries whose reference starts with `prefix`, in catalogue order. Used to
    // fill the choices a dialog offers rather than hard-coding a list twice.
    [[nodiscard]] std::vector<const ModuleEntry*> modulesStartingWith(std::string_view prefix) const;
    [[nodiscard]] std::vector<const ModuleEntry*> powerSupplies() const;
    [[nodiscard]] std::vector<const ModuleEntry*> backplanes() const;
    [[nodiscard]] const Cpu*         findCpu(std::string_view reference) const;
    [[nodiscard]] std::size_t moduleCount() const noexcept { return modules_.size(); }

    // Total slots the processor can address across the whole configuration.
    // Schneider publishes this per rack count rather than as a single number:
    // 11 for one rack, 23 for two, 35 for three, 47 for four.
    [[nodiscard]] static std::uint16_t maxSlotsForRacks(const Cpu&, std::uint16_t rackCount);

private:
    std::vector<Cpu>         cpus_;
    std::vector<ModuleEntry> modules_;
};

} // namespace importer
