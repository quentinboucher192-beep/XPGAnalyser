#include "SimulationHost.hpp"
#include "../core/CallTrail.hpp"   // 1.10.2 (CR) : la simulation, dans le journal interne

#include <chrono>

namespace app {

    core::Status SimulationHost::attach(std::shared_ptr<const domain::Project> project) {
        if (!project)
            return core::fail(core::ErrorCode::InvalidArgument, "no project to simulate");

        // Already simulating exactly this program: keep it. Rebuilding here is what
        // made leaving the screen for a moment cost the whole run.
        // Lot API 7 : ... tant que le programme n'a pas change depuis (stale()).
        if (runtime_ && project_ == project && preparedRevision_ == programRevision_) {
            // Lot API 8 : une commande de CETTE image (App ne donne la revision
            // qu'a l'image suivante) : l'empreinte la voit - attach() juste apres
            // une modification prend le programme a jour tout de suite.
            const auto print = sim::Runtime::programFingerprint(*project_);
            if (print == programPrint_) return core::ok();
            programPrint_ = print;
            ++programRevision_;
            changeSeenAt_ = clock_;
        }
        // Lot API 8 : en marche ou en pause, le programme change EN LIGNE - entre
        // deux cycles, les valeurs gardees (un cycle commence au pas a pas se
        // finit d'abord ; une preparation qui a echoue n'est pas refaite tant que
        // le code ne change pas).
        if (runtime_ && project_ == project && (state_ == State::Running || state_ == State::Paused)) {
            if (!changeBlocked()) (void)applyOnlineChange();
            return core::ok();
        }

        auto fresh = std::make_shared<sim::Runtime>(project);
        fresh->setContinueOnUnknownCalls(continueOnUnknown_);
        if (auto ready = fresh->prepare(); !ready) return ready;
        // Lot API 8 : le meme projet, arrete : le nouveau code repart des valeurs
        // initiales, mais les forcages restent (comme apres Arreter). Un autre
        // projet : ses points d'arret ne sont pas les notres.
        if (runtime_ && project_ == project) fresh->adoptForcingFrom(*runtime_);
        if (breakpointsOwner_ && breakpointsOwner_ != project.get()) breakpoints_.clear();
        breakpointsOwner_ = project.get();
        for (auto& b : breakpoints_) b.hits = 0;

        runtime_ = std::move(fresh);
        project_ = std::move(project);
        programPrint_ = sim::Runtime::programFingerprint(*project_);   // lot API 8
        failedChange_ = false;
        resumeAfterFix_ = false;
        partialMicros_ = 0;
        lastBreakHit_.reset();
        preparedRevision_ = programRevision_;
        ++generation_;
        state_ = State::Stopped;
        accumulator_ = 0.0;
        haltMessage_.clear();
        lastDiagnostics_.clear();
        pushBreakpoints();
        changed->emit();
        return core::ok();
    }

    void SimulationHost::detach() {
        if (!runtime_ && state_ == State::Stopped) return;
        XPG_TRACE(Sim, "simulation d\xC3\xA9tach\xC3\xA9" "e");   // 1.10.2 (CR)
        runtime_.reset();
        project_.reset();
        ++generation_;
        state_ = State::Stopped;
        accumulator_ = 0.0;
        haltMessage_.clear();
        lastDiagnostics_.clear();
        // Lot API 8 : plus de runtime - plus de passage, de modification en cours.
        lastBreakHit_.reset();
        lastOnlineChange_.reset();
        failedChange_ = false;
        resumeAfterFix_ = false;
        partialMicros_ = 0;
        for (auto& b : breakpoints_) b.hits = 0;
        changed->emit();
    }

    void SimulationHost::setState(State next) {
        if (!runtime_) return;
        if (next == State::Running && state_ == State::Halted) return;   // Stop first
        // 1.10.2 (CR) : le journal interne (dans l'ordre de State).
        static const char* const kState[] = {"arr\xC3\xAAt\xC3\xA9" "e", "en marche", "en pause", "arr\xC3\xAAt\xC3\xA9" "e sur erreur"};
        if (next != state_) XPG_TRACE(Sim, "simulation : %s -> %s", kState[static_cast<int>(state_) & 3], kState[static_cast<int>(next) & 3]);

        resumeAfterFix_ = false;                  // lot API 8 : l'utilisateur decide
        if (next == State::Stopped && state_ != State::Stopped) {
            runtime_->reset();
            haltMessage_.clear();
            lastDiagnostics_.clear();
            accumulator_ = 0.0;
            partialMicros_ = 0;                   // lot API 8 : le cycle commence au pas a pas aussi
            for (auto& b : breakpoints_) b.hits = 0;
        }
        // Lot API 8 : le dernier point d'arret passe s'efface au lancement suivant.
        if (next == State::Running || next == State::Stopped) lastBreakHit_.reset();
        state_ = next;
        changed->emit();
    }

