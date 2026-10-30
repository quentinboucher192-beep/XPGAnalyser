// =============================================================================
//  app/FileFinder.cpp - 1.14.0 (lot M5) : voir FileFinder.hpp
// =============================================================================
#include "FileFinder.hpp"

#include "../project/ProjectArchive.hpp"   // parse (le manifeste d'une archive), sizeText
#include "../ui/TextSearch.hpp"            // foldForSearch

#include "../../third_party/miniz.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <system_error>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#    include <shellapi.h>
#    include <shlobj.h>
#    include <winioctl.h>
#else
#    include <sys/resource.h>
#    include <sys/syscall.h>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

namespace app::finder {

namespace fs = std::filesystem;

namespace {

// ============================================================ les petits outils ==
std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

fs::path pathOf(std::string_view utf8) {
    const std::u8string u8(utf8.begin(), utf8.end());
    return fs::path(u8);
}

#if !defined(_WIN32)
std::string utf8Of(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}
#endif

#if defined(_WIN32)
constexpr char kSep = '\\';
std::wstring wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string narrow(const wchar_t* w, std::size_t len) {
    if (len == 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), s.data(), n, nullptr, nullptr);
    return s;
}
std::string narrow(const std::wstring& w) { return narrow(w.data(), w.size()); }
std::int64_t unixOf(const FILETIME& ft) {
    const std::uint64_t t = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    if (t < 116444736000000000ull) return 0;
    return static_cast<std::int64_t>((t - 116444736000000000ull) / 10000000ull);
}
#    ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#        define FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000
#    endif
#    ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#        define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#    endif
#else
constexpr char kSep = '/';
#endif

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    const char last = dir.back();
    if (last == '/' || last == '\\') return dir + name;
    return dir + kSep + name;
}

std::string parentOf(const std::string& path) {
    const auto at = path.find_last_of("/\\");
    if (at == std::string::npos) return {};
    if (at == 0) return path.substr(0, 1);
    // "C:\x" -> "C:\" (la racine garde son separateur)
    if (at == 2 && path.size() > 1 && path[1] == ':') return path.substr(0, 3);
    return path.substr(0, at);
}

std::string leafOf(std::string path) {
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) path.pop_back();
    const auto at = path.find_last_of("/\\");
    return at == std::string::npos ? path : path.substr(at + 1);
}

bool isDriveRoot(const std::string& dir) {
#if defined(_WIN32)
    return dir.size() <= 3 && dir.size() >= 2 && dir[1] == ':';
#else
    return dir == "/";
#endif
}

// Ouvrir un fichier en lecture binaire (un chemin UTF-8, meme accentue sous Windows).
std::FILE* openRead(const std::string& path) {
#if defined(_WIN32)
    return _wfopen(wide(path).c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

// Les `max` premiers octets d'un fichier.
std::string readHead(const std::string& path, std::size_t max) {
    std::FILE* f = openRead(path);
    if (!f) return {};
    std::string out(max, '\0');
    const std::size_t n = std::fread(out.data(), 1, max, f);
    std::fclose(f);
    out.resize(n);
    return out;
}

// Une entree texte d'un zip (la table des matieres seule est lue, puis cette entree).
bool zipEntryText(const std::string& path, const char* entry, std::string& out, std::size_t maxBytes) {
    std::FILE* f = openRead(path);
    if (!f) return false;
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    bool ok = false;
    if (mz_zip_reader_init_cfile(&zip, f, 0, 0)) {
        const int at = mz_zip_reader_locate_file(&zip, entry, nullptr, 0);
        mz_zip_archive_file_stat st;
        if (at >= 0 && mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(at), &st) && st.m_uncomp_size <= maxBytes) {
            std::size_t size = 0;
            if (void* data = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(at), &size, 0)) {
                out.assign(static_cast<const char*>(data), size);
                mz_free(data);
                ok = true;
            }
        }
        mz_zip_reader_end(&zip);
    }
    std::fclose(f);
    return ok;
}

// La valeur de `key=` dans une ligne de cles (key="a b" ou key=a) ; vide : absente.
std::string attribute(std::string_view text, std::string_view key) {
    std::size_t from = 0;
    while (from < text.size()) {
        const auto at = text.find(key, from);
        if (at == std::string_view::npos) return {};
        const bool start = at == 0 || text[at - 1] == ' ' || text[at - 1] == '\t' || text[at - 1] == '\n';
        const auto eq = at + key.size();
        if (start && eq < text.size() && text[eq] == '=') {
            std::size_t v = eq + 1;
            if (v < text.size() && text[v] == '"') {
                const auto end = text.find('"', v + 1);
                return std::string(text.substr(v + 1, end == std::string_view::npos ? std::string_view::npos : end - v - 1));
            }
            std::size_t end = v;
            while (end < text.size() && text[end] != ' ' && text[end] != '\r' && text[end] != '\n' && text[end] != '\t') ++end;
            return std::string(text.substr(v, end - v));
        }
        from = at + 1;
    }
    return {};
}

// La valeur d'une ligne "cle = valeur" (le manifeste d'un projet, l'en-tete d'un bloc) ;
// `prefix` : ce qui precede la cle sur la ligne ("#! " dans une macro).
std::string lineValue(std::string_view text, std::string_view key, std::string_view prefix = {}) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto eol = text.find('\n', pos);
        std::string_view line = text.substr(pos, (eol == std::string_view::npos ? text.size() : eol) - pos);
        pos = eol == std::string_view::npos ? text.size() : eol + 1;
        std::size_t a = 0;
        while (a < line.size() && (line[a] == ' ' || line[a] == '\t')) ++a;
        line.remove_prefix(a);
        if (!prefix.empty()) {
            if (line.substr(0, prefix.size()) != prefix) continue;
            line.remove_prefix(prefix.size());
            while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
        }
        if (line.substr(0, key.size()) != key) continue;
        line.remove_prefix(key.size());
        while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
        if (line.empty() || line.front() != '=') continue;
        line.remove_prefix(1);
        while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
        return std::string(line);
    }
    return {};
}

