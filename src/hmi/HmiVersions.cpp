// =============================================================================
//  hmi/HmiVersions.cpp - les versions du projet (lot 21)
// =============================================================================
#include "HmiVersions.hpp"

#include "HmiCrypto.hpp"
#include "HmiExport.hpp"
#include "HmiStore.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace hmi::ver {

namespace fs = std::filesystem;

namespace {

bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(0, p.size()) == p; }
bool endsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(s.size() - p.size()) == p; }

bool readAll(const fs::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

core::Status writeAll(const fs::path& p, std::string_view content) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + p.string());
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + p.string());
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        std::error_code ec2;
        fs::rename(tmp, p, ec2);
        if (ec2) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + p.string());
    }
    return core::ok();
}

std::string hashOf(std::string_view content) {
    return toHex(sha256(reinterpret_cast<const std::uint8_t*>(content.data()), content.size()));
}

fs::path versionsDir(const std::string& folder) { return fs::path(folder) / "versions"; }
fs::path objectPath(const std::string& folder, const std::string& hash) {
    return versionsDir(folder) / "objets" / hash.substr(0, 2) / hash.substr(2);
}
fs::path manifestPath(const std::string& folder, int number) {
    char name[32];
    std::snprintf(name, sizeof name, "V%04d.txt", number);
    return versionsDir(folder) / name;
}

std::string field(const Record& r, std::string_view key) {
    const auto* v = r.get(key);
    return v ? *v : std::string{};
}
long long fieldNum(const Record& r, std::string_view key) {
    const auto* v = r.get(key);
    if (!v || v->empty()) return 0;
    try { return std::stoll(*v); } catch (...) { return 0; }
}

// "2026-09-26 18:40" -> des minutes (pour "au plus une par heure").
long long minutesOf(std::string_view stamp) {
    // "AAAA-MM-JJ HH:MM" : les chiffres aux places attendues, sinon -1.
    if (stamp.size() < 16) return -1;
    const auto num = [&](std::size_t at, std::size_t len) {
        long long v = 0;
        for (std::size_t i = at; i < at + len; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(stamp[i]))) return -1LL;
            v = v * 10 + (stamp[i] - '0');
        }
        return v;
    };
    const long long y = num(0, 4), mo = num(5, 2), d = num(8, 2), h = num(11, 2), mi = num(14, 2);
    if (y < 0 || mo < 0 || d < 0 || h < 0 || mi < 0) return -1;
    // Des jours approximatifs suffisent : on compare deux dates proches.
    return (((y * 12 + mo) * 31 + d) * 24 + h) * 60 + mi;
}

// Le manifeste de l'API sans ses dates : un enregistrement qui ne change que
// "modified" n'est pas un changement.
std::string withoutStamps(std::string_view text) {
    std::string out;
    for (const auto& line : splitLines(text))
        if (!startsWith(line, "modified") && !startsWith(line, "created")) out += line + "\n";
    return out;
}

bool binary(std::string_view s) { return s.find('\0') != std::string_view::npos; }

// sections/index.txt, units/index.txt... : "nom ; ... ; fichier" -> le nom d'un fichier.
std::string sectionNameOf(const std::string& indexText, const std::string& file) {
    for (const auto& line : splitLines(indexText)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        std::string cur;
        for (char ch : line) {
            if (ch == ';') { f.push_back(cur); cur.clear(); } else cur += ch;
        }
        f.push_back(cur);
        auto trim = [](std::string s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
            while (!s.empty() && s.front() == ' ') s.erase(0, 1);
            return s;
        };
        if (f.size() >= 7 && trim(f[6]) == file) return trim(f[0]);
    }
    return {};
}

std::string stem(const std::string& path) {
    const auto slash = path.find_last_of('/');
    std::string s = slash == std::string::npos ? path : path.substr(slash + 1);
    const auto dot = s.find_last_of('.');
    return dot == std::string::npos ? s : s.substr(0, dot);
}

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string arrow() { return " \xE2\x80\xBA "; }

}   // namespace

// ---- les etats ----------------------------------------------------------------
std::string_view stateLabel(State s) noexcept {
    switch (s) {
        case State::Draft: return "Brouillon";
        case State::Validated: return "Valid\xC3\xA9" "e";
        case State::Delivered: return "Livr\xC3\xA9" "e";
        case State::Auto: return "auto";
        case State::BeforeRestore: return "avant restauration";
    }
    return "Brouillon";
}
std::string_view stateKey(State s) noexcept {
    switch (s) {
        case State::Draft: return "brouillon";
        case State::Validated: return "validee";
        case State::Delivered: return "livree";
        case State::Auto: return "auto";
        case State::BeforeRestore: return "avant_restauration";
    }
    return "brouillon";
}
State stateFromKey(std::string_view k) noexcept {
    if (k == "validee") return State::Validated;
    if (k == "livree") return State::Delivered;
    if (k == "auto") return State::Auto;
    if (k == "avant_restauration") return State::BeforeRestore;
    return State::Draft;
}
std::string_view autoLabel(AutoMode m) noexcept {
    switch (m) {
        case AutoMode::Never: return "jamais";
        case AutoMode::EverySave: return "\xC3\xA0 chaque enregistrement";
        case AutoMode::Hourly: return "au plus une par heure";
    }
    return "jamais";
}
std::string_view autoKey(AutoMode m) noexcept {
    switch (m) {
        case AutoMode::Never: return "jamais";
        case AutoMode::EverySave: return "enregistrement";
        case AutoMode::Hourly: return "heure";
    }
    return "jamais";
}
AutoMode autoFromKey(std::string_view k) noexcept {
    if (k == "enregistrement") return AutoMode::EverySave;
    if (k == "heure") return AutoMode::Hourly;
    return AutoMode::Never;
}

std::string Version::label() const {
    std::string n = "V" + std::to_string(number);
    if (state == State::Auto) return n + " \xC2\xB7 automatique";
    return n + " \xC2\xB7 " + (name.empty() ? std::string(stateLabel(state)) : name);
}

const Version* Store::find(int number) const noexcept {
    for (const auto& v : versions) if (v.number == number) return &v;
    return nullptr;
}
Version* Store::find(int number) noexcept {
    for (auto& v : versions) if (v.number == number) return &v;
    return nullptr;
}
const Version* Store::last() const noexcept { return versions.empty() ? nullptr : &versions.back(); }
int Store::nextNumber() const noexcept {
    int n = 0;
    for (const auto& v : versions) n = std::max(n, v.number);
    return n + 1;
}
std::uint64_t Store::storedBytes() const {
    std::uint64_t total = 0;
    std::error_code ec;
    const fs::path dir = versionsDir(folder);
    if (!fs::is_directory(dir, ec)) return 0;
    for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (it->is_regular_file(ec2)) total += static_cast<std::uint64_t>(it->file_size(ec2));
    }
    return total;
}
std::uint64_t Store::copyBytes() const noexcept {
    std::uint64_t total = 0;
    for (const auto& v : versions) total += v.bytes;
    return total;
}

// ---- ce qui entre dans une version ------------------------------------------------
bool included(std::string_view p) {
    if (p.empty() || endsWith(p, ".tmp") || endsWith(p, "~")) return false;
    if (p == "project.xpgproj" || p == "project.lock") return true;
    static const char* const kDirs[] = {"config/", "vars/", "ddt/", "dfb/", "units/", "sections/", "tables/", "ihm/", "donnees/"};
    for (const char* d : kDirs)
        if (startsWith(p, d)) return !startsWith(p, "ihm/historique/") && !startsWith(p, "ihm/corbeille/");
    return false;
}

