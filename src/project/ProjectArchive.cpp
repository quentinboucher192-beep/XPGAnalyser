// =============================================================================
//  project/ProjectArchive.cpp - 1.12.30 : voir ProjectArchive.hpp
// =============================================================================
#include "ProjectArchive.hpp"

#include "Security.hpp"
#include "../core/AtomicFile.hpp"
#include "../../third_party/miniz.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace project::archive {

namespace {

constexpr const char* kManifestName = "manifeste.txt";
// 1.12.31 : le cache de construction de l'IHM (hmi/HmiPipeline : buildFolderOf, build-cache.txt).
constexpr std::string_view kBuildCache = ".xpg/build/build-cache.txt";

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string extOf(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return {};
    return lower(path.substr(dot));
}

std::string leafOf(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool endsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

// ---- les dates -----------------------------------------------------------------------
// 1.12.31 : la page web n'a pas de disque (elle ne lit que read(), une archive en
// memoire) et sa libc++ wasm n'a pas std::chrono::clock_cast - les dates de fichiers
// n'y servent pas.
#ifdef XPG_WEB
std::int64_t mtimeOf(const fs::path&) { return 0; }
void setMtime(const fs::path&, std::int64_t) {}
#else
std::int64_t mtimeOf(const fs::path& p) {
    std::error_code ec;
    const auto ft = fs::last_write_time(p, ec);
    if (ec) return 0;
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(ft);
    return static_cast<std::int64_t>(std::chrono::system_clock::to_time_t(sys));
}

void setMtime(const fs::path& p, std::int64_t t) {
    if (t <= 0) return;
    std::error_code ec;
    const auto sys = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(t));
    fs::last_write_time(p, std::chrono::clock_cast<std::chrono::file_clock>(sys), ec);
}
#endif

std::tm localOf(std::int64_t t) {
    std::tm tm{};
    const std::time_t tt = static_cast<std::time_t>(t);
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    return tm;
}

struct DosTime { std::uint16_t time{0}, date{(1 << 5) | 1}; };
DosTime dosOf(std::int64_t t) {
    if (t <= 0) t = static_cast<std::int64_t>(std::time(nullptr));
    const std::tm tm = localOf(t);
    DosTime d;
    if (tm.tm_year < 80) return d;      // avant 1980 : le zip ne sait pas l'ecrire
    d.time = static_cast<std::uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
    d.date = static_cast<std::uint16_t>(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
    return d;
}

// ---- une ligne "mot cle=valeur ..." (le format des fichiers de l'IHM) ------------
std::string quoteValue(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    out += '"';
    return out;
}

struct Line {
    std::string word;
    std::vector<std::pair<std::string, std::string>> fields;
    [[nodiscard]] const std::string* get(std::string_view k) const {
        for (const auto& f : fields) if (f.first == k) return &f.second;
        return nullptr;
    }
    [[nodiscard]] std::string text(std::string_view k) const {
        const auto* v = get(k);
        return v ? *v : std::string{};
    }
    [[nodiscard]] long long number(std::string_view k) const {
        const auto* v = get(k);
        return v ? std::strtoll(v->c_str(), nullptr, 10) : 0;
    }
};

bool parseLine(std::string_view line, Line& out, std::string& error) {
    out = Line{};
    std::size_t i = 0;
    const auto space = [&] { while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i; };
    space();
    while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) out.word += line[i++];
    for (;;) {
        space();
        if (i >= line.size()) break;
        std::string k;
        while (i < line.size() && line[i] != '=' && !std::isspace(static_cast<unsigned char>(line[i]))) k += line[i++];
        if (i >= line.size() || line[i] != '=') { error = "'" + k + "' sans '='"; return false; }
        ++i;
        std::string v;
        if (i < line.size() && line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') {
                if (line[i] == '\\' && i + 1 < line.size()) {
                    const char e = line[i + 1];
                    v += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e;
                    i += 2;
                } else {
                    v += line[i++];
                }
            }
            if (i >= line.size()) { error = "guillemet non ferm\xC3\xA9"; return false; }
            ++i;
        } else {
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) v += line[i++];
        }
        out.fields.emplace_back(std::move(k), std::move(v));
    }
    return true;
}

std::string digest(std::string_view data) { return toHex(sha256(data)); }

bool sameDigest(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

// ---- le zip ---------------------------------------------------------------------
void put16(std::string& out, std::uint32_t v) {
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
}
void put32(std::string& out, std::uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, (v >> 16) & 0xFFFF);
}

// Ce qui est deja compresse : le recompresser couterait du temps pour rien.
bool alreadyPacked(std::string_view path) {
    static const char* const kPacked[] = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".ico", ".mp3", ".mp4", ".m4a", ".m4v",
                                          ".avi", ".mov", ".wmv", ".mkv", ".ogg", ".flac", ".zip", ".xlsx", ".xlsm", ".docx",
                                          ".pptx", ".pdf", ".gz", ".7z", ".rar", ".xihm", ".xapi", ".woff", ".woff2"};
    const std::string ext = extOf(path);
    for (const char* e : kPacked) if (ext == e) return true;
    return false;
}

