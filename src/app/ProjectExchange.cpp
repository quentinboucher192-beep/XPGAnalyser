// =============================================================================
//  app/ProjectExchange.cpp - 1.12.30 : voir ProjectExchange.hpp
// =============================================================================
#include "ProjectExchange.hpp"

#include "App.hpp"
#include "BackgroundTasks.hpp"
#include "Dossiers.hpp"
#include "screens/Screens.hpp"
#include "../core/AtomicFile.hpp"
#include "../core/Edition.hpp"
#include "../core/Version.hpp"
#include "../hmi/HmiArchive.hpp"
#include "../hmi/HmiPipeline.hpp"    // 1.12.31 : les artefacts du cache de construction d'un dossier .xapi
#include "../project/ApiArchive.hpp"
#include "../project/ProjectArchive.hpp"
#include "../project/SharedLibrary.hpp"
#include "../ui/widgets/PathBrowse.hpp"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace app::exchange {

namespace fs = std::filesystem;
namespace pa = project::archive;

namespace {

// ---- les taches a cote --------------------------------------------------------------
struct Job {
    std::thread               thread;
    std::atomic<bool>         done{false};
    std::function<void(App&)> finish;
    int                       task{0};
};

std::vector<std::unique_ptr<Job>>& jobs() {
    static std::vector<std::unique_ptr<Job>> list;
    return list;
}

std::string& report() {
    static std::string text;
    return text;
}

// `work` tourne sur un autre fil ; `finish`, ensuite, sur celui de l'interface (poll).
void run(std::string label, std::function<void()> work, std::function<void(App&)> finish) {
    auto job = std::make_unique<Job>();
    job->task = bgtasks::begin(std::move(label));
    job->finish = std::move(finish);
    Job* raw = job.get();
    job->thread = std::thread([raw, w = std::move(work)] {
        try {
            w();
        } catch (...) {
        }
        raw->done.store(true, std::memory_order_release);
    });
    jobs().push_back(std::move(job));
}

// ---- les petits morceaux ----------------------------------------------------------
std::string leafOf(const std::string& path) {
    fs::path p(path);
    if (!p.has_filename()) p = p.parent_path();
    return p.filename().string();
}

std::string stemOf(const std::string& path) { return fs::path(path).stem().string(); }

// Un chemin colle : sans ses guillemets ni ses espaces autour.
std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '"' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '"')) ++i;
    return s.substr(i);
}

std::string errorText(const core::Error& e) { return e.context.empty() ? e.message() : e.context; }

std::uint64_t treeBytes(const fs::path& dir) {
    std::uint64_t total = 0;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) total += static_cast<std::uint64_t>(it->file_size(e2));
    }
    return total;
}

std::string toolName() { return core::productName() + " " + XPG_ANALYZER_VERSION; }

pa::Kind ownKind() { return core::hasApi() ? pa::Kind::Api : pa::Kind::Ihm; }

void say(App& app, std::string text, double seconds = 8.0) { app.events().publish(StatusNotice{std::move(text), seconds}); }

void tell(App& app, std::string title, std::string text, MessageDialog::Icon icon) {
    app.menus().ShowDialog(std::make_unique<MessageDialog>(std::move(title), std::move(text), icon), [](const menu::DialogResult&) {});
}

// F1 dans un dialogue : son sujet du centre d'aide (API Studio : celui de l'automate).
void helpOn(App& app, FormDialog& dialog, const char* ihmTopic) {
    dialog.setHelp([&app, topic = std::string(core::hasApi() ? "echanges-api" : ihmTopic)] {
        app.setHelpTopic(topic);
        app.menus().PushMenu(core::hasApi() ? "help" : "help.hmi");
    });
}

std::string listed(const std::vector<std::string>& items, std::size_t max = 6) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i == max) {
            out += "\n- \xE2\x80\xA6 et " + std::to_string(items.size() - max) + " autre(s)";
            break;
        }
        out += "\n- " + items[i];
    }
    return out;
}

