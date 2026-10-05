// =============================================================================
//  project/GrafcetEdit.hpp — editing a chart means editing the ST that builds it
// -----------------------------------------------------------------------------
//  THE THING TO UNDERSTAND BEFORE READING ANY OF THIS.
//
//  A chart in this project is not stored anywhere. It is ST source that fills
//  three arrays, and grafcet::findCharts reconstructs the chart by reading that
//  source. So editing a chart is editing SOURCE TEXT, and the direction of
//  authority never reverses: the text is the truth, the chart is a view of it.
//
//  That rules out the obvious design. Regenerating the section from the model
//  would be far easier to write and would throw away every comment, every
//  alignment, every hand-written line between the Builder calls - and those
//  sections are full of all three. So each edit is SURGICAL: it finds the exact
//  line or block it owns, replaces that, and leaves every other byte alone.
//
//  The measure of that is not "it looks right". It is: setting a value to what
//  it already is must leave the section BYTE-IDENTICAL. The tests assert exactly
//  that, because a round trip that drifts by one space is a round trip that will
//  eventually drift by one statement.
//
//  Undo keeps the whole previous section text. That is a few kilobytes and it is
//  exactly inverse, which a reconstructed edit would not be.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../domain/ProjectModel.hpp"
#include "Grafcet.hpp"

#include <memory>
#include <string>

namespace project {

    using ProjectPtr = std::shared_ptr<domain::Project>;

    // ---------------------------------------------------------------------------
    //  Trans_<prefix>[id].Condition := <expression>;
    //
    //  The line exists for every transition in a well-formed chart; the parser warns
    //  when it does not, and this refuses rather than inventing one, because where
    //  to put a missing line is a question with no safe answer - after the last
    //  condition, before the engine call, somewhere among the comments? Guessing
    //  would put executable code in a place the author did not choose.
    // ---------------------------------------------------------------------------
    class SetTransitionConditionCommand final : public core::ICommand {
    public:
        SetTransitionConditionCommand(ProjectPtr project, domain::Index section,
            int transitionId, std::string expression);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        ProjectPtr    project_;
        domain::Index section_;
        int           transitionId_;
        std::string   expression_;
        std::string   previousBody_;
        bool          applied_{ false };
    };

    // ---------------------------------------------------------------------------
    //  The statements inside  IF Acts_<prefix>[id].Out THEN ... END_IF;
    //
    //  The block is found with grafcet::blockEnd, not by counting substrings: an
    //  ELSIF inside the body would fool a substring count into swallowing the rest
    //  of the section, and here that would not merely display wrongly, it would
    //  delete code.
    //
    //  The indentation of the first replaced line is reused for all of them, so an
    //  edited body sits the way the rest of the file does instead of announcing
    //  which lines a tool wrote.
    // ---------------------------------------------------------------------------
    class SetActionBodyCommand final : public core::ICommand {
    public:
        SetActionBodyCommand(ProjectPtr project, domain::Index actionSection,
            std::string arrayPrefix, int actionId, std::string body);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        ProjectPtr    project_;
        domain::Index section_;
        std::string   arrayPrefix_;
        int           actionId_;
        std::string   body_;
        std::string   previousBody_;
        bool          applied_{ false };
    };

    // The two lookups the commands use, exposed because the editor wants to know
    // whether an edit is possible before it offers it - a menu entry that opens a
    // box and then refuses is worse than one that was never enabled.
    [[nodiscard]] bool findConditionLine(const domain::Project&, domain::Index section,
        std::string_view arrayPrefix, int transitionId,
        std::size_t& line);
    [[nodiscard]] bool findActionBlock(const domain::Project&, domain::Index section,
        std::string_view arrayPrefix, int actionId,
        std::size_t& openLine, std::size_t& closeLine);

} // namespace project