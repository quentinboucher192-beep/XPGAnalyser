// =============================================================================
//  tests/crashtrail_test.cpp - 1.10.2 : l'historique interne, le rapport de
//  plantage et le chien de garde (Linux ; sans ecran).
//
//    g++ -std=c++20 -O0 -Isrc tests/crashtrail_test.cpp src/core/CallTrail.cpp
//        src/core/CrashGuard.cpp -lpthread -rdynamic -o crashtrail_test
//    ./crashtrail_test <dossier temporaire>
//  Sous Windows (wine) : crashtrail_test.exe <dossier> --plantage | --blocage
//  joue le cas dans le processus lui-meme ; le script d'essai lit le rapport.
//
//  Le plantage et le blocage se jouent dans un processus fils (fork) : le
//  pere lit le rapport qu'il a laisse.
// =============================================================================
#include "core/CallTrail.hpp"
#include "core/CrashGuard.hpp"
#include "core/Version.hpp"   // 1.11 (I111) : la version du rapport, pas un 1.10.2 ecrit en dur

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#  include <sys/wait.h>
#  include <time.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace core;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("ECHEC %s:%d : %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

#if !defined(_WIN32)
static std::vector<fs::path> reports(const fs::path& dir, const char* prefix) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.path().filename().string().rfind(prefix, 0) == 0 && e.path().extension() == ".txt") out.push_back(e.path());
    return out;
}
#endif

static void testRing() {
    trail::resetForTests();
    // Debordement : 25 000 entrees d'un fil, on garde les 20 000 dernieres, dans l'ordre.
    for (int i = 0; i < 25000; ++i) XPG_TRACE(Info, "n%d", i);
    auto all = trail::recent(100000);
    CHECK(all.size() == trail::kCapacity);
    CHECK(trail::written() == 25000);
    CHECK(std::strcmp(all.front().text, "n5000") == 0);
    CHECK(std::strcmp(all.back().text, "n24999") == 0);
    bool ordered = true;
    for (std::size_t i = 1; i < all.size(); ++i) ordered = ordered && all[i].seq == all[i - 1].seq + 1;
    CHECK(ordered);

    // Plusieurs fils en meme temps : aucune entree dechiree (le texte dit son fil).
    trail::resetForTests();
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([t, &go] {
            char name[16];
            std::snprintf(name, sizeof name, "essai%d", t);
            trail::nameThread(name);
            while (!go) std::this_thread::yield();
            for (int i = 0; i < 20000; ++i) XPG_TRACE(Action, "fil %d entree %d xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", t, i);
        });
    // Un lecteur pendant les ecritures.
    std::size_t readWhileWriting = 0;
    go = true;
    while (trail::written() < 70000) readWhileWriting += trail::recent(2000).size();
    for (auto& th : threads) th.join();
    CHECK(trail::written() == 80000);
    all = trail::recent(trail::kCapacity);
    // Une case recrite par deux ecrivains a 20 000 numeros d'ecart (un fil preempte
    // longtemps) est sautee par le lecteur : tres rare, et sans dechirure.
    std::printf("tampon : %zu entrees relues sur %zu\n", all.size(), trail::kCapacity);
    CHECK(all.size() + 200 >= trail::kCapacity);
    int torn = 0;
    for (const auto& e : all) {
        int t = -1, i = -1;
        if (std::sscanf(e.text, "fil %d entree %d", &t, &i) != 2 || t < 0 || t > 3) ++torn;
    }
    CHECK(torn == 0);
    CHECK(readWhileWriting > 0);
    std::printf("tampon : 80 000 entrees de 4 fils, %zu lues pendant, %d dechirees\n", readWhileWriting, torn);
}