std::string plural(std::uint64_t n, const char* one, const char* many) {
    return thousands(n) + " " + (n > 1 ? many : one);
}

// ================================================================ le catalogue ==
const char* const kGroupProjects = "Projets";
const char* const kGroupExports = "Exports Control Expert";
const char* const kGroupPackages = "Paquets IHM";
const char* const kGroupLibraries = "Biblioth\xC3\xA8ques";
const char* const kGroupOthers = "Autres";

} // namespace

const std::vector<ExtInfo>& catalog() {
    // StartPage.cpp (Ouvrir...), les Importer... des volets, la bibliotheque (libs/),
    // les themes, la galerie des modeles ; .xrec : les enregistrements (1.14.0).
    static const std::vector<ExtInfo> k = {
        {"xpgproj", "Projet (dossier)", kGroupProjects, Studio::Both},
        {"xihm", "Archive IHM Studio", kGroupProjects, Studio::Ihm},
        {"xapi", "Archive API Studio", kGroupProjects, Studio::Api},
        {"xpg", "Export programme", kGroupExports, Studio::Api},
        {"xhw", "Export mat\xC3\xA9riel", kGroupExports, Studio::Api},
        {"xdb", "Export base", kGroupExports, Studio::Api},
        {"xef", "Export XEF", kGroupExports, Studio::Api},
        {"xpgvues", "Vues", kGroupPackages, Studio::Ihm},
        {"xpgsymboles", "Symboles", kGroupPackages, Studio::Ihm},
        {"xpgtypes", "Types IHM", kGroupPackages, Studio::Ihm},
        {"xpgfonctions", "Fonctions IHM", kGroupPackages, Studio::Ihm},
        {"xpgscripts", "Scripts", kGroupPackages, Studio::Ihm},
        {"xpgst", "Op\xC3\xA9rateurs / scripts ST", kGroupPackages, Studio::Ihm},
        {"xpgmodele", "Mod\xC3\xA8le de vue", kGroupPackages, Studio::Ihm},
        {"xpgbib", "Biblioth\xC3\xA8que IHM", kGroupLibraries, Studio::Ihm},
        {"dfb", "Bloc DFB", kGroupLibraries, Studio::Api},
        {"ddt", "Type DDT", kGroupLibraries, Studio::Api},
        {"mac", "Macro", kGroupLibraries, Studio::Api},
        {"xpgtheme", "Th\xC3\xA8me", kGroupOthers, Studio::Both},
        {"xrec", "Enregistrement", kGroupOthers, Studio::Ihm},
    };
    return k;
}

const ExtInfo* info(std::string_view key) {
    for (const auto& e : catalog())
        if (e.key == key) return &e;
    return nullptr;
}

std::vector<std::string_view> groups() {
    std::vector<std::string_view> out;
    for (const auto& e : catalog())
        if (std::find(out.begin(), out.end(), e.group) == out.end()) out.push_back(e.group);
    return out;
}

std::string keyOf(std::string_view fileName) {
    const auto dot = fileName.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= fileName.size()) return {};
    const auto ext = fileName.substr(dot + 1);
    if (ext.size() > 12) return {};
    std::string k = lowerAscii(ext);
    return info(k) ? k : std::string{};
}

bool mine(const ExtInfo& e, core::Edition ed) {
    switch (ed) {
        case core::Edition::Api: return e.studio != Studio::Ihm;
        case core::Edition::Ihm: return e.studio != Studio::Api;
        case core::Edition::Both: break;
    }
    return true;
}

std::string_view studioName(Studio s) {
    switch (s) {
        case Studio::Api: return "API Studio";
        case Studio::Ihm: return "IHM Studio";
        case Studio::Both: break;
    }
    return "API Studio et IHM Studio";
}

// ============================================================ les dossiers passes ==
bool skipFolder(std::string_view name, bool driveRoot, bool hidden) {
    if (hidden) return true;
    const std::string n = lowerAscii(name);
    // Partout : la corbeille, les volumes, les restes des mises a jour de Windows.
    static const char* const kAnywhere[] = {"$recycle.bin", "recycler", "system volume information", "$windows.~bt", "$windows.~ws",
                                            "$sysreset", "$winreagent", "config.msi"};
    for (const char* s : kAnywhere)
        if (n == s) return true;
    if (!driveRoot) return false;
    // A la racine d'un lecteur : Windows, les programmes, leurs donnees (et l'equivalent ailleurs).
    static const char* const kAtRoot[] = {"windows", "program files", "program files (x86)", "programdata", "recovery", "perflogs",
                                          "msocache", "proc", "sys", "dev", "run", "snap", "usr", "bin", "sbin", "lib", "lib32",
                                          "lib64", "libx32", "boot", "etc", "var", "opt", "srv", "lost+found"};
    for (const char* s : kAtRoot)
        if (n == s) return true;
    return false;
}

