// =============================================================================
//  tests/parser_robustness_test.cpp
// -----------------------------------------------------------------------------
//  Feeds the parser damaged versions of a real export: truncations, whole
//  element kinds removed, orphaned bodies, references to things that do not
//  exist, and 200 random byte flips. Nothing here may crash; a malformed file
//  must produce a diagnostic, not an out-of-range access.
//
//  Build it with -D_GLIBCXX_ASSERTIONS -fsanitize=address,undefined. That gives
//  the same checking MSVC's debug STL does, which is what turns a silent
//  overrun into a test failure instead of a crash on someone's desk.
// =============================================================================
#include "../src/import/ProjectImporter.hpp"
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>
using namespace importer;

static std::string slurp(const char* p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}
static bool run(const std::string& text, const char* label) {
    const std::string tmp = "/tmp/fuzz-case.XPG";
    { std::ofstream o(tmp, std::ios::binary); o << text; }
    core::EventBus bus; ProjectImporter imp(bus);
    auto r = imp.importFile(tmp);
    std::printf("  %-46s %s\n", label, r ? "parsed" : "rejected cleanly");
    return true;
}
int main(int argc, char** argv) {
    const auto full = slurp(argv[1]);

    // 1. truncations at many points: a half-written file must not crash.
    for (int i = 1; i <= 12; ++i) {
        char label[64];
        std::snprintf(label, sizeof label, "truncated at %d%%", i * 8);
        run(full.substr(0, full.size() * static_cast<std::size_t>(i) / 13), label);
    }

    // 2. structural elements removed, one kind at a time.
    for (const char* tag : {"FBSource", "programUnit", "DDTSource", "dataBlock",
                            "taskDesc", "logicConf", "structure"}) {
        std::string s = full;
        const std::string open = std::string("<") + tag;
        for (std::size_t at = 0; (at = s.find(open, at)) != std::string::npos;)
            s.replace(at, open.size(), std::string("<x_") + tag);
        char label[80];
        std::snprintf(label, sizeof label, "all <%s> renamed away", tag);
        run(s, label);
    }

    // 3. a body element with no owning POU, and a stray close.
    run("<?xml version=\"1.0\"?><PGMExchangeFile>"
        "<program><identProgram name=\"orphan\" type=\"section\"/><STSource>x;</STSource></program>"
        "</PGMExchangeFile>", "section with no task and no POU");
    run("<?xml version=\"1.0\"?><PGMExchangeFile>"
        "<variables name=\"v\" typeName=\"INT\"></variables></PGMExchangeFile>",
        "declaration outside any group");
    run("<?xml version=\"1.0\"?><PGMExchangeFile>"
        "<FBSource nameOfFBType=\"F\"/><program><identProgram name=\"s\" task=\"MAST\"/>"
        "<STSource>y;</STSource></program></PGMExchangeFile>",
        "self-closed FBSource then a task section");
    run("<?xml version=\"1.0\"?><PGMExchangeFile><logicConf><resource resIdent=\"BMX P34 2020\">"
        "<taskDesc task=\"MAST\"><programUnitDesc name=\"ghost\" SectionOrder=\"3\"/></taskDesc>"
        "</resource></logicConf></PGMExchangeFile>",
        "programUnitDesc naming a unit that does not exist");
    run("<?xml version=\"1.0\"?><PGMExchangeFile><programUnit name=\"U\">"
        "<inOutParameters><variables name=\"a\" typeName=\"INT\"/></inOutParameters>"
        "</programUnit><dataBlock><variables name=\"g\" typeName=\"INT\"/></dataBlock>"
        "</PGMExchangeFile>", "program unit then dataBlock");

    // 4. random byte flips: 200 mutations of the real file.
    std::mt19937 gen(1234);
    std::uniform_int_distribution<std::size_t> pos(0, full.size() - 1);
    std::uniform_int_distribution<int> byte(32, 126);
    for (int i = 0; i < 200; ++i) {
        std::string s = full;
        for (int k = 0; k < 8; ++k) s[pos(gen)] = static_cast<char>(byte(gen));
        const std::string tmp = "/tmp/fuzz-case.XPG";
        { std::ofstream o(tmp, std::ios::binary); o << s; }
        core::EventBus bus; ProjectImporter imp(bus);
        (void)imp.importFile(tmp);
    }
    std::printf("  200 random mutations survived\n");
    std::printf("\nno out-of-range access reached\n");
    return 0;
}
