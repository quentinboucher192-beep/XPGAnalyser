// =============================================================================
//  main.cpp - entry point
// -----------------------------------------------------------------------------
//  Two modes on purpose:
//    * GUI   : the desktop application.
//    * --cli : parse + analyse + print, no window. This is what CI runs against
//              a corpus of customer exports, and what lets the import pipeline
//              be regression-tested without a display server.
// =============================================================================
#include "app/App.hpp"
#include "app/CrashDialogs.hpp"   // 1.10.2 (CR) : la fenetre de blocage de l'appli
#include "app/Dossiers.hpp"
#include "app/Settings.hpp"
#include "core/Version.hpp"
#include "help/TutorialLaunch.hpp"
#include "core/CrashGuard.hpp"   // 1.10.2 (CR) : le rapport de plantage et de blocage
#include "core/CallTrail.hpp"    // 1.10.2 (CR) : --sans-historique
#include "core/SingleInstance.hpp" // 1.11.2 (UNI, decision 195) : une seule instance
#include "help/ProblemReport.hpp" // 1.11 (T2) : « Signaler le probleme » apres un plantage
#include "import/ProjectImporter.hpp"
#include "project/SharedLibrary.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {

    // ---- Lot API 8 : le dossier de travail -----------------------------------------
    //  L'appli lit resources\ et libs\ dans le dossier courant. Lancee par un raccourci
    //  sans "Demarrer dans", par une association de fichier ou depuis une invite ouverte
    //  ailleurs, elle ne les trouvait pas. Quand ils ne sont pas dans le dossier courant
    //  mais a cote de l'executable, on s'y place - apres avoir rendu absolus les chemins
    //  donnes en arguments. Sinon (le cas des sessions rejouees), rien ne change.
    std::filesystem::path executableFolder() {
#if defined(_WIN32)
        std::wstring buffer(32768, L'\0');
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0 || n >= buffer.size()) return {};
        buffer.resize(n);
        return std::filesystem::path(buffer).parent_path();
#else
        std::error_code ec;
        const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
        return ec ? std::filesystem::path{} : self.parent_path();
#endif
    }

    // Les chemins donnes en arguments, rendus absolus AVANT de changer de dossier de travail.
    void makeAbsolute(const std::vector<std::string*>& paths) {
        namespace fs = std::filesystem;
        std::error_code ec;
        for (auto* p : paths) {
            if (!p || p->empty()) continue;
            const auto absolute = fs::absolute(fs::path(*p), ec);
            if (!ec) *p = absolute.string();
        }
    }

    void settleWorkingFolder(const std::vector<std::string*>& paths) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (fs::exists(fs::path("resources") / "schneider_library.txt", ec)) return;
        const auto folder = executableFolder();
        if (folder.empty() || !fs::exists(folder / "resources" / "schneider_library.txt", ec)) return;
        makeAbsolute(paths);
        fs::current_path(folder, ec);
    }

    // ---- 1.8.0 : l'exe Release n'ouvre plus de console --------------------------
    //  C'est une application Windows (sous-systeme GUI) : plus de fenetre noire a
    //  cote de l'appli. --cli et --version ecrivent quand meme dans l'invite de
    //  commandes qui les lance ; une sortie deja redirigee (un fichier, un tube :
    //  c'est ce que font les scripts de outils\) est gardee telle quelle.
    void attachParentConsole() {
#if defined(_WIN32)
        const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out != nullptr && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) return;
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
        (void)std::freopen("CONOUT$", "w", stdout);
        (void)std::freopen("CONOUT$", "w", stderr);
        SetConsoleOutputCP(CP_UTF8);
#endif
    }

    int runCli(const std::vector<std::string>& paths) {
        core::EventBus bus;
        importer::ProjectImporter imp(bus);

        auto result = imp.importFiles(paths);
        if (!result) {
            std::fprintf(stderr, "error: %s\n", result.error().message().c_str());
            return 2;
        }
        const auto& r = result->report;
        const auto& p = *result->project;

        std::printf("%s v%s  \xE2\x80\x94  %s %s (OS %s)\n",
            p.header.projectName.c_str(), p.header.projectVersion.c_str(),
            p.hardware.family.c_str(), p.hardware.cpuReference.c_str(),
            p.hardware.cpuFirmware.c_str());
        std::printf("variables %u   POUs %zu   sections %u   DDT %u   DFB %u\n",
            r.totalVariables, p.pous.size(), r.sections, r.derivedTypes, r.functionBlockTypes);
        std::printf("lines %u   statements %u   findings %zu   %.1f ms\n",
            r.linesOfCode, r.statements, r.findings.size(), result->totalMilliseconds);

        for (const auto& notice : p.partialDataNotices) std::printf("note: %s\n", notice.c_str());

        int errors = 0;
        for (const auto& f : r.findings)
            if (f.severity == importer::Finding::Severity::Error) {
                std::printf("error: %s \xE2\x80\x94 %s\n", f.subject.c_str(), f.detail.c_str());
                ++errors;
            }
        return errors == 0 ? 0 : 1;   // usable as a build gate
    }

} // namespace