// ================================================================ la version lue ==
std::string readVersion(const std::string& path, std::string_view key, std::string* edition) {
    if (edition) edition->clear();
    if (key == "xpgproj") {
        // Le manifeste du projet : "version = 1.0.0", "state = DEV", "edition = ihm".
        const std::string text = readHead(path, 16 * 1024);
        if (edition) *edition = lowerAscii(lineValue(text, "edition"));
        const std::string version = lineValue(text, "version");
        const std::string state = lineValue(text, "state");
        if (version.empty()) return {};
        return state.empty() ? version : version + " \xC2\xB7 " + state;
    }
    if (key == "xihm" || key == "xapi") {
        // L'archive d'un projet : son manifeste (manifeste.txt, en tete) dit l'outil qui l'a faite.
        std::string text;
        if (!zipEntryText(path, "manifeste.txt", text, 8u * 1024u * 1024u)) return {};
        project::archive::Manifest m;
        std::vector<std::string> problems;
        project::archive::parse(text, m, problems);
        if (edition && m.kindKnown) *edition = std::string(project::archive::key(m.kind));
        const auto space = m.tool.find_last_of(' ');
        const std::string last = space == std::string::npos ? m.tool : m.tool.substr(space + 1);
        if (!last.empty() && std::isdigit(static_cast<unsigned char>(last.front()))) return last;
        return m.tool;
    }
    if (key == "xpg" || key == "xhw" || key == "xdb" || key == "xef") {
        // L'export de Control Expert : <fileHeader ... product="Control Expert V15.3 - 230214C" ...>.
        std::string head = readHead(path, 4096);
        if (head.size() >= 3 && static_cast<unsigned char>(head[0]) == 0xEF) head.erase(0, 3);
        if (head.rfind("<?xml", 0) != 0) return {};
        const auto header = head.find("<fileHeader");
        if (header == std::string::npos) return {};
        std::string product = attribute(std::string_view(head).substr(header + 1), "product");
        if (const auto dash = product.find(" - "); dash != std::string::npos) product.resize(dash);
        return product;
    }
    if (key == "dfb" || key == "ddt") return lineValue(readHead(path, 4096), "version");
    if (key == "mac") return lineValue(readHead(path, 8192), "version", "#!");
    if (key == "xpgvues" || key == "xpgsymboles" || key == "xpgtypes" || key == "xpgfonctions" || key == "xpgscripts"
        || key == "xpgmodele") {
        std::string text;
        if (!zipEntryText(path, "paquet.txt", text, 1024u * 1024u)) return {};
        return attribute(text, "version");
    }
    if (key == "xpgbib") {
        std::string text;
        if (!zipEntryText(path, "bibliotheque.txt", text, 1024u * 1024u)) return {};
        return attribute(text, "version");
    }
    return {};
}

// ================================================================== les racines ==
namespace {

#if defined(_WIN32)
// Un disque "fixe" branche par USB (un disque externe), FireWire, une carte SD.
bool externalBus(wchar_t letter) {
    const std::wstring device = std::wstring(L"\\\\.\\") + letter + L":";
    HANDLE h = CreateFileW(device.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceProperty;
    q.QueryType = PropertyStandardQuery;
    alignas(8) unsigned char buffer[1024] = {};
    DWORD got = 0;
    bool external = false;
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, buffer, sizeof buffer, &got, nullptr) && got >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        const auto* d = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer);
        external = d->BusType == BusTypeUsb || d->BusType == BusType1394 || d->BusType == BusTypeSd || d->BusType == BusTypeMmc;
    }
    CloseHandle(h);
    return external;
}

bool volumeReady(const std::wstring& root) {
    const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
    const BOOL ready = GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0);
    SetErrorMode(oldMode);
    return ready != FALSE;
}

struct Drive { std::string root; bool network{false}; };
// `probe` : lire les volumes (le parcours) ; sans : vite (le libelle).
std::vector<Drive> drives(bool network, bool removable, bool probe) {
    std::vector<Drive> out;
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((mask & (1u << i)) == 0) continue;
        const wchar_t letter = static_cast<wchar_t>(L'A' + i);
        const std::wstring root = std::wstring(1, letter) + L":\\";
        const UINT type = GetDriveTypeW(root.c_str());
        bool net = false;
        switch (type) {
            case DRIVE_FIXED:
            case DRIVE_RAMDISK:
                if (probe && !removable && externalBus(letter)) continue;
                break;
            case DRIVE_REMOVABLE:
                if (!removable) continue;
                if (probe && !volumeReady(root)) continue;    // un lecteur de cartes vide
                break;
            case DRIVE_REMOTE:
                if (!network) continue;
                net = true;
                break;
            default:
                continue;                                     // CD, DVD, inconnu
        }
        out.push_back({narrow(root), net});
    }
    return out;
}
#endif

bool networkPath(const std::string& p) {
#if defined(_WIN32)
    if (p.size() >= 2 && (p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/')) return true;
    if (p.size() >= 2 && p[1] == ':') {
        const std::wstring root = std::wstring(1, static_cast<wchar_t>(p[0])) + L":\\";
        return GetDriveTypeW(root.c_str()) == DRIVE_REMOTE;
    }
    return false;
#else
    (void)p;
    return false;
#endif
}

} // namespace

std::string systemDrive() {
#if defined(_WIN32)
    const char* d = std::getenv("SystemDrive");
    return d && *d ? std::string(d) : std::string("C:");
#else
    return "/";
#endif
}

std::string documentsFolder() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, buf))) return narrow(buf, wcslen(buf));
    const char* home = std::getenv("USERPROFILE");
    return home ? std::string(home) + "\\Documents" : std::string{};
#else
    const char* home = std::getenv("HOME");
    const std::string h = home ? home : "";
    if (h.empty()) return {};
    std::error_code ec;
    if (std::FILE* f = std::fopen((h + "/.config/user-dirs.dirs").c_str(), "r")) {
        char line[1024];
        std::string found;
        while (std::fgets(line, sizeof line, f)) {
            std::string s = line;
            if (s.rfind("XDG_DOCUMENTS_DIR=", 0) != 0) continue;
            s = s.substr(18);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
            if (s.rfind("$HOME", 0) == 0) s = h + s.substr(5);
            found = s;
        }
        std::fclose(f);
        if (!found.empty() && fs::is_directory(pathOf(found), ec)) return found;
    }
    if (fs::is_directory(pathOf(h + "/Documents"), ec)) return h + "/Documents";
    return h;
