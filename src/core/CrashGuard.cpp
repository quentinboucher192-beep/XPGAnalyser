// =============================================================================
//  core/CrashGuard.cpp - 1.10.2 : le rapport de plantage et de blocage
// -----------------------------------------------------------------------------
//  Ce qui tourne dans un gestionnaire n'alloue pas : un ecrivain a tampon fixe
//  (g_writer), des chemins dans des tableaux poses a l'installation, et les
//  appels du systeme. Sous Windows, le rapport est ecrit par un fil « rapporteur »
//  cree a l'installation (comme Breakpad) : le fil qui plante peut n'avoir plus
//  de pile (debordement) ou tenir un verrou ; il attend le rapporteur.
// =============================================================================
#include "CrashGuard.hpp"

#include "CallTrail.hpp"
#include "Version.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <dbghelp.h>
#else
#  include <dlfcn.h>
#  include <execinfo.h>
#  include <fcntl.h>
#  include <pthread.h>
#  include <signal.h>
#  include <sys/utsname.h>
#  include <unistd.h>
#endif

namespace core::crash {
namespace {

namespace fs = std::filesystem;

fs::path pathFromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string utf8Of(const fs::path& p) { const auto u = p.u8string(); return std::string(u.begin(), u.end()); }

constexpr int kMaxFrames = 64;
constexpr std::size_t kTrailInReport = 2000;
constexpr int kReportsKept = 20;

// ---- pose a l'installation, lu par les gestionnaires ------------------------------
char g_folder[1024];          // UTF-8
char g_system[256];
std::atomic<bool> g_installed{false};
bool g_dialogs = true;
std::atomic<int> g_handling{0};
std::uint16_t g_mainThread = 0;

char g_docs[4096];
std::atomic<int> g_docsBusy{0};

std::atomic<HangDialog> g_hangDialog{nullptr};
std::atomic<CrashBox> g_crashBox{nullptr};

// ---- le chien de garde -------------------------------------------------------------
std::atomic<std::uint64_t> g_lastBeatMs{0};
std::atomic<std::uint64_t> g_lastGapMs{0};    // le dernier long arret, vu par heartbeat() lui-meme
std::atomic<int> g_hold{0};

// Une boite de plantage ou de blocage est ouverte : le chien de garde se tait (1.10.2).
// Sans cela, la boite du plantage laissee ouverte plus de 8 s devenait AUSSI un blocage
// (un 2e rapport, une 2e boite, et le lancement suivant parlait du blocage).
struct BoxOpen {
    BoxOpen() noexcept { g_hold.fetch_add(1, std::memory_order_relaxed); }
    ~BoxOpen() { g_hold.fetch_sub(1, std::memory_order_relaxed); }
    BoxOpen(const BoxOpen&) = delete;
    BoxOpen& operator=(const BoxOpen&) = delete;
};
std::thread g_watchdog;
std::mutex g_wdMutex;
std::condition_variable g_wdCv;
bool g_wdStop = false;
std::atomic<bool> g_recovered{false};
std::atomic<std::uint64_t> g_recoveredMs{0};
std::mutex g_lastHangMutex;
std::string g_lastHang;

// ---- les essais caches ---------------------------------------------------------------
std::atomic<int> g_test{0};
std::atomic<std::uint64_t> g_testAt{0};

// ---- un ecrivain sans allocation -----------------------------------------------------
struct Writer {
#if defined(_WIN32)
    HANDLE h = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif
    char buf[16384];
    std::size_t n = 0;

    bool ok() const noexcept {
#if defined(_WIN32)
        return h != INVALID_HANDLE_VALUE;
#else
        return fd >= 0;
#endif
    }
    void flush() noexcept {
        if (!ok() || n == 0) { n = 0; return; }
#if defined(_WIN32)
        DWORD done = 0;
        WriteFile(h, buf, static_cast<DWORD>(n), &done, nullptr);
#else
        std::size_t off = 0;
        while (off < n) {
            const ssize_t w = ::write(fd, buf + off, n - off);
            if (w <= 0) break;
            off += static_cast<std::size_t>(w);
        }
#endif
        n = 0;
    }
    void put(char c) noexcept { if (n == sizeof buf) flush(); buf[n++] = c; }
    void str(const char* s) noexcept { while (s && *s) put(*s++); }
    void str(const char* s, std::size_t len) noexcept { for (std::size_t i = 0; i < len && s[i]; ++i) put(s[i]); }
    void num(std::uint64_t v) noexcept {
        char b[24];
        int k = 0;
        do { b[k++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
        while (k) put(b[--k]);
    }
    void hex(std::uint64_t v, int width = 1) noexcept {
        char b[20];
        int k = 0;
        do { b[k++] = "0123456789ABCDEF"[v & 15]; v >>= 4; } while (v || k < width);
        str("0x");
        while (k) put(b[--k]);
    }
    void line(const char* s = "") noexcept { str(s); put('\n'); }
    void title(const char* s) noexcept {
        put('\n');
        line(s);
        std::size_t cols = 0;
        for (const char* p = s; *p; ++p) if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80) ++cols;
        for (std::size_t i = 0; i < cols; ++i) put('-');
        put('\n');
    }
    void close() noexcept {
        flush();
#if defined(_WIN32)
        if (h != INVALID_HANDLE_VALUE) { FlushFileBuffers(h); CloseHandle(h); }
        h = INVALID_HANDLE_VALUE;
#else
        if (fd >= 0) ::close(fd);
        fd = -1;
#endif
    }
};
Writer g_writer;

// Le nom d'un rapport : plantage-2026-10-02_13-05-11.txt (heure locale).
void reportName(char* out, std::size_t size, const char* prefix, std::uint64_t utcMs, const char* extension) noexcept {
    char when[24];
    trail::formatDateTime(utcMs, when);
    for (char* p = when; *p; ++p) {
        if (*p == ' ') *p = '_';
        else if (*p == ':') *p = '-';
    }
    std::snprintf(out, size, "%s-%s%s", prefix, when, extension);
}

const char* kindWord(ReportKind kind) noexcept {
    switch (kind) {
        case ReportKind::Crash:  return "plantage";
        case ReportKind::Hang:   return "blocage";
        case ReportKind::Manual: return "rapport";
    }
    return "rapport";
}

// ---- la pile : un cadre en une ligne (module+decalage, et le symbole si on l'a) -------
#if defined(_WIN32)
struct DbgHelp {
    HMODULE module = nullptr;
    decltype(&::SymInitialize) symInitialize = nullptr;
    decltype(&::SymSetOptions) symSetOptions = nullptr;
    decltype(&::SymFromAddr) symFromAddr = nullptr;
    decltype(&::SymGetLineFromAddr64) symGetLineFromAddr64 = nullptr;
    decltype(&::StackWalk64) stackWalk64 = nullptr;
    decltype(&::SymFunctionTableAccess64) symFunctionTableAccess64 = nullptr;
    decltype(&::SymGetModuleBase64) symGetModuleBase64 = nullptr;
    decltype(&::MiniDumpWriteDump) miniDumpWriteDump = nullptr;
    bool symbols = false;
};
DbgHelp g_dbg;
CRITICAL_SECTION g_dbgLock;     // dbghelp n'est pas sur entre les fils

using MessageBoxW_t = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
MessageBoxW_t g_messageBox = nullptr;
wchar_t g_folderW[1024];
HANDLE g_mainThreadHandle = nullptr;
ULONG_PTR g_mainStackLow = 0, g_mainStackHigh = 0;

template <class F> void load(F& f, HMODULE m, const char* name) { f = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name))); }