// Le projet est-il pret a partir ? (un dossier, enregistre au moins une fois)
bool ready(App& app, const std::string& what) {
    if (!app.project() && !app.hmi()) {
        say(app, "Ouvre d'abord un projet.", 6.0);
        return false;
    }
    if (app.projectFolder().empty()) {
        tell(app, "Enregistre d'abord le projet",
             "Le projet n'a pas encore de dossier : " + what + " prend tout le dossier du projet. "
             "Projet \xE2\x80\xBA Enregistrer sous\xE2\x80\xA6 lui en donne un.",
             MessageDialog::Icon::Info);
        return false;
    }
    return true;
}

// Des modifications non enregistrees : les enregistrer d'abord, ou prendre le projet enregistre.
void withSaved(App& app, const std::string& what, std::function<void()> next) {
    if (!app.commands().isModified()) {
        next();
        return;
    }
    const std::string name = app.manifest().name.empty() ? leafOf(app.projectFolder()) : app.manifest().name;
    const std::string keep = "Les enregistrer d'abord";
    const std::string skip = "Prendre le projet enregistr\xC3\xA9, sans elles";
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Les modifications de " + name, keep, "", false, {keep, skip}});
    app.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.exchangeSave", "Modifications non enregistr\xC3\xA9" "es",
                                     name + " a des modifications qui ne sont pas encore enregistr\xC3\xA9" "es. " + what
                                         + " prend le projet tel qu'il est enregistr\xC3\xA9 sur le disque.",
                                     std::move(fields), "Continuer"),
        [&app, keep, then = std::move(next)](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty() || v.front() == keep) {
                if (auto st = app.saveProject(); !st) {
                    tell(app, "Enregistrement impossible", errorText(st.error()), MessageDialog::Icon::Error);
                    return;
                }
            }
            then();
        });
}

// ---- deballer, puis ouvrir ------------------------------------------------------------
std::string apiRefusal(const pa::Contents& c) {
    if (!c.hasManifest) return "ce n'est pas une archive de projet : son manifeste (manifeste.txt) manque";
    if (c.newer)
        return "cette archive a \xC3\xA9t\xC3\xA9 \xC3\xA9" "crite par une version plus r\xC3\xA9" "cente ("
             + (c.manifest.tool.empty() ? "format " + std::to_string(c.manifest.format) : c.manifest.tool) + ") : cette version d'"
             + std::string(XPG_PRODUCT_API) + " (" + XPG_ANALYZER_VERSION + ") ne sait pas la relire en entier. Mets "
             + XPG_PRODUCT_API + " \xC3\xA0 jour pour l'ouvrir";
    if (!c.file("project.xpgproj")) return "elle ne contient pas de projet de l'automate (project.xpgproj manque)";
    return {};
}

