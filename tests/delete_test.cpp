// =============================================================================
//  tests/delete_test.cpp — removing things from the middle, and taking it back
// -----------------------------------------------------------------------------
//  Deletion is the first operation in this program that renumbers the model.
//  Creation appends and undo truncates, so nothing ever moved; removing element
//  k from a flat vector moves everything above it, and every relationship in
//  this model is a 32-bit index into a flat vector.
//
//  So this test does not check that "delete worked". It checks the two things
//  that would let a bad delete reach a customer's file:
//
//   1. REFERENTIAL INTEGRITY. After every operation, walk the whole model and
//      assert that no stored index points outside its vector, and that the
//      references still name the entities they named before. An index that has
//      slipped by one still parses, still exports, and quietly attaches a
//      comment to the wrong variable.
//
//   2. UNDO IS EXACT, MEASURED AT THE FILE. Export the untouched project, delete,
//      undo, export again, and compare the two documents BYTE FOR BYTE. Counting
//      entities is not enough: a section restored at the end of MAST instead of
//      in the middle gives the same count and a different scan order. The bytes
//      catch that; the counts do not.
//
//  The byte comparison is the same instrument roundtrip_test uses on the writer,
//  pointed at a different risk.
// =============================================================================
#include "../src/core/Command.hpp"
#include "../src/domain/Reindex.hpp"
#include "../src/export/XpgWriter.hpp"
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/DeleteCommands.hpp"
#include "../src/project/EditCommands.hpp"
#include "../src/project/ProjectStore.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>

using namespace project;
using namespace domain;

namespace {

std::shared_ptr<Project> importProject(const std::string& path) {
    core::EventBus bus;
    importer::ProjectImporter imp(bus);
    auto res = imp.importFile(path);
    if (!res) {
        std::fprintf(stderr, "import failed: %s\n", res.error().message().c_str());
        return nullptr;
    }
    return res->project;
}

std::string exportOf(const Project& p) {
    exporter::XpgWriteOptions opts;
    opts.dateTime = "date_and_time#2026-1-1-0:00:00";   // fixed, so passes compare
    auto out = exporter::writeXpg(p, opts);
    assert(out && "writing must succeed");
    return *out;
}

void assertSound(const Project& p, const char* when) {
    const auto bad = domain::danglingReferences(p);
    if (!bad.empty()) {
        std::fprintf(stderr, "dangling references %s:\n", when);
        for (const auto& b : bad) std::fprintf(stderr, "  %s\n", b.c_str());
    }
    assert(bad.empty() && "no stored index may point outside its vector");
}

// The relationships, spelled out by NAME rather than by index. This is the check
// that catches an index which shifted by one: the counts are identical and the
// names are not.
struct Relations {
    std::map<std::string, std::string>              sectionOwner;    // section -> POU
    std::map<std::string, std::vector<std::string>> taskSections;    // task -> sections, in order
    std::map<std::string, std::vector<std::string>> ddtFields;       // DDT -> fields, in order
    std::map<std::string, std::vector<std::string>> pouLocals;
    std::map<std::string, std::string>              variableType;    // variable -> type name
    // Read from Variable::owner itself, resolved through the scope. The lists
    // above cannot stand in for this: a remap that renumbers owner against the
    // wrong vector leaves every list intact and every owner field wrong, which
    // is invisible until the analyzer or the simulator asks who declares what.
    std::map<std::string, std::string>              variableOwner;
    std::map<std::string, std::string>              pouParent;       // POU -> parent POU
    bool operator==(const Relations&) const = default;
};

Relations relationsOf(const Project& p) {
    Relations r;
    auto text = [&](SymbolId id) { return std::string(p.strings.text(id)); };

    for (Index i = 0; i < p.sections.size(); ++i) {
        const auto& s = p.sections[i];
        const std::string owner = (s.owner != kNoIndex && s.owner < p.pous.size())
                                      ? text(p.pous[s.owner].name) : std::string("<task>");
        // Several sections are called "Init", one per program unit, so the key
        // has to carry the owner as well - roundtrip_test learned that first.
        r.sectionOwner[owner + "::" + text(s.name)] = owner;
    }
    for (const auto& t : p.tasks) {
        auto& list = r.taskSections[text(t.name)];
        for (Index s : t.sections)
            list.push_back(s < p.sections.size() ? text(p.sections[s].name) : "<dangling>");
    }
    for (const auto& d : p.derivedTypes) {
        auto& list = r.ddtFields[text(d.name)];
        for (Index f : d.fields)
            list.push_back(f < p.variables.size() ? text(p.variables[f].name) : "<dangling>");
    }
    for (const auto& pou : p.pous) {
        auto& list = r.pouLocals[text(pou.name)];
        for (Index v : pou.locals)
            list.push_back(v < p.variables.size() ? text(p.variables[v].name) : "<dangling>");
        r.pouParent[text(pou.name)] =
            pou.parent == kNoIndex ? "<none>"
                                   : (pou.parent < p.pous.size() ? text(p.pous[pou.parent].name)
                                                                 : "<dangling>");
    }
    for (const auto& v : p.variables) {
        r.variableType[text(v.name)] = text(v.type.name);

        std::string owner = "<none>";
        if (v.scope == VariableScope::DerivedMember) {
            owner = v.owner < p.derivedTypes.size()
                        ? "ddt:" + text(p.derivedTypes[v.owner].name) : "ddt:<dangling>";
        } else if (v.owner != kNoIndex) {
            owner = v.owner < p.pous.size()
                        ? "pou:" + text(p.pous[v.owner].name) : "pou:<dangling>";
        }
        r.variableOwner[text(v.name)] = owner;
    }
    return r;
}

Index findSection(const Project& p, std::string_view name) {
    for (Index i = 0; i < p.sections.size(); ++i)
        if (p.strings.text(p.sections[i].name) == name) return i;
    return kNoIndex;
}

Index findPou(const Project& p, std::string_view name) {
    for (Index i = 0; i < p.pous.size(); ++i)
        if (p.strings.text(p.pous[i].name) == name) return i;
    return kNoIndex;
}

// A DDT nothing is declared with, so it can actually be deleted.
Index unusedDerivedType(const Project& p) {
    for (Index i = 0; i < p.derivedTypes.size(); ++i)
        if (firstUserOfDerivedType(p, i).empty()) return i;
    return kNoIndex;
}

Index usedDerivedType(const Project& p) {
    for (Index i = 0; i < p.derivedTypes.size(); ++i)
        if (!firstUserOfDerivedType(p, i).empty()) return i;
    return kNoIndex;
}

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : "tests/fixtures/MAST.XPG";

