// =============================================================================
//  ui/FileExplorerModel.cpp - lot API 8 : l'explorateur de fichiers, sans ecran
// =============================================================================
#include "FileExplorerModel.hpp"

#include "TextSearch.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#    include <shlobj.h>
#    include <knownfolders.h>
#    if defined(_MSC_VER)
#        pragma comment(lib, "shell32.lib")
#        pragma comment(lib, "ole32.lib")
#        pragma comment(lib, "uuid.lib") // Lot API 8 : Windows - les GUID FOLDERID_* (MinGW : libuuid)
#    endif
#endif

namespace ui::files {

    namespace {

        namespace fs = std::filesystem;

        fs::path pathOf(std::string_view utf8) {
            const std::u8string u8(utf8.begin(), utf8.end());
            return fs::path(u8);
        }

        std::string utf8Of(const fs::path& p) {
            const auto u8 = p.u8string();
            return std::string(u8.begin(), u8.end());
        }

        bool isSep(char c) noexcept { return c == '/' || c == '\\'; }

        std::string trimmed(std::string_view s) {
            std::size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
            return std::string(s.substr(a, b - a));
        }

        std::string lowerAscii(std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        bool driveLetterPath(std::string_view p) noexcept {
            return p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':';
        }

        bool uncPath(std::string_view p) noexcept { return p.size() >= 2 && isSep(p[0]) && isSep(p[1]); }

        // Le separateur d'un chemin : celui qu'il porte, sinon \ pour D: et
        // \\serveur, sinon /.
        char separatorOf(std::string_view p) noexcept {
            for (const char c : p)
                if (isSep(c)) return c;
            return driveLetterPath(p) ? '\\' : '/';
        }

        std::vector<std::string> parts(std::string_view s) {
            std::vector<std::string> out;
            std::string cur;
            for (const char c : s) {
                if (isSep(c)) {
                    if (!cur.empty() && cur != ".") out.push_back(cur);
                    cur.clear();
                } else {
                    cur += c;
                }
            }
            if (!cur.empty() && cur != ".") out.push_back(cur);
            return out;
        }

        std::tm localOf(std::int64_t t) {
            std::tm out{};
            const auto tt = static_cast<std::time_t>(t);
#if defined(_WIN32)
            localtime_s(&out, &tt);
#else
            localtime_r(&tt, &out);
#endif
            return out;
        }

        std::string two(int v) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "%02d", v);
            return buf;
        }

        // Les noms plies (sans casse ni accents), une fois par tri.
        int naturalCompareFolded(std::string_view a, std::string_view b) {
            std::size_t i = 0, j = 0;
            while (i < a.size() && j < b.size()) {
                const bool da = std::isdigit(static_cast<unsigned char>(a[i])) != 0;
                const bool db = std::isdigit(static_cast<unsigned char>(b[j])) != 0;
                if (da && db) {
                    std::size_t ea = i, eb = j;
                    while (ea < a.size() && std::isdigit(static_cast<unsigned char>(a[ea]))) ++ea;
                    while (eb < b.size() && std::isdigit(static_cast<unsigned char>(b[eb]))) ++eb;
                    std::size_t za = i, zb = j;
                    while (za + 1 < ea && a[za] == '0') ++za;
                    while (zb + 1 < eb && b[zb] == '0') ++zb;
                    const auto la = ea - za, lb = eb - zb;
                    if (la != lb) return la < lb ? -1 : 1;
                    const int c = a.substr(za, la).compare(b.substr(zb, lb));
                    if (c != 0) return c < 0 ? -1 : 1;
                    i = ea;
                    j = eb;
                    continue;
                }
                const auto ca = static_cast<unsigned char>(a[i]), cb = static_cast<unsigned char>(b[j]);
                if (ca != cb) return ca < cb ? -1 : 1;
                ++i;
                ++j;
            }
            if (i < a.size()) return 1;
            if (j < b.size()) return -1;
            return 0;
        }

#if defined(_WIN32)
        std::wstring wideOf(const std::string& utf8) { return pathOf(utf8).wstring(); }
        std::string  utf8OfWide(const std::wstring& w) { return utf8Of(fs::path(w)); }

        std::int64_t unixOf(const FILETIME& ft) {
            const auto ticks = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
            if (ticks < 116444736000000000ull) return 0;
            return static_cast<std::int64_t>((ticks - 116444736000000000ull) / 10000000ull);
        }
#endif

