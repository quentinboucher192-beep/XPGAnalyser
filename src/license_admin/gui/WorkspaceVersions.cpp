// =============================================================================
//  license_admin/gui/WorkspaceVersions.cpp - 1.44.0 : LES FICHIERS, LES MESSAGES
//  A UN PC, LES VERSIONS, LES ADRESSES DE SECOURS, LA SAUVEGARDE COMPLETE, vus
//  par le Workspace (docs/ADMINS.md, par. 6, 9 et 10 ; voir Workspace.hpp)
// -----------------------------------------------------------------------------
//  LES FICHIERS : un fichier (un installateur...) part en morceaux de 1 Mio. Son
//  MANIFESTE (genre << fichier >>, signe par la cle de l'editeur) dit son nom,
//  sa taille, son SHA-256 (lu en flux, jamais tout le fichier en memoire) ; le
//  service dit les morceaux qui manquent (un envoi coupe REPREND : le meme
//  manifeste, garde dans fichiers-envoyes.json, et seulement ce qui manque).
//
//  LES MESSAGES : le contenu (texte, fichiers joints et leurs liens, l'action)
//  est SCELLE pour la cle du PC (gateway::sealFor) ; la cle du PC vient de ce
//  que ce poste connait (le registre des activations, les demandes relevees, le
//  releve des licences en ligne). Un lien de fichier vaut 48 h au plus (le
//  service ne permet pas plus).
//
//  LES VERSIONS : la version d'un installateur est lue dans sa ressource
//  VS_VERSIONINFO (le PE est lu comme des octets, jamais lance) : ProductVersion
//  (VersionInfoProductVersion = {#Version} des .iss), sinon FileVersion
//  (VersionInfoVersion = {#Version}.0), sinon les nombres de VS_FIXEDFILEINFO.
//
//  LA SAUVEGARDE (par. 9) : sauvegarde-licences-AAAA-MM-JJ-HHMM.xpgsauve
//      XPG-SAUVEGARDE 1
//      cle=<identifiant de la cle de l'editeur>
//      creee=<heure ISO> service=<adresse>
//      argon2id ops=<n> mem=<octets> sel=<32 hexadecimaux>
//      <base64 : nonce (24 octets) + XChaCha20-Poly1305 du contenu ; les quatre
//       lignes d'en-tete sont les donnees associees>
//  Le contenu (JSON) : {"format":1,"cree","logiciel","service_url",
//  "service":{"manifeste":{...},"tables":{"<table>":[<lignes TELLES QUELLES>]}},
//  "poste":{"<chemin relatif>":"<base64>"}, "cle_editeur":{<la charge utile de la
//  cle : KeyBackup.hpp>}, "cle_synchro":"XPGS1-...", "codes":{"<licence>":"<code>"}}.
//  Les lignes du service ne sont jamais relues ni reecrites : leur texte JSON
//  passe tel quel de GET /admin/sauvegarde a POST /admin/restauration/lot.
//  Les fichiers lies a CE compte (les cles *.cle, les codes, le mot de passe du
//  mail, sauvegarde.cle, le jeton d'admin) ne sont pas copies : la cle de synchro
//  et les codes vont dans le contenu chiffre (en clair dedans), la cle de
//  l'editeur aussi ; a la restauration, ils sont proteges de nouveau pour CE
//  compte. La cle derivee de la phrase (et son sel) est gardee protegee pour ce
//  compte dans sauvegarde.cle : la sauvegarde du jour se fait sans la phrase.
//
//  LES FICHIERS DU DOSSIER DE LA CLE (en plus de ceux de WorkspaceAdmins.cpp) :
//    fichiers-envoyes.json   les fichiers deposes par ce poste (le manifeste signe,
//                            le chemin, complet) : reprendre, renvoyer
//    sauvegarde.json         le dossier des sauvegardes, la derniere
//    sauvegarde.cle          la cle derivee de la phrase et son sel (DPAPI)
// =============================================================================
#include "Workspace.hpp"

#include "../Mail.hpp"
#include "../../core/Json.hpp"
#include "../../deploy/Crypto.hpp"
#include "../../deploy/Paths.hpp"
#include "../../licensing/Gateway.hpp"
#include "../../licensing/Service.hpp"
#include "../../licensing/SignedFile.hpp"

#include <sodium.h>

#include "../../../third_party/miniz.h"   // 1.46.1 : un installateur dans un .zip (Versions)

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <system_error>

namespace license_admin::gui {

namespace {

namespace fs = std::filesystem;
namespace crypto = deploy::crypto;
namespace adm = licensing::admins;
namespace gw = licensing::gateway;
namespace svc = licensing::service;
using core::json::Value;
using licensing::DocKind;
using licensing::SignedBlock;

constexpr const char* kSentFilesFile = "fichiers-envoyes.json";
constexpr const char* kBackupSettingsFile = "sauvegarde.json";
constexpr const char* kBackupKeyFile = "sauvegarde.cle";
constexpr const char* kAdminTokenFile = "jeton-admin.xpgjet";
constexpr const char* kBackupPrefix = "sauvegarde-licences-";
constexpr const char* kBackupExtension = ".xpgsauve";
constexpr const char* kBackupMarker = "XPG-SAUVEGARDE 1";
constexpr const char* kDerivedFormat = "xpg-sauvegarde-cle-derivee/1";
constexpr std::uint64_t kChunk = 1048576;
constexpr int kLinkSeconds = 172800;            // 48 h : un lien << une seule fois >> (un courriel)
constexpr int kAttachSeconds = 30 * 86400;      // 30 jours : un fichier joint a un message (le service le garde autant)
constexpr std::size_t kKeptBackups = 30;
constexpr std::size_t kLotBytes = 2u * 1024u * 1024u;   // un lot de la restauration (le service : 8 Mio au plus)
constexpr std::size_t kMaxSentRecords = 200;
constexpr std::uint64_t kMaxFolderBytes = 512ull * 1024ull * 1024ull;   // le dossier du poste dans une sauvegarde, au plus
constexpr std::uint64_t kMaxBackupFile = 1024ull * 1024ull * 1024ull;
constexpr unsigned long long kMinOps = 2, kMaxOps = 16;
constexpr std::size_t kMinMem = 64u * 1024u * 1024u, kMaxMem = 1024u * 1024u * 1024u;

// ---- les fichiers ----
fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::string at(const std::string& folder, const char* name) { return deploy::joinPath(folder, name); }
bool isFile(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(P(path), ec);
}
bool readText(const std::string& path, std::string& out, std::string* why, std::uint64_t max = 64u * 1024u * 1024u) {
    crypto::Bytes raw;
    if (!crypto::readFile(path, raw, why, max)) return false;
    out.assign(raw.begin(), raw.end());
    crypto::wipe(raw.data(), raw.size());
    return true;
}
bool writeText(const std::string& path, const std::string& text, std::string* why) {
    std::error_code ec;
    fs::create_directories(P(path).parent_path(), ec);
    crypto::Bytes bytes(text.begin(), text.end());
    const bool ok = crypto::writePrivateFile(path, bytes, why);
    crypto::wipe(bytes.data(), bytes.size());
    return ok;
}
bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}
std::string plural(std::uint64_t n, const std::string& one, const std::string& many) { return std::to_string(n) + " " + (n > 1 ? many : one); }
std::string cite(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }
std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    std::size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
    return s.substr(a);
}
std::string clip(std::string s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) --n;
    s.resize(n);
    return s;
}
std::string sizeText(std::uint64_t n) {
    char b[32];
    if (n >= 1024ull * 1024ull) std::snprintf(b, sizeof b, "%.1f Mio", static_cast<double>(n) / (1024.0 * 1024.0));
    else if (n >= 1024) std::snprintf(b, sizeof b, "%.0f Kio", static_cast<double>(n) / 1024.0);
    else std::snprintf(b, sizeof b, "%llu octets", static_cast<unsigned long long>(n));
    return b;
}
licensing::Signer signerOf(const licensing::SigningKey& key) {
    return [&key](std::string_view m) { return key.sign(m); };
}
bool keyFromHex(const std::string& hex, crypto::PublicKey& out) {
    crypto::Bytes raw;
    if (!licensing::isKeyHex(hex) || !crypto::unhex(hex, raw, 32) || raw.size() != 32) return false;
    std::copy(raw.begin(), raw.end(), out.begin());
    return true;
}
std::string randomHex(std::size_t bytes) {
    std::uint8_t b[32];
    crypto::randomBytes(b, bytes);
    return crypto::hex(b, bytes);
}
bool isLowerHex(std::string_view s, std::size_t n) {
    return s.size() == n && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
// La date locale "AAAA-MM-JJ" et le nom "AAAA-MM-JJ-HHMM".
std::tm localTm(Seconds t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    return tm;
}
std::string localDay(Seconds t) {
    if (t <= 0) return {};
    const std::tm tm = localTm(t);
    char b[48];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return b;
}
std::string localStamp(Seconds t) {
    const std::tm tm = localTm(t);
    char b[64];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d-%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    return b;
}
std::string fileStamp(Seconds t) {
    std::string s = licensing::isoTime(t);
    for (auto& c : s)
        if (c == ':') c = '-';
    return s;
}

// ---- le JSON tel quel (les lignes du service ne sont jamais relues ni reecrites) ----
std::size_t skipWs(std::string_view s, std::size_t i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    return i;
}
// s[i] == '"' : l'indice apres le guillemet fermant ; npos : mal forme.
std::size_t skipString(std::string_view s, std::size_t i) {
    if (i >= s.size() || s[i] != '"') return std::string_view::npos;
    for (++i; i < s.size(); ++i) {
        if (s[i] == '\\') {
            ++i;
            continue;
        }
        if (s[i] == '"') return i + 1;
    }
    return std::string_view::npos;
}
// L'indice apres la valeur qui commence en i (apres les blancs) ; npos : mal forme.
std::size_t skipValue(std::string_view s, std::size_t i) {
    i = skipWs(s, i);
    if (i >= s.size()) return std::string_view::npos;
    if (s[i] == '"') return skipString(s, i);
    if (s[i] == '{' || s[i] == '[') {
        int depth = 0;
        while (i < s.size()) {
            const char c = s[i];
            if (c == '"') {
                i = skipString(s, i);
                if (i == std::string_view::npos) return i;
                continue;
            }
            if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') {
                if (--depth == 0) return i + 1;
            }
            ++i;
        }
        return std::string_view::npos;
    }
    const std::size_t start = i;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n') ++i;
    return i > start ? i : std::string_view::npos;
}
// Les membres d'un objet : (nom, valeur telle quelle).
bool rawMembers(std::string_view obj, std::vector<std::pair<std::string, std::string_view>>& out) {
    out.clear();
    std::size_t i = skipWs(obj, 0);
    if (i >= obj.size() || obj[i] != '{') return false;
    i = skipWs(obj, i + 1);
    if (i < obj.size() && obj[i] == '}') return true;
    while (i < obj.size()) {
        const std::size_t k = skipString(obj, i);
        if (k == std::string_view::npos) return false;
        Value key;
        if (!core::json::parse(obj.substr(i, k - i), key, nullptr) || !key.isString()) return false;
        i = skipWs(obj, k);
        if (i >= obj.size() || obj[i] != ':') return false;
        const std::size_t vs = skipWs(obj, i + 1);
        const std::size_t ve = skipValue(obj, vs);
        if (ve == std::string_view::npos) return false;
        out.emplace_back(key.asString(), obj.substr(vs, ve - vs));
        i = skipWs(obj, ve);
        if (i < obj.size() && obj[i] == ',') {
            i = skipWs(obj, i + 1);
            continue;
        }
        return i < obj.size() && obj[i] == '}';
    }
    return false;
}
bool rawMember(std::string_view obj, std::string_view name, std::string_view& out) {
    std::vector<std::pair<std::string, std::string_view>> m;
    if (!rawMembers(obj, m)) return false;
    for (const auto& [k, v] : m)
        if (k == name) {
            out = v;
            return true;
        }
    return false;
}
// Les elements d'un tableau, tels quels.
bool rawItems(std::string_view arr, std::vector<std::string_view>& out) {
    out.clear();
    std::size_t i = skipWs(arr, 0);
    if (i >= arr.size() || arr[i] != '[') return false;
    i = skipWs(arr, i + 1);
    if (i < arr.size() && arr[i] == ']') return true;
    while (i < arr.size()) {
        const std::size_t e = skipValue(arr, i);
        if (e == std::string_view::npos) return false;
        out.push_back(arr.substr(i, e - i));
        i = skipWs(arr, e);
        if (i < arr.size() && arr[i] == ',') {
            i = skipWs(arr, i + 1);
            continue;
        }
        return i < arr.size() && arr[i] == ']';
    }
    return false;
}

// ---- la version d'un installateur (PE, ressource VS_VERSIONINFO) ----
std::uint16_t le16(const std::string& b, std::size_t o) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(b[o]) | (static_cast<unsigned char>(b[o + 1]) << 8));
}
std::uint32_t le32(const std::string& b, std::size_t o) {
    return static_cast<std::uint32_t>(le16(b, o)) | (static_cast<std::uint32_t>(le16(b, o + 2)) << 16);
}
bool readAt(std::ifstream& f, std::uint64_t size, std::uint64_t off, std::size_t n, std::string& out) {
    if (off > size || n > size - off) return false;
    out.assign(n, '\0');
    f.clear();
    f.seekg(static_cast<std::streamoff>(off));
    f.read(out.data(), static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(f.gcount()) == n;
}
void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}
// Une chaine UTF-16LE terminee par un zero, de `p` a `end` au plus ; `p` avance apres le zero.
std::string utf16At(const std::string& b, std::size_t& p, std::size_t end) {
    std::string out;
    while (p + 2 <= end) {
        std::uint32_t c = le16(b, p);
        p += 2;
        if (c == 0) break;
        if (c >= 0xD800 && c <= 0xDBFF && p + 2 <= end) {
            const std::uint32_t lo = le16(b, p);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                p += 2;
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
            }
        }
        if (out.size() < 256) appendUtf8(out, c);
    }
    return out;
}
std::size_t align4(std::size_t x) { return (x + 3) & ~static_cast<std::size_t>(3); }

struct VerNode {
    std::size_t   start{0}, end{0}, valueOff{0}, valueLen{0}, childOff{0};
    std::uint16_t type{0};
    std::string   key;
};
bool readVerNode(const std::string& b, std::size_t off, std::size_t limit, VerNode& n) {
    if (off + 6 > limit) return false;
    const std::size_t len = le16(b, off);
    if (len < 6) return false;
    n.start = off;
    n.end = std::min(off + len, limit);
    const std::uint16_t vlen = le16(b, off + 2);
    n.type = le16(b, off + 4);
    std::size_t p = off + 6;
    n.key = utf16At(b, p, n.end);
    n.valueOff = std::min(align4(p), n.end);
    n.valueLen = std::min<std::size_t>(n.type == 1 ? static_cast<std::size_t>(vlen) * 2 : vlen, n.end - n.valueOff);
    n.childOff = std::min(align4(n.valueOff + n.valueLen), n.end);
    return true;
}