    void SimulationHost::setContinueOnUnknownCalls(bool on) {
        continueOnUnknown_ = on;
        if (runtime_) runtime_->setContinueOnUnknownCalls(on);
        changed->emit();
    }

    void SimulationHost::setScanIntervalMs(std::int64_t ms) {
        scanIntervalMs_ = ms < 1 ? 1 : ms;
    }

    std::uint64_t SimulationHost::scanCount() const noexcept {
        return runtime_ ? runtime_->scanCount() : 0;
    }

    void SimulationHost::step() {
        if (!runtime_ || state_ == State::Halted) return;
        XPG_PORTEE("SimulationHost::step (un cycle)");   // 1.10.2 (CR)
        syncProgram();                            // lot API 8 : le nouveau code d'abord
        if (!runtime_ || state_ == State::Halted) return;
        resumeAfterFix_ = false;
        lastBreakHit_.reset();
        state_ = State::Paused;
        const auto t0 = std::chrono::steady_clock::now();
        // Lot API 8 : un cycle commence section par section se finit ici.
        const auto report = runtime_->step(scanIntervalMs_);
        lastScanMicros_ = partialMicros_ + std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
        partialMicros_ = 0;
        lastStatements_ = report.statements;
        applyReport(report);
        if (takeBreakHit()) breakHit->emit();
        changed->emit();
    }

    void SimulationHost::tick(double deltaSeconds) {
        // Lot API 8 : LA MODIFICATION EN LIGNE, entre deux cycles, une fois le
        // programme stable depuis onlineDelay_ (l'editeur fait une commande par
        // touche : un mot a moitie tape ne se prepare pas).
        clock_ += deltaSeconds;
        if (runtime_ && stale() && (state_ == State::Running || state_ == State::Paused) && !changeBlocked()
            && clock_ - changeSeenAt_ >= onlineDelay_)
            (void)applyOnlineChange();
        if (!runtime_ || state_ != State::Running) return;

        // Lot API 7 : la vitesse. x1 et x10 suivent l'horloge ; "au plus vite"
        // enchaine les cycles tant qu'une image en laisse le temps (12 ms).
        const auto t0 = std::chrono::steady_clock::now();
        const bool fastest = speed_ == 0;
        accumulator_ += deltaSeconds * 1000.0 * (fastest ? 1.0 : static_cast<double>(speed_));
        int scans = 0;
        // Capped: after a long stall - a modal dialog, a slow frame - the
        // simulation catches up gradually instead of running thousands of scans in
        // one frame and freezing the window it was supposed to be reporting to.
        const int cap = fastest ? 5000 : speed_ > 1 ? 20 * speed_ : 20;
        bool hit = false;                         // lot API 8 : un point d'arret passe
        for (;;) {
            if (!fastest && accumulator_ < static_cast<double>(scanIntervalMs_)) break;
            if (scans >= cap) break;
            if (fastest && std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(12)) break;
            if (!fastest) accumulator_ -= static_cast<double>(scanIntervalMs_);
            ++scans;
            const auto s0 = std::chrono::steady_clock::now();
            const auto report = runtime_->step(scanIntervalMs_);
            lastScanMicros_ = partialMicros_ + std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - s0).count();
            partialMicros_ = 0;
            lastStatements_ = report.statements;
            applyReport(report);
            if (state_ == State::Halted) break;
            // Lot API 8 : un point d'arret est passe - le cycle est fini (il va
            // toujours au bout), la simulation se met en pause.
            if (takeBreakHit()) {
                state_ = State::Paused;
                hit = true;
                break;
            }
        }
        if (fastest || hit) accumulator_ = 0.0;
        if (hit) breakHit->emit();
        if (scans > 0) changed->emit();
    }

    void SimulationHost::applyReport(const sim::ScanReport& report) {
        if (!report.diagnostics.empty()) lastDiagnostics_ = report.diagnostics;
        scanned->emit(report);
        if (!report.halted) return;

        haltMessage_.clear();
        for (const auto& d : report.diagnostics)
            if (d.severity == sim::Diagnostic::Severity::Error) {
                haltMessage_ = "Scan " + std::to_string(report.scan) + " stopped - " + d.message;
                break;
            }
        if (haltMessage_.empty())
            haltMessage_ = "Scan " + std::to_string(report.scan) + " stopped";
        state_ = State::Halted;
    }

    std::string_view SimulationHost::stateName() const noexcept {
        switch (state_) {
        case State::Running: return "RUNNING";
        case State::Paused:  return "PAUSED";
        case State::Halted:  return "HALTED";
        case State::Stopped: break;
        }
        return "STOPPED";
    }

    std::string SimulationHost::statusLine() const {
        if (!runtime_) return {};
        return std::string(stateName()) + "  scan " + std::to_string(scanCount());
    }

} // namespace app