void unpackAndOpen(std::shared_ptr<pa::Contents> contents, const std::string& source, const std::string& folder, bool skipDamaged) {
    const bool api = contents->manifest.kindKnown && contents->manifest.kind == pa::Kind::Api;
    struct Out {
        bool                     ok{false};
        std::string              error;
        std::vector<std::string> problems, notes;
        std::size_t              files{0};
        std::size_t              counts{0};
        std::string              build;      // 1.12.31 : ce que dit le cache de construction deballe ("" : pas de cache)
    };
    auto out = std::make_shared<Out>();
    run("Ouverture de l'archive", [contents, folder, skipDamaged, api, out] {
        out->error = "le d\xC3\xA9" "ballage s'est interrompu";
        if (api) {
            pa::UnpackOptions uo;
            uo.skipDamaged = skipDamaged;
            uo.libraryRoot = project::SharedLibrary::defaultRoot();
            pa::ApiReport rep;
            if (auto st = pa::unpackApiArchive(*contents, folder, uo, &rep); !st) {
                out->error = errorText(st.error());
                return;
            }
            out->ok = true;
            out->problems = rep.problems;
            out->notes = rep.notes;
            if (!rep.unpacked.macrosAdded.empty())
                out->notes.push_back(std::to_string(rep.unpacked.macrosAdded.size())
                                     + " macro(s) ajout\xC3\xA9" "e(s) \xC3\xA0 ta biblioth\xC3\xA8que (elles n'y \xC3\xA9taient pas)");
            out->files = rep.unpacked.written.size();
            out->counts = rep.counts.size();
            // 1.12.31 : un dossier qui a aussi son IHM (un projet de la 1.11, ouvert par les deux) - le cache de son build
            // a suivi ; ses artefacts se refont ici, comme au deballage d'une .xihm : IHM Studio le trouvera a jour.
            std::error_code ec;
            if (contents->file(".xpg/build/build-cache.txt") && hmi::exists(folder)
                && fs::is_regular_file(fs::path(hmi::pipeline::buildFolderOf(folder)) / "build-cache.txt", ec))
                if (auto hp = hmi::load(folder)) (void)hmi::pipeline::restoreArtifacts(*hp, {}, hmi::pipeline::buildFolderOf(folder));
        } else {
            hmi::ArchiveReport rep;
            if (auto st = hmi::unpackProjectArchive(*contents, folder, &rep, skipDamaged); !st) {
                out->error = errorText(st.error());
                return;
            }
            out->ok = true;
            out->problems = rep.problems;
            out->notes = rep.notes;
            out->files = rep.entries.size();
            out->counts = rep.counts.size();
            // 1.12.31 : le cache de construction a suivi - le build reprend ou il en etait.
            if (rep.buildCache)
                out->build = rep.stale == 0
                                 ? std::string(" Son cache de construction a suivi : le build reprend o\xC3\xB9 il en \xC3\xA9tait.")
                                 : " Son cache de construction a suivi ; " + std::to_string(rep.stale)
                                       + (rep.stale > 1 ? " \xC3\xA9l\xC3\xA9ments ont chang\xC3\xA9 depuis le dernier build : ils seront refaits."
                                                        : " \xC3\xA9l\xC3\xA9ment a chang\xC3\xA9 depuis le dernier build : il sera refait.");
        }
    }, [out, folder, source](App& a) {
        if (!out->ok) {
            report() = "deballage impossible : " + out->error;
            tell(a, "D\xC3\xA9" "ballage impossible", leafOf(source) + " : " + out->error + ".", MessageDialog::Icon::Error);
            return;
        }
        report() = "archive deballee : " + folder + " (" + std::to_string(out->files) + " fichiers, " + std::to_string(out->problems.size())
                 + " probleme(s))";
        bgtasks::post({"echange:ouvert", "Projet", "Archive ouverte : " + leafOf(source), folder, "Ouvrir le dossier", "open.folder:" + folder, "ok"});
        // Le projet ouvert d'abord (ses modifications se demandent), puis celui de l'archive.
        App* app = &a;
        a.confirmReplace([app, out, folder] {
            app->openProjectFolder(folder);
            if (out->problems.empty() && out->notes.empty()) {
                say(*app, "Archive d\xC3\xA9" "ball\xC3\xA9" "e dans " + folder + " : " + std::to_string(out->files)
                             + " fichiers intacts, le contr\xC3\xB4le retrouve chaque compte (" + std::to_string(out->counts) + ")." + out->build, 12.0);
                return;
            }
            std::string text = "Le projet est d\xC3\xA9" "ball\xC3\xA9 dans " + folder + " (" + std::to_string(out->files) + " fichiers) et ouvert."
                             + out->build;
            if (!out->problems.empty()) text += "\n\nCe que le contr\xC3\xB4le n'a pas retrouv\xC3\xA9 :" + listed(out->problems);
            if (!out->notes.empty()) text += "\n\n\xC3\x80 savoir :" + listed(out->notes);
            tell(*app, "Projet ouvert depuis l'archive", text, out->problems.empty() ? MessageDialog::Icon::Info : MessageDialog::Icon::Warning);
        });
    });
}

