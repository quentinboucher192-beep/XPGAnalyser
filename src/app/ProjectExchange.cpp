// =============================================================================
//  app/ProjectExchange.cpp - 1.12.30 : voir ProjectExchange.hpp
// =============================================================================
#include "ProjectExchange.hpp"

#include "App.hpp"
#include "BackgroundTasks.hpp"
#include "Dossiers.hpp"
#include "HmiWebPage.hpp"
#include "screens/Screens.hpp"
#include "../core/AtomicFile.hpp"
#include "../core/Edition.hpp"
#include "../core/Version.hpp"
#include "../export/ProgramBook.hpp"
#include "../hmi/HmiArchive.hpp"
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

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

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

// "23/10/2026 a 19:40" : la date d'une page.
std::string frenchNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[64];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d \xC3\xA0 %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    return b;
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

// La page web du projet : celle qu'il a choisie, sinon <dossier>.html a cote du dossier.
std::string besideFolder(const std::string& projectFolder) {
    fs::path folder(projectFolder);
    if (!folder.has_filename()) folder = folder.parent_path();
    return (folder.parent_path() / (folder.filename().string() + ".html")).string();
}

std::string webTarget(App& app) {
    if (!app.manifest().webFile.empty()) return app.manifest().webFile;
    return besideFolder(app.projectFolder());
}

// ---- la page web : dessinee ici, ecrite a cote ---------------------------------------
struct WebJob {
    bool                                   api{false};
    bool                                   quiet{false};      // a l'enregistrement : un mot dans la barre d'etat
    bool                                   history{true};
    std::string                            folder, path;
    WebPageInfo                            info;
    std::unique_ptr<HmiWebPageBuilder>     builder;           // l'IHM : ses vues se dessinent ici
    std::shared_ptr<const domain::Project> plc;               // l'automate : une copie
    exporter::book::Options                book;
    int                                    task{0};
};
std::unique_ptr<WebJob>& webJob() {
    static std::unique_ptr<WebJob> job;
    return job;
}
// La page s'ecrit (sur l'autre fil) : une autre attend son tour (le meme fichier).
bool& webWriting() {
    static bool writing = false;
    return writing;
}
bool& webAgain() {
    static bool again = false;
    return again;
}

void startWeb(App& app, const std::string& path, bool quiet, bool history) {
    if (webJob() || webWriting()) {
        webAgain() = true;                      // une page en cours : une autre, juste apres
        return;
    }
    auto j = std::make_unique<WebJob>();
    j->api = core::hasApi();
    j->quiet = quiet;
    j->history = history;
    j->folder = app.projectFolder();
    j->path = path;
    j->info.application = toolName();
    j->info.exportedAt = frenchNow();
    j->info.manifest = app.manifest();
    j->info.hasManifest = true;
    j->info.archiveName = leafOf(j->folder) + std::string(pa::extension(ownKind()));
    if (!j->api) {
        const auto doc = app.hmi();
        if (!doc) {
            say(app, "La page web : ce projet n'a pas d'IHM.", 8.0);
            return;
        }
        j->builder = std::make_unique<HmiWebPageBuilder>(doc->project, app.theme());
    } else {
        const auto plc = app.project();
        if (!plc) return;
        j->plc = std::make_shared<const domain::Project>(*plc);   // le fil lit la copie
        j->book.exportedAt = j->info.exportedAt;
        j->book.appName = core::productName();
        j->book.appVersion = XPG_ANALYZER_VERSION;
        j->book.projectName = app.manifest().name;
    }
    j->task = bgtasks::begin("Page web \xC2\xB7 les vues");
    webJob() = std::move(j);
}

