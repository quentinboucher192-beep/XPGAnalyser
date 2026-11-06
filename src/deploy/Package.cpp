// =============================================================================
//  deploy/Package.cpp - 1.28.0 : le paquet .xpgdeploy (voir Package.hpp)
// -----------------------------------------------------------------------------
//  Le zip : miniz, en flux des deux cotes (un fichier source lu par blocs, le
//  paquet ecrit puis relu par blocs : rien n'est charge en entier, rien n'est
//  extrait). Le SHA-256 en flux : l'etat incremental de libsodium (Crypto.hpp
//  n'expose que l'empreinte d'un bloc ou d'un fichier entier).
// =============================================================================
#include "Package.hpp"

#include "DevKey.hpp"

#include "../core/Json.hpp"
#include "../core/Version.hpp"

#include "../../third_party/miniz.h"

#include <sodium.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace deploy {

namespace fs = std::filesystem;

namespace {

// ---- petits outils ---------------------------------------------------------------
fs::path P(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

std::string U(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}
std::string UG(const fs::path& p) {
    const auto u8 = p.generic_u8string();
    return std::string(u8.begin(), u8.end());
}

bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(0, p.size()) == p; }
bool endsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(s.size() - p.size()) == p; }

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// Un chemin montre dans un message : sans caractere de controle, 120 octets au plus.
std::string shown(std::string_view p) {
    std::string out;
    for (char c : p.substr(0, 120)) out += (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) ? '?' : c;
    if (p.size() > 120) out += "\xE2\x80\xA6";
    return out;
}

std::string cite(std::string_view p) { return "\xC2\xAB " + shown(p) + " \xC2\xBB"; }

// UTF-8 valide (ni surlongue, ni moitie de paire UTF-16, ni au-dela de U+10FFFF) ; `units` : sa longueur en UTF-16.
bool utf8Units(std::string_view s, std::size_t& units) {
    units = 0;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0;
        std::size_t len = 0;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; len = 4; }
        else return false;
        if (i + len > s.size()) return false;
        for (std::size_t k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)) return false;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        units += cp >= 0x10000 ? 2 : 1;
        i += len;
    }
    return true;
}

// Un nom reserve de Windows : la partie avant le premier point, sans les espaces de fin, quelle que soit la casse.
bool reservedName(std::string_view segment) {
    std::string base(segment.substr(0, segment.find('.')));
    while (!base.empty() && base.back() == ' ') base.pop_back();
    for (char& c : base)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" || base == "CONIN$" || base == "CONOUT$") return true;
    if (base.size() >= 4 && (startsWith(base, "COM") || startsWith(base, "LPT"))) {
        const std::string tail = base.substr(3);
        if (tail.size() == 1 && tail[0] >= '0' && tail[0] <= '9') return true;
        if (tail == "\xC2\xB9" || tail == "\xC2\xB2" || tail == "\xC2\xB3") return true;   // les exposants 1, 2, 3
    }
    return false;
}

// Ce qui ne va pas dans un chemin du paquet (vide : il est sur).
std::string pathProblem(std::string_view p, std::size_t maxLength) {
    if (p.empty()) return "il est vide";
    std::size_t units = 0;
    if (!utf8Units(p, units)) return "ce n'est pas de l'UTF-8 valide";
    if (p.front() == '/' || p.front() == '\\') return "il commence par / ou \\ (un chemin absolu)";
    if (p.find('\\') != std::string_view::npos) return "il contient \\ (un s\xC3\xA9parateur de Windows : il pourrait sortir du dossier)";
    if (p.find(':') != std::string_view::npos) return "il contient : (une lettre de lecteur ou un flux NTFS)";
    for (char c : p) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) return "il contient un caract\xC3\xA8re de contr\xC3\xB4le";
        if (c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            return std::string("il contient ") + c + " (un caract\xC3\xA8re que Windows refuse dans un nom)";
    }
    if (units > maxLength)
        return "il d\xC3\xA9passe " + std::to_string(maxLength) + " caract\xC3\xA8res (" + std::to_string(units) + ")";
    std::size_t pos = 0;
    while (true) {
        const auto slash = p.find('/', pos);
        const std::string_view seg = p.substr(pos, slash == std::string_view::npos ? std::string_view::npos : slash - pos);
        if (seg.empty()) return "il contient un segment vide (// ou / \xC3\xA0 la fin)";
        if (seg == "." || seg == "..") return "il contient . ou .. (il sortirait du dossier)";
        if (seg.back() == '.' || seg.back() == ' ')
            return "le nom " + cite(seg) + " finit par un point ou une espace (Windows le changerait)";
        if (reservedName(seg)) return cite(seg) + " est un nom r\xC3\xA9serv\xC3\xA9 de Windows (CON, PRN, AUX, NUL, COM1, LPT1...)";
        if (slash == std::string_view::npos) break;
        pos = slash + 1;
    }
    if (!startsWith(p, kPackageAppPrefix)) return "il n'est pas sous app/";
    return {};
}

std::string sizeLimitText(std::uint64_t bytes) { return packageSizeText(bytes); }

// ---- le SHA-256 en flux -------------------------------------------------------------
struct Hasher {
    crypto_hash_sha256_state st{};
    Hasher() { crypto_hash_sha256_init(&st); }
    void add(const void* data, std::size_t n) {
        if (n) crypto_hash_sha256_update(&st, static_cast<const unsigned char*>(data), static_cast<unsigned long long>(n));
    }
    std::string hex() {
        crypto::Digest d{};
        crypto_hash_sha256_final(&st, d.data());
        return crypto::hex(d);
    }
};

// ---- le manifeste du projet (project.xpgproj) et ses versions -----------------------
std::string trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return std::string(s);
}

template <class F>
void eachLine(std::string_view text, F&& f) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        auto eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        f(line);
        pos = eol + 1;
    }
}

// Les champs d'une ligne "mot cle=valeur cle=\"valeur avec espaces\"" (le format des fichiers de l'IHM).
std::map<std::string, std::string> recordFields(std::string_view line) {
    std::map<std::string, std::string> out;
    std::size_t i = line.find(' ');
    while (i != std::string_view::npos && i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        const auto eq = line.find('=', i);
        if (eq == std::string_view::npos) break;
        const std::string key(line.substr(i, eq - i));
        i = eq + 1;
        std::string value;
        if (i < line.size() && line[i] == '"') {
            for (++i; i < line.size() && line[i] != '"'; ++i) {
                if (line[i] == '\\' && i + 1 < line.size()) {
                    const char e = line[++i];
                    value += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e;
                } else {
                    value += line[i];
                }
            }
            ++i;
        } else {
            const auto end = line.find(' ', i);
            value = std::string(line.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i));
            i = end;
        }
        out[key] = std::move(value);
    }
    return out;
}

