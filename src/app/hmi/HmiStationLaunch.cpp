// app/hmi/HmiStationLaunch.cpp - lancer le poste d'exploitation (lot 14).
#include "HmiStationLaunch.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "advapi32.lib")
#  endif
#endif

namespace app::station {

namespace fs = std::filesystem;

namespace {

// Entre guillemets doubles pour sh (guillemets, dollars, accents graves et
// barres obliques inverses echappes).
std::string shQuoted(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '$' || c == '`' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

// Entre guillemets doubles pour Windows (un chemin ne contient pas de ").
std::string winQuoted(const std::string& s) { return "\"" + s + "\""; }

// Un argument de la ligne Exec d'un .desktop : entre guillemets, avec les
// guillemets, accents graves, dollars et barres obliques inverses echappes ;
// puis la regle des chaines du fichier double chaque barre oblique inverse.
std::string desktopArg(const std::string& s) {
    std::string quoted = "\"";
    for (const char c : s) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') quoted += '\\';
        quoted += c;
    }
    quoted += '"';
    std::string out;
    for (const char c : quoted) {
        if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

#ifdef _WIN32
const char* const kRunKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring widen(const std::string& s, unsigned codePage) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(codePage, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(codePage, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w, unsigned codePage) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(codePage, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(codePage, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string systemMessage(long code) {
    char* text = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(code), 0,
                   reinterpret_cast<char*>(&text), 0, nullptr);
    std::string m = text ? text : "";
    if (text) LocalFree(text);
    while (!m.empty() && (m.back() == '\n' || m.back() == '\r' || m.back() == ' ' || m.back() == '.')) m.pop_back();
    return m.empty() ? "erreur " + std::to_string(code) : m;
}
#else
fs::path autostartFolder() {
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return fs::path(x) / "autostart";
    if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h) / ".config" / "autostart";
    return {};
}
#endif

} // namespace

std::string currentExecutable() {
#ifdef _WIN32
    std::vector<char> buf(32768, '\0');
    const DWORD n = GetModuleFileNameA(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    return n > 0 && n < buf.size() ? std::string(buf.data(), n) : std::string{};
#else
    std::error_code ec;
    const auto p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string{} : p.string();
#endif
}

std::string commandLine(const std::string& exe, const std::string& projectFolder, bool windows) {
    return windows ? winQuoted(exe) + " --ihm " + winQuoted(projectFolder) : shQuoted(exe) + " --ihm " + shQuoted(projectFolder);
}

std::string launcherName(bool windows) { return windows ? "lancer-poste.cmd" : "lancer-poste.sh"; }

std::string launcherText(const std::string& exe, const std::string& projectName, bool windows) {
    std::ostringstream o;
    if (windows) {
        // CRLF : le format des fichiers de commandes. chcp 65001 : le chemin du
        // programme est ecrit en UTF-8.
        o << "@echo off\r\n"
          << "rem Poste d'exploitation - " << projectName << " (xpg_analyzer)\r\n"
          << "rem L'IHM seule, en plein ecran. Le projet est le dossier de ce fichier :\r\n"
          << "rem copie avec lui, il marche encore. Ctrl+Alt+Q pour en sortir.\r\n"
          << "chcp 65001 >nul\r\n"
          << "start \"\" " << winQuoted(exe) << " --ihm \"%~dp0.\"\r\n";
    } else {
        o << "#!/bin/sh\n"
          << "# Poste d'exploitation - " << projectName << " (xpg_analyzer)\n"
          << "# L'IHM seule, en plein ecran. Le projet est le dossier de ce fichier :\n"
          << "# copie avec lui, il marche encore. Ctrl+Alt+Q pour en sortir.\n"
          << "cd \"$(dirname \"$0\")\" || exit 1\n"
          << "exec " << shQuoted(exe) << " --ihm \"$(pwd)\" \"$@\"\n";
    }
    return o.str();
}

bool writeLauncher(const std::string& exe, const std::string& projectFolder, const std::string& projectName, std::string* where,
                   std::string* why) {
    const auto fail = [&](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    if (projectFolder.empty()) return fail("le projet n'a pas de dossier : enregistrez-le d'abord");
    if (exe.empty()) return fail("le chemin du programme est inconnu");
    std::string text;
#ifdef _WIN32
    text = launcherText(narrow(widen(exe, CP_ACP), CP_UTF8), projectName, true);
#else
    text = launcherText(exe, projectName, false);
#endif
    const fs::path file = fs::path(projectFolder) / launcherName();
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out) return fail("\xC3\xA9" "criture impossible : " + file.string());
        out << text;
        if (!out) return fail("\xC3\xA9" "criture impossible : " + file.string());
    }
#ifndef _WIN32
    std::error_code ec;
    fs::permissions(file, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#endif
    if (where) *where = file.string();
    return true;
}

std::string autostartName(const std::string& projectName) {
    (void)projectName;
    return "XPG Poste d'exploitation";
}

std::string autostartFile(const std::string& projectName) {
    (void)projectName;
#ifdef _WIN32
    return {};
#else
    const auto folder = autostartFolder();
    return folder.empty() ? std::string{} : (folder / "xpg-poste.desktop").string();
#endif
}

std::string folderOfCommand(const std::string& command) {
    const auto at = command.find("--ihm");
    if (at == std::string::npos) return {};
    std::string rest = command.substr(at + 5);
    while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
    while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\r' || rest.back() == '\n')) rest.pop_back();
    if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') rest = rest.substr(1, rest.size() - 2);
#ifndef _WIN32
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
#else
    return rest;
#endif
}

std::string autostartProject(std::string* where) {
    std::string command;
    if (!autostartEnabled({}, &command, where)) return {};
    return folderOfCommand(command);
}

std::string desktopEntry(const std::string& projectName, const std::string& exe, const std::string& projectFolder) {
    std::ostringstream o;
    o << "[Desktop Entry]\n"
      << "Type=Application\n"
      << "Name=" << autostartName(projectName) << "\n"
      << "Comment=Poste d'exploitation : l'IHM seule, en plein ecran (xpg_analyzer) - un projet par PC\n"
      << "Exec=" << desktopArg(exe) << " --ihm " << desktopArg(projectFolder) << "\n"
      << "Terminal=false\n"
      << "X-GNOME-Autostart-enabled=true\n";
    return o.str();
}

bool autostartEnabled(const std::string& projectName, std::string* command, std::string* where) {
#ifdef _WIN32
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    const std::wstring name = widen(autostartName(projectName), CP_ACP);
    DWORD type = 0, bytes = 0;
    bool found = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS && type == REG_SZ;
    if (found && command) {
        std::wstring value(bytes / sizeof(wchar_t) + 1, L'\0');
        DWORD size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        if (RegQueryValueExW(key, name.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &size) == ERROR_SUCCESS) {
            value.resize(size / sizeof(wchar_t));
            while (!value.empty() && value.back() == L'\0') value.pop_back();
            *command = narrow(value, CP_ACP);
        } else {
            found = false;
        }
    }
    RegCloseKey(key);
    if (found && where) *where = std::string("HKCU\\") + kRunKey + " \xE2\x80\x94 " + autostartName(projectName);
    return found;
#else
    const std::string file = autostartFile(projectName);
    std::error_code ec;
    if (file.empty() || !fs::exists(file, ec)) return false;
    if (command) {
        std::ifstream in(file);
        std::string line;
        command->clear();
        while (std::getline(in, line))
            if (line.rfind("Exec=", 0) == 0) *command = line.substr(5);
    }
    if (where) *where = file;
    return true;
#endif
}

bool setAutostart(const std::string& projectName, const std::string& exe, const std::string& projectFolder, bool on, std::string* where,
                  std::string* why) {
    const auto fail = [&](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    if (on && projectFolder.empty()) return fail("le projet n'a pas de dossier : enregistrez-le d'abord");
    if (on && exe.empty()) return fail("le chemin du programme est inconnu");
    // Retirer : seulement l'entree de CE projet (celle d'un autre reste).
    if (!on) {
        const std::string current = autostartProject();
        std::error_code same;
        if (current.empty()) return true;
        if (!projectFolder.empty() && !fs::equivalent(current, projectFolder, same) && current != projectFolder) {
            if (where) *where = current;
            return true;
        }
    }
#ifdef _WIN32
    HKEY key = nullptr;
    if (const LONG rc = RegCreateKeyExA(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr); rc != ERROR_SUCCESS)
        return fail("cl\xC3\xA9 Run inaccessible : " + systemMessage(rc));
    const std::wstring name = widen(autostartName(projectName), CP_ACP);
    LONG rc = ERROR_SUCCESS;
    // Les entrees du lot 14 (une par projet, "XPG Poste - <projet>") s'effacent.
    {
        std::vector<std::wstring> old;
        for (DWORD i = 0;; ++i) {
            wchar_t valueName[512];
            DWORD len = 512;
            if (RegEnumValueW(key, i, valueName, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            if (std::wstring(valueName, len).rfind(L"XPG Poste - ", 0) == 0) old.emplace_back(valueName, len);
        }
        for (const auto& v : old) RegDeleteValueW(key, v.c_str());
    }
    if (on) {
        const std::wstring value = widen(commandLine(exe, projectFolder, true), CP_ACP);
        rc = RegSetValueExW(key, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, name.c_str());
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) return fail(systemMessage(rc));
    if (where) *where = std::string("HKCU\\") + kRunKey + " \xE2\x80\x94 " + autostartName(projectName);
    return true;
#else
    const std::string file = autostartFile(projectName);
    if (file.empty()) return fail("dossier de d\xC3\xA9marrage introuvable (ni XDG_CONFIG_HOME ni HOME)");
    std::error_code ec;
    // Les fichiers du lot 14 (xpg-poste-<projet>.desktop) s'effacent.
    for (const auto& e : fs::directory_iterator(fs::path(file).parent_path(), ec)) {
        const std::string n = e.path().filename().string();
        if (n.rfind("xpg-poste-", 0) == 0 && e.path().extension() == ".desktop") fs::remove(e.path(), ec);
    }
    ec.clear();
    if (!on) {
        fs::remove(file, ec);
        if (ec) return fail("suppression impossible : " + file + " (" + ec.message() + ")");
        if (where) *where = file;
        return true;
    }
    fs::create_directories(fs::path(file).parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return fail("\xC3\xA9" "criture impossible : " + file);
    out << desktopEntry(projectName, exe, projectFolder);
    if (!out) return fail("\xC3\xA9" "criture impossible : " + file);
    if (where) *where = file;
    return true;
#endif
}

} // namespace app::station