// Les vues dessinees : la suite (l'archive, la page, le fichier) a cote.
void finishWeb() {
    std::shared_ptr<WebJob> j(std::move(webJob()));
    bgtasks::end(j->task);
    webWriting() = true;
    struct Out {
        std::string   error;
        std::uint64_t bytes{0};
        WebPageStats  stats;
    };
    auto out = std::make_shared<Out>();
    run("Page web", [j, out] {
        out->error = "l'\xC3\xA9" "criture s'est interrompue";      // ce qui reste si rien ne la remplace
        pa::Bytes zip;
        if (!j->api) {
            hmi::ProjectArchiveOptions o;
            o.history = j->history;
            o.tool = j->info.application;
            auto z = hmi::projectArchive(j->folder, o, nullptr);
            if (!z) {
                out->error = errorText(z.error());
                return;
            }
            zip = std::move(*z);
        } else {
            pa::ApiOptions o;
            o.collect.history = j->history;
            o.tool = j->info.application;
            o.libraryRoot = project::SharedLibrary::defaultRoot();
            auto z = pa::apiArchive(j->folder, o, nullptr);
            if (!z) {
                out->error = errorText(z.error());
                return;
            }
            zip = std::move(*z);
        }
        j->info.archive = &zip;
        std::string html;
        if (!j->api) {
            html = j->builder->page(j->info, &out->stats);
        } else {
            exporter::book::HtmlExtras extras;
            const auto& m = j->info.manifest;
            extras.card = {{"\xC3\x89tat", std::string(project::toString(m.state))},
                           {"Version", m.version},
                           {"Auteur", m.author},
                           {"Cr\xC3\xA9\xC3\xA9", m.created},
                           {"Modifi\xC3\xA9", m.modified},
                           {"Commentaire", m.comment}};
            extras.archive = &zip;
            extras.archiveName = j->info.archiveName;
            html = exporter::book::toHtml(exporter::book::build(*j->plc, j->book), j->book, *j->plc, extras);
        }
        j->info.archive = nullptr;
        std::error_code ec;
        fs::create_directories(fs::path(j->path).parent_path(), ec);
        if (auto st = core::writeFileAtomic(fs::path(j->path), html, {false, false}); !st) {
            out->error = errorText(st.error());
            return;
        }
        out->error.clear();
        out->bytes = html.size();
    }, [j, out](App& app) {
        webWriting() = false;
        if (!out->error.empty()) {
            report() = "page web impossible : " + out->error;
            bgtasks::post({"echange:page", "Projet", "La page web ne s'est pas \xC3\xA9" "crite", out->error, "", "", "warning"});
            say(app, "La page web ne s'est pas \xC3\xA9" "crite : " + out->error, 10.0);
        } else {
            report() = "page web ecrite : " + j->path + " (" + pa::sizeText(out->bytes) + ")";
            const std::string folder = fs::path(j->path).parent_path().string();
            bgtasks::post({"echange:page", "Projet", "Page web \xC3\xA9" "crite : " + leafOf(j->path), j->path + " \xC2\xB7 " + pa::sizeText(out->bytes),
                           "Ouvrir le dossier", "open.folder:" + folder, "ok"});
            if (j->quiet)
                say(app, "Page web mise \xC3\xA0 jour : " + j->path + " (" + pa::sizeText(out->bytes) + ")", 5.0);
            else
                say(app, "Page web \xC3\xA9" "crite : " + j->path + " (" + pa::sizeText(out->bytes) + ") \xE2\x80\x94 elle s'ouvre dans un "
                         "navigateur, et se r\xC3\xA9importe par Projet \xE2\x80\xBA Importer depuis une page web\xE2\x80\xA6", 12.0);
        }
        if (webAgain()) {
            webAgain() = false;
            if (!app.projectFolder().empty() && app.projectFolder() == j->folder) startWeb(app, webTarget(app), true, j->history);
        }
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
                             + " fichiers intacts, le contr\xC3\xB4le retrouve chaque compte (" + std::to_string(out->counts) + ").", 12.0);
                return;
            }
            std::string text = "Le projet est d\xC3\xA9" "ball\xC3\xA9 dans " + folder + " (" + std::to_string(out->files) + " fichiers) et ouvert.";
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
    text += c->fromPage ? "Le projet que porte la page web devient un dossier de projet, ici (un dossier neuf, ou vide) ; "
                        : "Elle devient un dossier de projet, ici (un dossier neuf, ou vide) ; ";
    text += "puis le projet s'ouvre, et son contr\xC3\xB4le dit ce qu'il a retrouv\xC3\xA9.";
    auto dialog = std::make_unique<FormDialog>(c->fromPage ? "dialog.webOpen" : "dialog.archiveOpen",
                                               c->fromPage ? "Importer depuis une page web" : "Ouvrir l'archive", text, std::move(fields),
                                               "D\xC3\xA9" "baller et ouvrir");
    helpOn(app, *dialog, c->fromPage ? "page-web" : "archive-projet");
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

// La question du fichier (Importer une archive, Importer depuis une page web).
void askFile(App& app, bool page) {
    const bool api = core::hasApi();
    const std::string root = dossiers::utf8De(App::projectsRoot());
    std::vector<FormDialog::Field> fields;
    fields.push_back({page ? "Page web" : "Archive", "",
                      page ? "C:\\Echanges\\Ligne_gaz.html" : (api ? "C:\\Echanges\\Ligne_gaz.xapi" : "C:\\Echanges\\Ligne_gaz.xihm"), false, {}});
    const std::string text = page
        ? "Une page web \xC3\xA9" "crite par " + core::productName() + " (Projet \xE2\x80\xBA Exporter en page web) porte le projet entier : il "
          "redevient un dossier de projet, \xC3\xA0 l'identique, qui s'ouvre. Une page qu'une autre application a \xC3\xA9" "crite est refus\xC3\xA9" "e."
        : std::string(api ? "Une archive de projet de l'automate (.xapi)" : "Une archive de projet IHM (.xihm, ou l'ancienne .zip de l'IHM)")
              + " : elle redevient un dossier de projet, \xC3\xA0 l'identique, qui s'ouvre. Chaque fichier est v\xC3\xA9rifi\xC3\xA9 (taille, empreinte) ; "
                "une archive ab\xC3\xAEm\xC3\xA9" "e est dite, fichier par fichier.";
    const std::string filters = page ? "Pages web (.html)|*.html;*.htm"
                                     : (api ? "Archives API Studio (.xapi)|*.xapi" : "Archives IHM Studio (.xihm)|*.xihm;*.zip");
    const std::string title = page ? "Importer depuis une page web" : "Importer une archive";
    auto dialog = std::make_unique<FormDialog>(page ? "dialog.exchangeImportWeb" : "dialog.exchangeImport", title, text, std::move(fields), "Ouvrir");
    helpOn(app, *dialog, page ? "page-web" : "archive-projet");
    dialog->setValidator([page](const std::vector<std::string>& v, std::size_t& field) -> std::string {
        field = 0;
        const std::string path = v.empty() ? std::string{} : trimmed(v[0]);
        std::error_code ec;
        if (path.empty()) return page ? "Choisis la page web." : "Choisis l'archive.";
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
                 leafOf(path) + " n'est ni une archive de projet (.xihm, .xapi) ni la page web d'un projet.", MessageDialog::Icon::Warning);
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
            "dialog.exchangeArchive", title, text + " Ne sont pas pris : le cache de construction, exports/ et la corbeille.", std::move(fields),
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
                struct Out {
                    std::string        error;
                    hmi::ArchiveReport hmi;
                    pa::ApiReport      api;
                };
                auto out = std::make_shared<Out>();
                run("Archive du projet", [out, api, folder, path, withVersions, withHistory, withMacros] {
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

void askExportWeb(App& app) {
    const bool api = core::hasApi();
    if (!ready(app, "la page web")) return;
    withSaved(app, "La page web", [&app, api] {
        const std::string folder = app.projectFolder();
        const auto hBytes = treeBytes(fs::path(folder) / "ihm" / "historique");
        const std::string yes = "Oui", no = "Non", joinOne = "Le joindre", leaveOne = "Le laisser";
        std::vector<FormDialog::Field> fields;
        fields.push_back({"Page web", webTarget(app), "", false, {}});
        fields.push_back({"Mettre \xC3\xA0 jour la page web \xC3\xA0 chaque enregistrement", app.manifest().webPage ? yes : no, "", false, {yes, no}});
        fields.push_back({hBytes > 0 ? "L'historique de marche, dans le projet de la page (" + pa::sizeText(hBytes) + ")"
                                     : std::string("L'historique de marche, dans le projet de la page (vide)"),
                          joinOne, "", false, {joinOne, leaveOne}});
        const std::string text = api
            ? "Le programme en page web : un seul fichier .html, qui s'ouvre dans n'importe quel navigateur, sans Internet - la fiche du "
              "projet, la configuration, les t\xC3\xA2" "ches et l'ordre d'ex\xC3\xA9" "cution, chaque section et son code, les unit\xC3\xA9s, les blocs DFB, "
              "les types d\xC3\xA9riv\xC3\xA9s, les variables. Le projet entier (.xapi) est dans la page : Projet \xE2\x80\xBA Importer depuis une page "
              "web\xE2\x80\xA6 le reconstruit \xC3\xA0 l'identique."
            : "L'IHM en page web : un seul fichier .html, qui s'ouvre dans n'importe quel navigateur, sans Internet - chaque vue et chaque "
              "popup dessin\xC3\xA9" "e comme dans IHM Studio (les boutons qui ouvrent une vue ou une popup se cliquent), le menu des vues, les "
              "alarmes, les variables, les scripts et la fiche du projet. Le projet entier (.xihm) est dans la page : Projet \xE2\x80\xBA "
              "Importer depuis une page web\xE2\x80\xA6 le reconstruit \xC3\xA0 l'identique.";
        auto dialog = std::make_unique<FormDialog>(
            "dialog.exchangeWeb", "Exporter en page web (.html)",
            text + "\n\n\xC3\x80 chaque enregistrement (Oui) : la page se r\xC3\xA9\xC3\xA9" "crit toute seule, \xC3\xA0 cet endroit, sans ralentir "
                   "l'enregistrement.",
            std::move(fields), "\xC3\x89" "crire la page");
        helpOn(app, *dialog, "page-web");
        dialog->setValidator([](const std::vector<std::string>& v, std::size_t& field) -> std::string {
            field = 0;
            return v.empty() || trimmed(v[0]).empty() ? "O\xC3\xB9 \xC3\xA9" "crire la page ?" : std::string{};
        });
        app.menus().ShowDialog(
            FormDialog::withBrowse(std::move(dialog), 0, ui::saveFile("Pages web (.html)|*.html", webTarget(app), "Exporter en page web")),
            [&app, yes, joinOne](const menu::DialogResult& r) {
                if (!r.accepted()) return;
                const auto v = FormDialog::split(r.payload);
                std::string path = v.empty() ? std::string{} : trimmed(v[0]);
                if (path.empty()) return;
                if (fs::path(path).extension().empty()) path += ".html";
                const bool automatic = v.size() > 1 && v[1] == yes;
                const bool history = v.size() < 3 || v[2] == joinOne;
                // La case, gardee dans la fiche du projet ; le fichier, s'il n'est pas a cote du projet.
                std::string file = path;
                {
                    std::error_code ec;
                    const auto chosen = fs::weakly_canonical(fs::path(path), ec);
                    const auto beside = fs::weakly_canonical(fs::path(besideFolder(app.projectFolder())), ec);
                    if (lower(chosen.string()) == lower(beside.string())) file.clear();
                }
                if (automatic != app.manifest().webPage || (automatic && file != app.manifest().webFile))
                    if (auto st = app.setWebPage(automatic, automatic ? file : std::string{}); !st)
                        say(app, "Mettre \xC3\xA0 jour \xC3\xA0 chaque enregistrement : " + errorText(st.error()), 10.0);
                startWeb(app, path, false, history);
            });
    });
}

void askImportArchive(App& app) { askFile(app, false); }
void askImportWeb(App& app) { askFile(app, true); }

// ============================================================================== ouvrir ==
bool isArchive(const std::string& path) { return pa::sniff(path).archive; }

bool isExchangeFile(const std::string& path) {
    const auto s = pa::sniff(path);
    return s.archive || s.page;
}

bool openArchive(App& app, const std::string& path) {
    const auto sn = pa::sniff(path);
    if (!sn.archive) {
        if (sn.page) {
            report() = "page sans archive : " + sn.why;
            tell(app, "Importer depuis une page web", leafOf(path) + " : " + sn.why + ".", MessageDialog::Icon::Warning);
            return true;
        }
        return false;
    }
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
void afterSave(App& app) {
    if (!app.manifest().webPage || app.projectFolder().empty()) return;
    startWeb(app, webTarget(app), true, true);
}

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
    // La page web : ses vues, une par une, entre deux images (20 ms au plus par image).
    if (auto& web = webJob()) {
        bool drawn = true;
        if (web->builder) {
            drawn = web->builder->draw(20.0);
            bgtasks::progress(web->task, web->builder->progress());
        }
        if (drawn) finishWeb();
    }
}

void shutdown() {
    for (auto& job : jobs())
        if (job->thread.joinable()) job->thread.join();
    for (auto& job : jobs()) bgtasks::end(job->task);
    jobs().clear();
    if (webJob()) bgtasks::end(webJob()->task);
    webJob().reset();
    webWriting() = false;
    webAgain() = false;
}

bool busy() { return !jobs().empty() || webJob() != nullptr; }

const std::string& lastReport() { return report(); }

} // namespace app::exchange