    // =========================================================================
    //  Part 1 - the mapping itself, before any project is involved.
    // =========================================================================
    {
        const auto removal = mappingForRemoval(5, 2);
        assert(removal[0] == 0 && removal[1] == 1);
        assert(removal[2] == kNoIndex && "the victim has no new home");
        assert(removal[3] == 2 && removal[4] == 3 && "everything above slides down one");

        const auto insertion = mappingForInsertion(4, 2);
        assert(insertion[0] == 0 && insertion[1] == 1);
        assert(insertion[2] == 3 && insertion[3] == 4 && "and back up again");
    }

    // =========================================================================
    //  Part 2 - a synthetic project, where every index is known by hand.
    // =========================================================================
    {
        auto p = ProjectStore::createEmpty("Machine", "BMXP342020");
        core::CommandStack stack;

        assert(stack.push(std::make_unique<AddSectionCommand>(p, "First",  "MAST", PouLanguage::ST)).has_value());
        assert(stack.push(std::make_unique<AddSectionCommand>(p, "Second", "MAST", PouLanguage::ST)).has_value());
        assert(stack.push(std::make_unique<AddSectionCommand>(p, "Third",  "MAST", PouLanguage::ST)).has_value());
        assert(p->sections.size() == 3 && p->tasks.front().sections.size() == 3);

        const auto before = relationsOf(*p);

        // Delete the MIDDLE one. Deleting the last would prove nothing: it is
        // the case the old truncate-only design already handled.
        assert(stack.push(std::make_unique<RemoveEntityCommand>(
                   p, EntityKind::Section, 1)).has_value());
        assertSound(*p, "after deleting a middle section");
        assert(p->sections.size() == 2);
        assert(p->strings.text(p->sections[0].name) == "First");
        assert(p->strings.text(p->sections[1].name) == "Third"
               && "the section above the hole must have slid down");
        assert(p->tasks.front().sections.size() == 2);
        assert(p->tasks.front().sections[1] == 1
               && "the task's own list must follow the renumbering");
        // The synthetic POU that wrapped the bare task section goes with it.
        assert(findPou(*p, "Second") == kNoIndex && "an empty wrapper POU is not left behind");

        assert(stack.undo().has_value());
        assertSound(*p, "after undoing a section deletion");
        assert(p->sections.size() == 3);
        assert(relationsOf(*p) == before && "undo must restore every relationship, by name");
        assert(p->strings.text(p->sections[1].name) == "Second"
               && "and put it back where it was, not at the end");

        // Order inside the task is the scan order of the program: a restore that
        // appends instead of inserting changes what the PLC does.
        const auto& order = p->tasks.front().sections;
        assert(p->strings.text(p->sections[order[0]].name) == "First");
        assert(p->strings.text(p->sections[order[1]].name) == "Second");
        assert(p->strings.text(p->sections[order[2]].name) == "Third");
    }

