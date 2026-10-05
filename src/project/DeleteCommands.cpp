#include "DeleteCommands.hpp"

#include <algorithm>

namespace project {

using namespace domain;

namespace {

std::string nameOf(const Project& p, EntityKind kind, Index i) {
    switch (kind) {
        case EntityKind::Variable:
            return i < p.variables.size() ? std::string(p.strings.text(p.variables[i].name)) : std::string{};
        case EntityKind::DerivedType:
            return i < p.derivedTypes.size() ? std::string(p.strings.text(p.derivedTypes[i].name)) : std::string{};
        case EntityKind::Pou:
            return i < p.pous.size() ? std::string(p.strings.text(p.pous[i].name)) : std::string{};
        case EntityKind::Section:
            return i < p.sections.size() ? std::string(p.strings.text(p.sections[i].name)) : std::string{};
    }
    return {};
}

std::size_t vectorSize(const Project& p, EntityKind kind) {
    switch (kind) {
        case EntityKind::Variable:    return p.variables.size();
        case EntityKind::DerivedType: return p.derivedTypes.size();
        case EntityKind::Pou:         return p.pous.size();
        case EntityKind::Section:     return p.sections.size();
    }
    return 0;
}

// Where a variable is declared, spelled out. "Gaz_1.Step" says more than
// "variable 4127", and the whole point of refusing is to be actionable.
std::string qualify(const Project& p, const Variable& v) {
    std::string owner;
    if (v.scope == VariableScope::DerivedMember) {
        if (v.owner < p.derivedTypes.size()) owner = p.strings.text(p.derivedTypes[v.owner].name);
    } else if (v.owner < p.pous.size()) {
        owner = p.strings.text(p.pous[v.owner].name);
    }
    const std::string self(p.strings.text(v.name));
    return owner.empty() ? self : owner + "." + self;
}

} // namespace

std::string firstUserOfDerivedType(const Project& p, Index ddt) {
    for (const auto& v : p.variables) {
        // A member of the type is part of it, not a user of it: a DDT whose own
        // fields blocked its deletion could never be deleted at all.
        if (v.scope == VariableScope::DerivedMember && v.owner == ddt) continue;
        if (v.type.derivedIndex == ddt) return qualify(p, v);
    }
    return {};
}

std::string firstInstanceOfBlock(const Project& p, Index pou) {
    for (const auto& v : p.variables) {
        if (v.owner == pou && v.scope != VariableScope::DerivedMember) continue;   // its own declarations
        if (v.type.fbTypeIndex == pou) return qualify(p, v);
    }
    return {};
}

// ---------------------------------------------------------------------------
RemoveEntityCommand::RemoveEntityCommand(ProjectPtr project, EntityKind kind, Index index)
    : project_(std::move(project)), kind_(kind), index_(index) {
    if (project_) name_ = nameOf(*project_, kind_, index_);
}

// Lot API 5 : en francais (l'historique, la barre d'etat, la confirmation).
namespace {
std::string_view kindLabel(EntityKind k) noexcept {
    switch (k) {
        case EntityKind::Variable:    return "la variable";
        case EntityKind::DerivedType: return "le type";
        case EntityKind::Pou:         return "le POU";
        case EntityKind::Section:     return "la section";
    }
    return "l'\xC3\xA9l\xC3\xA9ment";
}
} // namespace

std::string RemoveEntityCommand::label() const {
    std::string what(kindLabel(kind_));
    return name_.empty() ? "Supprimer " + what : "Supprimer " + what + " " + name_;
}

// ---------------------------------------------------------------------------
core::Status RemoveEntityCommand::refusalReason() const {
    if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "aucun projet");
    const auto& p = *project_;

    if (index_ >= vectorSize(p, kind_))
        return core::fail(core::ErrorCode::OutOfRange,
                          std::string(kindLabel(kind_)) + " n'existe plus");

    if (kind_ == EntityKind::DerivedType) {
        if (auto user = firstUserOfDerivedType(p, index_); !user.empty())
            return core::fail(core::ErrorCode::IncompleteProject,
                              "'" + name_ + "' est encore le type de " + user
                                  + ". Changer ou supprimer d'abord ce qui l'utilise.");
    }
    if (kind_ == EntityKind::Pou) {
        const auto& pou = p.pous[index_];
        if (pou.kind == PouKind::FunctionBlockType) {
            if (auto inst = firstInstanceOfBlock(p, index_); !inst.empty())
                return core::fail(core::ErrorCode::IncompleteProject,
                                  "'" + name_ + "' a encore l'instance " + inst
                                      + ". Supprimer d'abord ses instances.");
        }
    }
    return core::ok();
}

