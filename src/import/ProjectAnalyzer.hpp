// =============================================================================
//  import/ProjectAnalyzer.hpp — metrics and findings for the dashboard
// -----------------------------------------------------------------------------
//  The analyzer is a pure function of the Project: same input, same report, no
//  hidden state, no UI dependency. That makes it runnable on a worker thread and
//  testable against a fixture project.
//
//  COST MODEL
//  The expensive part is cross-referencing. Naively, "is variable V used?" over
//  50 000 variables and 10 MB of source is 50 000 substring searches. Instead the
//  analyzer tokenises every section body once into an identifier multiset keyed
//  by interned SymbolId; the answer for every variable is then one hash lookup.
//  Complexity is O(bytes of code + number of symbols), not the product.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "../domain/ProjectModel.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace importer {

enum class FindingKind : std::uint8_t {
    UnusedGlobalVariable,
    UnusedLocalVariable,
    UnusedDerivedType,
    UnusedFunctionBlockType,
    UnreferencedSection,        // never scheduled and never called
    EmptySection,
    UnconditionalSection,       // no activation condition: worth flagging in a review
    DuplicateAddress,           // two symbols mapped onto the same %MW
    UndefinedType,
    MissingComment,             // located I/O without documentation
    LongSection,                // exceeds the maintainability threshold
};

struct Finding {
    FindingKind   kind{FindingKind::UnusedGlobalVariable};
    std::string   subject;      // symbol or section name
    std::string   detail;
    domain::Index target{domain::kNoIndex};
    enum class Severity : std::uint8_t { Info, Warning, Error } severity{Severity::Info};
};

// One row per distinct type actually declared in the project, with what it
// costs. Replaces the old "top 10 most declared types", which answered a
// question nobody asks: what matters is not which type appears most often but
// which one is eating the memory.
struct TypeUsage {
    std::string      name;
    domain::TypeClass klass{domain::TypeClass::Unknown};
    std::uint32_t    declarations{0};   // variables declaring this type
    std::uint32_t    instances{0};      // DFB instances / DDT instances
    std::uint32_t    bitsEach{0};
    std::uint64_t    bitsTotal{0};
    bool             resolved{true};    // false: the type is not defined in this export

    [[nodiscard]] double bytesEach() const { return bitsEach / 8.0; }
    [[nodiscard]] double bytesTotal() const { return static_cast<double>(bitsTotal) / 8.0; }
};

// Where the declared memory goes. These are declaration footprints, not the
// PLC's actual allocation map: the build report is the authority for that.
struct MemoryBreakdown {
    std::uint64_t totalBits{0};
    std::uint64_t globalBits{0};
    std::uint64_t localBits{0};
    std::uint64_t parameterBits{0};
    std::uint64_t locatedBits{0};
    std::uint64_t derivedTypeBits{0};   // one instance of every DDT
    std::uint64_t dfbInstanceBits{0};
    std::uint64_t unresolvedTypes{0};   // declarations whose size could not be computed
};

struct LanguageBreakdown {
    domain::PouLanguage language{domain::PouLanguage::Unknown};
    std::uint32_t sections{0};
    std::uint32_t lines{0};
};

struct AnalysisReport {
    // counters shown on the dashboard
    std::uint32_t totalVariables{0};
    std::uint32_t globalVariables{0};
    std::uint32_t localVariables{0};
    std::uint32_t constants{0};
    std::uint32_t locatedVariables{0};
    std::uint32_t derivedTypes{0};
    std::uint32_t derivedTypeFields{0};
    std::uint32_t functionBlockTypes{0};
    std::uint32_t dfbInstances{0};
    std::uint32_t programUnits{0};
    std::uint32_t sections{0};
    std::uint32_t tasks{0};
    std::uint32_t animationTables{0};

    std::uint32_t linesOfCode{0};
    std::uint32_t statements{0};
    std::uint32_t commentedVariables{0};

    std::vector<LanguageBreakdown>                    byLanguage;
    std::vector<TypeUsage>                            typeUsage;       // every type, by memory
    MemoryBreakdown                                   memory;
    std::vector<std::pair<std::string, std::uint32_t>> topTypes;        // type -> declarations
    std::vector<std::pair<std::string, std::uint32_t>> largestSections; // name -> lines
    std::vector<std::pair<std::string, std::uint32_t>> mostUsedDfbs;

    std::vector<Finding> findings;

    double analysisMilliseconds{0.0};

    [[nodiscard]] std::uint32_t count(FindingKind) const;
    [[nodiscard]] double totalBytes() const { return static_cast<double>(memory.totalBits) / 8.0; }
    [[nodiscard]] float documentationCoverage() const {
        return totalVariables ? static_cast<float>(commentedVariables) / static_cast<float>(totalVariables) : 0.f;
    }
};

struct AnalysisOptions {
    bool          detectUnusedVariables{true};
    bool          detectUnusedSections{true};
    bool          detectDuplicateAddresses{true};
    bool          requireCommentsOnIo{true};
    std::uint32_t longSectionThreshold{500};    // lines
    std::uint32_t topN{10};
};

class ProjectAnalyzer {
public:
    using ProgressFn = std::function<void(float fraction, std::string_view stage)>;

    core::Result<AnalysisReport> analyze(const domain::Project&,
                                         const AnalysisOptions& = {},
                                         const ProgressFn& = {},
                                         const std::atomic_bool* cancel = nullptr) const;

    // Exposed because the cross-reference index is also what powers "find all
    // references" in the variable browser; the UI reuses it instead of rescanning.
    using ReferenceIndex = std::unordered_map<domain::SymbolId, std::uint32_t>;
    [[nodiscard]] static ReferenceIndex buildReferenceIndex(const domain::Project&);
};

std::string_view toString(FindingKind) noexcept;

} // namespace importer