    // =========================================================================
    //  Part 3 - refusing, and refusing without touching anything.
    // =========================================================================
    {
        auto p = ProjectStore::createEmpty("Machine", "BMXP342020");
        core::CommandStack stack;

        assert(stack.push(std::make_unique<AddDerivedTypeCommand>(p, "ST_Step", "0.01")).has_value());
        assert(stack.push(std::make_unique<AddFunctionBlockCommand>(p, "DFB_Motor", "1.00")).has_value());

        {
            AddVariableCommand::Spec s;
            s.name = "state"; s.type = "BOOL"; s.scope = VariableScope::DerivedMember; s.owner = 0;
            assert(stack.push(std::make_unique<AddVariableCommand>(p, s)).has_value());
        }
        {
            AddVariableCommand::Spec s;                 // an instance of the DDT
            s.name = "gCabinet"; s.type = "ST_Step"; s.scope = VariableScope::Global;
            assert(stack.push(std::make_unique<AddVariableCommand>(p, s)).has_value());
        }
        {
            AddVariableCommand::Spec s;                 // an instance of the DFB
            s.name = "gMotor"; s.type = "DFB_Motor"; s.scope = VariableScope::Global;
            assert(stack.push(std::make_unique<AddVariableCommand>(p, s)).has_value());
        }

        const auto guarded = relationsOf(*p);
        const auto counts  = std::make_tuple(p->variables.size(), p->derivedTypes.size(),
                                             p->pous.size(), p->libraries.size());

        // A type that something is declared with is not deletable: doing it
        // would be a silent type change, not a deletion.
        auto refusedDdt = stack.push(std::make_unique<RemoveEntityCommand>(
            p, EntityKind::DerivedType, 0));
        assert(!refusedDdt.has_value() && "a DDT still in use must be refused");
        assert(refusedDdt.error().message().find("gCabinet") != std::string::npos
               && "and the message must name what is using it");

        const Index motor = findPou(*p, "DFB_Motor");
        auto refusedDfb = stack.push(std::make_unique<RemoveEntityCommand>(
            p, EntityKind::Pou, motor));
        assert(!refusedDfb.has_value() && "a DFB with a live instance must be refused");
        assert(refusedDfb.error().message().find("gMotor") != std::string::npos);

        assert(relationsOf(*p) == guarded && "a refused command must change nothing");
        assert(counts == std::make_tuple(p->variables.size(), p->derivedTypes.size(),
                                         p->pous.size(), p->libraries.size()));

        // Remove the instances, and now the type can go - with its own members,
        // which are part of it rather than users of it.
        const Index cabinet = [&] {
            for (Index i = 0; i < p->variables.size(); ++i)
                if (p->strings.text(p->variables[i].name) == "gCabinet") return i;
            return kNoIndex;
        }();
        assert(stack.push(std::make_unique<RemoveEntityCommand>(
                   p, EntityKind::Variable, cabinet)).has_value());
        assertSound(*p, "after deleting an instance");

        const auto vars = p->variables.size();
        assert(stack.push(std::make_unique<RemoveEntityCommand>(
                   p, EntityKind::DerivedType, 0)).has_value());
        assertSound(*p, "after deleting an unused DDT");
        assert(p->derivedTypes.empty());
        assert(p->variables.size() == vars - 1 && "the DDT's member goes with it");

        assert(stack.undo().has_value());
        assertSound(*p, "after undoing a DDT deletion");
        assert(p->derivedTypes.size() == 1);
        assert(p->derivedTypes[0].fields.size() == 1 && "and its member comes back with it");
        assert(p->strings.text(p->variables[p->derivedTypes[0].fields[0]].name) == "state");
    }

