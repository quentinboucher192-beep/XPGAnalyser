// =============================================================================
//  project/GrafcetRewrite.hpp — inserting and removing, in the text
// -----------------------------------------------------------------------------
//  domain/Reindex.hpp solved this problem once, for the model: removing element
//  k from a flat vector renumbers everything above it. Here it comes back in a
//  worse form, because the vectors are SPELLED OUT IN SOURCE TEXT and nothing is
//  typed. Inserting a step at 5 means rewriting, across two sections:
//
//     Steps_ManuA[5..n]        the array subscripts
//     id=5.. in T:=1           the step descriptors
//     s= and d= in T:=2        the step ids a transition joins
//     s= in T:=3               the step an action belongs to
//
//  and NOT: id= in T:=2 or T:=3 (those are transition and action ids), and not
//  Trans_ManuA[i] or Acts_ManuA[i] (different vectors entirely).
//
//  THE TRAP, WRITTEN DOWN BECAUSE IT IS THE WHOLE DIFFICULTY. The same letters
//  mean different things depending on which Builder call they are in. `s=` is a
//  SOURCE STEP on a transition and a BOUND STEP on an action - both step ids, so
//  both shift. `id=` is a step id on T:=1 and a transition id on T:=2 - only the
//  first shifts. Getting this wrong does not fail to compile and does not look
//  wrong: it quietly reattaches an action to the neighbouring step.
//
//  So the rewrite is driven by the Builder call's own T:= value, never by
//  pattern-matching a field name on its own.
//
//  THE MEASURE, as everywhere else here: inserting and then removing the same
//  element must return the section BYTE-IDENTICAL.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../domain/ProjectModel.hpp"
#include "Grafcet.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace project {

    using ProjectPtr = std::shared_ptr<domain::Project>;

    // Which of the chart's three vectors an id belongs to.
    enum class ChartVector : std::uint8_t { Step, Transition, Action };

    // Shifts every id at or above `from` by `delta`, in both of the chart's
    // sections. Exposed for the tests and for a future move operation; the commands
    // below are how the editor should reach it.
    void shiftIds(domain::Project&, const grafcet::Chart&, ChartVector, int from, int delta);

    // ---------------------------------------------------------------------------
    //  Inserting a step.
    //
    //  The new declaration goes after the last step declaration BELOW its id and
    //  before the first one above it, so the Builder calls stay in the order a
    //  reader expects. Everything at or above the new id moves up by one.
    //
    //  Refused when the chart is full. The limit is not a constant this tool can
    //  change: Steps_<name> is declared ARRAY[0..27] in the program unit AND the
    //  engine takes ARRAY[0..27] as an InOut parameter, so widening it changes
    //  DFB_GRAFCETENGINE's signature and therefore all seventeen instances. That is
    //  a decision about the automation code, not about a viewer.
    // ---------------------------------------------------------------------------
    class InsertStepCommand final : public core::ICommand {
    public:
        InsertStepCommand(ProjectPtr project, domain::Index section, int newId,
            std::string name, bool initial = false, bool isFinal = false);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        ProjectPtr    project_;
        domain::Index section_;
        int           newId_;
        std::string   name_;
        bool          initial_, final_;
        std::string   previousChart_, previousActions_;
        domain::Index actionSection_{ domain::kNoIndex };
        // 1.10 (R2) : les autres sections de l'unite que shiftIds renumerote (SFC_DEBUG).
        std::vector<std::pair<domain::Index, std::string>> previousOthers_;
        bool          applied_{ false };
    };

    // ---------------------------------------------------------------------------
    //  Removing a step.
    //
    //  `renumber` is the question the editor must put to the reader rather than
    //  answer for them. Renumbering keeps the chart contiguous, which is what the
    //  engine's arrays want and what every existing chart looks like - but it
    //  changes the name of every step above the deleted one, and those names are on
    //  the operator's screens, in the maintenance notes, and in people's heads.
    //  Leaving the hole keeps the names and leaves a gap the engine tolerates.
    //
    //  Transitions that referenced the removed step are NOT silently repaired: they
    //  are reported, because a transition quietly re-pointed at a neighbour is the
    //  kind of edit that is discovered on the machine.
    // ---------------------------------------------------------------------------
    class RemoveStepCommand final : public core::ICommand {
    public:
        RemoveStepCommand(ProjectPtr project, domain::Index section, int stepId, bool renumber);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

        // What the deletion would orphan, for the confirmation box. Computed before
        // anything is touched.
        [[nodiscard]] std::vector<std::string> orphaned() const;

    private:
        ProjectPtr    project_;
        domain::Index section_;
        int           stepId_;
        bool          renumber_;
        std::string   previousChart_, previousActions_;
        domain::Index actionSection_{ domain::kNoIndex };
        std::vector<std::pair<domain::Index, std::string>> previousOthers_;   // 1.10 (R2)
        bool          applied_{ false };
    };

    // ---------------------------------------------------------------------------
    //  Inserting a transition, and its condition line with it.
    //
    //  A transition without a '.Condition :=' can never fire, so the two are written
    //  together: offering an editor a transition that is dead until they find the
    //  right section to type a second line in is offering them a trap. The condition
    //  goes with the other conditions, at the matching index - that block is written
    //  in transition order and keeping it so is the whole reason it is readable.
    // ---------------------------------------------------------------------------
    class InsertTransitionCommand final : public core::ICommand {
    public:
        InsertTransitionCommand(ProjectPtr project, domain::Index section, int newId,
            std::vector<int> sources, std::vector<int> destinations,
            std::string label, std::string expression);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        ProjectPtr       project_;
        domain::Index    section_;
        int              newId_;
        std::vector<int> sources_, destinations_;
        std::string      label_, expression_;
        std::string      previousChart_;
        std::vector<std::pair<domain::Index, std::string>> previousOthers_;   // 1.10 (R2)
        bool             applied_{ false };
    };

    // ---------------------------------------------------------------------------
    //  Inserting an action, and the body block for it in the companion section.
    //
    //  Two comment lines go above the block, as asked: which step the action belongs
    //  to, and what kind of action it is. They are not decoration. "k=4" is what the
    //  descriptor says and "IF Acts_PompageA[7].Out THEN" is what the body says;
    //  neither tells a maintainer reading the section at 3am which step this belongs
    //  to or when it fires.
    // ---------------------------------------------------------------------------
    class InsertActionCommand final : public core::ICommand {
    public:
        InsertActionCommand(ProjectPtr project, domain::Index section, int newId,
            std::string name, grafcet::ActionKind kind, int boundStep,
            std::string body, std::string delay = {});

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        ProjectPtr          project_;
        domain::Index       section_;
        int                 newId_;
        std::string         name_;
        grafcet::ActionKind kind_;
        int                 boundStep_;
        std::string         body_, delay_;
        std::string         previousChart_, previousActions_;
        domain::Index       actionSection_{ domain::kNoIndex };
        std::vector<std::pair<domain::Index, std::string>> previousOthers_;   // 1.10 (R2)
        bool                applied_{ false };
    };

    // ---------------------------------------------------------------------------
    //  Re-pointing a transition.
    //
    //  Only the descriptor's s= or d= changes; the condition, the delay and
    //  everything around them are left exactly where they are. Nothing else in the
    //  chart moves, because nothing else has to: the ids of the steps and of the
    //  transition are unchanged.
    //
    //  It refuses to leave a transition with no source or no destination. A
    //  transition with an empty end does not fail to compile and does not look
    //  wrong - it simply never fires, which is the hardest kind of fault to find.
    // ---------------------------------------------------------------------------
    class SetTransitionEndpointsCommand final : public core::ICommand {
    public:
        SetTransitionEndpointsCommand(ProjectPtr project, domain::Index section, int transitionId,
            std::vector<int> sources, std::vector<int> destinations);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        ProjectPtr       project_;
        domain::Index    section_;
        int              transitionId_;
        std::vector<int> sources_, destinations_;
        std::string      previousChart_;
        bool             applied_{ false };
    };

    // ---------------------------------------------------------------------------
    //  Renaming a step, and changing what it is.
    //
    //  Only the descriptor changes - the n=, i= and f= fields of one Builder(T:=1)
    //  call. The step's id does not move, so nothing else in the chart has to: no
    //  transition, no action, no array subscript.
    //
    //  The name is limited to four characters, and this refuses beyond that rather
    //  than truncating. BUILDING assigns Name only when the value fits, so a longer
    //  one leaves the PLC with no name at all - the step would lose the name it
    //  already had, which is the opposite of renaming it.
    // ---------------------------------------------------------------------------
    class RenameStepCommand final : public core::ICommand {
    public:
        RenameStepCommand(ProjectPtr project, domain::Index section, int stepId,
            std::string name, bool initial, bool isFinal);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        ProjectPtr    project_;
        domain::Index section_;
        int           stepId_;
        std::string   name_;
        bool          initial_, final_;
        std::string   previousChart_;
        bool          applied_{ false };
    };

    // How many slots the chart's arrays have, read from its own declaration rather
    // than assumed. Zero when it cannot be determined.
    [[nodiscard]] int chartCapacity(const domain::Project&, const grafcet::Chart&);

} // namespace project