#endif
}

std::vector<std::string> rootsFor(const Request& r, std::vector<bool>* network) {
    std::vector<std::string> out;
    std::vector<bool> net;
    if (!r.roots.empty()) {
        out = r.roots;
        for (const auto& p : out) net.push_back(networkPath(p));
    } else {
        switch (r.where) {
            case Where::WholePc:
#if defined(_WIN32)
                for (const auto& d : drives(r.network, r.removable, true)) {
                    out.push_back(d.root);
                    net.push_back(d.network);
                }
#else
                out.push_back("/");
                net.push_back(false);
#endif
                break;
            case Where::SystemDrive:
#if defined(_WIN32)
                out.push_back(systemDrive() + "\\");
#else
                out.push_back("/");
#endif
                net.push_back(false);
                break;
            case Where::Documents:
                if (auto d = documentsFolder(); !d.empty()) {
                    net.push_back(networkPath(d));
                    out.push_back(std::move(d));
                }
                break;
            case Where::Folder:
                if (!r.folder.empty()) {
                    out.push_back(r.folder);
                    net.push_back(networkPath(r.folder));
                }
                break;
        }
    }
    if (network) *network = std::move(net);
    return out;
}

std::string drivesLabel(bool network, bool removable) {
#if defined(_WIN32)
    std::string out;
    for (const auto& d : drives(network, removable, false)) {
        if (!out.empty()) out += ", ";
        out += d.root.substr(0, 2);
    }
    return out.empty() ? std::string("C:") : out;
#else
    (void)network;
    (void)removable;
    return "/";
#endif
}

// ===================================================================== le parcours ==
struct Search::Run {
    Request                 request;
    std::uint64_t           generation{0};
    std::atomic_bool        stop{false};
    std::atomic_bool        done{false};
    mutable std::mutex      lock;
    std::vector<Hit>        hits;
    Progress                progress;
};

namespace {

struct DirEntry {
    std::string   name;
    bool          folder{false};
    bool          link{false};      // un lien symbolique, une jonction : pas suivi
    bool          hidden{false};    // cache ou systeme (Windows), .nom ailleurs
    bool          cloud{false};     // un fichier dans le nuage (OneDrive) : le lire le telechargerait
    std::uint64_t size{0};
    std::int64_t  mtime{0};
};

// Lire un dossier ; faux : refuse (les droits, un lecteur absent).
bool readDir(const std::string& dir, const std::atomic_bool& stop, std::vector<DirEntry>& out) {
    out.clear();
#if defined(_WIN32)
    std::wstring pattern = wide(dir);
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') pattern += L'\\';
    pattern += L'*';
    WIN32_FIND_DATAW fd{};
    const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    SetErrorMode(oldMode);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;   // vide : lu
    do {
        if (stop.load(std::memory_order_relaxed)) break;
        const wchar_t* w = fd.cFileName;
        if ((w[0] == L'.' && w[1] == 0) || (w[0] == L'.' && w[1] == L'.' && w[2] == 0)) continue;
        DirEntry e;
        e.name = narrow(w, wcslen(w));
        e.folder = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        // Un lien symbolique, une jonction, un point de montage : un "substitut de nom".
        // Les dossiers et fichiers du nuage (OneDrive) sont aussi des points
        // d'analyse, mais pas des liens : ils se parcourent.
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsReparseTagNameSurrogate(fd.dwReserved0)) e.link = true;
        e.hidden = (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
        e.cloud = (fd.dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) != 0;
        e.size = (static_cast<std::uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        e.mtime = unixOf(fd.ftLastWriteTime);
        out.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
#else
    std::error_code ec;
    fs::directory_iterator it(pathOf(dir), ec);
    if (ec) return false;
    const auto fileNow = fs::file_time_type::clock::now();
    const auto sysNow = std::chrono::system_clock::now();
    for (const fs::directory_iterator end; !ec && it != end; it.increment(ec)) {
        if (stop.load(std::memory_order_relaxed)) break;
        const auto& de = *it;
        DirEntry e;
        e.name = utf8Of(de.path().filename());
        std::error_code e2;
        e.link = de.is_symlink(e2);
        if (!e.link) e.folder = de.is_directory(e2);
        e.hidden = !e.name.empty() && e.name.front() == '.';
        if (!e.folder && !e.link && (!keyOf(e.name).empty() || lowerAscii(fs::path(e.name).extension().string()) == ".zip")) {
            std::error_code e3;
            const auto size = de.file_size(e3);
            if (!e3) e.size = size;
            const auto ft = de.last_write_time(e3);
            if (!e3) {
                const auto sys = sysNow + std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - fileNow);
                e.mtime = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count());
            }
        }
        out.push_back(std::move(e));
    }
    return true;
#endif
}

void lowerPriority() {
#if defined(_WIN32)
    // Le mode arriere-plan : la priorite du fil, et celle de ses lectures sur le disque.
    if (!SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN)) (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#else
    (void)setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
}

bool add(Search::Run& run, Hit h) {
    h.folded = ui::foldForSearch(h.name);
    std::lock_guard<std::mutex> g(run.lock);
    run.hits.push_back(std::move(h));
    run.progress.files = run.hits.size();
    if (run.hits.size() >= kMaxHits) {
        run.progress.truncated = true;
        return false;
    }
    return true;
}

// La table des matieres d'un .zip : ce qu'elle contient du catalogue.
bool scanZip(Search::Run& run, const std::string& path) {
    std::FILE* f = openRead(path);
    if (!f) return true;
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    bool more = true;
    if (mz_zip_reader_init_cfile(&zip, f, 0, 0)) {
        {
            std::lock_guard<std::mutex> g(run.lock);
            ++run.progress.zips;
        }
        const mz_uint n = mz_zip_reader_get_num_files(&zip);
        for (mz_uint i = 0; i < n && more && !run.stop.load(std::memory_order_relaxed); ++i) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
            std::string inner = st.m_filename;
            for (auto& c : inner) if (c == '\\') c = '/';
            const std::string leaf = leafOf(inner);
            const std::string key = keyOf(leaf);
            if (key.empty()) continue;
            Hit h;
            h.name = leaf;
            h.key = key;
            h.archive = path;
            std::string shown = inner;
#if defined(_WIN32)
            for (auto& c : shown) if (c == '/') c = '\\';
#endif
            h.path = joinPath(path, shown);
            const auto slash = inner.find_last_of('/');
            h.dir = path + (slash == std::string::npos ? std::string{} : " \xE2\x80\xBA " + inner.substr(0, slash));
            h.size = st.m_uncomp_size;
            h.mtime = static_cast<std::int64_t>(st.m_time);
            more = add(run, std::move(h));
        }
        mz_zip_reader_end(&zip);
    }
    std::fclose(f);
    return more;
}