// La derniere version validee ou livree : "V3" (vide : aucune).
std::string lastClosedVersion(const std::string& folder) {
    crypto::Bytes raw;
    if (!crypto::readFile(U(P(folder) / "versions" / "index.txt"), raw, nullptr, 16u * 1024u * 1024u)) return {};
    long best = 0;
    eachLine(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()), [&](std::string_view line) {
        if (!startsWith(line, "version ")) return;
        const auto f = recordFields(line);
        const auto n = f.find("n");
        const auto st = f.find("etat");
        if (n == f.end() || st == f.end() || (st->second != "validee" && st->second != "livree")) return;
        const long v = std::strtol(n->second.c_str(), nullptr, 10);
        if (v > best) best = v;
    });
    return best > 0 ? "V" + std::to_string(best) : std::string{};
}

// ---- ce qui part, ce qui reste ---------------------------------------------------------
// Les dossiers laisses sous ihm/ et donnees/ (chemins relatifs au projet).
const char* skippedSubfolder(const std::string& rel) {
    if (rel == "ihm/corbeille") return "la corbeille de l'IHM";
    if (rel == "ihm/supprimes") return "la corbeille du projet";
    if (rel == "ihm/historique") return "l'historique de marche de ce PC (alarmes, mesures, audit, valeurs retenues)";
    if (rel == "ihm/historique-edition") return "l'historique des modifications (la conception)";
    if (rel == "ihm/reseau") return "les machines d\xC3\xA9" "couvertes depuis ce PC";
    if (rel == "ihm/ressources/enregistrements") return "les enregistrements et dossiers d'incident";
    if (rel == "donnees/extraits") return "les extraits (ce qu'Extraire a produit)";
    const auto leaf = rel.substr(rel.rfind('/') + 1);
    if (leaf == ".git" || leaf == ".svn") return "un d\xC3\xA9p\xC3\xB4t de versions";
    return nullptr;
}

// Un fichier laisse partout : temporaire, copie de secours, paquet ou archive, fichier du systeme.
const char* skippedFile(const std::string& name) {
    const std::string low = lowerAscii(name);
    if (endsWith(low, ".tmp") || endsWith(low, "~") || startsWith(low, "~$") || endsWith(low, ".part") || endsWith(low, ".bak"))
        return "un fichier temporaire ou une copie de secours";
    if (endsWith(low, ".xpgdeploy") || endsWith(low, ".xihm") || endsWith(low, ".xapi")) return "un paquet ou une archive (il se contiendrait)";
    if (low == "thumbs.db" || low == "desktop.ini" || low == ".ds_store") return "un fichier du syst\xC3\xA8me";
    return nullptr;
}

// Une entree de la racine du projet qui ne part pas : pourquoi.
std::string skippedTop(const std::string& name, bool dir) {
    if (dir) {
        if (name == "versions") return "les versions du projet (elles restent sur ce PC)";
        if (name == "exports") return "ce qui sort du projet (documents, exports)";
        if (name == ".xpg") return "le cache de construction et la r\xC3\xA9manence de la simulation (refaits sur le poste)";
        if (name == "config" || name == "vars" || name == "ddt" || name == "dfb" || name == "units" || name == "sections" || name == "tables"
            || name == "src")
            return "le programme de l'automate (API Studio) : le poste IHM ne le lit pas";
        if (name == ".git" || name == ".svn") return "un d\xC3\xA9p\xC3\xB4t de versions";
        return "hors du projet IHM : le poste ne le lit pas";
    }
    if (name == "project.lock") return "le verrou (un projet FINISH n'en a pas)";
    if (name == "lancer-poste.cmd" || name == "lancer-poste.sh") return "le lanceur de ce PC (le paquet porte le sien)";
    if (const char* why = skippedFile(name)) return why;
    return "hors du projet IHM : le poste ne le lit pas";
}

// Les fichiers sous `dir` (sans suivre les liens) : combien, combien d'octets.
void countTree(const fs::path& dir, std::size_t& files, std::uint64_t& bytes) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_symlink(e2)) {
            if (it->is_directory(e2)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(e2)) {
            ++files;
            const auto raw = it->file_size(e2);
            if (!e2) bytes += static_cast<std::uint64_t>(raw);
        }
    }
}

// Les medias deja compresses partent tels quels (les recompresser ne gagne rien et coute du temps).
bool alreadyPacked(std::string_view path) {
    const std::string low = lowerAscii(path);
    static const char* const kExt[] = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".mp3", ".mp4", ".webm", ".ogg", ".m4a", ".avi", ".mkv",
                                       ".mov", ".zip", ".7z", ".gz", ".xlsx", ".xlsm", ".docx", ".pptx", ".pdf", ".xrec"};
    for (const char* e : kExt)
        if (endsWith(low, e)) return true;
    return false;
}

std::string utcNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// ---- les flux de miniz -----------------------------------------------------------------
// Le paquet en ecriture : un fichier, hache au passage tant que miniz ecrit d'un trait.
struct Sink {
    std::ofstream out;
    std::uint64_t pos{0}, size{0};
    bool          sequential{true};
    bool          failed{false};
    Hasher        hash;
};

size_t sinkWrite(void* opaque, mz_uint64 ofs, const void* buf, size_t n) {
    auto* s = static_cast<Sink*>(opaque);
    if (ofs != s->pos) {
        s->out.seekp(static_cast<std::streamoff>(ofs));
        s->sequential = false;
    }
    s->out.write(static_cast<const char*>(buf), static_cast<std::streamsize>(n));
    if (!s->out) {
        s->failed = true;
        return 0;
    }
    if (s->sequential) s->hash.add(buf, n);
    s->pos = ofs + n;
    s->size = std::max(s->size, s->pos);
    return n;
}

// Un fichier du projet en lecture : hache, compte, la progression a chaque bloc.
struct Source {
    std::ifstream            in;
    std::uint64_t            expected{0}, done{0};
    Hasher                   hash;
    bool                     failed{false}, cancelled{false};
    const PackageProgressFn* progress{nullptr};
    PackageProgress*         state{nullptr};
};

constexpr size_t kAbort = static_cast<size_t>(-1);   // plus grand qu'un bloc : miniz s'arrete en erreur

size_t sourceRead(void* opaque, mz_uint64 ofs, void* buf, size_t n) {
    auto* s = static_cast<Source*>(opaque);
    if (s->cancelled || s->failed) return kAbort;
    if (ofs != s->done) {
        s->failed = true;
        return kAbort;
    }
    s->in.read(static_cast<char*>(buf), static_cast<std::streamsize>(n));
    const auto got = static_cast<std::size_t>(s->in.gcount());
    if (s->in.bad()) {
        s->failed = true;
        return kAbort;
    }
    s->hash.add(buf, got);
    s->done += got;
    if (got && s->progress && *s->progress && s->state) {
        s->state->bytesDone += got;
        if (!(*s->progress)(*s->state)) s->cancelled = true;
    }
    return got;
}

// Le paquet en lecture : un fichier, par position.
struct ZipFile {
    std::ifstream in;
    std::uint64_t size{0};
};

