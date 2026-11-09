// =============================================================================
//  license_admin/ServiceDeploy.cpp - 1.45.0 : voir ServiceDeploy.hpp
// =============================================================================
#include "ServiceDeploy.hpp"

#include "../licensing/Admins.hpp"   // parseSemver, compareSemver

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cwchar>
#include <cwctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <cerrno>
#include <climits>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace license_admin::deployer {

namespace fs = std::filesystem;

namespace {

// ============================================================================================ les petits outils
fs::path P(const std::string& u8) { return fs::path(std::u8string(u8.begin(), u8.end())); }
std::string U(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
bool isFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}
double steady() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string readSmall(const fs::path& p, std::size_t max) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::string s;
    s.resize(max);
    f.read(s.data(), static_cast<std::streamsize>(max));
    s.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, f.gcount())));
    return s;
}

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool contains(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

// Les essais (setTestHooks), lus et ecrits sous verrou.
std::mutex g_hooksMutex;
TestHooks  g_hooks;
TestHooks hooks() {
    std::lock_guard<std::mutex> lk(g_hooksMutex);
    return g_hooks;
}

// Un seul deploiement (ou connexion) a la fois ; stopAll l'arrete.
std::atomic<bool> g_running{false};
std::atomic<bool> g_stop{false};
struct RunGuard {
    bool ok{false};
    RunGuard() {
        bool expected = false;
        ok = g_running.compare_exchange_strong(expected, true);
        if (ok) g_stop.store(false);
    }
    ~RunGuard() {
        if (ok) g_running.store(false);
    }
};

#if defined(_WIN32)
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring envW(const wchar_t* name) {
    const DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return {};
    std::wstring v(n, L'\0');
    const DWORD got = GetEnvironmentVariableW(name, v.data(), n);
    v.resize(got < n ? got : 0);
    return v;
}
// Un argument de ligne de commande Windows, entre guillemets (les regles de CommandLineToArgvW).
std::wstring quote(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : a) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') out.append(backslashes * 2 + 1, L'\\');
        else out.append(backslashes, L'\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}
// Le PATH du registre (machine ou compte) : ce qu'un terminal ouvert maintenant verrait (Node.js installe apres le
// lancement de l'Admin y est deja).
std::wstring registryPath(HKEY root, const wchar_t* sub) {
    DWORD type = 0, size = 0;
    const DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND;
    if (RegGetValueW(root, sub, L"Path", flags, &type, nullptr, &size) != ERROR_SUCCESS || size < sizeof(wchar_t)) return {};
    std::wstring v(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, sub, L"Path", flags, &type, v.data(), &size) != ERROR_SUCCESS) return {};
    v.resize(wcsnlen(v.c_str(), v.size()));
    if (type == REG_EXPAND_SZ && !v.empty()) {
        const DWORD n = ExpandEnvironmentStringsW(v.c_str(), nullptr, 0);
        if (n > 0) {
            std::wstring e(n, L'\0');
            const DWORD got = ExpandEnvironmentStringsW(v.c_str(), e.data(), n);
            if (got > 0 && got <= n) {
                e.resize(wcsnlen(e.c_str(), e.size()));
                v = e;
            }
        }
    }
    return v;
}
#endif

std::string programDir() {
#if defined(_WIN32)
    std::wstring self(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    if (n == 0 || n >= self.size()) return {};
    self.resize(n);
    return U(fs::path(self).parent_path());
#elif defined(__linux__)
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return {};
    return U(P(std::string(buf, static_cast<std::size_t>(n))).parent_path());
#else
    return {};
#endif
}

#if defined(_WIN32)
constexpr char kPathSep = ';';
#else
constexpr char kPathSep = ':';
#endif

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == kPathSep) {
            std::string d = cur;
            if (d.size() >= 2 && d.front() == '"' && d.back() == '"') d = d.substr(1, d.size() - 2);
            if (!d.empty()) out.push_back(d);
            cur.clear();
        } else {
            cur += path[i];
        }
    }
    return out;
}

bool runnable(const fs::path& p) {
    if (!isFile(p)) return false;
#if defined(_WIN32)
    return true;
#else
    return ::access(p.c_str(), X_OK) == 0;
#endif
}

// ============================================================================================ la ligne
// Une sequence ESC (couleurs, deplacements) : sa longueur a partir de s[i] (ESC).
std::size_t escapeLength(const std::string& s, std::size_t i) {
    if (i + 1 >= s.size()) return 1;
    const char k = s[i + 1];
    if (k == '[') {   // CSI : parametres, puis une lettre finale (0x40 a 0x7E)
        std::size_t j = i + 2;
        while (j < s.size() && !(static_cast<unsigned char>(s[j]) >= 0x40 && static_cast<unsigned char>(s[j]) <= 0x7E)) ++j;
        return std::min(s.size(), j + 1) - i;
    }
    if (k == ']') {   // OSC : jusqu'a BEL ou ESC '\'
        std::size_t j = i + 2;
        while (j < s.size() && s[j] != '\x07' && !(s[j] == '\x1B' && j + 1 < s.size() && s[j + 1] == '\\')) ++j;
        if (j < s.size() && s[j] == '\x1B') ++j;
        return std::min(s.size(), j + 1) - i;
    }
    return 2;
}

// De l'UTF-8 valide (un octet faux : '?') ; sans caractere de controle (une tabulation : une espace).
std::string cleanText(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size();) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c == 0x1B) {
            i += escapeLength(raw, i);
            continue;
        }
        if (c < 0x80) {
            if (c == '\t') out += ' ';
            else if (c >= 0x20 && c != 0x7F) out += static_cast<char>(c);
            ++i;
            continue;
        }
        const std::size_t len = c >= 0xF0 ? (c <= 0xF4 ? 4u : 0u) : c >= 0xE0 ? 3u : (c >= 0xC2 && c <= 0xDF) ? 2u : 0u;
        bool good = len > 0 && i + len <= raw.size();
        for (std::size_t k = 1; good && k < len; ++k) good = (static_cast<unsigned char>(raw[i + k]) & 0xC0) == 0x80;
        if (good) {
            out.append(raw, i, len);
            i += len;
        } else {
            out += '?';
            ++i;
        }
    }
    return out;
}