        // Un lecteur retire : la racine du chemin n'est plus la.
        bool rootGone(const std::string& folder) {
            std::error_code ec;
            if (driveLetterPath(folder)) {
                const std::string root = folder.substr(0, 2) + "\\";
                return !fs::exists(pathOf(root), ec);
            }
            // /media/<utilisateur>/<cle>, /run/media/<utilisateur>/<cle>, /mnt/<x>
            const auto p = parts(folder);
            std::size_t depth = 0;
            if (!p.empty() && p[0] == "media") depth = 3;
            else if (p.size() >= 2 && p[0] == "run" && p[1] == "media") depth = 4;
            else if (!p.empty() && p[0] == "mnt") depth = 2;
            if (depth == 0 || p.size() < depth) return false;
            std::string mount;
            for (std::size_t i = 0; i < depth; ++i) mount += "/" + p[i];
            return !fs::exists(pathOf(mount), ec);
        }

    } // namespace

    // ================================================================ lister ===
    bool dotHidden(std::string_view name) noexcept { return !name.empty() && name.front() == '.'; }

    std::string errorMessage(ListError e, std::string_view folder) {
        switch (e) {
            case ListError::None:         return {};
            case ListError::AccessDenied: return "Acc\xC3\xA8s refus\xC3\xA9 \xC3\xA0 ce dossier";
            case ListError::NotFound:     return "Ce dossier n'existe plus";
            case ListError::NotAFolder:   return "Ce n'est pas un dossier";
            case ListError::DriveGone: {
                if (driveLetterPath(folder))
                    return "La cl\xC3\xA9 " + std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(folder[0]))))
                         + ": n'est plus l\xC3\xA0";
                return "Ce lecteur n'est plus l\xC3\xA0";
            }
            case ListError::Other:        return "Ce dossier ne se lit pas";
        }
        return {};
    }

    Listing listFolder(const std::string& folder) {
        Listing out;
        out.folder = folder;
        const auto fail = [&out, &folder](ListError e) {
            out.error = e;
            out.message = errorMessage(e, folder);
            out.entries.clear();
            return out;
        };
        if (trimmed(folder).empty()) return fail(ListError::NotFound);
        std::error_code ec;
        const fs::path dir = pathOf(folder);
        const auto st = fs::status(dir, ec);
        if (st.type() == fs::file_type::not_found || (ec && ec != std::errc::permission_denied))
            return fail(rootGone(folder) ? ListError::DriveGone : ListError::NotFound);
        if (!ec && !fs::is_directory(st)) return fail(ListError::NotAFolder);
#if defined(_WIN32)
        // UNE lecture : FindFirstFileEx rend le nom, les attributs, la taille et
        // la date de chaque element (std::filesystem le fait aussi, mais sans
        // l'attribut "cache").
        std::wstring pattern = wideOf(folder);
        if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') pattern += L'\\';
        pattern += L'*';
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND) return out;          // un dossier vide (une racine)
            if (err == ERROR_ACCESS_DENIED) return fail(ListError::AccessDenied);
            if (err == ERROR_NOT_READY || err == ERROR_INVALID_DRIVE) return fail(ListError::DriveGone);
            if (err == ERROR_PATH_NOT_FOUND) return fail(rootGone(folder) ? ListError::DriveGone : ListError::NotFound);
            return fail(ListError::Other);
        }
        do {
            const std::wstring wname = fd.cFileName;
            if (wname == L"." || wname == L"..") continue;
            if ((fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0) {
                ++out.systemHidden;
                continue;
            }
            Entry e;
            e.name = utf8OfWide(wname);
            e.folder = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            e.path = joinPath(folder, e.name);
            e.ext = e.folder ? std::string{} : extensionOf(e.name);
            e.family = familyOf(e.name, e.folder);
            e.size = e.folder ? 0u : ((static_cast<std::uintmax_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow);
            e.modified = unixOf(fd.ftLastWriteTime);
            out.entries.push_back(std::move(e));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
#else
        fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        if (ec) return fail(ec == std::errc::permission_denied ? ListError::AccessDenied : ListError::Other);
        // Les dates : l'horloge des fichiers vers celle du systeme, un seul
        // ecart pour tout le dossier.
        const auto fileNow = fs::file_time_type::clock::now();
        const auto sysNow = std::chrono::system_clock::now();
        for (const auto end = fs::directory_iterator(); it != end; it.increment(ec)) {
            if (ec) break;
            const auto& de = *it;
            Entry e;
            e.name = utf8Of(de.path().filename());
            if (dotHidden(e.name)) {
                ++out.systemHidden;
                continue;
            }
            std::error_code e2;
            e.folder = de.is_directory(e2);
            e.path = joinPath(folder, e.name);
            e.ext = e.folder ? std::string{} : extensionOf(e.name);
            e.family = familyOf(e.name, e.folder);
            if (!e.folder) {
                const auto size = de.file_size(e2);
                e.size = e2 ? 0u : size;
            }
            const auto ft = de.last_write_time(e2);
            if (!e2) {
                const auto sys = sysNow + std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - fileNow);
                e.modified = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count());
            }
            out.entries.push_back(std::move(e));
        }
#endif
        return out;
    }

    // =================================================================== tri ===
    int compareNames(std::string_view a, std::string_view b) {
        return naturalCompareFolded(foldForSearch(a), foldForSearch(b));
    }

    void sortEntries(std::vector<Entry>& entries, SortKey key, bool ascending) {
        std::vector<std::string> folded(entries.size());
        std::vector<std::string> types(key == SortKey::Type ? entries.size() : 0u);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            folded[i] = foldForSearch(entries[i].name);
            if (key == SortKey::Type) types[i] = foldForSearch(typeLabel(entries[i].name, entries[i].folder));
        }
        std::vector<std::size_t> order(entries.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) {
            const Entry& a = entries[x];
            const Entry& b = entries[y];
            if (a.folder != b.folder) return a.folder;            // les dossiers d'abord, toujours
            int c = 0;
            switch (key) {
                case SortKey::Name: break;
                case SortKey::Modified: c = a.modified < b.modified ? -1 : a.modified > b.modified ? 1 : 0; break;
                case SortKey::Type: c = types[x].compare(types[y]); c = c < 0 ? -1 : c > 0 ? 1 : 0; break;
                case SortKey::Size: c = a.size < b.size ? -1 : a.size > b.size ? 1 : 0; break;
            }
            if (c == 0) c = naturalCompareFolded(folded[x], folded[y]);
            if (c == 0) c = a.name.compare(b.name) < 0 ? -1 : a.name == b.name ? 0 : 1;
            return ascending ? c < 0 : c > 0;
        });
        std::vector<Entry> sorted;
        sorted.reserve(entries.size());
        for (const auto i : order) sorted.push_back(std::move(entries[i]));
        entries = std::move(sorted);
    }

    std::string sortKeyName(SortKey k) {
        switch (k) {
            case SortKey::Name:     return "nom";
            case SortKey::Modified: return "modifie";
            case SortKey::Type:     return "type";
            case SortKey::Size:     return "taille";
        }
        return "nom";
    }

    bool sortKeyFrom(std::string_view s, SortKey& out) {
        const std::string f = foldForSearch(s);
        if (f == "nom") out = SortKey::Name;
        else if (f == "modifie" || f == "date") out = SortKey::Modified;
        else if (f == "type") out = SortKey::Type;
        else if (f == "taille") out = SortKey::Size;
        else return false;
        return true;
    }

    // ================================================================ filtre ===
    std::string FilterGroup::label() const {
        if (all()) return name.empty() ? std::string("Tous les fichiers (*.*)") : name + " (*.*)";
        std::string pats, shown;
        for (const auto& e : extensions) {
            pats += (pats.empty() ? "*." : "; *.") + e;
            std::string up = e;
            for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            shown += (shown.empty() ? "" : ", ") + up;
        }
        return (name.empty() ? "Fichiers " + shown : name) + " (" + pats + ")";
    }

    bool FilterGroup::accepts(const Entry& e) const {
        if (e.folder || all()) return true;
        return std::find(extensions.begin(), extensions.end(), e.ext) != extensions.end();
    }

    std::vector<FilterGroup> filterGroups(const std::vector<std::pair<std::string, std::string>>& pickFilters) {
        std::vector<FilterGroup> out;
        bool haveAll = false;
        for (const auto& [name, pattern] : pickFilters) {
            FilterGroup g;
            g.name = trimmed(name);
            std::string token;
            const auto flush = [&g, &token] {
                std::string t = trimmed(token);
                token.clear();
                while (!t.empty() && (t.front() == '*' || t.front() == '.')) t.erase(t.begin());
                if (t.empty()) return;
                t = lowerAscii(t);
                if (std::find(g.extensions.begin(), g.extensions.end(), t) == g.extensions.end()) g.extensions.push_back(t);
            };
            for (const char c : pattern) {
                if (c == ';' || c == ',' || c == ' ') flush();
                else token += c;
            }
            flush();
            if (g.all()) {
                if (haveAll) continue;
                haveAll = true;
                if (g.name.empty()) g.name = "Tous les fichiers";
                continue;   // mis a la fin
            }
            out.push_back(std::move(g));
        }
        FilterGroup all;
        all.name = "Tous les fichiers";
        out.push_back(std::move(all));
        return out;
    }

    int findFilter(const std::vector<FilterGroup>& groups, std::string_view prefix) {
        const std::string want = foldForSearch(trimmed(prefix));
        if (want.empty()) return -1;
        for (std::size_t i = 0; i < groups.size(); ++i) {
            const std::string name = foldForSearch(groups[i].name);
            if (name.rfind(want, 0) == 0) return static_cast<int>(i);
        }
        for (std::size_t i = 0; i < groups.size(); ++i) {
            if (foldForSearch(groups[i].label()).find(want) != std::string::npos) return static_cast<int>(i);
        }
        return -1;
    }

    Shown shownEntries(const std::vector<Entry>& all, const FilterGroup* filter, std::string_view search, SortKey key,
                       bool ascending, bool showFiltered) {
        Shown out;
        const SearchQuery q(search);
        out.rows.reserve(all.size());
        for (const auto& e : all) {
            if (!q.empty() && !q.matches({std::string_view(e.name)})) continue;
            if (filter && !filter->accepts(e)) {
                ++out.hiddenByFilter;
                if (!showFiltered) continue;
            }
            out.rows.push_back(e);
        }
        sortEntries(out.rows, key, ascending);
        return out;
    }

    int typeAhead(const std::vector<Entry>& rows, std::string_view typed, int from) {
        const std::string want = foldForSearch(typed);
        if (want.empty() || rows.empty()) return -1;
        const auto n = static_cast<int>(rows.size());
        const int start = from < 0 || from >= n ? 0 : from;
        for (int k = 0; k < n; ++k) {
            const int i = (start + k) % n;
            if (foldForSearch(rows[static_cast<std::size_t>(i)].name).rfind(want, 0) == 0) return i;
        }
        return -1;
    }

    // ========================================================== fil d'Ariane ===
    std::vector<Crumb> breadcrumb(std::string_view pathIn) {
        std::vector<Crumb> out{{"Ce PC", {}}};
        const std::string path = trimmed(pathIn);
        if (path.empty()) return out;
        const char sep = separatorOf(path);
        const std::string s(1, sep);
        std::string cur;
        std::vector<std::string> rest;
        if (uncPath(path)) {
            const auto p = parts(std::string_view(path).substr(2));
            if (p.empty()) return out;
            cur = s + s + p[0];
            if (p.size() >= 2) cur += s + p[1];
            out.push_back({cur, cur});
            rest.assign(p.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(2, p.size())), p.end());
        } else if (driveLetterPath(path)) {
            const std::string drive = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(path[0])))) + ":";
            cur = drive + s;
            out.push_back({drive, cur});
            rest = parts(std::string_view(path).substr(2));
        } else if (isSep(path.front())) {
            cur = s;
            out.push_back({s, cur});
            rest = parts(path);
        } else {
            rest = parts(path);
        }
        for (const auto& part : rest) {
            if (cur.empty()) cur = part;
            else if (isSep(cur.back())) cur += part;
            else cur += s + part;
            out.push_back({part, cur});
        }
        return out;
    }

    std::string parentOf(std::string_view path) {
        const auto crumbs = breadcrumb(path);
        if (crumbs.size() < 3) return {};
        return crumbs[crumbs.size() - 2].path;
    }

    std::string joinPath(std::string_view folder, std::string_view name) {
        if (folder.empty()) return std::string(name);
        if (name.empty()) return std::string(folder);
        std::string out(folder);
        if (!isSep(out.back())) out += separatorOf(folder);
        out += name;
        return out;
    }

    bool samePath(std::string_view a, std::string_view b) {
        const auto norm = [](std::string_view p) {
            std::string s = trimmed(p);
            for (auto& c : s)
                if (c == '\\') c = '/';
            while (s.size() > 1 && s.back() == '/' && !(s.size() == 3 && s[1] == ':')) s.pop_back();
            bool fold = driveLetterPath(s) || uncPath(s);
#if defined(_WIN32)
            fold = true;
#endif
            return fold ? lowerAscii(s) : s;
        };
        return norm(a) == norm(b);
    }

    std::vector<std::string> completeFolder(std::string_view typedIn, std::size_t max) {
        std::vector<std::string> out;
        const std::string typed = trimmed(typedIn);
        if (typed.empty()) return out;
        std::string parent, leaf;
        const auto cut = typed.find_last_of("/\\");
        if (cut == std::string::npos) return out;
        parent = typed.substr(0, cut + 1);
        leaf = typed.substr(cut + 1);
        const std::string want = foldForSearch(leaf);
        std::error_code ec;
        std::vector<std::string> names;
        for (fs::directory_iterator it(pathOf(parent), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
             it.increment(ec)) {
            std::error_code e2;
            if (!it->is_directory(e2)) continue;
            const std::string name = utf8Of(it->path().filename());
            if (dotHidden(name) && (leaf.empty() || leaf.front() != '.')) continue;
            if (foldForSearch(name).rfind(want, 0) == 0) names.push_back(name);
        }
        std::sort(names.begin(), names.end(), [](const std::string& x, const std::string& y) { return compareNames(x, y) < 0; });
        for (const auto& n : names) {
            if (out.size() >= max) break;
            out.push_back(parent + n);
        }
        return out;
    }

    // =================================================== nom a enregistrer ===
    std::string nameError(std::string_view nameIn) {
        const std::string name(nameIn);
        if (trimmed(name).empty()) return "Le nom est vide";
        if (name == "." || name == "..") return "Ce nom est impossible";
        for (const char c : name) {
            if (static_cast<unsigned char>(c) < 32) return "Un caract\xC3\xA8re invisible est interdit dans un nom";
            if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
                return std::string("Caract\xC3\xA8re interdit dans un nom : ") + c + "  (interdits : < > : \" / \\ | ? *)";
        }
        if (name.back() == '.' || name.back() == ' ') return "Un nom ne finit ni par un point ni par une espace";
        if (name.size() > 255) return "Nom trop long (255 caract\xC3\xA8res au plus)";
        // Les noms reserves de Windows, avec ou sans extension ("CON", "nul.txt").
        std::string base = name.substr(0, name.find('.'));
        while (!base.empty() && base.back() == ' ') base.pop_back();
        std::string up = base;
        for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        static const char* const reserved[] = {"CON", "PRN", "AUX", "NUL"};
        bool isReserved = std::find_if(std::begin(reserved), std::end(reserved), [&up](const char* r) { return up == r; })
                        != std::end(reserved);
        if (!isReserved && up.size() == 4 && (up.rfind("COM", 0) == 0 || up.rfind("LPT", 0) == 0) && up[3] >= '1' && up[3] <= '9')
            isReserved = true;
        if (isReserved) return "Nom r\xC3\xA9serv\xC3\xA9 de Windows : " + base;
        return {};
    }

    std::string withExtension(std::string name, const FilterGroup* filter, std::string_view fallbackExt) {
        name = trimmed(name);
        if (name.empty() || !extensionOf(name).empty()) return name;
        std::string ext;
        if (filter && !filter->all()) ext = filter->extensions.front();
        else ext = lowerAscii(fallbackExt);
        while (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
        if (ext.empty()) return name;
        return name + "." + ext;
    }

    SaveCheck checkSaveName(std::string_view folder, std::string_view typedIn, const FilterGroup* filter, std::int64_t now,
                            std::string_view fallbackExt) {
        SaveCheck out;
        std::string typed = trimmed(typedIn);
        if (typed.size() >= 2 && typed.front() == '"' && typed.back() == '"') typed = trimmed(std::string_view(typed).substr(1, typed.size() - 2));
        std::string dir(folder);
        // Un chemin tape : son dossier et son nom.
        const auto cut = typed.find_last_of("/\\");
        if (cut != std::string::npos) {
            const std::string head = typed.substr(0, cut + 1);
            typed = typed.substr(cut + 1);
            const bool absolute = driveLetterPath(head) || isSep(head.front());
            dir = absolute ? head : joinPath(folder, head);
        } else if (driveLetterPath(typed)) {
            out.error = nameError(typed);
            return out;
        }
        out.name = withExtension(typed, filter, fallbackExt);
        out.error = nameError(out.name);
        if (!out.error.empty()) return out;
        out.path = joinPath(dir, out.name);
        std::error_code ec;
        const auto p = pathOf(out.path);
        const auto st = fs::status(p, ec);
        if (!ec && fs::is_directory(st)) {
            out.error = "\xC2\xAB " + out.name + " \xC2\xBB est un dossier";
            return out;
        }
        if (!ec && fs::exists(st)) {
            out.exists = true;
            std::int64_t when = 0;
            const auto ft = fs::last_write_time(p, ec);
            if (!ec) {
                const auto sys = std::chrono::system_clock::now()
                               + std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now());
                when = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count());
            }
            std::string rel = when > 0 ? relativeTime(when, now) : std::string{};
            if (!rel.empty() && std::isdigit(static_cast<unsigned char>(rel.front()))) rel = "le " + rel;
            out.warning = out.name + " existe d\xC3\xA9j\xC3\xA0" + (rel.empty() ? std::string{} : " (modifi\xC3\xA9 " + rel + ")")
                        + " : il sera remplac\xC3\xA9";
        }
        return out;
    }

    // ============================================================= libelles ===
    std::int64_t nowSeconds() {
        return static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    }

    std::string relativeTime(std::int64_t t, std::int64_t now) {
        if (t <= 0) return {};
        const std::int64_t delta = now - t;
        if (delta >= 0 && delta < 60) return "\xC3\xA0 l'instant";
        if (delta >= 0 && delta < 3600) return "il y a " + std::to_string(delta / 60) + " min";
        const std::tm lt = localOf(t);
        const std::tm ln = localOf(now);
        const std::string hm = two(lt.tm_hour) + ":" + two(lt.tm_min);
        if (lt.tm_year == ln.tm_year && lt.tm_yday == ln.tm_yday) return "aujourd'hui " + hm;
        std::tm y = ln;
        y.tm_mday -= 1;
        y.tm_hour = 12;
        y.tm_min = 0;
        y.tm_sec = 0;
        y.tm_isdst = -1;
        const std::time_t yt = std::mktime(&y);
        const std::tm yl = localOf(static_cast<std::int64_t>(yt));
        if (lt.tm_year == yl.tm_year && lt.tm_yday == yl.tm_yday) return "hier " + hm;
        return two(lt.tm_mday) + "/" + two(lt.tm_mon + 1) + "/" + std::to_string(lt.tm_year + 1900);
    }

    std::string sizeLabel(std::uintmax_t bytes) {
        if (bytes == 0) return "0 octet";
        if (bytes == 1) return "1 octet";
        if (bytes < 1024) return std::to_string(bytes) + " octets";
        const double kb = static_cast<double>(bytes) / 1024.0;
        if (kb < 1024.0) return std::to_string(static_cast<std::uintmax_t>(std::ceil(kb))) + " Ko";
        const auto withUnit = [](double v, const char* unit) {
            if (v < 10.0) {
                const auto tenths = static_cast<long long>(std::llround(v * 10.0));
                return std::to_string(tenths / 10) + "," + std::to_string(tenths % 10) + " " + unit;
            }
            return std::to_string(static_cast<long long>(std::llround(v))) + " " + unit;
        };
        const double mb = kb / 1024.0;
        if (mb < 1024.0) return withUnit(mb, "Mo");
        const double gb = mb / 1024.0;
        if (gb < 1024.0) return withUnit(gb, "Go");
        return withUnit(gb / 1024.0, "To");
    }

    // ============================================================== memoire ===
    void Memory::remember(const std::string& kind, const std::string& chosen, bool isFolder) {
        if (chosen.empty()) return;
        Recent r;
        if (isFolder) {
            r.folder = chosen;
        } else {
            r.folder = parentOf(chosen);
            r.file = chosen;
            if (r.folder.empty()) return;
        }
        auto& list = recents_[kind];
        list.erase(std::remove_if(list.begin(), list.end(), [&r](const Recent& x) { return samePath(x.folder, r.folder); }), list.end());
        list.insert(list.begin(), std::move(r));
        if (list.size() > kRecents) list.resize(kRecents);
    }

    void Memory::rememberFolder(const std::string& kind, const std::string& folder) { remember(kind, folder, true); }

    std::vector<Memory::Recent> Memory::recents(const std::string& kind) const {
        const auto it = recents_.find(kind);
        return it == recents_.end() ? std::vector<Recent>{} : it->second;
    }

    std::string Memory::lastFolder(const std::string& kind) const {
        const auto it = recents_.find(kind);
        return it == recents_.end() || it->second.empty() ? std::string{} : it->second.front().folder;
    }

    bool Memory::isRecentFile(const std::string& path) const {
        for (const auto& [kind, list] : recents_)
            for (const auto& r : list)
                if (!r.file.empty() && samePath(r.file, path)) return true;
        return false;
    }

    bool Memory::pin(const std::string& folder) {
        if (folder.empty() || pinned(folder)) return false;
        pins_.push_back(folder);
        return true;
    }

    bool Memory::unpin(const std::string& folder) {
        const auto before = pins_.size();
        pins_.erase(std::remove_if(pins_.begin(), pins_.end(), [&folder](const std::string& p) { return samePath(p, folder); }), pins_.end());
        return pins_.size() != before;
    }

    bool Memory::pinned(const std::string& folder) const {
        return std::any_of(pins_.begin(), pins_.end(), [&folder](const std::string& p) { return samePath(p, folder); });
    }

    Memory::ViewState Memory::view(const std::string& kind) const {
        const auto it = views_.find(kind);
        return it == views_.end() ? ViewState{} : it->second;
    }

    void Memory::setView(const std::string& kind, ViewState v) { views_[kind] = std::move(v); }

    // Les lignes : les champs separes par '>' (interdit dans un chemin de
    // Windows) ; Settings::setList les separe par '|' (interdit aussi).
    std::vector<std::string> Memory::save() const {
        std::vector<std::string> out;
        for (const auto& [kind, list] : recents_)
            for (const auto& r : list) out.push_back("recent>" + kind + ">" + r.folder + ">" + r.file);
        for (const auto& p : pins_) out.push_back("epingle>" + p);
        for (const auto& [kind, v] : views_) {
            std::string widths;
            for (const float w : v.widths) widths += (widths.empty() ? "" : ",") + std::to_string(static_cast<int>(std::lround(w)));
            out.push_back("vue>" + kind + ">" + (v.thumbnails ? "vignettes" : "details") + ">" + sortKeyName(v.sort) + ">"
                          + (v.ascending ? "1" : "0") + ">" + widths);
        }
        return out;
    }

    void Memory::load(const std::vector<std::string>& lines) {
        recents_.clear();
        pins_.clear();
        views_.clear();
        for (const auto& line : lines) {
            std::vector<std::string> f;
            std::string cur;
            for (const char c : line) {
                if (c == '>') {
                    f.push_back(cur);
                    cur.clear();
                } else {
                    cur += c;
                }
            }
            f.push_back(cur);
            if (f.size() >= 4 && f[0] == "recent" && !f[2].empty()) {
                auto& list = recents_[f[1]];
                if (list.size() < kRecents) list.push_back({f[2], f[3]});
            } else if (f.size() >= 2 && f[0] == "epingle" && !f[1].empty()) {
                if (!pinned(f[1])) pins_.push_back(f[1]);
            } else if (f.size() >= 6 && f[0] == "vue") {
                ViewState v;
                v.thumbnails = f[2] == "vignettes";
                (void)sortKeyFrom(f[3], v.sort);
                v.ascending = f[4] != "0";
                std::string w;
                for (const char c : f[5] + ",") {
                    if (c == ',') {
                        if (!w.empty()) v.widths.push_back(static_cast<float>(std::atoi(w.c_str())));
                        w.clear();
                    } else {
                        w += c;
                    }
                }
                views_[f[1]] = std::move(v);
            }
        }
    }

    std::string requestKind(PickMode mode, const std::vector<FilterGroup>& groups) {
        if (mode == PickMode::Folder) return "dossier";
        std::string ext = "*";
        for (const auto& g : groups)
            if (!g.all()) {
                ext = g.extensions.front();
                break;
            }
        return (mode == PickMode::Save ? "enregistrer:" : "ouvrir:") + ext;
    }

    // ========================================================= emplacements ===
    bool spaceOf(const std::string& folder, std::uint64_t& total, std::uint64_t& free) {
        std::error_code ec;
        const auto info = fs::space(pathOf(folder), ec);
        if (ec || info.capacity == 0 || info.capacity == static_cast<std::uintmax_t>(-1)) return false;
        total = static_cast<std::uint64_t>(info.capacity);
        free = static_cast<std::uint64_t>(info.available);
        return true;
    }

    std::vector<Place> projectPlaces(const std::string& projectDir, const std::string& sourceXpg) {
        std::vector<Place> out;
        std::error_code ec;
        if (!projectDir.empty() && fs::is_directory(pathOf(projectDir), ec)) {
            Place p;
            p.group = Place::Group::Project;
            const auto crumbs = breadcrumb(projectDir);
            p.label = crumbs.back().label;
            p.path = projectDir;
            out.push_back(p);
            for (const char* sub : {"exports", "simulation", "ressources"}) {
                const std::string path = joinPath(projectDir, sub);
                if (!fs::is_directory(pathOf(path), ec)) continue;
                Place s;
                s.group = Place::Group::Project;
                s.label = sub;
                s.path = path;
                out.push_back(s);
            }
        }
        if (!sourceXpg.empty()) {
            const std::string dir = parentOf(sourceXpg);
            if (!dir.empty() && fs::is_directory(pathOf(dir), ec)
                && std::none_of(out.begin(), out.end(), [&dir](const Place& p) { return samePath(p.path, dir); })) {
                Place s;
                s.group = Place::Group::Project;
                s.label = breadcrumb(dir).back().label;
                s.detail = "le dossier du .XPG d'origine";
                s.path = dir;
                out.push_back(s);
            }
        }
        return out;
    }

    std::vector<Place> computerPlaces() {
        std::vector<Place> out;
        std::error_code ec;
        const auto addFolder = [&out, &ec](std::string label, const std::string& path) {
            if (path.empty() || !fs::is_directory(pathOf(path), ec)) return;
            if (std::any_of(out.begin(), out.end(), [&path](const Place& p) { return samePath(p.path, path); })) return;
            Place p;
            p.group = Place::Group::Computer;
            p.label = std::move(label);
            p.path = path;
            out.push_back(std::move(p));
        };
#if defined(_WIN32)
        const auto known = [](REFKNOWNFOLDERID id) {
            PWSTR raw = nullptr;
            std::string path;
            if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw) path = utf8OfWide(raw);
            if (raw) CoTaskMemFree(raw);
            return path;
        };
        addFolder("Bureau", known(FOLDERID_Desktop));
        addFolder("Documents", known(FOLDERID_Documents));
        addFolder("T\xC3\xA9l\xC3\xA9" "chargements", known(FOLDERID_Downloads));
        const DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if ((mask & (1u << i)) == 0) continue;
            const std::string letter(1, static_cast<char>('A' + i));
            const std::wstring root = std::wstring(1, static_cast<wchar_t>(L'A' + i)) + L":\\";
            const UINT type = GetDriveTypeW(root.c_str());
            if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_UNKNOWN) continue;
            Place p;
            p.group = Place::Group::Computer;
            p.family = Family::Drive;
            p.label = letter + ":";
            p.path = letter + ":\\";
            p.removable = type == DRIVE_REMOVABLE;
            p.network = type == DRIVE_REMOTE;
            wchar_t volume[MAX_PATH + 1] = {};
            // Un lecteur sans support (un lecteur de cartes vide) ne repond pas : on le passe.
            const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
            const BOOL ready = GetVolumeInformationW(root.c_str(), volume, MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0);
            SetErrorMode(oldMode);
            if (!ready && type != DRIVE_REMOTE) continue;
            p.detail = utf8OfWide(volume);
            if (p.detail.empty())
                p.detail = type == DRIVE_REMOVABLE ? "Cl\xC3\xA9 USB" : type == DRIVE_REMOTE ? "Lecteur r\xC3\xA9seau" : type == DRIVE_CDROM ? "Lecteur de disque" : "Disque local";
            ULARGE_INTEGER avail{}, total{}, freeAll{};
            if (GetDiskFreeSpaceExW(root.c_str(), &avail, &total, &freeAll)) {
                p.total = static_cast<std::uint64_t>(total.QuadPart);
                p.free = static_cast<std::uint64_t>(avail.QuadPart);
            }
            out.push_back(std::move(p));
        }