    // =========================================================================
    //  Part 3b - the one field with two meanings.
    // -------------------------------------------------------------------------
    //  Variable::owner indexes derivedTypes for a member of a DDT and pous for
    //  everything else. remapReferences has to look at the scope before it
    //  touches that field, and getting it wrong does not crash: it silently
    //  moves declarations from one owner to another.
    //
    //  A mutation test found that the rest of this file could not see the bug.
    //  Deleting a DDT only disturbs owners in the DDT numbering, and in the
    //  earlier cases the POU indices in play never overlapped the DDT indices in
    //  play, so a mis-remapped owner landed on kNoIndex or on itself. The two
    //  ranges have to OVERLAP for the confusion to show, so they are made to.
    // =========================================================================
    {
        auto p = ProjectStore::createEmpty("Machine", "BMXP342020");
        core::CommandStack stack;

        // Two program units: POU indices 0 and 1.
        assert(stack.push(std::make_unique<AddProgramUnitCommand>(p, "UnitZero", "MAST")).has_value());
        assert(stack.push(std::make_unique<AddProgramUnitCommand>(p, "UnitOne",  "MAST")).has_value());
        const Index unitZero = findPou(*p, "UnitZero");
        const Index unitOne  = findPou(*p, "UnitOne");
        assert(unitZero == 0 && unitOne == 1 && "the construction depends on these indices");

        // Two derived types: DDT indices 0 and 1. The ranges now overlap.
        assert(stack.push(std::make_unique<AddDerivedTypeCommand>(p, "ST_Alpha", "0.01")).has_value());
        assert(stack.push(std::make_unique<AddDerivedTypeCommand>(p, "ST_Beta",  "0.01")).has_value());

        {   // owned by POU 1 - the index that a bad remap would move to 0
            AddVariableCommand::Spec s;
            s.name = "localOfUnitOne"; s.type = "INT";
            s.scope = VariableScope::Local; s.owner = unitOne;
            assert(stack.push(std::make_unique<AddVariableCommand>(p, s)).has_value());
        }
        {   // a member of DDT 1 - the mirror case
            AddVariableCommand::Spec s;
            s.name = "fieldOfBeta"; s.type = "INT";
            s.scope = VariableScope::DerivedMember; s.owner = 1;
            assert(stack.push(std::make_unique<AddVariableCommand>(p, s)).has_value());
        }

        // A block type at POU index 2, and instances of both surviving types.
        // Without these, TypeRef::derivedIndex and TypeRef::fbTypeIndex are
        // never pointed at anything that has to move, and dropping either
        // remap goes unnoticed - which a mutation run proved.
        assert(stack.push(std::make_unique<AddFunctionBlockCommand>(p, "DFB_Pump", "1.00")).has_value());
        const Index pump = findPou(*p, "DFB_Pump");
        assert(pump == 2);
        {
            AddVariableCommand::Spec s;
            s.name = "gBeta"; s.type = "ST_Beta"; s.scope = VariableScope::Global;
            assert(stack.push(std::make_unique<AddVariableCommand>(p, s)).has_value());
        }
        {
            AddVariableCommand::Spec s;
            s.name = "gPump"; s.type = "DFB_Pump"; s.scope = VariableScope::Global;
            assert(stack.push(std::make_unique<AddVariableCommand>(p, s)).has_value());
        }
        {
            const auto& beta = *std::find_if(p->variables.begin(), p->variables.end(),
                [&](const Variable& v) { return p->strings.text(v.name) == "gBeta"; });
            assert(beta.type.derivedIndex == 1 && "gBeta is declared with DDT 1");
        }

        const auto before = relationsOf(*p);
        assert(before.pouLocals.at("UnitOne").size() == 1);
        assert(before.pouLocals.at("UnitZero").empty());

        // Delete DDT 0. Only the derivedTypes numbering may move.
        assert(stack.push(std::make_unique<RemoveEntityCommand>(
                   p, EntityKind::DerivedType, 0)).has_value());
        assertSound(*p, "after deleting DDT 0 with overlapping POU indices");
        assert(p->pous[unitOne].locals.size() == 1
               && "a POU-owned local must not follow a derived-type renumbering");
        assert(p->pous[unitZero].locals.empty()
               && "and must not have been moved to the POU next door");
        // The list and the owner field are two different statements of the same
        // fact, and a bad remap breaks only the second one.
        assert(relationsOf(*p).variableOwner.at("localOfUnitOne") == "pou:UnitOne"
               && "the local still says it belongs to UnitOne");
        assert(p->derivedTypes.size() == 1);
        assert(p->strings.text(p->derivedTypes[0].name) == "ST_Beta");
        assert(p->variables[p->derivedTypes[0].fields[0]].owner == 0
               && "the surviving DDT's member must follow it down");
        assert(relationsOf(*p).variableType.at("gBeta") == "ST_Beta"
               && "an instance must still name the type it was declared with");
        {
            const auto& beta = *std::find_if(p->variables.begin(), p->variables.end(),
                [&](const Variable& v) { return p->strings.text(v.name) == "gBeta"; });
            assert(beta.type.derivedIndex == 0
                   && "and its type index must have followed the type down");
        }

        assert(stack.undo().has_value());
        assertSound(*p, "after undoing it");
        assert(relationsOf(*p) == before);

        // The mirror: delete POU 0, and the DDT member's owner must not move.
        const auto ddtFieldsBefore = relationsOf(*p).ddtFields;
        assert(stack.push(std::make_unique<RemoveEntityCommand>(
                   p, EntityKind::Pou, unitZero)).has_value());
        assertSound(*p, "after deleting POU 0 with overlapping DDT indices");
        assert(relationsOf(*p).ddtFields == ddtFieldsBefore
               && "a DDT member must not follow a POU renumbering");
        assert(relationsOf(*p).variableOwner.at("fieldOfBeta") == "ddt:ST_Beta"
               && "and still says which type it is a member of");
        {
            const auto& inst = *std::find_if(p->variables.begin(), p->variables.end(),
                [&](const Variable& v) { return p->strings.text(v.name) == "gPump"; });
            assert(inst.type.fbTypeIndex == findPou(*p, "DFB_Pump")
                   && "a block instance must follow its type down the POU vector");
        }
        assert(!p->libraries.empty() && p->libraries[0].pouIndex == findPou(*p, "DFB_Pump")
               && "and so must the library entry that names it");

        assert(stack.undo().has_value());
        assertSound(*p, "after undoing that too");
        assert(relationsOf(*p) == before);
        std::printf("scope    : Variable::owner keeps its two meanings apart\n");
    }