std::string rootLabel(const std::string& root) {
#if defined(_WIN32)
    if (root.size() <= 3 && root.size() >= 2 && root[1] == ':') return root.substr(0, 2);
#endif
    return root;
}

void walk(const std::shared_ptr<Search::Run>& runPtr) {
    auto& run = *runPtr;
    lowerPriority();
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&started] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    };
    {
        std::lock_guard<std::mutex> g(run.lock);
        run.progress.current = "les lecteurs\xE2\x80\xA6";
    }
    std::vector<bool> net;
    const auto roots = rootsFor(run.request, &net);
    {
        std::string where;
        for (const auto& r : roots) where += (where.empty() ? "" : ", ") + rootLabel(r);
        std::lock_guard<std::mutex> g(run.lock);
        run.progress.where = std::move(where);
    }
    struct Frame { std::string dir; bool network; };
    std::vector<Frame> stack;
    for (std::size_t i = roots.size(); i-- > 0;) stack.push_back({roots[i], i < net.size() && net[i]});
    std::vector<DirEntry> entries;
    std::vector<Frame> sub;
    std::uint64_t folders = 0, refused = 0, links = 0, skipped = 0;
    const auto& req = run.request;
    bool more = true;
    while (!stack.empty() && more && !run.stop.load(std::memory_order_relaxed)) {
        Frame f = std::move(stack.back());
        stack.pop_back();
        {
            std::lock_guard<std::mutex> g(run.lock);
            run.progress.current = f.dir;
            run.progress.folders = folders;
            run.progress.refused = refused;
            run.progress.links = links;
            run.progress.skipped = skipped;
            run.progress.seconds = elapsed();
        }
        // Un lecteur reseau a son rythme : une petite pause par dossier.
        if (f.network) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const bool refusedForTests = std::find(req.refuseForTests.begin(), req.refuseForTests.end(), f.dir) != req.refuseForTests.end();
        if (refusedForTests || !readDir(f.dir, run.stop, entries)) {
            ++refused;
            continue;
        }
        ++folders;
        const bool root = isDriveRoot(f.dir);
        sub.clear();
        for (auto& e : entries) {
            if (run.stop.load(std::memory_order_relaxed)) break;
            if (e.link) {
                ++links;
                continue;
            }
            if (e.folder) {
                if (req.skipSystem && skipFolder(e.name, root, e.hidden)) {
                    ++skipped;
                    continue;
                }
#if !defined(_WIN32)
                // Ailleurs que sous Windows, les supports amovibles sont montes sous /media, /mnt.
                if (!req.removable && root && (e.name == "media" || e.name == "mnt")) {
                    ++skipped;
                    continue;
                }
#endif
                sub.push_back({joinPath(f.dir, e.name), f.network});
                continue;
            }
            const std::string key = keyOf(e.name);
            if (key.empty()) {
                if (req.zips && lowerAscii(fs::path(e.name).extension().string()) == ".zip" && !e.cloud)
                    more = scanZip(run, joinPath(f.dir, e.name)) && more;
                if (!more) break;
                continue;
            }
            Hit h;
            h.name = e.name;
            h.key = key;
            h.path = joinPath(f.dir, e.name);
            h.dir = f.dir;
            // Un projet : le nom de son dossier (tous ses manifestes s'appellent project.xpgproj).
            if (key == "xpgproj" && !root) h.name = leafOf(f.dir);
            h.size = e.size;
            h.mtime = e.mtime;
            if (!e.cloud) h.version = readVersion(h.path, key, &h.edition);
            if (!add(run, std::move(h))) {
                more = false;
                break;
            }
        }
        // Le premier sous-dossier sort le premier (l'ordre du disque).
        for (std::size_t i = sub.size(); i-- > 0;) stack.push_back(std::move(sub[i]));
    }
    std::lock_guard<std::mutex> g(run.lock);
    run.progress.folders = folders;
    run.progress.refused = refused;
    run.progress.links = links;
    run.progress.skipped = skipped;
    run.progress.seconds = elapsed();
    run.progress.running = false;
    run.progress.current.clear();
    if (run.stop.load()) run.progress.stopped = true;
    else run.progress.finished = true;
    run.done.store(true, std::memory_order_release);
}

} // namespace

Search::Search() = default;

Search::~Search() {
    // La sortie de l'appli : le fil voit le drapeau a l'entree suivante. Une lecture
    // bloquee (un lecteur reseau qui ne repond plus) ne retient pas la fermeture : apres
    // une seconde, le fil est laisse finir seul (il ne touche qu'a son etat, partage).
    stop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    const auto settle = [&deadline](const std::shared_ptr<Run>& run, std::thread& t) {
        while (run && !run->done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!t.joinable()) return;
        if (!run || run->done.load(std::memory_order_acquire)) t.join();
        else t.detach();
    };
    for (auto& [run, t] : finishing_) {
        run->stop.store(true);
        settle(run, t);
    }
    settle(run_, worker_);
}