std::string deflated(const Bytes& data) {
#if defined(XPG_HAVE_MINIZ)
    if (data.size() < 64) return {};
    std::size_t outLen = 0;
    void* p = tdefl_compress_mem_to_heap(data.data(), data.size(), &outLen, TDEFL_DEFAULT_MAX_PROBES);
    if (!p) return {};
    std::string out(static_cast<const char*>(p), outLen);
    mz_free(p);
    // Il faut y gagner (5 % au moins) : sinon, stocke tel quel.
    if (out.size() + out.size() / 20 >= data.size()) return {};
    return out;
#else
    (void)data;
    return {};
#endif
}

std::uint32_t crcOf(const Bytes& data) {
    return static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, data.data(), data.size()));
}

std::string readHead(const std::string& path, std::size_t max) {
    std::ifstream in(fs::path(path), std::ios::binary);
    if (!in) return {};
    std::string out(max, '\0');
    in.read(out.data(), static_cast<std::streamsize>(max));
    out.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, in.gcount())));
    return out;
}

// ---- base64 ---------------------------------------------------------------------
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int valueOf(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::string attributeOf(std::string_view tag, std::string_view name) {
    const std::string needle = std::string(name) + "=\"";
    const auto at = tag.find(needle);
    if (at == std::string_view::npos) return {};
    const auto from = at + needle.size();
    const auto to = tag.find('"', from);
    if (to == std::string_view::npos) return {};
    return std::string(tag.substr(from, to - from));
}

std::string cleanName(std::string_view name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            out += '_';
        else
            out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out.empty() ? std::string("Projet") : out;
}

} // namespace

// =================================================================== le genre ==
std::string_view extension(Kind k) noexcept { return k == Kind::Api ? ".xapi" : ".xihm"; }
std::string_view key(Kind k) noexcept { return k == Kind::Api ? "api" : "ihm"; }
std::string_view application(Kind k) noexcept { return k == Kind::Api ? "API Studio" : "IHM Studio"; }
std::optional<Kind> kindFromKey(std::string_view s) noexcept {
    if (s == "ihm") return Kind::Ihm;
    if (s == "api") return Kind::Api;
    return std::nullopt;
}

std::string_view originKey(Origin o) noexcept {
    switch (o) {
        case Origin::Folder:   return "";
        case Origin::External: return "externe";
        case Origin::Beside:   return "a-cote";
        case Origin::Library:  return "macro";
    }
    return "";
}

// =============================================================== le manifeste ==
std::string toText(const Manifest& m) {
    std::string out = "# " + std::string(application(m.kind)) + " - archive du projet (format " + std::to_string(m.format) + ")\n";
    out += "# Ce que l'archive contient : chaque fichier, sa taille, son empreinte SHA-256 et sa date.\n";
    out += "# Ouvrir l'archive dans " + std::string(application(m.kind)) + " relit chacun et refait les comptes.\n";
    out += "archive format=" + std::to_string(m.format) + " genre=" + std::string(key(m.kind)) + " outil=" + quoteValue(m.tool);
    if (m.dataFormat > 0) out += " format_ihm=" + std::to_string(m.dataFormat);
    out += " cree=" + quoteValue(m.created) + " projet=" + quoteValue(m.project) + " dossier=" + quoteValue(m.folder) + "\n";
    for (const auto& c : m.counts)
        out += "section nom=" + c.key + " libelle=" + quoteValue(c.label) + " nombre=" + std::to_string(c.expected) + "\n";
    for (const auto& e : m.entries) {
        out += "fichier chemin=" + quoteValue(e.path) + " octets=" + std::to_string(e.bytes) + " sha256=" + e.sha256;
        if (e.mtime > 0) out += " date=" + std::to_string(e.mtime);
        if (e.origin != Origin::Folder) out += " origine=" + std::string(originKey(e.origin));
        if (!e.link.empty()) out += " lien=" + quoteValue(e.link);
        out += "\n";
    }
    out += "fin\n";
    return out;
}

void parse(std::string_view m, Manifest& out, std::vector<std::string>& problems) {
    out = Manifest{};
    out.format = 1;                       // une archive de la 1.x ne le dit pas
    std::size_t pos = 0;
    while (pos < m.size()) {
        const std::size_t eol = m.find('\n', pos);
        std::string_view line = m.substr(pos, (eol == std::string_view::npos ? m.size() : eol) - pos);
        pos = eol == std::string_view::npos ? m.size() : eol + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line[0] == '#') continue;
        Line r;
        std::string error;
        if (!parseLine(line, r, error)) {
            problems.push_back("manifeste : " + error);
            continue;
        }
        if (r.word == "archive") {
            if (const auto* f = r.get("format")) out.format = static_cast<int>(std::strtol(f->c_str(), nullptr, 10));
            if (const auto* g = r.get("genre"))
                if (const auto k = kindFromKey(*g)) { out.kind = *k; out.kindKnown = true; }
            out.tool = r.text("outil");
            out.created = r.text("cree");
            out.project = r.text("projet");
            out.folder = r.text("dossier");
            out.dataFormat = static_cast<int>(r.number("format_ihm"));
        } else if (r.word == "section") {
            Count c;
            c.key = r.text("nom");
            c.label = r.text("libelle");
            if (c.label.empty()) c.label = c.key;
            c.expected = r.number("nombre");
            if (!c.key.empty()) out.counts.push_back(std::move(c));
        } else if (r.word == "fichier") {
            Entry e;
            e.path = r.text("chemin");
            for (auto& c : e.path) if (c == '\\') c = '/';
            e.bytes = static_cast<std::uint64_t>(std::strtoull(r.text("octets").c_str(), nullptr, 10));
            e.sha256 = r.text("sha256");
            e.mtime = r.number("date");
            const std::string origin = r.text("origine");
            e.origin = origin == "externe" ? Origin::External : origin == "a-cote" ? Origin::Beside
                     : origin == "macro"   ? Origin::Library  : Origin::Folder;
            e.link = r.text("lien");
            if (!e.path.empty()) out.entries.push_back(std::move(e));
        }
    }
}