void askWhere(App& app, std::shared_ptr<pa::Contents> c, const std::string& source, bool skipDamaged) {
    const auto& m = c->manifest;
    const std::string name = !m.folder.empty() ? m.folder : !m.project.empty() ? m.project : stemOf(source);
    const std::string root = dossiers::utf8De(App::projectsRoot());
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Dossier du projet", pa::freeFolder(root, name), "", false, {}});
    std::string text = "\xC2\xAB " + (m.project.empty() ? name : m.project) + " \xC2\xBB : " + std::to_string(m.entries.size()) + " fichiers, "
                     + pa::sizeText(c->archiveBytes) + (m.created.empty() ? std::string{} : ", \xC3\xA9" "crite le " + m.created)
                     + (m.tool.empty() ? std::string{} : " par " + m.tool) + ".\n";
    text += "Elle devient un dossier de projet, ici (un dossier neuf, ou vide) ; ";
    text += "puis le projet s'ouvre, et son contr\xC3\xB4le dit ce qu'il a retrouv\xC3\xA9.";
    auto dialog = std::make_unique<FormDialog>("dialog.archiveOpen", "Ouvrir l'archive", text, std::move(fields),
                                               "D\xC3\xA9" "baller et ouvrir");
    helpOn(app, *dialog, "archive-projet");
    dialog->setValidator([](const std::vector<std::string>& v, std::size_t& field) -> std::string {
        field = 0;
        const std::string folder = v.empty() ? std::string{} : trimmed(v[0]);
        if (folder.empty()) return "Choisis o\xC3\xB9 d\xC3\xA9" "baller le projet.";
        std::error_code ec;
        if (fs::exists(fs::path(folder), ec) && !fs::is_empty(fs::path(folder), ec))
            return "Ce dossier n'est pas vide : choisis-en un neuf (un nom qui n'existe pas encore).";
        return {};
    });
    app.menus().ShowDialog(FormDialog::withBrowse(std::move(dialog), 0, ui::newFolder(root, "O\xC3\xB9 d\xC3\xA9" "baller le projet")),
                           [&app, c, source, skipDamaged](const menu::DialogResult& r) {
                               if (!r.accepted()) return;
                               const auto v = FormDialog::split(r.payload);
                               const std::string folder = v.empty() ? std::string{} : trimmed(v[0]);
                               if (folder.empty()) return;
                               unpackAndOpen(c, source, folder, skipDamaged);
                           });
}

void afterRead(App& app, const std::string& path, std::shared_ptr<pa::Contents> c, const std::string& error, std::optional<pa::Kind> sniffed) {
    const std::string leaf = leafOf(path);
    if (!c) {
        report() = "archive illisible : " + error;
        tell(app, "Archive illisible", leaf + " : " + error + ".", MessageDialog::Icon::Error);
        return;
    }
    const pa::Kind kind = c->manifest.kindKnown ? c->manifest.kind : sniffed.value_or(pa::Kind::Ihm);
    if (kind == pa::Kind::Api && !core::hasApi()) {
        report() = "archive refusee : une archive de l'automate";
        tell(app, "Une archive de l'automate",
             leaf + " porte un projet de l'automate (.xapi) : il s'ouvre avec API Studio.\n\nIHM Studio ouvre les archives de ses projets (.xihm).",
             MessageDialog::Icon::Info);
        return;
    }
    if (kind == pa::Kind::Ihm && !core::hasIhm()) {
        report() = "archive refusee : une archive de l'IHM";
        tell(app, "Une archive de l'IHM",
             leaf + " porte un projet IHM (.xihm) : il s'ouvre avec IHM Studio.\n\nAPI Studio ouvre les archives de ses projets (.xapi).",
             MessageDialog::Icon::Info);
        return;
    }
    const std::string why = kind == pa::Kind::Api ? apiRefusal(*c) : hmi::refusal(*c);
    if (!why.empty()) {
        report() = "archive refusee : " + why;
        tell(app, c->newer ? "Une archive d'une version plus r\xC3\xA9" "cente" : "Archive refus\xC3\xA9" "e", leaf + " : " + why + ".",
             MessageDialog::Icon::Warning);
        return;
    }
    if (!c->intact()) {
        const std::vector<std::string> broken = c->problems;
        report() = "archive abimee : " + std::to_string(broken.size()) + " probleme(s)";
        auto ask = std::make_unique<MessageDialog>(
            "Archive ab\xC3\xAEm\xC3\xA9" "e",
            leaf + " est ab\xC3\xAEm\xC3\xA9" "e : " + std::to_string(broken.size()) + " probl\xC3\xA8me(s)" + listed(broken)
                + "\n\nTu peux d\xC3\xA9" "baller ce qui est intact : le projet s'ouvre, et ce qui manque est dit. Sinon, redemande l'archive.",
            MessageDialog::Icon::Warning, "D\xC3\xA9" "baller ce qui est intact");
        app.menus().ShowDialog(std::move(ask), [&app, c, path](const menu::DialogResult& r) {
            if (r.accepted()) askWhere(app, c, path, true);
        });
        return;
    }
    askWhere(app, c, path, false);
}