void Search::reap(bool all) {
    for (auto it = finishing_.begin(); it != finishing_.end();) {
        if (all || it->first->done.load(std::memory_order_acquire)) {
            if (it->second.joinable()) it->second.join();
            it = finishing_.erase(it);
        } else {
            ++it;
        }
    }
}

void Search::start(Request r) {
    if (run_) {
        run_->stop.store(true);
        if (worker_.joinable()) finishing_.emplace_back(run_, std::move(worker_));
    }
    reap(false);
    request_ = r;
    auto run = std::make_shared<Run>();
    run->request = std::move(r);
    run->generation = ++generation_;
    run->progress.running = true;
    run->progress.generation = run->generation;
    run_ = run;
    worker_ = std::thread([run] { walk(run); });
}

void Search::stop() {
    if (!run_) return;
    run_->stop.store(true);
    // L'ecran le dit tout de suite : le fil s'arrete a l'entree suivante.
    std::lock_guard<std::mutex> g(run_->lock);
    if (run_->progress.running) {
        run_->progress.running = false;
        run_->progress.stopped = true;
    }
}

void Search::wait() {
    if (worker_.joinable()) worker_.join();
    reap(true);
}

Progress Search::progress() const {
    if (!run_) return {};
    std::lock_guard<std::mutex> g(run_->lock);
    Progress p = run_->progress;
    // Arretee : le fil peut encore finir son dossier ; l'ecran dit "arretee".
    if (run_->stop.load() && !run_->done.load()) {
        p.running = false;
        p.stopped = true;
        p.finished = false;
    }
    return p;
}

std::size_t Search::found() const {
    if (!run_) return 0;
    std::lock_guard<std::mutex> g(run_->lock);
    return run_->hits.size();
}

std::vector<Hit> Search::since(std::size_t from) const {
    if (!run_) return {};
    std::lock_guard<std::mutex> g(run_->lock);
    if (from >= run_->hits.size()) return {};
    return std::vector<Hit>(run_->hits.begin() + static_cast<std::ptrdiff_t>(from), run_->hits.end());
}

Search& shared() {
    static Search s;
    return s;
}

// ===================================================================== le texte ==
std::string thousands(std::uint64_t n) {
    std::string d = std::to_string(n), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";   // espace fine insecable
        out += d[i];
    }
    return out;
}

std::string typeText(const Hit& h) {
    if (h.key == "xpgproj") {
        if (h.edition == "ihm") return "Projet IHM Studio";
        if (h.edition == "api") return "Projet API Studio";
        return "Projet (API et IHM, 1.11)";
    }
    const auto* e = info(h.key);
    return e ? std::string(e->label) : h.key;
}

std::string sizeText(std::uint64_t bytes) { return project::archive::sizeText(bytes); }

std::string dateText(std::int64_t mtime) {
    if (mtime <= 0) return "\xE2\x80\x94";
    const std::time_t t = static_cast<std::time_t>(mtime);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[64];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    return b;
}

std::string progressText(const Progress& p) {
    if (p.generation == 0) return "Pr\xC3\xAAt.";
    std::string tail;
    if (p.refused) tail += " \xC2\xB7 " + plural(p.refused, "dossier refus\xC3\xA9", "dossiers refus\xC3\xA9s");
    if (p.links) tail += " \xC2\xB7 " + plural(p.links, "lien pass\xC3\xA9", "liens pass\xC3\xA9s");
    if (p.running)
        return "Recherche\xE2\x80\xA6 " + plural(p.folders, "dossier lu", "dossiers lus") + " \xC2\xB7 " + plural(p.files, "fichier", "fichiers")
             + (p.current.empty() ? std::string{} : " \xC2\xB7 " + p.current);
    const std::string where = p.where.empty() ? std::string{} : " (" + p.where + ")";
    if (p.truncated)
        return "Arr\xC3\xAAt\xC3\xA9" "e \xC3\xA0 " + thousands(kMaxHits) + " fichiers : affine la recherche (O\xC3\xB9 chercher, un dossier)" + tail;
    if (p.stopped)
        return "Arr\xC3\xAAt\xC3\xA9" "e : " + plural(p.files, "fichier trouv\xC3\xA9", "fichiers trouv\xC3\xA9s") + " jusque-l\xC3\xA0 ("
             + plural(p.folders, "dossier lu", "dossiers lus") + ")" + tail;
    const auto secs = static_cast<long long>(p.seconds + 0.5);
    const std::string took = secs < 1 ? std::string("en moins d'une seconde")
                           : secs < 120 ? "en " + std::to_string(secs) + " s"
                                        : "en " + std::to_string(secs / 60) + " min " + std::to_string(secs % 60) + " s";
    return "Termin\xC3\xA9 : " + plural(p.files, "fichier", "fichiers") + " dans " + plural(p.folders, "dossier", "dossiers") + where + ", " + took + tail;
}

// ===================================================================== les gestes ==
Studio studioOf(const Hit& h, core::Edition here) {
    const Studio running = here == core::Edition::Api ? Studio::Api : here == core::Edition::Ihm ? Studio::Ihm : Studio::Both;
    if (h.key == "xpgproj") {
        if (h.edition == "ihm") return Studio::Ihm;
        if (h.edition == "api") return Studio::Api;
        return running;
    }
    const auto* e = info(h.key);
    if (!e || e->studio == Studio::Both) return running;
    return e->studio;
}

bool openedHere(const Hit& h, core::Edition here) {
    const Studio s = studioOf(h, here);
    if (here == core::Edition::Both || s == Studio::Both) return true;
    return (s == Studio::Api) == (here == core::Edition::Api);
}

