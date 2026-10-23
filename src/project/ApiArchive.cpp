// =============================================================================
//  project/ApiArchive.cpp - 1.12.30 : voir ApiArchive.hpp
// =============================================================================
#include "ApiArchive.hpp"

#include "GrafcetLayoutFile.hpp"
#include "ProjectStore.hpp"
#include "../core/AtomicFile.hpp"
#include "../domain/ProjectModel.hpp"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace project::archive {

namespace {

struct CountName { const char* key; const char* label; };
constexpr CountName kApiCounts[] = {
    {"sections", "Sections"}, {"unites", "Unit\xC3\xA9s de programme"}, {"dfb", "Blocs DFB"}, {"ddt", "Types d\xC3\xA9riv\xC3\xA9s (DDT)"},
    {"variables", "Variables globales"}, {"taches", "T\xC3\xA2" "ches"}, {"modules", "Modules"}, {"tables", "Tables d'animation"},
    {"versions", "Versions"},
};

std::string folderLeaf(const std::string& folder) {
    fs::path p(folder);
    if (!p.has_filename()) p = p.parent_path();
    return p.filename().string();
}

} // namespace

std::vector<Count> apiCounts(const domain::Project& p, long long versions) {
    long long units = 0, dfbs = 0, globals = 0, modules = 0;
    for (const auto& pou : p.pous) {
        if (pou.kind == domain::PouKind::ProgramUnit) ++units;
        else if (pou.kind == domain::PouKind::FunctionBlockType && pou.userDefined) ++dfbs;
    }
    for (const auto& v : p.variables) globals += v.scope == domain::VariableScope::Global;
    for (const auto& r : p.hardware.racks) modules += static_cast<long long>(r.modules.size());
    const long long values[] = {
        static_cast<long long>(p.sections.size()), units, dfbs, static_cast<long long>(p.derivedTypes.size()), globals,
        static_cast<long long>(p.tasks.size()), modules, static_cast<long long>(p.animationTables.size()), versions,
    };
    std::vector<Count> out;
    for (std::size_t i = 0; i < std::size(kApiCounts); ++i) {
        Count c;
        c.key = kApiCounts[i].key;
        c.label = kApiCounts[i].label;
        c.expected = c.found = values[i];
        out.push_back(std::move(c));
    }
    return out;
}