    // =========================================================================
    //  Part 3c - nested program units.
    // -------------------------------------------------------------------------
    //  Pou::parent and Pou::children are in the model, and RemoveEntityCommand
    //  walks children to cascade, but nothing exercises either: the reference
    //  project has no nesting (0 of 29 POUs have a parent) and no command can
    //  create any. A mutation run found both fields unprotected, which is the
    //  worst kind of untested code - it looks finished.
    //
    //  So the nest is built directly on the model. That is a liberty a domain
    //  test may take and a command may not; the point is to exercise the
    //  renumbering, not to pretend the editor can make this shape yet.
    // =========================================================================
    {
        auto p = ProjectStore::createEmpty("Machine", "BMXP342020");
        core::CommandStack stack;
        assert(stack.push(std::make_unique<AddProgramUnitCommand>(p, "UnitA", "MAST")).has_value());
        assert(stack.push(std::make_unique<AddProgramUnitCommand>(p, "UnitB", "MAST")).has_value());
        assert(stack.push(std::make_unique<AddProgramUnitCommand>(p, "UnitC", "MAST")).has_value());
        const Index a = findPou(*p, "UnitA"), b = findPou(*p, "UnitB"), c = findPou(*p, "UnitC");
        assert(a == 0 && b == 1 && c == 2);

        p->pous[c].parent = b;                  // UnitC sits inside UnitB
        p->pous[b].children.push_back(c);
        assertSound(*p, "with a hand-built nest");
        const auto nested = relationsOf(*p);
        assert(nested.pouParent.at("UnitC") == "UnitB");

        // Delete the unit BELOW the nest. Everything above it slides down, and
        // both halves of the parent/child link have to slide with it.
        assert(stack.push(std::make_unique<RemoveEntityCommand>(
                   p, EntityKind::Pou, a)).has_value());
        assertSound(*p, "after deleting the POU below a nest");
        assert(p->pous.size() == 2);
        assert(relationsOf(*p).pouParent.at("UnitC") == "UnitB"
               && "the child must still point at its parent, one slot lower");
        assert(p->pous[findPou(*p, "UnitB")].children.size() == 1
               && p->pous[findPou(*p, "UnitB")].children[0] == findPou(*p, "UnitC")
               && "and the parent must still list its child");

        assert(stack.undo().has_value());
        assertSound(*p, "after undoing it");
        assert(relationsOf(*p) == nested);

        // Deleting the parent takes the child with it: a unit inside a unit does
        // not outlive it.
        const auto damage = RemoveEntityCommand(p, EntityKind::Pou, b).collateralDamage();
        assert(std::find(damage.begin(), damage.end(), std::string("POU UnitC")) != damage.end()
               && "the confirmation must say the nested unit goes too");

        assert(stack.push(std::make_unique<RemoveEntityCommand>(
                   p, EntityKind::Pou, b)).has_value());
        assertSound(*p, "after deleting a nesting POU");
        assert(findPou(*p, "UnitC") == kNoIndex && "the nested unit went with its parent");
        assert(findPou(*p, "UnitA") != kNoIndex && "and nothing else did");

        assert(stack.undo().has_value());
        assertSound(*p, "after undoing a cascade through a nest");
        assert(relationsOf(*p) == nested && "parent and child both come back, linked");
        std::printf("nesting  : parent and child survive renumbering, and fall together\n");
    }