void loadDbgHelp() {
    InitializeCriticalSection(&g_dbgLock);
    g_dbg.module = LoadLibraryW(L"dbghelp.dll");
    if (!g_dbg.module) return;
    load(g_dbg.symInitialize, g_dbg.module, "SymInitialize");
    load(g_dbg.symSetOptions, g_dbg.module, "SymSetOptions");
    load(g_dbg.symFromAddr, g_dbg.module, "SymFromAddr");
    load(g_dbg.symGetLineFromAddr64, g_dbg.module, "SymGetLineFromAddr64");
    load(g_dbg.stackWalk64, g_dbg.module, "StackWalk64");
    load(g_dbg.symFunctionTableAccess64, g_dbg.module, "SymFunctionTableAccess64");
    load(g_dbg.symGetModuleBase64, g_dbg.module, "SymGetModuleBase64");
    load(g_dbg.miniDumpWriteDump, g_dbg.module, "MiniDumpWriteDump");
    if (g_dbg.symSetOptions) g_dbg.symSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
    if (g_dbg.symInitialize) g_dbg.symbols = g_dbg.symInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
}

void writeFrame(Writer& w, int index, void* address) noexcept {
    w.str(index < 10 ? "   " : "  ");
    w.num(static_cast<std::uint64_t>(index));
    w.str("  ");
    HMODULE module = nullptr;
    const auto addr = reinterpret_cast<std::uint64_t>(address);
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module) && module) {
        wchar_t path[MAX_PATH];
        const DWORD len = GetModuleFileNameW(module, path, MAX_PATH);
        const wchar_t* base = path;
        for (DWORD i = 0; i < len; ++i) if (path[i] == L'\\' || path[i] == L'/') base = path + i + 1;
        char name[MAX_PATH * 3];
        const int m = WideCharToMultiByte(CP_UTF8, 0, base, -1, name, sizeof name, nullptr, nullptr);
        w.str(m > 0 ? name : "?");
        w.put('+');
        w.hex(addr - reinterpret_cast<std::uint64_t>(module));
    } else {
        w.hex(addr, 8);
    }
    if (g_dbg.symbols && g_dbg.symFromAddr) {
        EnterCriticalSection(&g_dbgLock);
        alignas(SYMBOL_INFO) static char storage[sizeof(SYMBOL_INFO) + 512];
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
        std::memset(storage, 0, sizeof storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 displacement = 0;
        if (g_dbg.symFromAddr(GetCurrentProcess(), addr, &displacement, symbol)) {
            w.str("  ");
            w.str(symbol->Name);
            w.str("+");
            w.hex(displacement);
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof line;
            DWORD lineDisplacement = 0;
            if (g_dbg.symGetLineFromAddr64 && g_dbg.symGetLineFromAddr64(GetCurrentProcess(), addr, &lineDisplacement, &line)) {
                w.str("  (");
                w.str(line.FileName);
                w.put(':');
                w.num(line.LineNumber);
                w.put(')');
            }
        }
        LeaveCriticalSection(&g_dbgLock);
    }
    w.put('\n');
}

// Remonter une pile depuis un contexte : StackWalk64 quand dbghelp est la.
int walkContext(const CONTEXT& start, HANDLE thread, void** out, int max) noexcept {
    if (!g_dbg.stackWalk64) return 0;
    CONTEXT ctx = start;
    STACKFRAME64 sf{};
    DWORD machine = 0;
#if defined(_M_X64) || defined(__x86_64__)
    machine = IMAGE_FILE_MACHINE_AMD64;
    sf.AddrPC.Offset = ctx.Rip;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_IX86) || defined(__i386__)
    machine = IMAGE_FILE_MACHINE_I386;
    sf.AddrPC.Offset = ctx.Eip;
    sf.AddrFrame.Offset = ctx.Ebp;
    sf.AddrStack.Offset = ctx.Esp;
#else
    return 0;
#endif
    sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;
    int n = 0;
    EnterCriticalSection(&g_dbgLock);
    while (n < max && g_dbg.stackWalk64(machine, GetCurrentProcess(), thread, &sf, &ctx, nullptr,
                                         g_dbg.symFunctionTableAccess64, g_dbg.symGetModuleBase64, nullptr)) {
        if (sf.AddrPC.Offset == 0) break;
        out[n++] = reinterpret_cast<void*>(sf.AddrPC.Offset);
    }
    LeaveCriticalSection(&g_dbgLock);
    return n;
}

