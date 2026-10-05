// =============================================================================
//  project/CrossReference.hpp — where a variable is written, and where it is read
// -----------------------------------------------------------------------------
//  The project already had a reference index: a COUNT per symbol, enough to say
//  "this variable is never used" and nothing else. The question a reviewer
//  actually asks is the other one - WHO WRITES THIS, and under what condition -
//  and a count cannot answer it.
//
//  FOUR THINGS THAT DECIDE THE WHOLE IMPLEMENTATION.
//
//   ST IS CASE-INSENSITIVE. gMotor, GMOTOR and gmotor are one variable. The
//   importer already lower-cases its indices for this reason; anything here that
//   compared case-sensitively would find some of the uses and miss the rest,
//   which is worse than finding none.
//
//   WHOLE TOKENS ONLY. Searching for "test" must not match "test2" or
//   "contest". A substring search produces a cross-reference that looks complete
//   and is wrong, and nothing about it says so.
//
//   COMMENTS AND STRING LITERALS ARE NOT CODE. (* set gMotor here *) is not a
//   use of gMotor, and neither is 'gMotor'. Counting them inflates every number
//   on the screen.
//
//   A WRITE IS A NAME ON THE LEFT OF ':='. That rule is simple, and it is not
//   complete: an InOut parameter of a block is written by the block, and this
//   sees it on the right of a ':=' and calls it a read. The limitation is
//   recorded on the reference itself rather than hidden, because a maintainer
//   who trusts "never written" and is wrong has been actively misled.
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"

#include <string>
#include <vector>

namespace project {

    struct Reference {
        enum class Kind : std::uint8_t {
            Write,          // on the left of a ':='
            Read,           // anywhere else
            CallArgument,   // passed to a block: read, unless the parameter is InOut
            // L'appel d'une sous-routine. En ST, appeler une SR c'est ecrire son nom
            // seul, en instruction: "Init_ES;". Ce n'est donc ni une lecture ni une
            // ecriture de variable - c'est l'endroit ou ce code s'execute, et c'est
            // la seule chose qu'on veuille savoir d'une SR.
            Call,
        };

        domain::Index section{ domain::kNoIndex };
        std::string   sectionName;
        std::string   ownerName;      // the POU it belongs to, or empty for a task section
        std::size_t   line{ 0 };
        Kind          kind{ Kind::Read };
        std::string   path;           // what was touched: gStatus, or gStatus.bEngineOn
        std::string   text;           // the statement, trimmed
        std::string   condition;      // the IF this sits under, when there is one
    };

    struct CrossReference {
        std::string            name;
        std::vector<Reference> writes;
        std::vector<Reference> reads;
        std::vector<Reference> calls;

        [[nodiscard]] std::size_t total() const noexcept {
            return writes.size() + reads.size() + calls.size();
        }
        // How many distinct sections mention it. "Used in 3 sections" is the number a
        // reviewer wants; "9 references" is the one they have to work out.
        [[nodiscard]] std::size_t sectionCount() const;
    };

    [[nodiscard]] CrossReference crossReference(const domain::Project&, std::string_view name);

    // ---------------------------------------------------------------------------
    //  A variable's structure, expanded.
    //
    //  A DDT holds DDTs, so this recurses - with a depth limit, because a type that
    //  contains itself is a type this would follow until the stack ran out. Control
    //  Expert forbids it; a file on disk is not obliged to be valid.
    // ---------------------------------------------------------------------------
    struct StructureNode {
        std::string   name;          // the leaf name
        std::string   path;          // the full path from the root variable
        std::string   type;
        std::string   comment;
        std::size_t   depth{ 0 };
        bool          isStruct{ false };
        bool          truncated{ false };   // the recursion stopped here
        std::vector<StructureNode> children;
    };

    [[nodiscard]] StructureNode expandStructure(const domain::Project&, domain::Index variable,
        std::size_t maxDepth = 8);

} // namespace project