    // =========================================================================
    //  Part 4 - deleting a DFB takes its interface and its code with it.
    // =========================================================================
    {
        auto p = ProjectStore::createEmpty("Machine", "BMXP342020");
        core::CommandStack stack;
        assert(stack.push(std::make_unique<AddFunctionBlockCommand>(p, "DFB_Valve", "1.00")).has_value());
        const Index valve = findPou(*p, "DFB_Valve");

        assert(stack.push(std::make_unique<AddSectionCommand>(
                   p, "Body", "", PouLanguage::ST, valve)).has_value());
        {
            AddVariableCommand::Spec s;
            s.name = "cmd"; s.type = "BOOL"; s.scope = VariableScope::Input; s.owner = valve;
            assert(stack.push(std::make_unique<AddVariableCommand>(p, s)).has_value());
        }
        assert(p->pous[valve].sections.size() == 1);
        assert(p->pous[valve].parameters.size() == 1);
        assert(p->libraries.size() == 1 && "a DFB type is registered in the library");

        const auto before = relationsOf(*p);
        const auto sectionsBefore = p->sections.size();
        const auto varsBefore     = p->variables.size();

        RemoveEntityCommand probe(p, EntityKind::Pou, valve);
        const auto damage = probe.collateralDamage();
        assert(damage.size() == 2 && "the interface and the body go with the block");

        assert(stack.push(std::make_unique<RemoveEntityCommand>(
                   p, EntityKind::Pou, valve)).has_value());
        assertSound(*p, "after deleting a DFB type");
        assert(p->sections.size()  == sectionsBefore - 1);
        assert(p->variables.size() == varsBefore - 1);
        assert(p->libraries.empty() && "its library entry must not outlive it");

        assert(stack.undo().has_value());
        assertSound(*p, "after undoing a DFB deletion");
        assert(relationsOf(*p) == before);
        assert(p->libraries.size() == 1 && "and the library entry comes back");
        assert(p->libraries[0].pouIndex == findPou(*p, "DFB_Valve"));
    }

    // =========================================================================
    //  Part 5 - the real project. Bytes, not counts.
    // =========================================================================
    auto p = importProject(source);
    assert(p && "the reference export must import");
    assertSound(*p, "on import");

    const std::string pristine  = exportOf(*p);
    const auto        relations = relationsOf(*p);
    std::printf("reference: %zu variables, %zu DDT, %zu POU, %zu sections, %zu bytes exported\n",
                p->variables.size(), p->derivedTypes.size(), p->pous.size(),
                p->sections.size(), pristine.size());

    core::CommandStack stack;

