// =============================================================================
//  tests/edit_test.cpp — creating things, and taking them back
// -----------------------------------------------------------------------------
//  Two properties matter more than the individual commands:
//
//   1. undo must restore the model exactly, so that a mistake costs nothing;
//   2. a command that fails must leave the model untouched, so a rejected name
//      or a full slot does not half-create something.
//
//  Both are checked by comparing entity counts before and after, and by driving
//  everything through the CommandStack rather than calling execute() directly -
//  which is how the application does it.
// =============================================================================
#include "../src/core/Command.hpp"
#include "../src/import/ProjectParser.hpp"
#include "../src/project/EditCommands.hpp"
#include "../src/project/ProjectStore.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <system_error>

using namespace project;
using namespace domain;

namespace {

struct Counts {
    std::size_t variables{}, sections{}, pous{}, ddts{}, libraries{}, racks{}, modules{};
    bool operator==(const Counts&) const = default;
};

Counts countOf(const Project& p) {
    Counts c;
    c.variables = p.variables.size();
    c.sections  = p.sections.size();
    c.pous      = p.pous.size();
    c.ddts      = p.derivedTypes.size();
    c.libraries = p.libraries.size();
    c.racks     = p.hardware.racks.size();
    for (const auto& r : p.hardware.racks) c.modules += r.modules.size();
    return c;
}

} // namespace

