// =============================================================================
//  tests/library_test.cpp — the Control Expert block library
// -----------------------------------------------------------------------------
//  What matters here is not that entries exist but that each one says where it
//  came from. A signature taken from IEC 61131-3, one read out of the user's own
//  program, and one guessed from a naming convention are three different claims,
//  and the last must never be presented as if it were the first.
// =============================================================================
#include "../src/project/BlockLibrary.hpp"
#include "../src/project/EditCommands.hpp"
#include "../src/project/ProjectStore.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>

using namespace project;
using namespace domain;

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "resources/schneider_library.txt";

    // --- the built-in seed alone keeps the editor usable ---------------------
    {
        const auto seed = BlockLibrary::builtin();
        assert(seed.find("TON") && "the editor must work with no resource file");
        assert(seed.find("ton") && "lookup ignores case, as ST does");
        assert(!seed.find("INT_TO_REAL") && "the full table is the data file's job");
    }

    // --- the data file ------------------------------------------------------
    auto loaded = BlockLibrary::loadFromFile(path);
    assert(loaded && "resources/schneider_library.txt must parse");
    const auto& lib = *loaded;

    std::printf("%zu blocks: %zu from IEC 61131-3, %zu observed in the project, %zu unverified\n",
                lib.size(),
                lib.countWith(Provenance::Standard),
                lib.countWith(Provenance::Observed),
                lib.countWith(Provenance::Unverified));
    assert(lib.size() > 90);
    assert(lib.countWith(Provenance::Standard) > 30);
    assert(lib.countWith(Provenance::Observed) > 10);

    // --- timers, with their pins and directions ------------------------------
    {
        const auto* ton = lib.find("TON");
        assert(ton && ton->kind == BlockKind::FunctionBlock);
        assert(ton->provenance == Provenance::Standard);
        assert(ton->parameters.size() == 4);
        assert(ton->parameters[0].name == "IN" && ton->parameters[0].type == "BOOL");
        assert(ton->parameters[0].direction == BlockParameter::Direction::In);
        assert(ton->parameters[1].name == "PT" && ton->parameters[1].type == "TIME");
        assert(ton->parameters[2].name == "Q");
        assert(ton->parameters[2].direction == BlockParameter::Direction::Out
               && "Q is an output; showing it as an input would be worse than useless");
        assert(ton->parameters[3].name == "ET");
        std::printf("  %s\n", ton->signature().c_str());

        for (const char* t : {"TOF", "TP"}) {
            const auto* b = lib.find(t);
            assert(b && b->parameters.size() == 4 && b->kind == BlockKind::FunctionBlock);
        }
    }

    // --- conversions, and the honesty of their labels -------------------------
    {
        const auto* conv = lib.find("INT_TO_REAL");
        assert(conv && conv->kind == BlockKind::Function);
        assert(conv->returns == "REAL");
        assert(conv->parameters.size() == 1 && conv->parameters[0].type == "INT");
        assert(conv->provenance == Provenance::Observed
               && "this one is used by the reference project, so it certainly exists");
        std::printf("  %s   [%s]\n", conv->signature().c_str(),
                    std::string(toString(conv->provenance)).c_str());

        // The ones the reference program uses are all present and marked observed.
        for (const char* name : {"TIME_TO_UDINT", "UDINT_TO_DINT", "UDINT_TO_TIME",
                                 "REAL_TO_TIME", "TIME_TO_UINT", "STRING_TO_INT",
                                 "INT_TO_UDINT", "UDINT_TO_INT", "REAL_TO_INT",
                                 "INT_TO_STRING", "CONCAT_STR", "LEN_INT",
                                 "LEFT_INT", "RIGHT_INT", "FIND_INT", "SHL"}) {
            const auto* b = lib.find(name);
            assert(b && "every function the reference project calls must be known");
            assert(b->provenance == Provenance::Observed
                   && "and must be marked as confirmed by that code, not guessed");
        }

        // A guess must be labelled a guess.
        const auto* guessed = lib.find("REPLACE_INT");
        assert(guessed && guessed->provenance == Provenance::Unverified);
    }

    // --- the library reaches the editor --------------------------------------
    // Installed explicitly: a test that depended on the working directory would
    // pass from one place and fail from another, which is the same trap the
    // application avoids by installing it at start-up.
    BlockLibrary::installShared(lib);
    {
        auto p = ProjectStore::createEmpty("Machine", "BMXP342020");
        core::CommandStack stack;
        (void)stack.push(std::make_unique<AddSectionCommand>(
            p, "Cycle", "MAST", PouLanguage::ST));

        const auto conv = suggestionsFor(*p, 0, "INT_TO");
        std::printf("  'INT_TO' offers %zu conversions\n", conv.size());
        assert(conv.size() >= 4);
        assert(std::any_of(conv.begin(), conv.end(),
                           [](const Suggestion& s) { return s.text == "INT_TO_REAL"; }));

        const auto timers = suggestionsFor(*p, 0, "TO");
        assert(std::any_of(timers.begin(), timers.end(),
                           [](const Suggestion& s) { return s.text == "TON"; }));

        // Signature help answers for a library block, with the directions.
        CallSignature sig;
        assert(signatureFor(*p, "TON", sig));
        assert(sig.parameters.size() == 4);
        assert(sig.parameters[0].rfind("IN ", 0) == 0);
        assert(sig.parameters[2].rfind("OUT", 0) == 0);

        CallSignature conversion;
        assert(signatureFor(*p, "int_to_real", conversion) && "case-insensitive, as ST is");
        assert(conversion.returns == "REAL");

        // An unverified definition says so where the user can see it.
        CallSignature unsure;
        assert(signatureFor(*p, "REPLACE_INT", unsure));
        assert(unsure.name.find("unverified") != std::string::npos);
    }

    // --- a project DFB still wins over a library block of the same name -------
    {
        auto p = ProjectStore::createEmpty("Machine", "BMXP342020");
        core::CommandStack stack;
        (void)stack.push(std::make_unique<AddFunctionBlockCommand>(p, "TON", "1.00"));
        Index dfb = 0;
        for (Index i = 0; i < p->pous.size(); ++i)
            if (p->pous[i].kind == PouKind::FunctionBlockType) dfb = i;
        AddVariableCommand::Spec s;
        s.name = "myPin"; s.type = "BOOL"; s.scope = VariableScope::Input; s.owner = dfb;
        (void)stack.push(std::make_unique<AddVariableCommand>(p, s));

        CallSignature sig;
        assert(signatureFor(*p, "TON", sig));
        assert(sig.parameters.size() == 1 && sig.parameters[0].find("myPin") != std::string::npos
               && "a block declared in the project shadows the library one");
    }

    std::printf("\nlibrary_test: all assertions passed\n");
    return 0;
}