// ---------------------------------------------------------------------------
std::vector<std::pair<EntityKind, Index>> RemoveEntityCommand::plan() const {
    std::vector<std::pair<EntityKind, Index>> out;
    if (!project_) return out;
    const auto& p = *project_;
    if (index_ >= vectorSize(p, kind_)) return out;

    switch (kind_) {
        case EntityKind::Variable:
            out.emplace_back(EntityKind::Variable, index_);
            break;

        case EntityKind::Section: {
            out.emplace_back(EntityKind::Section, index_);
            // AddSectionCommand wraps a bare task section in a POU of its own so
            // the explorer stays uniform. Removing the section and leaving the
            // wrapper would leave an empty node in the tree that owns nothing.
            const Index owner = p.sections[index_].owner;
            if (owner != kNoIndex && owner < p.pous.size()) {
                const auto& pou = p.pous[owner];
                if (pou.kind == PouKind::Section && pou.sections.size() == 1
                    && pou.sections.front() == index_)
                    out.emplace_back(EntityKind::Pou, owner);
            }
            break;
        }

        case EntityKind::DerivedType:
            for (Index f : p.derivedTypes[index_].fields)
                out.emplace_back(EntityKind::Variable, f);
            out.emplace_back(EntityKind::DerivedType, index_);
            break;

        case EntityKind::Pou: {
            // Nested program units are contained, not referenced: a unit inside
            // a unit does not outlive it.
            std::vector<Index> queue{index_};
            for (std::size_t at = 0; at < queue.size(); ++at) {
                const Index i = queue[at];
                if (i >= p.pous.size()) continue;
                for (Index c : p.pous[i].children) queue.push_back(c);
            }
            for (Index i : queue) {
                if (i >= p.pous.size()) continue;
                const auto& pou = p.pous[i];
                for (Index s : pou.sections)   out.emplace_back(EntityKind::Section, s);
                for (Index v : pou.parameters) out.emplace_back(EntityKind::Variable, v);
                for (Index v : pou.locals)     out.emplace_back(EntityKind::Variable, v);
                out.emplace_back(EntityKind::Pou, i);
            }
            // Belt as well as braces. The lists above and the owner fields below
            // say the same thing in a well-formed project, and the parser keeps
            // them in step. But a section left behind because it was not in its
            // POU's list would keep an owner index pointing at a POU that no
            // longer exists, and dangling scalars are what this whole design
            // exists to prevent. The duplicates are removed below.
            for (Index i : queue) {
                for (Index s = 0; s < p.sections.size(); ++s)
                    if (p.sections[s].owner == i) out.emplace_back(EntityKind::Section, s);
                for (Index v = 0; v < p.variables.size(); ++v)
                    if (p.variables[v].scope != VariableScope::DerivedMember
                        && p.variables[v].owner == i)
                        out.emplace_back(EntityKind::Variable, v);
            }
            break;
        }
    }

    // Duplicates would be a double erase, and a section listed by both its POU
    // and its task is easy to produce. One entry per entity.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());

    // Erase leaves first and, within a kind, from the highest index down: an
    // erase renumbers everything above it, so anything still to be erased must
    // sit below. Sections and variables are erased before the POUs that held
    // them only for readability - the kinds do not disturb each other's
    // numbering - but the descending order within a kind is load-bearing.
    auto rank = [](EntityKind k) {
        switch (k) {
            case EntityKind::Section:     return 0;
            case EntityKind::Variable:    return 1;
            case EntityKind::DerivedType: return 2;
            case EntityKind::Pou:         return 3;
        }
        return 4;
    };
    std::sort(out.begin(), out.end(), [&](const auto& a, const auto& b) {
        if (rank(a.first) != rank(b.first)) return rank(a.first) < rank(b.first);
        return a.second > b.second;
    });
    return out;
}

std::vector<std::string> RemoveEntityCommand::collateralDamage() const {
    std::vector<std::string> out;
    if (!project_) return out;
    const auto& p = *project_;

    for (const auto& [kind, i] : plan()) {
        if (kind == kind_ && i == index_) continue;      // the target itself
        auto n = nameOf(p, kind, i);
        // « POU UnitC », « variable Gc_1 » : le genre sans article (la liste se lit
        // comme un inventaire, et delete_test y cherche « POU UnitC »).
        const std::string_view noun = kind == EntityKind::DerivedType ? std::string_view("type") : toString(kind);
        out.push_back(std::string(noun) + " " + (n.empty() ? "(sans nom)" : n));
    }

    // Not deleted, but the reason people undo. The analyzer's count is the best
    // number available and it is named as such rather than presented as fact.
    if (kind_ == EntityKind::Variable && index_ < p.variables.size()) {
        const auto refs = p.variables[index_].referenceCount;
        if (refs > 0)
            out.push_back("nomm\xC3\xA9" "e " + std::to_string(refs)
                          + " fois dans le code, d'apr\xC3\xA8s la derni\xC3\xA8re analyse");
    }
    return out;
}

// ---------------------------------------------------------------------------
core::Status RemoveEntityCommand::execute() {
    if (auto r = refusalReason(); !r) return r;       // nothing has been touched yet
    auto& p = *project_;

    const auto steps = plan();
    if (steps.empty())
        return core::fail(core::ErrorCode::OutOfRange, "rien \xC3\xA0 supprimer");

    // A library entry that named this DFB would otherwise survive it and export
    // a reference to a block that is not there.
    removedLibraryEntries_.clear();
    if (kind_ == EntityKind::Pou) {
        for (std::size_t i = p.libraries.size(); i-- > 0;)
            if (p.libraries[i].pouIndex == index_) {
                removedLibraryEntries_.emplace_back(static_cast<Index>(i), p.libraries[i]);
                p.libraries.erase(p.libraries.begin() + static_cast<std::ptrdiff_t>(i));
            }
    }

    removed_.clear();
    removed_.reserve(steps.size());
    for (const auto& [kind, i] : steps)
        removed_.push_back(eraseEntity(p, kind, i));

    return core::ok();
}

core::Status RemoveEntityCommand::undo() {
    if (!project_ || removed_.empty()) return core::ok();
    auto& p = *project_;

    // Reverse order: each restoreEntity is the inverse of one eraseEntity, and
    // the inverse of a sequence is the reversed sequence of inverses.
    for (std::size_t i = removed_.size(); i-- > 0;)
        restoreEntity(p, removed_[i]);

    // The entries were collected from the back forwards, so replaying them in
    // that same order puts each one back below the ones already restored.
    for (const auto& [at, entry] : removedLibraryEntries_) {
        const auto pos = std::min<std::size_t>(at, p.libraries.size());
        p.libraries.insert(p.libraries.begin() + static_cast<std::ptrdiff_t>(pos), entry);
    }

    removed_.clear();
    removedLibraryEntries_.clear();
    return core::ok();
}

} // namespace project
