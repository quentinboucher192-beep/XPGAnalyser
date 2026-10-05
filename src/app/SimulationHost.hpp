// =============================================================================
//  app/SimulationHost.hpp — the simulation belongs to the application
// -----------------------------------------------------------------------------
//  It used to belong to the simulation screen: the run state was a member of
//  SimulationScreen, and the scan loop was in its Update(). Two consequences,
//  both wrong.
//
//  First, MenuManager::popEntry destroys a screen when you leave it, so going
//  back to the workspace destroyed the run state - and OnExit had a line that
//  forced it to Stopped anyway. Second, only the menu on top of the stack gets
//  Update(), so even without that line the scan simply stopped being driven.
//  The program was not paused; it was gone.
//
//  Nothing about a simulation is a property of a screen. A PLC does not stop
//  because you looked away from it. So the runtime, the state and the pacing
//  live here, App owns one of these, and App::frame ticks it every frame no
//  matter which screen is showing. A screen becomes what it should always have
//  been: a view onto something that exists whether or not you are watching.
//
//  This is also what makes a run indicator on the workspace possible at all -
//  there is now something for it to show.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "../core/Signal.hpp"
#include "../sim/Runtime.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace app {

    // ---- Lot API 8 : le debogage et la modification en ligne (le moteur) ----
    //  L'interface que le Centre de simulation lit. Les ebauches de
    //  SimulationHostDebug.cpp la font compiler ; le moteur la remplit.
    struct SimBreakpoint {
        std::uint32_t id{0};
        std::string   section;          // "SFC_PurgeA", ou "BLOC.Section" (le corps d'un DFB)
        int           line{0};          // 1 = la premiere ligne
        std::string   condition;        // ST facultatif : "Armoires[0].etat = 3"
        bool          enabled{true};
        std::uint64_t hits{0};
        std::string   note;             // "decale a la ligne 43", "condition invalide : ..."
    };
    struct SimBreakHit {
        std::uint32_t id{0};
        std::string   section;
        int           line{0};
        std::uint64_t scan{0};
        std::vector<std::pair<std::string, std::string>> values;   // les variables de la ligne, au passage
        std::vector<std::string> stack;                            // MAST > unite > section > instance de DFB > section du bloc
    };
    struct SimSectionTime {
        std::string   entry;            // l'entree de MAST (une section, une unite)
        std::string   section;
        std::uint64_t statements{0};
        std::int64_t  micros{0};
        bool          active{true};     // 1.10.2 : sa condition d'activation etait vraie
        std::string   condition;        // 1.10.2 : sa condition d'activation (vide : aucune)
    };
    struct SimLastWrite {
        std::string   section;
        int           line{0};
        std::uint64_t scan{0};
    };
    struct SimOnlineChange {
        std::uint64_t atScan{0};
        std::size_t   kept{0}, added{0}, dropped{0};
        bool          failed{false};
        std::string   summary;          // en francais, pret a montrer
        // Lot API 8 (le moteur) : un echec - la section et la ligne qui ne se
        // lisent pas, et le message du moteur (SimulationPane::frenchMessage le traduit).
        std::string   section;
        int           line{0};
        std::string   detail;
    };

    class SimulationHost {
    public:
        enum class State : std::uint8_t { Stopped, Running, Paused, Halted };

        // Prepares a runtime for this project. If one is already prepared for the
        // same project it is kept, untouched, with its scan count and its forcing
        // intact - which is what makes leaving and re-entering the screen a
        // navigation rather than a restart.
        // Lot API 8 : le programme a change depuis (stale()) - en marche ou en
        // pause, la MODIFICATION EN LIGNE (entre deux cycles : les valeurs, le
        // cycle, les forcages restent) ; arretee ou en halte, le nouveau code
        // se prepare depuis les valeurs initiales (les forcages restent).
        core::Status attach(std::shared_ptr<const domain::Project> project);

        // A different project was opened. The old simulation described a program
        // that is no longer loaded, and stale values are worse than none.
        void detach();

        [[nodiscard]] sim::Runtime* runtime()       noexcept { return runtime_.get(); }
        [[nodiscard]] const sim::Runtime* runtime() const noexcept { return runtime_.get(); }
        [[nodiscard]] std::shared_ptr<sim::Runtime> runtimeRef() const noexcept { return runtime_; }
        [[nodiscard]] bool  attached() const noexcept { return runtime_ != nullptr; }
        [[nodiscard]] State state()    const noexcept { return state_; }

        // Stopping resets the runtime; every other transition leaves it alone.
        // Running from Halted is refused: the state after a halt is not trustworthy
        // and pretending otherwise produces a second, more confusing failure.
        void setState(State next);

        // One scan, and leaves the simulation paused. Same path as the run loop, so
        // a halt is produced and reported identically either way.
        void step();

        void                       setScanIntervalMs(std::int64_t ms);
        [[nodiscard]] std::int64_t scanIntervalMs() const noexcept { return scanIntervalMs_; }

        // Lot API 7 : la vitesse - x1 (le temps reel), x10, ou 0 : au plus vite
        // (autant de cycles qu'une image en laisse, 12 ms de calcul par image).
        void                setSpeed(int factor) noexcept { speed_ = factor < 0 ? 1 : factor; }
        [[nodiscard]] int   speed() const noexcept { return speed_; }
        // Lot API 7 : une fonction inconnue rend 0 et le cycle continue (vrai, le
        // defaut de l'appli) ou arrete le cycle (faux, comme avant).
        void                setContinueOnUnknownCalls(bool on);
        [[nodiscard]] bool  continueOnUnknownCalls() const noexcept { return continueOnUnknown_; }
        // Le temps de calcul du dernier cycle (en microsecondes) et ses instructions.
        [[nodiscard]] std::int64_t  lastScanMicros() const noexcept { return lastScanMicros_; }
        [[nodiscard]] std::uint32_t lastScanStatements() const noexcept { return lastStatements_; }
        // Lot API 7 : LE PROGRAMME A CHANGE DEPUIS LA PREPARATION. Le projet est
        // modifie en place (le meme objet) : comparer les pointeurs ne voit ni une
        // section retouchee, ni un bloc mis a jour depuis la bibliotheque - et
        // Simuler relancait l'ANCIEN code (BUILDING 0.22 apres le passage en
        // 0.24). L'application donne ici le numero de revision du programme a
        // chaque image ; attach() prepare a nouveau si celle de la preparation
        // n'est plus la bonne, et stale() le dit a ceux qui voudraient savoir.
        // Lot API 8 : la revision ne sert plus qu'a savoir QUAND regarder : a
        // chaque revision nouvelle, l'empreinte du programme est recalculee
        // (sim::Runtime::programFingerprint - sections, variables, types, DFB,
        // unites, taches ; pas l'IHM, ni les versions, ni les commentaires) et
        // seul un changement de l'empreinte rend la simulation perimee.
        void setProgramRevision(std::uint64_t revision) noexcept;
        [[nodiscard]] bool stale() const noexcept { return runtime_ && preparedRevision_ != programRevision_; }
        // Lot API 7 : UN NUMERO PAR RUNTIME. Il change a chaque runtime prepare
        // (et a chaque detach) : un nouveau runtime peut naitre a l'ADRESSE de
        // celui qu'on vient de detruire (detach puis attach : le meme bloc
        // memoire), et qui compare des pointeurs garde alors des pointeurs vers
        // les valeurs de l'ancien. Comparer ce numero ne se trompe pas.
        [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

        // ---- Lot API 8 : debogage et modification en ligne (voir SimBreakpoint...) ----
        std::uint32_t addBreakpoint(const std::string& section, int line, const std::string& condition = {});
        bool          removeBreakpoint(std::uint32_t id);
        bool          setBreakpointEnabled(std::uint32_t id, bool enabled);
        bool          setBreakpointCondition(std::uint32_t id, const std::string& condition);
        void          clearBreakpoints();
        [[nodiscard]] std::vector<SimBreakpoint> breakpoints() const;
        // En pause sur un point d'arret : ou, pourquoi (vide sinon ; efface au lancement suivant).
        [[nodiscard]] const std::optional<SimBreakHit>& lastBreakHit() const noexcept;
        void          stepSection();                       // la section suivante seulement, puis pause
        [[nodiscard]] std::string nextSection() const;     // ce que stepSection executera ("" : un cycle commence)
        [[nodiscard]] std::vector<SimSectionTime> sectionTimes() const;   // au dernier cycle complet
        [[nodiscard]] bool lastWrite(const std::string& path, SimLastWrite& out) const;
        // LE PROGRAMME DIRAIT : ce que le programme ecrit sous un forcage (la
        // case, pas le forcage : sim::Runtime::programValue), dit comme la
        // valeur (Value::display). Faux : pas de simulation, chemin inconnu.
        [[nodiscard]] bool unforcedValue(const std::string& path, std::string& out) const;
        [[nodiscard]] const std::optional<SimOnlineChange>& lastOnlineChange() const noexcept;
        const core::SignalPtr<> breakHit      = core::Signal<>::create();   // un point d'arret vient de mettre en pause
        const core::SignalPtr<> onlineChanged = core::Signal<>::create();   // une modification en ligne vient d'etre faite (ou a echoue)
        // ---- Lot API 8 : le moteur (ajouts) ----
        //  Un cycle commence par stepSection() et pas encore fini ; les entrees
        //  de MAST dans l'ordre (sections, unites en bloc) et celle qui suit.
        [[nodiscard]] bool scanInProgress() const noexcept;
        [[nodiscard]] std::vector<std::string> taskEntries() const;
        [[nodiscard]] std::size_t nextEntryIndex() const noexcept;
        //  La modification en ligne attend que le programme ne bouge plus depuis
        //  ce delai (secondes de tick ; 0 : tout de suite) : l'editeur de code
        //  fait une commande par touche, et un mot a moitie tape ne se prepare
        //  pas. Un geste (Simuler, Un cycle, la section suivante) n'attend pas.
        void setOnlineChangeDelay(double seconds) noexcept { onlineDelay_ = seconds < 0.0 ? 0.0 : seconds; }
        [[nodiscard]] double onlineChangeDelay() const noexcept { return onlineDelay_; }

        // Advances the simulated clock. Called once per frame by App, whatever is on
        // screen. The clock moves by the configured interval per scan rather than by
        // real time: the point is a repeatable sequence, not a race with the frame
        // rate.
        void tick(double deltaSeconds);

        [[nodiscard]] std::uint64_t scanCount() const noexcept;
        [[nodiscard]] const std::string& haltMessage() const noexcept { return haltMessage_; }
        [[nodiscard]] const std::vector<sim::Diagnostic>& lastDiagnostics() const noexcept {
            return lastDiagnostics_;
        }

        // "RUNNING  scan 86". One spelling of the state, used by the simulation
        // screen's banner and by the workspace indicator, so the two can never
        // disagree about what the simulator is doing.
        [[nodiscard]] std::string_view stateName() const noexcept;
        [[nodiscard]] std::string      statusLine() const;

        // Emitted after any state change and after any frame that ran at least one
        // scan, so a screen refreshes without polling.
        const core::SignalPtr<>                       changed = core::Signal<>::create();
        // Every scan that produced diagnostics, including the ones that ran while no
        // simulation screen existed.
        const core::SignalPtr<const sim::ScanReport&> scanned =
            core::Signal<const sim::ScanReport&>::create();

    private:
        void applyReport(const sim::ScanReport& report);

        std::shared_ptr<sim::Runtime>          runtime_;
        std::shared_ptr<const domain::Project> project_;
        State                                  state_{ State::Stopped };
        double                                 accumulator_{ 0.0 };
        std::int64_t                           scanIntervalMs_{ 20 };
        int                                    speed_{ 1 };
        bool                                   continueOnUnknown_{ true };
        std::int64_t                           lastScanMicros_{ 0 };
        std::uint32_t                          lastStatements_{ 0 };
        std::uint64_t                          programRevision_{ 0 };    // lot API 7
        std::uint64_t                          preparedRevision_{ 0 };
        std::uint64_t                          generation_{ 0 };         // lot API 7
        // Lot API 8 (le moteur les remplit ; SimulationHostDebug.cpp)
        std::vector<SimBreakpoint>             breakpoints_;
        std::uint32_t                          nextBreakpointId_{ 1 };
        std::optional<SimBreakHit>             lastBreakHit_;
        std::optional<SimOnlineChange>         lastOnlineChange_;
        std::string                            haltMessage_;
        std::vector<sim::Diagnostic>           lastDiagnostics_;
        // ---- Lot API 8 : le moteur (SimulationHostDebug.cpp) ----
        std::uint64_t                          rawRevision_{0};          // la revision vue (App)
        bool                                   rawSeen_{false};
        std::uint64_t                          programPrint_{0};         // l'empreinte du programme du projet
        std::uint64_t                          failedPrint_{0};          // celle dont la preparation a echoue
        bool                                   failedChange_{false};
        double                                 clock_{0.0};              // les secondes de tick
        double                                 changeSeenAt_{0.0};
        double                                 onlineDelay_{0.75};
        bool                                   resumeAfterFix_{false};   // en pause a cause d'un echec : reprendre une fois corrige
        std::int64_t                           partialMicros_{0};        // le calcul d'un cycle fait section par section
        const void*                            breakpointsOwner_{nullptr};
        bool applyOnlineChange();
        void syncProgram();                  // avant un geste : le nouveau code
        [[nodiscard]] bool changeBlocked() const noexcept;
        void pushBreakpoints();
        void pullHits();
        bool takeBreakHit();
    };

} // namespace app