std::vector<FileEntry> scan(const std::string& projectFolder) {
    std::vector<FileEntry> out;
    std::error_code ec;
    const fs::path root(projectFolder);
    if (projectFolder.empty() || !fs::is_directory(root, ec)) return out;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        const std::string rel = fs::relative(it->path(), root, ec2).generic_string();
        if (it->is_directory(ec2)) {
            if (rel == "versions" || rel == "exports" || rel == ".xpg" || rel == "ihm/historique" || rel == "ihm/corbeille") it.disable_recursion_pending();   // 1.11.13 : .xpg (le build)
            continue;
        }
        if (!it->is_regular_file(ec2) || !included(rel)) continue;
        std::string content;
        if (!readAll(it->path(), content)) continue;
        out.push_back({rel, static_cast<std::uint64_t>(content.size()), hashOf(content)});
    }
    std::sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) { return a.path < b.path; });
    return out;
}

// ---- le magasin ----------------------------------------------------------------------
core::Result<Store> open(const std::string& projectFolder) {
    Store s;
    s.folder = projectFolder;
    std::string text;
    if (!readAll(versionsDir(projectFolder) / "index.txt", text)) return s;
    std::size_t lineNo = 0;
    for (const auto& line : splitLines(text)) {
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;
        Record r;
        std::string error;
        if (!parseRecord(line, r, error))
            return core::fail(core::ErrorCode::XmlMalformed, "versions/index.txt, ligne " + std::to_string(lineNo) + " : " + error);
        if (r.word == "reglages") {
            s.autoMode = autoFromKey(field(r, "auto"));
            if (const auto k = fieldNum(r, "garder_auto"); k > 0) s.keepAuto = static_cast<int>(k);
        } else if (r.word == "version") {
            Version v;
            v.number = static_cast<int>(fieldNum(r, "n"));
            v.name = field(r, "nom");
            v.state = stateFromKey(field(r, "etat"));
            v.date = field(r, "date");
            v.author = field(r, "auteur");
            v.base = static_cast<int>(fieldNum(r, "base"));
            v.fileCount = static_cast<std::size_t>(fieldNum(r, "fichiers"));
            v.bytes = static_cast<std::uint64_t>(fieldNum(r, "octets"));
            v.newBytes = static_cast<std::uint64_t>(fieldNum(r, "nouveaux"));
            v.fingerprint = field(r, "empreinte");
            v.since = field(r, "depuis");
            v.comment = field(r, "commentaire");
            if (v.number > 0) s.versions.push_back(std::move(v));
        }
    }
    std::sort(s.versions.begin(), s.versions.end(), [](const Version& a, const Version& b) { return a.number < b.number; });
    return s;
}

core::Status saveIndex(const Store& s) {
    std::string text = "# XpgAnalyzer - les versions du projet. Les fichiers de chacune : V0001.txt... ;\n"
                       "# leur contenu, une fois par empreinte : objets/.\n";
    text += "reglages auto=" + std::string(autoKey(s.autoMode)) + " garder_auto=" + std::to_string(s.keepAuto) + "\n";
    for (const auto& v : s.versions) {
        text += "version n=" + std::to_string(v.number) + " nom=" + quote(v.name) + " etat=" + std::string(stateKey(v.state))
              + " date=" + quote(v.date) + " auteur=" + quote(v.author) + " base=" + std::to_string(v.base)
              + " fichiers=" + std::to_string(v.fileCount) + " octets=" + std::to_string(v.bytes)
              + " nouveaux=" + std::to_string(v.newBytes) + " empreinte=" + quote(v.fingerprint)
              + " depuis=" + quote(v.since) + " commentaire=" + quote(v.comment) + "\n";
    }
    return writeAll(versionsDir(s.folder) / "index.txt", text);
}

core::Result<std::vector<FileEntry>> filesOf(const Store& s, int number) {
    if (number == 0) return scan(s.folder);
    if (!s.find(number)) return core::fail(core::ErrorCode::InvalidArgument, "pas de version V" + std::to_string(number));
    std::string text;
    if (!readAll(manifestPath(s.folder, number), text))
        return core::fail(core::ErrorCode::FileNotFound, "la liste des fichiers de V" + std::to_string(number) + " manque");
    std::vector<FileEntry> out;
    for (const auto& line : splitLines(text)) {
        if (line.empty() || line[0] == '#') continue;
        const auto a = line.find(' ');
        const auto b = a == std::string::npos ? std::string::npos : line.find(' ', a + 1);
        if (b == std::string::npos) continue;
        FileEntry e;
        e.hash = line.substr(0, a);
        try { e.size = static_cast<std::uint64_t>(std::stoull(line.substr(a + 1, b - a - 1))); } catch (...) { e.size = 0; }
        e.path = line.substr(b + 1);
        out.push_back(std::move(e));
    }
    return out;
}

core::Result<std::string> contentOf(const Store& s, int number, const std::string& path) {
    std::string content;
    if (number == 0) {
        if (!readAll(fs::path(s.folder) / path, content)) return core::fail(core::ErrorCode::FileNotFound, path);
        return content;
    }
    auto files = filesOf(s, number);
    if (!files) return core::fail(files.error().code, files.error().context);
    for (const auto& f : *files)
        if (f.path == path) {
            if (!readAll(objectPath(s.folder, f.hash), content))
                return core::fail(core::ErrorCode::FileNotFound, "le contenu de " + path + " manque dans versions/objets");
            return content;
        }
    return core::fail(core::ErrorCode::FileNotFound, path + " n'est pas dans V" + std::to_string(number));
}

core::Result<Version> create(Store& s, const std::string& name, State state, const std::string& comment,
                             const std::string& author, const std::string& now) {
    const auto files = scan(s.folder);
    if (files.empty())
        return core::fail(core::ErrorCode::InvalidArgument, "rien \xC3\xA0 garder : le projet n'est pas encore enregistr\xC3\xA9 dans son dossier");
    Version v;
    v.number = s.nextNumber();
    v.name = name;
    v.state = state;
    v.comment = comment;
    v.author = author;
    v.date = now.empty() ? nowStamp() : now;
    v.base = s.last() ? s.last()->number : 0;
    v.fileCount = files.size();
    std::string manifest = "# XpgAnalyzer - la version " + std::to_string(v.number) + " : empreinte SHA-256, taille, chemin\n";
    for (const auto& f : files) {
        v.bytes += f.size;
        manifest += f.hash + " " + std::to_string(f.size) + " " + f.path + "\n";
        std::error_code ec;
        const fs::path obj = objectPath(s.folder, f.hash);
        if (fs::exists(obj, ec)) continue;
        std::string content;
        if (!readAll(fs::path(s.folder) / f.path, content))
            return core::fail(core::ErrorCode::FileUnreadable, "lecture impossible : " + f.path);
        if (hashOf(content) != f.hash)
            return core::fail(core::ErrorCode::FileUnreadable, f.path + " a chang\xC3\xA9 pendant la cr\xC3\xA9" "ation de la version : recommence");
        if (auto st = writeAll(obj, content); !st) return core::Err<core::Error>(st.error());
        v.newBytes += f.size;
    }
    v.fingerprint = hashOf(manifest).substr(0, 12);
    if (auto st = writeAll(manifestPath(s.folder, v.number), manifest); !st) return core::Err<core::Error>(st.error());
    // Ce qui a change depuis la precedente (la colonne "Depuis la precedente").
    if (v.base > 0) {
        if (auto c = compare(s, v.base, 0)) v.since = c->elements.empty() ? std::string("aucun changement") : c->summary();
    } else {
        v.since = "la premi\xC3\xA8re";
    }
    s.versions.push_back(v);
    if (auto st = saveIndex(s); !st) return core::Err<core::Error>(st.error());
    return v;
}