    // --- a section from the middle of a real task ---------------------------
    {
        assert(!p->tasks.empty() && p->tasks.front().sections.size() > 3);
        const Index victim = p->tasks.front().sections[1];
        const std::string name(p->strings.text(p->sections[victim].name));

        assert(stack.push(std::make_unique<RemoveEntityCommand>(
                   p, EntityKind::Section, victim)).has_value());
        assertSound(*p, "after deleting a real section");
        assert(exportOf(*p) != pristine && "the deletion must actually reach the file");

        assert(stack.undo().has_value());
        assertSound(*p, "after undoing a real section deletion");
        assert(relationsOf(*p) == relations && "every relationship, by name");
        assert(exportOf(*p) == pristine
               && "delete then undo must reproduce the export byte for byte");
        std::printf("section  : '%s' removed and restored, export identical\n", name.c_str());
    }

    // --- a variable from the middle of the dictionary ------------------------
    {
        const Index victim = static_cast<Index>(p->variables.size() / 2);
        assert(stack.push(std::make_unique<RemoveEntityCommand>(
                   p, EntityKind::Variable, victim)).has_value());
        assertSound(*p, "after deleting a real variable");

        assert(stack.undo().has_value());
        assertSound(*p, "after undoing a real variable deletion");
        assert(relationsOf(*p) == relations);
        assert(exportOf(*p) == pristine);
        std::printf("variable : index %u removed and restored, export identical\n", victim);
    }

    // --- the mapping alone, on 891 variables and 28 DDTs --------------------
    //
    //  Every derived type in the reference project is in use, so the deletion
    //  path above never reaches a real DDT, and the check that matters most -
    //  that Variable::owner is renumbered against derivedTypes for a member and
    //  against pous for everything else - would go untested on real data.
    //
    //  THE OBVIOUS TEST IS WRONG, and it took a failure to see it: removing an
    //  index and putting it back through remapReferences alone is NOT the
    //  identity. Removal deliberately drops the dead index out of every list it
    //  appeared in, and no mapping can conjure it back - that loss is exactly
    //  why RemovedEntity records memberships. Asserting the identity there was
    //  asserting something the design says is false.
    //
    //  What IS true, and what carries the risk, is that the mapping is a
    //  bijection when nothing is being removed: shift everything from k up by
    //  one, shift it back down, and every stored index must land where it
    //  started. Confusing the two vectors behind Variable::owner would move
    //  hundreds of declarations under the wrong owner, and the bytes say so.
    {
        const auto kinds = {EntityKind::Variable, EntityKind::DerivedType,
                            EntityKind::Pou, EntityKind::Section};
        for (auto kind : kinds) {
            std::size_t n = 0;
            switch (kind) {
                case EntityKind::Variable:    n = p->variables.size();    break;
                case EntityKind::DerivedType: n = p->derivedTypes.size(); break;
                case EntityKind::Pou:         n = p->pous.size();         break;
                case EntityKind::Section:     n = p->sections.size();     break;
            }
            assert(n > 4);
            const Index k = static_cast<Index>(n / 3);      // well inside the vector

            // Up: k and everything above it gain one. No index is dropped.
            remapReferences(*p, kind, mappingForInsertion(n, k));
            // Down again: the exact inverse, over the widened numbering.
            std::vector<Index> back(n + 1);
            for (Index i = 0; i < back.size(); ++i) back[i] = (i > k) ? i - 1 : i;
            remapReferences(*p, kind, back);

            assertSound(*p, "after a lossless remap round trip");
            assert(exportOf(*p) == pristine
                   && "renumbering up and back down again must be the identity");
        }
        std::printf("remap    : up and back down, all four kinds, export identical\n");
    }

    // --- a derived type deleted from the middle -----------------------------
    //
    //  None of the project's own 28 are removable, so two are added and the
    //  FIRST of them is deleted: the second has to slide down over it. This is
    //  the only way to reach middle-removal of a DDT on top of a real model.
    {
        assert(stack.push(std::make_unique<AddDerivedTypeCommand>(p, "ST_Probe_A", "0.01")).has_value());
        assert(stack.push(std::make_unique<AddDerivedTypeCommand>(p, "ST_Probe_B", "0.01")).has_value());
        const Index first = static_cast<Index>(p->derivedTypes.size() - 2);

        assert(stack.push(std::make_unique<RemoveEntityCommand>(
                   p, EntityKind::DerivedType, first)).has_value());
        assertSound(*p, "after deleting a DDT from the middle");
        assert(p->strings.text(p->derivedTypes[first].name) == "ST_Probe_B"
               && "the one above must have slid down");

        assert(stack.undo().has_value());     // the deletion
        assert(stack.undo().has_value());     // ST_Probe_B
        assert(stack.undo().has_value());     // ST_Probe_A
        assertSound(*p, "after unwinding the DDT chain");
        assert(relationsOf(*p) == relations);
        assert(exportOf(*p) == pristine
               && "a creation and a deletion unwind through each other cleanly");
        std::printf("DDT      : middle removal over the real model, export identical\n");
    }

