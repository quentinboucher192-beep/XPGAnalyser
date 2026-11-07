// =============================================================================
//  tests/core_process_test.cpp - 1.21.0 : core::process (Process.hpp)
// -----------------------------------------------------------------------------
//  L'essai se relance lui-meme comme enfant (un premier argument dit quoi faire) :
//  l'entree et la sortie, le code de sortie, l'environnement vide, le temps
//  depasse, la memoire depassee, la sortie trop longue (coupee), l'arret a la
//  demande, un programme absent.
//  Securite : l'enfant n'herite d'aucun autre descripteur que 0, 1, 2 (Linux) ;
//  un enfant qui ecrit beaucoup AVANT de lire son entree ne bloque pas le parent.
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

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

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
    if (std::strcmp(what, "bavard-avant") == 0) {   // 1 Mo de sortie avant de lire l'entree
        const std::string line(1023, 'x');
        for (int i = 0; i < 1024; ++i) std::printf("%s\n", line.c_str());
        std::fflush(stdout);
        std::size_t got = 0;
        char b[65536];
        for (std::size_t n; (n = std::fread(b, 1, sizeof b, stdin)) > 0;) got += n;
        std::fprintf(stderr, "%zu", got);
        return 0;
    }
#if !defined(_WIN32)
    if (std::strcmp(what, "descripteurs") == 0) {   // un descripteur au-dela de 2 encore ouvert ?
        int open = 0;
        for (int fd = 3; fd < 1024; ++fd)
            if (fcntl(fd, F_GETFD) != -1) ++open;
        std::printf("%d", open);
        return 0;
    }
#endif
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
    // Une entree de 1 Mo pour un enfant qui ecrit 1 Mo d'abord : ni l'un ni l'autre ne reste bloque.
    pr::Request big;
    big.program = pr::selfPath();
    big.args = {"bavard-avant"};
    big.input.assign(std::size_t{1} << 20, 'e');
    big.limits.seconds = 20.0;
    big.limits.outputBytes = std::size_t{4} << 20;
    const auto g = pr::run(big);
    check(g.started && !g.timedOut && g.exitCode == 0 && g.out.size() == std::size_t{1} << 20 && g.err == std::to_string(std::size_t{1} << 20),
          "1 Mo de sortie avant de lire 1 Mo d'entree : fini, sans blocage (" + g.summary() + ", entree lue : " + g.err + ")");
#if !defined(_WIN32)
    // Un descripteur heritable du parent (sans O_CLOEXEC) : l'enfant ne l'a pas.
    const int leak = ::open("/dev/null", O_RDONLY);
    pr::Request fds;
    fds.program = pr::selfPath();
    fds.args = {"descripteurs"};
    const auto h = pr::run(fds);
    if (leak >= 0) ::close(leak);
    check(leak >= 0 && h.started && h.exitCode == 0 && h.out == "0", "l'enfant n'herite d'aucun descripteur au-dela de 0, 1, 2 (ouverts : " + h.out + ")");
#endif
    pr::Request bad;
    bad.program = "/nexiste/pas/du/tout";
    const auto f = pr::run(bad);
    check(!f.started && !f.error.empty(), "un programme absent : pas lance, et dit");
    std::printf("core_process_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