core::Status update(Store& s, int number, const std::string& name, State state, const std::string& comment) {
    auto* v = s.find(number);
    if (!v) return core::fail(core::ErrorCode::InvalidArgument, "pas de version V" + std::to_string(number));
    v->name = name;
    v->state = state;
    v->comment = comment;
    return saveIndex(s);
}

core::Status remove(Store& s, int number) {
    const auto it = std::find_if(s.versions.begin(), s.versions.end(), [&](const Version& v) { return v.number == number; });
    if (it == s.versions.end()) return core::fail(core::ErrorCode::InvalidArgument, "pas de version V" + std::to_string(number));
    s.versions.erase(it);
    std::error_code ec;
    fs::remove(manifestPath(s.folder, number), ec);
    if (auto st = saveIndex(s); !st) return st;
    // Les objets que plus aucune version ne cite.
    std::set<std::string> kept;
    for (const auto& v : s.versions)
        if (auto files = filesOf(s, v.number))
            for (const auto& f : *files) kept.insert(f.hash);
    const fs::path objects = versionsDir(s.folder) / "objets";
    if (!fs::is_directory(objects, ec)) return core::ok();
    std::vector<fs::path> gone;
    for (fs::recursive_directory_iterator it2(objects, ec), end; it2 != end; it2.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it2->is_regular_file(ec2)) continue;
        const std::string hash = it2->path().parent_path().filename().string() + it2->path().filename().string();
        if (!kept.count(hash)) gone.push_back(it2->path());
    }
    for (const auto& p : gone) {
        fs::remove(p, ec);
        std::error_code ec3;
        if (fs::is_empty(p.parent_path(), ec3)) fs::remove(p.parent_path(), ec3);
    }
    return core::ok();
}

bool changedSinceLast(const Store& s) {
    const auto* last = s.last();
    if (!last) return !scan(s.folder).empty();
    auto c = compare(s, last->number, 0);
    return !c || !c->elements.empty();
}

core::Result<std::optional<Version>> restore(Store& s, int number, const std::string& author, const std::string& now) {
    if (!s.find(number)) return core::fail(core::ErrorCode::InvalidArgument, "pas de version V" + std::to_string(number));
    auto target = filesOf(s, number);
    if (!target) return core::fail(target.error().code, target.error().context);
    std::optional<Version> before;
    if (changedSinceLast(s)) {
        auto b = create(s, "Avant restauration de V" + std::to_string(number), State::BeforeRestore,
                        "Le projet tel qu'il \xC3\xA9tait avant de revenir \xC3\xA0 V" + std::to_string(number) + ".", author, now);
        if (!b) return core::fail(b.error().code, b.error().context);
        before = *b;
    }
    const auto current = scan(s.folder);
    std::map<std::string, const FileEntry*> wanted;
    for (const auto& f : *target) wanted[f.path] = &f;
    std::error_code ec;
    const fs::path root(s.folder);
    // 1. ce que la version n'a pas s'en va (et les dossiers vides qu'il laisse)
    for (const auto& f : current) {
        if (wanted.count(f.path)) continue;
        fs::path p = root / f.path;
        fs::remove(p, ec);
        for (fs::path dir = p.parent_path(); dir != root && !dir.empty(); dir = dir.parent_path()) {
            std::error_code ec2;
            if (!fs::is_empty(dir, ec2) || ec2) break;
            fs::remove(dir, ec2);
        }
    }
    // 2. ce qui differe est reecrit
    std::map<std::string, std::string> have;
    for (const auto& f : current) have[f.path] = f.hash;
    for (const auto& f : *target) {
        if (const auto h = have.find(f.path); h != have.end() && h->second == f.hash) continue;
        std::string content;
        if (!readAll(objectPath(s.folder, f.hash), content))
            return core::fail(core::ErrorCode::FileNotFound, "le contenu de " + f.path + " manque dans versions/objets");
        if (auto st = writeAll(root / f.path, content); !st) return core::Err<core::Error>(st.error());
    }
    return before;
}

core::Status extract(const Store& s, int number, const std::string& toFolder) {
    auto files = filesOf(s, number);
    if (!files) return core::Err<core::Error>(files.error());
    std::error_code ec;
    const fs::path root(toFolder);
    if (fs::exists(root, ec) && !fs::is_empty(root, ec))
        return core::fail(core::ErrorCode::InvalidArgument, "le dossier " + toFolder + " n'est pas vide : choisis-en un neuf");
    for (const auto& f : *files) {
        std::string content;
        if (number == 0 ? !readAll(fs::path(s.folder) / f.path, content) : !readAll(objectPath(s.folder, f.hash), content))
            return core::fail(core::ErrorCode::FileNotFound, "le contenu de " + f.path + " manque");
        if (auto st = writeAll(root / f.path, content); !st) return st;
    }
    return core::ok();
}

core::Status exportZip(const Store& s, int number, const std::string& zipPath) {
    auto files = filesOf(s, number);
    if (!files) return core::Err<core::Error>(files.error());
    std::vector<std::pair<std::string, std::string>> items;
    std::string about = "XpgAnalyzer - le projet";
    if (const auto* v = s.find(number)) {
        about += ", version " + std::to_string(v->number) + " : " + v->name + "\r\n\xC3\x89tat : " + std::string(stateLabel(v->state))
               + "\r\nDate : " + v->date + "\r\nAuteur : " + v->author + "\r\nCommentaire : " + v->comment
               + "\r\nEmpreinte : " + v->fingerprint + "\r\n";
    } else {
        about += " tel qu'il est enregistr\xC3\xA9\r\n";
    }
    about += "Ouvrir : d\xC3\xA9" "compresser, puis Projet > Ouvrir sur le dossier.\r\n";
    items.emplace_back("VERSION.txt", about);
    for (const auto& f : *files) {
        std::string content;
        if (number == 0 ? !readAll(fs::path(s.folder) / f.path, content) : !readAll(objectPath(s.folder, f.hash), content))
            return core::fail(core::ErrorCode::FileNotFound, "le contenu de " + f.path + " manque");
        items.emplace_back(f.path, std::move(content));
    }
    const Bytes zip = zipStored(items);
    return writeAll(fs::path(zipPath), std::string_view(reinterpret_cast<const char*>(zip.data()), zip.size()));
}

