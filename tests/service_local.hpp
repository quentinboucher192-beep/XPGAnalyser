// =============================================================================
//  tests/service_local.hpp - 1.35.0 : LE SERVICE DES LICENCES SUR CE POSTE,
//  pour les essais C++ (Studios, outil de l'editeur)
// -----------------------------------------------------------------------------
//  Lance le VRAI Worker (service/, dans workerd, par `wrangler dev --local` :
//  outils/service/lancer-local.sh) sur un port libre de 127.0.0.1, avec les
//  cles PUBLIQUES de l'editeur donnees (PUBLISHER_KEYS, hexadecimal) et un
//  dossier d'etat neuf (efface a la fin), attend qu'il reponde (GET /v1/etat,
//  60 s au plus) et rend son adresse "http://127.0.0.1:PORT". Le destructeur
//  l'arrete (tout son groupe de processus : bash, node, workerd).
//
//      xpg_test::LocalService svc({editeur->publicKeyHex()});
//      if (svc.skipped()) { std::printf("SAUTE : %s\n", svc.error().c_str()); return 0; }
//      if (!svc.ready()) { std::printf("ECHEC : %s\n", svc.error().c_str()); return 1; }
//      ... svc.url() ...
//
//  SAUTE (skipped) : Node.js (22 ou plus) ou wrangler manquent sur ce poste
//  (`npm ci` dans service/), ou ce n'est pas Linux/macOS : l'essai le dit et
//  rend 0 (l'integrateur decide). Toute autre panne : ready() faux, error().
//
//  LES LIMITES (10 activations par heure et par adresse...) sont levees par
//  defaut : tous les appels d'un essai viennent de 127.0.0.1 (contractLimits :
//  les garder). LE TEMPS : restart(decalage) relance le service sur le MEME etat avec son
//  horloge avancee de `decalage` secondes (un bail echu, un jeton expire) ; la
//  cle du service et les tables sont gardees, et le MEME port (le certificat du
//  service nomme son adresse : les Studios refusent un document d'une autre
//  adresse) - s'il est pris entre-temps, un autre (url() change).
//
//  Ou est le depot : le parametre `sourceDir`, sinon la variable XPG_SOURCE_DIR,
//  sinon le dossier de ce fichier (__FILE__ : .../tests/service_local.hpp).
//  XPG_SERVICE_LANCEUR peut nommer un autre lancer-local.sh.
//
//  1.46.0 : RIEN NE RESTE APRES L'ESSAI, MEME EN ECHEC. Avant, un essai qui partait
//  sans son destructeur (abort, exception non attrapee, std::exit, un signal)
//  laissait tourner le lanceur, wrangler et workerd - et ils gardaient les
//  descripteurs de l'essai, dont le verrou d'une construction (flock) : la suivante
//  attendait sans fin. Maintenant (childHygiene, dans l'enfant, avant exec) :
//    - PR_SET_PDEATHSIG (Linux) : le lanceur recoit SIGTERM des que l'essai meurt,
//      quelle qu'en soit la cause ; son piege (lancer-local.sh) arrete wrangler et
//      ses workerd ;
//    - aucun descripteur au-dela de 0, 1, 2 n'est herite (close_range, sinon une
//      boucle) : ni le verrou, ni les tubes d'un autre service ou du faux Stripe ;
//    - le tube que l'essai garde est FD_CLOEXEC (une console lancee par popen ne
//      l'herite pas).
//  Le meme pour tout programme qu'un essai lance a part (le faux Stripe : FakeStripe).
//  `flock -o` (le verrou ferme avant la commande) suffit aussi pour le verrou : il
//  n'est alors tenu que par flock, qui le rend a la fin de la commande, meme si un
//  descendant reste.
// =============================================================================
#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif
#endif

namespace xpg_test {

#if !defined(_WIN32)
// 1.46.0 : DANS L'ENFANT, apres fork et la mise en place de 0, 1, 2, avant exec (voir plus haut) : il meurt avec l'essai
// (`parent` : le pid de l'essai, lu AVANT fork) et n'herite d'aucun autre descripteur. Seulement des appels permis
// apres fork (prctl, getppid, close, syscall, sysconf, kill).
inline void childHygiene(pid_t parent) noexcept {
#if defined(__linux__)
    (void)::prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (::getppid() != parent) ::_exit(126);   // l'essai est deja parti (entre fork et prctl)
#else
    (void)parent;
#endif
#if defined(__linux__) && defined(SYS_close_range)
    if (::syscall(SYS_close_range, 3U, ~0U, 0U) == 0) return;
#endif
    long max = ::sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 65536) max = 65536;
    for (int fd = 3; fd < static_cast<int>(max); ++fd) (void)::close(fd);
}

