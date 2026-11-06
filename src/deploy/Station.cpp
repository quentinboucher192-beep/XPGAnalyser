// =============================================================================
//  deploy/Station.cpp - 1.29.0 : le poste lance par l'agent (voir Station.hpp)
// =============================================================================
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  include <tlhelp32.h>
#else
#  include <fcntl.h>
#  include <signal.h>
#  include <spawn.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <unistd.h>
extern char** environ;
#endif

#include "Station.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>
#include <vector>

namespace deploy::station {

namespace fs = std::filesystem;

namespace {

fs::path P(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

// `child` est-il sous `root` (normalises lexicalement ; sans la casse sous Windows) ?
bool under(const std::string& root, const std::string& child) {
    if (root.empty() || child.empty()) return false;
    std::error_code ec;
    fs::path r = fs::weakly_canonical(P(root), ec);
    if (ec) r = P(root);
    fs::path c = fs::weakly_canonical(P(child), ec);
    if (ec) c = P(child);
    std::string rs = U(r.lexically_normal().make_preferred()), cs = U(c.lexically_normal().make_preferred());
#if defined(_WIN32)
    const auto lower = [](std::string& s) {
        for (char& ch : s)
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    };
    lower(rs);
    lower(cs);
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    while (rs.size() > 1 && rs.back() == sep) rs.pop_back();
    return cs.size() > rs.size() && cs.compare(0, rs.size(), rs) == 0 && cs[rs.size()] == sep;
}

#if defined(_WIN32)
std::wstring W(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string N(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string systemMessage(DWORD code) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                   reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string m = text ? N(text) : std::string();
    if (text) LocalFree(text);
    while (!m.empty() && (m.back() == '\n' || m.back() == '\r' || m.back() == ' ' || m.back() == '.')) m.pop_back();
    return m.empty() ? "erreur " + std::to_string(code) : m;
}

// Un argument de ligne de commande Windows (les regles de CommandLineToArgvW).
std::wstring quoteArg(const std::wstring& a) {
    std::wstring out = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t c : a) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    out += L'"';
    return out;
}

std::int64_t fileTime(const FILETIME& f) { return (static_cast<std::int64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

std::int64_t createdOf(HANDLE h) {
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(h, &c, &e, &k, &u)) return 0;
    return fileTime(c);
}

std::wstring lowerW(std::wstring s) {
    for (wchar_t& c : s)
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    std::replace(s.begin(), s.end(), L'/', L'\\');
    return s;
}

// La ligne de commande d'un autre processus (Windows 8.1 et plus : ProcessCommandLineInformation).
bool commandLineOf(HANDLE h, std::wstring& out) {
    using Fn = LONG(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static const Fn query = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    if (!query) return false;
    struct Ustr {
        USHORT Length, MaximumLength;
        PWSTR Buffer;
    };
    std::vector<unsigned char> buf(8192);
    ULONG got = 0;
    LONG st = query(h, 60, buf.data(), static_cast<ULONG>(buf.size()), &got);
    if (st != 0 && got > buf.size() && got < 1u << 20) {
        buf.resize(got);
        st = query(h, 60, buf.data(), static_cast<ULONG>(buf.size()), &got);
    }
    if (st == 0) {
        const auto* u = reinterpret_cast<const Ustr*>(buf.data());
        if (!u->Buffer) return false;
        out.assign(u->Buffer, u->Length / sizeof(wchar_t));
        return true;
    }
    // Avant Windows 8.1 (et sous Wine 9) : la ligne lue dans le PEB du processus (meme architecture,
    // 64 bits : ProcessParameters a +0x20 du PEB, CommandLine a +0x70 des parametres).
#if defined(_WIN64)
    struct Basic {
        PVOID reserved1;
        PVOID peb;
        PVOID reserved2[2];
        ULONG_PTR pid;
        PVOID reserved3;
    } pbi{};
    if (query(h, 0, &pbi, sizeof pbi, &got) != 0 || !pbi.peb) return false;
    PVOID params = nullptr;
    SIZE_T n = 0;
    if (!ReadProcessMemory(h, static_cast<char*>(pbi.peb) + 0x20, &params, sizeof params, &n) || !params) return false;
    Ustr cl{};
    if (!ReadProcessMemory(h, static_cast<char*>(params) + 0x70, &cl, sizeof cl, &n) || !cl.Buffer || cl.Length > 65534) return false;
    std::wstring text(cl.Length / sizeof(wchar_t), L'\0');
    if (!ReadProcessMemory(h, cl.Buffer, text.data(), cl.Length, &n)) return false;
    out = std::move(text);
    return true;
#else
    return false;
#endif
}

const wchar_t* const kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#else
// Le champ 22 de /proc/<pid>/stat (l'heure de demarrage), +1 ; 0 : absent ou zombie.
std::int64_t procStart(int pid) {
    std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(in, line)) return 0;
    const auto close = line.rfind(')');
    if (close == std::string::npos) return 0;
    std::istringstream rest(line.substr(close + 1));
    std::string field;
    std::int64_t start = -1;
    for (int i = 3; i <= 22 && rest >> field; ++i) {
        if (i == 3 && (field == "Z" || field == "X")) return 0;
        if (i == 22) start = std::atoll(field.c_str());
    }
    return start < 0 ? 0 : start + 1;
}

fs::path autostartFolder() {
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return fs::path(x) / "autostart";
    if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h) / ".config" / "autostart";
    return {};
}

std::string shQuoted(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '$' || c == '`' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

// Un argument de la ligne Exec d'un .desktop (comme app/hmi/HmiStationLaunch).
std::string desktopArg(const std::string& s) {
    std::string out;
    for (const char c : shQuoted(s)) {
        if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

bool writeText(const fs::path& file, const std::string& text, std::string* why) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return fail(why, "\xC3\xA9" "criture impossible : " + file.string());
        out << text;
        if (!out) return fail(why, "\xC3\xA9" "criture impossible : " + file.string());
    }
    fs::rename(tmp, file, ec);
    if (ec) return fail(why, "impossible de remplacer " + file.string() + " (" + ec.message() + ")");
    return true;
}
#endif

} // namespace

// ================================================================ Process ===
struct Process::Impl {
    int pid{0};
    std::int64_t created{0};
    bool alive{false};
    int code{-1};
#if defined(_WIN32)
    HANDLE handle{nullptr};
    ~Impl() {
        if (handle) CloseHandle(handle);
    }
#endif
};

Process::Process() : d_(std::make_unique<Impl>()) {}
Process::~Process() = default;
Process::Process(Process&&) noexcept = default;
Process& Process::operator=(Process&&) noexcept = default;

bool Process::valid() const noexcept { return d_ && d_->pid > 0; }
int Process::pid() const noexcept { return d_ ? d_->pid : 0; }
std::int64_t Process::created() const noexcept { return d_ ? d_->created : 0; }
int Process::exitCode() const noexcept { return d_ ? d_->code : -1; }

void Process::release() { d_ = std::make_unique<Impl>(); }

bool Process::launch(const std::string& program, const std::vector<std::string>& args,
                     const std::vector<std::pair<std::string, std::string>>& env, const std::string& workDir,
                     const std::string& outputFile, std::string* why) {
    release();
#if defined(_WIN32)
    (void)outputFile;   // le poste est une application fenetree : sa sortie ne va nulle part
    std::wstring cmd = quoteArg(W(program));
    for (const auto& a : args) cmd += L" " + quoteArg(W(a));
    // L'environnement : celui de l'agent, sans les noms redefinis, plus les notres ; trie (sans la casse).
    std::vector<std::wstring> vars;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
            const std::wstring entry(p);
            const auto eq = entry.find(L'=', 1);
            const std::wstring name = lowerW(entry.substr(0, eq));
            bool redefined = false;
            for (const auto& [k, v] : env) redefined = redefined || lowerW(W(k)) == name;
            if (!redefined) vars.push_back(entry);
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& [k, v] : env) vars.push_back(W(k) + L"=" + W(v));
    std::sort(vars.begin(), vars.end(), [](const std::wstring& a, const std::wstring& b) {
        return lowerW(a.substr(0, a.find(L'=', 1))) < lowerW(b.substr(0, b.find(L'=', 1)));
    });
    std::wstring block;
    for (const auto& v : vars) {
        block += v;
        block += L'\0';
    }
    block += L'\0';
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmd;
    const std::wstring dir = W(workDir);
    // Sans heritage de handles (ni la socket d'ecoute) ; son propre groupe (le Ctrl+C de l'agent ne l'atteint pas).
    if (!CreateProcessW(W(program).c_str(), mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, block.data(),
                        dir.empty() ? nullptr : dir.c_str(), &si, &pi))
        return fail(why, "lancement impossible de " + program + " : " + systemMessage(GetLastError()));
    CloseHandle(pi.hThread);
    d_->handle = pi.hProcess;
    d_->pid = static_cast<int>(pi.dwProcessId);
    d_->created = createdOf(pi.hProcess);
    d_->alive = true;
    return true;
#else
    std::vector<std::string> argvS;
    argvS.push_back(program);
    argvS.insert(argvS.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : argvS) argv.push_back(s.data());
    argv.push_back(nullptr);
    std::vector<std::string> envS;
    for (char** e = environ; e && *e; ++e) {
        const std::string entry(*e);
        const std::string name = entry.substr(0, entry.find('='));
        bool redefined = false;
        for (const auto& [k, v] : env) redefined = redefined || k == name;
        if (!redefined) envS.push_back(entry);
    }
    for (const auto& [k, v] : env) envS.push_back(k + "=" + v);
    std::vector<char*> envp;
    for (auto& s : envS) envp.push_back(s.data());
    envp.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, outputFile.empty() ? "/dev/null" : outputFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
#  if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    posix_spawn_file_actions_addclosefrom_np(&fa, 3);   // aucun autre descripteur (ni la socket d'ecoute)
#  endif
#  if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29))
    if (!workDir.empty()) posix_spawn_file_actions_addchdir_np(&fa, workDir.c_str());
#  endif
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t none, defaults;
    sigemptyset(&none);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGHUP);
    posix_spawnattr_setsigmask(&at, &none);
    posix_spawnattr_setsigdefault(&at, &defaults);
    // Sa propre session : un Ctrl+C dans la console de l'agent ne l'arrete pas.
    posix_spawnattr_setflags(&at, static_cast<short>(POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF));
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, program.c_str(), &fa, &at, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    if (rc != 0) return fail(why, "lancement impossible de " + program + " : " + std::strerror(rc));
    d_->pid = static_cast<int>(pid);
    d_->created = procStart(d_->pid);
    d_->alive = true;
    return true;
#endif
}

bool Process::attach(int pid, std::int64_t created) {
    release();
    if (pid <= 0 || created == 0) return false;
#if defined(_WIN32)
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    if (WaitForSingleObject(h, 0) != WAIT_TIMEOUT || createdOf(h) != created) {
        CloseHandle(h);
        return false;
    }
    d_->handle = h;
#else
    if (procStart(pid) != created) return false;
#endif
    d_->pid = pid;
    d_->created = created;
    d_->alive = true;
    return true;
}

bool Process::running() {
    if (!valid() || !d_->alive) return false;
#if defined(_WIN32)
    if (WaitForSingleObject(d_->handle, 0) == WAIT_TIMEOUT) return true;
    DWORD code = 0;
    if (GetExitCodeProcess(d_->handle, &code)) d_->code = static_cast<int>(code);
    d_->alive = false;
    return false;
#else
    // Notre fils (lance ici, ou retrouve par attach dans le meme processus) : waitpid le recolte.
    int st = 0;
    const pid_t r = ::waitpid(d_->pid, &st, WNOHANG);
    if (r == 0 && procStart(d_->pid) == d_->created) return true;
    if (r == d_->pid) {
        d_->code = WIFEXITED(st) ? WEXITSTATUS(st) : (WIFSIGNALED(st) ? 128 + WTERMSIG(st) : -1);
        d_->alive = false;
        return false;
    }
    // ECHILD : pas notre fils (l'agent a redemarre) ; son heure de creation dit s'il tourne.
    if (procStart(d_->pid) == d_->created) return true;
    d_->alive = false;
    return false;
#endif
}

bool Process::waitExit(int ms, const std::atomic<bool>* cancel) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, ms));
    for (;;) {
        if (!running()) return true;
        if (cancel && cancel->load()) return false;
        if (std::chrono::steady_clock::now() >= end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool Process::terminate(std::string* why) {
    if (!running()) return true;
#if defined(_WIN32)
    if (!TerminateProcess(d_->handle, 1)) return fail(why, "TerminateProcess : " + systemMessage(GetLastError()));
    if (!waitExit(5000)) return fail(why, "le poste ne s'arr\xC3\xAAte pas apr\xC3\xA8s TerminateProcess");
    return true;
#else
    ::kill(d_->pid, SIGTERM);
    if (waitExit(3000)) return true;
    ::kill(d_->pid, SIGKILL);
    if (waitExit(3000)) return true;
    return fail(why, "le poste ne s'arr\xC3\xAAte pas, m\xC3\xAAme apr\xC3\xA8s SIGKILL");
#endif
}

std::int64_t processCreated(int pid) {
    if (pid <= 0) return 0;
#if defined(_WIN32)
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return 0;
    DWORD code = 0;
    std::int64_t c = 0;
    if (GetExitCodeProcess(h, &code) && code == STILL_ACTIVE) c = createdOf(h);
    CloseHandle(h);
    return c;
#else
    return procStart(pid);
#endif
}

std::vector<Found> findStations(const std::string& program, const std::string& underDir) {
    std::vector<Found> out;
    if (program.empty() || underDir.empty()) return out;
#if defined(_WIN32)
    const std::wstring want = lowerW(W(program));
    const std::wstring wantName = lowerW(P(program).filename().wstring());
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    for (BOOL more = Process32FirstW(snap, &pe); more; more = Process32NextW(snap, &pe)) {
        if (lowerW(pe.szExeFile) != wantName || pe.th32ProcessID == GetCurrentProcessId()) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
        if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) continue;
        wchar_t image[MAX_PATH * 4];
        DWORD len = static_cast<DWORD>(std::size(image));
        std::wstring cmd;
        const bool same = QueryFullProcessImageNameW(h, 0, image, &len) && lowerW(std::wstring(image, len)) == want;
        if (same && commandLineOf(h, cmd)) {
            int n = 0;
            LPWSTR* argv = CommandLineToArgvW(cmd.c_str(), &n);
            for (int i = 1; argv && i + 1 < n; ++i) {
                if (std::wstring(argv[i]) != L"--ihm") continue;
                const std::string folder = N(argv[i + 1]);
                if (under(underDir, folder)) out.push_back({static_cast<int>(pe.th32ProcessID), createdOf(h), folder});
                break;
            }
            if (argv) LocalFree(argv);
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
#else
    std::error_code ec;
    const fs::path want = fs::weakly_canonical(P(program), ec);
    for (fs::directory_iterator it("/proc", ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        const int pid = std::atoi(name.c_str());
        if (pid == ::getpid()) continue;
        std::error_code e2;
        const fs::path exe = fs::read_symlink(it->path() / "exe", e2);
        if (e2 || exe != want) continue;
        std::ifstream in(it->path() / "cmdline", std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::vector<std::string> argv;
        for (std::size_t pos = 0; pos < raw.size();) {
            const auto z = raw.find('\0', pos);
            argv.push_back(raw.substr(pos, z == std::string::npos ? std::string::npos : z - pos));
            if (z == std::string::npos) break;
            pos = z + 1;
        }
        for (std::size_t i = 1; i + 1 < argv.size(); ++i) {
            if (argv[i] != "--ihm") continue;
            if (under(underDir, argv[i + 1])) out.push_back({pid, procStart(pid), argv[i + 1]});
            break;
        }
    }
#endif
    return out;
}

std::string currentExecutable() {
#if defined(_WIN32)
    std::vector<wchar_t> buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    return n > 0 && n < buf.size() ? N(std::wstring(buf.data(), n)) : std::string{};
#else
    std::error_code ec;
    const auto p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string{} : U(p);
#endif
}

// ================================================ demarrer avec la session ===
std::string stationCommand(const std::string& program, const std::string& folder) {
#if defined(_WIN32)
    return "\"" + program + "\" --ihm \"" + folder + "\"";
#else
    return shQuoted(program) + " --ihm " + shQuoted(folder);
#endif
}

std::string folderOfCommand(const std::string& command) {
    const auto at = command.find("--ihm");
    if (at == std::string::npos) return {};
    std::string rest = command.substr(at + 5);
    while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
    while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\r' || rest.back() == '\n')) rest.pop_back();
    if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') rest = rest.substr(1, rest.size() - 2);
#if defined(_WIN32)
    return rest;
#else
    // La ligne Exec d'un .desktop : les \\ doubles, puis les echappements de sh.
    std::string once;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '\\' && i + 1 < rest.size() && rest[i + 1] == '\\') {
            once += '\\';
            ++i;
        } else {
            once += rest[i];
        }
    }
    std::string out;
    for (std::size_t i = 0; i < once.size(); ++i) {
        if (once[i] == '\\' && i + 1 < once.size()) out += once[++i];
        else out += once[i];
    }
    return out;
#endif
}

#if defined(_WIN32)
namespace {
// Une valeur de la cle Run de `root` (dans la vue `view` du registre) ; faux si absente.
bool readRunView(HKEY root, REGSAM view, const char* valueName, std::string& command) {
    const std::wstring name = W(valueName);
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, kRunKey, 0, KEY_QUERY_VALUE | view, &key) != ERROR_SUCCESS) return false;
    bool found = false;
    DWORD type = 0, bytes = 0;
    if (RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        std::wstring value(bytes / sizeof(wchar_t) + 1, L'\0');
        DWORD size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        if (RegQueryValueExW(key, name.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &size) == ERROR_SUCCESS) {
            value.resize(size / sizeof(wchar_t));
            while (!value.empty() && value.back() == L'\0') value.pop_back();
            command = N(value);
            found = true;
        }
    }
    RegCloseKey(key);
    return found;
}

// HKLM : Windows lance au login les deux vues (Run et Wow6432Node\...\Run) ; l'installateur
// 64 bits ecrit dans la premiere, un installateur 32 bits dans la seconde.
bool readRun(HKEY root, const char* valueName, std::string& command) {
    if (root != HKEY_LOCAL_MACHINE) return readRunView(root, 0, valueName, command);
    return readRunView(root, KEY_WOW64_64KEY, valueName, command) || readRunView(root, KEY_WOW64_32KEY, valueName, command);
}
} // namespace
#endif

Autostart readAutostart(bool agent) {
    Autostart a;
#if defined(_WIN32)
    const char* name = agent ? kAgentAutostartName : kStationAutostartName;
    if (readRun(HKEY_CURRENT_USER, name, a.command)) {
        a.present = true;
        a.where = std::string("HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run \xE2\x80\x94 ") + name;
    } else if (agent && readRun(HKEY_LOCAL_MACHINE, name, a.command)) {
        a.present = true;
        a.machine = true;
        a.where = std::string("HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Run \xE2\x80\x94 ") + name;
    }
#else
    const fs::path folder = autostartFolder();
    if (folder.empty()) return a;
    const fs::path file = folder / (agent ? kAgentDesktopFile : kStationDesktopFile);
    std::error_code ec;
    if (!fs::exists(file, ec)) return a;
    a.present = true;
    a.where = file.string();
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line))
        if (line.rfind("Exec=", 0) == 0) a.command = line.substr(5);
