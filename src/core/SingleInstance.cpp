// =============================================================================
//  core/SingleInstance.cpp - 1.11.2 (UNI, decision 195) : une seule instance
// -----------------------------------------------------------------------------
//  Voir SingleInstance.hpp. Le fil d'ecoute ne touche a rien de l'appli : il range
//  la demande sous un verrou et appelle wake (App y pose un evenement SDL).
// =============================================================================
#include "SingleInstance.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <fcntl.h>
#  include <poll.h>
#  include <sys/file.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/time.h>
#  include <sys/un.h>
#  include <unistd.h>
#endif

namespace core::instance {

namespace {

constexpr char          kMagic[4] = {'X', 'P', 'G', 'I'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaxPayload = 1u << 20;   // 1 Mio : bien plus qu'une ligne de commande
constexpr char          kReply[2] = {'O', 'K'};

void put32(std::string& s, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
}
std::uint32_t get32(const char* p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(p[i])) << (8 * i);
    return v;
}

Guard* g_current = nullptr;

} // namespace

// --------------------------------------------------------------- le message --
std::string encode(const std::vector<std::string>& args) {
    std::string payload;
    for (const auto& a : args) {
        payload += a;
        payload.push_back('\0');
    }
    std::string s(kMagic, sizeof kMagic);
    put32(s, kVersion);
    put32(s, static_cast<std::uint32_t>(payload.size()));
    return s + payload;
}

bool decode(const std::string& bytes, std::vector<std::string>& args) {
    args.clear();
    if (bytes.size() < 12 || std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) return false;
    if (get32(bytes.data() + 4) != kVersion) return false;
    const std::uint32_t n = get32(bytes.data() + 8);
    if (n > kMaxPayload || bytes.size() != 12u + static_cast<std::size_t>(n)) return false;
    std::string current;
    for (std::size_t i = 12; i < bytes.size(); ++i) {
        if (bytes[i] == '\0') {
            args.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(bytes[i]);
        }
    }
    if (!current.empty()) {   // le dernier argument n'est pas termine : un message abime
        args.clear();
        return false;
    }
    return true;
}

// ------------------------------------------------------- les modes d'essai --
bool exempted(const std::vector<std::string>& args, const char* multiples, std::string* why) {
    for (const auto& a : args)
        if (a == "--instance-unique") return false;   // impose (la session du banc wine)
    if (multiples && *multiples && std::string(multiples) != "0") {
        if (why) *why = "XPG_INSTANCES_MULTIPLES=" + std::string(multiples);
        return true;
    }
    static const char* const kTests[] = {
        "--script", "--captures", "--size",                                  // les sessions rejouees
        "--verifier-tutoriels", "--verifier-tutoriel", "--verifier-sujets",  // les verifications (T1)
        "--verifier-dossier", "--tutoriel-texte", "--compter-tutoriels", "--bilan",
        "--essai-plantage", "--essai-blocage", "--sans-historique",          // les essais (CR)
        "--cli", "--version",                                                // sans fenetre
    };
    for (const auto& a : args)
        for (const char* t : kTests)
            if (a == t) {
                if (why) *why = a;
                return true;
            }
    return false;
}

// ------------------------------------------------------------------ l'etat --
struct Guard::Impl {
    Options                  options;
    Role                     role{Role::Unchecked};
    std::string              why;
    std::mutex               mutex;
    std::vector<Request>     inbox;
    std::function<void()>    wake;
    std::thread              listener;
#if defined(_WIN32)
    HANDLE                   instanceMutex{nullptr};
    HANDLE                   stop{nullptr};
    std::wstring             pipeName;
#else
    int                      lockFd{-1};
    int                      listenFd{-1};
    int                      stopPipe[2]{-1, -1};
    std::string              socketPath;
#endif