// 1.46.0 : LE FAUX STRIPE (tests/outils/faux_stripe.mjs) lance a part pour un essai : node, son port lu de sa sortie
// (<< faux stripe pret sur le port N >>), arrete a la fin (et avec l'essai : childHygiene). Les appels (__essai/...) :
// l'essai, avec son propre client HTTP.
class FakeStripeProcess {
public:
    FakeStripeProcess(const std::string& sourceDir, const std::string& key) {
        int fds[2];
        if (::pipe(fds) != 0) return;
        const std::string script = sourceDir + "/tests/outils/faux_stripe.mjs";
        const pid_t parent = ::getpid();
        const pid_t pid = ::fork();
        if (pid < 0) {
            ::close(fds[0]);
            ::close(fds[1]);
            return;
        }
        if (pid == 0) {
            ::setsid();
            ::dup2(fds[1], 1);
            const int nul = ::open("/dev/null", O_RDWR);
            if (nul >= 0) {
                ::dup2(nul, 0);
                ::dup2(nul, 2);
            }
            childHygiene(parent);
            ::execlp("node", "node", script.c_str(), "--cle", key.c_str(), static_cast<char*>(nullptr));
            ::_exit(127);
        }
        ::close(fds[1]);
        pid_ = pid;
        std::string out;
        for (int i = 0; i < 150 && port_ == 0; ++i) {
            struct pollfd p{fds[0], POLLIN, 0};
            if (::poll(&p, 1, 100) > 0) {
                char buf[256];
                const ssize_t n = ::read(fds[0], buf, sizeof buf);
                if (n <= 0) break;
                out.append(buf, static_cast<std::size_t>(n));
                const auto at = out.find("port ");
                if (at != std::string::npos && out.find('\n', at) != std::string::npos) port_ = std::atoi(out.c_str() + at + 5);
            }
        }
        ::close(fds[0]);
    }
    ~FakeStripeProcess() { stop(); }
    FakeStripeProcess(const FakeStripeProcess&) = delete;
    FakeStripeProcess& operator=(const FakeStripeProcess&) = delete;
    void stop() {
        if (pid_ <= 0) return;
        ::kill(-pid_, SIGTERM);
        int st = 0;
        bool gone = false;
        for (int i = 0; i < 60 && !gone; ++i) {
            const pid_t r = ::waitpid(pid_, &st, WNOHANG);
            if (r == pid_ || (r < 0 && errno == ECHILD)) gone = true;
            else {
                struct timespec ts{0, 50 * 1000000L};
                ::nanosleep(&ts, nullptr);
            }
        }
        if (!gone) {
            ::kill(-pid_, SIGKILL);
            ::waitpid(pid_, &st, 0);
        }
        pid_ = -1;
        port_ = 0;
    }
    [[nodiscard]] bool ready() const noexcept { return port_ > 0; }
    [[nodiscard]] int port() const noexcept { return port_; }
    [[nodiscard]] std::string base() const { return "http://127.0.0.1:" + std::to_string(port_); }

private:
    int   port_{0};
    pid_t pid_{-1};
};
#endif

class LocalService {
public:
    struct Options {
        long long   clockOffset{0};     // XPG_DECALAGE_HORLOGE_ESSAI (secondes)
        int         trialDays{30};      // DECOUVERTE_JOURS
        int         timeoutSeconds{60};
        // Faux (par defaut) : les limites par adresse et par PC sont levees (XPG_LIMITES_ESSAI=sans) - tous les appels
        // d'un essai viennent de 127.0.0.1. Vrai : les limites du contrat (essayer un 429).
        bool        contractLimits{false};
        std::string stateDir;           // vide : un dossier neuf dans le dossier temporaire (efface a la fin)
        std::string sourceDir;          // vide : XPG_SOURCE_DIR, sinon le depot de ce fichier
        // 1.46.0 : des variables de plus pour le lanceur (le faux Stripe de la boutique : XPG_STRIPE_SECRET_KEY,
        // XPG_STRIPE_WEBHOOK_SECRET - des valeurs essai_jeton_... - et XPG_STRIPE_API_BASE=http://127.0.0.1:PORT).
        std::vector<std::pair<std::string, std::string>> env;
    };

    explicit LocalService(std::vector<std::string> publisherKeysHex) : LocalService(std::move(publisherKeysHex), Options{}) {}
    LocalService(std::vector<std::string> publisherKeysHex, Options options) : keys_(std::move(publisherKeysHex)), opt_(std::move(options)) {
        for (const auto& k : keys_) {
            if (!keysCsv_.empty()) keysCsv_ += ",";
            keysCsv_ += k;
        }
        script_ = findScript();
        if (opt_.stateDir.empty()) {
            std::random_device rd;
            std::mt19937_64 rng(rd());
            stateDir_ = (std::filesystem::temp_directory_path() / ("xpg-service-local-" + std::to_string(rng() % 1000000000000ULL))).string();
            ownsState_ = true;
        } else {
            stateDir_ = opt_.stateDir;
        }
        start();
    }