#endif
    return a;
}

namespace {

// Ecrire (value non vide) ou effacer une entree.
bool writeEntry(bool agent, const std::string& value, const std::string& desktop, std::string* where, std::string* why) {
#if defined(_WIN32)
    (void)desktop;
    const std::wstring name = W(agent ? kAgentAutostartName : kStationAutostartName);
    HKEY key = nullptr;
    if (const LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr); rc != ERROR_SUCCESS)
        return fail(why, "cl\xC3\xA9 Run inaccessible : " + systemMessage(static_cast<DWORD>(rc)));
    LONG rc = ERROR_SUCCESS;
    if (!value.empty()) {
        const std::wstring v = W(value);
        rc = RegSetValueExW(key, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, name.c_str());
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) return fail(why, systemMessage(static_cast<DWORD>(rc)));
    if (where) *where = std::string("HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run \xE2\x80\x94 ") + (agent ? kAgentAutostartName : kStationAutostartName);
    return true;
#else
    (void)value;
    const fs::path folder = autostartFolder();
    if (folder.empty()) return fail(why, "dossier de d\xC3\xA9marrage introuvable (ni XDG_CONFIG_HOME ni HOME)");
    const fs::path file = folder / (agent ? kAgentDesktopFile : kStationDesktopFile);
    if (where) *where = file.string();
    std::error_code ec;
    if (desktop.empty()) {
        fs::remove(file, ec);
        if (ec) return fail(why, "suppression impossible : " + file.string() + " (" + ec.message() + ")");
        return true;
    }
    return writeText(file, desktop, why);
#endif
}

} // namespace

