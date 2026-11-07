// =============================================================================
//  core/SystemOpen.cpp - 1.32.0 : ouvrir avec le programme du systeme (voir SystemOpen.hpp)
// =============================================================================
#include "SystemOpen.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <fcntl.h>
#  include <spawn.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <thread>
#  include <unistd.h>
extern char** environ;
#endif

namespace core {

namespace {

std::filesystem::path P(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

#if defined(_WIN32)
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
#endif

} // namespace

bool launchesCode(std::string_view pathUtf8) {
    // Le nom seul (un point final ou des espaces, que Windows retire, ne cachent pas l'extension).
    std::string name(pathUtf8);
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = lowerAscii(name.substr(dot + 1));
    static const char* const kCode[] = {
        "exe",  "com",  "bat",  "cmd",  "ps1",  "psm1", "psd1", "ps1xml", "psc1", "vbs",     "vbe",      "js",  "jse", "wsf",  "wsh",
        "ws",   "hta",  "msi",  "msp",  "mst",  "scr",  "cpl",  "lnk",    "url",  "website", "pif",      "reg", "chm", "hlp",  "jar",
        "application", "appref-ms", "gadget", "msc", "inf", "sct", "shb", "shs", "library-ms", "settingcontent-ms", "xll", "iqy", "slk",
        "scf",  "vb",   "vba",  "mde",  "ade",  "adp",  "ins",  "isp",    "cer",  "crt",     "der",      "dll", "ocx", "sys",  "drv",
        "msix", "appx", "appinstaller", "diagcab", "mshxml", "grp", "xbap", "desktop", "sh", "bash", "run", "bin", "appimage", "command", "py",
        "pyw",  "pl",   "rb"};
    return std::find(std::begin(kCode), std::end(kCode), ext) != std::end(kCode);
}

std::string openWithSystem(const std::string& pathUtf8) {
    std::error_code ec;
    const auto p = P(pathUtf8);
    if (pathUtf8.empty() || !std::filesystem::exists(p, ec)) return pathUtf8 + " n'existe pas.";
    const bool folder = std::filesystem::is_directory(p, ec);
    if (!folder && launchesCode(pathUtf8))
        return "par s\xC3\xA9" "curit\xC3\xA9, un programme, un script ou un raccourci ne s'ouvre pas d'ici (" + pathUtf8
               + ") : ouvre son dossier et lance-le toi-m\xC3\xAAme si tu lui fais confiance";
#if defined(_WIN32)
    const std::wstring w = wide(pathUtf8);
    const auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (r <= 32) return "Windows n'a pas pu ouvrir " + pathUtf8 + " (code " + std::to_string(static_cast<long long>(r)) + ").";
    return {};
#else
    if (!folder) {
        struct stat st {};
        if (::stat(pathUtf8.c_str(), &st) == 0 && (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0)
            return "par s\xC3\xA9" "curit\xC3\xA9, un fichier ex\xC3\xA9" "cutable ne s'ouvre pas d'ici (" + pathUtf8 + ")";
    }
#  if defined(__APPLE__)
    const char* tool = "open";
#  else
    const char* tool = "xdg-open";
#  endif
    std::string a0 = tool, a1 = pathUtf8;
    char* argv[] = {a0.data(), a1.data(), nullptr};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, tool, &fa, nullptr, argv, environ);   // aucun shell : le chemin est UN argument
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return "aucun programme n'a pu ouvrir " + pathUtf8 + " (" + tool + " manque ?)";
    std::thread([pid] {
        int status = 0;
        (void)waitpid(pid, &status, 0);
    }).detach();
    return {};
#endif
}

void hardenLibrarySearch() noexcept {
#if defined(_WIN32)
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetDllDirectoryW(L"");   // le dossier courant retire de la recherche (les anciens appels sans drapeau)
#endif
}

bool linkBelow(const std::filesystem::path& target, const std::filesystem::path& root) {
    std::size_t skip = 0;
    for (const auto& e : root)
        if (!e.empty()) ++skip;
    std::filesystem::path cur;
    std::size_t at = 0;
    for (const auto& e : target) {
        if (e.empty()) continue;
        cur /= e;
        if (++at <= skip) continue;
#if defined(_WIN32)
        const DWORD a = GetFileAttributesW(cur.c_str());
        if (a == INVALID_FILE_ATTRIBUTES) return false;
        if (a & FILE_ATTRIBUTE_REPARSE_POINT) return true;
#else
        std::error_code ec;
        const auto st = std::filesystem::symlink_status(cur, ec);
        if (ec || !std::filesystem::exists(st)) return false;
        if (std::filesystem::is_symlink(st)) return true;
#endif
    }
    return false;
}

} // namespace core
