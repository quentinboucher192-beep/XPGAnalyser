// =============================================================================
//  project/DeleteCommands.hpp — removing things, undoably
// -----------------------------------------------------------------------------
//  EditCommands.hpp explains why *creation* can get away with append-and-
//  truncate. Deletion cannot, and domain/Reindex.hpp explains the design that
//  replaces it. This file is the policy layer on top of that mechanism: what may
//  be deleted, what must be refused, and what comes with it.
//
//  THREE RULES, THE SAME THREE EVERY TIME
//
//   1. REFUSE ON REFERENCE, DO NOT CASCADE ACROSS ONE. Deleting a DDT that
//      twelve variables are declared with is not a deletion, it is twelve
//      silent type changes. The command refuses and names the first user, so
//      the message is actionable: "ST_GC_Step is still used by Gaz_1.Step".
//
//   2. CASCADE ON CONTAINMENT. A DDT's fields, a DFB's parameters, locals and
//      code sections have no existence apart from it. They go with it, and they
//      come back with it.
//
//   3. VALIDATE BEFORE MUTATING. Piège nº 7: a command that fails must leave
//      the model untouched. Every check below happens before the first erase,
//      never between two of them.
//
//  Undo restores the removed entities in reverse order of removal, which makes
//  the pair exactly inverse: each restoreEntity() undoes one eraseEntity(), and
//  reversing the sequence undoes the sequence.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../domain/Reindex.hpp"

#include <memory>
#include <string>
#include <vector>

namespace project {

using ProjectPtr = std::shared_ptr<domain::Project>;

// ---------------------------------------------------------------------------
//  One command for all four entity kinds.
//
//  Four near-identical classes were the first draft. They differed only in
//  which vector they indexed and which references they had to check, and the
//  duplication meant a fix to one was a fix owed to three others. What varies
//  is data, so it is data.
// ---------------------------------------------------------------------------
class RemoveEntityCommand final : public core::ICommand {
public:
    RemoveEntityCommand(ProjectPtr project, domain::EntityKind kind, domain::Index index);

    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;

    // What execute() would delete along with the target, as human-readable
    // lines. The context menu shows this before asking for confirmation: a
    // deletion that quietly takes eight sections with it is one people undo.
    [[nodiscard]] std::vector<std::string> collateralDamage() const;
    // Lot API 5 : pourquoi execute() refuserait (un type encore utilise, un bloc
    // qui a des instances) - dit AVANT la confirmation, pas apres.
    [[nodiscard]] core::Status refusalReason() const;

private:
    // Everything that must go, deepest first, so an erase never invalidates an
    // index computed for a later erase in the same list.
    [[nodiscard]] std::vector<std::pair<domain::EntityKind, domain::Index>> plan() const;

    ProjectPtr                          project_;
    domain::EntityKind                  kind_;
    domain::Index                       index_;
    std::string                         name_;          // captured for the label
    std::vector<domain::RemovedEntity>  removed_;        // in removal order

    // Library entries are not reachable by index from anywhere in the model, so
    // they are not an EntityKind. They still have to come back on undo, and at
    // the position they held: the list is what the Libraries pane shows.
    std::vector<std::pair<domain::Index, domain::LibraryEntry>> removedLibraryEntries_;
};

// Is anything still declared with this type / instancing this block? Returns the
// name of the first user found, or an empty string. Exposed because the UI wants
// to grey the menu entry out before the command is ever built.
[[nodiscard]] std::string firstUserOfDerivedType(const domain::Project&, domain::Index);
[[nodiscard]] std::string firstInstanceOfBlock(const domain::Project&, domain::Index pou);

} // namespace project