#if defined(_M_X64) || defined(__x86_64__)
// Le fil principal suspendu : remonter sa pile SANS verrou ni allocation (il peut tenir
// celui du tas), avec les tables de deroulement du code (.pdata), comme le fait Windows.
int unwindSuspended(CONTEXT ctx, void** out, int max) noexcept {
    int n = 0;
    while (n < max && ctx.Rip) {
        out[n++] = reinterpret_cast<void*>(ctx.Rip);
        if (g_mainStackHigh && (ctx.Rsp < g_mainStackLow || ctx.Rsp >= g_mainStackHigh)) break;
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
        if (!fn) {
            ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
            ctx.Rsp += 8;
        } else {
            void* handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
        }
    }
    return n;
}
#endif

#else  // Linux
void writeFrame(Writer& w, int index, void* address) noexcept {
    w.str(index < 10 ? "   " : "  ");
    w.num(static_cast<std::uint64_t>(index));
    w.str("  ");
    Dl_info info{};
    const auto addr = reinterpret_cast<std::uint64_t>(address);
    if (dladdr(address, &info) && info.dli_fname) {
        const char* base = info.dli_fname;
        for (const char* p = info.dli_fname; *p; ++p) if (*p == '/') base = p + 1;
        w.str(base);
        w.put('+');
        w.hex(addr - reinterpret_cast<std::uint64_t>(info.dli_fbase));
        if (info.dli_sname) {
            w.str("  ");
            w.str(info.dli_sname);
            w.put('+');
            w.hex(addr - reinterpret_cast<std::uint64_t>(info.dli_saddr));
        }
    } else {
        w.hex(addr, 8);
    }
    w.put('\n');
}
pthread_t g_mainPthread{};
void* g_probeFrames[kMaxFrames];
std::atomic<int> g_probeCount{0};
std::atomic<int> g_probeDone{0};
#endif

// ---- le corps d'un rapport -------------------------------------------------------------
struct ScopeCtx { Writer* w; };

void writeScopes(Writer& w) noexcept {
    static trail::ThreadStack stacks[trail::kMaxThreads];
    const std::size_t n = trail::stacks(stacks, trail::kMaxThreads);
    const std::uint64_t now = trail::nowMs();
    for (std::size_t i = 0; i < n; ++i) {
        const auto& s = stacks[i];
        w.str("Fil #");
        w.num(s.thread);
        if (s.name[0]) { w.put(' '); w.str(s.name); }
        w.str(" : ");
        w.num(s.depth);
        w.line(s.depth == 1 ? " port\xC3\xA9" "e ouverte" : " port\xC3\xA9" "es ouvertes");
        for (std::size_t k = 0; k < s.kept; ++k) {
            w.str("   ");
            for (std::size_t d = 0; d < k && d < 20; ++d) w.str("  ");
            w.str(s.frames[k].name ? s.frames[k].name : "?");
            w.str("   depuis ");
            const std::uint64_t ms = now > s.frames[k].sinceMs ? now - s.frames[k].sinceMs : 0;
            w.num(ms / 1000);
            w.put(',');
            const std::uint64_t frac = ms % 1000;
            w.put(static_cast<char>('0' + frac / 100));
            w.put(static_cast<char>('0' + frac / 10 % 10));
            w.put(static_cast<char>('0' + frac % 10));
            w.line(" s");
        }
        if (s.depth > s.kept) w.line("   ... (les plus profondes ne sont pas gard\xC3\xA9" "es)");
    }
    if (n == 0) w.line("(aucun fil suivi)");
}

void writeTrail(Writer& w, std::size_t max) noexcept {
    trail::visitRecent(max, [](const trail::Entry& e, void* c) {
        char line[trail::kLineMax];
        const std::size_t len = trail::formatEntry(e, line, sizeof line);
        auto* out = static_cast<Writer*>(c);
        out->str(line, len);
        out->put('\n');
    }, &w);
}

void writeDocuments(Writer& w) noexcept {
    if (g_docsBusy.load(std::memory_order_acquire)) { w.line("(la liste changeait a cet instant)"); return; }
    if (!g_docs[0]) { w.line("(aucun)"); return; }
    w.line(g_docs);
}

// Le rapport entier dans g_writer, deja ouvert.
void writeBody(Writer& w, ReportKind kind, const char* reason, std::uint16_t thread, std::uint64_t whenMs,
               void* const* frames, int count, const char* stackNote) noexcept {
    w.str(XPG_ANALYZER_NAME " " XPG_ANALYZER_VERSION " - rapport de ");
    w.line(kindWord(kind));
    w.line("=================================================");
    w.str("Genre       : "); w.line(kindWord(kind));
    w.line("Version     : " XPG_ANALYZER_NAME " " XPG_ANALYZER_VERSION);
    w.str("Syst\xC3\xA8me     : "); w.line(g_system);
    char when[24];
    trail::formatDateTime(whenMs, when);
    w.str("Heure       : "); w.str(when); w.line(" (heure locale)");
    w.str("Raison      : "); w.line(reason ? reason : "?");
    char name[trail::kThreadNameMax];
    trail::threadName(thread, name, sizeof name);
    w.str("Fil         : #"); w.num(thread);
    if (name[0]) { w.put(' '); w.str(name); }
    w.put('\n');
    w.str("Historique  : "); w.num(trail::written()); w.line(" entr\xC3\xA9" "es \xC3\xA9" "crites depuis le lancement");

    w.title(kind == ReportKind::Hang ? "Pile d'appels du fil principal" : "Pile d'appels");
    if (stackNote && *stackNote) w.line(stackNote);
    for (int i = 0; i < count; ++i) writeFrame(w, i, frames[i]);
    if (count == 0) w.line("(pile non lue)");

    w.title("Port\xC3\xA9" "es ouvertes (la pile des appels suivis, par fil)");
    writeScopes(w);

    w.title("Documents ouverts");
    writeDocuments(w);

    w.title("Les 2 000 derni\xC3\xA8res entr\xC3\xA9" "es de l'historique interne");
    writeTrail(w, kTrailInReport);
    w.line();
    w.line("(fin du rapport)");
}