// 1.11 (T1, tranche 4) : --verifier-tutoriels ecrit une session (app/ScriptRunner) qui joue
// chaque tutoriel, pour chaque variante, sur un bac a sable neuf : la fin de chaque etape,
// rejouee sans animation (tutoriel-aller) ; chaque cible introuvable et chaque "A toi" qui
// n'est pas juste apres ses gestes est un echec (tutoriel-verifier) ; avec --captures, une
// capture par etape (<id>-<variante>-<n>.png). Le code de sortie est celui de la session :
// 0 pour livrer. A lancer en SDL_VIDEO_DRIVER=offscreen. CONCEPTION-T1, section 6.
// Tranche 9 : LE LOT, en un seul lancement (la session ne fait que tutoriel-lot, deplie a
// l'execution, apres l'inscription des tutoriels, des sujets et du deducteur) :
//   --verifier-tutoriels               les tutoriels ecrits (inscrits)
//   --verifier-tutoriel <cle>[:<var>]  un seul (un id, ou un sujet : son deduit)
//   --verifier-sujets                  chaque sujet du centre : l'ecrit, sinon le deduit
//   --verifier-dossier <dossier>       les .tuto d'un dossier
//   --bilan <fichier>                  le tableau (Markdown) ; par defaut, avec --captures :
//                                      bilan-tutoriels.md dans le dossier des captures
// Le tableau (sujet, variante, etapes justes / total, code, premiere faute, "A toi deja vrai")
// sort aussi sur la sortie standard, apres "[bilan]". Tranche 16 : un "A toi" deja juste avant
// ses gestes (tutoriel-avant) est un avertissement, pas une faute : il ne change pas le code.
//   --tutoriel-texte <sujet> <fichier|->  le texte deduit d'un sujet (T3), pour en faire un .tuto
//   --compter-tutoriels <fichier|->       "Tutoriels prets : n / N" et chaque sujet pas pret, avec sa raison
static std::string tutorialCheckSession(const std::string& lot, bool captures, const std::string& bilan,
                                        const std::string& text) {
    std::error_code ec;
    std::string s = "# --verifier-tutoriels (genere par main.cpp)\nattendre 30\n";
    if (!text.empty()) s += text + "\n";
    else {
        s += "tutoriel-lot " + lot + (captures ? " captures" : "") + "\n";
        s += "tutoriel-bilan" + (bilan.empty() ? std::string() : " \"" + bilan + "\"") + "\n";
    }
    s += "quitter\n";
    const auto path = std::filesystem::temp_directory_path(ec) / "xpg-verifier-tutoriels.txt";
    std::ofstream(path, std::ios::binary) << s;
    return path.string();
}