    void deliver(std::vector<std::string> args) {
        std::function<void()> w;
        {
            std::lock_guard<std::mutex> lock(mutex);
            inbox.push_back(Request{std::move(args)});
            w = wake;
        }
        if (w) w();
    }
};

#if defined(_WIN32)
// ================================================================= Windows ==
namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// L'utilisateur, en lettres sures pour un nom d'objet (pas de \), plus une empreinte
// du nom entier : deux comptes aux noms voisins n'ont pas le meme verrou.
std::wstring userTag() {
    wchar_t buffer[257] = {};
    DWORD   size = 257;
    const std::wstring user = GetUserNameW(buffer, &size) ? std::wstring(buffer) : std::wstring(L"inconnu");
    std::wstring  tag;
    std::uint32_t h = 2166136261u;
    for (const wchar_t c : user) {
        h = (h ^ static_cast<std::uint32_t>(c)) * 16777619u;
        const bool safe = (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
        tag.push_back(safe ? c : L'_');
    }
    static const wchar_t kHex[] = L"0123456789abcdef";
    tag.push_back(L'-');
    for (int i = 7; i >= 0; --i) tag.push_back(kHex[(h >> (4 * i)) & 0xFu]);
    return tag;
}

// Une operation commencee (started : ce qu'a rendu ReadFile, WriteFile ou
// ConnectNamedPipe) : 0 quand elle a fini (n octets), ERROR_MORE_DATA pour un
// morceau d'un message plus long, une autre erreur sinon (arret, delai, tube casse).
DWORD complete(HANDLE pipe, OVERLAPPED& ov, BOOL started, HANDLE stop, DWORD ms, DWORD& n) {
    n = 0;
    if (!started) {
        const DWORD e = GetLastError();
        if (e == ERROR_MORE_DATA) return GetOverlappedResult(pipe, &ov, &n, FALSE) ? 0 : GetLastError();
        if (e != ERROR_IO_PENDING) return e;
        HANDLE waits[2] = {ov.hEvent, stop};
        const DWORD w = WaitForMultipleObjects(stop ? 2 : 1, waits, FALSE, ms);
        if (w != WAIT_OBJECT_0) {
            CancelIo(pipe);
            (void)GetOverlappedResult(pipe, &ov, &n, TRUE);   // l'annulation se termine
            n = 0;
            return ERROR_TIMEOUT;
        }
    }
    return GetOverlappedResult(pipe, &ov, &n, FALSE) ? 0 : GetLastError();
}

bool readMessage(HANDLE pipe, HANDLE event, HANDLE stop, DWORD ms, std::string& out) {
    out.clear();
    char buffer[4096];
    for (;;) {
        OVERLAPPED ov{};
        ov.hEvent = event;
        ResetEvent(event);
        DWORD      n = 0;
        const DWORD e = complete(pipe, ov, ReadFile(pipe, buffer, sizeof buffer, nullptr, &ov), stop, ms, n);
        out.append(buffer, n);
        if (e == 0) return true;
        if (e != ERROR_MORE_DATA || out.size() > 12u + kMaxPayload) return false;
    }
}

bool writeAll(HANDLE pipe, HANDLE event, HANDLE stop, DWORD ms, const char* data, DWORD size) {
    OVERLAPPED ov{};
    ov.hEvent = event;
    ResetEvent(event);
    DWORD n = 0;
    return complete(pipe, ov, WriteFile(pipe, data, size, nullptr, &ov), stop, ms, n) == 0 && n == size;
}

// Le fil d'ecoute : une instance du tube a la fois ; une demande, la reponse "OK",
// puis on attend que la deuxieme ferme (sinon la reponse partirait avec la
// deconnexion), et on recommence.
void serve(Guard::Impl& d) {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) return;
    while (WaitForSingleObject(d.stop, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = CreateNamedPipeW(
            d.pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 1u << 16, 0,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(d.stop, 1000) == WAIT_OBJECT_0) break;
            continue;
        }
        OVERLAPPED ov{};
        ov.hEvent = event;
        ResetEvent(event);
        DWORD n = 0;
        const BOOL  started = ConnectNamedPipe(pipe, &ov);
        DWORD       e = started ? 0 : GetLastError();
        if (!started && e == ERROR_PIPE_CONNECTED) e = 0;
        else if (!started) {
            SetLastError(e);
            e = complete(pipe, ov, FALSE, d.stop, INFINITE, n);
        }
        if (e == 0) {
            std::string bytes;
            std::vector<std::string> args;
            if (readMessage(pipe, event, d.stop, 5000, bytes) && decode(bytes, args)) {
                (void)writeAll(pipe, event, d.stop, 5000, kReply, sizeof kReply);
                std::string rest;
                (void)readMessage(pipe, event, d.stop, 2000, rest);   // jusqu'a ce que la deuxieme ferme
                d.deliver(std::move(args));
            }
            DisconnectNamedPipe(pipe);
        }
        CloseHandle(pipe);
    }
    CloseHandle(event);
}

} // namespace

