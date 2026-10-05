// =============================================================================
//  app/ImportJob.cpp - 1.8.0 : voir ImportJob.hpp
// =============================================================================
#include "ImportJob.hpp"

#include "../import/ProjectAnalyzer.hpp"
#include "../import/ProjectImporter.hpp"
#include "../import/ProjectParser.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace app {

namespace {

// Les chemins de l'interface sont en UTF-8 (std::filesystem::path(std::string) les lirait
// dans la page de code du systeme sous Windows).
std::filesystem::path utf8Path(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

double nowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string leaf(const std::string& path) { return std::filesystem::path(path).filename().string(); }

std::string sizeText(std::uintmax_t bytes) {
    char buf[32];
    if (bytes >= 1024u * 1024u) std::snprintf(buf, sizeof buf, "%.1f Mo", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof buf, "%.0f Ko", static_cast<double>(bytes) / 1024.0);
    std::string s(buf);
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}

bool readAll(const std::string& path, std::string& out) {
    std::ifstream f(utf8Path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::size_t unitsOf(const domain::Project& p) {
    std::size_t n = 0;
    for (const auto& pou : p.pous) n += pou.kind == domain::PouKind::ProgramUnit ? 1u : 0u;
    return n;
}
std::size_t dfbsOf(const domain::Project& p) {
    std::size_t n = 0;
    for (const auto& pou : p.pous) n += pou.kind == domain::PouKind::FunctionBlockType && pou.userDefined ? 1u : 0u;
    return n;
}
std::size_t globalsOf(const domain::Project& p) {
    std::size_t n = 0;
    for (const auto& v : p.variables) n += v.scope == domain::VariableScope::Global ? 1u : 0u;
    return n;
}

} // namespace

ImportJob::ImportJob(Kind kind, std::vector<std::string> paths, std::shared_ptr<const domain::Project> current,
                     std::vector<project::mast::HmiRef> refs, std::string source)
    : kind_(kind), paths_(std::move(paths)), current_(std::move(current)), refs_(std::move(refs)), source_(std::move(source)) {
    std::uintmax_t bytes = 0;
    for (const auto& p : paths_) {
        std::error_code ec;
        const auto s = std::filesystem::file_size(utf8Path(p), ec);
        if (!ec) bytes += s;
    }
    const std::string files = source_ + " \xC2\xB7 " + sizeText(bytes);
    if (kind_ == Kind::Mast) {
        steps_ = {{"Lire le fichier", files},
                  {"Lire le programme (XML)", "sections, unit\xC3\xA9s, blocs, types, variables"},
                  {"Analyser", "r\xC3\xA9" "f\xC3\xA9rences crois\xC3\xA9" "es, contr\xC3\xB4les"},
                  {"Comparer au projet ouvert", "ce qui est gard\xC3\xA9, chang\xC3\xA9, supprim\xC3\xA9, nouveau"}};
        weights_ = {0.05f, 0.60f, 0.20f, 0.15f};
    } else {
        steps_ = {{"Lire le fichier", files},
                  {"Lire la configuration (XML)", "racks, modules, voies"},
                  {"Comparer \xC3\xA0 la configuration actuelle", "modules ajout\xC3\xA9s, retir\xC3\xA9s, chang\xC3\xA9s"},
                  {"Poser dans le projet", "une commande : Ctrl+Z la retire"}};
        weights_ = {0.10f, 0.60f, 0.20f, 0.10f};
    }
    state_.seconds.assign(steps_.size(), -1.0);
}

ImportJob::~ImportJob() {
    cancel();
    wait();
}

void ImportJob::start() {
    started_ = nowSeconds();
    stepStarted_ = started_;
    worker_ = std::thread([this] { run(); });
}

void ImportJob::cancel() { cancel_.store(true); }

void ImportJob::wait() {
    if (worker_.joinable()) worker_.join();
}

ImportJob::State ImportJob::state() const {
    const std::lock_guard<std::mutex> g(lock_);
    State s = state_;
    s.elapsed = (s.done && s.elapsed > 0.0) ? s.elapsed : nowSeconds() - started_;
    return s;
}

void ImportJob::enter(int step, std::string detail) {
    const std::lock_guard<std::mutex> g(lock_);
    state_.step = step;
    if (!detail.empty()) state_.detail = std::move(detail);
    else if (static_cast<std::size_t>(step) < steps_.size()) state_.detail = steps_[static_cast<std::size_t>(step)].title;
    float before = 0.f;
    for (int i = 0; i < step && static_cast<std::size_t>(i) < weights_.size(); ++i) before += weights_[static_cast<std::size_t>(i)];
    state_.fraction = before;
    stepStarted_ = nowSeconds();
}

void ImportJob::leave(int step) {
    const std::lock_guard<std::mutex> g(lock_);
    if (static_cast<std::size_t>(step) < state_.seconds.size()) state_.seconds[static_cast<std::size_t>(step)] = nowSeconds() - stepStarted_;
    float upTo = 0.f;
    for (int i = 0; i <= step && static_cast<std::size_t>(i) < weights_.size(); ++i) upTo += weights_[static_cast<std::size_t>(i)];
    state_.fraction = upTo;
}

void ImportJob::setDetail(std::string detail, float fraction) {
    const std::lock_guard<std::mutex> g(lock_);
    state_.detail = std::move(detail);
    float before = 0.f;
    for (int i = 0; i < state_.step && static_cast<std::size_t>(i) < weights_.size(); ++i) before += weights_[static_cast<std::size_t>(i)];
    const float w = static_cast<std::size_t>(state_.step) < weights_.size() ? weights_[static_cast<std::size_t>(state_.step)] : 0.f;
    state_.fraction = std::max(state_.fraction, before + w * std::clamp(fraction, 0.f, 1.f));
}

void ImportJob::run() {
    const auto fail = [this](std::string why) {
        const std::lock_guard<std::mutex> g(lock_);
        state_.failed = true;
        state_.error = std::move(why);
    };
    const auto cancelled = [this] {
        if (!cancel_.load()) return false;
        const std::lock_guard<std::mutex> g(lock_);
        state_.cancelled = true;
        return true;
    };
    try {
        // ---- 1. lire les fichiers ----
        enter(0, {});
        std::vector<std::string> buffers;
        for (const auto& path : paths_) {
            std::string data;
            if (!readAll(path, data)) {
                fail(leaf(path) + " : introuvable ou illisible");
                done_.store(true, std::memory_order_release);
                return;
            }
            buffers.push_back(std::move(data));
        }
        leave(0);
        if (cancelled()) {
            done_.store(true, std::memory_order_release);
            return;
        }
        // ---- 2. le XML ----
        enter(1, {});
        auto project = std::make_shared<domain::Project>();
        importer::XpgParser xpg;
        importer::XhwParser xhw;
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            const std::string_view view(buffers[i]);
            const auto fmt = importer::ProjectImporter::sniff(view.substr(0, std::min<std::size_t>(view.size(), 1024)), paths_[i]);
            importer::IProjectParser* parser = fmt == importer::SourceFormat::Xpg ? static_cast<importer::IProjectParser*>(&xpg)
                                             : fmt == importer::SourceFormat::Xhw ? static_cast<importer::IProjectParser*>(&xhw)
                                                                                  : nullptr;
            if (!parser) {
                fail(leaf(paths_[i]) + (kind_ == Kind::Mast ? " n'est pas un programme export\xC3\xA9 de Control Expert (.XPG)"
                                                             : " n'est pas une configuration mat\xC3\xA9rielle (.XHW)"));
                done_.store(true, std::memory_order_release);
                return;
            }
            project->header.sourceFile = paths_[i];
            const float share = 1.f / static_cast<float>(buffers.size());
            auto outcome = parser->parse(view, *project, importer::ParseOptions{}, [&, i, share](const importer::ParseProgress& pr) {
                // Sur ce fil : le projet neuf est a lui seul, ses comptes se lisent.
                std::string detail;
                if (kind_ == Kind::Mast) {
                    const auto& secs = project->sections;
                    detail = "Sections : " + std::to_string(secs.size());
                    if (!secs.empty()) detail += " \xC2\xB7 " + std::string(project->strings.text(secs.back().name));
                } else {
                    std::size_t modules = 0;
                    for (const auto& r : project->hardware.racks) modules += r.modules.size();
                    detail = "Racks : " + std::to_string(project->hardware.racks.size()) + " \xC2\xB7 modules : " + std::to_string(modules);
                }
                {
                    const std::lock_guard<std::mutex> g(lock_);
                    state_.sections = project->sections.size();
                    state_.units = unitsOf(*project);
                    state_.dfbs = dfbsOf(*project);
                    state_.ddts = project->derivedTypes.size();
                    state_.variables = globalsOf(*project);
                }
                setDetail(std::move(detail), share * static_cast<float>(i) + share * pr.fraction);
            });
            if (!outcome) {
                fail(leaf(paths_[i]) + " : " + (outcome.error().context.empty() ? outcome.error().message() : outcome.error().context));
                done_.store(true, std::memory_order_release);
                return;
            }
        }
        {
            const std::lock_guard<std::mutex> g(lock_);
            state_.sections = project->sections.size();
            state_.units = unitsOf(*project);
            state_.dfbs = dfbsOf(*project);
            state_.ddts = project->derivedTypes.size();
            state_.variables = globalsOf(*project);
            state_.racks = project->hardware.racks.size();
            std::size_t modules = 0, channels = 0;
            for (const auto& r : project->hardware.racks) {
                modules += r.modules.size();
                for (const auto& m : r.modules) channels += m.channels.size();
            }
            state_.modules = modules;
            state_.channels = channels;
        }
        leave(1);
        if (cancelled()) {
            done_.store(true, std::memory_order_release);
            return;
        }
        if (kind_ == Kind::Mast) {
            if (project->sections.empty() && project->variables.empty()) {
                fail(source_ + " ne donne aucun programme");
                done_.store(true, std::memory_order_release);
                return;
            }
            // ---- 3. l'analyse ----
            std::size_t lines = 0;
            for (const auto& s : project->sections) lines += s.lineCount;
            enter(2, "R\xC3\xA9" "f\xC3\xA9rences crois\xC3\xA9" "es sur " + std::to_string(lines) + " lignes");
            importer::ProjectAnalyzer analyzer;
            auto report = analyzer.analyze(*project, importer::AnalysisOptions{},
                                           [this, lines](float f, std::string_view) {
                                               setDetail("R\xC3\xA9" "f\xC3\xA9rences crois\xC3\xA9" "es sur " + std::to_string(lines) + " lignes", f);
                                           },
                                           &cancel_);
            if (cancelled()) {
                done_.store(true, std::memory_order_release);
                return;
            }
            if (!report) {
                fail("l'analyse : " + report.error().message());
                done_.store(true, std::memory_order_release);
                return;
            }
            leave(2);
            // ---- 4. la comparaison (sur la copie du projet ouvert) ----
            enter(3, current_ ? "Avec le projet ouvert : " + std::to_string(current_->sections.size()) + " sections" : std::string{});
            if (current_) {
                keepPlan = project::mast::makePlan(*current_, *project, refs_, project::mast::Options{true}, source_);
                replacePlan = project::mast::makePlan(*current_, *project, refs_, project::mast::Options{false}, source_);
            }
            leave(3);
        } else {
            // ---- 3. la comparaison des modules ----
            enter(2, {});
            std::set<std::string> before, after;
            const auto key = [](const domain::Module& m) { return std::to_string(m.rack) + "/" + std::to_string(m.slot) + "/" + m.reference; };
            if (current_)
                for (const auto& r : current_->hardware.racks)
                    for (const auto& m : r.modules) before.insert(key(m));
            for (const auto& r : project->hardware.racks)
                for (const auto& m : r.modules) after.insert(key(m));
            std::size_t changes = 0;
            for (const auto& k : before) changes += after.count(k) ? 0u : 1u;
            for (const auto& k : after) changes += before.count(k) ? 0u : 1u;
            hardwareChanges = changes;
            setDetail(changes == 0 ? std::string("Les m\xC3\xAAmes modules qu'avant") : std::to_string(changes) + " module(s) ajout\xC3\xA9(s), retir\xC3\xA9(s) ou chang\xC3\xA9(s)", 1.f);
            leave(2);
            // L'etape 4 (poser dans le projet) : sur le fil de l'interface, apres.
            enter(3, "Poser dans le projet");
            if (project->hardware.racks.empty()) {
                fail(source_ + " : aucun rack, aucun module");
                done_.store(true, std::memory_order_release);
                return;
            }
            leave(3);   // la pose suit tout de suite, sur le fil de l'interface
        }
        imported = std::move(project);
    } catch (const std::exception& e) {
        fail(std::string("import interrompu : ") + e.what());
    } catch (...) {
        fail("import interrompu");
    }
    {
        const std::lock_guard<std::mutex> g(lock_);
        state_.elapsed = nowSeconds() - started_;
        state_.done = true;                      // le temps ecoule s'arrete la
        if (!state_.failed && !state_.cancelled) {
            state_.step = static_cast<int>(steps_.size());
            state_.fraction = 1.f;
        }
    }
    done_.store(true, std::memory_order_release);
}

} // namespace app