// ============================================================ prendre le dossier ==
bool excluded(std::string_view p, const CollectOptions& opt, std::string* why) {
    const auto say = [why](const char* text) {
        if (why) *why = text;
        return true;
    };
    if (p.empty()) return say("");
    if (endsWith(p, ".tmp") || endsWith(p, "~")) return say("fichier temporaire");
    // 1.12.31 : LE CACHE DE CONSTRUCTION VIENT AVEC (.xpg/build/build-cache.txt : ce que le dernier build a fait,
    // element par element) - le projet rouvert ne se regenere pas. Ses artefacts se deduisent du projet : ils ne
    // voyagent pas, le deballage les refait d'apres le cache (hmi::pipeline::restoreArtifacts). Ni le verrou du
    // build (le poste qui construit), ni la copie de secours du cache.
    if (startsWith(p, ".xpg/build/") && p != kBuildCache) {
        if (p == ".xpg/build/build.lock") return say("le verrou du build (le poste qui construit)");
        if (endsWith(p, ".bak")) return say("la copie de secours du cache de construction");
        return say("les artefacts du build, refaits au d\xC3\xA9" "ballage d'apr\xC3\xA8s le cache");
    }
    if (p == "exports" || startsWith(p, "exports/")) return say("ce qui sort du projet (documents, exports)");
    if (p == "ihm/corbeille" || startsWith(p, "ihm/corbeille/")) return say("la corbeille de l'IHM");
    if (p == ".git" || startsWith(p, ".git/") || p == ".svn" || startsWith(p, ".svn/")) return say("le d\xC3\xA9p\xC3\xB4t de versions");
    if (!opt.versions && (p == "versions" || startsWith(p, "versions/"))) return say("les versions (non demand\xC3\xA9" "es)");
    if (!opt.history && (p == "ihm/historique" || startsWith(p, "ihm/historique/")))
        return say("l'historique de marche (non demand\xC3\xA9)");
    // Une archive ou une page d'archive posee dans le dossier : elle se contiendrait.
    if (archiveExtension(p)) return say("une archive de projet");
    return false;
}

bool readItem(const std::string& file, Item& out) {
    std::string content;
    if (!core::readFileAll(fs::path(file), content)) return false;
    out.data = std::make_shared<const Bytes>(content.begin(), content.end());
    out.mtime = mtimeOf(fs::path(file));
    return true;
}

long long versionCount(const std::string& folder) {
    std::string text;
    if (!core::readFileAll(fs::path(folder) / "versions" / "index.txt", text)) return 0;
    long long n = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto eol = text.find('\n', pos);
        const std::string_view line(text.data() + pos, (eol == std::string::npos ? text.size() : eol) - pos);
        if (line.rfind("version ", 0) == 0) ++n;
        pos = eol == std::string::npos ? text.size() : eol + 1;
    }
    return n;
}