bool nameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'; }

// Un nom qui annonce un secret (en minuscules) : un jeton, un mot de passe, une cle d'API ; devant un '=' seulement (une
// adresse OAuth : ?code=...&state=...), aussi les noms courts (code, state, key) - << [code: 10000] >> reste lisible. Une
// cle PUBLIQUE (PUBLISHER_KEYS) n'en est pas un.
bool sensitiveName(const std::string& lowerName, bool equals) {
    static const char* const parts[] = {"token", "secret", "password", "passwd", "apikey", "api_key", "api-key", "authorization",
                                        "cookie", "jeton", "credential", "session_id", "sessionid"};
    for (const char* p : parts)
        if (lowerName.find(p) != std::string::npos) return true;
    if (!equals) return false;
    static const char* const exact[] = {"code", "state", "code_challenge", "code_verifier", "key", "auth", "sig", "signature"};   // PWD= : le dossier
    for (const char* e : exact)
        if (lowerName == e) return true;
    return false;
}

const char* const kMasked = "[masqu\xC3\xA9]";

// La valeur qui suit `at` (apres = ou :, des espaces, un guillemet) : masquee jusqu'a une espace, & , ; ou le guillemet.
std::size_t maskValue(std::string& s, std::size_t at) {
    std::size_t v = at;
    while (v < s.size() && (s[v] == ' ' || s[v] == '"' || s[v] == '\'')) ++v;
    if (s.compare(v, 7, "Bearer ") == 0) v += 7;
    if (s.compare(v, 6, "Basic ") == 0) v += 6;
    std::size_t e = v;
    while (e < s.size() && s[e] != ' ' && s[e] != '&' && s[e] != '"' && s[e] != '\'' && s[e] != ',' && s[e] != ';' && s[e] != ')') ++e;
    if (e == v) return at;
    s.replace(v, e - v, kMasked);
    return v + std::strlen(kMasked);
}

// Une longue suite opaque (40 caracteres au moins, lettres ET chiffres, pas seulement de l'hexadecimal : une cle publique,
// une empreinte se lisent) : un jeton qui n'a pas dit son nom.
void maskOpaque(std::string& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        if (!(std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '-')) {
            ++i;
            continue;
        }
        std::size_t j = i;
        bool letter = false, digit = false, hexOnly = true;
        while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_' || s[j] == '-')) {
            const char c = s[j];
            if (std::isdigit(static_cast<unsigned char>(c))) digit = true;
            else if (std::isalpha(static_cast<unsigned char>(c))) letter = true;
            if (!std::isxdigit(static_cast<unsigned char>(c))) hexOnly = false;
            ++j;
        }
        if (j - i >= 40 && letter && digit && !hexOnly) {
            s.replace(i, j - i, kMasked);
            i += std::strlen(kMasked);
        } else {
            i = j;
        }
    }
}

std::string maskSecrets(std::string s) {
    // << Bearer xxx >> ou qu'il soit.
    for (std::size_t p = 0; (p = s.find("Bearer ", p)) != std::string::npos;) p = maskValue(s, p + 7) + 1;
    // NOM=valeur, NOM: valeur (et ?nom=valeur&...) : le nom qui le precede dit si c'est un secret.
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c != '=' && c != ':') continue;
        if (c == ':' && i + 1 < s.size() && s[i + 1] == '/') continue;   // https://
        std::size_t b = i;
        while (b > 0 && s[b - 1] == ' ') --b;
        std::size_t a = b;
        while (a > 0 && nameChar(s[a - 1])) --a;
        if (a == b) continue;
        std::string name = lowerAscii(s.substr(a, b - a));
        if (name.size() > 1 && (name.front() == '-')) name.erase(0, name.find_first_not_of('-'));   // --token=...
        if (!sensitiveName(name, c == '=')) continue;
        if (s.compare(i + 1, std::strlen(kMasked), kMasked) == 0) continue;
        i = maskValue(s, i + 1);
    }
    maskOpaque(s);
    return s;
}

// ============================================================================================ lancer un enfant
struct RunResult {
    bool        started{false};
    int         exitCode{-1};
    bool        cancelled{false};
    bool        timedOut{false};
    std::string error;
};

// Les lignes de la sortie : coupees a \n ou \r (\r\n : une seule), nettoyees, gardees (5000 au plus) et donnees a `onLine`.
class LineSplitter {
public:
    LineSplitter(std::vector<std::string>& lines, const LineSink& onLine) : lines_(lines), onLine_(onLine) {}
    void feed(const char* data, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            const char c = data[i];
            if (c == '\n' && afterCr_) {
                afterCr_ = false;
                continue;
            }
            afterCr_ = c == '\r';
            if (c == '\n' || c == '\r') {
                emit();
                continue;
            }
            if (cur_.size() < 64u * 1024u) cur_ += c;
        }
    }
    void finish() {
        if (!cur_.empty()) emit();
    }

private:
    void emit() {
        const std::string line = scrub(cur_);
        cur_.clear();
        lines_.push_back(line);
        if (lines_.size() > 5000) lines_.erase(lines_.begin(), lines_.begin() + 1000);
        if (onLine_) onLine_(line);
    }
    std::vector<std::string>& lines_;
    const LineSink&           onLine_;
    std::string               cur_;
    bool                      afterCr_{false};
};

bool stopAsked(const std::atomic<bool>* cancel) { return (cancel && cancel->load()) || g_stop.load(); }

// Les variables donnees a l'enfant en plus de celles de l'Admin : le PATH ou Node.js a ete trouve, pas de couleurs, pas
// de mesures envoyees par wrangler.
std::vector<std::pair<std::string, std::string>> childOverrides(const std::string& path) {
    return {{"PATH", path}, {"NO_COLOR", "1"}, {"FORCE_COLOR", "0"}, {"WRANGLER_SEND_METRICS", "false"}};
}

#if defined(_WIN32)

bool sameNameW(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (towupper(a[i]) != towupper(b[i])) return false;
    return true;
}

