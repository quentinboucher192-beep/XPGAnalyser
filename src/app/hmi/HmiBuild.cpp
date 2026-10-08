#include "HmiBuild.hpp"

#include "../../domain/ProjectModel.hpp"

#include <algorithm>

namespace app {

namespace pl = hmi::pipeline;
using Clock = std::chrono::steady_clock;

// ================================================================ l'apparence ==
HmiStateLook hmiStateLook(pl::State s, int warnings) {
    switch (s) {
        case pl::State::UpToDate:
        case pl::State::Generated:
        case pl::State::Compiled:
            if (warnings > 0) return {"\xE2\x9A\xA0", ui::Tone::Warning};          // ⚠
            return {"\xE2\x9C\x93", ui::Tone::Ok};                                  // ✓
        case pl::State::Modified:
        case pl::State::GenerationRequired:
        case pl::State::CompilationRequired: return {"\xE2\x97\x8F", ui::Tone::Warning};   // ●
        case pl::State::NotGenerated: return {"\xE2\x97\x8C", ui::Tone::Muted};            // ◌
        case pl::State::Generating: return {"\xE2\x9A\x99", ui::Tone::Info};                // ⚙
        case pl::State::Compiling: return {"\xE2\x97\x94", ui::Tone::Accent};               // ◔
        case pl::State::GenerationFailed:
        case pl::State::CompilationFailed: return {"\xE2\x9C\x95", ui::Tone::Error};        // ✕
        case pl::State::InvalidDependency: return {"\xE2\x9B\x93", ui::Tone::Warning};      // ⛓
        case pl::State::Obsolete: return {"\xE2\x8F\xB1", ui::Tone::Info};                  // ⏱
    }
    return {"?", ui::Tone::Muted};
}

int hmiStateRank(pl::State s, int warnings) {
    switch (s) {
        case pl::State::GenerationFailed:
        case pl::State::CompilationFailed: return 8;
        case pl::State::InvalidDependency: return 7;
        case pl::State::Generating:
        case pl::State::Compiling: return 6;
        case pl::State::Modified:
        case pl::State::GenerationRequired:
        case pl::State::CompilationRequired: return 5;
        case pl::State::Obsolete: return 4;
        case pl::State::NotGenerated: return 3;
        case pl::State::UpToDate:
        case pl::State::Generated:
        case pl::State::Compiled: return warnings > 0 ? 1 : 0;
    }
    return 0;
}

// ================================================================ l'etat ======
const pl::Shown* HmiBuildStatus::of(std::string_view key) const {
    const auto it = shown.find(std::string(key));
    return it == shown.end() ? nullptr : &it->second;
}
std::string HmiBuildStatus::keyOfView(hmi::Id view) const {
    const auto it = viewKeys.find(view);
    return it == viewKeys.end() ? std::string{} : it->second;
}
const pl::Shown* HmiBuildStatus::ofView(hmi::Id view) const {
    const auto k = keyOfView(view);
    return k.empty() ? nullptr : of(k);
}
const HmiBuildStatus::Summary* HmiBuildStatus::folder(std::string_view path) const {
    const auto it = folders.find(std::string(path));
    return it == folders.end() ? nullptr : &it->second;
}

namespace {
std::string count(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// Les nombres d'un dossier, par famille d'etat (le texte de son infobulle).
struct Tally {
    std::size_t elements{0}, modified{0}, notGenerated{0}, failed{0}, invalid{0}, obsolete{0}, running{0}, warned{0};
    int         worst{-1};
    pl::State   state{pl::State::UpToDate};
    int         warnings{0};
    void add(const pl::Shown& s) {
        ++elements;
        switch (s.state) {
            case pl::State::Modified: case pl::State::GenerationRequired: case pl::State::CompilationRequired: ++modified; break;
            case pl::State::NotGenerated: ++notGenerated; break;
            case pl::State::GenerationFailed: case pl::State::CompilationFailed: ++failed; break;
            case pl::State::InvalidDependency: ++invalid; break;
            case pl::State::Obsolete: ++obsolete; break;
            case pl::State::Generating: case pl::State::Compiling: ++running; break;
            default: if (s.warnings > 0) ++warned; break;
        }
        const int r = hmiStateRank(s.state, s.warnings);
        if (r > worst) { worst = r; state = s.state; }
        warnings += s.warnings;
    }
    [[nodiscard]] std::size_t pending() const { return modified + notGenerated + failed + invalid + obsolete + running; }
    [[nodiscard]] std::string text() const {
        std::string out;
        const auto part = [&out](std::size_t n, const char* one, const char* many) { if (n) out += (out.empty() ? "" : ", ") + count(n, one, many); };
        part(failed, "en erreur", "en erreur");
        part(invalid, "d\xC3\xA9pendance invalide", "d\xC3\xA9pendances invalides");
        part(modified, "modifi\xC3\xA9 (build requis)", "modifi\xC3\xA9s (build requis)");
        part(obsolete, "obsol\xC3\xA8te", "obsol\xC3\xA8tes");
        part(notGenerated, "jamais g\xC3\xA9n\xC3\xA9r\xC3\xA9", "jamais g\xC3\xA9n\xC3\xA9r\xC3\xA9s");
        part(running, "en cours", "en cours");
        if (out.empty()) out = count(elements, "\xC3\xA9l\xC3\xA9ment \xC3\xA0 jour", "\xC3\xA9l\xC3\xA9ments \xC3\xA0 jour");
        else out = count(elements, "\xC3\xA9l\xC3\xA9ment", "\xC3\xA9l\xC3\xA9ments") + " : " + out;
        if (warnings > 0) out += " \xC2\xB7 " + count(static_cast<std::size_t>(warnings), "avertissement", "avertissements");
        return out;
    }
};
} // namespace

std::string HmiBuildStatus::headline() const {
    if (totals.elements == 0) return {};
    if (upToDate()) return "Projet \xC3\xA0 jour \xC2\xB7 " + count(totals.elements, "\xC3\xA9l\xC3\xA9ment", "\xC3\xA9l\xC3\xA9ments");
    std::string out;
    const auto part = [&out](std::size_t n, const char* one, const char* many) { if (n) out += (out.empty() ? "" : " \xC2\xB7 ") + count(n, one, many); };
    part(totals.failed, "en erreur", "en erreur");
    part(totals.invalid, "d\xC3\xA9pendance invalide", "d\xC3\xA9pendances invalides");
    part(totals.modified, "modifi\xC3\xA9", "modifi\xC3\xA9s");
    part(totals.obsolete, "obsol\xC3\xA8te", "obsol\xC3\xA8tes");
    part(totals.notGenerated, "non g\xC3\xA9n\xC3\xA9r\xC3\xA9", "non g\xC3\xA9n\xC3\xA9r\xC3\xA9s");
    part(totals.required, "\xC3\xA0 r\xC3\xA9g\xC3\xA9n\xC3\xA9rer", "\xC3\xA0 r\xC3\xA9g\xC3\xA9n\xC3\xA9rer");
    part(totals.toCompile, "\xC3\xA0 compiler", "\xC3\xA0 compiler");
    if (!analysis.deleted.empty()) part(analysis.deleted.size(), "supprim\xC3\xA9", "supprim\xC3\xA9s");
    return out.empty() ? std::string("Build requis") : out;
}

std::shared_ptr<HmiBuildStatus> hmiBuildStatusOf(pl::Analysis analysis, pl::Cache cache) {
    auto st = std::make_shared<HmiBuildStatus>();
    st->analysis = std::move(analysis);
    st->cache = std::move(cache);
    std::unordered_map<std::string, Tally> folders;
    for (const auto& e : st->analysis.elements) {
        const auto s = pl::shown(st->analysis, st->cache, e.key);
        st->shown[e.key] = s;
        if ((e.kind == pl::ElementKind::View || e.kind == pl::ElementKind::Popup || e.kind == pl::ElementKind::Symbol || e.kind == pl::ElementKind::ViewTemplate)
            && e.id != hmi::kNoId && e.key.rfind("paquet-modele:", 0) != 0)
            st->viewKeys[e.id] = e.key;
        // l'element compte pour son chemin et pour chacun de ses dossiers
        folders[e.path].add(s);
        for (std::size_t at = e.path.find('/'); at != std::string::npos; at = e.path.find('/', at + 1)) folders[e.path.substr(0, at)].add(s);
        auto& t = st->totals;
        ++t.elements;
        switch (s.state) {
            case pl::State::UpToDate: case pl::State::Generated: case pl::State::Compiled: ++t.upToDate; break;
            case pl::State::Modified: ++t.modified; ++t.toGenerate; break;
            case pl::State::GenerationRequired: ++t.required; ++t.toGenerate; break;
            case pl::State::CompilationRequired: ++t.toCompile; break;
            case pl::State::NotGenerated: ++t.notGenerated; ++t.toGenerate; break;
            case pl::State::GenerationFailed: case pl::State::CompilationFailed: ++t.failed; break;
            case pl::State::InvalidDependency: ++t.invalid; break;
            case pl::State::Obsolete: ++t.obsolete; ++t.toGenerate; break;
            default: break;
        }
        t.errors += s.errors;
        t.warnings += s.warnings;
    }
    for (const auto& [path, t] : folders) {
        HmiBuildStatus::Summary s;
        s.state = t.state;
        s.warnings = t.worst <= 1 ? t.warnings : 0;
        s.elements = t.elements;
        s.pending = t.pending();
        s.failed = t.failed;
        s.tip = t.text();
        st->folders.emplace(path, std::move(s));
    }
    return st;
}

// ================================================================ l'API =======
pl::ApiInfo hmiApiInfo(const domain::Project* plc) {
    pl::ApiInfo out;
    if (!plc) return out;
    std::size_t globals = 0;
    for (const auto& v : plc->variables) {
        if (v.scope != domain::VariableScope::Global) continue;
        ++globals;
        out.variables.push_back({std::string(plc->strings.text(v.name)), std::string(plc->strings.text(v.type.name)), v.address.raw});
    }
    for (const auto& dt : plc->derivedTypes) {
        std::string def;
        for (const auto f : dt.fields)
            if (f < plc->variables.size())
                def += std::string(plc->strings.text(plc->variables[f].name)) + " : " + std::string(plc->strings.text(plc->variables[f].type.name)) + "; ";
        out.types.push_back({std::string(plc->strings.text(dt.name)), def});
    }
    out.config = "variables globales : " + std::to_string(globals) + " ; types d\xC3\xA9riv\xC3\xA9s : " + std::to_string(plc->derivedTypes.size());
    return out;
}

// ================================================================ le gestionnaire
HmiBuildManager::HmiBuildManager(SetupFn setup) : setup_(std::move(setup)) { dirtyAt_ = Clock::now() - std::chrono::seconds(1); }

HmiBuildManager::~HmiBuildManager() {
    cancel_ = true;
    join();
}

void HmiBuildManager::join() {
    if (worker_.joinable()) worker_.join();
}

void HmiBuildManager::invalidate() {
    dirty_ = true;
    dirtyAt_ = Clock::now();
}

void HmiBuildManager::analyseNow() {
    dirty_ = true;
    dirtyAt_ = Clock::now() - std::chrono::seconds(1);
}

void HmiBuildManager::cancel() {
    if (building_) cancel_ = true;
}

pl::Progress HmiBuildManager::progress() const {
    std::lock_guard<std::mutex> g(mutex_);
    return progress_;
}

double HmiBuildManager::elapsed() const {
    const auto end = building_ ? Clock::now() : endedAt_;
    return std::max(0.0, std::chrono::duration<double>(end - startedAt_).count());
}

bool HmiBuildManager::start(const pl::Request& request, std::string* why) {
    if (building_) {
        if (why) *why = "un build est d\xC3\xA9j\xC3\xA0 en cours";
        return false;
    }
    if (busy()) {   // une analyse : elle finit (quelques millisecondes), son etat est garde
        join();
        (void)collectDone();   // pas poll() : il relancerait une analyse sur le fil qu'on va prendre
    }
    auto s = setup_ ? setup_() : std::nullopt;
    if (!s || !s->project) {
        if (why) *why = "aucun projet IHM ouvert";
        return false;
    }
    request_ = request;
    building_ = true;
    dirty_ = false;   // le build rend son etat ; une modification pendant le build le refera
    launch(std::move(*s), request);
    return true;
}

void HmiBuildManager::launch(HmiBuildSetup setup, std::optional<pl::Request> request) {
    // Un seul fil a la fois : un std::thread encore joignable remplace serait std::terminate.
    if (worker_.joinable()) {
        join();
        (void)collectDone();
    }
    cancel_ = false;
    finishedFlag_ = false;
    {
        std::lock_guard<std::mutex> g(mutex_);
        progress_ = pl::Progress{};
        progressSeen_ = false;
        done_.reset();
    }
    if (request) startedAt_ = Clock::now();
    const std::string projectBuild = setup.projectFolder.empty() || memoryOnly_ ? std::string{} : pl::buildFolderOf(setup.projectFolder);
    std::optional<pl::Cache> mem = memCache_;
    worker_ = std::thread([this, setup = std::move(setup), request, projectBuild, mem]() mutable {
        Done d;
        std::string folder = projectBuild;
        // Le verrou d'abord : un dossier en lecture seule (un projet sur un partage, une cle
        // protegee) ne refuse pas le build - il reste en memoire, et le journal le dit.
        pl::Lock lock;
        std::string readOnlyWhy;
        if (request && !folder.empty()) {
            lock = pl::acquireLock(folder);
            if (!lock.held && lock.owner.rfind("dossier en lecture seule", 0) == 0) {
                readOnlyWhy = lock.owner;
                folder.clear();
                d.readOnly = true;
            }
        }
        pl::Options o;
        o.buildFolder = folder;
        o.projectFolder = setup.projectFolder;
        o.plcHasName = setup.plcHasName;
        o.plcPaths = setup.plcPaths;
        o.commPlan = setup.plan ? &*setup.plan : nullptr;
        pl::Cache memc;
        if (folder.empty()) {
            if (mem) memc = *mem;
            o.cache = &memc;
            // sans dossier : rien n'est ecrit, les artefacts sont « la » tant que le cache le dit
            o.artifacts = [](const std::string&, const std::string&) { return true; };
            o.writeArtifact = [](const std::string&, const std::string&) {};
            o.removeArtifact = [](const std::string&) {};
        }
        const pl::ArtifactCheck artifacts = folder.empty() ? o.artifacts : pl::diskArtifacts(folder);
        std::shared_ptr<HmiBuildStatus> status;
        if (request) {
            if (!folder.empty() && !lock.held) {
                auto rep = std::make_shared<pl::Report>();
                rep->locked = true;
                rep->errors = 1;
                pl::Diagnostic diag;
                diag.severity = pl::Severity::Error;
                diag.code = "E900";
                diag.category = "Build";
                diag.step = "Analyse";
                diag.message = "Un autre build est en cours sur ce projet (" + lock.owner + ") : r\xC3\xA9" "essaie dans un instant.";
                rep->diagnostics.push_back(diag);
                rep->log.push_back({pl::Severity::Error, "Build", diag.message, {}, 0, 0});
                d.report = rep;
            } else {
                const auto onProgress = [this](const pl::Progress& p) {
                    std::lock_guard<std::mutex> g(mutex_);
                    progress_ = p;
                    progressSeen_ = false;
                };
                auto rep = std::make_shared<pl::Report>(pl::run(*setup.project, setup.api, *request, o, onProgress, &cancel_));
                if (!folder.empty()) pl::releaseLock(folder);
                if (!readOnlyWhy.empty())
                    rep->log.insert(rep->log.begin(), pl::LogLine{pl::Severity::Warning, "Cache",
                                                                  "Le dossier du projet refuse l'\xC3\xA9" "criture (" + readOnlyWhy
                                                                      + ") : le build reste en m\xC3\xA9moire, rien n'est \xC3\xA9" "crit sur le disque.",
                                                                  {}, 0, 0});
                if (folder.empty()) d.memCache = rep->cache;
                status = hmiBuildStatusOf(pl::analyse(rep->analysis.elements, rep->cache, artifacts), rep->cache);
                d.report = rep;
            }
        }
        if (!status) {
            const pl::Cache c = folder.empty() ? memc : pl::loadCache(folder);
            status = hmiBuildStatusOf(pl::analyse(pl::collect(*setup.project, setup.api), c, artifacts), c);
        }
        d.status = status;
        {
            std::lock_guard<std::mutex> g(mutex_);
            done_ = std::move(d);
        }
        finishedFlag_ = true;
    });
}

bool HmiBuildManager::poll() {
    bool changed = false;
    if (building_) {
        std::lock_guard<std::mutex> g(mutex_);
        if (!progressSeen_) {
            progressSeen_ = true;
            changed = true;
        }
    }
    if (collectDone()) changed = true;
    if (!busy() && dirty_ && Clock::now() - dirtyAt_ >= std::chrono::milliseconds(300)) {
        dirty_ = false;
        if (auto s = setup_ ? setup_() : std::nullopt; s && s->project) launch(std::move(*s), std::nullopt);
    }
    return changed;
}

bool HmiBuildManager::collectDone() {
    bool changed = false;
    if (finishedFlag_.load()) {
        join();
        std::optional<Done> d;
        {
            std::lock_guard<std::mutex> g(mutex_);
            d = std::move(done_);
            done_.reset();
        }
        finishedFlag_ = false;
        const bool wasBuild = building_;
        building_ = false;
        if (wasBuild) endedAt_ = Clock::now();
        if (d) {
            if (d->readOnly) memoryOnly_ = true;
            if (d->memCache) memCache_ = std::move(d->memCache);
            if (d->status) {
                auto st = std::move(d->status);
                st->generation = status_->generation + 1;
                status_ = std::move(st);
            }
            if (d->report) report_ = d->report;
        }
        statusChanged->emit();
        if (wasBuild && report_) finished->emit(report_);
        changed = true;
    }
    return changed;
}

void HmiBuildManager::wait() {
    join();
    (void)poll();
}

} // namespace app