static void testScopes() {
    trail::resetForTests();
    trail::nameThread("principal");
    const auto me = trail::currentThread();
    {
        XPG_PORTEE("A::un");
        {
            XPG_PORTEE_TEXTE("B::deux", "MAST.XPG");
            trail::ThreadStack s;
            CHECK(trail::stackOf(me, s));
            CHECK(s.depth == 2);
            CHECK(std::strcmp(s.frames[0].name, "A::un") == 0);
            CHECK(std::strcmp(s.frames[1].name, "B::deux") == 0);
            CHECK(std::strcmp(s.name, "principal") == 0);
        }
        // Un autre fil lit la pile de celui-ci.
        trail::ThreadStack seen;
        bool found = false;
        std::thread([&] { found = trail::stackOf(me, seen); }).join();
        CHECK(found && seen.depth == 1);
    }
    trail::ThreadStack s;
    CHECK(trail::stackOf(me, s) && s.depth == 0);
    const auto all = trail::recent(10);
    CHECK(all.size() == 4);
    CHECK(all[0].kind == trail::Kind::Enter && all[1].kind == trail::Kind::Enter);
    CHECK(std::strcmp(all[1].text, "B::deux : MAST.XPG") == 0);
    CHECK(all[2].kind == trail::Kind::Leave && all[2].depth == 1);
    CHECK(all[3].kind == trail::Kind::Leave && all[3].depth == 0);
    char line[trail::kLineMax];
    trail::formatEntry(all[1], line, sizeof line);
    CHECK(std::strstr(line, "#") && std::strstr(line, "principal") && std::strstr(line, "> B::deux : MAST.XPG"));
    trail::formatEntry(all[2], line, sizeof line);
    CHECK(std::strstr(line, "< B::deux"));
    std::printf("ligne : %s\n", line);
    // Plus profond que kMaxDepth : compte, pas garde.
    {
        std::vector<std::unique_ptr<trail::Scope>> deep;
        for (std::size_t i = 0; i < trail::kMaxDepth + 5; ++i) deep.push_back(std::make_unique<trail::Scope>("profond"));
        CHECK(trail::stackOf(me, s) && s.depth == trail::kMaxDepth + 5 && s.kept == trail::kMaxDepth);
        while (!deep.empty()) deep.pop_back();
    }
    CHECK(trail::stackOf(me, s) && s.depth == 0);
    // Le texte est coupe sans couper un caractere.
    std::string longText(200, 'x');
    longText[trail::kTextMax - 2] = '\xC3';
    longText[trail::kTextMax - 1] = '\xA9';
    trail::note(trail::Kind::Info, longText);
    const auto last = trail::recent(1);
    CHECK(last.size() == 1 && std::strlen(last[0].text) == trail::kTextMax - 2);
}

static void testReportFormat(const fs::path& dir) {
    trail::resetForTests();
    XPG_PORTEE("essai::rapport");
    XPG_TRACE(Menu, "Aide > Journal interne");
    crash::setOpenDocuments("MAST.XPG\nArmoire_Gaz.xpgproj");
    const std::string path = crash::writeReportNow(crash::ReportKind::Manual, "rapport demande");
    CHECK(!path.empty() && fs::exists(fs::path(std::u8string(path.begin(), path.end()))));
    const std::string text = readAll(fs::path(std::u8string(path.begin(), path.end())));
    CHECK(text.find(XPG_ANALYZER_NAME " " XPG_ANALYZER_VERSION " - rapport de rapport") != std::string::npos);
    CHECK(text.find("Version     : " XPG_ANALYZER_NAME " " XPG_ANALYZER_VERSION) != std::string::npos);
    CHECK(text.find("Raison      : rapport demande") != std::string::npos);
    CHECK(text.find("Pile d'appels") != std::string::npos);
    CHECK(text.find("essai::rapport   depuis") != std::string::npos);
    CHECK(text.find("MAST.XPG\nArmoire_Gaz.xpgproj") != std::string::npos);
    CHECK(text.find("Aide > Journal interne") != std::string::npos);
    CHECK(text.find("(fin du rapport)") != std::string::npos);
    (void)dir;
}

