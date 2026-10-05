#include "Reindex.hpp"

#include <algorithm>
#include <string>

namespace domain {

std::string_view toString(EntityKind k) noexcept {
    switch (k) {
        case EntityKind::Variable:    return "variable";
        case EntityKind::DerivedType: return "derived type";
        case EntityKind::Pou:         return "POU";
        case EntityKind::Section:     return "section";
    }
    return "entity";
}

namespace {

// An index outside the mapping is one the mapping has nothing to say about —
// typically kNoIndex, or a reference into a vector that grew after the mapping
// was built. Leave it alone rather than guess.
Index mapScalar(const std::vector<Index>& m, Index i) {
    return i < m.size() ? m[i] : i;
}

void mapList(const std::vector<Index>& m, std::vector<Index>& list) {
    std::vector<Index> out;
    out.reserve(list.size());
    for (Index i : list) {
        const Index n = mapScalar(m, i);
        if (n != kNoIndex) out.push_back(n);      // a dead entry drops out
    }
    list.swap(out);
}

} // namespace

std::vector<Index> mappingForRemoval(std::size_t count, Index victim) {
    std::vector<Index> m(count);
    for (Index i = 0; i < count; ++i)
        m[i] = (i == victim) ? kNoIndex : (i > victim ? i - 1 : i);
    return m;
}

std::vector<Index> mappingForInsertion(std::size_t count, Index position) {
    std::vector<Index> m(count);
    for (Index i = 0; i < count; ++i) m[i] = (i >= position) ? i + 1 : i;
    return m;
}

// ---------------------------------------------------------------------------
//  THE EXHAUSTIVE LIST.
//
//  Every Index field in ProjectModel.hpp appears below, grouped by what it
//  points at. If you add one to the model, add it here. danglingReferences()
//  and the delete test exist to catch you if you do not.
// ---------------------------------------------------------------------------
void remapReferences(Project& p, EntityKind kind, const std::vector<Index>& m) {
    switch (kind) {
        // --- things that point at a Variable --------------------------------
        case EntityKind::Variable:
            for (auto& d : p.derivedTypes) mapList(m, d.fields);
            for (auto& pou : p.pous) {
                mapList(m, pou.parameters);
                mapList(m, pou.locals);
            }
            break;

        // --- things that point at a DerivedType -----------------------------
        case EntityKind::DerivedType:
            for (auto& v : p.variables) {
                v.type.derivedIndex = mapScalar(m, v.type.derivedIndex);
                // Variable::owner is a DDT index only for a member of one; for
                // every other scope it addresses p.pous and must not be touched
                // here. Getting this wrong silently re-parents declarations.
                if (v.scope == VariableScope::DerivedMember)
                    v.owner = mapScalar(m, v.owner);
            }
            break;

        // --- things that point at a Pou -------------------------------------
        case EntityKind::Pou:
            for (auto& v : p.variables) {
                v.type.fbTypeIndex = mapScalar(m, v.type.fbTypeIndex);
                if (v.scope != VariableScope::DerivedMember)
                    v.owner = mapScalar(m, v.owner);
            }
            for (auto& s : p.sections) s.owner = mapScalar(m, s.owner);
            for (auto& pou : p.pous) {
                pou.parent = mapScalar(m, pou.parent);
                mapList(m, pou.children);
            }
            for (auto& l : p.libraries) l.pouIndex = mapScalar(m, l.pouIndex);
            break;

        // --- things that point at a Section ---------------------------------
        case EntityKind::Section:
            for (auto& pou : p.pous) mapList(m, pou.sections);
            for (auto& t : p.tasks)  mapList(m, t.sections);
            break;
    }

    // The name maps hold indices too. They are cheap to rebuild and expensive to
    // patch correctly, so they are rebuilt.
    p.buildIndices();
}

// ---------------------------------------------------------------------------
namespace {

// Where does this index currently sit in the lists that hold it? Recorded
// before the erase, so undo can put it back in the same place rather than at
// the end — the order of a task's sections is the scan order of the program.
std::vector<Membership> collectMemberships(const Project& p, EntityKind kind, Index victim) {
    std::vector<Membership> out;
    auto scan = [&](const std::vector<Index>& list, Membership::List which, Index container) {
        for (Index i = 0; i < list.size(); ++i)
            if (list[i] == victim) out.push_back({which, container, i});
    };

    switch (kind) {
        case EntityKind::Variable:
            for (Index d = 0; d < p.derivedTypes.size(); ++d)
                scan(p.derivedTypes[d].fields, Membership::List::DerivedFields, d);
            for (Index i = 0; i < p.pous.size(); ++i) {
                scan(p.pous[i].parameters, Membership::List::PouParameters, i);
                scan(p.pous[i].locals,     Membership::List::PouLocals,     i);
            }
            break;
        case EntityKind::Section:
            for (Index i = 0; i < p.pous.size(); ++i)
                scan(p.pous[i].sections, Membership::List::PouSections, i);
            for (Index t = 0; t < p.tasks.size(); ++t)
                scan(p.tasks[t].sections, Membership::List::TaskSections, t);
            break;
        case EntityKind::Pou:
            for (Index i = 0; i < p.pous.size(); ++i)
                scan(p.pous[i].children, Membership::List::PouChildren, i);
            break;
        case EntityKind::DerivedType:
            break;   // nothing in the model holds a list of derived types
    }
    return out;
}

void insertInto(std::vector<Index>& list, Index position, Index value) {
    const auto at = std::min<std::size_t>(position, list.size());
    list.insert(list.begin() + static_cast<std::ptrdiff_t>(at), value);
}

} // namespace

RemovedEntity eraseEntity(Project& p, EntityKind kind, Index victim) {
    RemovedEntity r;
    r.kind        = kind;
    r.position    = victim;
    r.memberships = collectMemberships(p, kind, victim);

    std::size_t count = 0;
    switch (kind) {
        case EntityKind::Variable:
            count = p.variables.size();
            r.payload = p.variables[victim];
            p.variables.erase(p.variables.begin() + static_cast<std::ptrdiff_t>(victim));
            break;
        case EntityKind::DerivedType:
            count = p.derivedTypes.size();
            r.payload = p.derivedTypes[victim];
            p.derivedTypes.erase(p.derivedTypes.begin() + static_cast<std::ptrdiff_t>(victim));
            break;
        case EntityKind::Pou:
            count = p.pous.size();
            r.payload = p.pous[victim];
            p.pous.erase(p.pous.begin() + static_cast<std::ptrdiff_t>(victim));
            break;
        case EntityKind::Section:
            count = p.sections.size();
            r.payload = p.sections[victim];
            p.sections.erase(p.sections.begin() + static_cast<std::ptrdiff_t>(victim));
            break;
    }

    remapReferences(p, kind, mappingForRemoval(count, victim));
    return r;
}

void restoreEntity(Project& p, const RemovedEntity& r) {
    // Step 1: make room. Everything from the old position up moves back where
    // it was, so the recorded container indices and positions are meaningful
    // again.
    std::size_t count = 0;
    switch (r.kind) {
        case EntityKind::Variable:    count = p.variables.size();    break;
        case EntityKind::DerivedType: count = p.derivedTypes.size(); break;
        case EntityKind::Pou:         count = p.pous.size();         break;
        case EntityKind::Section:     count = p.sections.size();     break;
    }
    remapReferences(p, r.kind, mappingForInsertion(count, r.position));

    // Step 2: put the entity itself back.
    switch (r.kind) {
        case EntityKind::Variable:
            p.variables.insert(p.variables.begin() + static_cast<std::ptrdiff_t>(r.position),
                               std::get<Variable>(r.payload));
            break;
        case EntityKind::DerivedType:
            p.derivedTypes.insert(p.derivedTypes.begin() + static_cast<std::ptrdiff_t>(r.position),
                                  std::get<DerivedType>(r.payload));
            break;
        case EntityKind::Pou:
            p.pous.insert(p.pous.begin() + static_cast<std::ptrdiff_t>(r.position),
                          std::get<Pou>(r.payload));
            break;
        case EntityKind::Section:
            p.sections.insert(p.sections.begin() + static_cast<std::ptrdiff_t>(r.position),
                              std::get<Section>(r.payload));
            break;
    }

    // Step 3: re-thread it into the lists that used to hold it, at the same
    // offsets. Ascending order of position, so earlier insertions do not shift
    // the ones recorded after them.
    auto ordered = r.memberships;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Membership& a, const Membership& b) { return a.position < b.position; });

    for (const auto& mem : ordered) {
        switch (mem.list) {
            case Membership::List::DerivedFields:
                if (mem.container < p.derivedTypes.size())
                    insertInto(p.derivedTypes[mem.container].fields, mem.position, r.position);
                break;
            case Membership::List::PouParameters:
                if (mem.container < p.pous.size())
                    insertInto(p.pous[mem.container].parameters, mem.position, r.position);
                break;
            case Membership::List::PouLocals:
                if (mem.container < p.pous.size())
                    insertInto(p.pous[mem.container].locals, mem.position, r.position);
                break;
            case Membership::List::PouSections:
                if (mem.container < p.pous.size())
                    insertInto(p.pous[mem.container].sections, mem.position, r.position);
                break;
            case Membership::List::PouChildren:
                if (mem.container < p.pous.size())
                    insertInto(p.pous[mem.container].children, mem.position, r.position);
                break;
            case Membership::List::TaskSections:
                if (mem.container < p.tasks.size())
                    insertInto(p.tasks[mem.container].sections, mem.position, r.position);
                break;
        }
    }

    p.buildIndices();
}