struct PeVersion {
    std::string                  product, file;
    bool                         fixed{false};
    std::array<std::uint16_t, 4> fixedFile{}, fixedProduct{};
};

bool parseVersionInfo(const std::string& b, PeVersion& out, std::string* why) {
    VerNode root;
    if (!readVerNode(b, 0, b.size(), root) || root.key != "VS_VERSION_INFO") return fail(why, "sa ressource de version est illisible");
    if (root.valueLen >= 52 && le32(b, root.valueOff) == 0xFEEF04BDu) {
        const auto four = [&](std::size_t o) {
            const std::uint32_t ms = le32(b, root.valueOff + o), ls = le32(b, root.valueOff + o + 4);
            return std::array<std::uint16_t, 4>{static_cast<std::uint16_t>(ms >> 16), static_cast<std::uint16_t>(ms & 0xFFFF),
                                                static_cast<std::uint16_t>(ls >> 16), static_cast<std::uint16_t>(ls & 0xFFFF)};
        };
        out.fixedFile = four(8);
        out.fixedProduct = four(16);
        out.fixed = true;
    }
    for (std::size_t c = root.childOff; c < root.end;) {
        VerNode child;
        if (!readVerNode(b, c, root.end, child) || child.end <= c) break;
        if (child.key == "StringFileInfo") {
            for (std::size_t t = child.childOff; t < child.end;) {
                VerNode table;
                if (!readVerNode(b, t, child.end, table) || table.end <= t) break;
                for (std::size_t s = table.childOff; s < table.end;) {
                    VerNode str;
                    if (!readVerNode(b, s, table.end, str) || str.end <= s) break;
                    std::size_t p = str.valueOff;
                    const std::string value = utf16At(b, p, str.end);
                    if (str.key == "ProductVersion" && out.product.empty()) out.product = value;
                    if (str.key == "FileVersion" && out.file.empty()) out.file = value;
                    s = align4(str.end);
                }
                t = align4(table.end);
            }
        }
        c = align4(child.end);
    }
    return true;
}

bool readPeVersion(const std::string& path, PeVersion& out, std::string* why) {
    std::error_code ec;
    const auto size = fs::file_size(P(path), ec);
    if (ec) return fail(why, "le fichier " + path + " ne se lit pas");
    std::ifstream f(P(path), std::ios::binary);
    if (!f) return fail(why, "le fichier " + path + " ne s'ouvre pas");
    const std::string notExe = "ce n'est pas un programme Windows (.exe)";
    std::string dos, nt, opt, secs;
    if (!readAt(f, size, 0, 64, dos) || dos[0] != 'M' || dos[1] != 'Z') return fail(why, notExe);
    const std::uint32_t pe = le32(dos, 0x3C);
    if (!readAt(f, size, pe, 24, nt) || nt.compare(0, 4, std::string("PE\0\0", 4)) != 0) return fail(why, notExe);
    const std::uint16_t nsec = le16(nt, 6), optSize = le16(nt, 20);
    if (nsec == 0 || nsec > 96 || optSize < 96 || optSize > 4096 || !readAt(f, size, pe + 24, optSize, opt)) return fail(why, notExe + " (en-t\xC3\xAAte illisible)");
    const std::uint16_t magic = le16(opt, 0);
    std::size_t countOff = 0, dirsOff = 0;
    if (magic == 0x10b) {
        countOff = 92;
        dirsOff = 96;
    } else if (magic == 0x20b) {
        countOff = 108;
        dirsOff = 112;
    } else {
        return fail(why, notExe + " (en-t\xC3\xAAte inconnu)");
    }
    const std::string noVersion = "il n'a pas de ressource de version (VS_VERSIONINFO)";
    if (optSize < dirsOff + 24 || le32(opt, countOff) < 3) return fail(why, noVersion);
    const std::uint32_t resRva = le32(opt, dirsOff + 16), resSize = le32(opt, dirsOff + 20);
    if (resRva == 0 || resSize == 0) return fail(why, noVersion);
    if (!readAt(f, size, pe + 24ull + optSize, 40u * nsec, secs)) return fail(why, notExe + " (sections illisibles)");
    std::string sec;
    std::uint32_t secVa = 0;
    for (std::uint16_t i = 0; i < nsec; ++i) {
        const std::size_t o = 40u * i;
        const std::uint32_t vsize = le32(secs, o + 8), va = le32(secs, o + 12), rawSize = le32(secs, o + 16), rawPtr = le32(secs, o + 20);
        const std::uint32_t span = std::max(vsize, rawSize);
        if (resRva < va || resRva - va >= span) continue;
        const std::size_t len = std::min<std::size_t>(rawSize, 64u * 1024u * 1024u);
        if (!readAt(f, size, rawPtr, len, sec)) return fail(why, "sa section de ressources est tronqu\xC3\xA9" "e");
        secVa = va;
        break;
    }
    if (sec.empty()) return fail(why, noVersion);
    const std::size_t base = resRva - secVa;
    // Un repertoire de ressources : l'entree d'identifiant `want` (-1 : la premiere), sa valeur (bit fort : un sous-repertoire).
    const auto entry = [&](std::size_t dir, long want, std::uint32_t& value) -> bool {
        if (dir + 16 > sec.size()) return false;
        const std::size_t named = le16(sec, dir + 12), ids = le16(sec, dir + 14);
        for (std::size_t k = 0; k < named + ids && k < 4096; ++k) {
            const std::size_t e = dir + 16 + 8 * k;
            if (e + 8 > sec.size()) return false;
            const std::uint32_t name = le32(sec, e);
            if (want >= 0 && ((name & 0x80000000u) || static_cast<long>(name & 0xFFFF) != want)) continue;
            value = le32(sec, e + 4);
            return true;
        }
        return false;
    };
    std::uint32_t v1 = 0, v2 = 0, v3 = 0;
    if (!entry(base, 16, v1) || !(v1 & 0x80000000u)) return fail(why, noVersion);   // RT_VERSION
    const std::size_t d2 = base + (v1 & 0x7FFFFFFFu);
    if (!entry(d2, 1, v2) && !entry(d2, -1, v2)) return fail(why, noVersion);
    if (!(v2 & 0x80000000u)) return fail(why, "sa ressource de version est illisible");
    if (!entry(base + (v2 & 0x7FFFFFFFu), -1, v3) || (v3 & 0x80000000u)) return fail(why, "sa ressource de version est illisible");
    const std::size_t data = base + v3;
    if (data + 8 > sec.size()) return fail(why, "sa ressource de version est illisible");
    const std::uint32_t dataRva = le32(sec, data), dataSize = le32(sec, data + 4);
    if (dataRva < secVa || dataRva - secVa >= sec.size() || dataSize < 6) return fail(why, "sa ressource de version est illisible");
    const std::size_t off = dataRva - secVa;
    const std::string vi = sec.substr(off, std::min<std::size_t>({dataSize, sec.size() - off, 64u * 1024u}));
    return parseVersionInfo(vi, out, why);
}

// "1.44.1", "1.44.1.0", "1, 44, 1, 0" -> "1.44.1" ; vide : pas une version X.Y.Z.
std::string semverOf(std::string s) {
    std::string t;
    for (char c : s) {
        if (c == ' ' || c == '\t') continue;
        t += c == ',' ? '.' : c;
    }
    std::vector<std::string> parts;
    std::size_t pos = 0;
    while (true) {
        const std::size_t dot = t.find('.', pos);
        parts.push_back(t.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos));
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    for (const auto& p : parts)
        if (p.empty() || p.size() > 5 || !std::all_of(p.begin(), p.end(), [](char c) { return c >= '0' && c <= '9'; })) return {};
    if (parts.size() == 4 && parts[3] == "0") parts.pop_back();
    if (parts.size() != 3) return {};
    const auto num = [](const std::string& p) { return std::to_string(std::stoi(p)); };
    return num(parts[0]) + "." + num(parts[1]) + "." + num(parts[2]);
}

// ---- les fichiers deposes par ce poste (fichiers-envoyes.json) ----
std::mutex& sentMutex() {
    static std::mutex m;
    return m;
}

std::vector<SentFile> readSent(const std::string& folder) {
    std::vector<SentFile> out;
    std::string text;
    Value v;
    if (!isFile(at(folder, kSentFilesFile)) || !readText(at(folder, kSentFilesFile), text, nullptr) || !core::json::parse(text, v, nullptr) || !v.isObject())
        return out;
    if (const Value* arr = v.find("fichiers"); arr && arr->isArray())
        for (const auto& x : arr->items()) {
            if (!x.isObject()) continue;
            SentFile f;
            f.block = x.str("bloc");
            f.path = x.str("chemin");
            f.complete = x.flag("complet");
            f.sentAt = static_cast<Seconds>(x.integer("envoye"));
            std::vector<SignedBlock> blocks;
            if (!licensing::readBlocks(f.block, blocks, nullptr) || blocks.size() != 1 || blocks[0].kind != DocKind::FileManifest ||
                !adm::parseFileManifest(blocks[0].document, f.manifest, nullptr))
                continue;
            out.push_back(std::move(f));
        }
    return out;
}

bool writeSent(const std::string& folder, std::vector<SentFile> list, std::string* why) {
    if (list.size() > kMaxSentRecords) {   // les plus anciens (complets d'abord) partent
        std::stable_sort(list.begin(), list.end(), [](const SentFile& a, const SentFile& b) {
            if (a.complete != b.complete) return a.complete && !b.complete;
            return a.sentAt < b.sentAt;
        });
        list.erase(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(list.size() - kMaxSentRecords));
    }
    Value v = Value::object();
    v.set("format", "xpg-fichiers-envoyes/1");
    Value arr = Value::array();
    for (const auto& f : list) {
        Value x = Value::object();
        x.set("id", f.manifest.id);
        x.set("bloc", f.block);
        x.set("chemin", f.path);
        x.set("complet", f.complete);
        x.set("envoye", static_cast<std::int64_t>(f.sentAt));
        arr.push(std::move(x));
    }
    v.set("fichiers", std::move(arr));
    return writeText(at(folder, kSentFilesFile), core::json::write(v, true) + "\n", why);
}

// Ajouter ou mettre a jour l'entree de ce fichier (par son identifiant).
void noteSent(const std::string& folder, const SentFile& f) {
    std::lock_guard<std::mutex> lock(sentMutex());
    auto list = readSent(folder);
    bool found = false;
    for (auto& x : list)
        if (x.manifest.id == f.manifest.id) {
            x = f;
            found = true;
        }
    if (!found) list.push_back(f);
    (void)writeSent(folder, std::move(list), nullptr);
}
void forgetSent(const std::string& folder, const std::string& id) {
    std::lock_guard<std::mutex> lock(sentMutex());
    auto list = readSent(folder);
    list.erase(std::remove_if(list.begin(), list.end(), [&](const SentFile& x) { return x.manifest.id == id; }), list.end());
    (void)writeSent(folder, std::move(list), nullptr);
}

// ---- deposer un fichier ----
struct Uploaded {
    adm::FileManifest manifest;
    int               sent{0};        // les morceaux envoyes par cet appel
    int               already{0};     // ceux deja sur le service (un envoi repris)
    bool              resumed{false};
};

// Ce que rend POST /admin/fichiers et PUT /admin/fichiers/morceau.
struct FileState {
    std::uint64_t              chunks{0}, received{0};
    bool                       complete{false};
    std::vector<std::uint64_t> missing;
};
bool readFileState(const online::Reply& r, FileState& st) {
    if (!r.json.isObject()) return false;
    st.chunks = static_cast<std::uint64_t>(r.json.integer("morceaux"));
    st.received = static_cast<std::uint64_t>(r.json.integer("recus"));
    st.complete = r.json.flag("complet");
    st.missing.clear();
    if (const Value* m = r.json.find("manquants"); m && m->isArray())
        for (const auto& x : m->items())
            if (x.isNumber() && x.asNumber() >= 0) st.missing.push_back(static_cast<std::uint64_t>(x.asNumber()));
    return true;
}

// Le manifeste `block` (deja signe) et les morceaux qui manquent, lus de `path`.
bool pushFile(online::Client& c, const std::string& block, const adm::FileManifest& m, const std::string& path, const Progress& progress,
              std::uint64_t progressBase, std::uint64_t progressTotal, Uploaded& out, std::string* why) {
    out.manifest = m;
    const std::uint64_t total = progressTotal ? progressTotal : m.size;
    const auto report = [&](std::uint64_t received) {
        const std::uint64_t done = std::min<std::uint64_t>(received * m.chunk, m.size);
        return !progress || progress(progressBase + done, total);
    };
    std::ifstream f(P(path), std::ios::binary);
    if (!f) return fail(why, "le fichier " + path + " ne s'ouvre pas");
    bool first = true;
    for (int round = 0; round < 64; ++round) {
        Value body = Value::object();
        body.set("manifeste", block);
        const online::Reply r = c.call("POST", "/admin/fichiers", core::json::write(body, false));
        if (r.error.empty() && r.status == 409 && r.code() == "existe")
            return fail(why, "le service a d\xC3\xA9j\xC3\xA0 un autre fichier sous cet identifiant (" + m.id + ") : recommence l'envoi");
        if ((r.status != 200 && r.status != 201) || !r.error.empty()) return fail(why, c.explain(r, "d\xC3\xA9poser le manifeste de " + m.name));
        FileState st;
        if (!readFileState(r, st)) return fail(why, "le service a r\xC3\xA9pondu sans dire o\xC3\xB9 en est " + m.name);
        if (first) {
            out.already = static_cast<int>(st.received);
            out.resumed = r.status == 200 && st.received > 0 && !st.complete;
            first = false;
        }
        if (!report(st.received)) return fail(why, "envoi de " + m.name + " arr\xC3\xAAt\xC3\xA9 : il reprendra l\xC3\xA0 o\xC3\xB9 il s'est arr\xC3\xAAt\xC3\xA9");
        if (st.complete) return true;
        if (st.missing.empty()) return fail(why, "le service ne dit pas quels morceaux de " + m.name + " lui manquent");
        std::string part;
        for (const std::uint64_t n : st.missing) {
            if (n >= m.chunks()) return fail(why, "le service demande un morceau qui n'existe pas (" + std::to_string(n) + ")");
            const std::uint64_t off = n * m.chunk;
            const std::size_t len = static_cast<std::size_t>(std::min<std::uint64_t>(m.chunk, m.size - off));
            part.assign(len, '\0');
            f.clear();
            f.seekg(static_cast<std::streamoff>(off));
            f.read(part.data(), static_cast<std::streamsize>(len));
            if (static_cast<std::size_t>(f.gcount()) != len) return fail(why, "le fichier " + path + " a chang\xC3\xA9 (plus court) : recommence l'envoi");
            const online::Reply p = c.call("PUT", "/admin/fichiers/morceau?id=" + m.id + "&n=" + std::to_string(n), part);
            if (p.error.empty() && p.status == 409 && p.code() == "empreinte")
                return fail(why, "le fichier re\xC3\xA7u n'a pas le SHA-256 de son manifeste (" + path + " a chang\xC3\xA9 pendant l'envoi ?) : recommence");
            if (p.status != 200 || !p.error.empty()) return fail(why, c.explain(p, "d\xC3\xA9poser le morceau " + std::to_string(n) + " de " + m.name));
            ++out.sent;
            FileState now;
            if (readFileState(p, now)) {
                if (!report(now.received)) return fail(why, "envoi de " + m.name + " arr\xC3\xAAt\xC3\xA9 : il reprendra l\xC3\xA0 o\xC3\xB9 il s'est arr\xC3\xAAt\xC3\xA9");
                if (now.complete) return true;
            }
        }
    }
    return fail(why, "l'envoi de " + m.name + " ne finit pas (le service redemande des morceaux)");
}