// Ouvrir le fichier du rapport ; `utf8` recoit le chemin. Faux si impossible.
bool openReport(Writer& w, ReportKind kind, std::uint64_t whenMs, char* utf8, std::size_t size) noexcept {
    char name[96];
    reportName(name, sizeof name, kindWord(kind), whenMs, ".txt");
    std::snprintf(utf8, size, "%s%c%s", g_folder,
#if defined(_WIN32)
                  '\\',
#else
                  '/',
#endif
                  name);
    w.n = 0;
#if defined(_WIN32)
    wchar_t path[1200];
    int k = 0;
    for (const wchar_t* p = g_folderW; *p && k < 1100; ++p) path[k++] = *p;
    path[k++] = L'\\';
    for (const char* p = name; *p && k < 1190; ++p) path[k++] = static_cast<wchar_t>(*p);
    path[k] = L'\0';
    w.h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
    w.fd = ::open(utf8, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
#endif
    if (w.ok()) {
        // L'UTF-8 se lit bien dans le Bloc-notes avec sa marque.
        w.str("\xEF\xBB\xBF");
    }
    return w.ok();
}

#if defined(_WIN32)
// Le minidump, a cote du rapport (meme nom, .dmp).
void writeMiniDump(const char* reportUtf8, EXCEPTION_POINTERS* ep, DWORD threadId) noexcept {
    if (!g_dbg.miniDumpWriteDump) return;
    wchar_t path[1200];
    const int k = MultiByteToWideChar(CP_UTF8, 0, reportUtf8, -1, path, 1190);
    if (k < 5) return;
    std::wcscpy(path + k - 5, L".dmp");
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = threadId;
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;
    EnterCriticalSection(&g_dbgLock);
    g_dbg.miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h,
                            static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                            ep ? &mei : nullptr, nullptr, nullptr);
    LeaveCriticalSection(&g_dbgLock);
    CloseHandle(h);
}

void showCrashBox(const char* reportUtf8) noexcept {
    BoxOpen open;
    if (auto box = g_crashBox.load()) { box(reportUtf8); return; }
    if (!g_dialogs || !g_messageBox) return;
    static wchar_t text[2048];
    const wchar_t* head = L"XPGAnalyser a rencontré un problème et doit se fermer.\n\n"
                          L"Un rapport a été enregistré :\n";
    std::wcscpy(text, head);
    const std::size_t k = std::wcslen(text);
    if (!MultiByteToWideChar(CP_UTF8, 0, reportUtf8, -1, text + k, static_cast<int>(2040 - k))) text[k] = L'\0';
    g_messageBox(nullptr, text, L"XPGAnalyser", MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST | MB_TASKMODAL);
}

// ---- le rapporteur (cree a l'installation) ----
struct Job {
    ReportKind kind = ReportKind::Crash;
    char reason[512] = {};
    EXCEPTION_POINTERS* ep = nullptr;
    DWORD threadId = 0;
    HANDLE thread = nullptr;
    std::uint16_t trailThread = 0;
    void* frames[kMaxFrames] = {};
    int count = 0;              // deja pris (RtlCaptureStackBackTrace) ; sinon depuis ep
    char path[1200] = {};
};
Job g_job;
HANDLE g_jobRequest = nullptr, g_jobDone = nullptr;

DWORD WINAPI reporterThread(LPVOID) {
    trail::nameThread("rapporteur");
    for (;;) {
        if (WaitForSingleObject(g_jobRequest, INFINITE) != WAIT_OBJECT_0) return 0;
        Job& j = g_job;
        if (j.count == 0 && j.ep && j.ep->ContextRecord)
            j.count = walkContext(*j.ep->ContextRecord, j.thread ? j.thread : GetCurrentThread(), j.frames, kMaxFrames);
        const std::uint64_t now = trail::nowMs();
        if (openReport(g_writer, j.kind, now, j.path, sizeof j.path)) {
            writeBody(g_writer, j.kind, j.reason, j.trailThread, now, j.frames, j.count, nullptr);
            g_writer.close();
            writeMiniDump(j.path, j.ep, j.threadId);
        } else {
            j.path[0] = '\0';
        }
        showCrashBox(j.path[0] ? j.path : "(le rapport n'a pas pu \xC3\xAA" "tre \xC3\xA9" "crit)");
        SetEvent(g_jobDone);
    }
}

// Depuis le fil qui plante : confier le rapport au rapporteur et l'attendre.
void reportFromHere(const char* reason, EXCEPTION_POINTERS* ep, int skipFrames) noexcept {
    Job& j = g_job;
    j.kind = ReportKind::Crash;
    std::snprintf(j.reason, sizeof j.reason, "%s", reason);
    j.ep = ep;
    j.threadId = GetCurrentThreadId();
    j.thread = OpenThread(THREAD_ALL_ACCESS, FALSE, j.threadId);
    j.trailThread = trail::currentThread();
    j.count = ep ? 0 : RtlCaptureStackBackTrace(static_cast<DWORD>(skipFrames), kMaxFrames, j.frames, nullptr);
    trail::note(trail::Kind::Crash, reason);
    if (g_jobRequest && g_jobDone) {
        SetEvent(g_jobRequest);
        WaitForSingleObject(g_jobDone, 120000);
    }
}

