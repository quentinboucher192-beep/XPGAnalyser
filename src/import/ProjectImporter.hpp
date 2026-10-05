// =============================================================================
//  import/ProjectImporter.hpp — orchestration
// -----------------------------------------------------------------------------
//  Responsibilities:
//    * pick a parser (Factory, by sniffing the first kilobyte rather than by
//      trusting the extension — MAST.XPG, MAST.XEF and MAST.TXT all happen);
//    * merge several source files into one Project;
//    * run off the UI thread and report progress through the EventBus;
//    * never leave a half-built project visible: the new Project is swapped in
//      only once parsing and analysis have both succeeded.
// =============================================================================
#pragma once

#include "../core/EventBus.hpp"
#include "../core/Result.hpp"
#include "ProjectAnalyzer.hpp"
#include "ProjectParser.hpp"

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace importer {

// ---- events published on the bus -------------------------------------------
struct ImportStarted  { std::string path; };
struct ImportProgress { std::string stage; float fraction; };
// One per source file, as it is parsed.
struct ImportParsed   { std::string path; std::size_t variables; std::size_t sections; double ms; };
// Once, at the end, carrying the finished model. Subscribers swap this in as
// the current project; nothing observes a partially built one.
struct ImportFinished {
    std::string                               path;
    std::shared_ptr<const domain::Project>    project;
    std::shared_ptr<AnalysisReport>           report;
    std::size_t                               variables{0};
    std::size_t                               sections{0};
    double                                    ms{0.0};
};
struct ImportFailed    { std::string path; std::string message; };
struct AnalysisFinished{ std::size_t findings; double ms; };

struct ImportResult {
    std::shared_ptr<domain::Project> project;
    AnalysisReport                   report;
    std::vector<ParseDiagnostic>     diagnostics;
    SourceFormat                     format{SourceFormat::Unknown};
    double                           totalMilliseconds{0.0};
};

class ProjectImporter {
public:
    explicit ProjectImporter(core::EventBus& bus);
    ~ProjectImporter();

    ProjectImporter(const ProjectImporter&)            = delete;
    ProjectImporter& operator=(const ProjectImporter&) = delete;

    // Registration is open: a customer-specific format is added without touching
    // the importer.
    void registerParser(ParserPtr p);

    [[nodiscard]] static SourceFormat sniff(std::string_view head, std::string_view path) noexcept;

    // Synchronous: used by the CLI and by tests.
    core::Result<ImportResult> importFile(const std::string& path,
                                          const ParseOptions& = {},
                                          const AnalysisOptions& = {});

    // A project assembled from several exports (.XPG + .XHW + .XDB).
    core::Result<ImportResult> importFiles(const std::vector<std::string>& paths,
                                           const ParseOptions& = {},
                                           const AnalysisOptions& = {});

    // What the UI calls.
    //
    // THIS NOW RUNS ON THE CALLING THREAD BY DEFAULT, and the name is kept only
    // so call sites do not all have to change. The reason is a measurement, not
    // a preference: parsing and analysing the reference project takes about 5 ms
    // and a six-megabyte one 93 ms. A worker thread buys nothing at that size
    // and costs a whole class of bugs - the model is a shared_ptr graph that the
    // render loop reads every frame, and handing it across a thread boundary
    // produced a heap use-after-free that AddressSanitizer catches in roughly
    // one run in four.
    //
    // The threaded path is still here for the plant-sized project that does not
    // exist yet. When it does, the handoff has to be redesigned - the model made
    // immutable-after-publish and transferred under a lock - not simply switched
    // back on.
    void importAsync(std::string path, ParseOptions = {}, AnalysisOptions = {});
    void importAsync(std::vector<std::string> paths, ParseOptions = {}, AnalysisOptions = {});

    // Opt in explicitly, knowing the above.
    void setThreaded(bool threaded) noexcept { threaded_ = threaded; }
    [[nodiscard]] bool threaded() const noexcept { return threaded_; }
    void cancel();
    [[nodiscard]] bool busy() const noexcept;

private:
    core::EventBus&        bus_;
    std::vector<ParserPtr> parsers_;
    ProjectAnalyzer        analyzer_;
    std::future<void>      worker_;
    std::atomic_bool       cancel_{false};
    std::atomic_bool       busy_{false};
    bool                   threaded_{false};
};

} // namespace importer