size_t zipRead(void* opaque, mz_uint64 ofs, void* buf, size_t n) {
    auto* z = static_cast<ZipFile*>(opaque);
    if (ofs >= z->size) return 0;
    z->in.clear();
    z->in.seekg(static_cast<std::streamoff>(ofs));
    if (!z->in) return 0;
    z->in.read(static_cast<char*>(buf), static_cast<std::streamsize>(n));
    return static_cast<size_t>(z->in.gcount());
}

// L'erreur de miniz, dite en francais (les cas courants ; sinon son texte).
std::string zipError(mz_zip_archive& zip) {
    switch (mz_zip_get_last_error(&zip)) {
        case MZ_ZIP_DECOMPRESSION_FAILED:
        case MZ_ZIP_CRC_CHECK_FAILED:
        case MZ_ZIP_UNEXPECTED_DECOMPRESSED_SIZE: return "donn\xC3\xA9" "es ab\xC3\xAEm\xC3\xA9" "es, le CRC ne correspond pas";
        case MZ_ZIP_NOT_AN_ARCHIVE:
        case MZ_ZIP_FAILED_FINDING_CENTRAL_DIR: return "pas de r\xC3\xA9pertoire de zip";
        case MZ_ZIP_INVALID_HEADER_OR_CORRUPTED: return "en-t\xC3\xAAte ab\xC3\xAEm\xC3\xA9";
        case MZ_ZIP_UNSUPPORTED_METHOD:
        case MZ_ZIP_UNSUPPORTED_ENCRYPTION:
        case MZ_ZIP_UNSUPPORTED_FEATURE:
        case MZ_ZIP_UNSUPPORTED_MULTIDISK:
        case MZ_ZIP_UNSUPPORTED_CDIR_SIZE: return "une forme de zip non prise en charge";
        case MZ_ZIP_FILE_READ_FAILED:
        case MZ_ZIP_FILE_SEEK_FAILED: return "lecture impossible";
        case MZ_ZIP_FILE_WRITE_FAILED: return "\xC3\xA9" "criture impossible (disque plein ?)";
        case MZ_ZIP_ALLOC_FAILED: return "m\xC3\xA9moire insuffisante";
        case MZ_ZIP_TOO_MANY_FILES:
        case MZ_ZIP_FILE_TOO_LARGE:
        case MZ_ZIP_ARCHIVE_TOO_LARGE: return "trop gros pour un zip";
        default: break;
    }
    const char* e = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
    return std::string("zip : ") + (e ? e : "erreur inconnue");
}

// ---- le JSON du manifeste ---------------------------------------------------------------
using core::json::Value;

bool duplicateKey(const Value& v, std::string& key) {
    if (v.isObject()) {
        std::set<std::string> seen;
        for (const auto& [k, x] : v.members()) {
            if (!seen.insert(k).second) {
                key = k;
                return true;
            }
            if (duplicateKey(x, key)) return true;
        }
    } else if (v.isArray()) {
        for (const auto& x : v.items())
            if (duplicateKey(x, key)) return true;
    }
    return false;
}

bool readString(const Value& o, std::string_view key, std::string& out, std::string& err, std::string_view where = {}) {
    const Value* v = o.find(key);
    const std::string name = std::string(where) + std::string(key);
    if (!v) {
        err = "le champ " + cite(name) + " manque";
        return false;
    }
    if (!v->isString()) {
        err = "le champ " + cite(name) + " n'est pas une cha\xC3\xAEne";
        return false;
    }
    out = v->asString();
    return true;
}

bool readCount(const Value& o, std::string_view key, std::uint64_t& out, std::string& err, std::string_view where = {}) {
    const Value* v = o.find(key);
    const std::string name = std::string(where) + std::string(key);
    if (!v) {
        err = "le champ " + cite(name) + " manque";
        return false;
    }
    const double d = v->asNumber(-1);
    if (!v->isNumber() || d < 0 || d != std::floor(d) || d > 9007199254740992.0) {
        err = "le champ " + cite(name) + " n'est pas un entier positif";
        return false;
    }
    out = static_cast<std::uint64_t>(d);
    return true;
}

bool isLowerHex(std::string_view s, std::size_t len) {
    if (s.size() != len) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

} // namespace

// =============================================================================== chemins ==
bool safePackagePath(std::string_view path, std::string* why, std::size_t maxLength) {
    const std::string problem = pathProblem(path, maxLength);
    if (problem.empty()) return true;
    if (why) *why = "le chemin " + cite(path) + " est refus\xC3\xA9 : " + problem;
    return false;
}

std::string packageSizeText(std::uint64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + (bytes > 1 ? " octets" : " octet");
    static const char* const kUnits[] = {"Kio", "Mio", "Gio", "Tio"};
    double v = static_cast<double>(bytes) / 1024.0;
    int u = 0;
    while (v >= 1024.0 && u < 3) {
        v /= 1024.0;
        ++u;
    }
    char buf[32];
    if (v == std::floor(v)) std::snprintf(buf, sizeof buf, "%.0f %s", v, kUnits[u]);
    else std::snprintf(buf, sizeof buf, "%.1f %s", v, kUnits[u]);
    std::string out = buf;
    std::replace(out.begin(), out.end(), '.', ',');
    return out;
}

// ============================================================================= manifeste ==
std::string writeManifest(const PackageManifest& m) {
    Value root = Value::object();
    root.set("format", m.format);
    root.set("version", m.version);
    root.set("application", m.application);
    root.set("appVersion", m.appVersion);
    root.set("createdAt", m.createdAt);
    root.set("createdOn", m.createdOn);
    Value runtime = Value::object();
    runtime.set("program", m.runtimeProgram);
    runtime.set("minVersion", m.runtimeMinVersion);
    runtime.set("mode", m.runtimeMode);
    root.set("runtime", std::move(runtime));
    root.set("launcher", m.launcher);
    Value publisher = Value::object();
    publisher.set("name", m.publisherName);
    publisher.set("key", m.publisherKey);
    root.set("publisher", std::move(publisher));
    Value files = Value::array();
    for (const auto& f : m.files) {
        Value o = Value::object();
        o.set("path", f.path);
        o.set("size", static_cast<std::uint64_t>(f.size));
        o.set("sha256", f.sha256);
        files.push(std::move(o));
    }
    root.set("files", std::move(files));
    root.set("totalSize", static_cast<std::uint64_t>(m.totalSize));
    return core::json::write(root, true) + "\n";
}