core::Result<std::vector<Item>> collectFolder(const std::string& folder, const CollectOptions& opt, std::vector<Skipped>* skipped) {
    std::vector<Item> out;
    std::error_code ec;
    const fs::path root(folder);
    if (folder.empty() || !fs::is_directory(root, ec))
        return core::fail(core::ErrorCode::FileNotFound, "le dossier du projet est introuvable : " + folder);
    std::map<std::string, Skipped> left;
    const auto leave = [&left](const std::string& what, const std::string& why, std::uint64_t bytes) {
        auto& s = left[what];
        s.what = what;
        s.why = why;
        ++s.files;
        s.bytes += bytes;
    };
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        std::string rel;
        try {
            rel = fs::relative(it->path(), root, ec2).generic_string();
        } catch (...) {
            continue;                     // un nom que ce systeme ne sait pas ecrire
        }
        if (ec2 || rel.empty()) continue;
        if (it->is_symlink(ec2)) {
            if (it->is_directory(ec2)) it.disable_recursion_pending();
            continue;
        }
        std::string why;
        if (it->is_directory(ec2)) {
            if (excluded(rel, opt, &why)) {
                // Ce qui est laisse : compte, pour le dire.
                std::uint64_t bytes = 0;
                std::size_t files = 0;
                for (fs::recursive_directory_iterator in(it->path(), fs::directory_options::skip_permission_denied, ec2), stop;
                     !ec2 && in != stop; in.increment(ec2))
                    if (in->is_regular_file(ec2)) { ++files; bytes += static_cast<std::uint64_t>(in->file_size(ec2)); }
                auto& s = left[rel + "/"];
                s.what = rel + "/";
                s.why = why;
                s.files += files;
                s.bytes += bytes;
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file(ec2)) continue;
        if (excluded(rel, opt, &why)) {
            leave(rel, why, static_cast<std::uint64_t>(it->file_size(ec2)));
            continue;
        }
        // Une page web qui porte une archive (la page de ce projet, posee dedans).
        if (pageExtension(rel) && readHead(it->path().string(), 8192).find("name=\"xpg-archive\"") != std::string::npos) {
            leave(rel, "une page web d'archive", static_cast<std::uint64_t>(it->file_size(ec2)));
            continue;
        }
        Item item;
        item.path = rel;
        if (!readItem(it->path().string(), item))
            return core::fail(core::ErrorCode::FileUnreadable, "lecture impossible : " + it->path().string()
                              + " (ouvert ailleurs, ou acc\xC3\xA8s refus\xC3\xA9)");
        out.push_back(std::move(item));
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.path < b.path; });
    if (skipped) {
        skipped->clear();
        for (auto& [k, s] : left) skipped->push_back(std::move(s));
    }
    return out;
}

// ===================================================================== ecrire ==
core::Result<Bytes> pack(Manifest& m, const std::vector<Item>& items) {
    m.entries.clear();
    for (const auto& it : items) {
        if (!it.data) continue;
        Entry e;
        e.path = it.path;
        e.bytes = it.data->size();
        e.sha256 = digest(std::string_view(reinterpret_cast<const char*>(it.data->data()), it.data->size()));
        e.mtime = it.mtime;
        e.origin = it.origin;
        e.link = it.link;
        m.entries.push_back(std::move(e));
    }
    const std::string manifestText = toText(m);
    const auto manifestBytes = std::make_shared<const Bytes>(manifestText.begin(), manifestText.end());

    std::string out, central;
    std::size_t count = 0;
    const auto add = [&](const std::string& name, const Bytes& data, std::int64_t mtime, bool tryDeflate) -> core::Status {
        if (data.size() > 0xFFFFFFF0u || out.size() > 0xFFFFFFF0u)
            return core::fail(core::ErrorCode::FileTooLarge, "l'archive d\xC3\xA9passerait 4 Go (" + name + ")");
        const DosTime when = dosOf(mtime);
        const std::uint32_t crc = crcOf(data);
        const std::string packed = tryDeflate ? deflated(data) : std::string{};
        const bool isDeflated = !packed.empty();
        const auto size = static_cast<std::uint32_t>(data.size());
        const auto csize = static_cast<std::uint32_t>(isDeflated ? packed.size() : data.size());
        const auto offset = static_cast<std::uint32_t>(out.size());
        const std::uint32_t method = isDeflated ? 8 : 0;
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, 0x0800);            // noms en UTF-8
        put16(out, method);
        put16(out, when.time);
        put16(out, when.date);
        put32(out, crc);
        put32(out, csize);
        put32(out, size);
        put16(out, static_cast<std::uint32_t>(name.size()));
        put16(out, 0);
        out += name;
        if (isDeflated) out += packed;
        else if (!data.empty()) out.append(reinterpret_cast<const char*>(data.data()), data.size());

        put32(central, 0x02014b50);
        put16(central, 20);
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, method);
        put16(central, when.time);
        put16(central, when.date);
        put32(central, crc);
        put32(central, csize);
        put32(central, size);
        put16(central, static_cast<std::uint32_t>(name.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, offset);
        central += name;
        ++count;
        return core::ok();
    };
    if (auto st = add(kManifestName, *manifestBytes, static_cast<std::int64_t>(std::time(nullptr)), true); !st)
        return core::Err<core::Error>(st.error());
    for (const auto& it : items) {
        if (!it.data) continue;
        if (auto st = add(it.path, *it.data, it.mtime, !alreadyPacked(it.path)); !st) return core::Err<core::Error>(st.error());
    }
    if (count > 0xFFFF) return core::fail(core::ErrorCode::FileTooLarge, "plus de 65 535 fichiers : l'archive ne sait pas les compter");
    if (out.size() + central.size() > 0xFFFFFFF0u) return core::fail(core::ErrorCode::FileTooLarge, "l'archive d\xC3\xA9passerait 4 Go");
    const auto centralAt = static_cast<std::uint32_t>(out.size());
    out += central;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<std::uint32_t>(count));
    put16(out, static_cast<std::uint32_t>(count));
    put32(out, static_cast<std::uint32_t>(central.size()));
    put32(out, centralAt);
    put16(out, 0);
    return Bytes(out.begin(), out.end());
}

core::Status writeFile(const std::string& path, const Bytes& bytes) {
    std::error_code ec;
    const fs::path target(path);
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    core::AtomicWrite how;
    how.keepBackup = false;
    how.verify = false;
    return core::writeFileAtomic(target, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), how);
}

