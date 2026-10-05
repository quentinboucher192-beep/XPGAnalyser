// =============================================================================
//  tests/project_test.cpp — the project folder, its states and its lock
// -----------------------------------------------------------------------------
//  The decisive check is the long one at the end: a real Control Expert export
//  goes into a project folder, comes back out, is exported again, and the
//  resulting .XPG is compared against a direct export of the same model. If the
//  folder format loses anything - a comment, an in/out parameter, a per-element
//  label - the two files differ and this fails.
// =============================================================================
#include "../src/export/XpgWriter.hpp"
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/ProjectStore.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace project;
using namespace domain;
namespace fs = std::filesystem;

namespace {

std::string workspace(const char* name) {
    const auto p = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    return p.string();
}

std::shared_ptr<const Project> importXpg(const std::string& path) {
    core::EventBus bus;
    importer::ProjectImporter imp(bus);
    auto r = imp.importFile(path);
    return r ? r->project : nullptr;
}

} // namespace

int main(int argc, char** argv) {
    const std::string fixture = argc > 1 ? argv[1] : "tests/fixtures/MAST.XPG";

    // --- 1. security primitives --------------------------------------------
    {
        // FIPS 180-4 vectors: an implementation that fails these is worthless.
        assert(toHex(sha256("")) ==
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        assert(toHex(sha256("abc")) ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

        // A password is never stored, and two projects with the same password
        // get different hashes because the salt differs.
        const auto a = makeLock("hunter2", "AABBCCDDEEFF");
        const auto b = makeLock("hunter2", "AABBCCDDEEFF");
        assert(a.salt != b.salt);
        assert(a.passwordHash != b.passwordHash);
        assert(a.passwordHash.find("hunter2") == std::string::npos);

        assert(checkPassword(a, "hunter2"));
        assert(!checkPassword(a, "hunter3"));
        assert(!checkPassword(a, ""));
        assert(checkMaster(a, "AABBCCDDEEFF"));
        assert(checkMaster(a, "aa:bb:cc:dd:ee:ff") == false && "the caller normalises, not us");
        assert(!checkMaster(a, "112233445566"));

        MasterKey key;
        assert(!key.defined());
        assert(MasterKey::looksLikeMac("A4:BB:6D:12:9F:03"));
        assert(MasterKey::looksLikeMac("A4-BB-6D-12-9F-03"));
        assert(!MasterKey::looksLikeMac("not a mac"));
        assert(!MasterKey::looksLikeMac("A4:BB:6D:12:9F"));
        key.setFromMac("a4:bb:6d:12:9f:03");
        assert(key.defined());
        assert(key.stored().find("A4BB") == std::string::npos && "the MAC is not stored in clear");
        assert(key.matches("A4-BB-6D-12-9F-03") && "separators and case must not matter");
        assert(!key.matches("A4:BB:6D:12:9F:04"));
        std::printf("security: SHA-256 vectors, salted hashes and MAC normalisation OK\n");
    }

    // --- 2. an empty project ------------------------------------------------
    {
        const auto dir = workspace("xpg-new-project");
        auto p = ProjectStore::createEmpty("Machine A", "BMXP342020");
        assert(p->tasks.size() == 1 && "every project needs a MAST task");

        Manifest m;
        m.name = "Machine A";
        m.state = State::New;
        assert(ProjectStore::save(*p, m, dir).has_value());
        assert(ProjectStore::isProjectFolder(dir));
        assert(fs::exists(fs::path(dir) / "project.xpgproj"));
        assert(fs::exists(fs::path(dir) / "config" / "tasks.txt"));

        auto opened = ProjectStore::open(dir);
        assert(opened && opened->project);
        assert(opened->manifest.name == "Machine A");
        assert(opened->manifest.state == State::New);
        assert(opened->project->tasks.size() == 1);
        std::printf("new project: created, saved and reopened\n");
    }

    // --- 3. import, store as a folder, reopen, re-export ---------------------
    auto original = importXpg(fixture);
    assert(original && "the reference export must import");

    const auto dir = workspace("xpg-roundtrip-project");
    {
        Manifest m;
        m.name         = original->header.projectName;
        m.version      = original->header.projectVersion;
        m.state        = State::Dev;
        m.cpuReference = original->hardware.cpuReference;
        m.company      = original->header.company;
        m.product      = original->header.product;
        m.dtdVersion   = original->header.dtdVersion;
        assert(ProjectStore::save(*original, m, dir).has_value());

        // The layout is the one that was asked for, and it is browsable.
        assert(fs::exists(fs::path(dir) / "vars" / "globals.txt"));
        assert(fs::exists(fs::path(dir) / "ddt"));
        assert(fs::exists(fs::path(dir) / "dfb"));
        assert(fs::exists(fs::path(dir) / "units"));
        assert(fs::exists(fs::path(dir) / "sections" / "index.txt"));

        std::size_t stFiles = 0;
        for (const auto& e : fs::recursive_directory_iterator(dir))
            if (e.is_regular_file() && e.path().extension() == ".st") ++stFiles;
        std::printf("folder: %zu .st files, one per section\n", stFiles);
        assert(stFiles == original->sections.size() && "every section is its own editable file");
    }

    auto reopened = ProjectStore::open(dir);
    assert(reopened && reopened->project);
    const auto& r = *reopened->project;

    std::printf("reopened: %zu variables, %zu DDT, %zu POUs, %zu sections\n",
                r.variables.size(), r.derivedTypes.size(), r.pous.size(), r.sections.size());
    assert(r.derivedTypes.size() == original->derivedTypes.size());
    assert(r.sections.size() == original->sections.size());
    assert(r.variables.size() == original->variables.size());
    assert(r.tasks.size() == original->tasks.size());

    // --- 4. the export must match a direct export of the same model ----------
    {
        exporter::XpgWriteOptions opts;
        opts.dateTime = "date_and_time#2026-1-1-0:00:00";

        auto direct = exporter::writeXpg(*original, opts);
        auto viaFolder = exporter::writeXpg(r, opts);
        assert(direct && viaFolder);
        std::printf("export: direct %zu bytes, via the folder %zu bytes\n",
                    direct->size(), viaFolder->size());
        assert(*direct == *viaFolder
               && "storing a project as a folder must not change what it exports");

        auto written = ProjectStore::exportSources(r, dir);
        assert(written && !written->empty());
        assert(fs::exists(fs::path(dir) / "src" / "MAST.XPG"));
        std::printf("exported: %s\n", written->front().c_str());
    }

    // --- 5. duplicate -------------------------------------------------------
    {
        const auto copy = workspace("xpg-duplicated-project");
        assert(ProjectStore::duplicate(dir, copy, "Machine A - variant").has_value());
        auto m = ProjectStore::readManifest(copy);
        assert(m && m->name == "Machine A - variant");
        assert(m->state == State::Dev && "a copy is always editable");
        assert(!fs::exists(fs::path(copy) / "src") && "generated sources are not copied");

        // Duplicating onto an existing folder must refuse rather than merge.
        assert(!ProjectStore::duplicate(dir, copy, "again").has_value());
    }

    // --- 6. states and the lock ---------------------------------------------
    {
        const auto locked = workspace("xpg-locked-project");
        Manifest m;
        m.name  = "Locked";
        m.state = State::Lock;
        const auto lock = makeLock("s3cret", "A4BB6D129F03");
        assert(ProjectStore::save(*original, m, locked, lock).has_value());
        assert(fs::exists(fs::path(locked) / "project.lock"));

        // Opening without a password gives the manifest and nothing else.
        auto refused = ProjectStore::open(locked);
        assert(refused && refused->needsPassword);
        assert(refused->project == nullptr && "not one section is parsed before authentication");

        assert(!ProjectStore::open(locked, "wrong").has_value());
        auto byPassword = ProjectStore::open(locked, "s3cret");
        assert(byPassword && byPassword->project);
        assert(byPassword->project->sections.size() == original->sections.size());

        // The master key is the recovery path when the password is forgotten.
        auto byMaster = ProjectStore::open(locked, "", "A4:BB:6D:12:9F:03");
        assert(!byMaster.has_value() && "the caller must normalise the MAC first");
        auto byMasterNormalised = ProjectStore::open(locked, "",
                                                     MasterKey::normaliseMac("A4:BB:6D:12:9F:03"));
        assert(byMasterNormalised && byMasterNormalised->project);

        // Saving in any other state removes the lock file.
        Manifest dev = m;
        dev.state = State::Dev;
        assert(ProjectStore::save(*original, dev, locked).has_value());
        assert(!fs::exists(fs::path(locked) / "project.lock"));
        assert(ProjectStore::open(locked)->needsPassword == false);

        std::printf("lock: refuses without the password, opens with it or with the master key\n");
    }

    // --- 7. states round-trip through the manifest ---------------------------
    for (auto state : {State::New, State::Dev, State::Finish, State::Lock}) {
        assert(stateFromString(toString(state)) == state);
    }

    std::printf("\nproject_test: all assertions passed\n");
    return 0;
}