std::unique_ptr<Guard> Guard::acquire(const Options& options) {
    std::unique_ptr<Guard> g(new Guard());
    g->impl_ = std::make_unique<Impl>();
    Impl& d = *g->impl_;
    d.options = options;
    const std::wstring base = wide(options.name) + L".instance." + userTag();
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) session = 0;
    d.pipeName = L"\\\\.\\pipe\\" + base + L"." + std::to_wstring(session);
    const std::wstring mutexName = L"Local\\" + base;
    SetLastError(0);
    HANDLE h = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    const DWORD e = GetLastError();
    if (h && e == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        d.role = Role::Secondary;
    } else if (!h && e == ERROR_ACCESS_DENIED) {
        d.role = Role::Secondary;   // il existe, cree par un autre contexte de securite
    } else if (!h) {
        d.role = Role::Unchecked;
        d.why = "CreateMutex : erreur " + std::to_string(e);
    } else {
        d.instanceMutex = h;
        d.role = Role::Primary;
    }
    return g;
}

Guard::~Guard() {
    if (!impl_) return;
    Impl& d = *impl_;
    if (d.stop) SetEvent(d.stop);
    if (d.listener.joinable()) d.listener.join();
    if (d.stop) CloseHandle(d.stop);
    if (d.instanceMutex) {
        ReleaseMutex(d.instanceMutex);
        CloseHandle(d.instanceMutex);
    }
    if (g_current == this) g_current = nullptr;
}

bool Guard::listen(std::function<void()> wake) {
    Impl& d = *impl_;
    if (d.role != Role::Primary) return false;
    setWake(std::move(wake));
    if (d.listener.joinable()) return true;
    d.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!d.stop) {
        d.why = "CreateEvent : erreur " + std::to_string(GetLastError());
        return false;
    }
    d.listener = std::thread([&d] { serve(d); });
    return true;
}