bool openable(std::string_view key) {
    return key == "xpgproj" || key == "xihm" || key == "xapi" || key == "xpg" || key == "xhw" || key == "xdb" || key == "xef";
}

ImportKind importKindOf(std::string_view key) {
    if (key == "xpgvues" || key == "xpgsymboles" || key == "xpgtypes" || key == "xpgfonctions" || key == "xpgscripts") return ImportKind::HmiPackage;
    if (key == "xpgmodele") return ImportKind::HmiTemplate;
    if (key == "xpgbib") return ImportKind::HmiLibrary;
    if (key == "xpg" || key == "xhw" || key == "xdb") return ImportKind::PlcExport;
    if (key == "dfb" || key == "ddt" || key == "mac") return ImportKind::Library;
    if (key == "xpgtheme") return ImportKind::Theme;
    if (key == "xrec") return ImportKind::Recording;
    if (key == "xpgst") return ImportKind::Operators;
    return ImportKind::None;
}

namespace {
const char* const kInZip = "dans une archive .zip : extrais-la d'abord (Ouvrir le dossier montre le .zip)";
}

std::string projectFolderOf(const Hit& h) { return h.key == "xpgproj" ? parentOf(h.path) : h.path; }

Gesture openGesture(const Hit& h, const Context& c) {
    Gesture g;
    g.label = "Ouvrir";
    if (!h.archive.empty()) {
        g.why = kInZip;
        return g;
    }
    const Studio s = studioOf(h, c.edition);
    const bool here = openedHere(h, c.edition);
    if (!openable(h.key)) {
        const auto kind = importKindOf(h.key);
        // Un paquet d'IHM Studio vu d'API Studio : IHM Studio le recoit (il l'importe dans son projet ouvert).
        if (!here && (kind == ImportKind::HmiPackage || kind == ImportKind::HmiTemplate)) {
            g.enabled = true;
            g.label = "Ouvrir avec " + std::string(studioName(s));
            return g;
        }
        const auto* e = info(h.key);
        const std::string what = e ? std::string(e->label) : h.key;
        g.why = here ? what + " : il s'importe (" + importGesture(h, c).label + ")"
                     : what + " : un fichier d'" + std::string(studioName(s)) + ", il s'importe depuis " + std::string(studioName(s));
        return g;
    }
    g.enabled = true;
    if (!here) g.label = "Ouvrir avec " + std::string(studioName(s));
    return g;
}

Gesture importGesture(const Hit& h, const Context& c) {
    Gesture g;
    g.label = "Importer dans le projet";
    if (!h.archive.empty()) {
        g.why = kInZip;
        return g;
    }
    const bool ihm = c.edition != core::Edition::Api;
    const bool api = c.edition != core::Edition::Ihm;
    const auto needIhmProject = [&g, &c](const char* what) {
        if (!c.workspace) g.why = std::string("ouvre d'abord le projet IHM qui recevra ") + what + " (puis Chercher des fichiers depuis lui)";
        else if (!c.hmiProject) g.why = std::string("ouvre d'abord le projet IHM qui recevra ") + what;
        else g.enabled = true;
    };
    switch (importKindOf(h.key)) {
        case ImportKind::None:
            g.why = h.key == "xef" ? "un export XEF s'ouvre (un nouveau projet) : Ouvrir" : "un projet s'ouvre, il ne s'importe pas : Ouvrir";
            return g;
        case ImportKind::HmiPackage:
            if (!ihm) { g.why = "un paquet d'IHM Studio : il s'importe dans IHM Studio (Ouvrir le lui passe)"; return g; }
            needIhmProject("le paquet");
            return g;
        case ImportKind::HmiTemplate:
            g.label = "Importer le mod\xC3\xA8le";
            if (!ihm) { g.why = "un mod\xC3\xA8le de vue d'IHM Studio : il s'importe dans IHM Studio (Ouvrir le lui passe)"; return g; }
            needIhmProject("le mod\xC3\xA8le");
            return g;
        case ImportKind::HmiLibrary:
            g.label = "Relier au projet";
            if (!ihm) { g.why = "une biblioth\xC3\xA8que d'IHM Studio : elle se relie dans IHM Studio (Biblioth\xC3\xA8ques)"; return g; }
            needIhmProject("la biblioth\xC3\xA8que");
            return g;
        case ImportKind::Operators:
            g.why = ihm ? "des op\xC3\xA9rateurs ou des scripts ST : Importer\xE2\x80\xA6 du volet Op\xC3\xA9rateurs d'un type ou de l'\xC3\xA9" "diteur de scripts"
                        : "des op\xC3\xA9rateurs d'IHM Studio : ils s'importent dans IHM Studio";
            return g;
        case ImportKind::Recording:
            g.label = "Importer dans Ressources \xE2\x80\xBA Enregistrements";
            if (!ihm) { g.why = "un enregistrement d'IHM Studio : il se rejoue dans IHM Studio"; return g; }
            if (!c.recordings) { g.why = "les enregistrements (Ressources \xE2\x80\xBA Enregistrements) ne sont pas dans cette version"; return g; }
            needIhmProject("l'enregistrement");
            return g;
        case ImportKind::PlcExport:
            if (!api) { g.why = "un export de Control Expert : il s'importe dans API Studio (Ouvrir le lance)"; return g; }
            if (!c.workspace) g.why = "ouvre d'abord le projet de l'automate qui le recevra (puis Chercher des fichiers depuis lui)";
            else if (!c.plcProject) g.why = "ouvre d'abord le projet de l'automate qui le recevra";
            else g.enabled = true;
            return g;
        case ImportKind::Library:
            g.label = "Ajouter \xC3\xA0 la biblioth\xC3\xA8que";
            if (!api) { g.why = h.key == "mac" ? "une macro d'API Studio : elle va dans sa biblioth\xC3\xA8que, depuis API Studio"
                                               : "un bloc de l'automate : il va dans la biblioth\xC3\xA8que d'API Studio, depuis API Studio"; return g; }
            g.enabled = true;
            return g;
        case ImportKind::Theme:
            g.label = "Ajouter aux th\xC3\xA8mes";
            g.enabled = true;
            return g;
    }
    return g;
}