// ======================================================================= lire ==
const std::string* Contents::file(const std::string& path) const {
    const auto it = files.find(path);
    return it == files.end() ? nullptr : &it->second;
}

core::Result<Contents> read(const Bytes& zipBytes) {
    Contents c;
    c.archiveBytes = zipBytes.size();
    std::set<std::string> listed;
    {
        mz_zip_archive zip;
        std::memset(&zip, 0, sizeof zip);
        if (zipBytes.size() < 22 || !mz_zip_reader_init_mem(&zip, zipBytes.data(), zipBytes.size(), 0))
            return core::fail(core::ErrorCode::FileUnreadable,
                              "ce n'est pas une archive lisible : le fichier est abim\xC3\xA9, coup\xC3\xA9 (copie interrompue) ou d'un autre genre");
        const mz_uint n = mz_zip_reader_get_num_files(&zip);
        std::uint64_t total = 0;
        for (mz_uint i = 0; i < n; ++i) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
            std::string name = st.m_filename;
            for (auto& ch : name) if (ch == '\\') ch = '/';
            // Une bombe (des Go annonces) ne s'ouvre pas en memoire.
            total += st.m_uncomp_size;
            if (st.m_uncomp_size > 0x7FFFFFFFu || total > 0xFFFFFFFFull) {
                mz_zip_reader_end(&zip);
                return core::fail(core::ErrorCode::FileTooLarge, "l'archive annonce plus de 4 Go : elle ne s'ouvre pas");
            }
            std::size_t size = 0;
            void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
            if (!data) {
                c.damaged.insert(name);
                continue;
            }
            c.files[name].assign(static_cast<const char*>(data), size);
            mz_free(data);
        }
        mz_zip_reader_end(&zip);
    }
    const auto manifest = c.files.find(kManifestName);
    if (manifest == c.files.end()) {
        if (c.damaged.count(kManifestName))
            c.problems.push_back(std::string(kManifestName) + " : alt\xC3\xA9r\xC3\xA9 (le contr\xC3\xB4le CRC du zip \xC3\xA9" "choue)");
        else
            c.problems.push_back("manifeste.txt absent : ce n'est pas une archive de projet (elle n'a pas \xC3\xA9t\xC3\xA9 faite par Exporter)");
        return c;
    }
    c.hasManifest = true;
    parse(manifest->second, c.manifest, c.problems);
    if (c.manifest.format > kFormat) c.newer = true;
    for (auto& e : c.manifest.entries) {
        listed.insert(e.path);
        const auto it = c.files.find(e.path);
        if (c.damaged.count(e.path)) {
            e.ok = false;
            c.problems.push_back(e.path + " : alt\xC3\xA9r\xC3\xA9 (le contr\xC3\xB4le CRC du zip \xC3\xA9" "choue)");
        } else if (it == c.files.end()) {
            e.ok = false;
            c.problems.push_back(e.path + " : absent de l'archive");
        } else if (it->second.size() != e.bytes || !sameDigest(digest(it->second), e.sha256)) {
            e.ok = false;
            c.problems.push_back(e.path + " : alt\xC3\xA9r\xC3\xA9 (taille ou empreinte SHA-256 diff\xC3\xA9rente)");
        }
    }
    for (const auto& name : c.damaged)
        if (!listed.count(name) && name != kManifestName)
            c.problems.push_back(name + " : entr\xC3\xA9" "e illisible (le contr\xC3\xB4le CRC du zip \xC3\xA9" "choue)");
    return c;
}

core::Result<Contents> readFile(const std::string& path) {
    std::string raw;
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(fs::path(path), ec))
        return core::fail(core::ErrorCode::FileNotFound, "fichier introuvable : " + path);
    if (!core::readFileAll(fs::path(path), raw)) return core::fail(core::ErrorCode::FileUnreadable, "lecture impossible : " + path);
    if (pageExtension(path) || (raw.size() > 16 && raw.compare(0, 2, "PK") != 0 && raw.find("xpg-archive") != std::string::npos)) {
        Bytes zip;
        std::string why;
        if (!archiveFromPage(raw, zip, nullptr, &why)) return core::fail(core::ErrorCode::FileUnreadable, why);
        auto c = read(zip);
        if (c) c->fromPage = true;
        return c;
    }
    return read(Bytes(raw.begin(), raw.end()));
}

// =================================================================== deballer ==
bool safePath(std::string_view p) noexcept {
    if (p.empty() || p.front() == '/' || p.front() == '\\') return false;
    if (p.size() >= 2 && p[1] == ':') return false;              // C:...
    if (p.find('\\') != std::string_view::npos || p.find('\0') != std::string_view::npos) return false;
    std::size_t from = 0;
    while (from <= p.size()) {
        const auto slash = p.find('/', from);
        const auto part = p.substr(from, slash == std::string_view::npos ? std::string_view::npos : slash - from);
        if (part.empty() || part == "." || part == "..") return false;
        if (slash == std::string_view::npos) break;
        from = slash + 1;
    }
    return true;
}