// Le bloc d'environnement de l'enfant : celui de l'Admin, ses variables remplacees ou ajoutees, trie (sans la casse).
std::wstring environmentBlock(const std::vector<std::pair<std::string, std::string>>& overrides) {
    std::vector<std::wstring> entries;
    if (wchar_t* env = GetEnvironmentStringsW()) {
        for (const wchar_t* p = env; *p; p += wcslen(p) + 1) entries.emplace_back(p);
        FreeEnvironmentStringsW(env);
    }
    for (const auto& [k, v] : overrides) {
        const std::wstring key = wide(k);
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const std::wstring& e) {
                                         const std::size_t eq = e.find(L'=', 1);   // "=C:=C:\..." : le nom commence apres le premier '='
                                         return eq != std::wstring::npos && sameNameW(e.substr(0, eq), key);
                                     }),
                      entries.end());
        entries.push_back(key + L"=" + wide(v));
    }
    std::stable_sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
        const std::size_t n = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < n; ++i) {
            const wint_t x = towupper(a[i]), y = towupper(b[i]);
            if (x != y) return x < y;
        }
        return a.size() < b.size();
    });
    std::wstring block;
    for (const auto& e : entries) {
        block += e;
        block += L'\0';
    }
    block += L'\0';
    return block;
}

RunResult runChild(const std::string& program, const std::vector<std::string>& args, const std::string& dir, const std::string& path, bool killTree,
                   double timeout, std::vector<std::string>& lines, const LineSink& onLine, const std::atomic<bool>* cancel) {
    RunResult r;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    HANDLE outR = nullptr, outW = nullptr, inR = nullptr, inW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&inR, &inW, &sa, 0)) {
        r.error = "tubes refus\xC3\xA9s (erreur " + std::to_string(GetLastError()) + ")";
        for (HANDLE h : {outR, outW, inR, inW})
            if (h) CloseHandle(h);
        return r;
    }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    // Le Job : l'enfant et les siens (npm, wrangler) tues ensemble a l'annulation, et a la fermeture du Job.
    HANDLE job = killTree ? CreateJobObjectW(nullptr, nullptr) : nullptr;
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    std::wstring line = quote(wide(program));
    for (const auto& a : args) line += L" " + quote(wide(a));
    STARTUPINFOEXW six{};
    STARTUPINFOW&  si = six.StartupInfo;
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = outW;   // la sortie et l'erreur dans le meme tube : l'ordre est garde
    si.hStdError = outW;
    si.hStdInput = inR;     // fermee tout de suite : jamais de question qui attend
    // Seuls ces deux handles passent a l'enfant (PROC_THREAD_ATTRIBUTE_HANDLE_LIST).
    HANDLE inherit[2] = {outW, inR};
    std::vector<char> attrBuf;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = nullptr;
    {
        SIZE_T size = 0;
        (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        if (size > 0) {
            attrBuf.resize(size);
            attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
            if (!InitializeProcThreadAttributeList(attrs, 1, 0, &size)) {
                attrs = nullptr;
            } else if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, nullptr, nullptr)) {
                DeleteProcThreadAttributeList(attrs);
                attrs = nullptr;
            }
        }
    }
    if (attrs) {
        si.cb = sizeof six;
        six.lpAttributeList = attrs;
    }
    std::wstring env = environmentBlock(childOverrides(path));
    const std::wstring wdir = wide(dir);
    PROCESS_INFORMATION pi{};
    // CREATE_NO_WINDOW : un programme en console, sans fenetre (ses enfants partagent cette console cachee).
    const DWORD flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | (attrs ? EXTENDED_STARTUPINFO_PRESENT : 0);
    const BOOL okStart = CreateProcessW(wide(program).c_str(), line.data(), nullptr, nullptr, TRUE, flags, env.data(), wdir.empty() ? nullptr : wdir.c_str(),
                                        &si, &pi);
    const DWORD startError = okStart ? 0 : GetLastError();
    if (attrs) DeleteProcThreadAttributeList(attrs);
    CloseHandle(outW);
    CloseHandle(inR);
    CloseHandle(inW);
    if (!okStart) {
        r.error = "Windows refuse de le lancer (erreur " + std::to_string(startError) + ") : " + program;
        CloseHandle(outR);
        if (job) CloseHandle(job);
        return r;
    }
    const bool inJob = job && AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    r.started = true;
    LineSplitter split(lines, onLine);
    const double t0 = steady();
    bool exited = false, broken = false;
    double exitedAt = 0.0;
    const auto kill = [&] {
        if (inJob) TerminateJobObject(job, 1);
        else TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 3000);
        exited = true;
        exitedAt = steady();
    };
    for (;;) {
        bool gotData = false;
        if (!broken) {
            DWORD avail = 0;
            if (PeekNamedPipe(outR, nullptr, 0, nullptr, &avail, nullptr)) {
                if (avail > 0) {
                    char buf[8192];
                    DWORD got = 0;
                    if (ReadFile(outR, buf, std::min<DWORD>(avail, sizeof buf), &got, nullptr) && got > 0) {
                        split.feed(buf, got);
                        gotData = true;
                    }
                }
            } else {
                broken = true;   // ERROR_BROKEN_PIPE : plus personne n'ecrit
            }
        }
        if (exited) {
            if (broken || steady() - exitedAt > 2.0) break;   // un petit-enfant garde le tube : on n'attend pas plus
            if (!gotData) Sleep(20);
            continue;
        }
        // Les donnees n'empechent jamais de voir la fin, l'annulation, la limite de temps.
        if (WaitForSingleObject(pi.hProcess, gotData ? 0 : 30) == WAIT_OBJECT_0) {
            exited = true;
            exitedAt = steady();
            continue;
        }
        if (stopAsked(cancel)) {
            r.cancelled = true;
            kill();
        } else if (steady() - t0 > timeout) {
            r.timedOut = true;
            kill();
        }
    }
    split.finish();
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exitCode = r.cancelled || r.timedOut ? -1 : static_cast<int>(code);
    CloseHandle(pi.hProcess);
    CloseHandle(outR);
    if (job) CloseHandle(job);   // KILL_ON_JOB_CLOSE : ce qui restait du deploiement s'en va
    return r;
}

