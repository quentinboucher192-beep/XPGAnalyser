// =============================================================================
//  tests/sharedlib_test.cpp — DDTs and DFBs shared between projects
// -----------------------------------------------------------------------------
//  The property that matters is not "does it copy a file" but: does a block
//  published from one project arrive in another intact - interface, body,
//  version - and does a version that has moved on get *notified* rather than
//  applied. A machine in service must not change because a colleague edited a
//  block, which is why nothing here updates anything on its own.
// =============================================================================
#include "../src/project/EditCommands.hpp"
#include "../src/project/ProjectStore.hpp"
#include "../src/project/SharedLibrary.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>

using namespace project;
using namespace domain;
namespace fs = std::filesystem;

namespace {

Index dfbIndex(const Project& p, std::string_view name) {
    for (Index i = 0; i < p.pous.size(); ++i)
        if (p.pous[i].kind == PouKind::FunctionBlockType && p.strings.text(p.pous[i].name) == name)
            return i;
    return kNoIndex;
}

} // namespace

int main() {
    const auto root = (fs::temp_directory_path() / "xpg-libs").string();
    std::error_code ec;
    fs::remove_all(root, ec);

    // --- a project with something worth sharing ------------------------------
    auto source = ProjectStore::createEmpty("Source", "BMXP342020");
    core::CommandStack stack;
    assert(stack.push(std::make_unique<AddDerivedTypeCommand>(source, "ST_Cabinet", "1.00")).has_value());
    {
        AddVariableCommand::Spec s;
        s.name = "ready"; s.type = "BOOL"; s.scope = VariableScope::DerivedMember; s.owner = 0;
        assert(stack.push(std::make_unique<AddVariableCommand>(source, s)).has_value());
        s.name = "level"; s.type = "INT";
        assert(stack.push(std::make_unique<AddVariableCommand>(source, s)).has_value());
    }
    assert(stack.push(std::make_unique<AddFunctionBlockCommand>(source, "CAPTEUR", "1.00")).has_value());
    const auto dfb = dfbIndex(*source, "CAPTEUR");
    for (auto [name, type, scope] : std::initializer_list<std::tuple<const char*, const char*, VariableScope>>{
             {"raw", "INT", VariableScope::Input},
             {"scaled", "REAL", VariableScope::Output},
             {"calls", "DINT", VariableScope::Local}}) {
        AddVariableCommand::Spec s;
        s.name = name; s.type = type; s.scope = scope; s.owner = dfb;
        assert(stack.push(std::make_unique<AddVariableCommand>(source, s)).has_value());
    }
    assert(stack.push(std::make_unique<AddSectionCommand>(
               source, "Body", "", PouLanguage::ST, dfb)).has_value());
    assert(stack.push(std::make_unique<SetSectionBodyCommand>(
               source, static_cast<Index>(source->sections.size() - 1),
               "calls := calls + 1;\nscaled := INT_TO_REAL(raw) * 2.0;\n")).has_value());

    // --- publish -------------------------------------------------------------
    SharedLibrary library(root);
    assert(library.scan().has_value() && "an empty library is not an error");
    assert(library.items().empty());

    assert(library.publishDerivedType(*source, "ST_Cabinet", "Cabinets").has_value());
    assert(library.publishFunctionBlock(*source, "CAPTEUR", "Sensors").has_value());
    assert(!library.publishFunctionBlock(*source, "NOT_THERE", "Sensors").has_value());

    assert(fs::exists(fs::path(root) / "index.txt"));
    assert(fs::exists(fs::path(root) / "Cabinets" / "ST_Cabinet.ddt"));
    assert(fs::exists(fs::path(root) / "Sensors" / "CAPTEUR.dfb"));

    // The categories are folders, and the index lists what is in them.
    SharedLibrary reopened(root);
    assert(reopened.scan().has_value());
    assert(reopened.items().size() == 2);
    const auto categories = reopened.categories();
    std::printf("library: %zu items in %zu categories (%s, %s)\n",
                reopened.items().size(), categories.size(),
                categories[0].c_str(), categories[1].c_str());
    assert(categories.size() == 2);
    assert(reopened.find("CAPTEUR") && reopened.find("CAPTEUR")->version == "1.00");

    // --- import into a different project --------------------------------------
    auto target = ProjectStore::createEmpty("Target", "BMXP342020");
    {
        auto imported = reopened.import(*target, "CAPTEUR");
        assert(imported && imported->importedAs == "CAPTEUR");
        const auto index = dfbIndex(*target, "CAPTEUR");
        assert(index != kNoIndex);
        const auto& pou = target->pous[index];
        assert(pou.version == "1.00");
        assert(pou.parameters.size() == 2 && "the interface came across");
        assert(pou.locals.size() == 1);
        assert(pou.sections.size() == 1 && "and so did the body");
        const auto& body = target->sections[pou.sections.front()].body;
        assert(body.find("INT_TO_REAL(raw)") != std::string::npos);
        std::printf("imported CAPTEUR: %zu pins, %zu locals, body %zu bytes\n",
                    pou.parameters.size(), pou.locals.size(), body.size());
    }
    {
        auto ddt = reopened.import(*target, "ST_Cabinet");
        assert(ddt && target->derivedTypes.size() == 1);
        assert(target->derivedTypes[0].fields.size() == 2);
    }

    // --- a name already taken -------------------------------------------------
    {
        // Refusing is the default: importing must not quietly replace work.
        assert(!reopened.import(*target, "CAPTEUR").has_value());

        auto duplicated = reopened.import(*target, "CAPTEUR", SharedLibrary::OnConflict::Duplicate);
        assert(duplicated && duplicated->importedAs == "CAPTEUR_v1_00");
        assert(dfbIndex(*target, "CAPTEUR_v1_00") != kNoIndex);
        assert(dfbIndex(*target, "CAPTEUR") != kNoIndex && "the original is still there");
        std::printf("duplicate import arrived as %s\n", duplicated->importedAs.c_str());

        const auto before = target->pous.size();
        auto overwritten = reopened.import(*target, "CAPTEUR", SharedLibrary::OnConflict::Overwrite);
        assert(overwritten && overwritten->replacedExisting);
        assert(target->pous.size() == before && "the old definition went, the new one took its place");
    }

    // --- versions are notified, never applied ---------------------------------
    {
        // The library moves on; the project does not.
        auto newer = ProjectStore::createEmpty("Newer", "BMXP342020");
        core::CommandStack s2;
        assert(s2.push(std::make_unique<AddFunctionBlockCommand>(newer, "CAPTEUR", "2.00")).has_value());
        SharedLibrary writable(root);
        assert(writable.scan().has_value());
        assert(!writable.publishFunctionBlock(*newer, "CAPTEUR", "Sensors").has_value()
               && "publishing over a different version must be refused unless asked");
        assert(writable.publishFunctionBlock(*newer, "CAPTEUR", "Sensors", /*replace*/ true)
                   .has_value());

        SharedLibrary current(root);
        assert(current.scan().has_value());
        assert(current.find("CAPTEUR")->version == "2.00");

        const auto notices = current.outdated(*target);
        std::printf("outdated: %zu\n", notices.size());
        assert(notices.size() == 1);
        assert(notices.front().name == "CAPTEUR");
        assert(notices.front().projectVersion == "1.00");
        assert(notices.front().libraryVersion == "2.00");

        // Asking must not have changed anything.
        const auto index = dfbIndex(*target, "CAPTEUR");
        assert(target->pous[index].version == "1.00"
               && "a project keeps what it imported until someone decides otherwise");
    }

    std::printf("\nsharedlib_test: all assertions passed\n");
    return 0;
}