// Deposer `path` (le manifeste signe par `editor` ; repris s'il a deja ete commence avec le meme contenu).
bool uploadWith(online::Client& c, const std::string& folder, const licensing::SigningKey& editor, Seconds now, const std::string& path,
                const std::string& kind, const std::string& version, const std::string& access, const Progress& progress, std::uint64_t progressBase,
                std::uint64_t progressTotal, Uploaded& out, std::string* why) {
    out = Uploaded{};
    const std::string name = U(P(path).filename());
    if (!adm::safeFileName(name) || name.back() == '.')
        return fail(why, "le nom " + cite(name) + " ne peut pas partir tel quel (lettres sans accent, chiffres, . _ - et espaces ; 120 caract\xC3\xA8res au plus) : renomme le fichier");
    if (kind != "admin" && kind != "studios" && kind != "autre") return fail(why, "sorte de fichier inconnue : " + kind);
    if (access != "public" && access != "admins" && access != "jeton") return fail(why, "acc\xC3\xA8s inconnu : " + access);
    int a = 0, b = 0, d = 0;
    if (!version.empty() && !adm::parseSemver(version, a, b, d)) return fail(why, "la version " + cite(version) + " n'est pas de la forme X.Y.Z");
    std::string sha;
    std::uint64_t size = 0;
    if (!isFile(path)) return fail(why, "le fichier " + path + " n'existe pas");
    if (!crypto::sha256File(path, sha, &size, why)) return false;
    if (size == 0) return fail(why, "le fichier " + name + " est vide");
    if (size > adm::kMaxFileBytes) return fail(why, name + " est trop gros (" + sizeText(size) + ") : 200 Mio au plus");
    // Le meme fichier deja commence (ou fini) par ce poste : son manifeste, tel quel.
    SentFile rec;
    bool reuse = false;
    {
        std::lock_guard<std::mutex> lock(sentMutex());
        for (const auto& x : readSent(folder))
            if (x.manifest.sha256 == sha && x.manifest.size == size && x.manifest.name == name && x.manifest.kind == kind && x.manifest.version == version &&
                x.manifest.access == access) {
                rec = x;
                reuse = true;
            }
    }
    if (!reuse) {
        adm::FileManifest m;
        m.id = randomHex(16);
        m.name = name;
        m.size = size;
        m.sha256 = sha;
        m.kind = kind;
        m.version = version;
        m.access = access;
        m.chunk = kChunk;
        m.issued = now;
        rec.manifest = m;
        rec.block = licensing::writeBlock(licensing::signBlock(DocKind::FileManifest, adm::writeFileManifest(m), editor.publicKey(), signerOf(editor)));
        rec.sentAt = now;
    }
    rec.path = path;
    rec.complete = false;
    noteSent(folder, rec);   // avant le premier octet : un envoi coupe reprend
    std::string w;
    if (!pushFile(c, rec.block, rec.manifest, path, progress, progressBase, progressTotal, out, &w)) {
        if (w.find("SHA-256") != std::string::npos) forgetSent(folder, rec.manifest.id);
        return fail(why, w);
    }
    rec.complete = true;
    noteSent(folder, rec);
    return true;
}

// ---- les PC que connait ce poste (relus du dossier : les fonctions lentes ne lisent pas l'etat du Workspace) ----
std::string versionIn(const std::string& software) {
    std::string best;
    std::size_t i = 0;
    while (i < software.size()) {
        if (software[i] >= '0' && software[i] <= '9') {
            std::size_t j = i;
            while (j < software.size() && ((software[j] >= '0' && software[j] <= '9') || software[j] == '.')) ++j;
            int x = 0, y = 0, z = 0;
            std::string cand = software.substr(i, j - i);
            while (!cand.empty() && cand.back() == '.') cand.pop_back();
            if (adm::parseSemver(cand, x, y, z)) best = cand;
            i = j;
        } else {
            ++i;
        }
    }
    return best;
}

std::vector<MessagePc> knownPcs(const std::string& folder) {
    std::map<std::string, MessagePc> byKey;
    std::map<std::string, std::string> customerOf;   // licence -> client
    const auto add = [&](const std::string& key, const std::string& name, const std::string& customer, const std::string& licenseId,
                         const std::string& software, Seconds seen) {
        crypto::PublicKey pk{};
        if (!keyFromHex(key, pk)) return;
        MessagePc& p = byKey[key];
        if (p.deviceKey.empty()) {
            p.deviceKey = key;
            p.deviceId = svc::deviceId(pk);
        }
        const bool newer = seen >= p.lastSeen;
        const auto take = [&](std::string& field, const std::string& value) {
            if (!value.empty() && (newer || field.empty())) field = value;
        };
        take(p.name, name);
        take(p.customer, customer);
        take(p.licenseId, licenseId);
        if (!software.empty() && (newer || p.studio.empty())) {
            p.studio = software;
            p.studioVersion = versionIn(software);
        }
        p.lastSeen = std::max(p.lastSeen, seen);
    };
    std::vector<ActivationRecord> records;
    (void)readActivations(folder, records, nullptr, nullptr);
    for (const auto& r : records) {
        if (!r.licenseId.empty() && !r.customer.empty()) customerOf[r.licenseId] = r.customer;
        if (r.deviceKey.empty()) continue;
        const Seconds seen = std::max({r.at, r.activatedAt, r.signedAt, r.requestedAt});
        add(r.deviceKey, r.deviceName, r.customer, r.licenseId, r.software, seen);
    }
    for (const auto& e : online::listRequests(folder)) {
        if (e.deviceKey.empty()) continue;
        add(e.deviceKey, e.deviceName, e.customer, e.licenseId, e.software, std::max(e.requestedAt, e.createdAt));
    }
    const online::Snapshot snap = online::readSnapshot(folder);
    for (const auto& l : snap.licences)
        for (const auto& a : l.activations) {
            const auto it = customerOf.find(l.licenseId);
            add(a.deviceKey, a.name, it == customerOf.end() ? std::string() : it->second, l.licenseId, {}, a.seenAt);
        }
    std::vector<MessagePc> out;
    for (auto& [k, p] : byKey) {
        if (p.customer.empty() && !p.licenseId.empty())
            if (const auto it = customerOf.find(p.licenseId); it != customerOf.end()) p.customer = it->second;
        out.push_back(std::move(p));
    }
    std::sort(out.begin(), out.end(), [](const MessagePc& a, const MessagePc& b) {
        if (a.customer != b.customer) return a.customer < b.customer;
        if (a.name != b.name) return a.name < b.name;
        return a.deviceId < b.deviceId;
    });
    return out;
}

// ---- les versions publiees (GET /admin/versions) ----
bool readVersions(online::Client& c, const detail::Caller& caller, const std::string& folder, VersionsView& view, std::string* why) {
    view = VersionsView{};
    const online::Reply r = c.call("GET", "/admin/versions");
    if (r.status != 200 || !r.error.empty() || !r.json.isObject()) return fail(why, c.explain(r, "lire les versions publi\xC3\xA9" "es"));
    std::map<std::string, std::string> localOf;
    for (const auto& s : readSent(folder))
        if (isFile(s.path)) localOf[s.manifest.id] = s.path;
    if (const Value* vs = r.json.find("versions"); vs && vs->isArray())
        for (const auto& x : vs->items()) {
            if (!x.isObject()) continue;
            PublishedVersion p;
            p.version = x.str("version");
            p.published = static_cast<Seconds>(x.integer("publiee"));
            // Le bloc (signe par une cle de l'editeur) : les notes, << importante >>.
            std::vector<SignedBlock> blocks;
            adm::VersionManifest vm;
            if (licensing::readBlocks(x.str("bloc"), blocks, nullptr) && blocks.size() == 1 && blocks[0].kind == DocKind::Version &&
                std::any_of(caller.editorKeys.begin(), caller.editorKeys.end(), [&](const crypto::PublicKey& k) { return licensing::verifyBlock(blocks[0], k); }) &&
                adm::parseVersion(blocks[0].document, vm, nullptr)) {
                p.important = vm.important;
                p.notes = vm.notes;
            }
            if (const Value* fs_ = x.find("fichiers"); fs_ && fs_->isArray())
                for (const auto& f : fs_->items()) {
                    if (!f.isObject()) continue;
                    VersionFileState s;
                    s.file.id = f.str("id");
                    s.file.name = f.str("nom");
                    s.file.size = static_cast<std::uint64_t>(f.integer("taille"));
                    s.file.sha256 = f.str("sha256");
                    s.file.kind = f.str("sorte");
                    s.file.audience = f.str("pour");
                    s.present = f.flag("present");
                    if (const auto it = localOf.find(s.file.id); it != localOf.end()) s.localPath = it->second;
                    p.files.push_back(std::move(s));
                }
            view.versions.push_back(std::move(p));
        }
    std::sort(view.versions.begin(), view.versions.end(), [](const PublishedVersion& a, const PublishedVersion& b) { return adm::compareSemver(a.version, b.version) > 0; });
    const std::string latest = view.latest();
    if (const Value* l = r.json.find("logiciels"); l && l->isObject()) {
        if (const Value* pv = l->find("par_version"); pv && pv->isObject())
            for (const auto& [v, n] : pv->members()) {
                view.byVersion[v] = static_cast<int>(n.asInt());
                if (!latest.empty() && adm::compareSemver(v, latest) >= 0) view.upToDate += static_cast<int>(n.asInt());
            }
        if (const Value* pl = l->find("par_logiciel"); pl && pl->isObject())
            for (const auto& [soft, m] : pl->members())
                if (m.isObject())
                    for (const auto& [v, n] : m.members()) view.bySoftware[soft][v] = static_cast<int>(n.asInt());
        view.total = static_cast<int>(l->integer("total"));
        std::set<std::string> editors;
        for (const auto& k : caller.editorKeys) editors.insert(crypto::hex(k));
        if (const Value* ad = l->find("admins"); ad && ad->isArray())
            for (const auto& a : ad->items()) {
                if (!a.isObject()) continue;
                SoftwareSeen s;
                s.key = a.str("cle");
                s.software = a.str("logiciel");
                s.version = a.str("version");
                s.seen = static_cast<Seconds>(a.integer("vu"));
                if (editors.count(s.key)) s.name = "admin+";
                else if (caller.admins)
                    if (const adm::Admin* m = caller.admins->byKey(s.key)) s.name = m->name;
                s.upToDate = !s.version.empty() && !latest.empty() && adm::compareSemver(s.version, latest) >= 0;
                view.admins.push_back(std::move(s));
            }
    }
    return true;
}

// Un lien (POST /admin/fichiers/jeton).
bool drawToken(online::Client& c, const std::string& id, int seconds, bool once, std::string& token, Seconds* expires, std::string* why) {
    Value body = Value::object();
    body.set("id", id);
    body.set("duree", std::clamp(seconds, 60, once ? kLinkSeconds : kAttachSeconds));   // 1.44.0 : 48 h une fois, 30 jours sinon
    body.set("une_fois", once);
    const online::Reply r = c.call("POST", "/admin/fichiers/jeton", core::json::write(body, false));
    if (r.status != 201 || !r.error.empty() || !r.json.isObject() || !isLowerHex(r.json.str("jeton"), 32))
        return fail(why, c.explain(r, "tirer un lien vers le fichier"));
    token = r.json.str("jeton");
    if (expires) *expires = static_cast<Seconds>(r.json.integer("expire"));
    return true;
}

// L'installateur de l'Admin a envoyer : celui de la derniere version publiee (encore sur le service), sinon `fallbackPath`
// (depose pour l'occasion, acces << jeton >>).
bool adminInstaller(online::Client& c, const detail::Caller& caller, const std::string& folder, Seconds now, const std::string& fallbackPath,
                    const Progress& progress, adm::MessageFile& out, std::string& whence, std::string* why) {
    VersionsView view;
    std::string w;
    if (readVersions(c, caller, folder, view, &w))
        for (const auto& v : view.versions)
            for (const auto& f : v.files)
                if (f.file.kind == "admin" && f.present) {
                    out = adm::MessageFile{};
                    out.id = f.file.id;
                    out.name = f.file.name;
                    out.size = f.file.size;
                    out.sha256 = f.file.sha256;
                    out.version = v.version;
                    whence = "l'installateur de l'Admin " + v.version + " (Versions)";
                    return true;
                }
    if (trimmed(fallbackPath).empty())
        return fail(why, "aucune version publi\xC3\xA9" "e n'a l'installateur de l'Admin sur le service : choisis le fichier (XPG-Admin-Setup-....exe, l'Admin des admins)");
    std::string vw;
    const std::string version = Workspace::readInstallerVersion(fallbackPath, &vw);
    Uploaded up;
    if (!uploadWith(c, folder, *caller.key, now, fallbackPath, "admin", version, "jeton", progress, 0, 0, up, why)) return false;
    out = adm::MessageFile{};
    out.id = up.manifest.id;
    out.name = up.manifest.name;
    out.size = up.manifest.size;
    out.sha256 = up.manifest.sha256;
    out.version = version;
    whence = up.manifest.name + " (d\xC3\xA9pos\xC3\xA9 pour l'occasion)";
    return true;
}

// ---- la sauvegarde ----
struct DerivedKey {
    unsigned long long                                    ops{0};
    std::size_t                                           mem{0};
    std::array<unsigned char, crypto_pwhash_SALTBYTES>    salt{};
    std::array<unsigned char, 32>                         key{};
    DerivedKey() = default;
    DerivedKey(const DerivedKey&) = delete;
    DerivedKey& operator=(const DerivedKey&) = delete;
    ~DerivedKey() { sodium_memzero(key.data(), key.size()); }
};

bool derive(std::string_view phrase, DerivedKey& k, std::string* why) {
    if (k.ops < kMinOps || k.ops > kMaxOps || k.mem < kMinMem || k.mem > kMaxMem) return fail(why, "param\xC3\xA8tres Argon2id hors des bornes");
    if (crypto_pwhash(k.key.data(), k.key.size(), phrase.data(), phrase.size(), k.salt.data(), k.ops, k.mem, crypto_pwhash_ALG_ARGON2ID13) != 0)
        return fail(why, "pas assez de m\xC3\xA9moire pour d\xC3\xA9river la cl\xC3\xA9 de la phrase (" + std::to_string(k.mem / (1024 * 1024)) + " Mio)");
    return true;
}