#else
        const char* home = std::getenv("HOME");
        const std::string h = home ? home : "";
        // Les dossiers XDG : ~/.config/user-dirs.dirs (XDG_DESKTOP_DIR="$HOME/Bureau").
        std::map<std::string, std::string> xdg;
        if (!h.empty()) {
            std::FILE* f = std::fopen((h + "/.config/user-dirs.dirs").c_str(), "r");
            if (f) {
                char line[1024];
                while (std::fgets(line, sizeof line, f)) {
                    std::string s = trimmed(line);
                    const auto eq = s.find('=');
                    if (s.empty() || s[0] == '#' || eq == std::string::npos) continue;
                    std::string key = s.substr(0, eq), value = s.substr(eq + 1);
                    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
                    if (value.rfind("$HOME", 0) == 0) value = h + value.substr(5);
                    xdg[key] = value;
                }
                std::fclose(f);
            }
        }
        const auto dirOr = [&xdg, &h](const char* key, const char* fallback) {
            const auto it = xdg.find(key);
            return it != xdg.end() ? it->second : (h.empty() ? std::string{} : h + "/" + fallback);
        };
        addFolder("Dossier personnel", h);
        addFolder("Bureau", dirOr("XDG_DESKTOP_DIR", "Desktop"));
        addFolder("Documents", dirOr("XDG_DOCUMENTS_DIR", "Documents"));
        addFolder("T\xC3\xA9l\xC3\xA9" "chargements", dirOr("XDG_DOWNLOAD_DIR", "Downloads"));
        // Les supports : /media/<utilisateur>/<cle>, /run/media/..., /mnt/<x>.
        const auto mounts = [&out, &ec](const std::string& base, int depth) {
            std::vector<std::string> level{base};
            for (int d = 0; d < depth; ++d) {
                std::vector<std::string> next;
                for (const auto& dir : level)
                    for (fs::directory_iterator it(pathOf(dir), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
                         it.increment(ec)) {
                        std::error_code e2;
                        if (it->is_directory(e2)) next.push_back(utf8Of(it->path()));
                    }
                ec.clear();
                level = std::move(next);
            }
            for (const auto& dir : level) {
                Place p;
                p.group = Place::Group::Computer;
                p.family = Family::Drive;
                p.label = breadcrumb(dir).back().label;
                p.path = dir;
                p.removable = base != "/mnt";
                p.detail = p.removable ? "Support amovible" : "Montage";
                (void)spaceOf(dir, p.total, p.free);
                out.push_back(std::move(p));
            }
        };
        Place root;
        root.group = Place::Group::Computer;
        root.family = Family::Drive;
        root.label = "/";
        root.path = "/";
        root.detail = "Syst\xC3\xA8me de fichiers";
        (void)spaceOf("/", root.total, root.free);
        out.push_back(root);
        mounts("/media", 2);
        mounts("/run/media", 2);
        mounts("/mnt", 1);
#endif
        return out;
    }

} // namespace ui::files
