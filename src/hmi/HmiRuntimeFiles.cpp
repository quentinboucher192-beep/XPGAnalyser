// =============================================================================
//  hmi/HmiRuntimeFiles.cpp - 1.12.26 : les fichiers et les ressources des scripts
// -----------------------------------------------------------------------------
//  FILE_READ, FILE_WRITE, DIR_LIST, RESOURCE_READ... : le dialecte lit les
//  arguments et rend le resultat (Interpreter.cpp, fileNative) ; ici, le disque
//  et le projet.
//
//    - un chemin relatif part de <projet>/donnees/ (DIR_DATA() le dit) ; un
//      chemin complet (C:\Donnees\recette.csv) va ou il dit ;
//    - le projet lui-meme (ihm/, project.xpgproj, son dossier) ne s'ecrit pas,
//      ne se deplace pas, ne se supprime pas par un script ;
//    - une expression (fx) lit, n'ecrit rien (HmiExpr.cpp le refuse avant) ;
//    - un texte se lit et s'ecrit tel quel (UTF-8) ; l'en-tete UTF-8 d'un
//      fichier venu d'ailleurs (Bloc-notes, Excel) est ote a la lecture ;
//    - un echec rend faux et sa raison (FILE_ERROR() la dit), dite aussi une
//      fois a la Console.
// =============================================================================
#include "HmiRuntime.hpp"

#include "../core/AtomicFile.hpp"
#include "../sim/DialectLib.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace hmi {

namespace fs = std::filesystem;

namespace {

constexpr std::uintmax_t kMaxRead = 16u * 1024u * 1024u;    // au-dela : trop gros pour un script
constexpr std::size_t    kMaxList = 10000;                  // DIR_LIST : les premiers noms

fs::path pathOf(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string shown(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::string generic(const fs::path& p) {
    const auto u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

void dropBom(std::string& s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB
        && static_cast<unsigned char>(s[2]) == 0xBF)
        s.erase(0, 3);
}

// Les lignes d'un texte : coupe a chaque \n, sans les \r ; un \n final ne fait pas une ligne vide de plus.
std::vector<std::string> linesOf(std::string_view text) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from < text.size()) {
        auto at = text.find('\n', from);
        if (at == std::string_view::npos) at = text.size();
        std::string_view line = text.substr(from, at - from);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.emplace_back(line);
        from = at + 1;
    }
    return out;
}

// Un nom contre un filtre : * (des caracteres, ou aucun), ? (un caractere), sans casse.
bool matches(std::string_view name, std::string_view filter) {
    std::size_t n = 0, f = 0, star = std::string_view::npos, back = 0;
    const auto same = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    while (n < name.size()) {
        if (f < filter.size() && (filter[f] == '?' || same(filter[f], name[n]))) {
            ++n;
            ++f;
        } else if (f < filter.size() && filter[f] == '*') {
            star = f++;
            back = n;
        } else if (star != std::string_view::npos) {
            f = star + 1;
            n = ++back;
        } else {
            return false;
        }
    }
    while (f < filter.size() && filter[f] == '*') ++f;
    return f == filter.size();
}

// L'heure d'un fichier, a l'heure du poste : des millisecondes depuis 1970, comme MAINTENANT().
std::int64_t wallMillis(fs::file_time_type t) {
    const auto sys = std::chrono::system_clock::now()
                   + std::chrono::duration_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now());
    const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    const std::int64_t days = sim::daysFromCivil(tm.tm_year + 1900, static_cast<unsigned>(tm.tm_mon + 1), static_cast<unsigned>(tm.tm_mday));
    return days * 86400000LL + (tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec) * 1000LL;
}