void describeException(const EXCEPTION_RECORD* r, char* out, std::size_t size) noexcept {
    const DWORD code = r ? r->ExceptionCode : 0;
    const char* what = "exception";
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:      what = "EXCEPTION_ACCESS_VIOLATION (acc\xC3\xA8s m\xC3\xA9moire invalide)"; break;
        case EXCEPTION_STACK_OVERFLOW:        what = "EXCEPTION_STACK_OVERFLOW (d\xC3\xA9" "bordement de pile)"; break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    what = "EXCEPTION_INT_DIVIDE_BY_ZERO (division enti\xC3\xA8re par z\xC3\xA9ro)"; break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:   what = "EXCEPTION_ILLEGAL_INSTRUCTION (instruction ill\xC3\xA9gale)"; break;
        case EXCEPTION_PRIV_INSTRUCTION:      what = "EXCEPTION_PRIV_INSTRUCTION"; break;
        case EXCEPTION_IN_PAGE_ERROR:         what = "EXCEPTION_IN_PAGE_ERROR"; break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: what = "EXCEPTION_ARRAY_BOUNDS_EXCEEDED"; break;
        case EXCEPTION_DATATYPE_MISALIGNMENT: what = "EXCEPTION_DATATYPE_MISALIGNMENT"; break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:    what = "EXCEPTION_FLT_DIVIDE_BY_ZERO"; break;
        case 0xE06D7363:                      what = "exception C++ (MSVC) non attrap\xC3\xA9" "e"; break;
        case 0xC0000409:                      what = "STATUS_STACK_BUFFER_OVERRUN (fast fail)"; break;
        default: break;
    }
    const auto address = reinterpret_cast<std::uint64_t>(r ? r->ExceptionAddress : nullptr);
    HMODULE module = nullptr;
    char where[MAX_PATH * 3] = "?";
    std::uint64_t offset = address;
    if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      reinterpret_cast<LPCWSTR>(address), &module) && module) {
        wchar_t path[MAX_PATH];
        const DWORD len = GetModuleFileNameW(module, path, MAX_PATH);
        const wchar_t* base = path;
        for (DWORD i = 0; i < len; ++i) if (path[i] == L'\\') base = path + i + 1;
        WideCharToMultiByte(CP_UTF8, 0, base, -1, where, sizeof where, nullptr, nullptr);
        offset = address - reinterpret_cast<std::uint64_t>(module);
    }
    int n = std::snprintf(out, size, "%s, code 0x%08lX, adresse 0x%llX (%s+0x%llX)", what,
                          static_cast<unsigned long>(code), static_cast<unsigned long long>(address), where,
                          static_cast<unsigned long long>(offset));
    if (code == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2 && n > 0 && static_cast<std::size_t>(n) < size) {
        const ULONG_PTR op = r->ExceptionInformation[0];
        std::snprintf(out + n, size - static_cast<std::size_t>(n), ", %s de 0x%llX",
                      op == 0 ? "lecture" : op == 1 ? "\xC3\xA9" "criture" : "ex\xC3\xA9" "cution",
                      static_cast<unsigned long long>(r->ExceptionInformation[1]));
    }
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* ep) {
    if (g_handling.exchange(1)) return EXCEPTION_CONTINUE_SEARCH;
    char reason[512];
    describeException(ep ? ep->ExceptionRecord : nullptr, reason, sizeof reason);
    reportFromHere(reason, ep, 0);
    return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void fatalHere(const char* reason) noexcept {
    if (!g_handling.exchange(1)) reportFromHere(reason, nullptr, 2);
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) Sleep(1000);
}

void __cdecl onPurecall() { fatalHere("appel d'une fonction virtuelle pure (purecall)"); }
void __cdecl onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    fatalHere("param\xC3\xA8tre invalide pass\xC3\xA9 \xC3\xA0 la biblioth\xC3\xA8que C (invalid parameter)");
}
void __cdecl onAbortSignal(int) { fatalHere("abort() (SIGABRT)"); }

#else  // ---- Linux ----------------------------------------------------------------------

alignas(16) char g_altStack[64 * 1024];

void stderrLine(const char* a, const char* b = "") noexcept {
    (void)!::write(2, a, std::strlen(a));
    (void)!::write(2, b, std::strlen(b));
    (void)!::write(2, "\n", 1);
}

void reportHere(const char* reason) noexcept {
    trail::note(trail::Kind::Crash, reason);
    void* frames[kMaxFrames];
    const int count = backtrace(frames, kMaxFrames);
    static char path[1200];
    const std::uint64_t now = trail::nowMs();
    if (openReport(g_writer, ReportKind::Crash, now, path, sizeof path)) {
        writeBody(g_writer, ReportKind::Crash, reason, trail::currentThread(), now, frames, count, nullptr);
        g_writer.close();
        stderrLine("XPGAnalyser a rencontr\xC3\xA9 un probl\xC3\xA8me et doit se fermer. Un rapport a \xC3\xA9t\xC3\xA9 enregistr\xC3\xA9 : ", path);
    } else {
        stderrLine("XPGAnalyser a rencontr\xC3\xA9 un probl\xC3\xA8me ; le rapport n'a pas pu \xC3\xAAtre \xC3\xA9" "crit dans ", g_folder);
        path[0] = '\0';
    }
    if (auto box = g_crashBox.load()) {   // pas de boite native ici ; celle d'un essai
        BoxOpen open;
        box(path[0] ? path : "(le rapport n'a pas pu \xC3\xAA" "tre \xC3\xA9" "crit)");
    }
}

const char* signalText(int sig) noexcept {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (acc\xC3\xA8s m\xC3\xA9moire invalide)";
        case SIGABRT: return "SIGABRT (abort)";
        case SIGFPE:  return "SIGFPE (erreur arithm\xC3\xA9tique)";
        case SIGILL:  return "SIGILL (instruction ill\xC3\xA9gale)";
        case SIGBUS:  return "SIGBUS (erreur de bus)";
        default:      return "signal";
    }
}

void onFatalSignal(int sig, siginfo_t* info, void*) {
    if (!g_handling.exchange(1)) {
        char reason[256];
        std::snprintf(reason, sizeof reason, "%s, adresse 0x%llX", signalText(sig),
                      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(info ? info->si_addr : nullptr)));
        reportHere(reason);
    }
    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
    raise(sig);
}