#else

// Ferme tout descripteur >= 3 sauf `keep` (ferme par l'exec) : l'enfant n'herite pas des fichiers de l'Admin.
void closeInherited(int keep) {
#if defined(SYS_close_range)
    bool ok = true;
    if (keep > 3) ok = syscall(SYS_close_range, 3u, static_cast<unsigned>(keep - 1), 0u) == 0;
    if (ok && syscall(SYS_close_range, static_cast<unsigned>(keep + 1), ~0u, 0u) == 0) return;
#endif
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 4096) max = 4096;
    for (int fd = 3; fd < static_cast<int>(max); ++fd)
        if (fd != keep) (void)close(fd);
}

RunResult runChild(const std::string& program, const std::vector<std::string>& args, const std::string& dir, const std::string& path, bool killTree,
                   double timeout, std::vector<std::string>& lines, const LineSink& onLine, const std::atomic<bool>* cancel) {
    RunResult r;
    // Tout ce que l'enfant emploie est pret AVANT fork (apres : seulement des appels surs).
    std::vector<std::string> argStore{program};
    argStore.insert(argStore.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& a : argStore) argv.push_back(a.data());
    argv.push_back(nullptr);
    std::vector<std::string> envStore;
    const auto overrides = childOverrides(path);
    for (char** e = environ; e && *e; ++e) {
        const std::string entry(*e);
        const std::size_t eq = entry.find('=');
        const std::string name = eq == std::string::npos ? entry : entry.substr(0, eq);
        if (std::none_of(overrides.begin(), overrides.end(), [&](const auto& o) { return o.first == name; })) envStore.push_back(entry);
    }
    for (const auto& [k, v] : overrides) envStore.push_back(k + "=" + v);
    std::vector<char*> envp;
    for (auto& e : envStore) envp.push_back(e.data());
    envp.push_back(nullptr);

    int out[2] = {-1, -1}, execFail[2] = {-1, -1};
    if (pipe2(out, O_CLOEXEC) != 0 || pipe2(execFail, O_CLOEXEC) != 0) {
        r.error = "tubes refus\xC3\xA9s";
        for (int fd : {out[0], out[1], execFail[0], execFail[1]})
            if (fd >= 0) close(fd);
        return r;
    }
    const int devnull = open("/dev/null", O_RDONLY | O_CLOEXEC);
    const pid_t pid = fork();
    if (pid < 0) {
        r.error = "fork refus\xC3\xA9";
        for (int fd : {out[0], out[1], execFail[0], execFail[1], devnull})
            if (fd >= 0) close(fd);
        return r;
    }
    if (pid == 0) {
        setpgid(0, 0);   // son groupe : l'annulation l'emporte avec les siens
        if (!dir.empty() && chdir(dir.c_str()) != 0) {
            const int e = errno;
            (void)!write(execFail[1], &e, sizeof e);
            _exit(126);
        }
        if (devnull >= 0) dup2(devnull, 0);
        else close(0);
        dup2(out[1], 1);
        dup2(out[1], 2);
        closeInherited(execFail[1]);
        execve(program.c_str(), argv.data(), envp.data());
        const int e = errno;
        (void)!write(execFail[1], &e, sizeof e);
        _exit(127);
    }
    setpgid(pid, pid);
    close(out[1]);
    if (devnull >= 0) close(devnull);
    close(execFail[1]);
    {
        int e = 0;
        if (read(execFail[0], &e, sizeof e) == static_cast<ssize_t>(sizeof e)) {
            close(execFail[0]);
            close(out[0]);
            int st = 0;
            (void)waitpid(pid, &st, 0);
            r.error = std::string("il ne se lance pas (") + std::strerror(e) + ") : " + program;
            return r;
        }
        close(execFail[0]);
    }
    r.started = true;
    LineSplitter split(lines, onLine);
    const double t0 = steady();
    bool exited = false, eof = false;
    double exitedAt = 0.0, termAt = -1.0;
    int status = 0;
    const auto signalChild = [&](int sig) {
        if (killTree) (void)::kill(-pid, sig);
        (void)::kill(pid, sig);
    };
    for (;;) {
        // Les donnees n'empechent jamais de voir la fin, l'annulation, la limite de temps (pas de continue).
        if (!eof) {
            pollfd p{out[0], POLLIN, 0};
            const int n = poll(&p, 1, 30);
            if (n > 0) {
                char buf[8192];
                const ssize_t got = read(out[0], buf, sizeof buf);
                if (got > 0) split.feed(buf, static_cast<std::size_t>(got));
                else if (got == 0 || (errno != EINTR && errno != EAGAIN)) eof = true;
            }
        } else if (!exited) {
            usleep(30 * 1000);
        }
        if (!exited) {
            const pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) {
                exited = true;
                exitedAt = steady();
            } else if (termAt < 0 && stopAsked(cancel)) {
                r.cancelled = true;
                signalChild(SIGTERM);
                termAt = steady();
            } else if (termAt < 0 && steady() - t0 > timeout) {
                r.timedOut = true;
                signalChild(SIGTERM);
                termAt = steady();
            } else if (termAt >= 0 && steady() - termAt > 2.0) {
                signalChild(SIGKILL);
                termAt = steady() + 3600.0;   // une fois
            }
        }
        if (exited) {
            if (killTree && (r.cancelled || r.timedOut)) (void)::kill(-pid, SIGKILL);   // les petits-enfants aussi
            if (eof || steady() - exitedAt > 2.0) break;
        }
    }
    split.finish();
    close(out[0]);
    if (r.cancelled || r.timedOut) r.exitCode = -1;
    else if (WIFEXITED(status)) r.exitCode = WEXITSTATUS(status);
    else r.exitCode = 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return r;
}

#endif

std::vector<std::string> tailOf(const std::vector<std::string>& lines, std::size_t n) {
    std::vector<std::string> out;
    for (auto it = lines.rbegin(); it != lines.rend() && out.size() < n; ++it)
        if (!it->empty()) out.push_back(*it);
    std::reverse(out.begin(), out.end());
    return out;
}

const char* const kNodeLink = "https://nodejs.org";