#if !defined(_WIN32)
// Un fils qui plante expres (SIGSEGV) : le rapport doit etre la, entier.
static void testCrash(const fs::path& dir) {
    const auto before = reports(dir, "plantage-").size();
    const pid_t pid = fork();
    if (pid == 0) {
        crash::Options o;
        o.folder = dir.string();
        o.dialogs = false;
        crash::install(o);   // deja fait par le pere : sans effet ; les gestionnaires sont herites
        XPG_PORTEE("main");
        XPG_TRACE(Action, "juste avant le plantage");
        crash::armTest(crash::Test::Crash, 0);
        crash::heartbeat();
        _exit(0);   // jamais atteint
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
    const auto after = reports(dir, "plantage-");
    CHECK(after.size() == before + 1);
    if (after.empty()) return;
    fs::path newest = after.front();
    for (const auto& p : after) if (p.filename().string() > newest.filename().string()) newest = p;
    const std::string text = readAll(newest);
    CHECK(text.find("Raison      : SIGSEGV") != std::string::npos);
    CHECK(text.find("adresse 0x0") != std::string::npos);
    CHECK(text.find("provokeCrash") != std::string::npos || text.find("crashtrail_test") != std::string::npos);
    CHECK(text.find("essai::plantage") != std::string::npos);
    CHECK(text.find("juste avant le plantage") != std::string::npos);
    CHECK(text.find("plantage") != std::string::npos);
    std::printf("plantage : %s (%zu octets)\n", newest.filename().string().c_str(), text.size());
}

// Un fils dont la boucle se bloque 2,5 s avec un chien de garde a 1 s.
static void testHang(const fs::path& dir) {
    const auto before = reports(dir, "blocage-").size();
    const pid_t pid = fork();
    if (pid == 0) {
        crash::startWatchdog(1, false);
        for (int i = 0; i < 5; ++i) { crash::heartbeat(); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
        {
            XPG_PORTEE("essai::boucle_bloquee");
            std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        }
        crash::heartbeat();
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        double seconds = 0;
        const bool recovered = crash::takeRecovery(seconds);
        crash::stopWatchdog();
        _exit(recovered && seconds > 1.5 ? 0 : 7);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    const auto after = reports(dir, "blocage-");
    CHECK(after.size() == before + 1);
    if (after.empty()) return;
    const std::string text = readAll(after.back());
    CHECK(text.find("Raison      : blocage : la boucle principale ne bat plus depuis") != std::string::npos);
    CHECK(text.find("Pile d'appels du fil principal") != std::string::npos);
    CHECK(text.find("(pile non lue)") == std::string::npos);
    CHECK(text.find("essai::boucle_bloquee   depuis 1,") != std::string::npos);
    std::printf("blocage : %s\n", after.back().filename().string().c_str());
}

// 1.10.2 : la boite du plantage, simulee, reste ouverte 10 s ; le chien de garde est a 1 s.
// Sous wine, la vraie boite laissee ouverte plus de 8 s faisait AUSSI un rapport de blocage
// (et une 2e boite) : il ne doit y avoir qu'un rapport, celui du plantage.
// Appelee depuis le gestionnaire du signal : write, clock_gettime et nanosleep seulement.
static void crashBoxOpen10s(const char*) {
    static const char open[] = "boite du plantage ouverte (10 s)\n";
    (void)!::write(2, open, sizeof open - 1);
    timespec start{}, now{};
    clock_gettime(CLOCK_MONOTONIC, &start);
    long long elapsedMs = 0;
    do {
        timespec step{0, 100 * 1000 * 1000};
        nanosleep(&step, nullptr);      // un signal peut l'interrompre : on reboucle
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsedMs = (now.tv_sec - start.tv_sec) * 1000LL + (now.tv_nsec - start.tv_nsec) / 1000000;
    } while (elapsedMs < 10000);
    static const char closed[] = "boite du plantage fermee\n";
    (void)!::write(2, closed, sizeof closed - 1);
}

static void testCrashBoxOpen(const fs::path& dir) {
    const auto crashesBefore = reports(dir, "plantage-").size();
    const auto hangsBefore = reports(dir, "blocage-").size();
    const auto start = std::chrono::steady_clock::now();
    const pid_t pid = fork();
    if (pid == 0) {
        crash::setCrashBox(crashBoxOpen10s);
        crash::startWatchdog(1, false);
        XPG_PORTEE("essai::boite_ouverte");
        for (int i = 0; i < 5; ++i) { crash::heartbeat(); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
        crash::armTest(crash::Test::Crash, 0);
        crash::heartbeat();
        _exit(0);   // jamais atteint
    }
    int status = 0;
    waitpid(pid, &status, 0);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
    CHECK(seconds >= 10.0);                                            // la boite est restee ouverte 10 s
    CHECK(reports(dir, "plantage-").size() == crashesBefore + 1);      // le rapport du plantage
    CHECK(reports(dir, "blocage-").size() == hangsBefore);             // et aucun rapport de blocage
    std::printf("boite du plantage ouverte 10 s (fils fini en %.1f s) : %zu plantage, %zu blocage en plus\n", seconds,
                reports(dir, "plantage-").size() - crashesBefore, reports(dir, "blocage-").size() - hangsBefore);
}
#endif

static void testPending(const fs::path& dir) {
    const auto first = crash::takeNewReport();
    CHECK(first.has_value());
    if (first) {
        CHECK(!first->when.empty() && !first->text.empty());
#if !defined(_WIN32)
        CHECK(first->kind == "plantage");   // le dernier : le plantage a la boite ouverte 10 s, pas un blocage
#endif
        std::printf("au lancement suivant : %s du %s\n", first->kind.c_str(), first->when.c_str());
    }
    CHECK(!crash::takeNewReport().has_value());   // montre une seule fois
    (void)dir;
}

// Le cout : une portee (deux entrees) et une note.
static void testCost() {
    trail::resetForTests();
    constexpr int n = 200000;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) { XPG_PORTEE("cout::portee"); }
    auto t1 = std::chrono::steady_clock::now();
    trail::setEnabled(false);
    for (int i = 0; i < n; ++i) { XPG_PORTEE("cout::portee"); }
    auto t2 = std::chrono::steady_clock::now();
    trail::setEnabled(true);
    const double on = std::chrono::duration<double, std::nano>(t1 - t0).count() / n;
    const double off = std::chrono::duration<double, std::nano>(t2 - t1).count() / n;
    std::printf("cout d'une portee (entree + sortie) : %.0f ns (coupe : %.0f ns)\n", on, off);
    CHECK(on < 5000);
}

int main(int argc, char** argv) {
    const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "crashtrail_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    crash::Options o;
    o.folder = dir.string();
    o.dialogs = false;
    crash::install(o);

    // Windows (wine) : le cas se joue ici meme, le script lit le rapport.
    const std::string mode = argc > 2 ? argv[2] : "";
    if (mode == "--plantage" || mode == "--blocage") {
        XPG_PORTEE("main");
        XPG_TRACE(Action, "essai %s", mode.c_str());
        if (mode == "--blocage") crash::startWatchdog(1, false);
        crash::armTest(mode == "--plantage" ? crash::Test::Crash : crash::Test::Hang, 200);
        // La boucle bat jusqu'a ce que le chien de garde voie la reprise (30 s au plus :
        // sous wine, le minidump et les symboles prennent quelques secondes).
        double seconds = 0;
        bool recovered = false;
        const auto start = std::chrono::steady_clock::now();
        while (!recovered && std::chrono::steady_clock::now() - start < std::chrono::seconds(30)) {
            crash::heartbeat();
            recovered = crash::takeRecovery(seconds);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        crash::stopWatchdog();
        std::printf("fin de l'essai %s (reprise : %s, %.1f s)\n", mode.c_str(), recovered ? "oui" : "non", seconds);
        return 0;
    }

    testRing();
    testScopes();
    testReportFormat(dir);
#if !defined(_WIN32)
    testCrash(dir);
    testHang(dir);
    testCrashBoxOpen(dir);
#endif
    testPending(dir);
    testCost();
    std::printf("crashtrail_test : %d verifications, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