void onProbe(int) {
    g_probeCount.store(backtrace(g_probeFrames, kMaxFrames), std::memory_order_relaxed);
    g_probeDone.store(1, std::memory_order_release);
}
#endif

[[noreturn]] void onTerminate() {
    char reason[512] = "std::terminate";
    if (auto ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            std::snprintf(reason, sizeof reason, "exception C++ non attrap\xC3\xA9" "e : %s", e.what());
        } catch (...) {
            std::snprintf(reason, sizeof reason, "exception C++ non attrap\xC3\xA9" "e (d'un type inconnu)");
        }
    }
#if defined(_WIN32)
    fatalHere(reason);
#else
    if (!g_handling.exchange(1)) reportHere(reason);
    _exit(3);
#endif
}

// ---- le rapport de blocage (depuis le chien de garde) -----------------------------------
std::string writeHangReport(double seconds) {
    void* frames[kMaxFrames];
    int count = 0;
    const char* stackNote = nullptr;
#if defined(_WIN32)
    if (g_mainThreadHandle && SuspendThread(g_mainThreadHandle) != static_cast<DWORD>(-1)) {
        alignas(16) CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(g_mainThreadHandle, &ctx)) {
#  if defined(_M_X64) || defined(__x86_64__)
            count = unwindSuspended(ctx, frames, kMaxFrames);
#  else
            count = walkContext(ctx, g_mainThreadHandle, frames, kMaxFrames);
#  endif
        }
        ResumeThread(g_mainThreadHandle);
    } else {
        stackNote = "(le fil principal n'a pas pu \xC3\xAAtre suspendu)";
    }
#else
    g_probeDone.store(0, std::memory_order_relaxed);
    if (pthread_kill(g_mainPthread, SIGUSR2) == 0) {
        for (int i = 0; i < 100 && !g_probeDone.load(std::memory_order_acquire); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (g_probeDone.load(std::memory_order_acquire)) {
            count = g_probeCount.load(std::memory_order_relaxed);
            std::memcpy(frames, g_probeFrames, sizeof(void*) * static_cast<std::size_t>(std::clamp(count, 0, kMaxFrames)));
        } else {
            stackNote = "(le fil principal n'a pas r\xC3\xA9pondu au signal)";
        }
    }
#endif
    char reason[160];
    std::snprintf(reason, sizeof reason, "blocage : la boucle principale ne bat plus depuis %.1f s", seconds);
    for (char* p = reason; *p; ++p) if (*p == '.') *p = ',';
    trail::note(trail::Kind::Watchdog, reason);
    static Writer writer;   // pas g_writer : un plantage peut survenir pendant
    char path[1200];
    const std::uint64_t now = trail::nowMs();
    if (!openReport(writer, ReportKind::Hang, now, path, sizeof path)) return {};
    writeBody(writer, ReportKind::Hang, reason, g_mainThread, now, frames, count, stackNote);
    writer.close();
#if defined(_WIN32)
    writeMiniDump(path, nullptr, 0);
#endif
    return path;
}

bool showHangDialog(const std::string& path, unsigned seconds) {
    char message[1600];
    std::snprintf(message, sizeof message,
                  "XPGAnalyser ne r\xC3\xA9pond plus depuis %u s.\n\nUn rapport a \xC3\xA9t\xC3\xA9 enregistr\xC3\xA9 :\n%s",
                  seconds, path.c_str());
    BoxOpen open;
    if (auto dialog = g_hangDialog.load()) return dialog("XPGAnalyser ne r\xC3\xA9pond plus", message);
#if defined(_WIN32)
    if (!g_messageBox) return false;
    std::string full = message;
    full += "\n\nOui : attendre.   Non : fermer XPGAnalyser.";
    wchar_t text[2400];
    if (!MultiByteToWideChar(CP_UTF8, 0, full.c_str(), -1, text, 2400)) return false;
    return g_messageBox(nullptr, text, L"XPGAnalyser ne répond plus",
                        MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST) == IDNO;
#else
    std::fprintf(stderr, "%s\n", message);
    return false;
#endif
}

void watchdogLoop(unsigned seconds, bool dialog) {
    trail::nameThread("chien de garde");
    const std::uint64_t limit = static_cast<std::uint64_t>(seconds) * 1000;
    std::uint64_t reportedBeat = 0;     // le battement d'avant le blocage rapporte
    int ticks = 0;
    std::unique_lock lock(g_wdMutex);
    while (!g_wdStop) {
        g_wdCv.wait_for(lock, std::chrono::milliseconds(250));
        if (g_wdStop) break;
        if (++ticks % 240 == 0) trail::refreshLocalOffset();
        const std::uint64_t beat = g_lastBeatMs.load(std::memory_order_acquire);
        if (beat == 0) continue;
        const std::uint64_t now = trail::nowMs();
        if (reportedBeat != 0) {
            if (beat != reportedBeat) {
                // La duree vraie de l'arret : l'ecart vu par le premier battement d'apres.
                std::uint64_t gap = g_lastGapMs.exchange(0, std::memory_order_acq_rel);
                if (gap == 0) gap = beat - reportedBeat;
                const double stopped = static_cast<double>(gap) / 1000.0;
                g_recoveredMs.store(gap, std::memory_order_relaxed);
                g_recovered.store(true, std::memory_order_release);
                trail::notef(trail::Kind::Watchdog, "la boucle principale repart apr\xC3\xA8s %.1f s", stopped);
                reportedBeat = 0;
            }
            continue;
        }
        // Une boite ouverte, ou un plantage en cours (le rapport, le minidump, sa boite :
        // le fil qui plante ne bat plus) : rien a dire.
        if (g_hold.load(std::memory_order_relaxed) > 0 || g_handling.load(std::memory_order_acquire) != 0) continue;
        if (now > beat && now - beat >= limit) {
            reportedBeat = beat;
            lock.unlock();
            const std::string path = writeHangReport(static_cast<double>(now - beat) / 1000.0);
            {
                std::lock_guard g(g_lastHangMutex);
                g_lastHang = path;
            }
            if (dialog && g_dialogs && showHangDialog(path, seconds)) {
                trail::note(trail::Kind::Watchdog, "Fermer XPGAnalyser (fen\xC3\xAAtre de blocage)");
#if defined(_WIN32)
                TerminateProcess(GetCurrentProcess(), 4);
#else
                _exit(4);
#endif
            }
            lock.lock();
        }
    }
}