bool parseManifest(std::string_view json, PackageManifest& out, std::string* why) {
    const auto fail = [&](const std::string& text) {
        if (why) *why = "le manifeste est illisible : " + text;
        return false;
    };
    Value root;
    std::string err;
    if (!core::json::parse(json, root, &err)) return fail(err);
    if (!root.isObject()) return fail("ce n'est pas un objet JSON");
    std::string dup;
    if (duplicateKey(root, dup)) return fail("la cl\xC3\xA9 " + cite(dup) + " y est deux fois");
    PackageManifest m;
    if (!readString(root, "format", m.format, err)) return fail(err);
    if (m.format != kPackageFormat) return fail("ce n'est pas un manifeste de paquet de d\xC3\xA9ploiement (format " + cite(m.format) + ")");
    std::uint64_t version = 0;
    if (!readCount(root, "version", version, err)) return fail(err);
    if (version != static_cast<std::uint64_t>(kPackageVersion)) {
        if (why)
            *why = version > static_cast<std::uint64_t>(kPackageVersion)
                       ? "le paquet est d'un format plus r\xC3\xA9" "cent (version " + std::to_string(version) + ") : mets \xC3\xA0 jour ce programme"
                       : "le manifeste est illisible : version " + std::to_string(version) + " inconnue";
        return false;
    }
    m.version = kPackageVersion;
    if (!readString(root, "application", m.application, err) || !readString(root, "appVersion", m.appVersion, err)
        || !readString(root, "createdAt", m.createdAt, err) || !readString(root, "createdOn", m.createdOn, err)
        || !readString(root, "launcher", m.launcher, err))
        return fail(err);
    const Value* runtime = root.find("runtime");
    if (!runtime || !runtime->isObject()) return fail("le champ \xC2\xAB runtime \xC2\xBB manque ou n'est pas un objet");
    if (!readString(*runtime, "program", m.runtimeProgram, err, "runtime.") || !readString(*runtime, "minVersion", m.runtimeMinVersion, err, "runtime.")
        || !readString(*runtime, "mode", m.runtimeMode, err, "runtime."))
        return fail(err);
    const Value* publisher = root.find("publisher");
    if (!publisher || !publisher->isObject()) return fail("le champ \xC2\xAB publisher \xC2\xBB manque ou n'est pas un objet");
    if (!readString(*publisher, "name", m.publisherName, err, "publisher.") || !readString(*publisher, "key", m.publisherKey, err, "publisher."))
        return fail(err);
    if (!isLowerHex(m.publisherKey, 64)) return fail("la cl\xC3\xA9 de l'\xC3\xA9" "diteur n'est pas une cl\xC3\xA9 Ed25519 (64 caract\xC3\xA8res hexad\xC3\xA9" "cimaux)");
    const Value* files = root.find("files");
    if (!files || !files->isArray()) return fail("le champ \xC2\xAB files \xC2\xBB manque ou n'est pas un tableau");
    m.files.reserve(files->items().size());
    for (std::size_t i = 0; i < files->items().size(); ++i) {
        const Value& f = files->items()[i];
        const std::string where = "files[" + std::to_string(i) + "].";
        if (!f.isObject()) return fail("l'\xC3\xA9l\xC3\xA9ment " + std::to_string(i) + " de \xC2\xAB files \xC2\xBB n'est pas un objet");
        PackageFile pf;
        if (!readString(f, "path", pf.path, err, where) || !readCount(f, "size", pf.size, err, where) || !readString(f, "sha256", pf.sha256, err, where))
            return fail(err);
        if (!isLowerHex(pf.sha256, 64)) return fail("l'empreinte de " + cite(pf.path) + " n'est pas un SHA-256 (64 caract\xC3\xA8res hexad\xC3\xA9" "cimaux minuscules)");
        m.files.push_back(std::move(pf));
    }
    if (!readCount(root, "totalSize", m.totalSize, err)) return fail(err);
    out = std::move(m);
    return true;
}

// ============================================================================== collecte ==
bool readProjectInfo(const std::string& projectFolder, ProjectInfo& out, std::string* why) {
    ProjectInfo info;
    info.folder = projectFolder;
    crypto::Bytes raw;
    const std::string manifestPath = U(P(projectFolder) / "project.xpgproj");
    if (!crypto::readFile(manifestPath, raw, nullptr, 1024u * 1024u)) {
        if (why) *why = "ce dossier n'est pas un projet (project.xpgproj manque ou ne se lit pas) : " + projectFolder;
        return false;
    }
    eachLine(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()), [&](std::string_view line) {
        const auto eq = line.find('=');
        if (eq == std::string_view::npos || (!line.empty() && line.front() == '#')) return;
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k == "name") info.name = v;
        else if (k == "version") info.version = v;
        else if (k == "state") info.state = v;
        else if (k == "edition") info.edition = v;
    });
    // Comme project::stateFromString : ce qui n'est ni DEV, ni FINISH, ni LOCK est NEW.
    if (info.state != "DEV" && info.state != "FINISH" && info.state != "LOCK") info.state = "NEW";
    if (info.name.empty()) info.name = U(P(projectFolder).lexically_normal().filename());
    if (info.name.empty()) info.name = U(P(projectFolder).lexically_normal().parent_path().filename());
    info.lastVersion = lastClosedVersion(projectFolder);
    out = std::move(info);
    return true;
}