bool storeDerived(const std::string& folder, const DerivedKey& k, std::string* why) {
    Value v = Value::object();
    v.set("format", kDerivedFormat);
    v.set("ops", static_cast<std::uint64_t>(k.ops));
    v.set("mem", static_cast<std::uint64_t>(k.mem));
    v.set("sel", crypto::hex(k.salt.data(), k.salt.size()));
    std::string keyHex = crypto::hex(k.key.data(), k.key.size());
    v.set("cle", keyHex);
    sodium_memzero(keyHex.data(), keyHex.size());
    std::string text = core::json::write(v, false);
    if (Value* s = v.find("cle")) {
        std::string& x = const_cast<std::string&>(s->asString());
        sodium_memzero(x.data(), x.size());
    }
    crypto::Bytes plain(text.begin(), text.end()), sealed;
    sodium_memzero(text.data(), text.size());
    const bool ok = crypto::protect(plain, sealed, /*machine=*/false, why);
    crypto::wipe(plain.data(), plain.size());
    if (!ok) return false;
    return crypto::writePrivateFile(at(folder, kBackupKeyFile), sealed, why);
}

bool loadDerived(const std::string& folder, DerivedKey& k, std::string* why) {
    crypto::Bytes sealed, plain;
    if (!isFile(at(folder, kBackupKeyFile)))
        return fail(why, "la sauvegarde n'est pas r\xC3\xA9gl\xC3\xA9" "e sur ce poste (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9 \xE2\x80\xBA Sauvegarde : le dossier et la phrase)");
    if (!crypto::readFile(at(folder, kBackupKeyFile), sealed, why, 4096) || !crypto::unprotect(sealed, plain, why)) return false;
    std::string text(plain.begin(), plain.end());
    crypto::wipe(plain.data(), plain.size());
    Value v;
    const bool parsed = core::json::parse(text, v, nullptr) && v.isObject() && v.str("format") == kDerivedFormat;
    sodium_memzero(text.data(), text.size());
    bool ok = false;
    if (parsed) {
        crypto::Bytes salt, key;
        std::string keyHex = v.str("cle");
        if (Value* s = v.find("cle")) {
            std::string& x = const_cast<std::string&>(s->asString());
            sodium_memzero(x.data(), x.size());
        }
        k.ops = static_cast<unsigned long long>(v.integer("ops"));
        k.mem = static_cast<std::size_t>(v.integer("mem"));
        ok = crypto::unhex(v.str("sel"), salt, k.salt.size()) && salt.size() == k.salt.size() && crypto::unhex(keyHex, key, 32) && key.size() == 32 &&
             k.ops >= kMinOps && k.ops <= kMaxOps && k.mem >= kMinMem && k.mem <= kMaxMem;
        sodium_memzero(keyHex.data(), keyHex.size());
        if (ok) {
            std::copy(salt.begin(), salt.end(), k.salt.begin());
            std::copy(key.begin(), key.end(), k.key.begin());
        }
        if (!key.empty()) crypto::wipe(key.data(), key.size());
    }
    if (!ok) return fail(why, "la cl\xC3\xA9 de la sauvegarde (sauvegarde.cle) est illisible : r\xC3\xA8gle de nouveau la sauvegarde (le dossier et la phrase)");
    return true;
}

Value readSettings(const std::string& folder) {
    std::string text;
    Value v;
    if (!isFile(at(folder, kBackupSettingsFile)) || !readText(at(folder, kBackupSettingsFile), text, nullptr) || !core::json::parse(text, v, nullptr) || !v.isObject())
        return Value::object();
    return v;
}
bool writeSettings(const std::string& folder, Value v, std::string* why) {
    v.set("format", "xpg-sauvegarde-reglage/1");
    return writeText(at(folder, kBackupSettingsFile), core::json::write(v, true) + "\n", why);
}

std::string headerText(const std::string& keyId, Seconds created, const std::string& url, const DerivedKey& k) {
    return std::string(kBackupMarker) + "\ncle=" + keyId + "\ncreee=" + licensing::isoTime(created) + " service=" + url + "\nargon2id ops=" + std::to_string(k.ops) +
           " mem=" + std::to_string(k.mem) + " sel=" + crypto::hex(k.salt.data(), k.salt.size()) + "\n";
}

// Chiffrer `plain` (l'en-tete en donnees associees) : le base64 en lignes de 76.
std::string sealContent(const std::string& header, const DerivedKey& k, const std::string& plain) {
    std::string box(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES, '\0');
    auto* nonce = reinterpret_cast<unsigned char*>(box.data());
    crypto::randomBytes(nonce, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    unsigned long long clen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(nonce + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, &clen, reinterpret_cast<const unsigned char*>(plain.data()),
                                               plain.size(), reinterpret_cast<const unsigned char*>(header.data()), header.size(), nullptr, nonce, k.key.data());
    box.resize(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + static_cast<std::size_t>(clen));
    const std::string b64 = crypto::base64(box.data(), box.size());
    std::string out;
    out.reserve(b64.size() + b64.size() / 76 + 2);
    for (std::size_t i = 0; i < b64.size(); i += 76) {
        out.append(b64, i, 76);
        out += '\n';
    }
    return out;
}

struct BackupFile {
    std::string        header;   // les quatre lignes (les donnees associees)
    std::string        keyId, url, created;
    unsigned long long ops{0};
    std::size_t        mem{0};
    std::array<unsigned char, crypto_pwhash_SALTBYTES> salt{};
    std::string        body;     // le base64
};

bool parseBackup(std::string_view text, BackupFile& b, std::string* why) {
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos < text.size() && lines.size() < 4) {
        const std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) break;
        std::string line(text.substr(pos, end - pos));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        pos = end + 1;
    }
    const std::string notOne = "ce n'est pas une sauvegarde compl\xC3\xA8te de l'Admin (" + std::string(kBackupMarker) + ")";
    if (lines.size() != 4 || lines[0] != kBackupMarker || lines[1].rfind("cle=", 0) != 0 || lines[2].rfind("creee=", 0) != 0 || lines[3].rfind("argon2id ", 0) != 0)
        return fail(why, notOne);
    b.keyId = lines[1].substr(4);
    const std::size_t sp = lines[2].find(" service=");
    if (sp == std::string::npos) return fail(why, notOne);
    b.created = lines[2].substr(6, sp - 6);
    b.url = lines[2].substr(sp + 9);
    unsigned long long ops = 0, mem = 0;
    char saltHex[80] = {};
    if (std::sscanf(lines[3].c_str(), "argon2id ops=%llu mem=%llu sel=%64s", &ops, &mem, saltHex) != 3) return fail(why, "la ligne des param\xC3\xA8tres de la sauvegarde est illisible");
    crypto::Bytes salt;
    if (!crypto::unhex(saltHex, salt, b.salt.size()) || salt.size() != b.salt.size()) return fail(why, "le sel de la sauvegarde est illisible");
    std::copy(salt.begin(), salt.end(), b.salt.begin());
    if (ops < kMinOps || ops > kMaxOps || mem < kMinMem || mem > kMaxMem)
        return fail(why, "les param\xC3\xA8tres de la sauvegarde sont refus\xC3\xA9s (trop faibles ou d\xC3\xA9mesur\xC3\xA9s) : " + lines[3]);
    b.ops = ops;
    b.mem = static_cast<std::size_t>(mem);
    b.header = lines[0] + "\n" + lines[1] + "\n" + lines[2] + "\n" + lines[3] + "\n";
    b.body = std::string(text.substr(pos));
    return true;
}

bool openContent(const BackupFile& b, const DerivedKey& k, std::string& plain, std::string* why) {
    crypto::Bytes box;
    if (!crypto::unbase64(b.body, box) || box.size() < crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + crypto_aead_xchacha20poly1305_ietf_ABYTES)
        return fail(why, "la sauvegarde est ab\xC3\xAEm\xC3\xA9" "e");
    const unsigned char* nonce = box.data();
    const std::size_t clen = box.size() - crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    plain.assign(clen, '\0');
    unsigned long long plen = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(reinterpret_cast<unsigned char*>(plain.data()), &plen, nullptr,
                                                              nonce + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, clen,
                                                              reinterpret_cast<const unsigned char*>(b.header.data()), b.header.size(), nonce, k.key.data());
    if (rc != 0) {
        sodium_memzero(plain.data(), plain.size());
        plain.clear();
        return fail(why, "la phrase de passe est fausse, ou la sauvegarde a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9" "e");
    }
    plain.resize(static_cast<std::size_t>(plen));
    return true;
}

// Les fichiers du dossier qui ne vont pas dans la sauvegarde (lies a CE compte, refaits, ou les sauvegardes elles-memes).
bool excludedFromBackup(const std::string& relative) {
    const std::string name = relative.substr(relative.rfind('/') == std::string::npos ? 0 : relative.rfind('/') + 1);
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".cle") == 0) return true;   // les cles (DPAPI)
    if (name.find(".cle.") != std::string::npos) return true;                             // une cle mise de cote
    if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".code") == 0) return true;   // un code d'activation (DPAPI)
    if (relative.rfind("codes-activation/", 0) == 0) return true;
    if (name.size() >= std::strlen(kBackupExtension) && name.compare(name.size() - std::strlen(kBackupExtension), std::string::npos, kBackupExtension) == 0) return true;
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".tmp") == 0) return true;
    if (relative == mail::kPasswordFile || relative == mail::kMicrosoftFile || relative == kBackupKeyFile || relative == kAdminTokenFile) return true;
    return false;
}

// Un chemin relatif sur (pas de .., pas de racine, pas de lecteur, des / seulement).
bool safeRelative(const std::string& rel) {
    if (rel.empty() || rel.size() > 400 || rel.front() == '/' || rel.find('\\') != std::string::npos || rel.find(':') != std::string::npos) return false;
    std::size_t pos = 0;
    while (pos <= rel.size()) {
        const std::size_t slash = rel.find('/', pos);
        const std::string seg = rel.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        if (seg.empty() || seg == "." || seg == "..") return false;
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }
    return true;
}

// Le dossier du poste : {"<chemin relatif>":"<base64>"} (le JSON), le nombre de fichiers.
bool folderFiles(const std::string& folder, const std::string& backupFolder, std::string& json, int& count, std::string* why) {
    Value files = Value::object();
    count = 0;
    std::uint64_t total = 0;
    std::error_code ec;
    const fs::path root = P(folder);
    fs::path skip;
    if (!backupFolder.empty()) skip = fs::weakly_canonical(P(backupFolder), ec);
    std::vector<std::pair<std::string, fs::path>> list;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_symlink(ec)) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory(ec)) {
            if (!skip.empty() && fs::weakly_canonical(it->path(), ec) == skip) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        std::string rel = U(fs::relative(it->path(), root, ec));
        if (ec) continue;
        for (auto& c : rel)
            if (c == '\\') c = '/';
        if (!safeRelative(rel) || excludedFromBackup(rel)) continue;
        list.emplace_back(rel, it->path());
    }
    std::sort(list.begin(), list.end());
    for (const auto& [rel, p] : list) {
        crypto::Bytes raw;
        if (!crypto::readFile(U(p), raw, why, kMaxFolderBytes)) return false;
        total += raw.size();
        if (total > kMaxFolderBytes) return fail(why, "le dossier de la cl\xC3\xA9 est trop gros pour une sauvegarde (" + sizeText(total) + ")");
        files.set(rel, crypto::base64(raw.data(), raw.size()));
        ++count;
    }
    json = core::json::write(files, false);
    return true;
}

// Les codes d'activation gardes sur ce poste (relus : ils repartent en clair DANS le contenu chiffre).
Value storedCodes(const std::string& folder, int& count) {
    Value codes = Value::object();
    count = 0;
    std::error_code ec;
    const fs::path dir = P(deploy::joinPath(folder, "codes-activation"));
    if (!fs::is_directory(dir, ec)) return codes;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".code") continue;
        const std::string id = U(e.path().stem());
        std::string code;
        if (!online::readCode(folder, id, code, nullptr)) continue;
        codes.set(id, code);
        sodium_memzero(code.data(), code.size());
        ++count;
    }
    return codes;
}

void wipeString(std::string& s) {
    if (!s.empty()) sodium_memzero(s.data(), s.size());
    s.clear();
}

// Les sauvegardes d'un dossier, les plus anciennes d'abord.
std::vector<fs::path> backupsIn(const std::string& folder) {
    std::vector<std::pair<fs::file_time_type, fs::path>> found;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(P(folder), ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string n = U(e.path().filename());
        if (n.rfind(kBackupPrefix, 0) != 0 || n.size() <= std::strlen(kBackupExtension) ||
            n.compare(n.size() - std::strlen(kBackupExtension), std::string::npos, kBackupExtension) != 0)
            continue;
        found.emplace_back(e.last_write_time(ec), e.path());
    }
    std::sort(found.begin(), found.end());
    std::vector<fs::path> out;
    for (auto& f : found) out.push_back(std::move(f.second));
    return out;
}

// GET /admin/sauvegarde : le sommaire (tel quel) et chaque table page par page (les lignes telles quelles).
bool dumpService(online::Client& c, std::string& json, std::uint64_t& rows, int& tables, std::string* why) {
    rows = 0;
    tables = 0;
    const online::Reply m = c.call("GET", "/admin/sauvegarde");
    if (m.status != 200 || !m.error.empty() || !m.json.isObject()) return fail(why, c.explain(m, "lire la sauvegarde du service"));
    std::string out = "{\"manifeste\":" + trimmed(m.body) + ",\"tables\":{";
    bool firstTable = true;
    if (const Value* ts = m.json.find("tables"); ts && ts->isArray())
        for (const auto& t : ts->items()) {
            const std::string name = t.str("nom");
            if (name.empty() || !std::all_of(name.begin(), name.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || ch == '_'; })) continue;
            out += (firstTable ? "\"" : ",\"") + name + "\":[";
            firstTable = false;
            ++tables;
            bool firstRow = true;
            std::string cursor;
            for (int guard = 0; guard < 200000; ++guard) {
                const std::string q = "/admin/sauvegarde?table=" + name + (cursor.empty() ? std::string() : "&depuis=" + cursor);
                const online::Reply p = c.call("GET", q);
                if (p.status != 200 || !p.error.empty() || !p.json.isObject()) return fail(why, c.explain(p, "lire la table " + name + " du service"));
                std::string_view lines;
                std::vector<std::string_view> items;
                if (!rawMember(p.body, "lignes", lines) || !rawItems(lines, items)) return fail(why, "le service a rendu une page illisible de la table " + name);
                for (const auto& it : items) {
                    if (!firstRow) out += ',';
                    out.append(it.data(), it.size());
                    firstRow = false;
                    ++rows;
                }
                const Value* next = p.json.find("suivant");
                if (!next || !next->isString() || next->asString().empty()) break;
                cursor = next->asString();
                if (!std::all_of(cursor.begin(), cursor.end(), [](char ch) {
                        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
                    }))
                    return fail(why, "le service a rendu un curseur illisible pour la table " + name);
            }
            out += ']';
        }
    out += "}}";
    json = std::move(out);
    return true;
}