void removeOldReports(const fs::path& folder) {
    std::error_code ec;
    std::vector<fs::path> reports;
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        const auto name = e.path().filename().string();
        if (e.path().extension() == ".txt" && (name.rfind("plantage-", 0) == 0 || name.rfind("blocage-", 0) == 0))
            reports.push_back(e.path());
    }
    if (static_cast<int>(reports.size()) <= kReportsKept) return;
    auto date = [](const fs::path& p) { const auto s = p.filename().string(); return s.substr(s.find('-') + 1); };
    std::sort(reports.begin(), reports.end(), [&](const fs::path& a, const fs::path& b) { return date(a) < date(b); });
    for (std::size_t i = 0; i + kReportsKept < reports.size(); ++i) {
        fs::remove(reports[i], ec);
        auto dump = reports[i];
        dump.replace_extension(".dmp");
        fs::remove(dump, ec);
    }
}

[[gnu::noinline]] void provokeCrash() {
    XPG_PORTEE("essai::plantage");
    trail::note(trail::Kind::Info, "--essai-plantage : ecriture a l'adresse 0");
    volatile int* nowhere = nullptr;
    *nowhere = 42;
}

[[gnu::noinline]] void provokeHang() {
    XPG_PORTEE("essai::blocage");
    trail::note(trail::Kind::Info, "--essai-blocage : la boucle principale s'arrete 12 s");
    std::this_thread::sleep_for(std::chrono::seconds(12));
}

} // namespace

void install(const Options& options) {
    if (g_installed.exchange(true)) return;
    g_dialogs = options.dialogs;
    trail::nameThread("principal");
    g_mainThread = trail::currentThread();
    trail::refreshLocalOffset();

    std::error_code ec;
    const fs::path folder = pathFromUtf8(options.folder);
    fs::create_directories(folder, ec);
    std::snprintf(g_folder, sizeof g_folder, "%s", options.folder.c_str());
    removeOldReports(folder);

#if defined(_WIN32)
    MultiByteToWideChar(CP_UTF8, 0, g_folder, -1, g_folderW, 1024);
    if (HMODULE user32 = LoadLibraryW(L"user32.dll")) load(g_messageBox, user32, "MessageBoxW");
    loadDbgHelp();
    // Le systeme : RtlGetVersion (GetVersionEx ment depuis Windows 8.1), et wine s'il est la.
    {
        RTL_OSVERSIONINFOW v{};
        v.dwOSVersionInfoSize = sizeof v;
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        using RtlGetVersion_t = LONG(WINAPI*)(RTL_OSVERSIONINFOW*);
        RtlGetVersion_t getVersion = nullptr;
        using WineVersion_t = const char*(CDECL*)();
        WineVersion_t wine = nullptr;
        if (ntdll) { load(getVersion, ntdll, "RtlGetVersion"); load(wine, ntdll, "wine_get_version"); }
        if (getVersion) getVersion(&v);
        std::snprintf(g_system, sizeof g_system, "Windows %lu.%lu (build %lu)%s%s%s",
                      static_cast<unsigned long>(v.dwMajorVersion), static_cast<unsigned long>(v.dwMinorVersion),
                      static_cast<unsigned long>(v.dwBuildNumber),
#  if defined(_M_X64) || defined(__x86_64__)
                      ", 64 bits",
#  else
                      ", 32 bits",
#  endif
                      wine ? ", sous wine " : "", wine ? wine() : "");
    }
    // Le fil principal, pour le chien de garde (une vraie poignee, pas GetCurrentThread()).
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_mainThreadHandle,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    {
        using Limits_t = VOID(WINAPI*)(PULONG_PTR, PULONG_PTR);
        Limits_t limits = nullptr;
        if (HMODULE k32 = GetModuleHandleW(L"kernel32.dll")) load(limits, k32, "GetCurrentThreadStackLimits");
        if (limits) limits(&g_mainStackLow, &g_mainStackHigh);
    }
    g_jobRequest = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_jobDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (HANDLE t = CreateThread(nullptr, 256 * 1024, reporterThread, nullptr, 0, nullptr)) CloseHandle(t);

    SetUnhandledExceptionFilter(onUnhandledException);
    std::signal(SIGABRT, onAbortSignal);
    // La CRT : l'appel virtuel pur et le parametre invalide (MSVC, ou la CRT chargee).
    using InvalidParam_t = void(__cdecl*)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t);
    using SetInvalidParam_t = InvalidParam_t(__cdecl*)(InvalidParam_t);
    using Purecall_t = void(__cdecl*)();
    using SetPurecall_t = Purecall_t(__cdecl*)(Purecall_t);
    for (const wchar_t* dll : {L"ucrtbase.dll", L"ucrtbased.dll", L"msvcrt.dll"}) {
        HMODULE m = GetModuleHandleW(dll);
        if (!m) continue;
        SetInvalidParam_t setInvalid = nullptr;
        SetPurecall_t setPurecall = nullptr;
        load(setInvalid, m, "_set_invalid_parameter_handler");
        load(setPurecall, m, "_set_purecall_handler");
        if (setInvalid) setInvalid(onInvalidParameter);
        if (setPurecall) setPurecall(onPurecall);
    }
#  if defined(_MSC_VER)
    _set_invalid_parameter_handler(onInvalidParameter);
    _set_purecall_handler(onPurecall);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#  endif
