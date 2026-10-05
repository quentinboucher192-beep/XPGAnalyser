// =============================================================================
//  app/SimulationHostDebug.cpp - lot API 8 : debogage et modification en ligne
// -----------------------------------------------------------------------------
//  L'interface de SimulationHost.hpp (bloc « Lot API 8 »), remplie par le
//  moteur (src/sim : Runtime.cpp, RuntimeDebug.cpp, Interpreter.cpp).
//
//  LA MODIFICATION EN LIGNE. Le lot 7 voyait chaque commande - une vue de
//  l'IHM deplacee, un Enregistrer - comme un programme change, et une
//  simulation en pause repartait du cycle 0 au lancement suivant. Maintenant :
//    * seule compte l'EMPREINTE du programme (sim::Runtime::programFingerprint),
//      recalculee quand la revision des commandes change ;
//    * en marche ou en pause, entre deux cycles, le nouveau programme est
//      prepare et reprend les valeurs de l'ancien (Runtime::adoptStateFrom) :
//      le cycle, le temps simule, les forcages, les courbes, les points d'arret
//      continuent ; generation() change (les ecrans relisent leurs cases) ;
//    * une preparation qui echoue (une section qui ne se lit plus) garde
//      l'ancien programme, met en pause et le dit ; corrige, le code repart
//      (et la simulation aussi, si c'est l'echec qui l'avait mise en pause) ;
//    * arretee, le lancement suivant prepare le nouveau code (valeurs
//      initiales), comme avant - les forcages restent.
//  L'editeur de code fait une commande par touche : la modification attend
//  que le programme ne bouge plus depuis onlineDelay_ (0,75 s de tick) ; un
//  geste (Simuler, Un cycle, la section suivante) la fait tout de suite.
//
//  LES POINTS D'ARRET vivent ici (la liste de l'utilisateur, ses numeros) et
//  dans le runtime (ce qu'il en a compris : la ligne effective, la note, les
//  passages). Chaque runtime prepare les recoit (pushBreakpoints) : ils
//  survivent a la modification en ligne, retrouves par section et ligne.
// =============================================================================
#include "SimulationHost.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace app {
namespace {

// Les milliers separes d'une espace fine insecable, comme l'onglet Simulation.
std::string grouped(std::uint64_t n) {
    const auto digits = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += digits[i];
    }
    return out;
}

std::string counted(std::size_t n, const char* one, const char* many) {
    return grouped(n) + " " + (n >= 2 ? many : one);
}