ProjectCollection collectProject(const std::string& projectFolder, const PackageLimits& limits) {
    ProjectCollection c;
    c.project.folder = projectFolder;
    std::error_code ec;
    const fs::path root = P(projectFolder);
    if (projectFolder.empty() || !fs::is_directory(root, ec)) {
        c.refusal = "le dossier du projet est introuvable : " + projectFolder;
        return c;
    }
    std::string why;
    const bool isProject = readProjectInfo(projectFolder, c.project, &why);

    std::map<std::string, SkippedFiles> left;
    const auto leave = [&left](const std::string& what, const std::string& reason, std::size_t files, std::uint64_t bytes) {
        auto& s = left[what];
        s.what = what;
        s.why = reason;
        s.files += files;
        s.bytes += bytes;
    };
    const auto take = [&c](const std::string& rel, const fs::path& source, std::uint64_t size) {
        c.files.push_back({rel, U(source), size});
        c.totalBytes += size;
    };
    // ihm/ et donnees/ : tout, sauf les sous-dossiers et les fichiers laisses.
    const auto walk = [&](const std::string& top) {
        const fs::path dir = root / top;
        std::error_code wec;
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, wec), end; !wec && it != end; it.increment(wec)) {
            std::error_code e2;
            std::string rel;
            try {
                rel = top + "/" + UG(it->path().lexically_relative(dir));
            } catch (...) {
                continue;   // un nom que ce systeme ne sait pas convertir
            }
            const std::string name = rel.substr(rel.rfind('/') + 1);
            if (it->is_symlink(e2)) {
                if (it->is_directory(e2)) it.disable_recursion_pending();
                leave(rel, "un lien symbolique (jamais suivi)", 0, 0);
                continue;
            }
            if (it->is_directory(e2)) {
                if (const char* reason = skippedSubfolder(rel)) {
                    std::size_t files = 0;
                    std::uint64_t bytes = 0;
                    countTree(it->path(), files, bytes);
                    leave(rel + "/", reason, files, bytes);
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!it->is_regular_file(e2)) continue;
            const auto raw = it->file_size(e2);
            const std::uint64_t size = e2 ? 0 : static_cast<std::uint64_t>(raw);   // illisible : la construction le dira
            if (const char* reason = skippedFile(name)) {
                leave(rel, reason, 1, size);
                continue;
            }
            take(rel, it->path(), size);
        }
    };
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        std::string name;
        try {
            name = U(it->path().filename());
        } catch (...) {
            continue;
        }
        const bool dir = it->is_directory(e2);
        if (it->is_symlink(e2)) {
            leave(name + (dir ? "/" : ""), "un lien symbolique (jamais suivi)", 0, 0);
            continue;
        }
        if (dir && (name == "ihm" || name == "donnees")) {
            walk(name);
            continue;
        }
        if (!dir && name == "project.xpgproj" && it->is_regular_file(e2)) {
            take(name, it->path(), static_cast<std::uint64_t>(it->file_size(e2)));
            continue;
        }
        std::size_t files = 0;
        std::uint64_t bytes = 0;
        if (dir) {
            countTree(it->path(), files, bytes);
        } else if (it->is_regular_file(e2)) {
            files = 1;
            const auto raw = it->file_size(e2);
            bytes = e2 ? 0 : static_cast<std::uint64_t>(raw);
        }
        leave(name + (dir ? "/" : ""), skippedTop(name, dir), files, bytes);
    }
    std::sort(c.files.begin(), c.files.end(), [](const CollectedFile& a, const CollectedFile& b) { return a.relative < b.relative; });
    for (auto& [k, s] : left) c.skipped.push_back(std::move(s));

    // Le refus : le premier qui compte, dans cet ordre.
    const ProjectInfo& p = c.project;
    if (!isProject) {
        c.refusal = why;
    } else if (p.edition == "api") {
        c.refusal = cite(p.name) + " est un projet d'API Studio : le poste d'exploitation est celui d'IHM Studio.";
    } else if (!p.finished()) {
        c.refusal = "Seul un projet FINISH se met en paquet, comme le poste d'exploitation : " + cite(p.name) + " est " + p.state
                    + ". Termine la version (Projet > \xC3\x89tat > FINISH), puis recommence.";
    } else if (!fs::is_regular_file(root / "ihm" / "ihm.txt", ec)) {
        c.refusal = cite(p.name) + " n'a pas d'IHM (ihm/ihm.txt manque) : il n'y a rien \xC3\xA0 lancer sur le poste.";
    }
    if (!c.refusal.empty()) return c;
    std::map<std::string, std::string> lowered;
    for (const auto& f : c.files) {
        const std::string inPackage = std::string(kPackageAppPrefix) + f.relative;
        const std::string problem = pathProblem(inPackage, limits.maxPathLength);
        if (!problem.empty()) {
            c.refusal = "Le fichier " + cite(f.relative) + " ne peut pas partir : " + problem + ". Renomme-le dans le projet, puis recommence.";
            return c;
        }
        const auto [at, fresh] = lowered.emplace(lowerAscii(inPackage), f.relative);
        if (!fresh) {
            c.refusal = "Les fichiers " + cite(at->second) + " et " + cite(f.relative)
                        + " ne diff\xC3\xA8rent que par la casse : Windows n'en garderait qu'un. Renomme l'un des deux.";
            return c;
        }
        if (f.size > limits.maxFileBytes) {
            c.refusal = "Le fichier " + cite(f.relative) + " p\xC3\xA8se " + packageSizeText(f.size) + " : un paquet n'accepte pas plus de "
                        + sizeLimitText(limits.maxFileBytes) + " par fichier.";
            return c;
        }
    }
    if (c.files.size() + 1 > limits.maxFiles) {   // + le lanceur
        c.refusal = "Le projet compte " + std::to_string(c.files.size()) + " fichiers : un paquet en porte "
                    + std::to_string(limits.maxFiles) + " au plus (le lanceur compris).";
    } else if (c.totalBytes > limits.maxTotalBytes) {
        c.refusal = "Le projet p\xC3\xA8se " + packageSizeText(c.totalBytes) + " : un paquet n'accepte pas plus de " + sizeLimitText(limits.maxTotalBytes)
                    + " au total.";
    }
    return c;
}