std::string freeFolder(const std::string& parent, std::string_view name) {
    const std::string base = cleanName(name);
    std::error_code ec;
    for (int n = 1; n < 1000; ++n) {
        const std::string leaf = n == 1 ? base : base + " (" + std::to_string(n) + ")";
        const fs::path p = fs::path(parent) / leaf;
        if (!fs::exists(p, ec)) return p.string();
    }
    return (fs::path(parent) / (base + " (copie)")).string();
}

core::Status unpack(const Contents& c, const std::string& folder, const UnpackOptions& opt, Unpacked* out) {
    Unpacked local;
    auto& u = out ? *out : local;
    u = Unpacked{};
    if (!c.hasManifest)
        return core::fail(core::ErrorCode::FileUnreadable, "ce n'est pas une archive de projet : manifeste.txt manque");
    if (c.newer)
        return core::fail(core::ErrorCode::NotImplemented,
                          "cette archive vient d'une version plus r\xC3\xA9" "cente (format " + std::to_string(c.manifest.format)
                              + ", " + (c.manifest.tool.empty() ? std::string("outil inconnu") : c.manifest.tool)
                              + ") : mets l'application \xC3\xA0 jour pour l'ouvrir");
    if (!c.intact() && !opt.skipDamaged) {
        std::string list;
        std::size_t shown = 0;
        for (const auto& p : c.problems) {
            if (shown++ == 4) { list += " ; ..."; break; }
            list += (list.empty() ? "" : " ; ") + p;
        }
        return core::fail(core::ErrorCode::FileUnreadable, "l'archive est ab\xC3\xAEm\xC3\xA9" "e (" + list + ") : rien n'a \xC3\xA9t\xC3\xA9 d\xC3\xA9" "ball\xC3\xA9");
    }
    std::error_code ec;
    fs::path root(folder);
    if (!root.has_filename()) root = root.parent_path();          // "C:\\Projets\\Ligne\\"
    if (folder.empty()) return core::fail(core::ErrorCode::InvalidArgument, "aucun dossier choisi");
    if (fs::exists(root, ec) && !fs::is_empty(root, ec))
        return core::fail(core::ErrorCode::InvalidArgument, "le dossier " + folder + " n'est pas vide : choisis-en un neuf");
    fs::create_directories(root, ec);
    if (ec) return core::fail(core::ErrorCode::FileUnreadable, "dossier impossible \xC3\xA0 cr\xC3\xA9" "er : " + folder + " (" + ec.message() + ")");

    const auto write = [](const fs::path& target, const std::string& content, std::int64_t mtime) -> core::Status {
        std::error_code e2;
        if (target.has_parent_path()) fs::create_directories(target.parent_path(), e2);
        std::ofstream f(target, std::ios::binary | std::ios::trunc);
        if (!f) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + target.string());
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        f.close();
        if (!f) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + target.string() + " (disque plein ?)");
        setMtime(target, mtime);
        return core::ok();
    };
    std::set<std::string> usedExternal;
    for (const auto& e : c.manifest.entries) {
        if (!e.ok) {
            u.notes.push_back(e.path + " : ab\xC3\xAEm\xC3\xA9, laiss\xC3\xA9 de c\xC3\xB4t\xC3\xA9");
            continue;
        }
        const std::string* content = c.file(e.path);
        if (!content) continue;
        if (!safePath(e.path)) {
            u.notes.push_back(e.path + " : chemin refus\xC3\xA9 (il sortirait du dossier)");
            continue;
        }
        switch (e.origin) {
            case Origin::Folder: {
                if (auto st = write(root / fs::path(e.path), *content, e.mtime); !st) return st;
                u.written.push_back(e.path);
                break;
            }
            case Origin::External: {
                // Dans le projet : externes/<nom> (un nom libre).
                std::string leaf = leafOf(e.link.empty() ? e.path : e.link);
                if (leaf.empty() || !safePath(leaf)) leaf = leafOf(e.path);
                std::string rel = "externes/" + leaf;
                for (int n = 2; usedExternal.count(rel) || fs::exists(root / fs::path(rel), ec); ++n) {
                    const auto dot = leaf.find_last_of('.');
                    rel = "externes/" + (dot == std::string::npos ? leaf + "_" + std::to_string(n)
                                                                    : leaf.substr(0, dot) + "_" + std::to_string(n) + leaf.substr(dot));
                }
                usedExternal.insert(rel);
                if (auto st = write(root / fs::path(rel), *content, e.mtime); !st) return st;
                u.written.push_back(rel);
                u.externals.push_back(e);
                u.externalPaths.push_back(rel);
                break;
            }
            case Origin::Beside: {
                // A cote du dossier, a son nom : <dossier>.xpglayout (le nom sans ce qui suit
                // son dernier point, comme LayoutFile::pathFor).
                const std::string ext = extOf(e.path);
                std::string leaf = root.filename().string();
                if (const auto dot = leaf.find_last_of('.'); dot != std::string::npos && dot > 0) leaf.resize(dot);
                const fs::path target = root.parent_path() / (leaf + ext);
                if (fs::exists(target, ec)) {
                    u.notes.push_back(target.filename().string() + " existe d\xC3\xA9j\xC3\xA0 \xC3\xA0 c\xC3\xB4t\xC3\xA9 du dossier : gard\xC3\xA9 tel quel");
                    break;
                }
                if (auto st = write(target, *content, e.mtime); !st) return st;
                u.beside = target.string();
                break;
            }
            case Origin::Library: {
                const std::string sub = startsWith(e.path, "_macros/") ? e.path.substr(8) : leafOf(e.path);
                if (opt.libraryRoot.empty() || !safePath(sub)) {
                    u.notes.push_back(e.path + " : pas de biblioth\xC3\xA8que o\xC3\xB9 la poser");
                    break;
                }
                const fs::path target = fs::path(opt.libraryRoot) / "Macros" / fs::path(sub);
                std::string have;
                if (fs::exists(target, ec)) {
                    u.macrosKept.push_back(sub);
                    if (core::readFileAll(target, have) && have != *content)
                        u.notes.push_back("macro " + sub + " : celle de ta biblioth\xC3\xA8que est diff\xC3\xA9rente, elle est gard\xC3\xA9" "e");
                    break;
                }
                if (auto st = write(target, *content, e.mtime); !st) {
                    u.notes.push_back("macro " + sub + " : " + st.error().context);
                    break;
                }
                u.macrosAdded.push_back(sub);
                break;
            }
        }
    }
    // 1.12.30 : LA PAGE WEB SUIT LE DOSSIER. Un chemin absolu ("web-fichier = D:\Partage\Ligne.html")
    // vient du poste qui a ecrit l'archive : il part, la page s'ecrira a cote du nouveau dossier.
    for (const auto& e : c.manifest.entries) {
        if (e.path != "project.xpgproj" || e.origin != Origin::Folder) continue;
        const fs::path file = root / "project.xpgproj";
        std::string text;
        if (!core::readFileAll(file, text) || text.find("web-fichier") == std::string::npos) break;
        std::string kept, dropped;
        std::size_t at = 0;
        while (at < text.size()) {
            const auto end = text.find('\n', at);
            const std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end + 1 - at);
            at = end == std::string::npos ? text.size() : end + 1;
            if (startsWith(line, "web-fichier")) {
                std::string value = line.substr(std::min(line.size(), line.find('=') == std::string::npos ? line.size() : line.find('=') + 1));
                while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) value.pop_back();
                while (!value.empty() && value.front() == ' ') value.erase(value.begin());
                const bool absolute = !value.empty() && (value[0] == '/' || value[0] == '\\' || (value.size() > 2 && value[1] == ':'));
                if (absolute) {
                    dropped = value;
                    continue;
                }
            }
            kept += line;
        }
        if (!dropped.empty()) {
            if (auto st = write(file, kept, e.mtime); !st) return st;
            u.notes.push_back("la page web du projet s'\xC3\xA9" "crira \xC3\xA0 c\xC3\xB4t\xC3\xA9 de son nouveau dossier (plus dans " + dropped + ")");
        }
        break;
    }
    return core::ok();
}