std::string lowerText(std::string_view s) {
    std::string out;
    for (const char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// « invalid argument: line 12: expected ... [Init] » : 12.
int lineInMessage(const std::string& m) {
    for (std::size_t at = m.find("line "); at != std::string::npos; at = m.find("line ", at + 1)) {
        std::size_t i = at + 5;
        int line = 0;
        while (i < m.size() && std::isdigit(static_cast<unsigned char>(m[i]))) line = line * 10 + (m[i++] - '0');
        if (i > at + 5 && i < m.size() && m[i] == ':') return line;
    }
    return 0;
}

} // namespace

// ---------------------------------------------------- l'empreinte, le rythme ---
void SimulationHost::setProgramRevision(std::uint64_t revision) noexcept {
    if (rawSeen_ && revision == rawRevision_) return;
    rawSeen_ = true;
    rawRevision_ = revision;
    if (!project_) return;                        // rien de prepare : attach() la prendra
    const auto print = sim::Runtime::programFingerprint(*project_);
    if (print == programPrint_) return;           // l'IHM, une version, un commentaire...
    programPrint_ = print;
    ++programRevision_;
    changeSeenAt_ = clock_;
}

// La modification en ligne attend : un cycle commence au pas a pas doit finir
// d'abord, et un code qui n'a pas pu se preparer ne se reessaie pas tant qu'il
// n'a pas change.
bool SimulationHost::changeBlocked() const noexcept {
    return !runtime_ || runtime_->scanInProgress() || (failedChange_ && failedPrint_ == programPrint_);
}

void SimulationHost::syncProgram() {
    if (!runtime_ || !project_ || !stale() || runtime_->scanInProgress()) return;
    if (state_ == State::Running || state_ == State::Paused) {
        if (!changeBlocked()) (void)applyOnlineChange();
        return;
    }
    const auto project = project_;
    (void)attach(project);                        // arretee : le nouveau code, valeurs initiales
}

bool SimulationHost::applyOnlineChange() {
    if (!runtime_ || !project_) return false;
    const auto atScan = static_cast<std::uint64_t>(runtime_->scanCount());
    auto fresh = std::make_shared<sim::Runtime>(project_);
    fresh->setContinueOnUnknownCalls(continueOnUnknown_);
    const auto ready = fresh->prepare();
    std::vector<sim::Diagnostic> errors;
    if (ready) errors = fresh->newPreparationErrors(*runtime_);

    SimOnlineChange change;
    change.atScan = atScan;
    const std::string when = "Le programme a chang\xC3\xA9 au cycle " + grouped(atScan);
    if (!ready || !errors.empty()) {
        // L'ancien programme reste : il tourne, lui. En pause, pour que ca se voie.
        change.failed = true;
        if (!errors.empty()) {
            change.section = errors.front().section;
            change.line = lineInMessage(errors.front().message);
            change.detail = errors.front().message;
        } else {
            change.detail = ready.error().message();
        }
        const bool wasRunning = state_ == State::Running;
        std::string where;
        if (!change.section.empty())
            where = " (section " + change.section + (change.line > 0 ? ", ligne " + std::to_string(change.line) : std::string{}) + ")";
        change.summary = when + " mais ne se pr\xC3\xA9pare pas" + where + " : la simulation garde l'ancien code et "
                         + (wasRunning ? "se met en pause" : "reste en pause");
        failedChange_ = true;
        failedPrint_ = programPrint_;
        if (wasRunning) {
            state_ = State::Paused;
            resumeAfterFix_ = true;
            accumulator_ = 0.0;
        }
        lastOnlineChange_ = std::move(change);
        onlineChanged->emit();
        changed->emit();
        return false;
    }

    pullHits();
    const auto adopted = fresh->adoptStateFrom(*runtime_);
    runtime_ = std::move(fresh);
    preparedRevision_ = programRevision_;
    failedChange_ = false;
    ++generation_;
    pushBreakpoints();
    // Mise en pause par un echec, corrige depuis : elle repart.
    if (state_ == State::Paused && resumeAfterFix_) state_ = State::Running;
    resumeAfterFix_ = false;
    change.kept = adopted.kept;
    change.added = adopted.added;
    change.dropped = adopted.dropped;
    change.summary = when
                     + (state_ == State::Running ? " : la simulation continue avec le nouveau code ; "
                                                 : " : en pause, la simulation reprendra avec le nouveau code ; ")
                     + counted(adopted.kept, "valeur gard\xC3\xA9" "e", "valeurs gard\xC3\xA9" "es") + ", "
                     + counted(adopted.added, "nouvelle", "nouvelles") + ", " + counted(adopted.dropped, "disparue", "disparues");
    lastOnlineChange_ = std::move(change);
    onlineChanged->emit();
    changed->emit();
    return true;
}

const std::optional<SimOnlineChange>& SimulationHost::lastOnlineChange() const noexcept { return lastOnlineChange_; }

// ------------------------------------------------------------ points d'arret ---
void SimulationHost::pushBreakpoints() {
    if (!runtime_) return;
    std::vector<sim::Breakpoint> list;
    list.reserve(breakpoints_.size());
    for (const auto& b : breakpoints_) {
        sim::Breakpoint s;
        s.id = b.id;
        s.section = b.section;
        s.line = static_cast<std::uint32_t>(std::max(0, b.line));
        s.condition = b.condition;
        s.enabled = b.enabled;
        s.hits = b.hits;
        list.push_back(std::move(s));
    }
    runtime_->setBreakpoints(std::move(list));
}

// Les passages comptes par le runtime reviennent dans la liste (avant de la lui
// renvoyer, ou de changer de runtime).
void SimulationHost::pullHits() {
    if (!runtime_) return;
    for (const auto& r : runtime_->breakpoints())
        for (auto& b : breakpoints_)
            if (b.id == r.id) b.hits = r.hits;
}

bool SimulationHost::takeBreakHit() {
    if (!runtime_) return false;
    auto hit = runtime_->takeBreakHit();
    if (!hit) return false;
    SimBreakHit out;
    out.id = hit->id;
    out.section = std::move(hit->section);
    out.line = static_cast<int>(hit->line);
    out.scan = hit->scan;
    out.values = std::move(hit->values);
    out.stack = std::move(hit->stack);
    lastBreakHit_ = std::move(out);
    return true;
}

std::uint32_t SimulationHost::addBreakpoint(const std::string& section, int line, const std::string& condition) {
    pullHits();
    SimBreakpoint bp;
    bp.id = nextBreakpointId_++;
    bp.section = section;
    bp.line = line;
    bp.condition = condition;
    breakpoints_.push_back(bp);
    pushBreakpoints();
    // UN POINT D'ARRET PAR LIGNE : un autre sur la meme ligne (effective : la
    // ligne 40 sans instruction decalee sur la 43 ou il y en a deja un) le
    // reprend - active, avec la condition demandee s'il y en a une.
    const auto view = breakpoints();
    const auto mine = std::find_if(view.begin(), view.end(), [&bp](const SimBreakpoint& b) { return b.id == bp.id; });
    if (mine != view.end()) {
        for (const auto& other : view) {
            if (other.id == bp.id || other.line != mine->line || lowerText(other.section) != lowerText(mine->section)) continue;
            breakpoints_.erase(std::remove_if(breakpoints_.begin(), breakpoints_.end(), [&bp](const SimBreakpoint& b) { return b.id == bp.id; }),
                               breakpoints_.end());
            for (auto& b : breakpoints_)
                if (b.id == other.id) {
                    b.enabled = true;
                    if (!condition.empty()) b.condition = condition;
                }
            pushBreakpoints();
            changed->emit();
            return other.id;
        }
    }
    changed->emit();
    return bp.id;
}

bool SimulationHost::removeBreakpoint(std::uint32_t id) {
    pullHits();
    const auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(), [id](const SimBreakpoint& b) { return b.id == id; });
    if (it == breakpoints_.end()) return false;
    breakpoints_.erase(it);
    pushBreakpoints();
    changed->emit();
    return true;
}