// Les options d'un client du service ouvert par la cle de l'editeur seule (la restauration : rien d'autre n'est encore lu).
online::Options editorOptions(const licensing::SigningKey& key, Seconds now) {
    online::Options o;
    o.now = now;
    o.tool = std::string("XPG-License-Manager-Admin/") + kAppVersion;
    o.software = std::string("XPG-License-Admin/") + kAppVersion;
    o.editorKeys = {key.publicKey()};
    return o;
}

} // namespace

// ============================================================================================ les fichiers
OpResult Workspace::uploadFile(const std::string& path, const std::string& kind, const std::string& version, const std::string& access, std::string& id,
                               const Progress& progress) const {
    id.clear();
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    Uploaded up;
    if (!uploadWith(*c, folder_, *caller.key, now(), path, kind, version, access, progress, 0, 0, up, &why)) return OpResult::fail(why);
    id = up.manifest.id;
    OpResult r = OpResult::done(up.manifest.name + " d\xC3\xA9pos\xC3\xA9 sur le service (" + sizeText(up.manifest.size) + ", " +
                                plural(up.manifest.chunks(), "morceau", "morceaux") + ").");
    if (up.resumed) r.details.push_back("Envoi repris : " + plural(static_cast<std::uint64_t>(up.already), "morceau \xC3\xA9tait", "morceaux \xC3\xA9taient") + " d\xC3\xA9j\xC3\xA0 l\xC3\xA0, " +
                                        plural(static_cast<std::uint64_t>(up.sent), "envoy\xC3\xA9", "envoy\xC3\xA9s") + ".");
    else if (up.sent == 0) r.details.push_back("D\xC3\xA9j\xC3\xA0 entier sur le service : rien \xC3\xA0 renvoyer.");
    r.details.push_back("Identifiant : " + up.manifest.id + " ; SHA-256 : " + up.manifest.sha256 + ".");
    r.output = up.manifest.id;
    (void)detail::flushJournalWith(folder_, *c);
    return r;
}

OpResult Workspace::fileToken(const std::string& fileId, int seconds, bool once, std::string& token, Seconds* expires) const {
    token.clear();
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    if (!isLowerHex(fileId, 32)) return OpResult::fail("l'identifiant du fichier est illisible (32 hexad\xC3\xA9" "cimaux)");
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    Seconds exp = 0;
    if (!drawToken(*c, fileId, seconds, once, token, &exp, &why)) return OpResult::fail(why);
    if (expires) *expires = exp;
    OpResult r = OpResult::done(std::string("Lien tir\xC3\xA9 : valable jusqu'au ") + dateTimeFr(exp) + (once ? ", pour un seul t\xC3\xA9l\xC3\xA9" "chargement." : "."));
    r.output = c->url() + "/v1/fichiers/lien?jeton=" + token;
    return r;
}

std::string Workspace::fileLink(const std::string& token) const {
    online::Config cfg;
    if (!online::readConfig(folder_, cfg, nullptr) || token.empty()) return {};
    return cfg.url + "/v1/fichiers/lien?jeton=" + token;
}

std::vector<SentFile> Workspace::sentFiles() const {
    std::lock_guard<std::mutex> lock(sentMutex());
    return readSent(folder_);
}

// ============================================================================================ les messages
std::vector<MessagePc> Workspace::pcsForMessages() const { return knownPcs(folder_); }

OpResult Workspace::sendMessageToPc(const std::string& device, const std::string& titleIn, const std::string& text,
                                    const std::vector<adm::MessageFile>& attachments, const std::string& action, int* number) const {
    if (number) *number = 0;
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    const std::string title = trimmed(titleIn);
    if (title.empty() || title.size() > 120) return OpResult::fail("le titre du message (120 caract\xC3\xA8res au plus) : le Studio le montre dans sa notification");
    if (text.size() > 4000) return OpResult::fail("le texte du message est trop long (4 000 caract\xC3\xA8res au plus)");
    if (attachments.size() > 4) return OpResult::fail("4 fichiers joints au plus par message");
    if (!action.empty() && action != "admin") return OpResult::fail("action inconnue : " + action);
    // Le PC : son identifiant (ou sa cle) parmi ceux que connait ce poste.
    MessagePc pc;
    const std::string wanted = trimmed(device);
    crypto::PublicKey pk{};
    if (keyFromHex(wanted, pk)) {
        pc.deviceKey = wanted;
        pc.deviceId = svc::deviceId(pk);
    }
    for (const auto& p : knownPcs(folder_))
        if (p.deviceId == wanted || p.deviceKey == wanted) pc = p;
    if (pc.deviceKey.empty() || !keyFromHex(pc.deviceKey, pk))
        return OpResult::fail("ce PC n'est pas connu de ce poste (" + clip(wanted, 16) + ") : il faut sa cl\xC3\xA9 (une demande, une activation)");
    adm::MessageContent content;
    content.text = text;
    content.files = attachments;
    content.action = action;
    std::string plain = adm::writeMessageContent(content);
    adm::Message m;
    m.device = pc.deviceId;
    m.title = title;
    m.from = clip(caller.key->name().empty() ? std::string("L'\xC3\xA9" "diteur") : caller.key->name(), 60);
    m.issued = now();
    m.sealed = gw::sealFor(pk, plain);
    wipeString(plain);
    if (m.sealed.empty()) return OpResult::fail("impossible de sceller le message pour la cl\xC3\xA9 de ce PC");
    if (m.sealed.size() > 60000) return OpResult::fail("le message est trop gros une fois scell\xC3\xA9 (texte et fichiers joints)");
    const std::string doc = adm::writeMessage(m);
    if (doc.size() > licensing::docKindMaxBytes(DocKind::Message)) return OpResult::fail("le message est trop gros (64 Kio au plus)");
    const std::string block = licensing::writeBlock(licensing::signBlock(DocKind::Message, doc, caller.key->publicKey(), signerOf(*caller.key)));
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    Value body = Value::object();
    body.set("poste", pc.deviceId);
    body.set("bloc", block);
    const online::Reply r = c->call("PUT", "/admin/messages", core::json::write(body, false));
    if (r.status != 201 || !r.error.empty() || !r.json.isObject()) return OpResult::fail(c->explain(r, "d\xC3\xA9poser le message pour ce PC"));
    const int n = static_cast<int>(r.json.integer("numero"));
    if (number) *number = n;
    const std::string who = pc.name.empty() ? "au PC " + pc.deviceId.substr(0, 8) : "\xC3\xA0 " + pc.name;   // << au PC 1a2b... >>, << a Poste A >>
    OpResult res = OpResult::done("Message envoy\xC3\xA9 " + who + (pc.customer.empty() ? std::string() : " (" + pc.customer + ")") + " : " + cite(title) +
                                  " - il le verra dans les notifications de son Studio.");
    if (!attachments.empty()) res.details.push_back(plural(attachments.size(), "fichier joint", "fichiers joints") + " (lien de 48 h).");
    res.details.push_back("Scell\xC3\xA9 pour la cl\xC3\xA9 de ce PC seulement : aucun autre PC ne peut le lire.");
    Value d = Value::object();
    d.set("poste", pc.deviceId);
    if (!pc.customer.empty()) d.set("client", pc.customer);
    d.set("titre", clip(title, 120));
    d.set("fichiers", static_cast<int>(attachments.size()));
    if (!action.empty()) d.set("action", action);
    (void)detail::queueJournal(folder_, *caller.key, now(), "fichier_envoyer", pc.deviceId,
                               clip("a envoy\xC3\xA9 un message " + who + (attachments.empty() ? std::string() : " avec " + plural(attachments.size(), "fichier", "fichiers")), 400),
                               core::json::write(d, false), {}, {}, nullptr);
    (void)detail::flushJournalWith(folder_, *c);
    return res;
}

OpResult Workspace::attachFile(const std::string& path, adm::MessageFile& out, const Progress& progress) const {
    out = adm::MessageFile{};
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    std::string vw;
    const std::string version = readInstallerVersion(path, &vw);   // un installateur : sa version (rien sinon)
    Uploaded up;
    if (!uploadWith(*c, folder_, *caller.key, now(), path, "autre", version, "jeton", progress, 0, 0, up, &why)) return OpResult::fail(why);
    std::string token;
    if (!drawToken(*c, up.manifest.id, kAttachSeconds, false, token, nullptr, &why)) return OpResult::fail(why);
    out.id = up.manifest.id;
    out.name = up.manifest.name;
    out.size = up.manifest.size;
    out.sha256 = up.manifest.sha256;
    out.version = version;
    out.token = token;
    OpResult r = OpResult::done(out.name + " pr\xC3\xAAt \xC3\xA0 joindre (" + sizeText(out.size) + ", lien de 30 jours).");
    r.output = out.id;
    return r;
}

// 1.49.0 (docs/ADMINS.md, par. 12) : la phrase du code d'invitation, dans le message qui part avec l'installateur.
static std::string invitationSentence(const std::string& code) {
    return "Ton code d'invitation : " + code + " (Ã  saisir dans l'installateur, valable 48 h)";
}

OpResult Workspace::sendAdminInstaller(const std::string& device, const std::string& adminName, const std::string& fallbackPath, const Progress& progress,
                                       const std::string& invitationCode) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    adm::MessageFile f;
    std::string whence;
    {
        std::unique_ptr<online::Client> c;
        if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
        if (!adminInstaller(*c, caller, folder_, now(), fallbackPath, progress, f, whence, &why)) return OpResult::fail(why);
        if (!drawToken(*c, f.id, kAttachSeconds, false, f.token, nullptr, &why)) return OpResult::fail(why);
    }
    const std::string name = trimmed(adminName);
    const std::string code = adm::normalizeInvitationCode(invitationCode);
    // 1.49.0 : avec un code d'invitation, l'installateur XPG Admin le demande ; l'Admin montre ensuite 6 chiffres a dire.
    const std::string text = (name.empty() ? std::string("Bonjour,") : "Bonjour " + clip(name, 60) + ",") +
                             (code.empty() ? std::string("\n\nL'admin+ t'invite \xC3\xA0 devenir admin des licences. Installe XPG License Manager Admin (le fichier joint), "
                                                         "puis choisis \xC2\xAB Demander \xC3\xA0 devenir admin \xC2\xBB et lis ton empreinte \xC3\xA0 l'admin+ au t\xC3\xA9l\xC3\xA9phone.\n\n")
                                           : "\n\nL'admin+ t'invite \xC3\xA0 devenir admin des licences. Installe XPG Admin (le fichier joint).\n\n" +
                                                 invitationSentence(code) +
                                                 ".\n\nAu premier lancement, l'Admin te montre un code de 6 chiffres : dis-le \xC3\xA0 l'admin+ au t\xC3\xA9l\xC3\xA9phone.\n\n") +
                             "Le lien du fichier vaut 48 heures.";
    OpResult r = sendMessageToPc(device, code.empty() ? "Installer XPG License Manager Admin" : "Installer XPG Admin", text, {f}, "admin");
    if (r.ok) r.details.insert(r.details.begin(), "Fichier : " + whence + ".");
    if (r.ok && !code.empty()) r.details.push_back(invitationSentence(code) + " : dans le message.");
    return r;
}

OpResult Workspace::adminInstallerLink(const std::string& fallbackPath, std::string& url, const Progress& progress, const std::string& invitationCode) const {
    url.clear();
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    adm::MessageFile f;
    std::string whence, token;
    Seconds exp = 0;
    if (!adminInstaller(*c, caller, folder_, now(), fallbackPath, progress, f, whence, &why)) return OpResult::fail(why);
    if (!drawToken(*c, f.id, kLinkSeconds, true, token, &exp, &why)) return OpResult::fail(why);
    url = c->url() + "/v1/fichiers/lien?jeton=" + token;
    OpResult r = OpResult::done("Lien vers " + f.name + " : valable jusqu'au " + dateTimeFr(exp) + ", pour un seul t\xC3\xA9l\xC3\xA9" "chargement.");
    r.details.push_back("Fichier : " + whence + ".");
    r.details.push_back("Envoie-le au futur admin (un courriel) : il l'ouvre dans son navigateur.");
    if (const std::string code = adm::normalizeInvitationCode(invitationCode); !code.empty()) r.details.push_back(invitationSentence(code) + ".");
    // 1.47.0 (docs/ADMINS.md, par. 11) : la connexion au service (le certificat verifie, signe par la cle de l'editeur ; rien
    // de secret), un second lien : enregistree a cote de l'installateur, l'Admin installe demarre connecte a ce service.
    const std::string conn = deploy::joinPath(folder_, connexion::kFileName);
    if (!connexion::exportText(folder_).empty()) {
        Uploaded cu;
        std::string ctoken, cw;
        Seconds cexp = 0;
        if (uploadWith(*c, folder_, *caller.key, now(), conn, "autre", "", "jeton", {}, 0, 0, cu, &cw) &&
            drawToken(*c, cu.manifest.id, kLinkSeconds, false, ctoken, &cexp, &cw)) {
            r.details.push_back("La connexion au service (" + std::string(connexion::kFileName) + ", sign\xC3\xA9" "e par ta cl\xC3\xA9) : " + c->url() +
                                "/v1/fichiers/lien?jeton=" + ctoken + " - \xC3\xA0 enregistrer dans le m\xC3\xAA" "me dossier que l'installateur.");
        } else {
            r.details.push_back("La connexion au service n'est pas partie (" + cw + ") : Service en ligne \xE2\x80\xBA Exporter la connexion\xE2\x80\xA6");
        }
    } else {
        r.details.push_back("Pas de connexion au service \xC3\xA0 joindre : V\xC3\xA9rifier (Service en ligne) la garde ; l'Admin du futur admin la "
                            "prendra sinon du Studio de son PC, ou de l'installateur publi\xC3\xA9.");
    }
    r.output = url;
    (void)detail::flushJournalWith(folder_, *c);
    return r;
}