int main() {
    auto project = ProjectStore::createEmpty("Machine", "BMXP342020");
    core::CommandStack stack;
    const auto empty = countOf(*project);

    // --- sections -----------------------------------------------------------
    assert(stack.push(std::make_unique<AddSectionCommand>(
               project, "Init", "MAST", PouLanguage::ST)).has_value());
    assert(project->sections.size() == 1);
    assert(project->tasks.front().sections.size() == 1 && "the section joins its task");
    assert(project->pous.size() == 1 && "a bare section gets its own POU");

    assert(stack.push(std::make_unique<AddSectionCommand>(
               project, "Cycle", "MAST", PouLanguage::ST)).has_value());
    assert(project->sections[1].order > project->sections[0].order
           && "a new section runs after the ones already there");

    // --- program units, DFB types, derived types ----------------------------
    assert(stack.push(std::make_unique<AddProgramUnitCommand>(
               project, "Pumping", "MAST")).has_value());
    assert(stack.push(std::make_unique<AddFunctionBlockCommand>(
               project, "DFB_Motor", "1.00")).has_value());
    assert(stack.push(std::make_unique<AddDerivedTypeCommand>(
               project, "ST_Cabinet", "0.01")).has_value());

    assert(project->derivedTypes.size() == 1);
    assert(project->libraries.size() == 1 && "a DFB type appears in the library");

    // A DFB created here must be signed the way Control Expert signs one.
    const auto& dfb = *std::find_if(project->pous.begin(), project->pous.end(),
                                    [&](const Pou& p) {
                                        return project->strings.text(p.name) == "DFB_Motor";
                                    });
    assert(!dfb.attributes.empty() && dfb.attributes.front().first == "UseNewTplSignAlgo");

    // --- names must not collide ---------------------------------------------
    const auto before = countOf(*project);
    assert(!stack.push(std::make_unique<AddDerivedTypeCommand>(
               project, "ST_Cabinet", "0.02")).has_value() && "duplicate name refused");
    assert(!stack.push(std::make_unique<AddFunctionBlockCommand>(
               project, "Pumping", "1.00")).has_value() && "a unit name is taken too");
    assert(countOf(*project) == before && "a refused command must change nothing");

    // --- variables ------------------------------------------------------------
    {
        AddVariableCommand::Spec s;
        s.name    = "gStartButton";
        s.type    = "EBOOL";
        s.address = "%I0.3";
        s.comment = "start push button";
        s.scope   = VariableScope::Global;
        assert(stack.push(std::make_unique<AddVariableCommand>(project, s)).has_value());

        const auto& v = project->variables.back();
        assert(v.located && v.address.area == MemoryArea::Input);
        assert(v.address.bit.value() == 3);
        assert(v.type.klass == TypeClass::Elementary);
    }
    {
        // A field inside the derived type just created.
        AddVariableCommand::Spec s;
        s.name  = "ready";
        s.type  = "BOOL";
        s.scope = VariableScope::DerivedMember;
        s.owner = 0;                       // ST_Cabinet
        assert(stack.push(std::make_unique<AddVariableCommand>(project, s)).has_value());
        assert(project->derivedTypes[0].fields.size() == 1);
    }
    {
        // An address that is not one must be refused, not stored as text.
        AddVariableCommand::Spec s;
        s.name    = "bad";
        s.address = "not an address";
        const auto snapshot = countOf(*project);
        assert(!stack.push(std::make_unique<AddVariableCommand>(project, s)).has_value());
        std::printf("rejected address: %zu variables before, %zu after\n",
                    snapshot.variables, project->variables.size());
        // Strict: a command that reports failure must not have touched anything.
        assert(countOf(*project) == snapshot && "a failed command must not mutate the model");
    }
    {
        // An owner that does not exist is refused before anything is created.
        AddVariableCommand::Spec s;
        s.name  = "orphan";
        s.scope = VariableScope::Local;
        s.owner = 9999;
        const auto snapshot = countOf(*project);
        assert(!stack.push(std::make_unique<AddVariableCommand>(project, s)).has_value());
        assert(countOf(*project) == snapshot);
    }

    // --- a section may repeat its name in another container -------------------
    {
        // Every program unit in the reference project has an "Init"; Control
        // Expert allows that, so the check has to be scoped, not global.
        const auto unit = *std::find_if(
            project->pous.begin(), project->pous.end(),
            [&](const Pou& p) { return p.kind == PouKind::ProgramUnit; });
        Index unitIndex = 0;
        for (Index i = 0; i < project->pous.size(); ++i)
            if (project->pous[i].name == unit.name) unitIndex = i;

        assert(stack.push(std::make_unique<AddSectionCommand>(
                   project, "Init", "", PouLanguage::ST, unitIndex)).has_value()
               && "the same name inside a program unit is fine");
        assert(!stack.push(std::make_unique<AddSectionCommand>(
                   project, "Init", "", PouLanguage::ST, unitIndex)).has_value()
               && "but not twice in the same one");
        assert(!stack.push(std::make_unique<AddSectionCommand>(
                   project, "Init", "MAST", PouLanguage::ST)).has_value()
               && "nor twice among the task sections");
    }

    // --- a DFB may hold a body and its own declarations -----------------------
    {
        Index dfbIndex = 0;
        for (Index i = 0; i < project->pous.size(); ++i)
            if (project->pous[i].kind == PouKind::FunctionBlockType) dfbIndex = i;

        assert(stack.push(std::make_unique<AddSectionCommand>(
                   project, "Main", "", PouLanguage::ST, dfbIndex)).has_value());
        assert(project->pous[dfbIndex].sections.size() == 1 && "the DFB now has a body");

        for (auto scope : {VariableScope::Input, VariableScope::Output, VariableScope::InOut,
                           VariableScope::Public, VariableScope::Local}) {
            AddVariableCommand::Spec s;
            s.name  = std::string(toString(scope)) + "Pin";
            s.type  = "BOOL";
            s.scope = scope;
            s.owner = dfbIndex;
            assert(stack.push(std::make_unique<AddVariableCommand>(project, s)).has_value());
        }
        assert(project->pous[dfbIndex].parameters.size() == 3 && "in, out and in/out");
        assert(project->pous[dfbIndex].locals.size() == 2 && "public and private");
    }

    // --- a DDT may hold fields, and a variable may be typed with it ------------
    {
        AddVariableCommand::Spec s;
        s.name  = "level";
        s.type  = "INT";
        s.scope = VariableScope::DerivedMember;
        s.owner = 0;                              // ST_Cabinet
        assert(stack.push(std::make_unique<AddVariableCommand>(project, s)).has_value());
        assert(project->derivedTypes[0].fields.size() == 2);

        // The type list a dialog offers must include the project's own types.
        const auto types = availableTypes(*project);
        assert(std::find(types.begin(), types.end(), "ST_Cabinet") != types.end());
        assert(std::find(types.begin(), types.end(), "DFB_Motor") != types.end());
        assert(std::find(types.begin(), types.end(), "BOOL") != types.end());

        AddVariableCommand::Spec instance;
        instance.name  = "cabinet1";
        instance.type  = "ST_Cabinet";
        instance.scope = VariableScope::Global;
        assert(stack.push(std::make_unique<AddVariableCommand>(project, instance)).has_value());
        assert(project->variables.back().type.klass == TypeClass::Derived
               && "the type resolves immediately, so the tree and memory are right at once");
    }

    // --- arrays, which the dialog could not declare before --------------------
    {
        AddVariableCommand::Spec s;
        s.name = "Cabinets";
        s.type = "ARRAY[0..3] OF ST_Cabinet";
        s.scope = VariableScope::Global;
        assert(stack.push(std::make_unique<AddVariableCommand>(project, s)).has_value());

        const auto& v = project->variables.back();
        assert(v.type.klass == TypeClass::Array);
        assert(v.type.arrayLow == 0 && v.type.arrayHigh == 3);
        assert(project->strings.text(v.type.elementType) == "ST_Cabinet");
        assert(v.type.derivedIndex != kNoIndex && "an array resolves on its element type");
        std::printf("declared %s -> %lld..%lld of %s\n",
                    std::string(project->strings.text(v.name)).c_str(),
                    (long long)v.type.arrayLow, (long long)v.type.arrayHigh,
                    std::string(project->strings.text(v.type.elementType)).c_str());
    }
    {
        // An array of an elementary type works the same way.
        AddVariableCommand::Spec s;
        s.name = "Buffer"; s.type = "ARRAY[1..10] OF INT";
        assert(stack.push(std::make_unique<AddVariableCommand>(project, s)).has_value());
        assert(project->variables.back().type.arrayLow == 1);
        assert(project->variables.back().type.arrayHigh == 10);
    }
    {
        // But an array of a function block is refused: a block has state, and
        // its instances are declared one by one.
        AddVariableCommand::Spec s;
        s.name = "Motors"; s.type = "ARRAY[0..3] OF DFB_Motor";
        const auto snapshot = countOf(*project);
        assert(!stack.push(std::make_unique<AddVariableCommand>(project, s)).has_value());
        assert(countOf(*project) == snapshot && "and nothing is half-created");
    }

    // --- hardware, with the rules Schneider publishes -------------------------
    assert(stack.push(std::make_unique<AddRackCommand>(project, "BMXXBP0800")).has_value());
    assert(project->hardware.racks.size() == 1);
    assert(project->hardware.racks[0].slotCount == 8 && "slot count comes from the catalog");
    assert(!project->hardware.inferred);

    // Rack 0 arrives with its supply and its processor already in place.
    {
        const auto& rack = project->hardware.racks[0];
        assert(rack.modules.size() == 2);
        assert(rack.modules[0].kind == ModuleKind::PowerSupply && rack.modules[0].slot == -1);
        assert(rack.modules[1].isCpu && rack.modules[1].slot == 0);
        std::printf("rack 0: %s at its own position, %s in slot 0\n",
                    rack.modules[0].reference.c_str(), rack.modules[1].reference.c_str());
    }

    // The supply is a choice, not something the tool decides for you.
    {
        const auto catalog = importer::HardwareCatalog::builtinFallback();
        const auto supplies = catalog.powerSupplies();
        std::printf("catalogue offers %zu power supplies\n", supplies.size());
        assert(supplies.size() >= 4 && "the dialog needs something to choose from");

        auto other = ProjectStore::createEmpty("Other", "BMXP342020");
        core::CommandStack s2;
        assert(s2.push(std::make_unique<AddRackCommand>(
                   other, "BMXXBP0600", "BMXCPS3020")).has_value());
        assert(other->hardware.racks[0].modules[0].reference == "BMXCPS3020"
               && "the chosen supply is the one that gets placed");
        assert(other->hardware.powerSupply == "BMXCPS3020");
        assert(!other->hardware.racks[0].modules[0].description.empty()
               && "and it is described from the catalogue");
    }

    assert(!stack.push(std::make_unique<AddModuleCommand>(
               project, 0, 0, "BMXDDI1602")).has_value()
           && "slot 0 of rack 0 belongs to the processor");
    assert(!stack.push(std::make_unique<AddModuleCommand>(
               project, 0, -1, "BMXCPS2000")).has_value()
           && "the supply is not placed in a numbered slot");

    assert(stack.push(std::make_unique<AddModuleCommand>(
               project, 0, 3, "BMXDDI3202K")).has_value());
    assert(project->hardware.racks[0].modules.size() == 3);

    assert(!stack.push(std::make_unique<AddModuleCommand>(
               project, 0, 3, "BMXDDO1602")).has_value() && "the slot is taken");
    assert(!stack.push(std::make_unique<AddModuleCommand>(
               project, 0, 9, "BMXDDO1602")).has_value() && "past the end of an 8-slot rack");
    assert(!stack.push(std::make_unique<AddModuleCommand>(
               project, 0, 4, "BMXNOTREAL")).has_value() && "unknown reference refused");
    assert(!stack.push(std::make_unique<AddModuleCommand>(
               project, 0, 5, "BMXXBP0800")).has_value() && "a backplane is not a module");
    assert(project->hardware.racks[0].modules.size() == 3 && "no half-created module");

    // A BMX P34 2020 addresses four racks and no more.
    for (int extra = 1; extra <= 3; ++extra)
        assert(stack.push(std::make_unique<AddRackCommand>(project, "BMXXBP1200")).has_value());
    assert(project->hardware.racks.size() == 4);
    assert(!stack.push(std::make_unique<AddRackCommand>(project, "BMXXBP0800")).has_value()
           && "the fifth rack is beyond what the processor addresses");

    // Extension racks have no processor: their slot 0 is an ordinary slot.
    assert(project->hardware.racks[1].modules.size() == 1 && "supply only");
    assert(stack.push(std::make_unique<AddModuleCommand>(
               project, 1, 0, "BMXDDI1602")).has_value()
           && "slot 0 of an extension rack is free");

    const auto full = countOf(*project);
    std::printf("built: %zu variables, %zu sections, %zu POUs, %zu DDT, %zu racks, %zu modules\n",
                full.variables, full.sections, full.pous, full.ddts, full.racks, full.modules);

    // --- undo everything, in order -------------------------------------------
    std::size_t undone = 0;
    while (stack.canUndo()) {
        assert(stack.undo().has_value() && "every command must be able to take itself back");
        ++undone;
    }
    std::printf("undid %zu commands\n", undone);
    assert(countOf(*project) == empty && "undo must restore the model exactly");

    // --- and redo it ----------------------------------------------------------
    while (stack.canRedo()) assert(stack.redo().has_value());
    assert(countOf(*project) == full && "redo must rebuild exactly what undo removed");
    std::printf("redo restored the same %zu variables and %zu modules\n",
                countOf(*project).variables, countOf(*project).modules);

    // --- what was built must survive a save and reload -----------------------
    {
        const auto dir = (std::filesystem::temp_directory_path() / "xpg-edit-test").string();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);

        Manifest m;
        m.name = "Machine";
        assert(ProjectStore::save(*project, m, dir).has_value());
        auto reopened = ProjectStore::open(dir);
        assert(reopened && reopened->project);
        const auto back = countOf(*reopened->project);
        std::printf("after save and reload: %zu variables, %zu sections, %zu modules\n",
                    back.variables, back.sections, back.modules);
        assert(back.sections == full.sections);
        assert(back.ddts == full.ddts);
        assert(back.racks == full.racks);
        assert(back.modules == full.modules);
    }

    std::printf("\nedit_test: all assertions passed\n");
    return 0;
}
