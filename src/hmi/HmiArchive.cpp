#include "HmiArchive.hpp"

#include "HmiAssets.hpp"
#include "HmiPipeline.hpp"      // 1.12.31 : les artefacts du cache de construction, refaits au deballage
#include "HmiRecording.hpp"     // 1.14.0 (relecture M4b) : le dossier des enregistrements
#include "../core/AtomicFile.hpp"
#include "../project/ProjectArchive.hpp"
#include "../core/Version.hpp"
#include "../project/Security.hpp"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>

namespace fs = std::filesystem;
namespace pa = project::archive;

namespace hmi {

namespace {

// Les sections du manifeste : leur cle (enregistree) et leur libelle (affiche).
struct SectionName { const char* key; const char* label; };
constexpr SectionName kSections[] = {
    {"vues", "Vues"}, {"objets", "Objets"}, {"calques", "Calques"}, {"ressources", "Ressources"},
    {"fichiers_externes", "Fichiers externes"}, {"scripts", "Scripts"}, {"variables", "Variables IHM"},
    {"fonctions", "Fonctions IHM"},                                      // lot 7 (absente des archives plus anciennes)
    {"actions", "Actions"}, {"alarmes", "Alarmes"}, {"recettes", "Recettes"}, {"jeux", "Jeux de recette"},
    {"utilisateurs", "Utilisateurs"}, {"groupes", "Groupes"}, {"roles", "R\xC3\xB4les"},
    {"historique", "Entr\xC3\xA9" "es d'historique"},
};
// 1.12.30 : l'archive du projet entier compte aussi ses versions.
constexpr SectionName kVersions = {"versions", "Versions"};

std::string toolName() { return std::string(XPG_PRODUCT_IHM) + " " + XPG_ANALYZER_VERSION; }

std::shared_ptr<const pa::Bytes> bytesOf(const std::string& text) {
    return std::make_shared<const pa::Bytes>(text.begin(), text.end());
}

std::string keyOf(const std::string& label) {
    for (const auto& s : kSections) if (label == s.label) return s.key;
    if (label == kVersions.label) return kVersions.key;
    return label;
}

std::vector<pa::Count> toCounts(const std::vector<ArchiveCount>& counts) {
    std::vector<pa::Count> out;
    for (const auto& c : counts) {
        pa::Count x;
        x.key = keyOf(c.section);
        x.label = c.section;
        x.expected = c.expected;
        x.found = c.found;
        out.push_back(std::move(x));
    }
    return out;
}

// Le format ecrit dans ihm.txt ("ihm format=25 ...") ; 0 : illisible.
int dataFormatOf(std::string_view index) {
    std::size_t at = 0;
    while (at < index.size()) {
        const auto eol = index.find('\n', at);
        const auto line = index.substr(at, eol == std::string_view::npos ? std::string_view::npos : eol - at);
        if (line.rfind("ihm format=", 0) == 0) return static_cast<int>(std::strtol(std::string(line.substr(11, 6)).c_str(), nullptr, 10));
        if (eol == std::string_view::npos) break;
        at = eol + 1;
    }
    return 0;
}

void fillFrom(ArchiveReport& rep, const pa::Contents& c) {
    rep.entries.clear();
    for (const auto& e : c.manifest.entries) rep.entries.push_back({e.path, e.bytes, e.sha256, e.ok});
    rep.problems = c.problems;
    rep.created = c.manifest.created;
    rep.projectName = c.manifest.project;
    rep.archiveBytes = c.archiveBytes;
    rep.format = c.hasManifest ? c.manifest.format : 0;
    rep.tool = c.manifest.tool;
}

void fillFrom(ArchiveReport& rep, const pa::Manifest& m, std::uint64_t bytes) {
    rep.entries.clear();
    for (const auto& e : m.entries) rep.entries.push_back({e.path, e.bytes, e.sha256, true});
    rep.created = m.created;
    rep.projectName = m.project;
    rep.archiveBytes = bytes;
    rep.format = m.format;
    rep.tool = m.tool;
}

// L'historique d'une archive (ses CSV et la memoire des reglages PID).
History historyOf(const pa::Contents& c, std::vector<std::string>& problems) {
    History h;
    for (const auto which : kHistoryFiles) {
        const auto* text = c.file("ihm/historique/" + std::string(which) + ".csv");
        if (!text) continue;
        std::string error;
        if (!parseHistoryCsv(*text, which, h, &error)) problems.push_back("historique " + std::string(which) + ".csv : " + error);
    }
    const std::string pid = "ihm/historique/pid/";
    for (auto it = c.files.lower_bound(pid); it != c.files.end() && it->first.rfind(pid, 0) == 0; ++it) {
        const std::string leaf = it->first.substr(pid.size());
        if (leaf.size() > 4 && leaf.compare(leaf.size() - 4, 4, ".mem") == 0 && leaf.find('/') == std::string::npos)
            h.pid[leaf.substr(0, leaf.size() - 4)] = it->second;
    }
    return h;
}

// Comparer les comptes relus a ceux du manifeste (par cle).
void compareCounts(ArchiveReport& rep, std::vector<ArchiveCount> found, const pa::Manifest& m) {
    rep.counts = std::move(found);
    for (auto& c : rep.counts) {
        const std::string key = keyOf(c.section);
        for (const auto& expected : m.counts) {
            if (expected.key != key) continue;
            c.expected = expected.expected;
            if (!c.ok())
                rep.problems.push_back(c.section + " : " + std::to_string(expected.expected) + " annonc\xC3\xA9(s), "
                                       + std::to_string(c.found) + " relu(s)");
        }
    }
}

std::string folderLeaf(const std::string& folder) {
    fs::path p(folder);
    if (!p.has_filename()) p = p.parent_path();
    return p.filename().string();
}

// Un fichier externe : son chemin relatif au dossier s'il y est ("" sinon).
std::string insideFolder(const std::string& resolved, const std::string& folder) {
    std::error_code ec;
    const fs::path a = fs::weakly_canonical(fs::path(resolved), ec);
    const fs::path b = fs::weakly_canonical(fs::path(folder), ec);
    if (ec) return {};
    const fs::path rel = a.lexically_relative(b);
    const std::string s = rel.generic_string();
    if (s.empty() || s == "." || s.rfind("..", 0) == 0 || rel.is_absolute()) return {};
    return s;
}

} // namespace

RecordingsShare recordingsShare(const RecordingSettings& settings, const std::string& projectFolder) {
    RecordingsShare out;
    const std::string native = rec::folderOf(RecordingSettings{}, projectFolder);
    const std::string mine = rec::folderOf(settings, projectFolder);
    if (mine != native) {
        out.folder = mine;
        out.outside = !mine.empty() && insideFolder(mine, projectFolder).empty();
    }
    std::set<std::string> seen;
    for (const auto& dir : {native, mine}) {
        if (dir.empty() || !seen.insert(dir).second) continue;
        std::error_code ec;
        for (fs::directory_iterator it(fs::path(std::u8string(dir.begin(), dir.end())), fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            const auto u = it->path().filename().u8string();
            const std::string leaf(u.begin(), u.end());
            if (!it->is_regular_file(ec) || leaf.size() < 5 || leaf.compare(leaf.size() - 5, 5, ".xrec") != 0) continue;
            ++out.files;
            out.bytes += static_cast<std::uint64_t>(it->file_size(ec));
        }
    }
    return out;
}

std::vector<ArchiveCount> projectCounts(const Project& p, const History* h) {
    const auto st = p.statistics();
    long long jeux = 0;
    for (const auto& r : p.recipes) jeux += static_cast<long long>(r.records.size());
    const long long history = h ? static_cast<long long>(h->alarms.size() + h->events.size() + h->system.size() + h->samples.size()
                                                        + h->audit.size()) : 0;     // lot 13 : l'audit compte
    const long long values[] = {
        static_cast<long long>(st.views), static_cast<long long>(st.objects), static_cast<long long>(st.layers),
        static_cast<long long>(st.resources), static_cast<long long>(p.assets.files.size()), static_cast<long long>(st.scripts),
        static_cast<long long>(p.programs.variables.size()), static_cast<long long>(p.programs.functions.size()),
        static_cast<long long>(st.actions),
        static_cast<long long>(p.alarms.size()), static_cast<long long>(p.recipes.size()), jeux,
        static_cast<long long>(p.security.users.size()), static_cast<long long>(p.security.groups.size()),
        static_cast<long long>(p.security.roles.size()), history,
    };
    std::vector<ArchiveCount> out;
    for (std::size_t i = 0; i < std::size(kSections); ++i) out.push_back({kSections[i].label, values[i], values[i]});
    return out;
}

// ============================================================ l'IHM en memoire ==
core::Status exportArchive(const Project& p, const History* history, const std::string& zipPath, ArchiveReport* report) {
    ArchiveReport local;
    auto& rep = report ? *report : local;
    rep = ArchiveReport{};
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    std::vector<pa::Item> items;
    int format = 0;
    for (const auto& f : serializeProject(p)) {
        if (f.path == "ihm.txt") format = dataFormatOf(std::string_view(reinterpret_cast<const char*>(f.data->data()), f.data->size()));
        items.push_back({"ihm/" + f.path, f.data, now, pa::Origin::Folder, {}});
    }
    if (history && !history->empty()) {
        for (const auto which : kHistoryFiles)
            items.push_back({"ihm/historique/" + std::string(which) + ".csv", bytesOf(historyCsv(*history, which)), now, pa::Origin::Folder, {}});
        for (const auto& [k, blob] : history->pid)
            items.push_back({"ihm/historique/pid/" + pidMemoryName(k) + ".mem", bytesOf(blob), now, pa::Origin::Folder, {}});
    }
    pa::Manifest m;
    m.kind = pa::Kind::Ihm;
    m.kindKnown = true;
    m.tool = toolName();
    m.created = nowStamp();
    m.project = p.config.name;
    m.dataFormat = format;
    rep.counts = projectCounts(p, history);
    m.counts = toCounts(rep.counts);
    auto zip = pa::pack(m, items);
    if (!zip) return core::Err<core::Error>(zip.error());
    fillFrom(rep, m, zip->size());
    return pa::writeFile(zipPath, *zip);
}

core::Result<Project> importArchive(const std::string& zipPath, History* history, ArchiveReport* report, LoadReport* load) {
    ArchiveReport local;
    auto& rep = report ? *report : local;
    rep = ArchiveReport{};
    auto contents = pa::readFile(zipPath);
    if (!contents) return core::fail(contents.error().code, "archive illisible : " + contents.error().context);
    fillFrom(rep, *contents);
    if (contents->newer) return core::fail(core::ErrorCode::NotImplemented, refusal(*contents));
    if (contents->hasManifest && contents->manifest.kindKnown && contents->manifest.kind == pa::Kind::Api)
        return core::fail(core::ErrorCode::InvalidArgument, refusal(*contents));

    // Le projet, relu par le meme code que le dossier ihm/.
    const auto read = [&contents](const std::string& path, std::string& content) {
        const auto* f = contents->file("ihm/" + path);
        if (!f) return false;
        content = *f;
        return true;
    };
    auto project = parseProject(read, load);
    if (!project) return core::fail(project.error().code, "archive : " + project.error().context);
    History imported = historyOf(*contents, rep.problems);
    // Le controle de coherence : chaque compte annonce, retrouve.
    compareCounts(rep, projectCounts(*project, &imported), contents->manifest);
    if (history) *history = std::move(imported);
    return project;
}

// ============================================================ le projet entier ==
core::Result<pa::Bytes> projectArchive(const std::string& folder, const ProjectArchiveOptions& opt, ArchiveReport* report) {
    ArchiveReport local;
    auto& rep = report ? *report : local;
    rep = ArchiveReport{};
    if (!exists(folder))
        return core::fail(core::ErrorCode::FileNotFound,
                          "ce dossier n'a pas d'IHM enregistr\xC3\xA9" "e (ihm/ihm.txt manque) : enregistre d'abord le projet");
    LoadReport loaded;
    auto project = load(folder, &loaded);
    if (!project) return core::fail(project.error().code, "l'IHM du dossier ne se relit pas : " + project.error().context);
    const History h = opt.history ? loadHistory(folder) : History{};

    std::vector<pa::Skipped> skipped;
    pa::CollectOptions co{opt.versions, opt.history, opt.recordings};
    // 1.14.0 (relecture M4b) : le dossier des enregistrements de la configuration - dans le projet, la case le prend ou
    // le laisse comme le natif ; ailleurs (un chemin absolu), ses .xrec sont joints dans le dossier natif de l'archive.
    const std::string recFolder = rec::folderOf(project->recording, folder);
    const std::string recInside = recFolder.empty() ? std::string{} : insideFolder(recFolder, folder);
    co.recordingsDir = recInside;
    auto items = pa::collectFolder(folder, co, &skipped);
    if (!items) return core::Err<core::Error>(items.error());
    for (const auto& s : skipped)
        rep.skipped.push_back(s.what + " : " + s.why + " (" + std::to_string(s.files) + " fichier(s), " + pa::sizeText(s.bytes) + ")");

    // Les fichiers externes : dans le dossier, ils y sont deja ; ailleurs, ils viennent avec.
    std::set<std::string> have;
    for (const auto& it : *items) have.insert(it.path);
    if (opt.recordings && !recFolder.empty() && recInside.empty()) {
        std::size_t joined = 0;
        std::error_code ec;
        for (fs::directory_iterator it(fs::path(std::u8string(recFolder.begin(), recFolder.end())), fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            const auto u = it->path().filename().u8string();
            const std::string leaf(u.begin(), u.end());
            if (!it->is_regular_file(ec) || leaf.size() < 5 || leaf.compare(leaf.size() - 5, 5, ".xrec") != 0) continue;
            const std::string inside = std::string(co.nativeRecordings) + "/" + leaf;
            if (have.count(inside)) continue;
            pa::Item item;
            const auto full = it->path().u8string();
            if (!pa::readItem(std::string(full.begin(), full.end()), item)) continue;
            item.path = inside;
            have.insert(inside);
            items->push_back(std::move(item));
            ++joined;
        }
        if (joined > 0)
            rep.notes.push_back(std::to_string(joined) + " enregistrement(s) joint(s) depuis " + recFolder
                                + " (hors du projet) : dans l'archive, ils sont dans ihm/ressources/enregistrements");
    }
    for (const auto& f : project->assets.files) {
        if (f.kind == ExternalKind::Database) continue;
        const std::string resolved = resolveExternalPath(f, folder);
        std::error_code ec;
        if (!fs::is_regular_file(fs::path(resolved), ec)) {
            rep.notes.push_back("fichier externe \xC2\xAB " + f.name + " \xC2\xBB introuvable (" + f.path + ") : il n'est pas dans l'archive");
            continue;
        }
        const std::string inside = insideFolder(resolved, folder);
        if (!inside.empty()) {
            if (have.count(inside)) continue;
            pa::Item it;
            if (!pa::readItem(resolved, it)) continue;
            it.path = inside;
            have.insert(inside);
            items->push_back(std::move(it));
            rep.notes.push_back("fichier externe \xC2\xAB " + f.name + " \xC2\xBB pris aussi (" + inside + ")");
            continue;
        }
        pa::Item it;
        if (!pa::readItem(resolved, it)) {
            rep.notes.push_back("fichier externe \xC2\xAB " + f.name + " \xC2\xBB illisible (" + resolved + ") : il n'est pas dans l'archive");
            continue;
        }
        it.path = "_externes/" + std::to_string(f.id) + "-" + fs::path(resolved).filename().string();
        it.origin = pa::Origin::External;
        it.link = f.path;
        items->push_back(std::move(it));
        rep.notes.push_back("fichier externe \xC2\xAB " + f.name + " \xC2\xBB joint (il est hors du projet : " + resolved + ")");
    }

    pa::Manifest m;
    m.kind = pa::Kind::Ihm;
    m.kindKnown = true;
    m.tool = opt.tool.empty() ? toolName() : opt.tool;
    m.created = nowStamp();
    m.project = project->config.name;
    m.folder = folderLeaf(folder);
    std::string index;
    if (core::readFileAll(fs::path(folder) / "ihm" / "ihm.txt", index)) m.dataFormat = dataFormatOf(index);
    rep.counts = projectCounts(*project, &h);
    const long long versions = opt.versions ? pa::versionCount(folder) : 0;
    rep.counts.push_back({kVersions.label, versions, versions});
    m.counts = toCounts(rep.counts);
    auto zip = pa::pack(m, *items);
    if (!zip) return zip;
    fillFrom(rep, m, zip->size());
    return zip;
}

core::Status exportProjectArchive(const std::string& folder, const std::string& path, const ProjectArchiveOptions& opt,
                                  ArchiveReport* report) {
    ArchiveReport local;
    auto& rep = report ? *report : local;
    auto zip = projectArchive(folder, opt, &rep);
    if (!zip) return core::Err<core::Error>(zip.error());
    return pa::writeFile(path, *zip);
}

std::string refusal(const pa::Contents& c) {
    if (!c.hasManifest)
        return "ce n'est pas une archive de projet : manifeste.txt manque (un zip fait \xC3\xA0 la main ne s'ouvre pas comme un projet)";
    if (c.newer)
        return "cette archive a \xC3\xA9t\xC3\xA9 \xC3\xA9" "crite par une version plus r\xC3\xA9" "cente ("
             + (c.manifest.tool.empty() ? std::string("format ") + std::to_string(c.manifest.format) : c.manifest.tool)
             + ") : cette version d'" + std::string(XPG_PRODUCT_IHM) + " (" + XPG_ANALYZER_VERSION + ") ne sait pas la relire en entier. "
               "Mets " + XPG_PRODUCT_IHM + " \xC3\xA0 jour pour l'ouvrir";
    if (c.manifest.kindKnown && c.manifest.kind == pa::Kind::Api)
        return "c'est l'archive d'un projet de l'automate (.xapi) : elle s'ouvre avec API Studio";
    if (c.manifest.dataFormat > kFormatVersion)
        return "son projet IHM est au format " + std::to_string(c.manifest.dataFormat) + ", plus r\xC3\xA9" "cent que celui de cette version ("
             + std::to_string(kFormatVersion) + ", " + XPG_PRODUCT_IHM + " " + XPG_ANALYZER_VERSION + ") : mets "
             + XPG_PRODUCT_IHM + " \xC3\xA0 jour pour l'ouvrir";
    if (!c.file("ihm/ihm.txt") && !c.damaged.count("ihm/ihm.txt"))
        return "cette archive ne contient pas d'IHM (ihm/ihm.txt manque)";
    return {};
}

core::Status unpackProjectArchive(const pa::Contents& c, const std::string& folder, ArchiveReport* report, bool skipDamaged) {
    ArchiveReport local;
    auto& rep = report ? *report : local;
    rep = ArchiveReport{};
    fillFrom(rep, c);
    if (const std::string why = refusal(c); !why.empty()) return core::fail(core::ErrorCode::InvalidArgument, why);
    pa::UnpackOptions uo;
    uo.skipDamaged = skipDamaged;
    pa::Unpacked u;
    if (auto st = pa::unpack(c, folder, uo, &u); !st) return st;
    rep.folder = folder;
    rep.notes = u.notes;

    // Une archive de la 1.x (l'IHM seule) : le dossier recoit sa fiche.
    std::error_code ec;
    if (!fs::exists(fs::path(folder) / "project.xpgproj", ec)) {
        const std::string name = c.manifest.project.empty() ? folderLeaf(folder) : c.manifest.project;
        const std::string now = pa::nowText() + ":00";
        std::string m = "# IHM Studio project. Safe to read; edit with care.\n";
        m += "formatVersion = 1\nname = " + name + "\nversion = 0.0.1\nstate = DEV\ncreated = " + now + "\nmodified = " + now
           + "\nauthor = \ncpu = \nfirmware = \ncomment = Recr\xC3\xA9\xC3\xA9 depuis une archive de l'IHM\nedition = ihm\n";
        if (auto st = core::writeFileAtomic(fs::path(folder) / "project.xpgproj", m, {false, false}); !st) return st;
        rep.notes.push_back("archive de l'IHM seule (1.x) : la fiche du projet (project.xpgproj) est cr\xC3\xA9\xC3\xA9" "e");
    }

    // Les fichiers externes lies hors du dossier : deja la, identiques, ou relies a leur copie.
    if (!u.externals.empty()) {
        LoadReport lr;
        if (auto p = load(folder, &lr)) {
            bool changed = false;
            for (std::size_t i = 0; i < u.externals.size(); ++i) {
                const auto& e = u.externals[i];
                const std::string& copy = u.externalPaths[i];
                for (auto& f : p->assets.files) {
                    if (f.path != e.link || f.kind == ExternalKind::Database) continue;
                    const std::string original = resolveExternalPath(f, folder);
                    std::string there;
                    const bool present = fs::is_regular_file(fs::path(original), ec);
                    if (present && core::readFileAll(fs::path(original), there)
                        && project::toHex(project::sha256(there)) == e.sha256) {
                        fs::remove(fs::path(folder) / fs::path(copy), ec);
                        rep.notes.push_back("fichier externe \xC2\xAB " + f.name + " \xC2\xBB : d\xC3\xA9j\xC3\xA0 l\xC3\xA0, identique ("
                                            + original + ") : le lien ne change pas");
                    } else {
                        rep.notes.push_back("fichier externe \xC2\xAB " + f.name + " \xC2\xBB : " + e.link
                                            + (present ? " est diff\xC3\xA9rent ici" : " n'existe pas ici")
                                            + " ; il est reli\xC3\xA9 \xC3\xA0 sa copie, " + copy + " (dans le projet)");
                        f.path = copy;
                        changed = true;
                    }
                }
            }
            if (changed) {
                const auto files = serializeProject(*p);
                if (!files.empty() && files.front().path == "ihm.txt") {
                    const auto& d = *files.front().data;
                    if (auto st = core::writeFileAtomic(fs::path(folder) / "ihm" / "ihm.txt",
                                                        std::string_view(reinterpret_cast<const char*>(d.data()), d.size()), {false, true});
                        !st)
                        return st;
                }
            }
            if (fs::is_empty(fs::path(folder) / "externes", ec) && !ec) fs::remove(fs::path(folder) / "externes", ec);
        }
    }

    // Le controle : le projet relu, chaque compte annonce retrouve.
    LoadReport lr;
    auto p = load(folder, &lr);
    if (!p) {
        rep.problems.push_back("l'IHM d\xC3\xA9" "ball\xC3\xA9" "e ne se relit pas : " + p.error().context);
        return core::ok();
    }
    for (const auto& w : lr.warnings) rep.notes.push_back("lecture : " + w);
    // 1.12.31 : le cache de construction est venu avec - ses artefacts, refaits d'apres lui (chacun a son empreinte).
    // L'API vide : c'est ce que voit le build d'IHM Studio (il ne lit rien du programme : ProjectStore::open lui
    // rend un modele vide, et sans automate pas d'elements de l'API) ; un artefact qui n'aurait pas exactement
    // l'empreinte du cache n'est de toute facon pas pose (il se regenere au build).
    if (c.file(".xpg/build/build-cache.txt") && fs::is_regular_file(fs::path(pipeline::buildFolderOf(folder)) / "build-cache.txt", ec)) {
        const auto restored = pipeline::restoreArtifacts(*p, pipeline::ApiInfo{}, pipeline::buildFolderOf(folder));
        rep.buildCache = restored.cache;
        rep.artifacts = restored.written + restored.present;
        rep.stale = restored.stale;
    }
    std::vector<std::string> historyProblems;
    const History h = loadHistory(folder, &historyProblems);
    for (const auto& w : historyProblems) rep.problems.push_back(w);
    auto found = projectCounts(*p, &h);
    const long long versions = pa::versionCount(folder);
    found.push_back({kVersions.label, versions, versions});
    compareCounts(rep, std::move(found), c.manifest);
    return core::ok();
}

} // namespace hmi