// La question du fichier (Importer une archive).
void askFile(App& app) {
    const bool api = core::hasApi();
    const std::string root = dossiers::utf8De(App::projectsRoot());
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Archive", "", api ? "C:\\Echanges\\Ligne_gaz.xapi" : "C:\\Echanges\\Ligne_gaz.xihm", false, {}});
    const std::string text =
        std::string(api ? "Une archive de projet de l'automate (.xapi)" : "Une archive de projet IHM (.xihm, ou l'ancienne .zip de l'IHM)")
              + " : elle redevient un dossier de projet, \xC3\xA0 l'identique, qui s'ouvre. Chaque fichier est v\xC3\xA9rifi\xC3\xA9 (taille, empreinte) ; "
                "une archive ab\xC3\xAEm\xC3\xA9" "e est dite, fichier par fichier.";
    const std::string filters = api ? "Archives API Studio (.xapi)|*.xapi" : "Archives IHM Studio (.xihm)|*.xihm;*.zip";
    const std::string title = "Importer une archive";
    auto dialog = std::make_unique<FormDialog>("dialog.exchangeImport", title, text, std::move(fields), "Ouvrir");
    helpOn(app, *dialog, "archive-projet");
    dialog->setValidator([](const std::vector<std::string>& v, std::size_t& field) -> std::string {
        field = 0;
        const std::string path = v.empty() ? std::string{} : trimmed(v[0]);
        std::error_code ec;
        if (path.empty()) return "Choisis l'archive.";
        if (!fs::is_regular_file(fs::path(path), ec)) return "Introuvable : " + path;
        return {};
    });
    app.menus().ShowDialog(FormDialog::withBrowse(std::move(dialog), 0, ui::openFile(filters, root, title)), [&app](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        const auto v = FormDialog::split(r.payload);
        const std::string path = v.empty() ? std::string{} : trimmed(v[0]);
        if (path.empty()) return;
        if (!openArchive(app, path))
            tell(app, "Pas une archive de projet",
                 leafOf(path) + " n'est pas une archive de projet (.xihm, .xapi).", MessageDialog::Icon::Warning);
    });
}

} // namespace