// ============================================================================= construire ==
PackageBuildResult buildPackage(const PackageBuildOptions& o, const DevKey& key) {
    PackageBuildResult r;
    const auto fail = [&r](std::string text) {
        r.ok = false;
        r.error = std::move(text);
        return r;
    };
    if (!crypto::init()) return fail("le hasard du syst\xC3\xA8me est indisponible (libsodium) : rien n'est construit.");
    if (o.outputPath.empty() || !endsWith(lowerAscii(o.outputPath), kPackageExtension))
        return fail("le paquet doit porter l'extension .xpgdeploy : " + cite(o.outputPath));
    if (o.launcherName.empty() || o.launcherName.find('/') != std::string::npos || o.launcherText.empty())
        return fail("le lanceur manque (son nom, sans dossier, et son contenu) : le poste ne saurait pas d\xC3\xA9marrer.");
    const std::string launcherPath = std::string(kPackageAppPrefix) + o.launcherName;
    if (std::string bad; !safePackagePath(launcherPath, &bad, o.limits.maxPathLength)) return fail("le lanceur : " + bad);

    ProjectCollection c = collectProject(o.projectFolder, o.limits);
    if (!c.ok()) return fail(c.refusal);
    if (o.launcherText.size() > o.limits.maxFileBytes) return fail("le lanceur est trop gros (" + packageSizeText(o.launcherText.size()) + ").");
    for (const auto& f : c.files)
        if (lowerAscii(std::string(kPackageAppPrefix) + f.relative) == lowerAscii(launcherPath))
            return fail("le projet contient d\xC3\xA9j\xC3\xA0 un fichier " + cite(f.relative) + " : le lanceur prendrait sa place.");
    if (c.totalBytes + o.launcherText.size() > o.limits.maxTotalBytes)
        return fail("le projet et son lanceur d\xC3\xA9passent " + sizeLimitText(o.limits.maxTotalBytes) + ".");

    PackageManifest m;
    m.application = o.application.empty() ? c.project.name : o.application;
    m.appVersion = !o.appVersion.empty() ? o.appVersion : !c.project.lastVersion.empty() ? c.project.lastVersion : c.project.version;
    m.createdAt = utcNow();
    m.createdOn = o.createdOn.empty() ? DevKey::computerName() : o.createdOn;
    m.runtimeProgram = o.runtimeProgram;
    m.runtimeMinVersion = o.runtimeMinVersion.empty() ? std::string(XPG_ANALYZER_VERSION) : o.runtimeMinVersion;
    m.runtimeMode = o.runtimeMode;
    m.launcher = launcherPath;
    m.publisherName = key.name();
    m.publisherKey = key.publicKeyHex();

    // Le fichier temporaire, a cote du paquet ; il prend sa place a la fin.
    const fs::path target = P(o.outputPath);
    const fs::path tmp = fs::path(target).concat(".tmp");
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    Sink sink;
    sink.out.open(tmp, std::ios::binary | std::ios::trunc);
    if (!sink.out) return fail("impossible d'\xC3\xA9" "crire le paquet : " + U(tmp) + " (dossier en lecture seule, ou acc\xC3\xA8s refus\xC3\xA9 ?)");
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    zip.m_pWrite = sinkWrite;
    zip.m_pIO_opaque = &sink;
    bool zipOpen = false;
    const auto abandon = [&](std::string text) {
        if (zipOpen) mz_zip_writer_end(&zip);
        zipOpen = false;
        sink.out.close();
        std::error_code e;
        fs::remove(tmp, e);
        return fail(std::move(text));
    };
    if (!mz_zip_writer_init_v2(&zip, 0, 0)) return abandon("le zip ne d\xC3\xA9marre pas : " + zipError(zip));
    zipOpen = true;

    PackageProgress progress;
    progress.filesTotal = c.files.size() + 1;
    progress.bytesTotal = c.totalBytes + o.launcherText.size();
    const auto tell = [&](const std::string& current) {
        progress.current = current;
        return !o.progress || o.progress(progress);
    };
    MZ_TIME_T now = std::time(nullptr);
    for (const auto& f : c.files) {
        const std::string name = std::string(kPackageAppPrefix) + f.relative;
        if (!tell(name)) {
            r.cancelled = true;
            return abandon("construction annul\xC3\xA9" "e : aucun paquet n'est \xC3\xA9" "crit.");
        }
        Source src;
        src.in.open(P(f.source), std::ios::binary);
        if (!src.in) return abandon("impossible de lire " + cite(f.relative) + " (ouvert ailleurs, ou acc\xC3\xA8s refus\xC3\xA9 ?)");
        src.expected = f.size;
        src.progress = &o.progress;
        src.state = &progress;
        const mz_uint level = alreadyPacked(f.relative) ? 0 : static_cast<mz_uint>(MZ_DEFAULT_LEVEL);
        const bool added = mz_zip_writer_add_read_buf_callback(&zip, name.c_str(), sourceRead, &src, f.size, &now, nullptr, 0, level, nullptr, 0,
                                                               nullptr, 0) != 0;
        if (src.cancelled) {
            r.cancelled = true;
            return abandon("construction annul\xC3\xA9" "e : aucun paquet n'est \xC3\xA9" "crit.");
        }
        if (src.failed || (added && src.done != f.size) || (!added && mz_zip_get_last_error(&zip) == MZ_ZIP_FILE_READ_FAILED))
            return abandon("le fichier " + cite(f.relative) + " a chang\xC3\xA9 ou ne se lit plus pendant la construction : recommence.");
        if (!added) return abandon("le zip refuse " + cite(f.relative) + " : " + zipError(zip));
        if (sink.failed) return abandon("l'\xC3\xA9" "criture du paquet a \xC3\xA9" "chou\xC3\xA9 (disque plein ?) : " + U(tmp));
        m.files.push_back({name, f.size, src.hash.hex()});
        ++progress.filesDone;
    }
    // Le lanceur, donne par l'appelant.
    if (!tell(launcherPath)) {
        r.cancelled = true;
        return abandon("construction annul\xC3\xA9" "e : aucun paquet n'est \xC3\xA9" "crit.");
    }
    if (!mz_zip_writer_add_mem_ex_v2(&zip, launcherPath.c_str(), o.launcherText.data(), o.launcherText.size(), nullptr, 0,
                                     static_cast<mz_uint>(MZ_DEFAULT_LEVEL), 0, 0, &now, nullptr, 0, nullptr, 0))
        return abandon("le zip refuse le lanceur : " + zipError(zip));
    m.files.push_back({launcherPath, o.launcherText.size(), crypto::sha256Hex(o.launcherText)});
    ++progress.filesDone;
    progress.bytesDone += o.launcherText.size();

    std::sort(m.files.begin(), m.files.end(), [](const PackageFile& a, const PackageFile& b) { return a.path < b.path; });
    for (const auto& f : m.files) m.totalSize += f.size;

    // Le manifeste et sa signature : ces octets-la, tels quels.
    const std::string manifestText = writeManifest(m);
    const std::string signature = crypto::hex(key.sign(manifestText));
    if (!mz_zip_writer_add_mem_ex_v2(&zip, std::string(kPackageManifestName).c_str(), manifestText.data(), manifestText.size(), nullptr, 0,
                                     static_cast<mz_uint>(MZ_DEFAULT_LEVEL), 0, 0, &now, nullptr, 0, nullptr, 0)
        || !mz_zip_writer_add_mem_ex_v2(&zip, std::string(kPackageSignatureName).c_str(), signature.data(), signature.size(), nullptr, 0, 0, 0, 0,
                                        &now, nullptr, 0, nullptr, 0))
        return abandon("le zip refuse le manifeste : " + zipError(zip));
    if (!mz_zip_writer_finalize_archive(&zip)) return abandon("le zip ne se termine pas : " + zipError(zip));
    mz_zip_writer_end(&zip);
    zipOpen = false;
    sink.out.flush();
    const bool written = !sink.failed && static_cast<bool>(sink.out);
    sink.out.close();
    if (!written) return abandon("l'\xC3\xA9" "criture du paquet a \xC3\xA9" "chou\xC3\xA9 (disque plein ?) : " + U(tmp));

    std::string digest;
    if (sink.sequential) {
        digest = sink.hash.hex();
    } else if (std::string hashWhy; !crypto::sha256File(U(tmp), digest, nullptr, &hashWhy)) {
        return abandon("le paquet \xC3\xA9" "crit ne se relit pas : " + hashWhy);
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        std::error_code e;
        fs::remove(target, e);
        ec.clear();
        fs::rename(tmp, target, ec);
    }
    if (ec) return abandon("impossible de remplacer " + cite(o.outputPath) + " : " + ec.message());
    if (o.progress) {
        progress.current.clear();
        (void)o.progress(progress);
    }
    r.ok = true;
    r.path = o.outputPath;
    r.sha256 = digest;
    r.packageBytes = sink.size;
    r.manifest = std::move(m);
    return r;
}

// ============================================================================== verifier ==
namespace {

struct Entry {
    mz_uint       index{0};
    std::string   name;
    std::uint64_t size{0};
    bool          directory{false};
    bool          link{false};
};

// Le zip ouvert en lecture, ferme a la fin quoi qu'il arrive.
struct ZipReader {
    ZipFile        file;
    mz_zip_archive zip{};
    bool           open{false};
    ZipReader() { std::memset(&zip, 0, sizeof zip); }
    ~ZipReader() {
        if (open) mz_zip_reader_end(&zip);
    }
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;
};

// Lire en memoire une petite entree (le manifeste, la signature) : sa taille a deja ete bornee.
bool readSmall(mz_zip_archive& zip, mz_uint index, std::string& out) {
    std::size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(&zip, index, &size, 0);
    if (!data) return false;
    out.assign(static_cast<const char*>(data), size);
    mz_free(data);
    return true;
}

} // namespace