int main(int argc, char** argv) {
    std::vector<std::string> files;
    app::AppOptions options;
    bool cli = false;
    bool version = false;
    bool checkTutorials = false;
    std::string checkLot = "inscrits";
    std::string checkBilan;
    std::string checkText;     // --tutoriel-texte : la session ne fait que l'ecrire
    core::crash::Test essai = core::crash::Test::None;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--cli")   cli = true;
        // 1.8.0 : le nom et la version, puis rien d'autre (les scripts de outils\ s'en servent).
        else if (a == "--version") version = true;
        else if (a == "--light") options.themeName = "Light";
        else if (a == "--verifier-tutoriels") { checkTutorials = true; checkLot = "inscrits"; }
        else if (a == "--verifier-tutoriel" && i + 1 < argc) { checkTutorials = true; checkLot = argv[++i]; }
        else if (a == "--verifier-sujets") { checkTutorials = true; checkLot = "sujets"; }
        else if (a == "--verifier-dossier" && i + 1 < argc) {
            checkTutorials = true;
            std::error_code ec;
            checkLot = "dossier \"" + std::filesystem::absolute(argv[++i], ec).string() + "\"";
        }
        // Tranche 9 : le texte deduit d'un sujet (T3), dans un fichier (ou sur la sortie avec "-").
        else if (a == "--tutoriel-texte" && i + 2 < argc) {
            checkTutorials = true;
            const std::string topic = argv[++i];
            const std::string out = argv[++i];
            std::error_code ec;
            checkText = "tutoriel-texte \"" + topic + "\"" + (out == "-" ? std::string() : " \"" + std::filesystem::absolute(out, ec).string() + "\"");
        }
        // Tranche 18 : le compteur "Tutoriels prets" du centre et chaque sujet qui n'est pas pret,
        // avec sa raison (dans un fichier Markdown, ou seulement sur la sortie avec "-").
        else if (a == "--compter-tutoriels" && i + 1 < argc) {
            checkTutorials = true;
            const std::string out = argv[++i];
            std::error_code ec;
            checkText = "tutoriel-compte" + (out == "-" ? std::string() : " \"" + std::filesystem::absolute(out, ec).string() + "\"");
        }
        else if (a == "--bilan" && i + 1 < argc) {
            std::error_code ec;
            checkBilan = std::filesystem::absolute(argv[++i], ec).string();
        }
        // 1.10.2 (CR) : les essais caches du rapport de plantage et de blocage.
        else if (a == "--essai-plantage") essai = core::crash::Test::Crash;
        else if (a == "--essai-blocage")  essai = core::crash::Test::Hang;
        // Sans l'historique interne (pour mesurer son cout : une image, un cycle).
        else if (a == "--sans-historique") core::trail::setEnabled(false);
        // 1.11.2 (UNI) : l'instance unique meme dans une session (le banc wine) ; lu par
        // core::instance::exempted.
        else if (a == "--instance-unique") {}
        // Rejouer une session (voir app/ScriptRunner.hpp) : c'est ainsi que
        // sont faites les captures des livraisons.
        else if (a == "--script" && i + 1 < argc)   options.script = argv[++i];
        else if (a == "--captures" && i + 1 < argc) options.capturesDir = argv[++i];
        // Lot 14 : le poste d'exploitation - l'IHM seule, en plein ecran, sans
        // l'editeur (Configuration > Poste d'exploitation).
        else if ((a == "--ihm" || a == "--poste") && i + 1 < argc) options.station = argv[++i];
        else if (a == "--size" && i + 1 < argc) {
            int w = 0, h = 0;
            if (std::sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                options.width = w;
                options.height = h;
                options.sizeGiven = true;
            }
        }
        else if (a.rfind("--", 0) == 0)
            std::fprintf(stderr, "warning: unknown option %s\n", a.c_str());
        else files.push_back(a);
    }

    if (checkTutorials) {
        if (checkBilan.empty() && !options.capturesDir.empty()) checkBilan = "bilan-tutoriels.md";   // dans les captures
        options.script = tutorialCheckSession(checkLot, !options.capturesDir.empty(), checkBilan, checkText);
    }

    if (version) {
        attachParentConsole();
        std::printf("%s %s\n", XPG_ANALYZER_NAME, XPG_ANALYZER_VERSION);
        std::fflush(stdout);
        return 0;
    }

    if (cli) {
        attachParentConsole();
        if (files.empty()) {
            std::fprintf(stderr, "usage: xpg-analyzer --cli <file.XPG> [more files\xE2\x80\xA6]\n");
            return 2;
        }
        return runCli(files);
    }

    // 1.11.2 (UNI, decision 195) : UNE SEULE INSTANCE. Deja ouverte (meme utilisateur,
    // meme session) : on lui passe la demande - le projet, ou le poste (--ihm), en chemins
    // absolus, avant tout changement de dossier de travail - et on s'arrete ; elle revient
    // au premier plan et l'ouvre (apres la question d'enregistrer le sien s'il est modifie).
    // Les modes d'essai et XPG_INSTANCES_MULTIPLES=1 : plusieurs a la fois, comme avant.
    std::unique_ptr<core::instance::Guard> instance;
    {
        const std::vector<std::string> args(argv + 1, argv + argc);
        std::string why;
        if (core::instance::exempted(args, std::getenv("XPG_INSTANCES_MULTIPLES"), &why)) {
            // (rien : les sessions, les essais et les series de R111 ne se genent pas)
        } else {
            instance = core::instance::Guard::acquire();
            if (instance->role() == core::instance::Guard::Role::Secondary) {
                namespace fs = std::filesystem;
                std::vector<std::string> request;
                std::error_code ec;
                const auto absolute = [&ec](const std::string& p) {
                    const auto a = fs::absolute(fs::path(p), ec);
                    return ec ? p : a.string();
                };
                if (!options.station.empty()) {
                    request.push_back("--poste");
                    request.push_back(absolute(options.station));
                }
                for (const auto& f : files) request.push_back(absolute(f));
                std::string failed;
                if (instance->forward(request, &failed)) return 0;
                core::instance::alert(XPG_ANALYZER_NAME,
                    "XPGAnalyser est d\xC3\xA9j\xC3\xA0 ouvert, mais sa fen\xC3\xAAtre ne r\xC3\xA9pond pas ("
                    + failed + ").\n\nAttends qu'elle r\xC3\xA9ponde, ou ferme-la (Gestionnaire des t\xC3\xA2" "ches), puis relance.");
                return 4;
            }
            if (instance->role() == core::instance::Guard::Role::Primary) {
                if (!instance->listen())
                    std::fprintf(stderr, "instance unique : pas d'\xC3\xA9" "coute (%s)\n", instance->why().c_str());
                core::instance::setCurrent(instance.get());
            } else {
                std::fprintf(stderr, "instance unique : non v\xC3\xA9rifi\xC3\xA9" "e (%s)\n", instance->why().c_str());
            }
        }
    }

    if (!files.empty()) options.openOnStart = files.front();
    // 1.8.0 : LA VERSION INSTALLEE (installation.ini a cote de l'exe) travaille dans
    // son dossier des donnees (app/Dossiers.hpp) ; sinon, comme avant : le dossier de
    // travail, ou celui de l'exe s'il a resources\ (lot API 8).
    {
        namespace fs = std::filesystem;
        const std::vector<std::string*> paths{&options.openOnStart, &options.script, &options.capturesDir, &options.station};
        const auto exe = executableFolder();
        std::error_code ec;
        const bool installed = !exe.empty() && fs::is_regular_file(exe / app::dossiers::cheminDe(app::dossiers::kNomIniInstallation), ec);
        if (installed) makeAbsolute(paths);
        else settleWorkingFolder(paths);
        const std::string settings = app::dossiers::utf8De(fs::path(app::Settings::defaultPath()).parent_path());
        const auto folders = app::dossiers::preparer(exe, settings);
        if (folders.installe)
            project::SharedLibrary::setDefaultRoot(
                app::dossiers::cheminDe(app::dossiers::actif(app::dossiers::Cle::Bibliotheque)).string());
        for (const auto& m : folders.messages) std::fprintf(stderr, "dossiers : %s\n", m.c_str());
    }

    // 1.10.2 (CR) : le rapport de plantage et de blocage, dans crashs/ a cote des
    // reglages (core/CrashGuard.hpp). Une session rejouee n'ouvre pas de fenetre native.
    const bool scripted = !options.script.empty();
    {
        namespace fs = std::filesystem;
        core::crash::Options crash;
        crash.folder = app::dossiers::utf8De(fs::path(app::Settings::defaultPath()).parent_path() / "crashs");
        crash.dialogs = !scripted;
        core::crash::install(crash);
        app::crashdialogs::install();
        if (essai != core::crash::Test::None) core::crash::armTest(essai);
    }

    auto app = app::App::create(std::move(options));
    if (!app) {
        std::fprintf(stderr, "startup failed: %s\n", app.error().message().c_str());
        return 2;
    }
    // 1.11 (T2) : le bouton « Signaler le probleme » de la fenetre du plantage. Il
    // prepare le zip de Signaler (help::report::writeZip) dans <reglages>/signalements,
    // le rapport de crashs/ joint tel quel. Rien ne part par le reseau.
    app::crashdialogs::setReportHook([](const core::crash::PendingReport& r) -> std::string {
        namespace fs = std::filesystem;
        help::report::Form form;
        form.what = std::string(r.kind == "blocage" ? "Un blocage" : "Un plantage") + " s'est produit le " + r.when + ".";
        form.withProject = false;   // aucun projet n'est encore ouvert
        form.withLog = false;       // le rapport joint a deja l'historique interne
        form.attachedName = app::dossiers::utf8De(app::dossiers::cheminDe(r.path).filename());
        form.attachedText = r.text;
        help::report::Facts facts;
        facts.version = XPG_ANALYZER_VERSION;
        facts.system = help::report::systemName();
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        const help::report::Stamp when{tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min};
        std::string why;
        const fs::path zip = help::report::writeZip(fs::path(app::Settings::defaultPath()).parent_path(), form, facts,
                                                    {}, when, &why);
        if (zip.empty()) return "Le signalement n'a pas pu \xC3\xAAtre pr\xC3\xA9par\xC3\xA9 : " + why;
        return "Le signalement est pr\xC3\xAAt (rien n'est envoy\xC3\xA9) :\n" + app::dossiers::utf8De(zip);
    });
    if (!scripted) app::crashdialogs::showPendingReport();   // 1.10.2 (CR) : « Un plantage s'est produit le … »
    core::crash::startWatchdog(8, !scripted);
    const int code = (*app)->run();
    core::crash::stopWatchdog();
    (*app).reset();           // 1.11.2 (UNI) : l'appli d'abord (elle ne se fait plus reveiller)...
    core::instance::setCurrent(nullptr);
    instance.reset();         // ...puis le verrou : une instance lancee maintenant demarre
    return code;
}