// ============================================================================================ les versions
// ============================================================================ 1.46.1 : un installateur dans un .zip
namespace {

constexpr std::uint64_t kMaxZipEntry = 1024ull * 1024ull * 1024ull;   // 1 Gio : un installateur fait moins de 200 Mio
constexpr std::uint64_t kMaxZipRatio = 1000;                          // plus : une bombe de decompression

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// Le nom d'un installateur attendu (le dernier element du chemin) : 1 Studios, 2 Admin, 0 autre.
int installerKind(const std::string& entryName) {
    std::string n = entryName;
    if (const auto k = n.find_last_of("/\\"); k != std::string::npos) n = n.substr(k + 1);
    const std::string l = lowerAscii(n);
    const auto ends = [&](const char* x) { const std::string e = x; return l.size() > e.size() && l.compare(l.size() - e.size(), e.size(), e) == 0; };
    if (!ends(".exe")) return 0;
    if (l.rfind("api-ihm-studio-setup-", 0) == 0) return 1;
    if (l.rfind("xpg-license-admin-setup-", 0) == 0) return 2;
    return 0;
}
// Un nom d'entree sur : relatif, sans << .. >>, sans lettre de lecteur ni caractere de controle.
bool safeEntryName(const std::string& n) {
    if (n.empty() || n.size() > 400 || n[0] == '/' || n[0] == '\\' || n.find(':') != std::string::npos) return false;
    for (unsigned char c : n)
        if (c < 0x20 || c == 0x7F) return false;
    std::size_t a = 0;
    while (a <= n.size()) {
        std::size_t b = n.find_first_of("/\\", a);
        if (b == std::string::npos) b = n.size();
        const std::string part = n.substr(a, b - a);
        if (part == "..") return false;
        a = b + 1;
    }
    return true;
}

struct ZipIn {
    std::ifstream in;
    std::uint64_t size{0};
};
size_t zipInRead(void* opaque, mz_uint64 ofs, void* buf, size_t n) {
    auto* z = static_cast<ZipIn*>(opaque);
    if (ofs >= z->size) return 0;
    z->in.clear();
    z->in.seekg(static_cast<std::streamoff>(ofs));
    z->in.read(static_cast<char*>(buf), static_cast<std::streamsize>(std::min<std::uint64_t>(n, z->size - ofs)));
    return static_cast<size_t>(z->in.gcount());
}
struct ExeOut {
    std::ofstream out;
    std::uint64_t written{0};
    bool          failed{false};
};
size_t exeOutWrite(void* opaque, mz_uint64, const void* buf, size_t n) {
    auto* o = static_cast<ExeOut*>(opaque);
    o->out.write(static_cast<const char*>(buf), static_cast<std::streamsize>(n));
    if (!o->out || o->written + n > kMaxZipEntry) {
        o->failed = true;
        return 0;
    }
    o->written += n;
    return n;
}

// Le zip lu : chaque installateur attendu (leur index), ou pourquoi il est refuse.
struct ZipLook {
    std::vector<mz_uint> studios, admin;
};
bool lookZip(mz_zip_archive& z, ZipIn& src, const std::string& zipPath, ZipLook& out, std::string* why) {
    const std::string name = U(P(zipPath).filename());
    std::error_code ec;
    src.size = fs::file_size(P(zipPath), ec);
    if (ec) return fail(why, name + " ne se lit pas");
    src.in.open(P(zipPath), std::ios::binary);
    if (!src.in) return fail(why, name + " ne s'ouvre pas");
    z.m_pRead = zipInRead;
    z.m_pIO_opaque = &src;
    if (!mz_zip_reader_init(&z, src.size, 0)) return fail(why, name + " n'est pas un zip lisible");
    const mz_uint n = mz_zip_reader_get_num_files(&z);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) return fail(why, name + " : une entr\xC3\xA9" "e illisible");
        if (st.m_is_directory) continue;
        const int kind = installerKind(st.m_filename);
        if (!kind) continue;
        if (!safeEntryName(st.m_filename)) return fail(why, name + " : " + cite(st.m_filename) + " sort du dossier (un chemin refus\xC3\xA9)");
        if (st.m_is_encrypted || (st.m_bit_flag & 1u)) return fail(why, name + " : " + cite(st.m_filename) + " est chiffr\xC3\xA9 (un zip sans mot de passe, s'il te pla\xC3\xAEt)");
        if (!st.m_is_supported) return fail(why, name + " : " + cite(st.m_filename) + " est compress\xC3\xA9 d'une fa\xC3\xA7on que l'Admin ne lit pas");
        if (st.m_uncomp_size == 0 || st.m_uncomp_size > kMaxZipEntry || st.m_uncomp_size > kMaxZipRatio * std::max<mz_uint64>(1, st.m_comp_size))
            return fail(why, name + " : " + cite(st.m_filename) + " a une taille absurde (" + std::to_string(st.m_uncomp_size) + " octets)");
        (kind == 2 ? out.admin : out.studios).push_back(i);
    }
    if (out.studios.size() > 1) return fail(why, name + " porte plusieurs installateurs des Studios : garde-n'en qu'un");
    if (out.admin.size() > 1) return fail(why, name + " porte plusieurs installateurs de l'Admin : garde-n'en qu'un");
    return true;
}

} // namespace

bool Workspace::isZipPath(const std::string& path) {
    const std::string l = lowerAscii(path);
    return l.size() > 4 && l.compare(l.size() - 4, 4, ".zip") == 0;
}

bool Workspace::installerKindsInZip(const std::string& zipPath, bool& studios, bool& admin, std::string* why) {
    studios = admin = false;
    mz_zip_archive z{};
    ZipIn src;
    ZipLook look;
    const bool ok = lookZip(z, src, zipPath, look, why);
    mz_zip_reader_end(&z);
    if (!ok) return false;
    studios = !look.studios.empty();
    admin = !look.admin.empty();
    if (!studios && !admin)
        return fail(why, U(P(zipPath).filename()) + " ne porte ni API-IHM-Studio-Setup-\xE2\x80\xA6.exe ni XPG-License-Admin-Setup-\xE2\x80\xA6.exe");
    return true;
}

bool Workspace::installerFromZip(const std::string& zipPath, bool admin, std::string& out, std::string* why) {
    out.clear();
    mz_zip_archive z{};
    ZipIn src;
    ZipLook look;
    if (!lookZip(z, src, zipPath, look, why)) {
        mz_zip_reader_end(&z);
        return false;
    }
    const std::vector<mz_uint>& want = admin ? look.admin : look.studios;
    if (want.empty()) {
        mz_zip_reader_end(&z);
        return fail(why, U(P(zipPath).filename()) + " ne porte pas " + (admin ? "XPG-License-Admin-Setup-\xE2\x80\xA6.exe (l'installateur de l'Admin)" : "API-IHM-Studio-Setup-\xE2\x80\xA6.exe (l'installateur des Studios)"));
    }
    mz_zip_archive_file_stat st;
    (void)mz_zip_reader_file_stat(&z, want.front(), &st);
    std::string base = st.m_filename;
    if (const auto k = base.find_last_of("/\\"); k != std::string::npos) base = base.substr(k + 1);
    // Un dossier temporaire a lui (le nom du fichier garde : la version se lit dans le fichier, le nom se montre) ; ceux
    // d'avant hier s'en vont.
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec);
    if (ec) dir = P(zipPath).parent_path();
    else {
        std::vector<fs::path> old;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const auto leaf = it->path().filename().u8string();
            std::error_code e2;
            if (std::string(leaf.begin(), leaf.end()).rfind("xpg-versions-zip-", 0) == 0 &&
                fs::last_write_time(it->path(), e2) < fs::file_time_type::clock::now() - std::chrono::hours(24) && !e2)
                old.push_back(it->path());
        }
        for (const auto& d : old) fs::remove_all(d, ec);
    }
    ec.clear();
    unsigned char rnd[8];
    randombytes_buf(rnd, sizeof rnd);
    dir /= P("xpg-versions-zip-" + crypto::hex(rnd, sizeof rnd));
    fs::create_directories(dir, ec);
    if (ec) {
        mz_zip_reader_end(&z);
        return fail(why, "le dossier temporaire ne se cr\xC3\xA9" "e pas (" + ec.message() + ")");
    }
    const fs::path target = dir / P(base);
    ExeOut o;
    o.out.open(target, std::ios::binary | std::ios::trunc);
    bool ok = o.out && mz_zip_reader_extract_to_callback(&z, want.front(), exeOutWrite, &o, 0) && !o.failed;
    o.out.close();
    mz_zip_reader_end(&z);
    if (!ok || o.written != st.m_uncomp_size) {
        fs::remove_all(dir, ec);
        return fail(why, U(P(zipPath).filename()) + " : " + cite(base) + " ne s'extrait pas (zip ab\xC3\xAEm\xC3\xA9, ou plus de place sur le disque)");
    }
    out = U(target);
    return true;
}

std::string Workspace::readInstallerVersion(const std::string& path, std::string* why) {
    PeVersion pv;
    std::string w;
    if (!readPeVersion(path, pv, &w)) {
        if (why) *why = U(P(path).filename()) + " : " + w;
        return {};
    }
    for (const std::string& s : {pv.product, pv.file})
        if (std::string v = semverOf(s); !v.empty()) return v;
    for (const auto& f : {pv.fixedProduct, pv.fixedFile})
        if (pv.fixed && f[3] == 0 && (f[0] || f[1] || f[2])) return std::to_string(f[0]) + "." + std::to_string(f[1]) + "." + std::to_string(f[2]);
    if (why)
        *why = U(P(path).filename()) + " : sa ressource de version ne dit pas une version X.Y.Z (ProductVersion " + cite(pv.product) + ", FileVersion " + cite(pv.file) + ")";
    return {};
}

OpResult Workspace::versions(VersionsView& out) const {
    out = VersionsView{};
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    if (!readVersions(*c, caller, folder_, out, &why)) return OpResult::fail(why);
    (void)detail::flushJournalWith(folder_, *c);
    if (out.versions.empty()) return OpResult::done("Aucune version publi\xC3\xA9" "e sur le service.");
    OpResult r = OpResult::done("Derni\xC3\xA8re version publi\xC3\xA9" "e : " + out.latest() + " (" + plural(out.versions.size(), "version", "versions") + ").");
    r.details.push_back(std::to_string(out.upToDate) + " sur " + std::to_string(out.total) + " \xC3\xA0 jour (Studios et Admins vus par le service).");
    return r;
}

OpResult Workspace::publishVersion(const std::string& studiosExe, const std::string& adminExe, const std::vector<std::string>& notesIn, bool important,
                                   const Progress& progress) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    std::string w1, w2;
    const std::string v1 = readInstallerVersion(studiosExe, &w1), v2 = readInstallerVersion(adminExe, &w2);
    if (v1.empty()) return OpResult::fail("l'installateur des Studios : " + w1);
    if (v2.empty()) return OpResult::fail("l'installateur de l'Admin : " + w2);
    if (v1 != v2) return OpResult::fail("les deux installateurs ne sont pas de la m\xC3\xAAme version (Studios " + v1 + ", Admin " + v2 + ")");
    std::vector<std::string> notes;
    for (const auto& n : notesIn) {
        std::string t = trimmed(n);
        if (!t.empty() && notes.size() < 40) notes.push_back(clip(t, 400));
    }
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    VersionsView view;
    if (!readVersions(*c, caller, folder_, view, &why)) return OpResult::fail(why);
    if (!view.latest().empty() && adm::compareSemver(v1, view.latest()) <= 0)
        return OpResult::fail("la version " + v1 + " n'est pas plus r\xC3\xA9" "cente que la derni\xC3\xA8re publi\xC3\xA9" "e (" + view.latest() + ")");
    std::error_code ec;
    const std::uint64_t s1 = fs::file_size(P(studiosExe), ec), s2 = fs::file_size(P(adminExe), ec);
    Uploaded u1, u2;
    if (!uploadWith(*c, folder_, *caller.key, now(), studiosExe, "studios", v1, "public", progress, 0, s1 + s2, u1, &why)) return OpResult::fail(why);
    if (!uploadWith(*c, folder_, *caller.key, now(), adminExe, "admin", v1, "admins", progress, s1, s1 + s2, u2, &why)) return OpResult::fail(why);
    adm::VersionManifest v;
    v.version = v1;
    v.published = now();
    v.important = important;
    v.notes = notes;
    const auto fileOf = [](const adm::FileManifest& m, const char* audience) {
        adm::VersionFile f;
        f.id = m.id;
        f.name = m.name;
        f.size = m.size;
        f.sha256 = m.sha256;
        f.kind = m.kind;
        f.audience = audience;
        return f;
    };
    v.files = {fileOf(u1.manifest, "tous"), fileOf(u2.manifest, "admins")};
    const std::string doc = adm::writeVersion(v);
    if (doc.size() > licensing::docKindMaxBytes(DocKind::Version)) return OpResult::fail("les notes de version sont trop longues (64 Kio au plus en tout)");
    Value body = Value::object();
    body.set("bloc", licensing::writeBlock(licensing::signBlock(DocKind::Version, doc, caller.key->publicKey(), signerOf(*caller.key))));
    const online::Reply r = c->call("PUT", "/admin/versions", core::json::write(body, false));
    if (r.error.empty() && r.status == 409 && r.code() == "ancien")
        return OpResult::fail("le service a d\xC3\xA9j\xC3\xA0 une version aussi r\xC3\xA9" "cente (" + r.json.str("version") + ")");
    if ((r.status != 201 && r.status != 200) || !r.error.empty()) return OpResult::fail(c->explain(r, "publier la version " + v1));
    OpResult res = OpResult::done("Version " + v1 + " publi\xC3\xA9" "e" + (important ? " (importante)" : "") + " : les Studios la proposent \xC3\xA0 leur prochain regard.");
    res.details.push_back("Studios : " + u1.manifest.name + " (" + sizeText(u1.manifest.size) + ", pour tous).");
    res.details.push_back("Admin : " + u2.manifest.name + " (" + sizeText(u2.manifest.size) + ", pour les admins).");
    res.details.push_back(notes.empty() ? std::string("Sans notes de version.") : plural(notes.size(), "note de version", "notes de version") + ".");
    Value d = Value::object();
    d.set("version", v1);
    d.set("importante", important);
    d.set("studios", u1.manifest.name);
    d.set("admin", u2.manifest.name);
    Value before = Value::object(), after = Value::object();
    if (!view.latest().empty()) before.set("version", view.latest());
    after.set("version", v1);
    (void)detail::queueJournal(folder_, *caller.key, now(), "version_publier", v1, "a publi\xC3\xA9 la version " + v1, core::json::write(d, false),
                               core::json::write(before, false), core::json::write(after, false), nullptr);
    (void)detail::flushJournalWith(folder_, *c);
    return res;
}

std::vector<std::string> Workspace::suggestedNotes(const std::string&) const {
    // Les notes de version des Studios (help/ReleaseNotes) ne sont pas dans l'Admin : l'admin+ les tape (ou les colle).
    return {};
}

OpResult Workspace::filesToResend(std::vector<FileToResend>& out) const {
    out.clear();
    VersionsView view;
    OpResult r = versions(view);
    if (!r.ok) return r;
    for (const auto& v : view.versions)
        for (const auto& f : v.files) {
            if (f.present) continue;
            FileToResend x;
            x.file = f.file;
            x.version = v.version;
            std::error_code ec;
            if (!f.localPath.empty() && fs::file_size(P(f.localPath), ec) == f.file.size && !ec) x.localPath = f.localPath;
            out.push_back(std::move(x));
        }
    return OpResult::done(out.empty() ? std::string("Le service a tous les fichiers des versions publi\xC3\xA9" "es.")
                                      : plural(out.size(), "fichier \xC3\xA0 renvoyer", "fichiers \xC3\xA0 renvoyer") + " (le service n'a plus leurs octets).");
}