    ~LocalService() {
        stop();
        if (ownsState_) {
            std::error_code ec;
            std::filesystem::remove_all(stateDir_, ec);
        }
    }

    LocalService(const LocalService&) = delete;
    LocalService& operator=(const LocalService&) = delete;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] bool skipped() const noexcept { return skipped_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] const std::string& url() const noexcept { return url_; }
    [[nodiscard]] int port() const noexcept { return port_; }
    [[nodiscard]] const std::string& stateDir() const noexcept { return stateDir_; }
    // Le temps de demarrage du dernier lancement (secondes).
    [[nodiscard]] double startSeconds() const noexcept { return startSeconds_; }

    // Relancer sur le meme etat, l'horloge du service avancee de `clockOffset` secondes. Faux : error().
    bool restart(long long clockOffset) {
        const int same = port_;
        stop();
        opt_.clockOffset = clockOffset;
        start(same);
        if (!ready_ && same > 0) {
            stop();
            start(0);   // le port a ete pris entre-temps : un autre
        }
        return ready_;
    }

    void stop() {
#if !defined(_WIN32)
        if (pid_ > 0) {
            ::kill(-pid_, SIGTERM);
            int status = 0;
            bool gone = false;
            for (int i = 0; i < 100 && !gone; ++i) {
                const pid_t r = ::waitpid(pid_, &status, WNOHANG);
                if (r == pid_ || (r < 0 && errno == ECHILD)) gone = true;
                else sleepMs(50);
            }
            // ce qui reste du groupe (workerd) : tue
            ::kill(-pid_, SIGKILL);
            if (!gone) ::waitpid(pid_, &status, 0);
            pid_ = -1;
        }
        if (outFd_ >= 0) {
            ::close(outFd_);
            outFd_ = -1;
        }
#endif
        ready_ = false;
    }

private:
    std::string findScript() const {
        namespace fs = std::filesystem;
        if (const char* s = std::getenv("XPG_SERVICE_LANCEUR"); s && *s) return s;
        const fs::path tail = fs::path("outils") / "service" / "lancer-local.sh";
        if (!opt_.sourceDir.empty()) return (fs::path(opt_.sourceDir) / tail).string();
        if (const char* d = std::getenv("XPG_SOURCE_DIR"); d && *d) return (fs::path(d) / tail).string();
        // le depot de ce fichier (__FILE__ est absolu, ou relatif au dossier de compilation), sinon en remontant depuis
        // le dossier courant et celui de l'executable (build-linux/ est dans le depot)
        std::vector<fs::path> starts;
        const fs::path here = fs::path(__FILE__).parent_path().parent_path();
        if (here.is_absolute()) starts.push_back(here);
        std::error_code ec;
        starts.push_back(fs::current_path(ec));
#if !defined(_WIN32)
        const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) starts.push_back(exe.parent_path());
#endif
        for (fs::path p : starts) {
            for (int up = 0; up < 6 && !p.empty(); ++up) {
                if (fs::exists(p / tail, ec)) return (p / tail).string();
                if (p == p.parent_path()) break;
                p = p.parent_path();
            }
        }
        return (here / tail).string();
    }

#if !defined(_WIN32)
    static void sleepMs(int ms) {
        struct timespec ts{ms / 1000, static_cast<long>(ms % 1000) * 1000000L};
        ::nanosleep(&ts, nullptr);
    }
    static double nowSeconds() {
        struct timespec ts{};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
    }
    static std::string readFile(const std::string& path) {
        std::string out;
        if (FILE* f = std::fopen(path.c_str(), "rb")) {
            char buf[4096];
            std::size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
            std::fclose(f);
        }
        return out;
    }
    static std::string lastLines(const std::string& text, int count) {
        std::size_t pos = text.size();
        while (pos > 0 && (text[pos - 1] == '\n' || text[pos - 1] == '\r')) --pos;
        int seen = 0;
        while (pos > 0) {
            if (text[pos - 1] == '\n' && ++seen == count) break;
            --pos;
        }
        std::string out = text.substr(pos);
        for (char& c : out)
            if (c == '\n' || c == '\r') c = ' ';
        return out;
    }