bool SimulationHost::setBreakpointEnabled(std::uint32_t id, bool enabled) {
    pullHits();
    for (auto& b : breakpoints_)
        if (b.id == id) {
            b.enabled = enabled;
            pushBreakpoints();
            changed->emit();
            return true;
        }
    return false;
}

bool SimulationHost::setBreakpointCondition(std::uint32_t id, const std::string& condition) {
    pullHits();
    for (auto& b : breakpoints_)
        if (b.id == id) {
            b.condition = condition;
            pushBreakpoints();
            changed->emit();
            return true;
        }
    return false;
}

void SimulationHost::clearBreakpoints() {
    if (breakpoints_.empty()) return;
    breakpoints_.clear();
    pushBreakpoints();
    changed->emit();
}

// La liste, avec ce que le runtime en a compris : la ligne ou il s'arrete
// vraiment (decalee), sa note, ses passages.
std::vector<SimBreakpoint> SimulationHost::breakpoints() const {
    auto out = breakpoints_;
    for (auto& b : out) {
        if (!runtime_) {
            b.note = "en attente : la simulation n'est pas pr\xC3\xA9par\xC3\xA9" "e";
            continue;
        }
        for (const auto& r : runtime_->breakpoints()) {
            if (r.id != b.id) continue;
            b.hits = r.hits;
            b.note = r.note;
            if (r.effectiveLine != 0) b.line = static_cast<int>(r.effectiveLine);
        }
    }
    return out;
}

const std::optional<SimBreakHit>& SimulationHost::lastBreakHit() const noexcept { return lastBreakHit_; }

// ------------------------------------------------------------- le pas a pas ---
// La prochaine entree de MAST, puis la pause. Le premier pas commence un cycle
// (l'horloge avance), le dernier le finit (courbes, compteur de cycles).
void SimulationHost::stepSection() {
    if (!runtime_ || state_ == State::Halted) return;
    syncProgram();
    if (!runtime_ || state_ == State::Halted) return;
    resumeAfterFix_ = false;
    lastBreakHit_.reset();
    state_ = State::Paused;
    const auto t0 = std::chrono::steady_clock::now();
    const auto report = runtime_->stepEntry(scanIntervalMs_);
    partialMicros_ += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    if (report.completed) {
        lastScanMicros_ = partialMicros_;
        partialMicros_ = 0;
        lastStatements_ = report.statements;
    }
    applyReport(report);
    if (takeBreakHit()) breakHit->emit();
    changed->emit();
}

std::string SimulationHost::nextSection() const { return runtime_ ? runtime_->nextEntry() : std::string{}; }

bool SimulationHost::scanInProgress() const noexcept { return runtime_ && runtime_->scanInProgress(); }

std::vector<std::string> SimulationHost::taskEntries() const {
    return runtime_ ? runtime_->entries() : std::vector<std::string>{};
}

std::size_t SimulationHost::nextEntryIndex() const noexcept { return runtime_ ? runtime_->nextEntryIndex() : 0; }

// ------------------------------------------------- ou passe le temps, qui ecrit ---
std::vector<SimSectionTime> SimulationHost::sectionTimes() const {
    std::vector<SimSectionTime> out;
    if (!runtime_) return out;
    for (auto& t : runtime_->sectionTimes()) out.push_back(SimSectionTime{std::move(t.entry), std::move(t.section), t.statements, t.micros, t.active, std::move(t.condition)});
    return out;
}

bool SimulationHost::lastWrite(const std::string& path, SimLastWrite& out) const {
    if (!runtime_) return false;
    sim::WriteSite site;
    if (!runtime_->lastWrite(path, site)) return false;
    out.section = std::move(site.section);
    out.line = static_cast<int>(site.line);
    out.scan = site.scan;
    return true;
}

// Le programme dirait : la case sous le forcage.
bool SimulationHost::unforcedValue(const std::string& path, std::string& out) const {
    if (!runtime_) return false;
    sim::Value v;
    if (!runtime_->programValue(path, v)) return false;
    out = v.display();
    return true;
}

} // namespace app