OpResult Workspace::resendFile(const std::string& fileId, const std::string& path, const Progress& progress) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    if (!isLowerHex(fileId, 32)) return OpResult::fail("l'identifiant du fichier est illisible (32 hexad\xC3\xA9" "cimaux)");
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    // Son manifeste : celui de ce poste, sinon celui que le service garde (la table << fichiers >>).
    SentFile rec;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(sentMutex());
        for (const auto& x : readSent(folder_))
            if (x.manifest.id == fileId) {
                rec = x;
                found = true;
            }
    }
    if (!found) {
        std::string cursor;
        for (int guard = 0; guard < 10000 && !found; ++guard) {
            const online::Reply p = c->call("GET", "/admin/sauvegarde?table=fichiers" + (cursor.empty() ? std::string() : "&depuis=" + cursor));
            if (p.status != 200 || !p.error.empty() || !p.json.isObject()) return OpResult::fail(c->explain(p, "lire le manifeste du fichier"));
            if (const Value* rows = p.json.find("lignes"); rows && rows->isArray())
                for (const auto& row : rows->items())
                    if (row.isObject() && row.str("id") == fileId) {
                        rec.block = row.str("manifeste");
                        std::vector<SignedBlock> blocks;
                        found = licensing::readBlocks(rec.block, blocks, nullptr) && blocks.size() == 1 && blocks[0].kind == DocKind::FileManifest &&
                                adm::parseFileManifest(blocks[0].document, rec.manifest, nullptr);
                    }
            const Value* next = p.json.find("suivant");
            if (!next || !next->isString() || next->asString().empty()) break;
            cursor = next->asString();
        }
    }
    if (!found) return OpResult::fail("le manifeste de ce fichier n'est ni sur ce poste ni sur le service");
    std::string sha;
    std::uint64_t size = 0;
    if (!isFile(path)) return OpResult::fail("le fichier " + path + " n'existe pas");
    if (!crypto::sha256File(path, sha, &size, &why)) return OpResult::fail(why);
    if (sha != rec.manifest.sha256 || size != rec.manifest.size)
        return OpResult::fail(U(P(path).filename()) + " n'est pas " + rec.manifest.name + " (sa taille ou son SHA-256 diff\xC3\xA8rent) : choisis le bon fichier");
    rec.path = path;
    rec.complete = false;
    if (rec.sentAt == 0) rec.sentAt = now();
    noteSent(folder_, rec);
    Uploaded up;
    if (!pushFile(*c, rec.block, rec.manifest, path, progress, 0, 0, up, &why)) return OpResult::fail(why);
    rec.complete = true;
    noteSent(folder_, rec);
    (void)detail::flushJournalWith(folder_, *c);
    return OpResult::done(rec.manifest.name + " renvoy\xC3\xA9 : le service l'a de nouveau entier (" + plural(static_cast<std::uint64_t>(up.sent), "morceau", "morceaux") + ").");
}

// ============================================================================================ les adresses de secours
std::vector<std::string> Workspace::fallbackServices() const { return hasRegistry_ ? registry_.fallbackServices : std::vector<std::string>{}; }

OpResult Workspace::setFallbackServices(const std::vector<std::string>& urlsIn) {
    if (!hasRegistry_) return OpResult::fail("pas encore de registre sign\xC3\xA9 : les adresses de secours en font partie");
    if (urlsIn.size() > 3) return OpResult::fail("3 adresses de secours au plus");
    std::vector<std::string> urls;
    for (const auto& u : urlsIn) {
        const std::string t = trimmed(u);
        if (t.empty()) continue;
        const std::string n = online::normalizeUrl(t);
        if (n.empty() || !svc::validUrl(n)) return OpResult::fail(cite(t) + " n'est pas une adresse de service valable (https://h\xC3\xB4te[:port], sans chemin)");
        if (service_.configured && svc::sameUrl(n, service_.config.url)) return OpResult::fail(cite(t) + " est l'adresse principale du service");
        for (const auto& prev : urls)
            if (svc::sameUrl(prev, n)) return OpResult::fail(cite(t) + " est deux fois");
        urls.push_back(n);
    }
    if (urls == registry_.fallbackServices) return OpResult::done("Les adresses de secours ne changent pas.");
    licensing::Registry edited = registry_;
    edited.fallbackServices = urls;
    OpResult r = signRegistry(edited);
    if (r.ok)
        r.details.insert(r.details.begin(), urls.empty() ? std::string("Plus d'adresse de secours : les Studios n'essaient que l'adresse principale.")
                                                         : "Adresses de secours (dans l'ordre) : " + [&] {
                                                               std::string s;
                                                               for (const auto& u : urls) s += (s.empty() ? "" : ", ") + u;
                                                               return s;
                                                           }() + " ; chaque service de secours se certifie comme le principal.");
    return r;
}

// ============================================================================================ la sauvegarde
BackupSettings Workspace::backupSettings() const {
    BackupSettings s;
    const Value v = readSettings(folder_);
    s.folder = v.str("dossier");
    s.last = static_cast<Seconds>(v.integer("derniere"));
    s.lastPath = v.str("dernier_fichier");
    const bool key = isFile(at(folder_, kBackupKeyFile));
    s.configured = !s.folder.empty() && key;
    if (s.folder.empty()) s.problem = "pas encore r\xC3\xA9gl\xC3\xA9" "e (le dossier et la phrase)";
    else if (!key) s.problem = "la cl\xC3\xA9 de la sauvegarde n'est pas sur ce poste : r\xC3\xA8gle de nouveau la phrase";
    return s;
}

OpResult Workspace::configureBackup(const std::string& folderIn, const std::string& passphrase, const KeyBackupOptions& strength) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    std::string dir = trimmed(folderIn);
    if (dir.empty()) return OpResult::fail("dans quel dossier ranger les sauvegardes ?");
    {   // un chemin complet (la console peut en donner un relatif)
        std::error_code aec;
        const fs::path abs = fs::absolute(P(dir), aec);
        if (!aec) dir = U(abs.lexically_normal());
    }
    if (!acceptablePassphrase(passphrase, &why)) return OpResult::fail(why);
    std::error_code ec;
    fs::create_directories(P(dir), ec);
    if (!fs::is_directory(P(dir), ec)) return OpResult::fail("le dossier " + dir + " ne peut pas \xC3\xAAtre cr\xC3\xA9\xC3\xA9");
    {   // on peut y ecrire ?
        const std::string probe = deploy::joinPath(dir, ".xpg-essai-ecriture");
        if (!writeText(probe, "x", &why)) return OpResult::fail("impossible d'\xC3\xA9" "crire dans " + dir + " (" + why + ")");
        fs::remove(P(probe), ec);
    }
    if (!crypto::init()) return OpResult::fail("libsodium ne d\xC3\xA9marre pas");
    DerivedKey k;
    k.ops = strength.opsLimit ? strength.opsLimit : crypto_pwhash_OPSLIMIT_MODERATE;
    k.mem = strength.memLimit ? strength.memLimit : crypto_pwhash_MEMLIMIT_MODERATE;
    crypto::randomBytes(k.salt.data(), k.salt.size());
    if (!derive(passphrase, k, &why)) return OpResult::fail(why);
    if (!storeDerived(folder_, k, &why)) return OpResult::fail(why);
    Value v = readSettings(folder_);
    v.set("dossier", dir);
    v.set("reglee", static_cast<std::int64_t>(now()));
    if (!writeSettings(folder_, std::move(v), &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Sauvegarde r\xC3\xA9gl\xC3\xA9" "e : chaque jour dans " + dir + " (les 30 derni\xC3\xA8res gard\xC3\xA9" "es).");
    r.details.push_back("La phrase n'est gard\xC3\xA9" "e nulle part : note-la hors de ce PC. Sans elle, une sauvegarde ne se relit pas.");
    r.details.push_back("La cl\xC3\xA9 qui en est tir\xC3\xA9" "e reste sur ce poste, prot\xC3\xA9g\xC3\xA9" "e pour ce compte" + std::string(crypto::protectedHere() ? " Windows (DPAPI)." : "."));
    return r;
}

bool Workspace::backupDueToday() const {
    if (role_ != Role::Plus) return false;
    const BackupSettings s = backupSettings();
    return s.configured && localDay(s.last) != localDay(now());
}

OpResult Workspace::backupNow(std::string* outPath) const {
    if (outPath) outPath->clear();
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    if (!crypto::init()) return OpResult::fail("libsodium ne d\xC3\xA9marre pas");
    Value settings = readSettings(folder_);
    const std::string dir = settings.str("dossier");
    if (dir.empty()) return OpResult::fail("la sauvegarde n'est pas r\xC3\xA9gl\xC3\xA9" "e : choisis son dossier et sa phrase");
    DerivedKey k;
    if (!loadDerived(folder_, k, &why)) return OpResult::fail(why);
    const Seconds t = now();
    OpResult r = OpResult::done("");
    // 1. le service entier
    std::string serviceJson = "null", url;
    std::uint64_t rows = 0;
    int tables = 0;
    std::unique_ptr<online::Client> c;
    if (online::configured(folder_)) {
        if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, t), c, &why))
            return OpResult::fail("le service ne r\xC3\xA9pond pas : " + why + " (les sauvegardes d'avant restent dans " + dir + ")");
        url = c->url();
        if (!dumpService(*c, serviceJson, rows, tables, &why)) return OpResult::fail(why);
    } else {
        r.details.push_back("Pas de service r\xC3\xA9gl\xC3\xA9 : seulement le dossier du poste et la cl\xC3\xA9.");
    }
    // 2. le dossier du poste, la cle de l'editeur, la cle de synchro, les codes
    std::string folderJson, keyJson, syncCode = "null";
    int files = 0, codes = 0;
    bool withSync = false;
    if (!folderFiles(folder_, dir, folderJson, files, &why)) return OpResult::fail(why);
    if (!keyBackupPayload(*caller.key, keyJson, &why)) return OpResult::fail(why);
    if (archive::hasSyncKey(folder_)) {
        archive::SyncKey sk{};
        if (archive::loadSyncKey(folder_, sk, &why)) {
            std::string code = archive::recoveryCode(sk);
            syncCode = core::json::write(Value(code), false);
            wipeString(code);
            withSync = true;
        } else {
            r.details.push_back("ATTENTION : la cl\xC3\xA9 de synchro ne se relit pas (" + why + ") : elle n'est pas dans la sauvegarde.");
        }
        crypto::wipe(sk);
    }
    std::string codesJson = core::json::write(storedCodes(folder_, codes), false);
    Value head = Value::object();
    head.set("format", 1);
    head.set("cree", static_cast<std::int64_t>(t));
    head.set("logiciel", std::string("XPG-License-Admin/") + kAppVersion);
    head.set("service_url", url);
    std::string content = core::json::write(head, false);
    content.pop_back();   // le } final : la suite est ajoutee telle quelle
    content += ",\"service\":" + serviceJson;
    wipeString(serviceJson);
    content += ",\"poste\":" + folderJson;
    wipeString(folderJson);
    content += ",\"cle_editeur\":" + keyJson;
    wipeString(keyJson);
    content += ",\"cle_synchro\":" + syncCode;
    wipeString(syncCode);
    content += ",\"codes\":" + codesJson + "}";
    wipeString(codesJson);
    const std::string header = headerText(caller.key->keyId(), t, url, k);
    const std::string sealed = sealContent(header, k, content);
    wipeString(content);
    // 3. le fichier (ecrit a cote puis renomme), les 30 dernieres
    std::error_code ec;
    fs::create_directories(P(dir), ec);
    std::string name = std::string(kBackupPrefix) + localStamp(t) + kBackupExtension;
    for (int i = 2; i < 100 && fs::exists(P(deploy::joinPath(dir, name)), ec); ++i)
        name = std::string(kBackupPrefix) + localStamp(t) + "-" + std::to_string(i) + kBackupExtension;
    const std::string path = deploy::joinPath(dir, name);
    const std::string tmp = path + ".tmp";
    if (!writeText(tmp, header + sealed, &why)) return OpResult::fail(why);
    fs::rename(P(tmp), P(path), ec);
    if (ec) {
        fs::remove(P(tmp), ec);
        return OpResult::fail("impossible d'\xC3\xA9" "crire " + path);
    }
    auto all = backupsIn(dir);
    int removed = 0;
    while (all.size() > kKeptBackups) {
        fs::remove(all.front(), ec);
        all.erase(all.begin());
        ++removed;
    }
    settings.set("derniere", static_cast<std::int64_t>(t));
    settings.set("dernier_fichier", path);
    (void)writeSettings(folder_, std::move(settings), nullptr);
    if (c) (void)detail::flushJournalWith(folder_, *c);
    r.message = "Sauvegarde \xC3\xA9" "crite : " + path;
    if (c) r.details.insert(r.details.begin(), "Service : " + plural(static_cast<std::uint64_t>(tables), "table", "tables") + ", " + plural(rows, "ligne", "lignes") + " (" + url + ").");
    r.details.push_back("Dossier du poste : " + plural(static_cast<std::uint64_t>(files), "fichier", "fichiers") + " ; la cl\xC3\xA9 de l'\xC3\xA9" "diteur" +
                        (withSync ? ", la cl\xC3\xA9 de synchro" : std::string()) + (codes ? ", " + plural(static_cast<std::uint64_t>(codes), "code d'activation", "codes d'activation") : std::string()) +
                        " (chiffr\xC3\xA9s par la phrase).");
    if (removed) r.details.push_back(plural(static_cast<std::uint64_t>(removed), "ancienne sauvegarde effac\xC3\xA9" "e", "anciennes sauvegardes effac\xC3\xA9" "es") + " (les 30 derni\xC3\xA8res restent).");
    r.output = path;
    if (outPath) *outPath = path;
    return r;
}

OpResult Workspace::describeBackup(const std::string& path, BackupInfo* info) {
    std::string text, why;
    if (!readText(path, text, &why, 4096 + kMaxBackupFile)) return OpResult::fail(why);
    BackupFile b;
    if (!parseBackup(text, b, &why)) return OpResult::fail(why);
    BackupInfo i;
    i.keyId = b.keyId;
    i.serviceUrl = b.url;
    (void)licensing::parseIsoTime(b.created, i.created);
    i.params = "Argon2id : " + std::to_string(b.ops) + " passes, " + std::to_string(b.mem / (1024 * 1024)) + " Mio";
    if (info) *info = i;
    OpResult r = OpResult::done("Sauvegarde compl\xC3\xA8te du " + (i.created ? dateTimeFr(i.created) : b.created) + " (cl\xC3\xA9 de l'\xC3\xA9" "diteur " + b.keyId + ").");
    r.details.push_back(b.url.empty() ? std::string("Sans service (le dossier du poste et la cl\xC3\xA9 seulement).") : "Service : " + b.url + ".");
    r.details.push_back("Chiffr\xC3\xA9" "e par une phrase de passe (" + i.params + ").");
    return r;
}

