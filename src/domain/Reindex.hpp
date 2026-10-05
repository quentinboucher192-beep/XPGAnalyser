// =============================================================================
//  domain/Reindex.hpp — removing an entity from the middle, safely
// -----------------------------------------------------------------------------
//  THE DECISION, AND WHY
//
//  Creation appends and undo truncates, which is why EditCommands could ignore
//  this problem entirely. Deletion cannot: removing element k from a flat vector
//  renumbers every element after it, and this model is nothing but 32-bit
//  indices into flat vectors.
//
//  Two designs were on the table.
//
//   (a) TOMBSTONES — mark the entity dead, leave the slot. Undo is trivial and
//       no index ever moves. The cost is that every loop over a vector must
//       learn to skip the dead: the writer, the analyzer, the runtime, the view
//       models, the store, and the tests. There are about thirty such loops. A
//       loop that is missed does not crash — it puts a deleted DFB back into the
//       exported .XPG. A wrong file that looks right is the worst failure this
//       program can produce, and tombstones make it the default outcome of
//       forgetting one line.
//
//   (b) REMOVE AND RENUMBER — erase, then rewrite every stored index. More work
//       up front, and the work is exhaustive-or-broken. But a mistake here
//       produces a reference that points at the wrong entity, which the delete
//       test and the round-trip test both catch immediately: export, delete,
//       undo, export, compare bytes.
//
//  (b) was chosen. A failure that a test can see beats a failure that ships.
//
//  The whole renumbering lives in ONE function, remapReferences(). Every Index
//  field in domain/ProjectModel.hpp is listed there. Adding a new one and
//  forgetting to list it is the single way to break this; the delete test walks
//  the model afterwards and asserts that no reference points outside its vector,
//  so the omission surfaces as a failing test rather than a bad export.
//
//  The mapping is also its own inverse in the sense that matters: removal
//  shifts everything above k down by one, insertion shifts everything from k up
//  by one, and both go through the same code. So undo is not a second
//  implementation to keep in step with the first.
// =============================================================================
#pragma once

#include "ProjectModel.hpp"

#include <variant>
#include <vector>

namespace domain {

// Which flat vector an index refers to. Only these four are reachable by index
// from elsewhere in the model; racks and modules are values inside their own
// containers and tasks are looked up by name.
enum class EntityKind : std::uint8_t { Variable, DerivedType, Pou, Section };

[[nodiscard]] std::string_view toString(EntityKind) noexcept;

// mapping[i] is the new index of old index i. kNoIndex means "this one is gone":
// list entries are dropped, scalar fields become kNoIndex. Indices at or beyond
// mapping.size() are left untouched, which is what makes it safe to call this
// with a mapping built before an unrelated vector grew.
void remapReferences(Project&, EntityKind, const std::vector<Index>& mapping);

// Removing element `victim` from a vector of `count`: everything above it slides
// down one place.
[[nodiscard]] std::vector<Index> mappingForRemoval(std::size_t count, Index victim);

// Putting an element back at `position` in a vector of `count`: everything from
// there up slides one place higher. The exact inverse of the above.
[[nodiscard]] std::vector<Index> mappingForInsertion(std::size_t count, Index position);

// ---------------------------------------------------------------------------
//  Membership: the lists that held the removed index.
//
//  remapReferences drops a dead index out of every list it appears in, which is
//  right for the removal but loses the information an undo needs — *where* in
//  the list it sat. A section restored to the end of MAST instead of the middle
//  would change the scan order of the program, which is a behavioural change,
//  not a cosmetic one. So the positions are recorded before the erase.
// ---------------------------------------------------------------------------
struct Membership {
    enum class List : std::uint8_t {
        TaskSections, PouSections, PouParameters, PouLocals, PouChildren, DerivedFields
    };
    List  list{List::PouSections};
    Index container{kNoIndex};   // the task / pou / ddt holding the list
    Index position{0};           // where in that list the index sat
};

// Everything needed to put one entity back exactly as it was.
struct RemovedEntity {
    EntityKind              kind{EntityKind::Variable};
    Index                   position{kNoIndex};
    std::vector<Membership> memberships;
    std::variant<Variable, DerivedType, Pou, Section> payload;
};

// Erase one entity and rewrite every reference to it. Nothing here cascades and
// nothing here refuses: the caller decides whether a deletion is legitimate
// (see project::RemoveCommand) and this only carries it out.
[[nodiscard]] RemovedEntity eraseEntity(Project&, EntityKind, Index);

// Put one back. Exact inverse of eraseEntity for the same entity, provided the
// vectors are in the state eraseEntity left them in — which the strictly LIFO
// command stack guarantees, exactly as it does for the append/truncate case.
void restoreEntity(Project&, const RemovedEntity&);

// ---------------------------------------------------------------------------
//  Referential integrity check. Not used in the normal path; it is what the
//  delete test asserts, and what a future "verify project" command would call.
//  Returns an empty vector when every stored index is in range.
// ---------------------------------------------------------------------------
[[nodiscard]] std::vector<std::string> danglingReferences(const Project&);

} // namespace domain