core::Result<std::optional<Version>> afterSave(Store& s, const std::string& author, const std::string& now) {
    if (s.autoMode == AutoMode::Never) return std::optional<Version>{};
    if (!changedSinceLast(s)) return std::optional<Version>{};
    const std::string stamp = now.empty() ? nowStamp() : now;
    if (s.autoMode == AutoMode::Hourly) {
        for (auto it = s.versions.rbegin(); it != s.versions.rend(); ++it)
            if (it->state == State::Auto) {
                const long long a = minutesOf(it->date), b = minutesOf(stamp);
                if (a >= 0 && b >= 0 && b - a < 60) return std::optional<Version>{};
                break;
            }
    }
    auto v = create(s, "", State::Auto, "", author, stamp);
    if (!v) return core::fail(v.error().code, v.error().context);
    // Les plus anciennes automatiques s'en vont (jamais une version nommee).
    std::vector<int> autos;
    for (const auto& x : s.versions) if (x.state == State::Auto) autos.push_back(x.number);
    while (static_cast<int>(autos.size()) > std::max(1, s.keepAuto)) {
        if (auto st = remove(s, autos.front()); !st) break;
        autos.erase(autos.begin());
    }
    return std::optional<Version>{*v};
}

// ---- comparer ---------------------------------------------------------------------------
std::size_t Comparison::count(Side side) const noexcept {
    return static_cast<std::size_t>(std::count_if(elements.begin(), elements.end(), [&](const Element& e) { return e.side == side; }));
}

std::string Comparison::summary(std::size_t maxParts) const {
    // Par categorie : "+2 vues", "? 14 variables", "? Matrice" (un seul element : son nom).
    struct Part { std::string category; std::size_t added{0}, removed{0}, modified{0}; std::string single; int items{0}; };
    std::vector<Part> parts;
    for (const auto& e : elements) {
        auto it = std::find_if(parts.begin(), parts.end(), [&](const Part& p) { return p.category == e.category; });
        if (it == parts.end()) {
            Part fresh;
            fresh.category = e.category;
            parts.push_back(fresh);
            it = parts.end() - 1;
        }
        // Une liste (les variables) compte ses articles.
        if (e.kind == "liste" && (e.added + e.removed + e.modified) > 0) {
            it->added += static_cast<std::size_t>(e.added);
            it->removed += static_cast<std::size_t>(e.removed);
            it->modified += static_cast<std::size_t>(e.modified);
        } else if (e.change == Change::Added) ++it->added;
        else if (e.change == Change::Removed) ++it->removed;
        else ++it->modified;
        it->single = e.name;
        ++it->items;
    }
    const auto noun = [](const std::string& category, std::size_t n) {
        std::string c = category;
        for (auto& ch : c) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (n == 1 && c.size() > 1 && c.back() == 's') c.pop_back();
        return c;
    };
    std::vector<std::string> out;
    for (const auto& p : parts) {
        if (p.items == 1 && p.added + p.removed + p.modified <= 1) {
            out.push_back(std::string(p.added ? "+" : p.removed ? "\xE2\x88\x92" : "\xE2\x9C\x8E ") + p.single);
            continue;
        }
        if (p.added) out.push_back("+" + std::to_string(p.added) + " " + noun(p.category, p.added));
        if (p.modified) out.push_back("\xE2\x9C\x8E " + std::to_string(p.modified) + " " + noun(p.category, p.modified));
        if (p.removed) out.push_back("\xE2\x88\x92" + std::to_string(p.removed) + " " + noun(p.category, p.removed));
    }
    std::string text;
    for (std::size_t i = 0; i < out.size() && i < maxParts; ++i) text += (i ? " \xC2\xB7 " : "") + out[i];
    if (out.size() > maxParts) text += " \xC2\xB7 \xE2\x80\xA6";
    return text;
}

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at <= text.size()) {
        const auto nl = text.find('\n', at);
        std::string line(text.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (nl == std::string_view::npos) {
            if (!line.empty()) out.push_back(std::move(line));
            break;
        }
        out.push_back(std::move(line));
        at = nl + 1;
    }
    return out;
}

std::vector<DiffLine> diffLines(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    // Les debuts et les fins communs d'abord (le cas courant : quelques lignes
    // changees dans un long texte), puis la plus longue sous-suite commune.
    std::size_t pre = 0;
    while (pre < a.size() && pre < b.size() && a[pre] == b[pre]) ++pre;
    std::size_t suf = 0;
    while (suf < a.size() - pre && suf < b.size() - pre && a[a.size() - 1 - suf] == b[b.size() - 1 - suf]) ++suf;
    const std::size_t n = a.size() - pre - suf, m = b.size() - pre - suf;
    std::vector<DiffLine> out;
    for (std::size_t i = 0; i < pre; ++i) out.push_back({DiffLine::Same, static_cast<int>(i), static_cast<int>(i)});
    std::vector<DiffLine> mid;
    if (n > 0 || m > 0) {
        if (n * m > 4000000) {
            // Trop long pour la table : tout le milieu retire, puis ajoute.
            for (std::size_t i = 0; i < n; ++i) mid.push_back({DiffLine::Removed, static_cast<int>(pre + i), -1});
            for (std::size_t j = 0; j < m; ++j) mid.push_back({DiffLine::Added, -1, static_cast<int>(pre + j)});
        } else {
            std::vector<std::vector<int>> L(n + 1, std::vector<int>(m + 1, 0));
            for (std::size_t i = n; i-- > 0;)
                for (std::size_t j = m; j-- > 0;)
                    L[i][j] = a[pre + i] == b[pre + j] ? L[i + 1][j + 1] + 1 : std::max(L[i + 1][j], L[i][j + 1]);
            std::size_t i = 0, j = 0;
            while (i < n || j < m) {
                if (i < n && j < m && a[pre + i] == b[pre + j]) {
                    mid.push_back({DiffLine::Same, static_cast<int>(pre + i), static_cast<int>(pre + j)});
                    ++i; ++j;
                } else if (i < n && (j == m || L[i + 1][j] >= L[i][j + 1])) {
                    // Retiree d'abord : une ligne remplacee se lit "retiree, puis ajoutee".
                    mid.push_back({DiffLine::Removed, static_cast<int>(pre + i), -1});
                    ++i;
                } else {
                    mid.push_back({DiffLine::Added, -1, static_cast<int>(pre + j)});
                    ++j;
                }
            }
        }
    }
    // Une suite de lignes retirees suivie d'autant d'ajoutees : "modifiees", face a face.
    for (std::size_t k = 0; k < mid.size();) {
        if (mid[k].kind != DiffLine::Removed) { out.push_back(mid[k]); ++k; continue; }
        std::size_t r = k;
        while (r < mid.size() && mid[r].kind == DiffLine::Removed) ++r;
        std::size_t ad = r;
        while (ad < mid.size() && mid[ad].kind == DiffLine::Added) ++ad;
        const std::size_t removed = r - k, added = ad - r;
        const std::size_t pairs = std::min(removed, added);
        for (std::size_t p = 0; p < pairs; ++p) out.push_back({DiffLine::Changed, mid[k + p].left, mid[r + p].right});
        for (std::size_t p = pairs; p < removed; ++p) out.push_back(mid[k + p]);
        for (std::size_t p = pairs; p < added; ++p) out.push_back(mid[r + p]);
        k = ad;
    }
    for (std::size_t i = 0; i < suf; ++i)
        out.push_back({DiffLine::Same, static_cast<int>(a.size() - suf + i), static_cast<int>(b.size() - suf + i)});
    return out;
}

