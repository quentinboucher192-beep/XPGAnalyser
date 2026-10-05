// =============================================================================
//  project/Macro.hpp - scripting the editor, in the language it already speaks
// -----------------------------------------------------------------------------
//  A macro is ST. Not a new language: THE SAME ONE, parsed by the same
//  sim::parse and run by the same sim::execute that the simulator uses.
//
//  That is not laziness, it is the cheapest correct answer to three problems at
//  once. The people who will write these macros write ST all day, so the
//  learning cost is nil. The parser handles IF, CASE, FOR, WHILE and REPEAT and
//  is covered by simulation_test, so none of that has to be written or proved
//  again. And sim::Environment already has the exact seam a host needs:
//
//      virtual bool call(name, instance, arguments, result) = 0;
//
//  sim::Runtime implements it for the PLC. MacroEnvironment implements it for
//  the project. Same evaluator, different world.
//
//  FOUR RULES THAT SHAPE EVERYTHING BELOW.
//
//   1. A MACRO NEVER TOUCHES THE PROJECT DIRECTLY. Every native that changes
//      something builds an ICommand and hands it over. One run is one compound
//      command, so a macro that went wrong is undone by one Ctrl+Z - not by
//      remembering what it did.
//
//   2. PREVIEW BEFORE APPLY, AND BOTH REALLY RUN. Every command is executed the
//      moment it is built, in both modes; Preview simply undoes the lot at the
//      end. That is not a detail - it is what makes a macro able to CREATE
//      SOMETHING AND THEN USE IT. The first version staged without executing, so
//      AddProgramUnit followed by AddSection into that unit reported "the unit
//      does not exist": the query looked at a project the staged command had not
//      reached yet. It also means a preview reports the REAL refusals, not the
//      ones we imagine.
//
//   3. A MACRO CAN ONLY CALL WHAT IS REGISTERED. The function table IS the
//      permission list. No shell, no arbitrary path, no way to reach the disk
//      except through a native that was written for it.
//
//   4. QUESTIONS ARE ASKED IN ROUNDS, NOT MID-RUN. sim::execute runs to
//      completion and cannot be suspended, and ShowDialog answers a frame or
//      more later. So a run that meets Ask() records the question and carries
//      on with a blank; the host asks everything at once and runs again. Two
//      rounds cover a dependent question, and one dialog with seven fields beats
//      seven dialogs.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../domain/ProjectModel.hpp"
#include "../sim/Interpreter.hpp"
#include "Table.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace project {

    class SharedLibrary;

    // ---------------------------------------------------------------------------
    //  What a macro asked the reader, and what it was told.
    // ---------------------------------------------------------------------------
    struct MacroQuestion {
        enum class Kind : std::uint8_t { Text, Choice, Number, YesNo };
        Kind        kind{ Kind::Text };
        std::string key;          // identifies it across rounds; without it a
        // question inside a loop would be asked every pass
        std::string prompt;
        std::string preset;
        std::vector<std::string> choices;
        double      minimum{ 0.0 }, maximum{ 0.0 };
    };

    // What a run actually did, in numbers. A preview that says "34 actions" and a
    // list too long to read tells nobody whether it created the two instances it was
    // supposed to.
    struct MacroTally {
        std::size_t variables{ 0 };
        std::size_t instances{ 0 };    // variables whose type is a DFB
        std::size_t types{ 0 };
        std::size_t sections{ 0 };
        std::size_t lines{ 0 };        // statements appended
        std::size_t imports{ 0 };
        std::size_t rowsRead{ 0 };
        std::size_t rowsSkipped{ 0 };
    };

    struct MacroReport {
        MacroTally                 tally;
        bool                       ok{ true };
        bool                       needsAnswers{ false };   // another round is required
        std::vector<MacroQuestion> questions;             // asked this round
        std::vector<std::string>   log;                   // Log()
        std::vector<std::string>   warnings;              // Warn()
        std::vector<std::string>   actions;               // what the commands would do
        std::vector<sim::Diagnostic> diagnostics;         // parse and run errors
        std::string                failure;               // Fail(), or the first error
        // LOT MACROS 1 : TOUTES les questions que ce tour a rencontrees, dans
        // l'ordre, qu'elles aient deja une reponse ou non. `questions` ne dit que
        // ce qui reste a demander ; le formulaire, lui, doit montrer chaque champ
        // que la macro atteint avec les reponses du moment - et faire disparaitre
        // celui d'une branche que la derniere reponse a ecartee.
        std::vector<MacroQuestion> reached;
        // AskNow a arrete le tour (et non une question sans reponse arrivee au
        // bout) : un tour qui n'a pas ete arrete est alle au bout avec les
        // valeurs proposees, et son bilan vaut apercu.
        bool                       stopped{ false };
    };

    enum class MacroMode : std::uint8_t { Preview, Apply };

    // ---------------------------------------------------------------------------
    //  One run of one macro, against one project.
    // ---------------------------------------------------------------------------
    class MacroRunner {
    public:
        using ProjectPtr = std::shared_ptr<domain::Project>;

        MacroRunner(ProjectPtr project, SharedLibrary* library = nullptr);
        ~MacroRunner();

        // Answers from a previous round, by question key. A key that is not here is
        // asked again.
        void setAnswers(std::map<std::string, std::string> answers);

        // Where a table may be read from. A macro cannot name a path itself: it says
        // OpenTable('cartes') and the host decides which file that is. A script that
        // could open any path would be a script that could read anything.
        void addTable(std::string name, std::string csvBytes, TableOptions = {});
        [[nodiscard]] std::vector<std::string> tableNames() const;

        [[nodiscard]] MacroReport run(std::string_view source, std::string name,
            MacroMode mode = MacroMode::Preview);

        // The commands a successful Apply produced, as one undoable step.
        [[nodiscard]] core::CommandPtr takeCommand();

        // Every native, for the editor's completion and for the documentation.
        struct NativeInfo {
            std::string category;
            std::string signature;
            std::string help;
        };
        [[nodiscard]] static const std::vector<NativeInfo>& natives();

    private:
        class Environment;
        std::unique_ptr<Environment> env_;
    };

    // ---------------------------------------------------------------------------
    //  Several commands, undone as one.
    //
    //  A macro that created a type, three variables and two sections must be undone
    //  by one Ctrl+Z. Undoing it statement by statement would leave the reader
    //  pressing Ctrl+Z six times and wondering after each one whether they had gone
    //  far enough.
    // ---------------------------------------------------------------------------
    class CompoundCommand final : public core::ICommand {
    public:
        explicit CompoundCommand(std::string label);

        // A command that has ALREADY been executed. The macro runs each one as it
        // builds it, so the compound has to be told - otherwise undo() believes
        // nothing went through and quietly does nothing, which is how a preview
        // leaves its changes behind.
        void addApplied(core::CommandPtr command);
        void add(core::CommandPtr command);
        [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }
        [[nodiscard]] std::size_t size() const noexcept { return commands_.size(); }

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        std::string                  label_;
        std::vector<core::CommandPtr> commands_;
        std::size_t                  applied_{ 0 };   // how many went through
        bool                         skipFirst_{ false };
    };

} // namespace project