#endif

    void start(int wantedPort = 0) {
        ready_ = false;
        skipped_ = false;
        error_.clear();
        url_.clear();
        port_ = 0;
#if defined(_WIN32)
        skipped_ = true;
        error_ = "le service local des licences n'est lance que sous Linux (node, wrangler, workerd)";
#else
        std::error_code ec;
        if (!std::filesystem::exists(script_, ec)) {
            skipped_ = true;
            error_ = "le lanceur du service est introuvable (" + script_ + ")";
            return;
        }
        if (::access("/bin/bash", X_OK) != 0) {
            skipped_ = true;
            error_ = "bash manque sur ce poste";
            return;
        }
        std::filesystem::create_directories(stateDir_, ec);
        const std::string errPath = stateDir_ + "/lanceur-erreurs.txt";
        int fds[2];
        if (::pipe(fds) != 0) {
            error_ = "pipe impossible";
            return;
        }
        const std::string portArg = std::to_string(wantedPort > 0 ? wantedPort : 0);   // avant fork : rien n'est alloue dans l'enfant
        const double t0 = nowSeconds();
        const pid_t parent = ::getpid();   // 1.46.0 : childHygiene
        const pid_t pid = ::fork();
        if (pid < 0) {
            ::close(fds[0]);
            ::close(fds[1]);
            error_ = "fork impossible";
            return;
        }
        if (pid == 0) {
            // l'enfant : son propre groupe (tue d'un coup a la fin), la sortie dans le tube, les erreurs dans un fichier
            ::setsid();
            ::dup2(fds[1], 1);
            ::close(fds[0]);
            ::close(fds[1]);
            const int err = ::open(errPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (err >= 0) {
                ::dup2(err, 2);
                ::close(err);
            }
            const int nul = ::open("/dev/null", O_RDONLY);
            if (nul >= 0) {
                ::dup2(nul, 0);
                ::close(nul);
            }
            ::setenv("XPG_DECALAGE_HORLOGE_ESSAI", std::to_string(opt_.clockOffset).c_str(), 1);
            ::setenv("XPG_DECOUVERTE_JOURS", std::to_string(opt_.trialDays).c_str(), 1);
            ::setenv("XPG_SERVICE_ATTENTE", std::to_string(opt_.timeoutSeconds).c_str(), 1);
            if (opt_.contractLimits) ::unsetenv("XPG_LIMITES_ESSAI");
            else ::setenv("XPG_LIMITES_ESSAI", "sans", 1);
            for (const auto& [k, v] : opt_.env) ::setenv(k.c_str(), v.c_str(), 1);   // 1.46.0
            childHygiene(parent);   // 1.46.0 : il meurt avec l'essai, sans heriter du verrou ni des tubes
            ::execl("/bin/bash", "bash", script_.c_str(), portArg.c_str(), stateDir_.c_str(), keysCsv_.c_str(), static_cast<char*>(nullptr));
            ::_exit(127);
        }
        ::close(fds[1]);
        pid_ = pid;
        outFd_ = fds[0];
        (void)::fcntl(outFd_, F_SETFD, FD_CLOEXEC);   // 1.46.0 : pas herite par une console lancee par l'essai
        std::string out;
        const std::string marker = "service pret sur le port ";
        const double deadline = t0 + opt_.timeoutSeconds + 5;
        while (nowSeconds() < deadline) {
            struct pollfd p{outFd_, POLLIN, 0};
            const int r = ::poll(&p, 1, 200);
            if (r > 0) {
                char buf[512];
                const ssize_t n = ::read(outFd_, buf, sizeof buf);
                if (n > 0) out.append(buf, static_cast<std::size_t>(n));
                const auto at = out.find(marker);
                if (at != std::string::npos && out.find('\n', at) != std::string::npos) {
                    port_ = std::atoi(out.c_str() + at + marker.size());
                    if (port_ > 0) {
                        url_ = "http://127.0.0.1:" + std::to_string(port_);
                        ready_ = true;
                        startSeconds_ = nowSeconds() - t0;
                        return;
                    }
                }
                if (n == 0) {
                    // le lanceur s'est arrete : pourquoi ?
                    int status = 0;
                    ::waitpid(pid_, &status, 0);
                    pid_ = -1;
                    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                    const std::string why = lastLines(readFile(errPath), 3);
                    if (code == 3) {
                        skipped_ = true;
                        error_ = "le service local ne peut pas etre lance sur ce poste : " + why;
                    } else {
                        error_ = "le service local n'a pas demarre (code " + std::to_string(code) + ") : " + why;
                    }
                    ::close(outFd_);
                    outFd_ = -1;
                    return;
                }
            }
        }
        error_ = "le service local n'a pas repondu en " + std::to_string(opt_.timeoutSeconds) + " s : " + lastLines(readFile(errPath), 3);
        stop();
#endif
    }

    std::vector<std::string> keys_;
    std::string              keysCsv_;
    Options                  opt_;
    std::string              script_;
    std::string              stateDir_;
    bool                     ownsState_{false};
    bool                     ready_{false};
    bool                     skipped_{false};
    std::string              error_;
    std::string              url_;
    int                      port_{0};
    double                   startSeconds_{0};
#if !defined(_WIN32)
    pid_t pid_{-1};
    int   outFd_{-1};
#endif
};

} // namespace xpg_test
