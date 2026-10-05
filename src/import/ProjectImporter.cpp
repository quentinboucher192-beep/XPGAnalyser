#include "ProjectImporter.hpp"
#include "XmlReader.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>

namespace importer {

ProjectImporter::ProjectImporter(core::EventBus& bus) : bus_(bus) {
    registerParser(std::make_unique<XpgParser>());
    registerParser(std::make_unique<XdbParser>());
    registerParser(std::make_unique<XhwParser>());
}

ProjectImporter::~ProjectImporter() {
    cancel();
    if (worker_.valid()) worker_.wait();
}

void ProjectImporter::registerParser(ParserPtr p) { parsers_.push_back(std::move(p)); }

SourceFormat ProjectImporter::sniff(std::string_view head, std::string_view path) noexcept {
    if (head.find("PGMExchangeFile") != std::string_view::npos) return SourceFormat::Xpg;
    if (head.find("HWExchangeFile")  != std::string_view::npos) return SourceFormat::Xhw;
    if (head.find("VariableDeclarationFile") != std::string_view::npos) return SourceFormat::Xdb;
    if (head.find("PLCExchangeFile") != std::string_view::npos) return SourceFormat::Xef;
    if (head.size() >= 2 && head[0] == 'P' && head[1] == 'K')   return SourceFormat::Zef;  // zip

    // Extension is the last resort, not the first.
    auto ext = std::filesystem::path(std::string(path)).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".xpg" || ext == ".mast") return SourceFormat::Xpg;
    if (ext == ".xhw") return SourceFormat::Xhw;
    if (ext == ".xdb") return SourceFormat::Xdb;
    if (ext == ".xef") return SourceFormat::Xef;
    if (ext == ".zef") return SourceFormat::Zef;
    if (ext == ".stu") return SourceFormat::Stu;
    return SourceFormat::Unknown;
}

core::Result<ImportResult> ProjectImporter::importFile(const std::string& path,
                                                       const ParseOptions& po,
                                                       const AnalysisOptions& ao) {
    return importFiles(std::vector<std::string>{path}, po, ao);
}

core::Result<ImportResult> ProjectImporter::importFiles(const std::vector<std::string>& paths,
                                                        const ParseOptions& po,
                                                        const AnalysisOptions& ao) {
    const auto t0 = std::chrono::steady_clock::now();
    ImportResult result;
    result.project = std::make_shared<domain::Project>();

    for (const auto& path : paths) {
        bus_.publish(ImportStarted{path});

        auto buffer = readFile(path, po.maxBytes);
        if (!buffer) {
            bus_.publish(ImportFailed{path, buffer.error().message()});
            return core::Err<core::Error>(buffer.error());
        }
        const std::string_view view(*buffer);
        const auto head = view.substr(0, std::min<std::size_t>(view.size(), 1024));
        const auto fmt  = sniff(head, path);

        if (fmt == SourceFormat::Stu) {
            const auto msg = std::string(
                ".STU is Control Expert's binary working file, not an exchange format. "
                "Export the project as .XEF (or the section as .XPG) and import that.");
            bus_.publish(ImportFailed{path, msg});
            return core::fail(core::ErrorCode::XmlUnexpectedRoot, msg, path);
        }

        auto it = std::find_if(parsers_.begin(), parsers_.end(),
                               [&](const ParserPtr& p) { return p->canParse(head); });
        if (it == parsers_.end()) {
            const auto msg = std::string("no parser recognises this file");
            bus_.publish(ImportFailed{path, msg});
            return core::fail(core::ErrorCode::XmlUnexpectedRoot, msg, path);
        }

        result.project->header.sourceFile = path;
        auto outcome = (*it)->parse(view, *result.project, po,
                                    [this](const ParseProgress& pr) {
                                        bus_.publish(ImportProgress{pr.stage, pr.fraction});
                                    });
        if (!outcome) {
            bus_.publish(ImportFailed{path, outcome.error().message()});
            return core::Err<core::Error>(outcome.error());
        }
        result.format = (*it)->format();
        result.diagnostics.insert(result.diagnostics.end(),
                                  outcome->diagnostics.begin(), outcome->diagnostics.end());
        bus_.publish(ImportParsed{path,
                                  result.project->variables.size(),
                                  result.project->sections.size(),
                                  outcome->milliseconds});
    }

    auto report = analyzer_.analyze(*result.project, ao,
                                    [this](float f, std::string_view s) {
                                        bus_.publish(ImportProgress{std::string(s), f});
                                    },
                                    &cancel_);
    if (!report) return core::Err<core::Error>(report.error());
    result.report = std::move(*report);
    bus_.publish(AnalysisFinished{result.report.findings.size(), result.report.analysisMilliseconds});

    result.totalMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    // Published last, with the finished model attached: a subscriber that swaps
    // this in can never see a half-parsed project.
    auto report_ptr = std::make_shared<AnalysisReport>(result.report);
    bus_.publish(ImportFinished{paths.empty() ? std::string{} : paths.back(),
                                result.project,   // shared_ptr<T> -> shared_ptr<const T>
                                std::move(report_ptr),
                                result.project->variables.size(),
                                result.project->sections.size(),
                                result.totalMilliseconds});
    return result;
}

void ProjectImporter::importAsync(std::string path, ParseOptions po, AnalysisOptions ao) {
    importAsync(std::vector<std::string>{std::move(path)}, po, ao);
}

void ProjectImporter::importAsync(std::vector<std::string> paths, ParseOptions po, AnalysisOptions ao) {
    if (paths.empty()) return;
    if (busy_.exchange(true)) return;              // one import at a time
    cancel_ = false;

    if (!threaded_) {
        // Synchronous. The events still go through the bus, so every subscriber
        // behaves identically; they are simply delivered on the next drain()
        // instead of on some later frame.
        (void)importFiles(paths, po, ao);
        busy_ = false;
        return;
    }

    worker_ = std::async(std::launch::async, [this, paths = std::move(paths), po, ao] {
        (void)importFiles(paths, po, ao);          // all reporting goes through the bus
        busy_ = false;
    });
}

void ProjectImporter::cancel() { cancel_ = true; }
bool ProjectImporter::busy() const noexcept { return busy_.load(); }

} // namespace importer