Gesture recentGesture(const Hit& h, const Context& c) {
    Gesture g;
    g.label = "Ajouter aux r\xC3\xA9" "cents";
    if (h.key != "xpgproj") {
        g.why = "seul un projet (project.xpgproj) va dans les r\xC3\xA9" "cents";
        return g;
    }
    if (!h.archive.empty()) {
        g.why = kInZip;
        return g;
    }
    if (!openedHere(h, c.edition)) {
        g.why = "un projet d'" + std::string(studioName(studioOf(h, c.edition))) + " : il va dans ses r\xC3\xA9" "cents quand il s'y ouvre";
        return g;
    }
    g.enabled = true;
    return g;
}

// ===================================================================== le systeme ==
std::string revealInExplorer(const std::string& path) {
    std::error_code ec;
    if (path.empty() || !fs::exists(pathOf(path), ec)) return path + " n'existe plus.";
#if defined(_WIN32)
    const std::wstring args = L"/select,\"" + wide(path) + L"\"";
    const auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL));
    if (r <= 32) return "l'Explorateur ne s'ouvre pas (code " + std::to_string(static_cast<long long>(r)) + ")";
    return {};
#else
    const std::string dir = fs::is_directory(pathOf(path), ec) ? path : parentOf(path);
    if (dir.find('"') != std::string::npos) return "chemin refus\xC3\xA9 : " + dir;
    const std::string command = "xdg-open \"" + dir + "\" >/dev/null 2>&1 &";
    if (std::system(command.c_str()) != 0) return "aucun programme n'a pu ouvrir " + dir;
    return {};
#endif
}

std::string launchStudio(Studio which, const std::string& path) {
    const std::string exeName = which == Studio::Api ? "API-Studio" : "IHM-Studio";
#if defined(_WIN32)
    std::wstring self(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    if (n == 0 || n >= self.size()) return "le programme est introuvable";
    self.resize(n);
    const fs::path exe = fs::path(self).parent_path() / (wide(exeName) + L".exe");
    std::error_code ec;
    if (!fs::exists(exe, ec)) return std::string(studioName(which)) + " n'est pas install\xC3\xA9 \xC3\xA0 c\xC3\xB4t\xC3\xA9 (" + exeName + ".exe)";
    std::wstring line = L"\"" + exe.wstring() + L"\" \"" + wide(path) + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return "le lancement est refus\xC3\xA9 (erreur " + std::to_string(GetLastError()) + ")";
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return {};
#else
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (ec || self.empty()) return "le programme est introuvable";
    const fs::path exe = self.parent_path() / exeName;
    if (!fs::exists(exe, ec)) return std::string(studioName(which)) + " n'est pas install\xC3\xA9 \xC3\xA0 c\xC3\xB4t\xC3\xA9 (" + exeName + ")";
    const pid_t pid = fork();
    if (pid < 0) return "le lancement est refus\xC3\xA9";
    if (pid == 0) {
        if (fork() != 0) _exit(0);
        const std::string e = exe.string();
        execl(e.c_str(), e.c_str(), path.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    (void)waitpid(pid, &status, 0);
    return {};
#endif
}

// ===================================================================== les reglages ==
std::string serialize(const Choices& c) {
    std::string ext;
    for (const auto& e : c.extensions) ext += (ext.empty() ? "" : ",") + e;
    // Le dossier en dernier : il peut contenir n'importe quoi.
    return "v1|ext=" + ext + "|ou=" + std::to_string(static_cast<int>(c.where)) + "|reseau=" + (c.network ? "1" : "0")
         + "|usb=" + (c.removable ? "1" : "0") + "|systeme=" + (c.skipSystem ? "1" : "0") + "|zip=" + (c.zips ? "1" : "0")
         + "|dossier=" + c.folder;
}

Choices parseChoices(std::string_view text) {
    Choices c;
    for (const auto& e : catalog()) c.extensions.emplace_back(e.key);
    if (text.rfind("v1|", 0) != 0) return c;
    std::size_t pos = 3;
    while (pos <= text.size()) {
        if (text.substr(pos, 8) == "dossier=") {
            c.folder = std::string(text.substr(pos + 8));
            break;
        }
        const auto bar = text.find('|', pos);
        const auto part = text.substr(pos, bar == std::string_view::npos ? std::string_view::npos : bar - pos);
        const auto eq = part.find('=');
        if (eq != std::string_view::npos) {
            const auto k = part.substr(0, eq);
            const auto v = part.substr(eq + 1);
            if (k == "ext") {
                c.extensions.clear();
                std::size_t a = 0;
                while (a < v.size()) {
                    const auto comma = v.find(',', a);
                    const auto one = std::string(v.substr(a, comma == std::string_view::npos ? std::string_view::npos : comma - a));
                    if (info(one)) c.extensions.push_back(one);
                    if (comma == std::string_view::npos) break;
                    a = comma + 1;
                }
            } else if (k == "ou") {
                const int w = std::atoi(std::string(v).c_str());
                if (w >= 0 && w <= 3) c.where = static_cast<Where>(w);
            } else if (k == "reseau") c.network = v == "1";
            else if (k == "usb") c.removable = v == "1";
            else if (k == "systeme") c.skipSystem = v == "1";
            else if (k == "zip") c.zips = v == "1";
        }
        if (bar == std::string_view::npos) break;
        pos = bar + 1;
    }
    return c;
}

} // namespace app::finder