void nodeMissing(Report& r, const std::string& why) {
    r.outcome = Outcome::NodeMissing;
    r.reason = why;
    r.next = "Installe Node.js (la version LTS, 22 ou plus r\xC3\xA9" "cente) depuis nodejs.org : le bouton \xC2\xAB LTS \xC2\xBB, garde les choix propos\xC3\xA9s. Puis recommence.";
    r.link = kNodeLink;
}

// Le kit et Node.js : ce que deploy et login cherchent d'abord. Faux : `r` dit pourquoi.
bool prepare(const Options& o, Report& r, Kit& kit, NodeTools& tools, const std::function<void(const std::string&)>& say) {
    std::string why;
    if (!findKit(o.kit, kit, &why)) {
        r.outcome = Outcome::KitMissing;
        r.reason = "Le kit de mise en ligne du service est introuvable : " + why + ".";
        r.next = o.kit.empty() ? std::string("R\xC3\xA9installe l'Admin avec \xC2\xAB Le service en ligne \xC2\xBB coch\xC3\xA9 (la page \xC2\xAB Ce que tu installes \xC2\xBB), puis recommence.")
                               : std::string("Donne le dossier de deployer.bat (--dossier-kit), puis recommence.");
        return false;
    }
    r.kit = kit.folder;
    say("Kit du service : " + kit.folder + (kit.version.empty() ? std::string() : " (service " + kit.version + ")"));
    if (kit.service.empty()) {
        r.outcome = Outcome::Failed;
        r.reason = "Le kit n'a pas le dossier du service (wrangler.toml) : il est incomplet.";
        r.next = "R\xC3\xA9installe l'Admin avec \xC2\xAB Le service en ligne \xC2\xBB coch\xC3\xA9, puis recommence.";
        return false;
    }
    const std::string path = !o.path.empty() ? o.path : searchPath();
    if (!findNode(path, kit.folder, tools)) {
        say("Node.js est introuvable sur ce PC (ni dans le dossier du kit, ni dans le PATH).");
        nodeMissing(r, "Node.js n'est pas install\xC3\xA9 sur ce PC : la mise en ligne en a besoin (une fois).");
        return false;
    }
    if (tools.npx.empty()) {
        say("npx est introuvable \xC3\xA0 c\xC3\xB4t\xC3\xA9 de Node.js.");
        nodeMissing(r, "Node.js est incomplet sur ce PC (npx manque) : r\xC3\xA9installe-le.");
        return false;
    }
    say("Node.js : " + tools.node);
    return true;
}

} // namespace

// ============================================================================================ le kit
std::vector<std::string> kitCandidates(const std::string& given) {
    if (!given.empty()) return {given};
    const TestHooks h = hooks();
    if (!h.kit.empty()) return {h.kit};
    std::vector<std::string> out;
#if defined(_WIN32)
    const std::wstring la = envW(L"LOCALAPPDATA");
    if (!la.empty()) out.push_back(U(fs::path(la) / "XPG" / "ServiceLicences"));
#endif
    if (const std::string dir = programDir(); !dir.empty()) out.push_back(U(P(dir) / "service-licences"));
    return out;
}

std::string serviceVersionIn(const std::string& serviceFolder) {
    if (serviceFolder.empty()) return {};
    const std::string text = readSmall(P(serviceFolder) / "src" / "licences.js", 256u * 1024u);   // VERSION est en tete du fichier
    const std::string marker = "const VERSION = \"";
    std::size_t p = text.find(marker);
    if (p == std::string::npos) return {};
    p += marker.size();
    const std::size_t e = text.find('"', p);
    if (e == std::string::npos || e - p > 20) return {};
    const std::string v = text.substr(p, e - p);
    int a = 0, b = 0, c = 0;
    return licensing::admins::parseSemver(v, a, b, c) ? v : std::string();
}

bool findKit(const std::string& given, Kit& out, std::string* why) {
    out = Kit{};
    const auto candidates = kitCandidates(given);
    for (const auto& c : candidates) {
        std::error_code ec;
        fs::path dir = fs::absolute(P(c), ec);
        if (ec) dir = P(c);
        if (!isFile(dir / "deployer.mjs")) continue;
        out.folder = U(dir.lexically_normal());
        out.script = U((dir / "deployer.mjs").lexically_normal());
        // Comme deployer.mjs : le dossier du kit, son sous-dossier service, ou ../../service (le depot).
        for (const fs::path& s : {dir, dir / "service", dir / ".." / ".." / "service"})
            if (isFile(s / "wrangler.toml")) {
                out.service = U(s.lexically_normal());
                break;
            }
        out.version = serviceVersionIn(out.service);
        out.publicKey = isFile(dir / "cle-publique.txt") || (!out.service.empty() && isFile(P(out.service) / "cle-publique.txt"));
        return true;
    }
    if (why) {
        if (!given.empty()) *why = given + " n'est pas le kit du service (deployer.mjs n'y est pas : c'est le dossier de deployer.bat)";
        else if (candidates.empty()) *why = "aucun dossier o\xC3\xB9 le chercher";
        else {
            std::string list;
            for (const auto& c : candidates) list += (list.empty() ? "" : ", ni ") + c;
            *why = "ni " + list;
        }
    }
    return false;
}

const Kit* installedKit(bool refresh) {
    static std::mutex  m;
    static Kit         kit;
    static bool        found = false;
    static double      at = -1.0;
    static std::string forHook;
    std::lock_guard<std::mutex> lk(m);
    const std::string hook = hooks().kit;
    const double now = steady();
    if (refresh || at < 0 || now - at > 30.0 || hook != forHook) {
        found = findKit({}, kit, nullptr);
        at = now;
        forHook = hook;
    }
    return found ? &kit : nullptr;
}