namespace {

// Les noms lisibles des proprietes les plus courantes (sinon : la cle).
std::string propLabel(const std::string& key) {
    static const std::map<std::string, std::string> labels = {
        {"x", "X"}, {"y", "Y"}, {"w", "Largeur"}, {"h", "Hauteur"}, {"rot", "Rotation"},
        {"fill", "Couleur de fond"}, {"stroke", "Contour"}, {"strokeWidth", "\xC3\x89paisseur du contour"},
        {"text", "Texte"}, {"value", "Valeur"}, {"label", "Libell\xC3\xA9"}, {"visible", "Visibilit\xC3\xA9"},
        {"image", "Image"}, {"font", "Police"}, {"fontSize", "Taille du texte"}, {"color", "Couleur du texte"},
        {"variable", "Variable"}, {"opacity", "Opacit\xC3\xA9"}, {"radius", "Rayon"}, {"style", "Style"}};
    const auto it = labels.find(key);
    return it == labels.end() ? key : it->second;
}

std::string shortValue(const Prop& p) {
    std::string v = p.expr.empty() ? p.value : "{" + p.expr + "}";
    if (v.size() > 40) v = v.substr(0, 37) + "\xE2\x80\xA6";
    return v.empty() ? std::string("(vide)") : v;
}

std::string viewCategory(const View& v) {
    if (v.role == "popup") return "Popups";
    if (v.role == "symbole") return "Symboles";
    if (v.role == "modele" || v.role == "entete" || v.role == "pied") return "Mod\xC3\xA8les";
    return "Vues";
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Un element "texte" entre deux textes (un script, une fonction...).
void textElement(Element& e, const std::string& a, const std::string& b) {
    const auto d = diffLines(splitLines(a), splitLines(b));
    for (const auto& l : d) {
        if (l.kind == DiffLine::Added) ++e.added;
        else if (l.kind == DiffLine::Removed) ++e.removed;
        else if (l.kind == DiffLine::Changed) { ++e.added; ++e.removed; }
    }
    e.detail = "+" + std::to_string(e.added) + " \xE2\x88\x92" + std::to_string(e.removed) + " lignes";
}

// Une liste nommee (alarmes, recettes, styles...) : un element par article qui change.
template <class T, class Name>
void listByName(std::vector<Element>& out, const std::vector<T>& a, const std::vector<T>& b, Name nameOf,
                const std::string& category, const std::string& where, const std::string& keyPrefix,
                const char* addedWord, const char* removedWord, const char* modifiedWord) {
    std::map<std::string, const T*> ma, mb;
    for (const auto& x : a) ma[lower(nameOf(x))] = &x;
    for (const auto& x : b) mb[lower(nameOf(x))] = &x;
    std::set<std::string> keys;
    for (const auto& [k, v] : ma) keys.insert(k);
    for (const auto& [k, v] : mb) keys.insert(k);
    for (const auto& k : keys) {
        const T* x = ma.count(k) ? ma[k] : nullptr;
        const T* y = mb.count(k) ? mb[k] : nullptr;
        if (x && y && *x == *y) continue;
        Element e;
        e.side = Side::Ihm;
        e.category = category;
        e.name = nameOf(y ? *y : *x);
        e.where = where + arrow() + e.name;
        e.change = !x ? Change::Added : !y ? Change::Removed : Change::Modified;
        e.detail = !x ? addedWord : !y ? removedWord : modifiedWord;
        e.kind = "element";
        e.key = keyPrefix + e.name;
        out.push_back(std::move(e));
    }
}

void compareIhm(const Project& A, const Project& B, std::vector<Element>& out) {
    const std::string ihm = "IHM";
    // Les vues, par identifiant (un renommage reste la meme vue).
    std::map<Id, const View*> va, vb;
    for (const auto& v : A.views) va[v.id] = &v;
    for (const auto& v : B.views) vb[v.id] = &v;
    std::set<Id> ids;
    for (const auto& [k, v] : va) ids.insert(k);
    for (const auto& [k, v] : vb) ids.insert(k);
    for (Id id : ids) {
        const View* x = va.count(id) ? va[id] : nullptr;
        const View* y = vb.count(id) ? vb[id] : nullptr;
        if (x && y && *x == *y) continue;
        const View& any = y ? *y : *x;
        Element e;
        e.side = Side::Ihm;
        e.category = viewCategory(any);
        e.name = any.name;
        e.where = ihm + arrow() + e.category + arrow() + e.name;
        e.kind = "vue";
        e.key = "vue:" + std::to_string(id);
        if (!x) {
            e.change = Change::Added;
            e.detail = "nouvelle (" + plural(y->objects.size(), "objet", "objets") + ")";
        } else if (!y) {
            e.change = Change::Removed;
            e.detail = "retir\xC3\xA9" "e (" + plural(x->objects.size(), "objet", "objets") + ")";
        } else {
            e.change = Change::Modified;
            const auto changes = compareViews(*x, *y);
            std::size_t add = 0, rem = 0, mod = 0;
            for (const auto& c : changes) (c.change == Change::Added ? add : c.change == Change::Removed ? rem : mod)++;
            std::vector<std::string> parts;
            if (x->name != y->name) parts.push_back("renomm\xC3\xA9" "e (" + x->name + ")");
            if (mod) parts.push_back(plural(mod, "objet modifi\xC3\xA9", "objets modifi\xC3\xA9s"));
            if (add) parts.push_back(plural(add, "ajout\xC3\xA9", "ajout\xC3\xA9s"));
            if (rem) parts.push_back(plural(rem, "retir\xC3\xA9", "retir\xC3\xA9s"));
            if (parts.empty()) parts.push_back("ses r\xC3\xA9glages (taille, calques, scripts, actions)");
            for (std::size_t i = 0; i < parts.size(); ++i) e.detail += (i ? ", " : "") + parts[i];
            e.added = static_cast<int>(add);
            e.removed = static_cast<int>(rem);
        }
        out.push_back(std::move(e));
    }
    // Les scripts generaux et les fonctions : du texte.
    const auto texts = [&](const auto& la, const auto& lb, const std::string& category, const std::string& prefix) {
        std::map<Id, const std::decay_t<decltype(la.front())>*> ma, mb;
        for (const auto& s : la) ma[s.id] = &s;
        for (const auto& s : lb) mb[s.id] = &s;
        std::set<Id> keys;
        for (const auto& [k, v] : ma) keys.insert(k);
        for (const auto& [k, v] : mb) keys.insert(k);
        for (Id k : keys) {
            const auto* x = ma.count(k) ? ma[k] : nullptr;
            const auto* y = mb.count(k) ? mb[k] : nullptr;
            if (x && y && *x == *y) continue;
            Element e;
            e.side = Side::Ihm;
            e.category = category;
            e.name = (y ? y : x)->name;
            e.where = ihm + arrow() + category + arrow() + e.name;
            e.kind = "texte";
            e.key = prefix + std::to_string(k);
            e.change = !x ? Change::Added : !y ? Change::Removed : Change::Modified;
            textElement(e, x ? x->body : std::string{}, y ? y->body : std::string{});
            if (!x) e.detail = "nouveau (" + std::to_string(e.added) + " lignes)";
            else if (!y) e.detail = "retir\xC3\xA9 (" + std::to_string(e.removed) + " lignes)";
            out.push_back(std::move(e));
        }
    };
    texts(A.programs.scripts, B.programs.scripts, "Scripts", "script:");
    texts(A.programs.functions, B.programs.functions, "Fonctions", "fonction:");
    // Les variables IHM : une liste, un element.
    if (A.programs.variables != B.programs.variables) {
        std::map<std::string, const Variable*> ma, mb;
        for (const auto& v : A.programs.variables) ma[lower(v.name)] = &v;
        for (const auto& v : B.programs.variables) mb[lower(v.name)] = &v;
        std::vector<std::string> added, removed, modified;
        for (const auto& [k, v] : mb) if (!ma.count(k)) added.push_back(v->name); else if (!(*ma[k] == *v)) modified.push_back(v->name);
        for (const auto& [k, v] : ma) if (!mb.count(k)) removed.push_back(v->name);
        if (!added.empty() || !removed.empty() || !modified.empty()) {
            Element e;
            e.side = Side::Ihm;
            e.category = "Variables IHM";
            e.name = "Variables IHM";
            e.where = ihm + arrow() + "Variables IHM";
            e.kind = "liste";
            e.key = "variables";
            e.change = Change::Modified;
            e.added = static_cast<int>(added.size());
            e.removed = static_cast<int>(removed.size());
            e.modified = static_cast<int>(modified.size());
            std::vector<std::string> parts;
            if (!added.empty()) parts.push_back("+" + std::to_string(added.size()));
            if (!modified.empty()) parts.push_back("\xE2\x9C\x8E" + std::to_string(modified.size()));
            if (!removed.empty()) parts.push_back("\xE2\x88\x92" + std::to_string(removed.size()));
            std::string names;
            for (std::size_t i = 0; i < added.size() && i < 3; ++i) names += (names.empty() ? "" : ", ") + added[i];
            for (std::size_t i = 0; i < modified.size() && i < 3 && names.size() < 60; ++i) names += (names.empty() ? "" : ", ") + modified[i];
            for (std::size_t i = 0; i < parts.size(); ++i) e.detail += (i ? " " : "") + parts[i];
            if (!names.empty()) e.detail += " : " + names + (added.size() + modified.size() > 3 ? "\xE2\x80\xA6" : "");
            out.push_back(std::move(e));
        }
    }
    if (A.programs.types != B.programs.types) {
        Element e;
        e.side = Side::Ihm;
        e.category = "Types IHM";
        e.name = "Types IHM";
        e.where = ihm + arrow() + "Types IHM";
        e.kind = "element";
        e.key = "types";
        e.detail = plural(B.programs.types.size(), "type", "types");
        out.push_back(std::move(e));
    }
    listByName(out, A.alarms, B.alarms, [](const AlarmDef& x) { return x.name; }, "Alarmes", ihm + arrow() + "Alarmes", "alarme:",
               "nouvelle", "retir\xC3\xA9" "e", "modifi\xC3\xA9" "e");
    listByName(out, A.recipes, B.recipes, [](const Recipe& x) { return x.name; }, "Recettes", ihm + arrow() + "Recettes", "recette:",
               "nouvelle", "retir\xC3\xA9" "e", "modifi\xC3\xA9" "e");
    listByName(out, A.styles, B.styles, [](const Style& x) { return x.name; }, "Styles", ihm + arrow() + "Styles", "style:",
               "nouveau", "retir\xC3\xA9", "modifi\xC3\xA9");
    listByName(out, A.scenarios, B.scenarios, [](const TestScenario& x) { return x.name; }, "Essais", ihm + arrow() + "Essais", "essai:",
               "nouveau", "retir\xC3\xA9", "modifi\xC3\xA9");
    listByName(out, A.reports, B.reports, [](const Report& x) { return x.name; }, "Rapports", ihm + arrow() + "Rapports", "rapport:",
               "nouveau", "retir\xC3\xA9", "modifi\xC3\xA9");
    listByName(out, A.equipments, B.equipments, [](const Equipment& x) { return x.name; }, "\xC3\x89quipements",
               ihm + arrow() + "\xC3\x89quipements", "equipement:", "nouveau", "retir\xC3\xA9", "modifi\xC3\xA9");
    listByName(out, A.assets.files, B.assets.files, [](const ExternalFile& x) { return x.name; }, "Fichiers externes",
               ihm + arrow() + "Fichiers externes", "externe:", "nouveau", "retir\xC3\xA9", "modifi\xC3\xA9");
    // Les ressources : leurs octets (le pointeur ne dit rien d'une version a l'autre).
    {
        std::map<std::string, const Resource*> ma, mb;
        for (const auto& r : A.assets.resources) ma[lower(r.name)] = &r;
        for (const auto& r : B.assets.resources) mb[lower(r.name)] = &r;
        std::set<std::string> keys;
        for (const auto& [k, v] : ma) keys.insert(k);
        for (const auto& [k, v] : mb) keys.insert(k);
        for (const auto& k : keys) {
            const Resource* x = ma.count(k) ? ma[k] : nullptr;
            const Resource* y = mb.count(k) ? mb[k] : nullptr;
            if (x && y) {
                Resource xa = *x, yb = *y;
                const bool sameBytes = (x->data && y->data) ? *x->data == *y->data : x->data == y->data;
                xa.data = nullptr;
                yb.data = nullptr;
                if (sameBytes && xa == yb) continue;
            }
            Element e;
            e.side = Side::Ihm;
            e.category = "Ressources";
            e.name = (y ? y : x)->name;
            e.where = ihm + arrow() + "Ressources" + arrow() + e.name;
            e.kind = "fichier";
            e.key = "ressource:" + e.name;
            e.change = !x ? Change::Added : !y ? Change::Removed : Change::Modified;
            e.detail = !x ? "nouvelle" : !y ? "retir\xC3\xA9" "e" : "remplac\xC3\xA9" "e (" + sizeText(y->bytes) + ")";
            out.push_back(std::move(e));
        }
    }
    // Les modeles de vues du projet (lot 20) : leur nom et leurs octets.
    {
        std::map<std::string, const ViewTemplateFile*> ma, mb;
        for (const auto& t : A.viewTemplates) ma[lower(t.name)] = &t;
        for (const auto& t : B.viewTemplates) mb[lower(t.name)] = &t;
        std::set<std::string> keys;
        for (const auto& [k, v] : ma) keys.insert(k);
        for (const auto& [k, v] : mb) keys.insert(k);
        for (const auto& k : keys) {
            const ViewTemplateFile* x = ma.count(k) ? ma[k] : nullptr;
            const ViewTemplateFile* y = mb.count(k) ? mb[k] : nullptr;
            if (x && y && ((x->data && y->data) ? *x->data == *y->data : x->data == y->data) && x->category == y->category
                && x->description == y->description)
                continue;
            Element e;
            e.side = Side::Ihm;
            e.category = "Mod\xC3\xA8les de vues";
            e.name = (y ? y : x)->name;
            e.where = ihm + arrow() + "Mod\xC3\xA8les de vues" + arrow() + e.name;
            e.kind = "fichier";
            e.key = "modele:" + e.name;
            e.change = !x ? Change::Added : !y ? Change::Removed : Change::Modified;
            e.detail = !x ? "nouveau" : !y ? "retir\xC3\xA9" : "modifi\xC3\xA9";
            out.push_back(std::move(e));
        }
    }
    // Le reste : un element par reglage qui change.
    const auto one = [&](bool differs, const char* category, const char* key) {
        if (!differs) return;
        Element e;
        e.side = Side::Ihm;
        e.category = category;
        e.name = category;
        e.where = ihm + arrow() + category;
        e.kind = "element";
        e.key = key;
        e.detail = "modifi\xC3\xA9s";
        out.push_back(std::move(e));
    };
    Config ca = A.config, cb = B.config;
    ca.created.clear(); ca.modified.clear();
    cb.created.clear(); cb.modified.clear();
    one(!(ca == cb), "Configuration de l'IHM", "config");
    one(!(A.alarmSettings == B.alarmSettings), "R\xC3\xA9glages des alarmes", "alarmes-reglages");
    if (!(A.security == B.security)) {
        Element e;
        e.side = Side::Ihm;
        e.category = "Utilisateurs";
        e.name = "Utilisateurs et s\xC3\xA9" "curit\xC3\xA9";
        e.where = ihm + arrow() + "Utilisateurs";
        e.kind = "element";
        e.key = "securite";
        std::set<std::string> la, lb;
        for (const auto& u : A.security.users) la.insert(u.login);
        for (const auto& u : B.security.users) lb.insert(u.login);
        std::size_t add = 0, rem = 0;
        for (const auto& l : lb) if (!la.count(l)) ++add;
        for (const auto& l : la) if (!lb.count(l)) ++rem;
        e.detail = add || rem ? ("+" + std::to_string(add) + " \xE2\x88\x92" + std::to_string(rem) + " utilisateurs") : std::string("modifi\xC3\xA9s");
        out.push_back(std::move(e));
    }
    one(!(A.history == B.history), "Historiques", "historiques");
    one(!(A.languages == B.languages), "Langues", "langues");
    one(!(A.displays == B.displays), "Unit\xC3\xA9s et formats", "unites");
    one(!(A.comm == B.comm), "Communication", "communication");
    one(!(A.station == B.station), "Poste d'exploitation", "poste");
    one(!(A.notify == B.notify), "Notifications", "notifications");
    one(!(A.web == B.web), "Acc\xC3\xA8s web", "web");
    one(!(A.simPorts == B.simPorts), "R\xC3\xA9seau simul\xC3\xA9", "reseau-simule");
}

// L'API et les donnees : un element par fichier (le manifeste, les sections,
// les unites, les DFB, les DDT, les variables globales, la configuration).
void compareFiles(const Store& s, int a, int b, const std::map<std::string, const FileEntry*>& ma,
                  const std::map<std::string, const FileEntry*>& mb, const std::vector<std::string>& paths, std::vector<Element>& out) {
    const auto text = [&](int n, const std::string& path, bool present) -> std::string {
        if (!present) return {};
        auto c = contentOf(s, n, path);
        return c ? *c : std::string{};
    };
    // Les index ne comptent que si rien d'autre ne change dans leur dossier.
    std::set<std::string> dirsWithContent;
    for (const auto& p : paths)
        if (!endsWith(p, "index.txt")) dirsWithContent.insert(p.substr(0, p.find('/') == std::string::npos ? 0 : p.find('/')));
    for (const auto& path : paths) {
        const bool inA = ma.count(path) > 0, inB = mb.count(path) > 0;
        const std::string ta = text(a, path, inA), tb = text(b, path, inB);
        Element e;
        e.side = Side::Api;
        e.change = !inA ? Change::Added : !inB ? Change::Removed : Change::Modified;
        e.key = "fichier:" + path;
        e.fileA = inA ? path : std::string{};
        e.fileB = inB ? path : std::string{};
        e.kind = binary(ta) || binary(tb) ? "fichier" : "texte";
        const auto slash = path.find('/');
        const std::string top = slash == std::string::npos ? path : path.substr(0, slash);
        const std::string rest = slash == std::string::npos ? std::string{} : path.substr(slash + 1);
        if (path == "project.xpgproj") {
            if (withoutStamps(ta) == withoutStamps(tb)) continue;
            e.category = "Projet";
            e.name = "Manifeste du projet";
            e.where = "API" + arrow() + "Projet (nom, version, \xC3\xA9tat, auteur)";
        } else if (path == "project.lock") {
            e.category = "Projet";
            e.name = "Protection du projet";
            e.where = "API" + arrow() + "Projet (mot de passe)";
            e.kind = "fichier";
        } else if (endsWith(path, "index.txt") && (top == "sections" || top == "units" || top == "dfb" || top == "ddt")) {
            if (dirsWithContent.count(top)) continue;
            e.category = top == "sections" ? "Sections" : top == "units" ? "Unit\xC3\xA9s" : top == "dfb" ? "DFB" : "DDT";
            e.name = "L'ordre et les r\xC3\xA9glages (" + e.category + ")";
            e.where = "API" + arrow() + e.category + " (index)";
        } else if (top == "sections") {
            e.category = "Sections";
            std::string name = sectionNameOf(text(inB ? b : a, "sections/index.txt", true), rest);
            e.name = name.empty() ? stem(path) : name;
            e.where = "API" + arrow() + "Sections" + arrow() + e.name;
            e.key = "section:" + e.name;
        } else if (top == "units" || top == "dfb") {
            e.category = top == "units" ? "Unit\xC3\xA9s" : "DFB";
            const auto slash2 = rest.find('/');
            const std::string pou = slash2 == std::string::npos ? rest : rest.substr(0, slash2);
            const std::string inner = slash2 == std::string::npos ? std::string{} : rest.substr(slash2 + 1);
            if (startsWith(inner, "code/")) {
                e.name = stem(inner) + " (" + pou + ")";
                e.where = "API" + arrow() + e.category + arrow() + pou + arrow() + stem(inner);
                e.key = "section:" + pou + "/" + stem(inner);
            } else {
                e.name = pou + (inner == "interface.txt" ? " (interface)" : " (" + inner + ")");
                e.where = "API" + arrow() + e.category + arrow() + pou;
            }
        } else if (top == "ddt") {
            e.category = "DDT";
            e.name = stem(path);
            e.where = "API" + arrow() + "DDT" + arrow() + e.name;
        } else if (path == "vars/globals.txt") {
            e.category = "Variables globales";
            e.name = "Variables globales";
            e.where = "API" + arrow() + "Variables globales";
            e.kind = "liste";
            const auto rows = [](const std::string& t) {
                std::map<std::string, std::string> m;
                for (const auto& line : splitLines(t)) {
                    if (line.empty() || line[0] == '#') continue;
                    const auto semi = line.find(';');
                    std::string name = line.substr(0, semi);
                    while (!name.empty() && name.back() == ' ') name.pop_back();
                    m[name] = line;
                }
                return m;
            };
            const auto ra = rows(ta), rb = rows(tb);
            std::vector<std::string> added, removed, modified;
            for (const auto& [k, v] : rb) if (!ra.count(k)) added.push_back(k); else if (ra.at(k) != v) modified.push_back(k);
            for (const auto& [k, v] : ra) if (!rb.count(k)) removed.push_back(k);
            e.added = static_cast<int>(added.size());
            e.removed = static_cast<int>(removed.size());
            e.modified = static_cast<int>(modified.size());
            std::vector<std::string> parts;
            for (std::size_t i = 0; i < added.size() && i < 4; ++i) parts.push_back("+" + added[i]);
            for (std::size_t i = 0; i < modified.size() && i < 4; ++i) parts.push_back("\xE2\x9C\x8E" + modified[i]);
            for (std::size_t i = 0; i < removed.size() && i < 4; ++i) parts.push_back("\xE2\x88\x92" + removed[i]);
            for (std::size_t i = 0; i < parts.size(); ++i) e.detail += (i ? ", " : "") + parts[i];
            if (added.size() + modified.size() + removed.size() > parts.size()) e.detail += "\xE2\x80\xA6";
            out.push_back(std::move(e));
            continue;
        } else if (top == "config") {
            e.category = "Configuration";
            e.name = path == "config/tasks.txt" ? "T\xC3\xA2" "ches" : path == "config/hardware.txt" ? "Mat\xC3\xA9riel" : stem(path);
            e.where = "API" + arrow() + "Configuration" + arrow() + e.name;
        } else if (top == "tables") {
            e.category = "Tables d'animation";
            e.name = "Tables d'animation";
            e.where = "API" + arrow() + "Tables d'animation";
        } else if (top == "donnees") {
            e.side = Side::Data;
            e.category = "Donn\xC3\xA9" "es";
            e.name = rest;
            e.where = "Donn\xC3\xA9" "es" + arrow() + rest;
        } else {
            e.category = "Autres";
            e.name = path;
            e.where = path;
        }
        if (e.kind == "texte") {
            textElement(e, ta, tb);
            if (e.change == Change::Added) e.detail = "nouveau (" + std::to_string(e.added) + " lignes)";
            else if (e.change == Change::Removed) e.detail = "retir\xC3\xA9 (" + std::to_string(e.removed) + " lignes)";
        } else {
            e.detail = e.change == Change::Added ? "nouveau" : e.change == Change::Removed ? "retir\xC3\xA9" : "modifi\xC3\xA9";
        }
        out.push_back(std::move(e));
    }
}

}   // namespace

core::Result<Project> hmiOf(const Store& s, int number) {
    if (number == 0) return load(s.folder);
    auto files = filesOf(s, number);
    if (!files) return core::fail(files.error().code, files.error().context);
    std::map<std::string, std::string> hashes;
    for (const auto& f : *files)
        if (startsWith(f.path, "ihm/")) hashes[f.path.substr(4)] = f.hash;
    const std::string folder = s.folder;
    const auto read = [&hashes, folder](const std::string& path, std::string& content) {
        const auto it = hashes.find(path);
        if (it == hashes.end()) return false;
        return readAll(objectPath(folder, it->second), content);
    };
    return parseProject(read);
}

core::Result<Comparison> compare(const Store& s, int a, int b) {
    auto fa = filesOf(s, a);
    if (!fa) return core::fail(fa.error().code, fa.error().context);
    auto fb = filesOf(s, b);
    if (!fb) return core::fail(fb.error().code, fb.error().context);
    Comparison c;
    c.a = a;
    c.b = b;
    std::map<std::string, const FileEntry*> ma, mb;
    for (const auto& f : *fa) ma[f.path] = &f;
    for (const auto& f : *fb) mb[f.path] = &f;
    std::set<std::string> all;
    for (const auto& [k, v] : ma) all.insert(k);
    for (const auto& [k, v] : mb) all.insert(k);
    bool ihmChanged = false;
    std::vector<std::string> paths;
    for (const auto& p : all) {
        const auto* x = ma.count(p) ? ma[p] : nullptr;
        const auto* y = mb.count(p) ? mb[p] : nullptr;
        if (x && y && x->hash == y->hash) continue;
        if (startsWith(p, "ihm/")) { ihmChanged = true; continue; }
        paths.push_back(p);
    }
    compareFiles(s, a, b, ma, mb, paths, c.elements);
    if (ihmChanged) {
        auto pa = hmiOf(s, a);
        auto pb = hmiOf(s, b);
        if (pa && pb) compareIhm(*pa, *pb, c.elements);
        else {
            Element e;
            e.side = Side::Ihm;
            e.category = "IHM";
            e.name = "IHM";
            e.where = "IHM";
            e.detail = "illisible : " + (pa ? pb.error().context : pa.error().context);
            e.kind = "element";
            e.key = "ihm";
            c.elements.push_back(std::move(e));
        }
    }
    // L'API d'abord, puis l'IHM, puis les donnees ; dans chaque cote, l'ordre
    // des categories tel qu'il vient (celui de l'arbre du projet).
    std::stable_sort(c.elements.begin(), c.elements.end(), [](const Element& x, const Element& y) {
        return static_cast<int>(x.side) < static_cast<int>(y.side);
    });
    return c;
}

std::vector<ObjectChange> compareViews(const View& a, const View& b) {
    std::vector<ObjectChange> out;
    std::map<Id, const Object*> ma, mb;
    for (const auto& o : a.objects) ma[o.id] = &o;
    for (const auto& o : b.objects) mb[o.id] = &o;
    for (const auto& o : b.objects) {
        const auto it = ma.find(o.id);
        if (it == ma.end()) { out.push_back({Change::Added, o.id, o.name, {}}); continue; }
        const Object& x = *it->second;
        if (x == o) continue;
        ObjectChange c{Change::Modified, o.id, o.name, {}};
        if (x.name != o.name) c.props.push_back("Nom " + x.name + " \xE2\x86\x92 " + o.name);
        std::map<std::string, const Prop*> pa, pb;
        for (const auto& p : x.props) pa[p.key] = &p;
        for (const auto& p : o.props) pb[p.key] = &p;
        std::set<std::string> keys;
        for (const auto& [k, v] : pa) keys.insert(k);
        for (const auto& [k, v] : pb) keys.insert(k);
        for (const auto& k : keys) {
            const Prop* p = pa.count(k) ? pa[k] : nullptr;
            const Prop* q = pb.count(k) ? pb[k] : nullptr;
            if (p && q && *p == *q) continue;
            c.props.push_back(propLabel(k) + " " + (p ? shortValue(*p) : std::string("(aucune)")) + " \xE2\x86\x92 "
                              + (q ? shortValue(*q) : std::string("(aucune)")));
        }
        if (!(x.actions == o.actions)) c.props.push_back("ses actions");
        if (x.layer != o.layer || x.parent != o.parent) c.props.push_back("son calque ou son groupe");
        if (x.locked != o.locked || x.hidden != o.hidden) c.props.push_back("verrouill\xC3\xA9 ou cach\xC3\xA9");
        if (x.kind != o.kind) c.props.push_back("son genre");
        out.push_back(std::move(c));
    }
    for (const auto& o : a.objects)
        if (!mb.count(o.id)) out.push_back({Change::Removed, o.id, o.name, {}});
    return out;
}

std::string defaultAuthor() {
    std::string user, host;
    for (const char* k : {"USERNAME", "USER", "LOGNAME"})
        if (const char* v = std::getenv(k); v && *v) { user = v; break; }
#if defined(_WIN32)
    if (const char* n = std::getenv("COMPUTERNAME")) host = n;
#else
    char b[256] = {};
    if (gethostname(b, sizeof b - 1) == 0) host = b;
#endif
    if (user.empty()) return host.empty() ? std::string("inconnu") : host;
    return host.empty() ? user : user + " (" + host + ")";
}

std::string sizeText(std::uint64_t bytes) {
    char buf[32];
    if (bytes < 1024) return std::to_string(bytes) + " o";
    if (bytes < 1024 * 1024) {
        std::snprintf(buf, sizeof buf, "%.0f Ko", static_cast<double>(bytes) / 1024.0);
        return buf;
    }
    std::snprintf(buf, sizeof buf, "%.1f Mo", static_cast<double>(bytes) / (1024.0 * 1024.0));
    std::string s = buf;
    for (auto& ch : s) if (ch == '.') ch = ',';
    return s;
}

} // namespace hmi::ver