// La raison d'un echec du systeme, en francais pour les cas courants.
std::string reason(const std::error_code& ec) {
    if (ec == std::errc::no_such_file_or_directory) return "introuvable";
    if (ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted) return "acc\xC3\xA8s refus\xC3\xA9";
    if (ec == std::errc::directory_not_empty) return "le dossier n'est pas vide";
    if (ec == std::errc::no_space_on_device) return "le disque est plein";
    if (ec == std::errc::device_or_resource_busy || ec == std::errc::text_file_busy) return "ouvert par un autre programme";
    if (ec == std::errc::file_exists) return "existe d\xC3\xA9j\xC3\xA0";
    if (ec == std::errc::read_only_file_system) return "disque en lecture seule";
    return ec.message();
}

// Le chemin d'un script : relatif, il part de <projet>/donnees/. Pour une ecriture, le projet lui-meme est refuse : son
// dossier ihm/ et ce qu'il contient, project.xpgproj, et tout dossier qui contient ihm/ (le projet, ses parents).
bool resolve(const std::string& projectFolder, const std::string& given, bool writing, fs::path& out, std::string& why) {
    const std::string text = trimmed(given);
    if (text.empty()) {
        why = "le chemin est vide";
        return false;
    }
    fs::path p = pathOf(text);
    if (!p.has_root_name() && !p.has_root_directory()) {
        if (projectFolder.empty()) {
            why = "le projet n'est pas encore enregistr\xC3\xA9 : un chemin relatif part de son dossier donnees/ (enregistre-le, "
                  "ou donne un chemin complet) - " + text;
            return false;
        }
        p = pathOf(projectFolder) / "donnees" / p;
    }
    p = p.lexically_normal();
    if (writing && !projectFolder.empty()) {
        const fs::path root = pathOf(projectFolder).lexically_normal();
        std::string target = lower(generic(p)), ihm = lower(generic(root / "ihm"));
        while (target.size() > 1 && target.back() == '/') target.pop_back();
        const bool insideIhm = target == ihm || target.rfind(ihm + "/", 0) == 0;
        const bool holdsIhm = ihm.rfind(target + "/", 0) == 0;
        const bool projectFile = target == lower(generic(root / "project.xpgproj"));
        if (insideIhm || holdsIhm || projectFile) {
            why = "les fichiers du projet (ihm/, project.xpgproj) ne se changent pas par un script - " + shown(p);
            return false;
        }
    }
    out = p;
    return true;
}

} // namespace

void Runtime::fileFailed(const std::string& op, const std::string& why) {
    if (!fileSaid_.insert(op + "|" + why).second) return;
    logAt(LogLevel::Warning, "Fichier", source_, op + " : " + why + " (FILE_ERROR() le dit au script)");
}