#else
    {
        utsname u{};
        if (uname(&u) == 0) std::snprintf(g_system, sizeof g_system, "%s %s, %s", u.sysname, u.release, u.machine);
        else std::snprintf(g_system, sizeof g_system, "Linux");
    }
    g_mainPthread = pthread_self();
    // backtrace() charge libgcc a son premier appel (il alloue) : le faire maintenant.
    void* warm[2];
    (void)backtrace(warm, 2);
    stack_t ss{};
    ss.ss_sp = g_altStack;
    ss.ss_size = sizeof g_altStack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = onFatalSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) sigaction(sig, &sa, nullptr);
    struct sigaction probe{};
    probe.sa_handler = onProbe;
    probe.sa_flags = SA_RESTART;
    sigemptyset(&probe.sa_mask);
    sigaction(SIGUSR2, &probe, nullptr);
#endif
    std::set_terminate(onTerminate);
    trail::notef(trail::Kind::Info, "%s %s : rapports de plantage dans %s", XPG_ANALYZER_NAME, XPG_ANALYZER_VERSION, g_folder);
}

bool installed() noexcept { return g_installed.load(); }
std::string folder() { return g_folder; }

void setOpenDocuments(std::string_view names) noexcept {
    g_docsBusy.store(1, std::memory_order_release);
    const std::size_t n = std::min(names.size(), sizeof g_docs - 1);
    std::memcpy(g_docs, names.data(), n);
    g_docs[n] = '\0';
    g_docsBusy.store(0, std::memory_order_release);
}

void setHangDialog(HangDialog dialog) noexcept { g_hangDialog.store(dialog); }
void setCrashBox(CrashBox box) noexcept { g_crashBox.store(box); }

void heartbeat() noexcept {
    const std::uint64_t now = trail::nowMs();
    const std::uint64_t previous = g_lastBeatMs.exchange(now, std::memory_order_acq_rel);
    if (previous != 0 && now > previous + 1000) g_lastGapMs.store(now - previous, std::memory_order_release);
    const int test = g_test.load(std::memory_order_relaxed);
    if (test != 0 && now >= g_testAt.load(std::memory_order_relaxed)) {
        g_test.store(0);
        if (test == static_cast<int>(Test::Crash)) provokeCrash();
        else if (test == static_cast<int>(Test::Hang)) provokeHang();
    }
}

void startWatchdog(unsigned seconds, bool dialog) {
    if (g_watchdog.joinable()) return;
    {
        std::lock_guard g(g_wdMutex);
        g_wdStop = false;
    }
    g_watchdog = std::thread(watchdogLoop, seconds ? seconds : 8, dialog);
}

void stopWatchdog() {
    if (!g_watchdog.joinable()) return;
    {
        std::lock_guard g(g_wdMutex);
        g_wdStop = true;
    }
    g_wdCv.notify_all();
    g_watchdog.join();
}

void holdWatchdog(bool hold) noexcept {
    if (hold) g_hold.fetch_add(1, std::memory_order_relaxed);
    else if (g_hold.fetch_sub(1, std::memory_order_relaxed) <= 0) g_hold.store(0);
    g_lastBeatMs.store(trail::nowMs(), std::memory_order_release);
}

bool takeRecovery(double& seconds) noexcept {
    if (!g_recovered.exchange(false, std::memory_order_acq_rel)) return false;
    seconds = static_cast<double>(g_recoveredMs.load(std::memory_order_relaxed)) / 1000.0;
    return true;
}

std::string lastHangReport() {
    std::lock_guard g(g_lastHangMutex);
    return g_lastHang;
}

std::string writeReportNow(ReportKind kind, const char* reason) {
    if (!g_folder[0]) return {};
    void* frames[kMaxFrames];
    int count = 0;
#if defined(_WIN32)
    count = RtlCaptureStackBackTrace(1, kMaxFrames, frames, nullptr);
#else
    count = backtrace(frames, kMaxFrames);
#endif
    static std::mutex m;
    std::lock_guard g(m);
    static Writer writer;
    char path[1200];
    const std::uint64_t now = trail::nowMs();
    if (!openReport(writer, kind, now, path, sizeof path)) return {};
    writeBody(writer, kind, reason, trail::currentThread(), now, frames, count, nullptr);
    writer.close();
    return path;
}

std::optional<PendingReport> takeNewReport() {
    if (!g_folder[0]) return std::nullopt;
    const fs::path folder = pathFromUtf8(g_folder);
    std::error_code ec;
    fs::path newest;
    std::string newestDate;
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        const auto name = e.path().filename().string();
        if (e.path().extension() != ".txt") continue;
        if (name.rfind("plantage-", 0) != 0 && name.rfind("blocage-", 0) != 0) continue;
        const auto date = name.substr(name.find('-') + 1);
        if (date > newestDate) { newestDate = date; newest = e.path(); }
    }
    if (newest.empty()) return std::nullopt;
    const fs::path marker = folder / "dernier-montre.txt";
    std::string seen;
    { std::ifstream in(marker); std::getline(in, seen); }
    if (!seen.empty() && newestDate <= seen) return std::nullopt;
    { std::ofstream out(marker, std::ios::trunc); out << newestDate << "\n"; }

    PendingReport r;
    r.path = utf8Of(newest);
    const auto name = newest.filename().string();
    r.kind = name.substr(0, name.find('-'));
    // 2026-10-02_13-05-11.txt -> « 02/10/2026 a 13:05 »
    if (newestDate.size() >= 16)
        r.when = newestDate.substr(8, 2) + "/" + newestDate.substr(5, 2) + "/" + newestDate.substr(0, 4) + " \xC3\xA0 "
               + newestDate.substr(11, 2) + ":" + newestDate.substr(14, 2);
    std::ifstream in(newest, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    r.text = text.str();
    if (r.text.rfind("\xEF\xBB\xBF", 0) == 0) r.text.erase(0, 3);
    return r;
}

void armTest(Test test, unsigned afterMs) noexcept {
    g_testAt.store(trail::nowMs() + afterMs, std::memory_order_relaxed);
    g_test.store(static_cast<int>(test), std::memory_order_relaxed);
}

} // namespace core::crash