    // --- a derived type nothing is declared with ----------------------------
    if (const Index ddt = unusedDerivedType(*p); ddt != kNoIndex) {
        const std::string name(p->strings.text(p->derivedTypes[ddt].name));
        const auto fields = p->derivedTypes[ddt].fields.size();

        assert(stack.push(std::make_unique<RemoveEntityCommand>(
                   p, EntityKind::DerivedType, ddt)).has_value());
        assertSound(*p, "after deleting a real DDT");

        assert(stack.undo().has_value());
        assertSound(*p, "after undoing a real DDT deletion");
        assert(relationsOf(*p) == relations);
        assert(exportOf(*p) == pristine);
        std::printf("DDT      : '%s' (%zu fields) removed and restored, export identical\n",
                    name.c_str(), fields);
    }

    // --- one that is in use must still be refused, on real data -------------
    if (const Index ddt = usedDerivedType(*p); ddt != kNoIndex) {
        const auto damage = RemoveEntityCommand(p, EntityKind::DerivedType, ddt).collateralDamage();
        (void)damage;
        assert(!stack.push(std::make_unique<RemoveEntityCommand>(
                   p, EntityKind::DerivedType, ddt)).has_value());
        assert(exportOf(*p) == pristine && "a refusal must not touch the model");
        std::printf("refusal  : '%s' is in use, deletion refused, export untouched\n",
                    std::string(p->strings.text(p->derivedTypes[ddt].name)).c_str());
    }

    // --- several deletions, then several undos ------------------------------
    {
        const Index a = findSection(*p, std::string(p->strings.text(p->sections[3].name)));
        assert(stack.push(std::make_unique<RemoveEntityCommand>(p, EntityKind::Section, a)).has_value());
        assertSound(*p, "after the first of two deletions");
        assert(stack.push(std::make_unique<RemoveEntityCommand>(p, EntityKind::Section, 1)).has_value());
        assertSound(*p, "after the second of two deletions");

        assert(stack.undo().has_value());
        assert(stack.undo().has_value());
        assertSound(*p, "after unwinding both");
        assert(relationsOf(*p) == relations);
        assert(exportOf(*p) == pristine && "two deletions unwound leave no trace either");
        std::printf("stacked  : two deletions unwound, export identical\n");
    }

    // --- a deep stack of mixed deletions ------------------------------------
    //
    //  Two deletions unwound is the easy case. This drives the stack down 20
    //  levels with sections and variables interleaved - each erase renumbering a
    //  different vector, each undo having to be the inverse of the one erase it
    //  corresponds to and not of the one before it - and then unwinds the whole
    //  thing. The pseudo-random choice is seeded, so a failure is reproducible.
    {
        std::uint32_t seed = 20260907u;
        auto next = [&seed](std::uint32_t bound) {
            seed = seed * 1664525u + 1013904223u;      // Numerical Recipes LCG
            return (seed >> 16) % bound;
        };

        int applied = 0;
        for (int step = 0; step < 20; ++step) {
            const bool section = (next(2) == 0);
            const auto kind    = section ? EntityKind::Section : EntityKind::Variable;
            const auto limit   = static_cast<std::uint32_t>(section ? p->sections.size()
                                                                    : p->variables.size());
            if (limit < 4) continue;
            const Index victim = next(limit);

            if (stack.push(std::make_unique<RemoveEntityCommand>(p, kind, victim))) {
                ++applied;
                assertSound(*p, "in the middle of a deep deletion stack");
            }
        }
        assert(applied >= 15 && "the stack must actually have gone somewhere");
        assert(exportOf(*p) != pristine);

        for (int i = 0; i < applied; ++i) {
            assert(stack.undo().has_value());
            assertSound(*p, "while unwinding a deep deletion stack");
        }
        assert(relationsOf(*p) == relations);
        assert(exportOf(*p) == pristine
               && "twenty deletions unwound must leave the file exactly as it was");
        std::printf("stack    : %d mixed deletions unwound, export identical\n", applied);
    }

    std::printf("delete_test: ok\n");
    return 0;
}