// ======================================================= une page qui la porte ==
std::string base64(const std::uint8_t* d, std::size_t n) {
    std::string out;
    out.reserve((n + 2) / 3 * 4 + n / 90 + 4);
    std::size_t col = 0;
    for (std::size_t i = 0; i < n; i += 3) {
        const std::uint32_t a = d[i];
        const std::uint32_t b = i + 1 < n ? d[i + 1] : 0;
        const std::uint32_t c = i + 2 < n ? d[i + 2] : 0;
        const std::uint32_t v = (a << 16) | (b << 8) | c;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += i + 1 < n ? kAlphabet[(v >> 6) & 63] : '=';
        out += i + 2 < n ? kAlphabet[v & 63] : '=';
        col += 4;
        if (col >= 120) { out += '\n'; col = 0; }
    }
    return out;
}

bool fromBase64(std::string_view text, Bytes& out) {
    out.clear();
    out.reserve(text.size() / 4 * 3);
    std::uint32_t acc = 0;
    int bits = 0, pad = 0;
    for (const char ch : text) {
        if (ch == '=') { ++pad; continue; }
        if (std::isspace(static_cast<unsigned char>(ch))) continue;
        if (pad) return false;                       // des donnees apres le remplissage
        const int v = valueOf(ch);
        if (v < 0) return false;
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return pad <= 2;
}

std::string pageHeadMarker(Kind k) {
    return "<meta name=\"xpg-archive\" content=\"" + std::string(key(k)) + "\">";
}

std::string pageArchiveBlock(const Bytes& zip, Kind k, std::string_view fileName) {
    std::string name(fileName);
    for (auto& c : name) if (c == '"' || c == '<' || c == '>' || c == '&') c = '_';
    std::string out = "<script type=\"application/x-xpg-archive\" id=\"xpg-archive\" data-genre=\"" + std::string(key(k))
                    + "\" data-nom=\"" + name + "\" data-octets=\"" + std::to_string(zip.size()) + "\" data-sha256=\""
                    + digest(std::string_view(reinterpret_cast<const char*>(zip.data()), zip.size())) + "\">\n";
    out += base64(zip.data(), zip.size());
    out += "\n</script>\n";
    return out;
}

bool archiveFromPage(std::string_view html, Bytes& zip, std::optional<Kind>* kind, std::string* why) {
    const auto fail = [why](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    const auto at = html.find("id=\"xpg-archive\"");
    if (at == std::string_view::npos)
        return fail("cette page n'a pas \xC3\xA9t\xC3\xA9 \xC3\xA9" "crite par IHM Studio ni par API Studio : elle ne porte pas d'archive de projet");
    const auto open = html.rfind("<script", at);
    const auto close = html.find('>', at);
    if (open == std::string_view::npos || close == std::string_view::npos) return fail("le bloc de l'archive de la page est abim\xC3\xA9");
    const std::string_view tag = html.substr(open, close - open);
    const auto end = html.find("</script>", close);
    if (end == std::string_view::npos) return fail("l'archive de la page est coup\xC3\xA9" "e (la page est incompl\xC3\xA8te)");
    if (!fromBase64(html.substr(close + 1, end - close - 1), zip)) return fail("l'archive de la page est abim\xC3\xA9" "e (base64 illisible)");
    const std::string bytes = attributeOf(tag, "data-octets");
    const std::string sha = attributeOf(tag, "data-sha256");
    if (!bytes.empty() && std::strtoull(bytes.c_str(), nullptr, 10) != zip.size())
        return fail("l'archive de la page est abim\xC3\xA9" "e (sa taille ne correspond pas)");
    if (!sha.empty() && !sameDigest(sha, digest(std::string_view(reinterpret_cast<const char*>(zip.data()), zip.size()))))
        return fail("l'archive de la page est abim\xC3\xA9" "e (son empreinte SHA-256 ne correspond pas)");
    if (kind) *kind = kindFromKey(attributeOf(tag, "data-genre"));
    return true;
}

// ==================================================================== reconnaitre ==
bool archiveExtension(std::string_view path) noexcept {
    const std::string ext = extOf(path);
    return ext == ".xihm" || ext == ".xapi";
}

bool pageExtension(std::string_view path) noexcept {
    const std::string ext = extOf(path);
    return ext == ".html" || ext == ".htm";
}

Sniff sniff(const std::string& path) {
    Sniff s;
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(fs::path(path), ec)) return s;
    const std::string ext = extOf(path);
    if (ext == ".xihm" || ext == ".xapi") {
        s.archive = true;
        s.kind = ext == ".xapi" ? Kind::Api : Kind::Ihm;
        return s;
    }
    if (pageExtension(path)) {
        s.page = true;
        const std::string head = readHead(path, 65536);
        const auto at = head.find("name=\"xpg-archive\"");
        if (at == std::string::npos) {
            s.why = "cette page n'a pas \xC3\xA9t\xC3\xA9 \xC3\xA9" "crite par IHM Studio ni par API Studio : elle ne porte pas d'archive de projet";
            return s;
        }
        s.archive = true;
        const auto tagEnd = head.find('>', at);
        const auto tagStart = head.rfind('<', at);
        if (tagEnd != std::string::npos && tagStart != std::string::npos)
            s.kind = kindFromKey(attributeOf(std::string_view(head).substr(tagStart, tagEnd - tagStart), "content"));
        return s;
    }
    if (ext == ".zip") {
        // Une archive de l'IHM de la 1.x (ou un .xihm renomme) : son manifeste le dit.
        mz_zip_archive zip;
        std::memset(&zip, 0, sizeof zip);
        if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) return s;
        const int at = mz_zip_reader_locate_file(&zip, kManifestName, nullptr, 0);
        if (at >= 0) {
            std::size_t size = 0;
            if (void* data = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(at), &size, 0)) {
                Manifest m;
                std::vector<std::string> problems;
                parse(std::string_view(static_cast<const char*>(data), size), m, problems);
                mz_free(data);
                if (!m.entries.empty() || m.kindKnown) {
                    s.archive = true;
                    s.kind = m.kind;
                }
            }
        }
        mz_zip_reader_end(&zip);
    }
    return s;
}

// ===================================================================== le texte ==
std::string sizeText(std::uint64_t b) {
    char buf[64];
    if (b < 1024) {
        std::snprintf(buf, sizeof buf, "%llu octets", static_cast<unsigned long long>(b));
    } else if (b < 1024ull * 1024) {
        std::snprintf(buf, sizeof buf, "%llu Ko", static_cast<unsigned long long>((b + 512) / 1024));
    } else if (b < 1024ull * 1024 * 1024) {
        std::snprintf(buf, sizeof buf, "%.1f Mo", static_cast<double>(b) / (1024.0 * 1024.0));
    } else {
        std::snprintf(buf, sizeof buf, "%.2f Go", static_cast<double>(b) / (1024.0 * 1024.0 * 1024.0));
    }
    std::string out = buf;
    for (auto& c : out) if (c == '.') c = ',';
    return out;
}

std::string nowText() {
    const std::tm tm = localOf(static_cast<std::int64_t>(std::time(nullptr)));
    char b[64];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    return b;
}

} // namespace project::archive