// ============================================================================ exporter ==
void askExportArchive(App& app) {
    const bool api = core::hasApi();
    const std::string ext(pa::extension(ownKind()));
    if (!ready(app, "l'archive")) return;
    withSaved(app, "L'archive", [&app, api, ext] {
        const std::string folder = app.projectFolder();
        const std::string leaf = leafOf(folder);
        const long long versions = pa::versionCount(folder);
        const auto vBytes = treeBytes(fs::path(folder) / "versions");
        const auto hBytes = treeBytes(fs::path(folder) / "ihm" / "historique");
        const std::string join = "Les joindre", leave = "Les laisser", joinOne = "Le joindre", leaveOne = "Le laisser";
        std::vector<FormDialog::Field> fields;
        fields.push_back({"Fichier", (fs::path(folder) / "exports" / (leaf + ext)).string(), "", false, {}});
        fields.push_back({versions > 0 ? "Les versions du projet (" + std::to_string(versions) + ", " + pa::sizeText(vBytes) + ")"
                                       : std::string("Les versions du projet (aucune)"),
                          join, "", false, {join, leave}});
        fields.push_back({hBytes > 0 ? "L'historique de marche (alarmes, mesures, audit : " + pa::sizeText(hBytes) + ")"
                                     : std::string("L'historique de marche (vide)"),
                          joinOne, "", false, {joinOne, leaveOne}});
        if (api) fields.push_back({"Les macros de la biblioth\xC3\xA8que (libs\\Macros)", join, "", false, {join, leave}});
        // 1.14.0 (M4) : les enregistrements de l'IHM (Ressources > Enregistrements) - laisses par defaut (ils pesent).
        if (!api) {
            const auto rBytes = treeBytes(fs::path(folder) / "ihm" / "ressources" / "enregistrements");
            fields.push_back({rBytes > 0 ? "Avec les enregistrements (Ressources \xE2\x80\xBA Enregistrements : " + pa::sizeText(rBytes) + ")"
                                         : std::string("Avec les enregistrements (aucun)"),
                              leave, "", false, {join, leave}});
        }
        const std::string text = api
            ? "Tout le projet de l'automate dans un seul fichier : la fiche, le programme (sections, ladders et GRAFCET gard\xC3\xA9s tels quels, "
              "unit\xC3\xA9s, DFB, DDT, variables), la configuration, les t\xC3\xA2" "ches, les tables d'animation, l'ic\xC3\xB4ne, les donn\xC3\xA9" "es, les "
              "versions, la disposition des grafcets et les macros. Il se rouvre \xC3\xA0 l'identique dans API Studio : Projet \xE2\x80\xBA Ouvrir\xE2\x80\xA6, "
              "ou un double-clic sur le fichier."
            : "Tout le projet dans un seul fichier : la fiche, les vues et les popups, les ressources (images, polices, sons, vid\xC3\xA9os, "
              "documents), les scripts, les alarmes, les \xC3\xA9quipements et la communication, la simulation, l'historique des objets, les "
              "mod\xC3\xA8les, les styles, les donn\xC3\xA9" "es, les versions - et les fichiers externes li\xC3\xA9s hors du projet. Il se rouvre "
              "\xC3\xA0 l'identique dans IHM Studio : Projet \xE2\x80\xBA Ouvrir\xE2\x80\xA6, ou un double-clic sur le fichier.";
        const std::string title = api ? "Exporter en archive .xapi" : "Exporter en archive .xihm";
        auto dialog = std::make_unique<FormDialog>(
            "dialog.exchangeArchive", title,
            text + (api ? std::string(" Ne sont pas pris : exports/ et la corbeille.")
                        : std::string(" Le cache de construction vient avec : rouvert, le projet ne se r\xC3\xA9g\xC3\xA9n\xC3\xA8re pas. "
                                      "Ne sont pas pris : exports/ et la corbeille.")),
            std::move(fields),
            "Exporter");
        helpOn(app, *dialog, "archive-projet");
        dialog->setValidator([](const std::vector<std::string>& v, std::size_t& field) -> std::string {
            field = 0;
            return v.empty() || trimmed(v[0]).empty() ? "O\xC3\xB9 \xC3\xA9" "crire l'archive ?" : std::string{};
        });
        app.menus().ShowDialog(
            FormDialog::withBrowse(std::move(dialog), 0,
                                   ui::saveFile(api ? "Archives API Studio (.xapi)|*.xapi" : "Archives IHM Studio (.xihm)|*.xihm",
                                                (fs::path(folder) / "exports").string(), title)),
            [&app, api, ext, folder, join, joinOne](const menu::DialogResult& r) {
                if (!r.accepted()) return;
                const auto v = FormDialog::split(r.payload);
                std::string path = v.empty() ? std::string{} : trimmed(v[0]);
                if (path.empty()) return;
                if (fs::path(path).extension().empty()) path += ext;
                const bool withVersions = v.size() < 2 || v[1] == join;
                const bool withHistory = v.size() < 3 || v[2] == joinOne;
                const bool withMacros = api && (v.size() < 4 || v[3] == join);
                const bool withRecordings = !api && v.size() >= 4 && v[3] == join;   // 1.14.0 (M4)
                struct Out {
                    std::string        error;
                    hmi::ArchiveReport hmi;
                    pa::ApiReport      api;
                };
                auto out = std::make_shared<Out>();
                run("Archive du projet", [out, api, folder, path, withVersions, withHistory, withMacros, withRecordings] {
                    out->error = "l'\xC3\xA9" "criture s'est interrompue";
                    std::error_code ec;
                    fs::create_directories(fs::path(path).parent_path(), ec);
                    if (api) {
                        pa::ApiOptions o;
                        o.collect.versions = withVersions;
                        o.collect.history = withHistory;
                        o.macros = withMacros;
                        o.libraryRoot = project::SharedLibrary::defaultRoot();
                        o.tool = toolName();
                        if (auto st = pa::exportApiArchive(folder, path, o, &out->api); !st) out->error = errorText(st.error());
                        else out->error.clear();
                    } else {
                        hmi::ProjectArchiveOptions o;
                        o.versions = withVersions;
                        o.history = withHistory;
                        o.recordings = withRecordings;
                        o.tool = toolName();
                        if (auto st = hmi::exportProjectArchive(folder, path, o, &out->hmi); !st) out->error = errorText(st.error());
                        else out->error.clear();
                    }
                }, [out, api, path](App& a) {
                    if (!out->error.empty()) {
                        report() = "archive impossible : " + out->error;
                        tell(a, "Export impossible", out->error + ".", MessageDialog::Icon::Error);
                        return;
                    }
                    const std::size_t files = api ? out->api.manifest.entries.size() : out->hmi.entries.size();
                    const std::uint64_t bytes = api ? out->api.archiveBytes : out->hmi.archiveBytes;
                    report() = "archive ecrite : " + path + " (" + std::to_string(files) + " fichiers, " + pa::sizeText(bytes) + ")";
                    const std::string folderOut = fs::path(path).parent_path().string();
                    bgtasks::post({"echange:archive", "Projet", "Archive \xC3\xA9" "crite : " + leafOf(path),
                                   std::to_string(files) + " fichiers \xC2\xB7 " + pa::sizeText(bytes), "Ouvrir le dossier", "open.folder:" + folderOut, "ok"});
                    say(a, "Archive \xC3\xA9" "crite : " + path + " (" + std::to_string(files) + " fichiers, " + pa::sizeText(bytes)
                               + ") \xE2\x80\x94 elle se rouvre par Projet \xE2\x80\xBA Ouvrir\xE2\x80\xA6",
                        12.0);
                    if (!api)
                        if (auto* screen = dynamic_cast<MainAnalysisScreen*>(a.menus().top())) screen->showHmiArchiveReport(out->hmi, path);
                });
            });
    });
}