PackageReport verifyPackage(const std::string& path, const PackageVerifyOptions& opt) {
    PackageReport r;
    const PackageLimits& lim = opt.limits;
    const auto stop = [&r](int step, PackageVerdict verdict, std::string reason) {
        r.failedStep = step;
        r.verdict = verdict;
        r.reason = std::move(reason);
        return r;
    };
    const auto refuse = [&stop](int step, const std::string& reason) { return stop(step, PackageVerdict::Refused, "Paquet refus\xC3\xA9 : " + reason); };
    if (!crypto::init()) return refuse(1, "libsodium ne d\xC3\xA9marre pas.");

    // ---- 1. le zip se lit ; le manifeste et la signature y sont ; les limites --------------
    ZipReader z;
    std::error_code ec;
    z.file.size = static_cast<std::uint64_t>(fs::file_size(P(path), ec));
    if (ec) return refuse(1, "le fichier " + cite(path) + " est introuvable ou illisible.");
    z.file.in.open(P(path), std::ios::binary);
    if (!z.file.in) return refuse(1, "le fichier " + cite(path) + " ne s'ouvre pas.");
    z.zip.m_pRead = zipRead;
    z.zip.m_pIO_opaque = &z.file;
    if (!mz_zip_reader_init(&z.zip, z.file.size, 0))
        return refuse(1, "ce n'est pas un zip lisible (" + zipError(z.zip) + ") : le fichier est ab\xC3\xAEm\xC3\xA9 ou n'est pas un paquet.");
    z.open = true;
    const mz_uint count = mz_zip_reader_get_num_files(&z.zip);
    if (static_cast<std::uint64_t>(count) > lim.maxFiles + 2)
        return refuse(1, "le paquet contient " + std::to_string(count) + " entr\xC3\xA9" "es : la limite est de " + std::to_string(lim.maxFiles)
                             + " fichiers.");
    std::vector<Entry> entries;
    entries.reserve(count);
    std::set<std::string> names;
    int manifestAt = -1, signatureAt = -1;
    std::uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z.zip, i, &st)) return refuse(1, "l'entr\xC3\xA9" "e " + std::to_string(i) + " du zip est illisible.");
        Entry e;
        e.index = i;
        const mz_uint need = mz_zip_reader_get_filename(&z.zip, i, nullptr, 0);
        std::vector<char> buf(need ? need : 1);
        mz_zip_reader_get_filename(&z.zip, i, buf.data(), static_cast<mz_uint>(buf.size()));
        e.name.assign(buf.data(), need ? need - 1 : 0);
        e.size = st.m_uncomp_size;
        e.directory = st.m_is_directory != 0;
        const std::uint32_t mode = st.m_external_attr >> 16;
        e.link = (mode & 0170000u) == 0120000u || (st.m_external_attr & 0x400u) != 0;   // lien Unix, point d'analyse Windows
        if (st.m_is_encrypted || !st.m_is_supported)
            return refuse(1, "l'entr\xC3\xA9" "e " + cite(e.name) + " est chiffr\xC3\xA9" "e ou compress\xC3\xA9" "e d'une fa\xC3\xA7on inconnue.");
        if (!mz_zip_validate_file(&z.zip, i, MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY))
            return refuse(1, "l'entr\xC3\xA9" "e " + cite(e.name) + " est incoh\xC3\xA9rente (son en-t\xC3\xAAte local ne correspond pas au r\xC3\xA9pertoire du zip).");
        if (!names.insert(e.name).second) return refuse(1, "deux entr\xC3\xA9" "es du zip portent le m\xC3\xAAme nom : " + cite(e.name) + ".");
        if (e.name == kPackageManifestName) {
            manifestAt = static_cast<int>(entries.size());
        } else if (e.name == kPackageSignatureName) {
            signatureAt = static_cast<int>(entries.size());
        } else {
            if (e.size > lim.maxFileBytes)
                return refuse(1, cite(e.name) + " p\xC3\xA8se " + packageSizeText(e.size) + " : la limite est de " + packageSizeText(lim.maxFileBytes)
                                     + " par fichier.");
            total += e.size;
            if (total > lim.maxTotalBytes)
                return refuse(1, "le contenu d\xC3\xA9passe " + packageSizeText(lim.maxTotalBytes) + " au total.");
        }
        entries.push_back(std::move(e));
    }
    if (manifestAt < 0) return refuse(1, "manifest.json manque : ce n'est pas un paquet de d\xC3\xA9ploiement.");
    if (signatureAt < 0) return refuse(1, "manifest.sig manque : le paquet n'est pas sign\xC3\xA9.");
    if (entries.size() - 2 > lim.maxFiles)
        return refuse(1, "le paquet contient " + std::to_string(entries.size() - 2) + " fichiers : la limite est de " + std::to_string(lim.maxFiles) + ".");
    const Entry& manifestEntry = entries[static_cast<std::size_t>(manifestAt)];
    const Entry& signatureEntry = entries[static_cast<std::size_t>(signatureAt)];
    if (manifestEntry.size > lim.maxManifestBytes)
        return refuse(1, "manifest.json p\xC3\xA8se " + packageSizeText(manifestEntry.size) + " : la limite est de " + packageSizeText(lim.maxManifestBytes) + ".");
    if (signatureEntry.size > 1024) return refuse(1, "manifest.sig est trop gros pour une signature.");
    std::string manifestBytes, signatureText;
    if (!readSmall(z.zip, manifestEntry.index, manifestBytes) || !readSmall(z.zip, signatureEntry.index, signatureText))
        return refuse(1, "le manifeste ou sa signature ne se lit pas (" + zipError(z.zip) + ").");

    // ---- 2. la signature, sur les octets lus ; la cle approuvee -------------------------------
    crypto::Bytes sigBytes;
    if (!crypto::unhex(signatureText, sigBytes, 64))
        return stop(2, PackageVerdict::BadSignature,
                    "Paquet refus\xC3\xA9 : manifest.sig n'est pas une signature Ed25519 (128 caract\xC3\xA8res hexad\xC3\xA9" "cimaux).");
    crypto::Signature sig{};
    std::copy(sigBytes.begin(), sigBytes.end(), sig.begin());
    const crypto::PublicKey* approved = nullptr;
    for (const auto& k : opt.approvedKeys)
        if (crypto::verify(k, manifestBytes, sig)) {
            approved = &k;
            break;
        }
    // Signe par une cle approuvee : le JSON vient d'un editeur de confiance. Sinon il est lu pour savoir qui il annonce.
    std::string why;
    if (!parseManifest(manifestBytes, r.manifest, &why)) return refuse(2, why + ".");
    r.manifestRead = true;
    crypto::Bytes pubBytes;
    crypto::PublicKey pub{};
    if (crypto::unhex(r.manifest.publisherKey, pubBytes, 32)) std::copy(pubBytes.begin(), pubBytes.end(), pub.begin());
    r.publisherFingerprint = keyFingerprint(pub);
    const std::string publisher = cite(r.manifest.publisherName) + " (" + r.publisherFingerprint + ")";
    if (approved) {
        if (*approved != pub) return refuse(2, "le manifeste annonce un autre \xC3\xA9" "diteur que la cl\xC3\xA9 qui l'a sign\xC3\xA9.");
        r.signatureValid = true;
        r.publisherApproved = true;
    } else if (crypto::verify(pub, manifestBytes, sig)) {
        r.signatureValid = true;
        if (!opt.inspect)
            return stop(2, PackageVerdict::UnapprovedPublisher,
                        "Signature valide, mais l'\xC3\xA9" "diteur " + publisher
                            + " n'est pas approuv\xC3\xA9 sur ce PC : seul un PC de d\xC3\xA9veloppement appair\xC3\xA9 peut y d\xC3\xA9ployer.");
    } else {
        return stop(2, PackageVerdict::BadSignature,
                    "Paquet refus\xC3\xA9 : la signature est fausse. Le manifeste a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9 apr\xC3\xA8s sa signature, "
                    "ou il a \xC3\xA9t\xC3\xA9 sign\xC3\xA9 par une autre cl\xC3\xA9 que celle qu'il annonce.");
    }
    const PackageManifest& m = r.manifest;

    // ---- 3. chaque chemin est sur ; ni lien, ni dossier ; pas deux chemins a la casse pres ------
    std::map<std::string, std::string> lowered;
    const auto checkPath = [&](const std::string& p, const char* what) -> std::string {   // what : le debut de la phrase
        if (const std::string problem = pathProblem(p, lim.maxPathLength); !problem.empty())
            return std::string(what) + " " + cite(p) + " : " + problem + ".";
        return {};
    };
    for (const auto& f : m.files) {
        if (auto bad = checkPath(f.path, "chemin dangereux dans le manifeste,"); !bad.empty()) return refuse(3, bad);
        const auto [at, fresh] = lowered.emplace(lowerAscii(f.path), f.path);
        if (!fresh)
            return refuse(3, at->second == f.path ? "le manifeste cite deux fois " + cite(f.path) + "."
                                                  : "les chemins " + cite(at->second) + " et " + cite(f.path)
                                                        + " ne diff\xC3\xA8rent que par la casse : Windows n'en garderait qu'un.");
    }
    if (auto bad = checkPath(m.launcher, "lanceur dangereux,"); !bad.empty()) return refuse(3, bad);
    for (const auto& e : entries) {
        if (e.name == kPackageManifestName || e.name == kPackageSignatureName) continue;
        if (e.link) return refuse(3, "l'entr\xC3\xA9" "e " + cite(e.name) + " est un lien symbolique.");
        if (e.directory) return refuse(3, "l'entr\xC3\xA9" "e " + cite(e.name) + " est un dossier : un paquet ne porte que des fichiers.");
        if (auto bad = checkPath(e.name, "entr\xC3\xA9" "e dangereuse dans le zip,"); !bad.empty()) return refuse(3, bad);
        const auto [at, fresh] = lowered.emplace(lowerAscii(e.name), e.name);
        if (!fresh && at->second != e.name)
            return refuse(3, "les chemins " + cite(at->second) + " et " + cite(e.name)
                                 + " ne diff\xC3\xA8rent que par la casse : Windows n'en garderait qu'un.");
    }

    // ---- 4. exactement les memes fichiers ; chaque taille, chaque SHA-256 -------------------------
    if (m.files.size() > lim.maxFiles)
        return refuse(4, "le manifeste cite " + std::to_string(m.files.size()) + " fichiers : la limite est de " + std::to_string(lim.maxFiles) + ".");
    std::map<std::string, const Entry*> inZip;
    for (const auto& e : entries)
        if (e.name != kPackageManifestName && e.name != kPackageSignatureName) inZip[e.name] = &e;
    std::set<std::string> inManifest;
    std::uint64_t sum = 0;
    bool launcherListed = false;
    for (const auto& f : m.files) {
        inManifest.insert(f.path);
        sum += f.size;
        launcherListed = launcherListed || f.path == m.launcher;
        if (inZip.find(f.path) == inZip.end()) return refuse(4, "un fichier du manifeste manque dans le paquet : " + cite(f.path) + ".");
    }
    for (const auto& [name, e] : inZip)
        if (!inManifest.count(name)) return refuse(4, "le paquet contient un fichier que le manifeste ne cite pas : " + cite(name) + ".");
    if (!launcherListed) return refuse(4, "le lanceur " + cite(m.launcher) + " n'est pas dans la liste des fichiers.");
    if (sum != m.totalSize)
        return refuse(4, "la taille totale annonc\xC3\xA9" "e (" + std::to_string(m.totalSize) + ") n'est pas la somme des fichiers (" + std::to_string(sum) + ").");
    for (const auto& f : m.files) {
        const Entry& e = *inZip[f.path];
        if (e.size != f.size)
            return refuse(4, "la taille de " + cite(f.path) + " ne correspond pas au manifeste (" + std::to_string(e.size) + " au lieu de "
                                 + std::to_string(f.size) + ").");
    }
    PackageProgress progress;
    progress.filesTotal = m.files.size();
    progress.bytesTotal = m.totalSize;
    std::vector<char> buf(64 * 1024);
    for (const auto& f : m.files) {
        const Entry& e = *inZip[f.path];
        progress.current = f.path;
        if (opt.progress && !opt.progress(progress)) {
            r.cancelled = true;
            return refuse(4, "v\xC3\xA9rification interrompue.");
        }
        mz_zip_reader_extract_iter_state* it = mz_zip_reader_extract_iter_new(&z.zip, e.index, 0);
        if (!it) return refuse(4, cite(f.path) + " ne se lit pas (" + zipError(z.zip) + ").");
        Hasher hash;
        std::uint64_t done = 0;
        bool tooLong = false, cancelled = false;
        while (true) {
            const std::size_t got = mz_zip_reader_extract_iter_read(it, buf.data(), buf.size());
            if (got == 0) break;
            done += got;
            if (done > f.size) {
                tooLong = true;
                break;
            }
            hash.add(buf.data(), got);
            progress.bytesDone += got;
            if (opt.progress && !opt.progress(progress)) {
                cancelled = true;
                break;
            }
        }
        const bool intact = mz_zip_reader_extract_iter_free(it) != 0;
        if (cancelled) {
            r.cancelled = true;
            return refuse(4, "v\xC3\xA9rification interrompue.");
        }
        if (tooLong || done != f.size || (!intact && !tooLong))
            return refuse(4, cite(f.path) + " est ab\xC3\xAEm\xC3\xA9 dans le zip (" + (intact ? std::string("taille fausse") : zipError(z.zip)) + ").");
        if (hash.hex() != f.sha256)
            return refuse(4, "le contenu de " + cite(f.path) + " ne correspond pas au manifeste (SHA-256) : le fichier a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9.");
        ++progress.filesDone;
    }

    const std::string content = std::to_string(m.files.size()) + " fichiers, " + packageSizeText(m.totalSize);
    r.failedStep = 0;
    if (r.publisherApproved) {
        r.verdict = PackageVerdict::Accepted;
        r.reason = "Paquet valide : " + m.application + " " + m.appVersion + ", sign\xC3\xA9 par " + publisher + " ; " + content + ".";
    } else {
        r.verdict = PackageVerdict::UnapprovedPublisher;
        r.reason = "Signature valide, \xC3\xA9" "diteur " + publisher + " non approuv\xC3\xA9 sur ce PC ; le contenu est intact (" + content + ").";
    }
    return r;
}

} // namespace deploy