core::Result<Bytes> apiArchive(const std::string& folder, const ApiOptions& opt, ApiReport* report) {
    ApiReport local;
    auto& rep = report ? *report : local;
    rep = ApiReport{};
    auto manifest = ProjectStore::readManifest(folder);
    if (!manifest) return core::fail(core::ErrorCode::FileNotFound, "ce dossier n'est pas un projet (project.xpgproj manque) : " + folder);
    auto items = collectFolder(folder, opt.collect, &rep.skipped);
    if (!items) return core::Err<core::Error>(items.error());

    // A cote du dossier : la disposition des grafcets (<projet>.xpglayout).
    {
        fs::path p(folder);
        if (!p.has_filename()) p = p.parent_path();
        const std::string beside = LayoutFile::pathFor(p.string());
        std::error_code ec;
        Item it;
        if (fs::is_regular_file(beside, ec) && readItem(beside, it)) {
            it.path = "_a-cote/" + fs::path(beside).filename().string();
            it.origin = Origin::Beside;
            items->push_back(std::move(it));
        }
    }
    // Au choix : les macros de la bibliotheque (pas la corbeille).
    if (opt.macros && !opt.libraryRoot.empty()) {
        const fs::path macros = fs::path(opt.libraryRoot) / "Macros";
        std::error_code ec;
        if (fs::is_directory(macros, ec))
            for (fs::recursive_directory_iterator it(macros, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
                 it.increment(ec)) {
                std::error_code e2;
                std::string rel;
                try {
                    rel = fs::relative(it->path(), macros, e2).generic_string();
                } catch (...) {
                    continue;
                }
                if (it->is_directory(e2)) {
                    if (rel == "_corbeille") it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file(e2) || rel.empty() || !safePath(rel)) continue;
                Item m;
                if (!readItem(it->path().string(), m)) continue;
                m.path = "_macros/" + rel;
                m.origin = Origin::Library;
                items->push_back(std::move(m));
            }
    }

    Manifest& m = rep.manifest;
    m = Manifest{};
    m.kind = Kind::Api;
    m.kindKnown = true;
    m.tool = opt.tool.empty() ? std::string("API Studio") : opt.tool;
    m.created = nowText();
    m.project = manifest->name;
    m.folder = folderLeaf(folder);
    if (auto opened = ProjectStore::openModel(folder); opened && opened->project) {
        m.counts = apiCounts(*opened->project, versionCount(folder));
    } else if (opened && opened->needsPassword) {
        rep.notes.push_back("projet verrouill\xC3\xA9 : ses comptes ne sont pas annonc\xC3\xA9s (les fichiers, eux, sont tous v\xC3\xA9rifi\xC3\xA9s)");
    } else if (!opened) {
        return core::fail(core::ErrorCode::FileUnreadable, "le projet ne se relit pas : " + opened.error().context);
    }
    rep.counts = m.counts;
    auto zip = pack(m, *items);
    if (zip) rep.archiveBytes = zip->size();
    return zip;
}

core::Status exportApiArchive(const std::string& folder, const std::string& path, const ApiOptions& opt, ApiReport* report) {
    ApiReport local;
    auto& rep = report ? *report : local;
    auto zip = apiArchive(folder, opt, &rep);
    if (!zip) return core::Err<core::Error>(zip.error());
    // L'archive ne s'ecrit pas dans le dossier qu'elle archive : elle s'y contiendrait a la prochaine.
    return writeFile(path, *zip);
}

core::Status unpackApiArchive(const Contents& c, const std::string& folder, const UnpackOptions& opt, ApiReport* report) {
    ApiReport local;
    auto& rep = report ? *report : local;
    rep = ApiReport{};
    rep.manifest = c.manifest;
    rep.archiveBytes = c.archiveBytes;
    if (c.hasManifest && c.manifest.kindKnown && c.manifest.kind != Kind::Api)
        return core::fail(core::ErrorCode::InvalidArgument,
                          "c'est l'archive d'un projet IHM (.xihm) : elle s'ouvre avec IHM Studio");
    if (auto st = unpack(c, folder, opt, &rep.unpacked); !st) return st;
    rep.notes = rep.unpacked.notes;
    for (const auto& p : c.problems) rep.problems.push_back(p);
    if (!ProjectStore::isProjectFolder(folder)) {
        rep.problems.push_back("project.xpgproj manque : le dossier d\xC3\xA9" "ball\xC3\xA9 n'est pas un projet");
        return core::ok();
    }
    // Le controle : le projet relu, chaque compte annonce retrouve.
    auto opened = ProjectStore::openModel(folder);
    if (!opened) {
        rep.problems.push_back("le projet d\xC3\xA9" "ball\xC3\xA9 ne se relit pas : " + opened.error().context);
        return core::ok();
    }
    if (!opened->project) {
        rep.notes.push_back("projet verrouill\xC3\xA9 : les comptes se refont \xC3\xA0 son ouverture (son mot de passe)");
        return core::ok();
    }
    rep.counts = apiCounts(*opened->project, versionCount(folder));
    for (auto& found : rep.counts) {
        for (const auto& expected : c.manifest.counts)
            if (expected.key == found.key) {
                found.expected = expected.expected;
                if (!found.ok())
                    rep.problems.push_back(found.label + " : " + std::to_string(expected.expected) + " annonc\xC3\xA9(s), "
                                           + std::to_string(found.found) + " relu(s)");
            }
    }
    return core::ok();
}

} // namespace project::archive