// ============================================================================================ Node.js
std::string searchPath() {
    const TestHooks h = hooks();
    if (!h.path.empty()) return h.path;
#if defined(_WIN32)
    std::wstring path = envW(L"PATH");
    std::vector<std::wstring> seen;
    {
        std::wstring cur;
        for (wchar_t c : path + L";") {
            if (c == L';') {
                if (!cur.empty()) seen.push_back(cur);
                cur.clear();
            } else cur += c;
        }
    }
    for (const auto& reg : {registryPath(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"),
                            registryPath(HKEY_CURRENT_USER, L"Environment")}) {
        std::wstring cur;
        for (wchar_t c : reg + L";") {
            if (c != L';') {
                cur += c;
                continue;
            }
            if (!cur.empty() && std::none_of(seen.begin(), seen.end(), [&](const std::wstring& s) { return sameNameW(s, cur); })) {
                seen.push_back(cur);
                if (!path.empty() && path.back() != L';') path += L';';
                path += cur;
            }
            cur.clear();
        }
    }
    return narrow(path);
#else
    const char* p = std::getenv("PATH");
    return p && *p ? std::string(p) : std::string("/usr/local/bin:/usr/bin:/bin");
#endif
}

bool findNode(const std::string& path, const std::string& firstDir, NodeTools& out) {
    out = NodeTools{};
    out.path = path;
    std::vector<std::string> dirs;
    if (!firstDir.empty()) dirs.push_back(firstDir);   // where node : le dossier courant (celui du kit) d'abord
    for (auto& d : splitPath(path)) dirs.push_back(std::move(d));
#if defined(_WIN32)
    static const char* const nodeNames[] = {"node.exe"};
    static const char* const npxNames[] = {"npx.cmd", "npx.exe", "npx.bat"};
#else
    static const char* const nodeNames[] = {"node"};
    static const char* const npxNames[] = {"npx"};
#endif
    for (const auto& d : dirs) {
        for (const char* n : nodeNames)
            if (out.node.empty() && runnable(P(d) / n)) out.node = U(P(d) / n);
        for (const char* n : npxNames)
            if (out.npx.empty() && runnable(P(d) / n)) out.npx = U(P(d) / n);
        if (!out.node.empty() && !out.npx.empty()) break;
    }
    return !out.node.empty();
}

// ============================================================================================ la sortie
std::string scrub(const std::string& raw) {
    std::string s = maskSecrets(cleanText(raw));
    // 400 caracteres au plus (sans couper un caractere en deux).
    std::size_t chars = 0, i = 0;
    for (; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (chars == 400) break;
            ++chars;
        }
    }
    if (i < s.size()) {
        s.resize(i);
        s += "\xE2\x80\xA6";
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

void classify(Report& r, const std::vector<std::string>& lines, bool cancelled, bool dryRun) {
    r.tail = tailOf(lines, 12);
    r.link.clear();
    if (cancelled) {
        r.outcome = Outcome::Cancelled;
        r.reason = "Mise en ligne arr\xC3\xAAt\xC3\xA9" "e \xC3\xA0 ta demande.";
        r.next = "Relance \xC2\xAB D\xC3\xA9ployer le service \xC2\xBB quand tu veux : la relancer ne casse rien (les licences, les activations et "
                 "l'archive restent chez Cloudflare).";
        return;
    }
    std::string all;
    for (const auto& l : lines) all += lowerAscii(l) + "\n";
    if (r.exitCode == 0) {
        r.outcome = Outcome::Ok;
        if (dryRun) {
            r.reason = "Essai r\xC3\xA9ussi : le service est pr\xC3\xAAt \xC3\xA0 partir (rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9 \xC3\xA0 Cloudflare).";
            r.next.clear();
        } else {
            r.reason = "Le service est en ligne" + (r.url.empty() ? std::string(".") : " : " + r.url + ".");
            r.next = "L'Admin relit son \xC3\xA9tat (/v1/etat) : sa version, ta cl\xC3\xA9, son certificat.";
        }
        return;
    }
    if (contains(all, "workers.dev subdomain")) {
        r.outcome = Outcome::Subdomain;
        r.reason = "Ton compte Cloudflare n'a pas encore son sous-domaine workers.dev.";
        r.next = "Sur dash.cloudflare.com, ouvre \xC2\xAB Workers et Pages \xC2\xBB et choisis ton sous-domaine (par exemple \xC2\xAB mon-entreprise \xC2\xBB), puis recommence.";
        r.link = "https://dash.cloudflare.com";
        return;
    }
    if (contains(all, "cloudflare_account_id")) {
        r.outcome = Outcome::Account;
        r.reason = "Ta connexion Cloudflare ouvre plusieurs comptes : la mise en ligne ne sait pas lequel prendre.";
        r.next = "Lance une fois menu D\xC3\xA9marrer \xE2\x80\xBA D\xC3\xA9ployer le service en ligne : il te demande le compte. Ensuite, ce bouton suffit.";
        return;
    }
    static const char* const auth[] = {"cloudflare_api_token", "not authenticated", "not logged in", "authentication error", "[code: 10000]",
                                       "invalid access token", "failed to refresh", "you must be logged in", "unable to authenticate"};
    if (std::any_of(std::begin(auth), std::end(auth), [&](const char* k) { return contains(all, k); })) {
        r.outcome = Outcome::NotLoggedIn;
        r.reason = "Ce PC n'est pas connect\xC3\xA9 \xC3\xA0 ton compte Cloudflare (ou la connexion a expir\xC3\xA9).";
        r.next = "Clique \xC2\xAB Se connecter \xC3\xA0 Cloudflare\xE2\x80\xA6 \xC2\xBB : ton navigateur s'ouvre, connecte-toi et clique \xC2\xAB Allow \xC2\xBB. Puis \xC2\xAB D\xC3\xA9ployer le service \xC2\xBB de nouveau.";
        return;
    }
    if (contains(all, "est trop ancien")) {
        nodeMissing(r, "Node.js est trop ancien sur ce PC : il faut la version 22 ou plus r\xC3\xA9" "cente.");
        r.outcome = Outcome::NodeTooOld;
        return;
    }
    if (contains(all, "node.js n'est pas install")) {
        nodeMissing(r, "Node.js n'est pas install\xC3\xA9 sur ce PC : la mise en ligne en a besoin (une fois).");
        return;
    }
    r.outcome = Outcome::Failed;
    std::string erreur;
    for (const auto& l : lines)
        if (l.rfind("ERREUR : ", 0) == 0) erreur = l.substr(9);
    r.reason = "La mise en ligne a \xC3\xA9" "chou\xC3\xA9" "e (code " + std::to_string(r.exitCode) + ")" + (erreur.empty() ? std::string(".") : " : " + erreur);
    r.next = "Les derni\xC3\xA8res lignes du journal disent pourquoi. V\xC3\xA9rifie ta connexion \xC3\xA0 Internet, puis recommence ; si cela recommence, "
             "envoie ces lignes \xC3\xA0 ton int\xC3\xA9grateur.";
}

std::string Report::text() const {
    std::string t = reason;
    if (!next.empty()) t += "\n" + next;
    if (!link.empty()) t += "\n" + link;
    if (outcome == Outcome::Failed && !tail.empty()) {
        t += "\nLes derni\xC3\xA8res lignes :";
        for (const auto& l : tail) t += "\n  " + l;
    }
    return t;
}

// ============================================================================================ lancer
bool running() noexcept { return g_running.load(); }
void stopAll() noexcept { g_stop.store(true); }

Report deploy(const Options& o, const LineSink& onLine, const std::atomic<bool>* cancel) {
    Report r;
    RunGuard guard;
    if (!guard.ok) {
        r.outcome = Outcome::Busy;
        r.reason = "Une mise en ligne tourne d\xC3\xA9j\xC3\xA0 : attends qu'elle finisse.";
        return r;
    }
    const double t0 = steady();
    std::vector<std::string> lines;
    const auto say = [&](const std::string& l) {
        const std::string c = scrub(l);
        lines.push_back(c);
        if (onLine) onLine(c);
    };
    Kit kit;
    NodeTools tools;
    if (!prepare(o, r, kit, tools, say)) {
        r.tail = tailOf(lines, 12);
        r.seconds = steady() - t0;
        return r;
    }
    if (!kit.publicKey) {
        r.outcome = Outcome::KeyMissing;
        r.reason = "Le kit n'a pas ta cl\xC3\xA9 PUBLIQUE (cle-publique.txt) : sans elle, le service refuserait tous les appels de ton Admin.";
        r.next = "Service en ligne \xE2\x80\xBA \xC3\x89" "crire cle-publique.txt\xE2\x80\xA6 (l'admin+ : la cl\xC3\xA9 de l'\xC3\xA9" "diteur), puis recommence.";
        r.tail = tailOf(lines, 12);
        r.seconds = steady() - t0;
        return r;
    }
    if (stopAsked(cancel)) {
        classify(r, lines, true, o.dryRun);
        return r;
    }
    std::vector<std::string> args{kit.script};
    if (o.dryRun) args.push_back("--essai");
    say(std::string("node deployer.mjs") + (o.dryRun ? " --essai" : "") + " (dans " + kit.folder + ")");
    const RunResult run = runChild(tools.node, args, kit.folder, tools.path, true, o.timeoutSeconds, lines, onLine, cancel);
    r.seconds = steady() - t0;
    if (!run.started) {
        r.outcome = Outcome::StartFailed;
        r.reason = "Node.js ne se lance pas : " + run.error + ".";
        r.next = "R\xC3\xA9installe Node.js (la version LTS) depuis nodejs.org, puis recommence.";
        r.link = kNodeLink;
        r.tail = tailOf(lines, 12);
        return r;
    }
    r.exitCode = run.exitCode;
    // L'adresse : << Ton service est en ligne : https://... >> (deployer.mjs), sinon adresse-du-service.txt.
    for (const auto& l : lines) {
        const std::string marker = "Ton service est en ligne : ";
        if (l.rfind(marker, 0) != 0) continue;
        std::string u = l.substr(marker.size());
        if (const auto sp = u.find(' '); sp != std::string::npos) u.resize(sp);
        if (u.rfind("https://", 0) == 0) r.url = u;
    }
    if (r.url.empty() && run.exitCode == 0 && !o.dryRun) {
        std::string u = readSmall(P(kit.folder) / "adresse-du-service.txt", 400);
        while (!u.empty() && (u.back() == '\n' || u.back() == '\r' || u.back() == ' ')) u.pop_back();
        if (u.rfind("https://", 0) == 0 && u.find_first_of(" \"'<>") == std::string::npos) r.url = u;
    }
    if (run.timedOut) {
        r.outcome = Outcome::Failed;
        r.reason = "La mise en ligne a pris trop de temps (" + std::to_string(static_cast<int>(o.timeoutSeconds / 60)) + " min) : elle a \xC3\xA9t\xC3\xA9 arr\xC3\xAAt\xC3\xA9" "e.";
        r.next = "V\xC3\xA9rifie ta connexion \xC3\xA0 Internet, puis recommence.";
        r.tail = tailOf(lines, 12);
        return r;
    }
    classify(r, lines, run.cancelled, o.dryRun);
    return r;
}

Report login(const Options& o, const LineSink& onLine, const std::atomic<bool>* cancel) {
    Report r;
    RunGuard guard;
    if (!guard.ok) {
        r.outcome = Outcome::Busy;
        r.reason = "Une mise en ligne tourne d\xC3\xA9j\xC3\xA0 : attends qu'elle finisse.";
        return r;
    }
    const double t0 = steady();
    std::vector<std::string> lines;
    const auto say = [&](const std::string& l) {
        const std::string c = scrub(l);
        lines.push_back(c);
        if (onLine) onLine(c);
    };
    Kit kit;
    NodeTools tools;
    if (!prepare(o, r, kit, tools, say)) {
        r.tail = tailOf(lines, 12);
        r.seconds = steady() - t0;
        return r;
    }
    // npx wrangler login : wrangler deja installe dans le kit (le premier deploiement l'y met) se lance directement ;
    // sinon npx (celui de Node.js : npx-cli.js a cote de node, ou le programme npx).
    std::string program = tools.node;
    std::vector<std::string> args;
    const fs::path wrangler = P(kit.service) / "node_modules" / "wrangler" / "bin" / "wrangler.js";
    const fs::path npxCli = P(tools.node).parent_path() / "node_modules" / "npm" / "bin" / "npx-cli.js";
    if (isFile(wrangler)) {
        args = {U(wrangler), "login"};
    } else if (isFile(npxCli)) {
        args = {U(npxCli), "--yes", "wrangler", "login"};
    } else {
#if defined(_WIN32)
        std::wstring comspec = envW(L"ComSpec");
        if (comspec.empty()) comspec = L"C:\\Windows\\System32\\cmd.exe";
        program = narrow(comspec);
        args = {"/d", "/c", tools.npx, "--yes", "wrangler", "login"};
#else
        program = tools.npx;
        args = {"--yes", "wrangler", "login"};
#endif
    }
    say("npx wrangler login (dans " + kit.service + ") : ton navigateur s'ouvre sur Cloudflare ; connecte-toi, puis clique \xC2\xAB Allow \xC2\xBB.");
    // Pas de Job ici : le navigateur que wrangler ouvre ne doit pas partir avec lui.
    const RunResult run = runChild(program, args, kit.service, tools.path, false, std::max(60.0, std::min(o.timeoutSeconds, 600.0)), lines, onLine, cancel);
    r.seconds = steady() - t0;
    r.tail = tailOf(lines, 12);
    if (!run.started) {
        r.outcome = Outcome::StartFailed;
        r.reason = "La connexion \xC3\xA0 Cloudflare ne se lance pas : " + run.error + ".";
        r.next = "R\xC3\xA9installe Node.js (la version LTS) depuis nodejs.org, puis recommence.";
        r.link = kNodeLink;
        return r;
    }
    r.exitCode = run.exitCode;
    if (run.cancelled) {
        r.outcome = Outcome::Cancelled;
        r.reason = "Connexion \xC3\xA0 Cloudflare annul\xC3\xA9" "e.";
        r.next = "Recommence \xC2\xAB Se connecter \xC3\xA0 Cloudflare\xE2\x80\xA6 \xC2\xBB quand tu veux.";
        return r;
    }
    if (run.exitCode == 0 && !run.timedOut) {
        r.outcome = Outcome::Ok;
        r.reason = "Ce PC est connect\xC3\xA9 \xC3\xA0 ton compte Cloudflare.";
        r.next = "Clique \xC2\xAB D\xC3\xA9ployer le service \xC2\xBB de nouveau.";
        return r;
    }
    r.outcome = Outcome::NotLoggedIn;
    r.reason = run.timedOut ? std::string("La connexion \xC3\xA0 Cloudflare a attendu trop longtemps : elle a \xC3\xA9t\xC3\xA9 arr\xC3\xAAt\xC3\xA9" "e.")
                            : "La connexion \xC3\xA0 Cloudflare n'a pas abouti (code " + std::to_string(run.exitCode) + ").";
    r.next = "Recommence \xC2\xAB Se connecter \xC3\xA0 Cloudflare\xE2\x80\xA6 \xC2\xBB : dans ton navigateur, connecte-toi \xC3\xA0 ton compte Cloudflare puis clique \xC2\xAB Allow \xC2\xBB.";
    return r;
}

// ============================================================================================ la version
VersionCheck checkVersion(const std::string& online, const std::string& kitVersion, const std::string& required) {
    namespace adm = licensing::admins;
    VersionCheck v;
    int a = 0, b = 0, c = 0;
    v.online = online;
    v.required = required;
    v.kit = adm::parseSemver(kitVersion, a, b, c) ? kitVersion : std::string();
    v.target = !v.kit.empty() && adm::compareSemver(v.kit, required) > 0 ? v.kit : required;
    v.known = adm::parseSemver(online, a, b, c);
    if (!v.known) return v;
    v.belowRequired = adm::compareSemver(online, required) < 0;
    v.kitNewer = !v.kit.empty() && adm::compareSemver(online, v.kit) < 0;
    v.outdated = v.belowRequired || v.kitNewer;
    return v;
}

std::string outdatedMessage(const VersionCheck& v) {
    if (!v.outdated) return {};
    namespace adm = licensing::admins;
    if (v.belowRequired && !v.kit.empty() && adm::compareSemver(v.kit, v.required) < 0)
        return "Ton service en ligne est en " + v.online + " ; cet Admin a besoin de la " + v.required + ", mais le kit install\xC3\xA9 avec lui porte la " + v.kit +
               " : r\xC3\xA9installe l'Admin avec \xC2\xAB Le service en ligne \xC2\xBB, puis d\xC3\xA9ploie.";
    if (v.belowRequired)
        return "Ton service en ligne est en " + v.online + " : d\xC3\xA9ploie la " + v.target + " pour " + kServiceRequiredFor + ".";
    return "Ton service en ligne est en " + v.online + ", la " + v.kit + " est pr\xC3\xAAte dans l'Admin : d\xC3\xA9ploie-la.";
}

// ============================================================================================ le reste
std::string openLink(const std::string& url) {
    if (url.rfind("https://", 0) != 0 || url.size() > 300 || url.find_first_of(" \"'<>^&|`$\\\r\n\t") != std::string::npos)
        return "cette adresse ne s'ouvre pas d'ici : " + url;
    if (const TestHooks h = hooks(); h.openLink) return h.openLink(url);
#if defined(_WIN32)
    const std::wstring w = wide(url);
    const auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (r <= 32) return "Windows n'a pas pu ouvrir " + url + " : ouvre-la toi-m\xC3\xAAme dans ton navigateur.";
    return {};
#else
#  if defined(__APPLE__)
    std::string a0 = "open";
#  else
    std::string a0 = "xdg-open";
#  endif
    std::string a1 = url;
    char* argv[] = {a0.data(), a1.data(), nullptr};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    const int e = posix_spawnp(&pid, a0.c_str(), &fa, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (e != 0) return "le navigateur ne s'ouvre pas (" + a0 + ") : ouvre " + url + " toi-m\xC3\xAAme.";
    std::thread([pid] {
        int st = 0;
        (void)waitpid(pid, &st, 0);
    }).detach();
    return {};
#endif
}

void setTestHooks(TestHooks h) {
    std::lock_guard<std::mutex> lk(g_hooksMutex);
    g_hooks = std::move(h);
}

} // namespace license_admin::deployer