void askImportArchive(App& app) { askFile(app); }

// ============================================================================== ouvrir ==
bool isArchive(const std::string& path) { return pa::sniff(path).archive; }

bool isExchangeFile(const std::string& path) { return pa::sniff(path).archive; }

bool openArchive(App& app, const std::string& path) {
    const auto sn = pa::sniff(path);
    if (!sn.archive) return false;
    struct Read {
        std::shared_ptr<pa::Contents> contents;
        std::string                   error;
    };
    auto read = std::make_shared<Read>();
    say(app, "Lecture de " + leafOf(path) + "\xE2\x80\xA6", 4.0);
    run("Lecture de l'archive", [read, path] {
        read->error = "la lecture s'est interrompue";
        auto c = pa::readFile(path);
        if (c) read->contents = std::make_shared<pa::Contents>(std::move(*c));
        else read->error = errorText(c.error());
    }, [read, path, kind = sn.kind](App& a) { afterRead(a, path, read->contents, read->error, kind); });
    return true;
}

// ============================================================================ la boucle ==
void poll(App& app) {
    auto& list = jobs();
    for (std::size_t i = 0; i < list.size();) {
        if (!list[i]->done.load(std::memory_order_acquire)) {
            ++i;
            continue;
        }
        auto job = std::move(list[i]);
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
        if (job->thread.joinable()) job->thread.join();
        bgtasks::end(job->task);
        if (job->finish) job->finish(app);
    }
}

void shutdown() {
    for (auto& job : jobs())
        if (job->thread.joinable()) job->thread.join();
    for (auto& job : jobs()) bgtasks::end(job->task);
    jobs().clear();
}

bool busy() { return !jobs().empty(); }

const std::string& lastReport() { return report(); }

} // namespace app::exchange
