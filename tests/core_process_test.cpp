// =============================================================================
//  tests/core_process_test.cpp - 1.21.0 : core::process (Process.hpp)
// -----------------------------------------------------------------------------
//  L'essai se relance lui-meme comme enfant (un premier argument dit quoi faire) :
//  l'entree et la sortie, le code de sortie, l'environnement vide, le temps
//  depasse, la memoire depassee, la sortie trop longue (coupee), l'arret a la
//  demande, un programme absent.
// =============================================================================
#include "../src/core/Process.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_ok = 0, g_fail = 0;
void check(bool ok, const std::string& what) {
    if (ok) ++g_ok;
    else {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

int child(const char* what) {
    if (std::strcmp(what, "echo") == 0) {
        char b[256];
        const std::size_t n = std::fread(b, 1, sizeof b, stdin);
        std::fwrite(b, 1, n, stdout);
        std::fprintf(stderr, "%s", std::getenv("HOME") || std::getenv("USERPROFILE") || std::getenv("PATH") ? "env" : "vide");
        return 3;
    }
    if (std::strcmp(what, "boucle") == 0) {
        volatile unsigned long x = 0;
        for (;;) x = x + 1;
    }
    if (std::strcmp(what, "memoire") == 0) {
        std::vector<std::vector<char>> v;
        for (;;) v.emplace_back(std::size_t{64} << 20, 'x');
    }
    if (std::strcmp(what, "bavard") == 0) {
        for (int i = 0; i < 200000; ++i) std::printf("ligne %d\n", i);
        return 0;
    }
    return 99;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) return child(argv[1]);
    namespace pr = core::process;
    pr::Request r;
    r.program = pr::selfPath();
    check(!r.program.empty(), "le chemin du programme en cours");
    r.args = {"echo"};
    r.input = "bonjour";
    const auto a = pr::run(r);
    check(a.started && a.exitCode == 3 && a.out == "bonjour" && a.err == "vide" && !a.ok(),
          "l'entree, la sortie, le code (3), un environnement vide (ni HOME, ni PATH)");
    r.args = {"boucle"};
    r.input.clear();
    r.limits.seconds = 1.0;
    const auto b = pr::run(r);
    check(b.started && b.timedOut && b.seconds < 4.0 && b.summary().find("temps") != std::string::npos, "une boucle sans fin : tuee au bout du temps, et dit");
    r.args = {"memoire"};
    r.limits.seconds = 20.0;
    r.limits.memoryBytes = std::size_t{256} << 20;
    const auto c = pr::run(r);
    check(c.started && !c.ok() && c.memoryExceeded, "une memoire sans fin : arretee a la limite, et dit (" + c.summary() + ")");
    r.args = {"bavard"};
    r.limits.memoryBytes = std::size_t{512} << 20;
    r.limits.outputBytes = 10000;
    const auto d = pr::run(r);
    check(d.started && d.outputCut && d.out.size() == 10000 && d.exitCode == 0, "une sortie trop longue : coupee a la limite, et dit");
    std::atomic<bool> stop{false};
    r.args = {"boucle"};
    r.limits.seconds = 30.0;
    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        stop = true;
    });
    const auto e = pr::run(r, &stop);
    t.join();
    check(e.cancelled && e.seconds < 5.0, "Arreter : l'enfant tue tout de suite");
    pr::Request bad;
    bad.program = "/nexiste/pas/du/tout";
    const auto f = pr::run(bad);
    check(!f.started && !f.error.empty(), "un programme absent : pas lance, et dit");
    std::printf("core_process_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