bool Runtime::fileOp(std::string_view op, const std::vector<std::string>& args, sim::Environment::FileResult& out, std::string& why) {
    const std::string u(op);
    const auto fail = [&](std::string w) {
        why = std::move(w);
        fileFailed(u, why);
        return false;
    };
    const auto arg = [&](std::size_t k) { return k < args.size() ? args[k] : std::string{}; };
    if (readOnly_ > 0 && sim::lib::changesFiles(u)) return fail("une fonction appel\xC3\xA9" "e par une expression ne change rien : " + u + " se fait dans un script");
    const std::string folder = hooks_.projectFolder ? hooks_.projectFolder() : std::string{};
    std::error_code ec;

    // ---- les ressources du projet (en memoire : ce que la liste des ressources garde)
    if (u.rfind("RESOURCE_", 0) == 0) {
        if (!project_) return fail("pas de projet");
        if (u == "RESOURCE_LIST") {
            const std::string want = lower(trimmed(arg(0)));
            for (const auto& r : project_->assets.resources)
                if (want.empty() || lower(trimmed(r.folder)) == want) out.lines.push_back(r.name);
            std::sort(out.lines.begin(), out.lines.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
            out.number = static_cast<std::int64_t>(out.lines.size());
            return true;
        }
        const std::string name = trimmed(arg(0));
        const Resource* r = project_->resourceByName(name);
        if (u == "RESOURCE_EXISTS") {
            out.number = r && r->data ? 1 : 0;
            return true;
        }
        if (!r) return fail("le projet n'a pas de ressource '" + name + "' (Ressources)");
        if (!r->data) return fail("le fichier de la ressource '" + r->name + "' manque sur le disque (ihm/ressources/)");
        const auto& bytes = *r->data;
        if (u == "RESOURCE_SIZE") {
            out.number = static_cast<std::int64_t>(bytes.size());
            return true;
        }
        std::string content(bytes.begin(), bytes.end());
        if (u == "RESOURCE_EXTRACT") {
            fs::path to;
            if (!resolve(folder, arg(1), true, to, why)) return fail(why);
            if (fs::is_directory(to, ec)) return fail(shown(to) + " est un dossier : donne le nom du fichier");
            if (const auto st = core::writeFileAtomic(to, content, core::AtomicWrite{false, false}); !st) return fail(st.error().message());
            out.number = 1;
            return true;
        }
        dropBom(content);
        if (u == "RESOURCE_READ") out.text = std::move(content);
        else out.lines = linesOf(content);                                  // RESOURCE_LINES
        out.number = 1;
        return true;
    }
    if (u == "DIR_DATA") {
        out.text = folder.empty() ? std::string{} : generic((pathOf(folder) / "donnees").lexically_normal());
        out.number = 1;
        return true;
    }

    // ---- le disque
    const bool changes = sim::lib::changesFiles(u) && u != "FILE_COPY";   // FILE_COPY lit sa source
    fs::path p;
    if (!resolve(folder, arg(0), changes, p, why)) return fail(why);
    if (u == "FILE_EXISTS") {
        out.number = fs::is_regular_file(p, ec) ? 1 : 0;
        return true;
    }
    if (u == "DIR_EXISTS") {
        out.number = fs::is_directory(p, ec) ? 1 : 0;
        return true;
    }
    const auto missing = [&] { return "introuvable : " + shown(p); };
    if (u == "FILE_SIZE") {
        if (!fs::is_regular_file(p, ec)) return fail(missing());
        const auto n = fs::file_size(p, ec);
        if (ec) return fail(shown(p) + " : " + reason(ec));
        out.number = static_cast<std::int64_t>(n);
        return true;
    }
    if (u == "FILE_TIME") {
        if (!fs::exists(p, ec)) return fail(missing());
        const auto t = fs::last_write_time(p, ec);
        if (ec) return fail(shown(p) + " : " + reason(ec));
        out.number = wallMillis(t);
        return true;
    }
    if (u == "FILE_READ" || u == "FILE_READ_LINES") {
        if (fs::is_directory(p, ec)) return fail(shown(p) + " est un dossier (DIR_LIST le lit)");
        if (!fs::is_regular_file(p, ec)) return fail(missing());
        if (const auto n = fs::file_size(p, ec); !ec && n > kMaxRead)
            return fail(shown(p) + " : trop gros pour un script (" + std::to_string(n / (1024 * 1024)) + " Mo ; 16 Mo au plus)");
        std::ifstream in(p, std::ios::binary);
        if (!in) return fail("ne s'ouvre pas : " + shown(p) + " (ouvert par un autre programme ? les droits ?)");
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        dropBom(data);
        if (u == "FILE_READ") out.text = std::move(data);
        else out.lines = linesOf(data);
        out.number = 1;
        return true;
    }
    if (u == "FILE_WRITE" || u == "FILE_WRITE_LINES" || u == "FILE_APPEND" || u == "FILE_APPEND_LINE") {
        if (fs::is_directory(p, ec)) return fail(shown(p) + " est un dossier : donne le nom du fichier");
        if (p.has_parent_path()) {
            fs::create_directories(p.parent_path(), ec);
            if (ec) return fail("le dossier " + shown(p.parent_path()) + " ne se cr\xC3\xA9" "e pas (" + reason(ec) + ")");
        }
        if (u == "FILE_WRITE" || u == "FILE_WRITE_LINES") {          // d'un bloc : jamais un fichier a moitie ecrit
            if (const auto st = core::writeFileAtomic(p, arg(1), core::AtomicWrite{false, false}); !st) return fail(st.error().message());
            out.number = 1;
            return true;
        }
        std::ofstream f(p, std::ios::binary | std::ios::app);
        if (!f) return fail("ne s'ouvre pas en \xC3\xA9" "criture : " + shown(p) + " (ouvert par un autre programme ? les droits ?)");
        const std::string data = u == "FILE_APPEND_LINE" ? arg(1) + "\n" : arg(1);
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        f.flush();
        if (!f) return fail("l'\xC3\xA9" "criture a \xC3\xA9" "chou\xC3\xA9 : " + shown(p) + " (disque plein ?)");
        out.number = 1;
        return true;
    }
    if (u == "FILE_COPY" || u == "FILE_MOVE") {
        fs::path to;
        if (!resolve(folder, arg(1), true, to, why)) return fail(why);
        if (!fs::exists(p, ec)) return fail(missing());
        if (u == "FILE_COPY" && fs::is_directory(p, ec)) return fail(shown(p) + " est un dossier : FILE_COPY copie un fichier");
        if (fs::is_directory(to, ec) && !fs::is_directory(p, ec)) to /= p.filename();        // dans un dossier : le meme nom
        if (to.has_parent_path()) fs::create_directories(to.parent_path(), ec);
        ec.clear();
        if (u == "FILE_COPY") {
            fs::copy_file(p, to, fs::copy_options::overwrite_existing, ec);
        } else {
            fs::rename(p, to, ec);
            if (ec && !fs::is_directory(p)) {                         // un autre disque : copier, puis retirer
                std::error_code e2;
                if (fs::copy_file(p, to, fs::copy_options::overwrite_existing, e2) && fs::remove(p, e2)) ec.clear();
            }
        }
        if (ec) return fail(shown(p) + " \xE2\x86\x92 " + shown(to) + " : " + reason(ec));
        out.number = 1;
        return true;
    }
    if (u == "FILE_DELETE") {
        if (fs::is_directory(p, ec)) return fail(shown(p) + " est un dossier (DIR_DELETE le supprime)");
        const bool gone = fs::remove(p, ec);
        if (ec) return fail("ne se supprime pas : " + shown(p) + " (" + reason(ec) + ")");
        out.number = gone ? 1 : 0;
        return true;
    }
    if (u == "DIR_CREATE") {
        fs::create_directories(p, ec);
        if (ec) return fail("ne se cr\xC3\xA9" "e pas : " + shown(p) + " (" + reason(ec) + ")");
        out.number = fs::is_directory(p, ec) ? 1 : 0;
        return true;
    }
    if (u == "DIR_DELETE") {
        if (!fs::exists(p, ec)) {
            out.number = 0;
            return true;
        }
        if (!fs::is_directory(p, ec)) return fail(shown(p) + " n'est pas un dossier (FILE_DELETE supprime un fichier)");
        if (arg(1) == "1") fs::remove_all(p, ec);
        else fs::remove(p, ec);
        if (ec == std::errc::directory_not_empty) return fail(shown(p) + " n'est pas vide (DIR_DELETE(chemin, TRUE) supprime aussi ce qu'il contient)");
        if (ec) return fail("ne se supprime pas : " + shown(p) + " (" + reason(ec) + ")");
        out.number = 1;
        return true;
    }
    if (u == "DIR_LIST") {
        if (!fs::is_directory(p, ec)) return fail("dossier introuvable : " + shown(p));
        const std::string filter = trimmed(arg(1));
        for (fs::directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = shown(it->path().filename());
            if (!filter.empty() && !matches(name, filter)) continue;
            std::error_code e2;
            out.lines.push_back(it->is_directory(e2) ? name + "/" : name);
            if (out.lines.size() >= kMaxList) break;
        }
        if (ec) return fail(shown(p) + " : " + reason(ec));
        std::sort(out.lines.begin(), out.lines.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
        out.number = static_cast<std::int64_t>(out.lines.size());
        return true;
    }
    return fail("fonction de fichier inconnue");
}

} // namespace hmi