bool setStationAutostart(bool on, const std::string& program, const std::string& folder, const std::string& ownedUnder, std::string* where,
                         std::string* why) {
    if (!on) {
        const Autostart cur = readAutostart(false);
        if (!cur.present) return true;
        const std::string f = folderOfCommand(cur.command);
        if (!under(ownedUnder, f)) {   // l'entree d'un autre projet reste
            if (where) *where = cur.where;
            return true;
        }
        return writeEntry(false, {}, {}, where, why);
    }
    if (program.empty() || folder.empty()) return fail(why, "le programme ou le dossier du poste manque");
    std::string desktop;
#if !defined(_WIN32)
    std::ostringstream o;
    o << "[Desktop Entry]\n"
      << "Type=Application\n"
      << "Name=" << kStationAutostartName << "\n"
      << "Comment=Poste d'exploitation : l'IHM seule, en plein ecran (IHM Studio) - un projet par PC\n"
      << "Exec=" << desktopArg(program) << " --ihm " << desktopArg(folder) << "\n"
      << "Terminal=false\n"
      << "X-GNOME-Autostart-enabled=true\n";
    desktop = o.str();
#endif
    return writeEntry(false, stationCommand(program, folder), desktop, where, why);
}

bool setAgentAutostart(bool on, const std::string& agentExe, std::string* where, std::string* why, std::string* note) {
#if defined(_WIN32)
    std::string machineCommand;
    const bool machine = readRun(HKEY_LOCAL_MACHINE, kAgentAutostartName, machineCommand);
    if (machine) {
        // L'installateur l'a posee pour tous les comptes : jamais une seconde dans HKCU.
        if (!writeEntry(true, {}, {}, where, why)) return false;
        if (where) *where = std::string("HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Run \xE2\x80\x94 ") + kAgentAutostartName;
        if (note)
            *note = on ? "l'installateur a d\xC3\xA9j\xC3\xA0 pos\xC3\xA9 le d\xC3\xA9marrage de l'agent pour tous les comptes (HKLM) : rien \xC3\xA0 ajouter"
                       : "la valeur pos\xC3\xA9" "e par l'installateur pour tous les comptes (HKLM) reste : seul l'installateur (ou un administrateur) peut la retirer";
        return true;
    }
#endif
    if (note) note->clear();
    if (!on) return writeEntry(true, {}, {}, where, why);
    if (agentExe.empty()) return fail(why, "le chemin de l'agent est inconnu");
    std::string desktop;
#if !defined(_WIN32)
    std::ostringstream o;
    o << "[Desktop Entry]\n"
      << "Type=Application\n"
      << "Name=" << kAgentAutostartName << "\n"
      << "Comment=L'agent de d\xC3\xA9ploiement du PC de maintenance (sans fen\xC3\xAAtre)\n"
      << "Exec=" << desktopArg(agentExe) << " --servir\n"
      << "Terminal=false\n"
      << "X-GNOME-Autostart-enabled=true\n";
    desktop = o.str();
    return writeEntry(true, agentExe + " --servir", desktop, where, why);
#else
    return writeEntry(true, "\"" + agentExe + "\" --servir", desktop, where, why);
#endif
}

} // namespace deploy::station