OpResult Workspace::restoreBackup(const std::string& path, const std::string& passphrase, const RestoreOptions& options, RestoreReport* report) const {
    RestoreReport rep;
    if (report) *report = rep;
    if (license_admin::classique()) return OpResult::fail(kClassicRefusal);   // 1.49.0 : la sauvegarde complete porte la cle de l'editeur
    if (!crypto::init()) return OpResult::fail("libsodium ne d\xC3\xA9marre pas");
    std::string text, why;
    if (!readText(path, text, &why, 4096 + kMaxBackupFile)) return OpResult::fail(why);
    BackupFile b;
    if (!parseBackup(text, b, &why)) return OpResult::fail(why);
    wipeString(text);
    DerivedKey k;
    k.ops = b.ops;
    k.mem = b.mem;
    k.salt = b.salt;
    if (!derive(passphrase, k, &why)) return OpResult::fail(why);
    std::string content;
    if (!openContent(b, k, content, &why)) return OpResult::fail(why);
    struct Wipe {
        std::string& s;
        ~Wipe() { wipeString(s); }
    } wipeContent{content};
    std::vector<std::pair<std::string, std::string_view>> top;
    if (!rawMembers(content, top)) return OpResult::fail("le contenu de la sauvegarde est illisible");
    const auto member = [&](const char* name) -> std::string_view {
        for (const auto& [kname, v] : top)
            if (kname == name) return v;
        return {};
    };
    if (member("format") != "1") return OpResult::fail("cette sauvegarde est d'un autre format (une version plus r\xC3\xA9" "cente de l'Admin ?)");
    // 1. la cle de l'editeur : avant tout le reste (un refus ne touche a rien)
    const std::string keyJson(member("cle_editeur"));
    std::string backupKeyId, backupKeyName;
    if (keyJson.empty() || keyJson == "null" || !checkKeyBackupPayload(keyJson, backupKeyId, backupKeyName, &why))
        return OpResult::fail("la cl\xC3\xA9 de l'\xC3\xA9" "diteur de la sauvegarde est illisible" + (why.empty() ? std::string() : " (" + why + ")"));
    const std::string keyPath = publisherKeyPath(folder_);
    bool keepExisting = false;
    if (!options.restoreFolder) {   // seulement le service : la cle de ce poste doit etre celle de la sauvegarde
        std::string w;
        auto existing = isFile(keyPath) ? loadPublisherKey(folder_, &w) : nullptr;
        if (!existing || existing->keyId() != backupKeyId)
            return OpResult::fail("ce poste n'a pas la cl\xC3\xA9 de l'\xC3\xA9" "diteur de la sauvegarde (" + backupKeyId + ") : restaure aussi le dossier du poste");
        if (!options.restoreService) return OpResult::fail("rien \xC3\xA0 restaurer (ni le dossier du poste, ni le service)");
        keepExisting = true;
    } else if (isFile(keyPath)) {
        std::string w;
        auto existing = loadPublisherKey(folder_, &w);
        if (existing && existing->keyId() == backupKeyId) {
            keepExisting = true;
        } else if (!options.replaceKey) {
            return OpResult::fail("une AUTRE cl\xC3\xA9 de l'\xC3\xA9" "diteur (" + (existing ? existing->keyId() : std::string("illisible")) +
                                  ") est d\xC3\xA9j\xC3\xA0 dans ce dossier : rien n'est restaur\xC3\xA9 (coche \xC2\xAB Remplacer la cl\xC3\xA9 \xC2\xBB : elle sera mise de c\xC3\xB4t\xC3\xA9, jamais effac\xC3\xA9" "e)");
        } else {
            std::error_code ec;
            fs::rename(P(keyPath), P(keyPath + ".remplacee-" + fileStamp(licensing::systemNow())), ec);
            if (ec) return OpResult::fail("impossible de mettre de c\xC3\xB4t\xC3\xA9 la cl\xC3\xA9 en place (" + ec.message() + ") : rien n'est restaur\xC3\xA9");
        }
    }
    OpResult r = OpResult::done("");
    // 2. le dossier du poste
    if (options.restoreFolder) {
        Value files;
        if (!core::json::parse(member("poste"), files, nullptr) || !files.isObject()) return OpResult::fail("le dossier du poste de la sauvegarde est illisible");
        for (const auto& [rel, v] : files.members()) {
            if (!safeRelative(rel) || excludedFromBackup(rel) || !v.isString()) continue;
            crypto::Bytes raw;
            if (!crypto::unbase64(v.asString(), raw)) return OpResult::fail("le fichier " + rel + " de la sauvegarde est ab\xC3\xAEm\xC3\xA9");
            const std::string dest = deploy::joinPath(folder_, rel);
            std::error_code ec;
            fs::create_directories(P(dest).parent_path(), ec);
            if (!crypto::writePrivateFile(dest, raw, &why)) return OpResult::fail(why);
            ++rep.files;
        }
    }
    // 3. la cle de l'editeur (protegee pour CE compte)
    if (!keepExisting) {
        auto key = restoreKeyBackupPayload(folder_, keyJson, &why);
        if (!key) return OpResult::fail("la cl\xC3\xA9 de l'\xC3\xA9" "diteur n'est pas r\xC3\xA9\xC3\xA9" "crite : " + why);
    }
    rep.editorKey = true;
    // 4. la cle de synchro, les codes
    if (Value code; options.restoreFolder && core::json::parse(member("cle_synchro"), code, nullptr) && code.isString() && !code.asString().empty()) {
        archive::SyncKey sk{};
        std::string w;
        rep.syncKey = archive::restoreSyncKey(folder_, code.asString(), sk, &w, true);
        crypto::wipe(sk);
        std::string& s = const_cast<std::string&>(code.asString());
        sodium_memzero(s.data(), s.size());
        if (!rep.syncKey) r.details.push_back("ATTENTION : la cl\xC3\xA9 de synchro n'est pas remise (" + w + ").");
    }
    if (Value codes; options.restoreFolder && core::json::parse(member("codes"), codes, nullptr) && codes.isObject()) {
        for (auto& [id, v] : codes.members()) {
            if (!v.isString()) continue;
            if (online::storeCode(folder_, id, v.asString(), nullptr)) ++rep.codes;
            std::string& s = const_cast<std::string&>(v.asString());
            sodium_memzero(s.data(), s.size());
        }
    }
    // 5. la sauvegarde de chaque jour sur ce poste : la meme cle derivee, protegee pour CE compte
    if (options.restoreFolder) (void)storeDerived(folder_, k, nullptr);
    // 6. l'adresse du service
    std::string url = b.url;
    if (!trimmed(options.serviceUrl).empty()) {
        online::Config cfg;
        cfg.url = trimmed(options.serviceUrl);
        if (!online::writeConfig(folder_, cfg, &why)) return OpResult::fail(why);
        (void)online::readConfig(folder_, cfg, nullptr);
        url = cfg.url;
    }
    auto key = loadPublisherKey(folder_, &why);
    if (!key) return OpResult::fail("la cl\xC3\xA9 de l'\xC3\xA9" "diteur remise ne se relit pas : " + why);
    if (options.restoreFolder) {
        r.message = "Dossier du poste restaur\xC3\xA9 (" + plural(static_cast<std::uint64_t>(rep.files), "fichier", "fichiers") + ") ; cl\xC3\xA9 de l'\xC3\xA9" "diteur " +
                    key->keyId() + (keepExisting ? " (d\xC3\xA9j\xC3\xA0 l\xC3\xA0)." : " prot\xC3\xA9g\xC3\xA9" "e pour ce compte.");
        if (rep.syncKey) r.details.push_back("Cl\xC3\xA9 de synchro de l'archive remise.");
        if (rep.codes) r.details.push_back(plural(static_cast<std::uint64_t>(rep.codes), "code d'activation gard\xC3\xA9", "codes d'activation gard\xC3\xA9s") + " de nouveau sur ce poste.");
        r.details.push_back("Le mot de passe du mail n'est pas dans la sauvegarde : tape-le de nouveau (Courriel).");
    } else {
        r.message = "Le dossier du poste reste tel quel (la cl\xC3\xA9 de l'\xC3\xA9" "diteur " + key->keyId() + " est d\xC3\xA9j\xC3\xA0 l\xC3\xA0).";
    }
    // 7. le service
    if (!options.restoreService) {
        (void)detail::queueJournal(folder_, *key, now(), "restauration", "poste", "a restaur\xC3\xA9 le dossier du poste depuis une sauvegarde", {}, {}, {}, nullptr);
        if (report) *report = rep;
        return r;
    }
    const std::string_view service = member("service");
    if (service.empty() || service == "null") return OpResult::fail("cette sauvegarde n'a pas le service (faite sans service r\xC3\xA9gl\xC3\xA9) : le dossier du poste est restaur\xC3\xA9");
    if (url.empty()) return OpResult::fail("l'adresse du service \xC3\xA0 remettre manque");
    std::string_view manifestRaw, tablesRaw;
    if (!rawMember(service, "manifeste", manifestRaw) || !rawMember(service, "tables", tablesRaw)) return OpResult::fail("le service de la sauvegarde est illisible");
    Value manifest;
    if (!core::json::parse(manifestRaw, manifest, nullptr) || !manifest.isObject()) return OpResult::fail("le sommaire du service de la sauvegarde est illisible");
    std::vector<std::pair<std::string, std::string_view>> tables;
    if (!rawMembers(tablesRaw, tables)) return OpResult::fail("les tables du service de la sauvegarde sont illisibles");
    // L'ordre de la restauration : celui du sommaire.
    std::vector<std::string> order;
    if (const Value* ts = manifest.find("tables"); ts && ts->isArray())
        for (const auto& t : ts->items()) order.push_back(t.str("nom"));
    for (const auto& [name, v] : tables)
        if (std::find(order.begin(), order.end(), name) == order.end()) order.push_back(name);
    const online::Options o = editorOptions(*key, now());
    online::Client c(url, *key, o);
    {   // un service en cours de restauration ne repond pas a /v1/etat (503) : on y va quand meme (l'heure de ce poste)
        const online::Reply e = c.call("GET", "/v1/etat", {}, false);
        if (!e.error.empty()) return OpResult::fail(c.explain(e, "joindre le service \xC3\xA0 remettre"));
        if (e.status == 200) (void)c.connect(nullptr);
    }
    Value start = Value::object();
    start.set("schema", static_cast<std::int64_t>(manifest.integer("schema")));
    start.set("remplacer", options.replaceService);
    const online::Reply d = c.call("POST", "/admin/restauration/debut", core::json::write(start, false));
    if (d.error.empty() && d.status == 409 && d.code() == "pas_vide") {
        rep.serviceNotEmpty = true;
        if (report) *report = rep;
        OpResult f = OpResult::fail("le service " + url + " n'est pas vide (des licences, des activations ou une liste des admins) : la restauration les remplacerait. "
                                    "Recommence avec \xC2\xAB Remplacer le service \xC2\xBB si c'est bien lui");
        f.details = r.details;
        f.details.insert(f.details.begin(), r.message);
        return f;
    }
    if (d.error.empty() && d.status == 409 && d.code() == "schema")
        return OpResult::fail("le service " + url + " n'est pas de la m\xC3\xAAme version que celui de la sauvegarde (sch\xC3\xA9ma " + std::to_string(d.json.integer("schema")) + ", la sauvegarde " +
                              std::to_string(manifest.integer("schema")) + ") : d\xC3\xA9ploie la m\xC3\xAAme version du service");
    if (d.status != 200 || !d.error.empty()) return OpResult::fail(c.explain(d, "commencer la restauration du service"));
    std::map<std::string, std::uint64_t> sent;
    for (const auto& name : order) {
        std::string_view arr;
        for (const auto& [n, v] : tables)
            if (n == name) arr = v;
        std::vector<std::string_view> items;
        if (arr.empty() || !rawItems(arr, items)) continue;
        sent[name] = items.size();
        std::size_t i = 0;
        while (i < items.size()) {
            const std::string prefix = "{\"table\":" + core::json::write(Value(name), false) + ",\"lignes\":[";
            std::string body = prefix;
            std::size_t n = 0;
            while (i < items.size() && (n == 0 || body.size() + items[i].size() + 3 <= kLotBytes)) {
                if (n) body += ',';
                body.append(items[i].data(), items[i].size());
                ++n;
                ++i;
            }
            body += "]}";
            const online::Reply l = c.call("POST", "/admin/restauration/lot", body);
            wipeString(body);
            if (l.status != 200 || !l.error.empty())
                return OpResult::fail(c.explain(l, "remettre la table " + name + " (" + std::to_string(i) + " lignes sur " + std::to_string(items.size()) +
                                                       ") ; le service reste en restauration : recommence (avec \xC2\xAB Remplacer le service \xC2\xBB)"));
        }
    }
    const online::Reply f = c.call("POST", "/admin/restauration/fin");
    if (f.status != 200 || !f.error.empty() || !f.json.isObject()) return OpResult::fail(c.explain(f, "finir la restauration du service"));
    rep.serviceRestored = true;
    rep.filesToResend = static_cast<int>(f.json.integer("fichiers_a_renvoyer"));
    rep.serviceKey = f.json.str("cle_service");
    std::uint64_t total = 0;
    std::vector<std::string> mismatch;
    if (const Value* ts = f.json.find("tables"); ts && ts->isArray())
        for (const auto& t : ts->items()) {
            const std::string n = t.str("nom");
            const auto lines = static_cast<std::uint64_t>(t.integer("lignes"));
            rep.tables.emplace_back(n, lines);
            total += lines;
            const auto it = sent.find(n);
            if ((it == sent.end() ? 0 : it->second) != lines) mismatch.push_back(n);
        }
    r.details.push_back("Service " + url + " remis : " + plural(rep.tables.size(), "table", "tables") + ", " + plural(total, "ligne", "lignes") + ", la m\xC3\xAAme cl\xC3\xA9 de service.");
    if (!mismatch.empty()) {
        std::string s;
        for (const auto& m : mismatch) s += (s.empty() ? "" : ", ") + m;
        r.details.push_back("ATTENTION : le service ne compte pas les m\xC3\xAAmes lignes que la sauvegarde pour : " + s + ".");
    }
    if (rep.filesToResend)
        r.details.push_back(plural(static_cast<std::uint64_t>(rep.filesToResend), "fichier \xC3\xA0 renvoyer", "fichiers \xC3\xA0 renvoyer") +
                            " (les installateurs ne sont pas dans la sauvegarde) : page Versions.");
    // Une autre adresse que celle de la sauvegarde : le certificat (il nomme l'adresse) est signe de nouveau pour elle.
    if (!b.url.empty() && !svc::sameUrl(b.url, url)) {
        const online::Result cert = online::certify(folder_, *key, o, rep.serviceKey);
        r.details.push_back(cert.ok ? "Certificat de la m\xC3\xAAme cl\xC3\xA9 de service sign\xC3\xA9 pour la nouvelle adresse (" + url + ")."
                                    : "ATTENTION : certificat pour la nouvelle adresse : " + cert.message + " (Service en ligne \xE2\x80\xBA Certifier).");
    }
    r.message = options.restoreFolder ? "Sauvegarde restaur\xC3\xA9" "e : le dossier du poste et le service (" + url + ")."
                                      : "Service restaur\xC3\xA9 depuis la sauvegarde (" + url + ") ; le dossier du poste reste tel quel.";
    if (report) *report = rep;
    return r;
}

} // namespace license_admin::gui
