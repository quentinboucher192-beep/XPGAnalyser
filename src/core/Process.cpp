// =============================================================================
//  core/Process.cpp - voir Process.hpp
// =============================================================================
#include "Process.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <climits>
#include <cstring>
#endif

namespace core::process {

namespace {

double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string secondsText(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", s);
    std::string t(buf);
    for (auto& c : t)
        if (c == '.') c = ',';
    return t;
}

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
#endif

} // namespace

std::string Result::summary() const {
    if (!started) return "pas lanc\xC3\xA9 : " + error;
    if (cancelled) return "arr\xC3\xAAt\xC3\xA9 \xC3\xA0 ta demande apr\xC3\xA8s " + secondsText(this->seconds) + " s";
    if (timedOut) return "arr\xC3\xAAt\xC3\xA9 : temps d\xC3\xA9pass\xC3\xA9 (" + secondsText(this->seconds) + " s)";
    if (memoryExceeded) return "arr\xC3\xAAt\xC3\xA9 : m\xC3\xA9moire d\xC3\xA9pass\xC3\xA9" "e";
    if (crashed) return "termin\xC3\xA9 anormalement apr\xC3\xA8s " + secondsText(this->seconds) + " s";
    return "termin\xC3\xA9 (code " + std::to_string(exitCode) + ") en " + secondsText(this->seconds) + " s";
}

#if defined(_WIN32)

std::string selfPath() {
    std::wstring self(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    if (n == 0 || n >= self.size()) return {};
    self.resize(n);
    return narrow(self);
}

Result run(const Request& req, const std::atomic<bool>* cancel) {
    Result r;
    const double t0 = now();
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    HANDLE outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr, inR = nullptr, inW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&errR, &errW, &sa, 0) || !CreatePipe(&inR, &inW, &sa, 0)) {
        r.error = "tubes refus\xC3\xA9s (erreur " + std::to_string(GetLastError()) + ")";
        for (HANDLE h : {outR, outW, errR, errW, inR, inW})
            if (h) CloseHandle(h);
        return r;
    }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    // Le Job : la memoire, un seul processus, tue a la fermeture.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
        li.BasicLimitInformation.ActiveProcessLimit = 1;
        li.ProcessMemoryLimit = req.limits.memoryBytes;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    std::wstring line = quote(wide(req.program));
    for (const auto& a : req.args) line += L" " + quote(wide(a));
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outW;
    si.hStdError = errW;
    si.hStdInput = inR;
    PROCESS_INFORMATION pi{};
    // Un environnement presque vide : SystemRoot (sans lui, rien ne demarre sous Windows), rien d'autre.
    std::wstring env;
    {
        wchar_t root[MAX_PATH] = {0};
        if (GetEnvironmentVariableW(L"SystemRoot", root, MAX_PATH) > 0) env += L"SystemRoot=" + std::wstring(root) + L'\0';
        env += L"XPG_BAC_A_SABLE=1";
        env += L'\0';
        env += L'\0';
    }
    const std::wstring dir = wide(req.workDir);
    const BOOL okStart = CreateProcessW(wide(req.program).c_str(), line.data(), nullptr, nullptr, TRUE,
                                        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env.data(),
                                        dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    CloseHandle(outW);
    CloseHandle(errW);
    CloseHandle(inR);
    if (!okStart) {
        r.error = "lancement refus\xC3\xA9 (erreur " + std::to_string(GetLastError()) + ")";
        CloseHandle(outR);
        CloseHandle(errR);
        CloseHandle(inW);
        if (job) CloseHandle(job);
        return r;
    }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    r.started = true;
    r.networkIsolated = false;   // pas d'isolement reseau sans droits d'administrateur
    // L'entree, puis fermee.
    if (!req.input.empty()) {
        DWORD wrote = 0;
        WriteFile(inW, req.input.data(), static_cast<DWORD>(req.input.size()), &wrote, nullptr);
    }
    CloseHandle(inW);
    // Les sorties lues sur deux fils (un tube plein bloquerait l'enfant).
    std::size_t total = 0;
    std::atomic<bool> cut{false};
    std::mutex m;
    const auto reader = [&](HANDLE h, std::string* into) {
        char buf[8192];
        DWORD got = 0;
        while (ReadFile(h, buf, sizeof buf, &got, nullptr) && got > 0) {
            std::lock_guard<std::mutex> lk(m);
            const std::size_t room = total < req.limits.outputBytes ? req.limits.outputBytes - total : 0;
            const std::size_t keep = std::min<std::size_t>(room, got);
            into->append(buf, keep);
            total += keep;
            if (keep < got) cut = true;
        }
    };
    std::thread to(reader, outR, &r.out), te(reader, errR, &r.err);
    for (;;) {
        const DWORD w = WaitForSingleObject(pi.hProcess, 50);
        if (w == WAIT_OBJECT_0) break;
        if (cancel && cancel->load()) {
            r.cancelled = true;
            TerminateProcess(pi.hProcess, 1);
        } else if (now() - t0 > req.limits.seconds) {
            r.timedOut = true;
            TerminateProcess(pi.hProcess, 1);
        }
        if (r.cancelled || r.timedOut) {
            WaitForSingleObject(pi.hProcess, 2000);
            break;
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    to.join();
    te.join();
    r.outputCut = cut.load();
    r.exitCode = (r.cancelled || r.timedOut) ? -1 : static_cast<int>(code);
    r.crashed = !r.cancelled && !r.timedOut && (code & 0xC0000000u) == 0xC0000000u;   // une exception NTSTATUS
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        if (QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li, nullptr))
            r.memoryExceeded = li.PeakProcessMemoryUsed >= req.limits.memoryBytes;
        CloseHandle(job);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(outR);
    CloseHandle(errR);
    r.seconds = now() - t0;
    return r;
}

#else

std::string selfPath() {
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return {};
    return std::string(buf, static_cast<std::size_t>(n));
}

Result run(const Request& req, const std::atomic<bool>* cancel) {
    Result r;
    const double t0 = now();
    int out[2], err[2], in[2], ready[2], execFail[2];
    // execFail : ferme tout seul par un exec reussi (O_CLOEXEC) ; un exec rate y ecrit son errno.
    if (pipe(out) != 0 || pipe(err) != 0 || pipe(in) != 0 || pipe(ready) != 0 || pipe2(execFail, O_CLOEXEC) != 0) {
        r.error = "tubes refus\xC3\xA9s";
        return r;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        r.error = "fork refus\xC3\xA9";
        for (int fd : {out[0], out[1], err[0], err[1], in[0], in[1], ready[0], ready[1], execFail[0], execFail[1]}) close(fd);
        return r;
    }
    if (pid == 0) {
        // L'enfant : son groupe, ses limites, son environnement vide, puis le programme.
        setpgid(0, 0);
        char isolated = '0';
#if defined(CLONE_NEWNET) && defined(CLONE_NEWUSER)
        if (req.isolateNetwork && unshare(CLONE_NEWUSER | CLONE_NEWNET) == 0) isolated = '1';
#endif
        (void)!write(ready[1], &isolated, 1);
        close(ready[0]);
        close(ready[1]);
        rlimit mem{static_cast<rlim_t>(req.limits.memoryBytes), static_cast<rlim_t>(req.limits.memoryBytes)};
        setrlimit(RLIMIT_AS, &mem);
        const rlim_t cpu = static_cast<rlim_t>(req.limits.seconds) + 2;
        rlimit cpuLim{cpu, cpu};
        setrlimit(RLIMIT_CPU, &cpuLim);
        rlimit core0{0, 0};
        setrlimit(RLIMIT_CORE, &core0);
        dup2(in[0], 0);
        dup2(out[1], 1);
        dup2(err[1], 2);
        for (int fd : {out[0], out[1], err[0], err[1], in[0], in[1]}) close(fd);
        if (!req.workDir.empty() && chdir(req.workDir.c_str()) != 0) _exit(126);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(req.program.c_str()));
        for (const auto& a : req.args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        char envSandbox[] = "XPG_BAC_A_SABLE=1";
        char* envp[] = {envSandbox, nullptr};
        close(execFail[0]);
        execve(req.program.c_str(), argv.data(), envp);
        const int e = errno;
        (void)!write(execFail[1], &e, sizeof e);
        _exit(127);
    }
    close(out[1]);
    close(err[1]);
    close(in[0]);
    close(ready[1]);
    close(execFail[1]);
    {
        char isolated = '0';
        if (read(ready[0], &isolated, 1) == 1) r.networkIsolated = isolated == '1';
        close(ready[0]);
        int e = 0;
        if (read(execFail[0], &e, sizeof e) == static_cast<ssize_t>(sizeof e)) {   // l'exec a echoue
            close(execFail[0]);
            close(out[0]);
            close(err[0]);
            close(in[1]);
            int st = 0;
            (void)waitpid(pid, &st, 0);
            r.error = "le programme ne se lance pas (" + std::string(std::strerror(e)) + ") : " + req.program;
            r.seconds = now() - t0;
            return r;
        }
        close(execFail[0]);
    }
    r.started = true;
    if (!req.input.empty()) {
        std::size_t done = 0;
        signal(SIGPIPE, SIG_IGN);
        while (done < req.input.size()) {
            const ssize_t w = write(in[1], req.input.data() + done, req.input.size() - done);
            if (w <= 0) break;
            done += static_cast<std::size_t>(w);
        }
    }
    close(in[1]);
    fcntl(out[0], F_SETFL, O_NONBLOCK);
    fcntl(err[0], F_SETFL, O_NONBLOCK);
    std::size_t total = 0;
    bool outOpen = true, errOpen = true;
    int status = 0;
    bool exited = false;
    const auto drain = [&](int fd, std::string* into, bool* open) {
        char buf[8192];
        for (;;) {
            const ssize_t n = read(fd, buf, sizeof buf);
            if (n > 0) {
                const std::size_t room = total < req.limits.outputBytes ? req.limits.outputBytes - total : 0;
                const std::size_t keep = std::min<std::size_t>(room, static_cast<std::size_t>(n));
                into->append(buf, keep);
                total += keep;
                if (keep < static_cast<std::size_t>(n)) r.outputCut = true;
                continue;
            }
            if (n == 0) *open = false;
            break;
        }
    };
    while (!exited || outOpen || errOpen) {
        pollfd fds[2] = {{out[0], POLLIN, 0}, {err[0], POLLIN, 0}};
        (void)poll(fds, 2, 50);
        if (outOpen) drain(out[0], &r.out, &outOpen);
        if (errOpen) drain(err[0], &r.err, &errOpen);
        if (!exited) {
            const pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) exited = true;
        }
        if (!exited) {
            if (cancel && cancel->load() && !r.cancelled) {
                r.cancelled = true;
                kill(-pid, SIGKILL);
            } else if (!r.timedOut && !r.cancelled && now() - t0 > req.limits.seconds) {
                r.timedOut = true;
                kill(-pid, SIGKILL);
            }
        } else if (now() - t0 > req.limits.seconds + 5.0) {
            break;   // un petit-enfant garde les tubes ouverts : on ne l'attend pas
        }
    }
    close(out[0]);
    close(err[0]);
    if (WIFEXITED(status)) r.exitCode = (r.cancelled || r.timedOut) ? -1 : WEXITSTATUS(status);
    if (WIFSIGNALED(status) && !r.cancelled && !r.timedOut) {
        r.crashed = true;
        if (WTERMSIG(status) == SIGXCPU) r.timedOut = true;
    }
    // Une allocation refusee par RLIMIT_AS : seulement quand l'enfant le dit (pas de devinette sur un signal).
    if (!r.cancelled && !r.timedOut) {
        const std::string& e = r.err;
        if (e.find("bad_alloc") != std::string::npos || e.find("annot allocate memory") != std::string::npos
            || e.find("Out of memory") != std::string::npos || e.find("m\xC3\xA9moire insuffisante") != std::string::npos)
            r.memoryExceeded = true;
    }
    r.seconds = now() - t0;
    return r;
}

#endif

} // namespace core::process