// ---------------------------------------------------------------------------
std::vector<std::string> danglingReferences(const Project& p) {
    std::vector<std::string> out;
    auto check = [&](Index i, std::size_t limit, const char* what) {
        if (i != kNoIndex && i >= limit) out.emplace_back(what);
    };

    for (Index i = 0; i < p.variables.size(); ++i) {
        const auto& v = p.variables[i];
        const std::string tag = "variable " + std::to_string(i);
        check(v.type.derivedIndex, p.derivedTypes.size(), (tag + ".type.derivedIndex").c_str());
        check(v.type.fbTypeIndex,  p.pous.size(),         (tag + ".type.fbTypeIndex").c_str());
        check(v.owner,
              v.scope == VariableScope::DerivedMember ? p.derivedTypes.size() : p.pous.size(),
              (tag + ".owner").c_str());
    }
    for (Index i = 0; i < p.derivedTypes.size(); ++i)
        for (Index f : p.derivedTypes[i].fields)
            check(f, p.variables.size(), ("derivedType " + std::to_string(i) + ".fields").c_str());

    for (Index i = 0; i < p.pous.size(); ++i) {
        const auto& pou = p.pous[i];
        const std::string tag = "pou " + std::to_string(i);
        check(pou.parent, p.pous.size(), (tag + ".parent").c_str());
        for (Index s : pou.sections)   check(s, p.sections.size(),  (tag + ".sections").c_str());
        for (Index v : pou.parameters) check(v, p.variables.size(), (tag + ".parameters").c_str());
        for (Index v : pou.locals)     check(v, p.variables.size(), (tag + ".locals").c_str());
        for (Index c : pou.children)   check(c, p.pous.size(),      (tag + ".children").c_str());
    }
    for (Index i = 0; i < p.sections.size(); ++i)
        check(p.sections[i].owner, p.pous.size(),
              ("section " + std::to_string(i) + ".owner").c_str());

    for (Index i = 0; i < p.tasks.size(); ++i)
        for (Index s : p.tasks[i].sections)
            check(s, p.sections.size(), ("task " + std::to_string(i) + ".sections").c_str());

    for (Index i = 0; i < p.libraries.size(); ++i)
        check(p.libraries[i].pouIndex, p.pous.size(),
              ("library " + std::to_string(i) + ".pouIndex").c_str());

    // The name maps are references too, and the easiest ones to forget: they
    // are rebuilt rather than patched, so an omitted buildIndices() leaves them
    // pointing at whatever now occupies the old slot. Nothing crashes, the
    // export is unaffected, and name resolution quietly returns the wrong
    // entity - which is how the case-insensitivity bug behaved as well.
    auto checkMap = [&](const std::unordered_map<SymbolId, Index>& map,
                        std::size_t limit, auto&& nameAt, const char* what) {
        for (const auto& [symbol, index] : map) {
            if (index >= limit) { out.emplace_back(std::string(what) + ": index out of range"); continue; }
            if (nameAt(index) != symbol)
                out.emplace_back(std::string(what) + ": '" + std::string(p.strings.text(symbol))
                                 + "' resolves to '" + std::string(p.strings.text(nameAt(index))) + "'");
        }
    };
    checkMap(p.variableByName, p.variables.size(),
             [&](Index i) { return p.variables[i].name; }, "variableByName");
    checkMap(p.typeByName, p.derivedTypes.size(),
             [&](Index i) { return p.derivedTypes[i].name; }, "typeByName");
    checkMap(p.pouByName, p.pous.size(),
             [&](Index i) { return p.pous[i].name; }, "pouByName");

    return out;
}

} // namespace domain