bool Guard::forward(const std::vector<std::string>& args, std::string* why) {
    Impl&             d = *impl_;
    const std::string message = encode(args);
    const auto        deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(d.options.connectMs);
    HANDLE            pipe = INVALID_HANDLE_VALUE;
    DWORD             e = 0;
    for (;;) {
        pipe = CreateFileW(d.pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) break;
        e = GetLastError();
        if (std::chrono::steady_clock::now() >= deadline) {
            if (why) *why = "la fen\xC3\xAAtre d\xC3\xA9j\xC3\xA0 ouverte n'\xC3\xA9" "coute pas (erreur " + std::to_string(e) + ")";
            return false;
        }
        if (e == ERROR_PIPE_BUSY) (void)WaitNamedPipeW(d.pipeName.c_str(), 250);
        else Sleep(100);   // la premiere demarre : son tube n'existe pas encore
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    (void)SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
    // AVANT d'envoyer : la premiere traite la demande des l'image suivante, et
    // SetForegroundWindow n'y est permis qu'une fois ceci fait.
    ULONG server = 0;
    if (GetNamedPipeServerProcessId(pipe, &server) && server != 0) (void)AllowSetForegroundWindow(server);
    DWORD written = 0, read = 0;
    char  reply[16] = {};
    bool  ok = WriteFile(pipe, message.data(), static_cast<DWORD>(message.size()), &written, nullptr)
             && written == message.size();
    if (ok) ok = ReadFile(pipe, reply, sizeof reply, &read, nullptr) && read >= sizeof kReply
              && std::memcmp(reply, kReply, sizeof kReply) == 0;
    e = ok ? 0 : GetLastError();
    CloseHandle(pipe);
    if (!ok && why) *why = "la fen\xC3\xAAtre d\xC3\xA9j\xC3\xA0 ouverte n'a pas r\xC3\xA9pondu (erreur " + std::to_string(e) + ")";
    return ok;
}

void bringToFront(void* nativeWindow) noexcept {
    HWND hwnd = static_cast<HWND>(nativeWindow);
    if (!hwnd || !IsWindow(hwnd)) return;
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    if (!SetForegroundWindow(hwnd)) {
        // Windows l'a refuse (la deuxieme n'avait pas le droit de le donner) : le
        // bouton de la barre des taches clignote, on voit ou est la fenetre.
        FLASHWINFO flash{};
        flash.cbSize = sizeof flash;
        flash.hwnd = hwnd;
        flash.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
        flash.uCount = 3;
        (void)FlashWindowEx(&flash);
    }
}

void alert(const std::string& utf8Title, const std::string& utf8Text) noexcept {
    try {
        (void)MessageBoxW(nullptr, wide(utf8Text).c_str(), wide(utf8Title).c_str(),
                          MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
    } catch (...) {
    }
}

#else
// =================================================================== Linux ==
namespace {

bool recvAll(int fd, char* p, std::size_t n) {
    while (n > 0) {
        const ssize_t k = ::recv(fd, p, n, 0);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        p += k;
        n -= static_cast<std::size_t>(k);
    }
    return true;
}

bool sendAll(int fd, const char* p, std::size_t n) {
    while (n > 0) {
        const ssize_t k = ::send(fd, p, n, MSG_NOSIGNAL);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        p += k;
        n -= static_cast<std::size_t>(k);
    }
    return true;
}

bool readMessage(int fd, std::string& out) {
    char head[12];
    if (!recvAll(fd, head, sizeof head)) return false;
    if (std::memcmp(head, kMagic, sizeof kMagic) != 0) return false;
    const std::uint32_t n = get32(head + 8);
    if (n > kMaxPayload) return false;
    out.assign(head, sizeof head);
    out.resize(sizeof head + n);
    return n == 0 || recvAll(fd, out.data() + sizeof head, n);
}

void timeouts(int fd, int seconds) {
    timeval tv{};
    tv.tv_sec = seconds;
    (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

bool fillAddress(const std::string& path, sockaddr_un& address) {
    address = sockaddr_un{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof address.sun_path) return false;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return true;
}

// Le fil d'ecoute : chaque demande d'une prise du meme utilisateur, "OK", et on la range.
void serve(Guard::Impl& d) {
    for (;;) {
        pollfd p[2] = {{d.listenFd, POLLIN, 0}, {d.stopPipe[0], POLLIN, 0}};
        const int r = ::poll(p, 2, -1);
        if (r < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (p[1].revents != 0) return;
        if ((p[0].revents & POLLIN) == 0) continue;
        const int c = ::accept4(d.listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (c < 0) continue;
        timeouts(c, 5);
        ucred     peer{};
        socklen_t size = sizeof peer;
        const bool sameUser = ::getsockopt(c, SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 && peer.uid == ::geteuid();
        std::string              bytes;
        std::vector<std::string> args;
        const bool ok = sameUser && readMessage(c, bytes) && decode(bytes, args);
        if (ok) (void)sendAll(c, kReply, sizeof kReply);
        ::close(c);
        if (ok) d.deliver(std::move(args));
    }
}

std::string lowered(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

} // namespace

std::unique_ptr<Guard> Guard::acquire(const Options& options) {
    namespace fs = std::filesystem;
    std::unique_ptr<Guard> g(new Guard());
    g->impl_ = std::make_unique<Impl>();
    Impl& d = *g->impl_;
    d.options = options;
    std::string folder = options.folder;
    bool        hidden = false;
    if (folder.empty()) {
        std::error_code ec;
        const char*     runtime = std::getenv("XDG_RUNTIME_DIR");
        const char*     home = std::getenv("HOME");
        if (runtime && *runtime && fs::is_directory(runtime, ec)) folder = runtime;
        else if (home && *home) {
            folder = home;
            hidden = true;   // dans HOME, un fichier cache
        } else folder = "/tmp";
    }
    const std::string stem = folder + "/" + (hidden ? "." : "") + lowered(options.name) + "-instance";
    const std::string lockPath = stem + ".lock";
    d.socketPath = stem + ".sock";
    const int fd = ::open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        d.role = Role::Unchecked;
        d.why = lockPath + " : " + std::strerror(errno);
    } else if (::flock(fd, LOCK_EX | LOCK_NB) == 0) {
        d.lockFd = fd;
        d.role = Role::Primary;
    } else {
        const int e = errno;
        ::close(fd);
        if (e == EWOULDBLOCK) d.role = Role::Secondary;
        else {
            d.role = Role::Unchecked;
            d.why = lockPath + " : " + std::strerror(e);
        }
    }
    return g;
}

Guard::~Guard() {
    if (!impl_) return;
    Impl& d = *impl_;
    if (d.stopPipe[1] >= 0) {
        const char b = 1;
        while (::write(d.stopPipe[1], &b, 1) < 0 && errno == EINTR) {
        }
    }
    if (d.listener.joinable()) d.listener.join();
    for (int& f : d.stopPipe)
        if (f >= 0) {
            ::close(f);
            f = -1;
        }
    if (d.listenFd >= 0) {
        ::close(d.listenFd);
        ::unlink(d.socketPath.c_str());   // avant de rendre le verrou : la suivante cree la sienne
    }
    if (d.lockFd >= 0) {
        (void)::flock(d.lockFd, LOCK_UN);
        ::close(d.lockFd);
    }
    if (g_current == this) g_current = nullptr;
}

bool Guard::listen(std::function<void()> wake) {
    Impl& d = *impl_;
    if (d.role != Role::Primary) return false;
    setWake(std::move(wake));
    if (d.listener.joinable()) return true;
    sockaddr_un address{};
    if (!fillAddress(d.socketPath, address)) {
        d.why = d.socketPath + " : chemin trop long pour une prise";
        return false;
    }
    // Une prise restee d'une instance arretee brutalement : le verrou est a nous, elle n'est a personne.
    ::unlink(d.socketPath.c_str());
    const int s = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0) {
        d.why = std::string("socket : ") + std::strerror(errno);
        return false;
    }
    if (::bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0 || ::listen(s, 8) != 0) {
        d.why = d.socketPath + " : " + std::strerror(errno);
        ::close(s);
        return false;
    }
    (void)::chmod(d.socketPath.c_str(), 0600);
    if (::pipe2(d.stopPipe, O_CLOEXEC) != 0) {
        d.why = std::string("pipe : ") + std::strerror(errno);
        ::close(s);
        ::unlink(d.socketPath.c_str());
        return false;
    }
    d.listenFd = s;
    d.listener = std::thread([&d] { serve(d); });
    return true;
}

bool Guard::forward(const std::vector<std::string>& args, std::string* why) {
    Impl&             d = *impl_;
    const std::string message = encode(args);
    sockaddr_un       address{};
    if (!fillAddress(d.socketPath, address)) {
        if (why) *why = d.socketPath + " : chemin trop long pour une prise";
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(d.options.connectMs);
    for (;;) {
        const int s = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (s < 0) {
            if (why) *why = std::string("socket : ") + std::strerror(errno);
            return false;
        }
        if (::connect(s, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0) {
            timeouts(s, 5);
            char       reply[2] = {};
            const bool ok = sendAll(s, message.data(), message.size()) && recvAll(s, reply, sizeof reply)
                         && std::memcmp(reply, kReply, sizeof kReply) == 0;
            const int e = errno;
            ::close(s);
            if (!ok && why) *why = std::string("la fen\xC3\xAAtre d\xC3\xA9j\xC3\xA0 ouverte n'a pas r\xC3\xA9pondu (") + std::strerror(e) + ")";
            return ok;
        }
        const int e = errno;
        ::close(s);
        if (std::chrono::steady_clock::now() >= deadline) {
            if (why) *why = std::string("la fen\xC3\xAAtre d\xC3\xA9j\xC3\xA0 ouverte n'\xC3\xA9" "coute pas (") + std::strerror(e) + ")";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));   // la premiere demarre : sa prise n'est pas prete
    }
}

void bringToFront(void*) noexcept {}

void alert(const std::string& utf8Title, const std::string& utf8Text) noexcept {
    std::fprintf(stderr, "%s : %s\n", utf8Title.c_str(), utf8Text.c_str());
}

#endif

// ------------------------------------------------------------ pour tous --
Guard::Role Guard::role() const noexcept { return impl_ ? impl_->role : Role::Unchecked; }

const std::string& Guard::why() const noexcept {
    static const std::string none;
    return impl_ ? impl_->why : none;
}

void Guard::setWake(std::function<void()> wake) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->wake = std::move(wake);
}

std::vector<Request> Guard::take() {
    std::vector<Request> out;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    out.swap(impl_->inbox);
    return out;
}

void   setCurrent(Guard* guard) noexcept { g_current = guard; }
Guard* current() noexcept { return g_current; }

} // namespace core::instance
