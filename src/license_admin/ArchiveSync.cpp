// =============================================================================
//  license_admin/ArchiveSync.cpp - 1.35.0 : l'archive privee de l'editeur et sa
//  synchro par le service des licences (voir ArchiveSync.hpp). JAMAIS LIVRE A
//  UN CLIENT. Le chiffrement, l'index et les regles de fusion sont ceux de la
//  synchro GitHub de la 1.31.0 ; seul le transport change (/admin/archive).
// =============================================================================
#include "ArchiveSync.hpp"

#include "Admin.hpp"
#include "ServiceClient.hpp"

#include "../app/ai/AiHttp.hpp"
#include "../core/Json.hpp"
#include "../deploy/Crypto.hpp"
#include "../deploy/Paths.hpp"
#include "../licensing/Documents.hpp"
#include "../licensing/SignedFile.hpp"
#include "../licensing/Store.hpp"

#include <sodium.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace license_admin::archive {

namespace fs = std::filesystem;
namespace crypto = deploy::crypto;
using core::json::Value;
using licensing::DocKind;

namespace {

constexpr const char* kSyncFormat = "xpg-synchro-licences/1";
constexpr const char* kSyncKeyFormat = "xpg-cle-synchro/1";
constexpr std::size_t kMaxReply = 200u * 1024u * 1024u;

fs::path P(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
bool isFile(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(P(path), ec);
}
bool readText(const std::string& path, std::string& out, std::string* why) {
    crypto::Bytes raw;
    if (!crypto::readFile(path, raw, why, kMaxReply)) return false;
    out.assign(raw.begin(), raw.end());
    return true;
}
bool writeFileAt(const std::string& path, std::string_view text, std::string* why) {
    std::error_code ec;
    fs::create_directories(P(path).parent_path(), ec);
    crypto::Bytes bytes(text.begin(), text.end());
    return crypto::writePrivateFile(path, bytes, why);
}
std::string cite(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

// ---- un secret garde sur ce poste (la cle de synchro) ----

struct SecretSpec {
    const char* format;
    const char* comment;
};

bool saveSecret(const std::string& path, const SecretSpec& spec, const crypto::Bytes& plain, std::string* why) {
    crypto::Bytes prot;
    if (!crypto::protect(plain, prot, false, why)) return false;
    std::string text = std::string("# ") + spec.comment + "\n# Ne se copie pas sur un autre poste ; ne se publie jamais.\n";
    text += std::string("format=") + spec.format + "\n";
    text += std::string("protection=") + (crypto::protectedHere() ? "dpapi" : "fichier") + "\n";
    text += "secret=" + crypto::hex(prot.data(), prot.size()) + "\n";
    crypto::wipe(prot.data(), prot.size());
    return writeFileAt(path, text, why);
}

bool loadSecret(const std::string& path, const SecretSpec& spec, crypto::Bytes& plain, std::string* why) {
    std::string text;
    if (!readText(path, text, why)) return false;
    std::map<std::string, std::string> kv;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    if (kv["format"] != spec.format) {
        if (why) *why = "le fichier " + path + " n'est pas du bon format (" + spec.format + ")";
        return false;
    }
    crypto::Bytes prot;
    if (!crypto::unhex(kv["secret"], prot)) {
        if (why) *why = "le fichier " + path + " est ab\xC3\xAEm\xC3\xA9";
        return false;
    }
    std::string w;
    if (!crypto::unprotect(prot, plain, &w)) {
        if (why) *why = "le fichier " + path + " ne se relit pas ici (" + w + ") : il a \xC3\xA9t\xC3\xA9 cr\xC3\xA9\xC3\xA9 sous un autre compte Windows ou sur un autre poste";
        return false;
    }
    return true;
}

const SecretSpec& syncKeySpec() {
    static const SecretSpec s{kSyncKeyFormat, "La cle de synchro des licences (XPG-License-Admin), protegee par DPAPI pour ce compte."};
    return s;
}

// ---- les cles derivees de la cle de synchro ----

std::array<std::uint8_t, 32> subKey(const SyncKey& key, std::uint64_t id, const char (&context)[9]) {
    std::array<std::uint8_t, 32> out{};
    crypto_kdf_derive_from_key(out.data(), out.size(), id, context, key.data());
    return out;
}

std::string base64Of(std::string_view bytes) { return app::ai::base64(bytes); }

bool fromBase64(std::string_view text, std::string& out) {
    out.assign(text.size(), '\0');
    std::size_t len = 0;
    if (sodium_base642bin(reinterpret_cast<unsigned char*>(out.data()), out.size(), text.data(), text.size(), "\n\r \t", &len, nullptr,
                          sodium_base64_VARIANT_ORIGINAL)
        != 0)
        return false;
    out.resize(len);
    return true;
}

std::string wrap76(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 76) out += s.substr(i, 76) + "\n";
    return out;
}

std::string indexMac(const SyncKey& key, std::string_view body) {
    auto k = subKey(key, 2, "XPGINDEX");
    std::array<std::uint8_t, 32> mac{};
    crypto_generichash(mac.data(), mac.size(), reinterpret_cast<const unsigned char*>(body.data()), body.size(), k.data(), k.size());
    crypto::wipe(k);
    return crypto::hex(mac);
}

// ---- base32 de Crockford ----
constexpr const char* kB32 = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

int b32Value(char c) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (c == 'O') c = '0';
    if (c == 'I' || c == 'L') c = '1';
    const char* at = std::strchr(kB32, c);
    return at && c ? static_cast<int>(at - kB32) : -1;
}

} // namespace

namespace {
std::string syncKeyPath(const std::string& folder) { return deploy::joinPath(folder, kSyncKeyFile); }
} // namespace

// ============================================================================
//  la cle de synchro
// ============================================================================

bool hasSyncKey(const std::string& folder) { return isFile(syncKeyPath(folder)); }

namespace {
bool storeSyncKey(const std::string& folder, const SyncKey& key, std::string* why) {
    crypto::Bytes plain(key.begin(), key.end());
    const bool ok = saveSecret(syncKeyPath(folder), syncKeySpec(), plain, why);
    crypto::wipe(plain.data(), plain.size());
    return ok;
}
} // namespace

bool createSyncKey(const std::string& folder, SyncKey& out, std::string* why) {
    if (!crypto::init()) {
        if (why) *why = "libsodium ne d\xC3\xA9marre pas";
        return false;
    }
    if (hasSyncKey(folder)) {
        if (why) *why = "une cl\xC3\xA9 de synchro existe d\xC3\xA9j\xC3\xA0 sur ce poste (" + syncKeyPath(folder) + ") : elle n'est jamais remplac\xC3\xA9" "e";
        return false;
    }
    crypto::randomBytes(out.data(), out.size());
    return storeSyncKey(folder, out, why);
}

bool loadSyncKey(const std::string& folder, SyncKey& out, std::string* why) {
    if (!hasSyncKey(folder)) {
        if (why) *why = "pas de cl\xC3\xA9 de synchro sur ce poste (archive-cle la cr\xC3\xA9" "e ; archive-cle --code-secours la redonne)";
        return false;
    }
    crypto::Bytes plain;
    if (!loadSecret(syncKeyPath(folder), syncKeySpec(), plain, why)) return false;
    if (plain.size() != out.size()) {
        crypto::wipe(plain.data(), plain.size());
        if (why) *why = "la cl\xC3\xA9 de synchro est ab\xC3\xAEm\xC3\xA9" "e (" + syncKeyPath(folder) + ")";
        return false;
    }
    std::copy(plain.begin(), plain.end(), out.begin());
    crypto::wipe(plain.data(), plain.size());
    return true;
}

std::string syncKeyId(const SyncKey& key) {
    std::string msg = std::string(kSyncKeyFormat) + "\n";
    msg.append(reinterpret_cast<const char*>(key.data()), key.size());
    const auto d = crypto::sha256(msg);
    sodium_memzero(msg.data(), msg.size());
    return "xpgs-" + crypto::hex(d.data(), 8);
}

std::string recoveryCode(const SyncKey& key) {
    std::vector<std::uint8_t> payload(key.begin(), key.end());
    const auto check = crypto::sha256(key.data(), key.size());
    payload.push_back(check[0]);
    payload.push_back(check[1]);
    std::string b32;
    unsigned buffer = 0;
    int bits = 0;
    for (auto byte : payload) {
        buffer = (buffer << 8) | byte;
        bits += 8;
        while (bits >= 5) {
            b32 += kB32[(buffer >> (bits - 5)) & 31u];
            bits -= 5;
        }
    }
    if (bits > 0) b32 += kB32[(buffer << (5 - bits)) & 31u];
    std::string out = "XPGS1";
    for (std::size_t i = 0; i < b32.size(); i += 5) out += "-" + b32.substr(i, 5);
    sodium_memzero(payload.data(), payload.size());
    sodium_memzero(b32.data(), b32.size());
    return out;
}

bool parseRecoveryCode(std::string_view code, SyncKey& out, std::string* why) {
    std::string clean;
    for (char c : code)
        if (c != '-' && c != ' ' && c != '\t' && c != '\r' && c != '\n') clean += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (clean.rfind("XPGS1", 0) != 0) {
        if (why) *why = "un code de secours commence par XPGS1-";
        return false;
    }
    clean.erase(0, 5);
    if (clean.size() != 55) {
        if (why) *why = "le code de secours n'a pas la bonne longueur (11 groupes de 5 apr\xC3\xA8s XPGS1)";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    unsigned buffer = 0;
    int bits = 0;
    for (char c : clean) {
        const int v = b32Value(c);
        if (v < 0) {
            if (why) *why = std::string("le code de secours contient un caract\xC3\xA8re inconnu : ") + c;
            return false;
        }
        buffer = (buffer << 5) | static_cast<unsigned>(v);
        bits += 5;
        if (bits >= 8) {
            bytes.push_back(static_cast<std::uint8_t>((buffer >> (bits - 8)) & 0xFFu));
            bits -= 8;
        }
    }
    if (bytes.size() < 34) {
        if (why) *why = "le code de secours est trop court";
        return false;
    }
    std::copy(bytes.begin(), bytes.begin() + 32, out.begin());
    const auto check = crypto::sha256(out.data(), out.size());
    const bool ok = check[0] == bytes[32] && check[1] == bytes[33];
    sodium_memzero(bytes.data(), bytes.size());
    if (!ok) {
        sodium_memzero(out.data(), out.size());
        if (why) *why = "le code de secours est faux (une faute de frappe ?)";
        return false;
    }
    return true;
}

bool restoreSyncKey(const std::string& folder, std::string_view code, SyncKey& out, std::string* why, bool replace, std::string* kept) {
    if (!parseRecoveryCode(code, out, why)) return false;
    if (hasSyncKey(folder)) {
        SyncKey current{};
        std::string w;
        const bool readable = loadSyncKey(folder, current, &w);
        const bool same = readable && sodium_memcmp(current.data(), out.data(), out.size()) == 0;
        const std::string currentId = readable ? syncKeyId(current) : std::string("illisible");
        sodium_memzero(current.data(), current.size());
        if (same) return true;
        if (!replace) {
            if (why)
                *why = "une AUTRE cl\xC3\xA9 de synchro (" + currentId + ") est d\xC3\xA9j\xC3\xA0 sur ce poste : rien n'est remplac\xC3\xA9. Pour mettre celle du code \xC3\xA0 sa place "
                       "(l'ancienne est gard\xC3\xA9" "e \xC3\xA0 c\xC3\xB4t\xC3\xA9) : ajoute --remplacer";
            return false;
        }
        std::string stamp = licensing::isoTime(licensing::systemNow());
        for (auto& c : stamp)
            if (c == ':') c = '-';
        const std::string old = syncKeyPath(folder) + ".ancienne-" + stamp;
        std::error_code ec;
        fs::rename(P(syncKeyPath(folder)), P(old), ec);
        if (ec) {
            if (why) *why = "impossible de mettre de c\xC3\xB4t\xC3\xA9 l'ancienne cl\xC3\xA9 de synchro (" + ec.message() + ") : rien n'est remplac\xC3\xA9";
            return false;
        }
        if (kept) *kept = old;
    }
    return storeSyncKey(folder, out, why);
}

// ============================================================================
//  chiffrer
// ============================================================================

namespace {
std::string aadOf(std::string_view path) { return std::string(kSyncFormat) + "\n" + std::string(path); }
} // namespace

std::string seal(const SyncKey& key, std::string_view path, std::string_view plain) {
    auto k = subKey(key, 1, "XPGCHIFF");
    std::string box(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES, '\0');
    auto* nonce = reinterpret_cast<unsigned char*>(box.data());
    crypto::randomBytes(nonce, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    const std::string aad = aadOf(path);
    unsigned long long len = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(nonce + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, &len,
                                               reinterpret_cast<const unsigned char*>(plain.data()), plain.size(),
                                               reinterpret_cast<const unsigned char*>(aad.data()), aad.size(), nullptr, nonce, k.data());
    crypto::wipe(k);
    box.resize(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + static_cast<std::size_t>(len));
    return "XPG-CHIFFRE 1 " + syncKeyId(key) + "\n" + wrap76(base64Of(box));
}

bool openSealed(const SyncKey& key, std::string_view path, std::string_view sealed, std::string& plain, std::string* why) {
    const auto nl = sealed.find('\n');
    const std::string head(sealed.substr(0, nl == std::string_view::npos ? sealed.size() : nl));
    const std::string prefix = "XPG-CHIFFRE 1 ";
    if (head.rfind(prefix, 0) != 0 || nl == std::string_view::npos) {
        if (why) *why = "ce n'est pas un fichier chiffr\xC3\xA9 par l'outil";
        return false;
    }
    const std::string id = head.substr(prefix.size());
    if (id != syncKeyId(key)) {
        if (why) *why = "chiffr\xC3\xA9 avec une autre cl\xC3\xA9 de synchro (" + id + ") ; celle de ce poste est " + syncKeyId(key);
        return false;
    }
    std::string box;
    if (!fromBase64(sealed.substr(nl + 1), box) || box.size() < crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + crypto_aead_xchacha20poly1305_ietf_ABYTES) {
        if (why) *why = "le fichier chiffr\xC3\xA9 est ab\xC3\xAEm\xC3\xA9";
        return false;
    }
    auto k = subKey(key, 1, "XPGCHIFF");
    const auto* nonce = reinterpret_cast<const unsigned char*>(box.data());
    const std::size_t clen = box.size() - crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    plain.assign(clen, '\0');
    unsigned long long len = 0;
    const std::string aad = aadOf(path);
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(reinterpret_cast<unsigned char*>(plain.data()), &len, nullptr,
                                                              nonce + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, clen,
                                                              reinterpret_cast<const unsigned char*>(aad.data()), aad.size(), nonce, k.data());
    crypto::wipe(k);
    if (rc != 0) {
        plain.clear();
        if (why) *why = "le fichier chiffr\xC3\xA9 ne se d\xC3\xA9" "chiffre pas : ab\xC3\xAEm\xC3\xA9, modifi\xC3\xA9 ou d\xC3\xA9plac\xC3\xA9 hors de l'outil";
        return false;
    }
    plain.resize(static_cast<std::size_t>(len));
    return true;
}

// ============================================================================
//  l'archive locale et ce que contient un fichier signe
// ============================================================================

std::string archiveFolder(const std::string& folder) { return deploy::joinPath(folder, kArchiveDir); }

std::string safeName(std::string_view id) {
    std::string out;
    for (char c : id) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
        out += !ok || (c == '.' && !out.empty() && out.back() == '.') ? '_' : c;   // jamais ".." dans un chemin
    }
    while (!out.empty() && out.front() == '.') out.erase(0, 1);
    if (out.empty()) out = "licence";
    return out.substr(0, 120);
}

std::vector<licensing::crypto::PublicKey> trustedKeys(const licensing::SigningKey* key) {
    std::vector<crypto::PublicKey> out;
    if (key) out.push_back(key->publicKey());
    for (const auto& t : licensing::compiledPublisherKeys())
        if (std::find(out.begin(), out.end(), t.key) == out.end()) out.push_back(t.key);
    return out;
}

namespace {

// Ce que l'index dit d'un fichier, et ce que le poste en sait.
struct Entry {
    std::string   path;          // dans le depot, sous la racine : "licences/XPG-2026-00042.xpglic.chiffre"
    std::string   kind;          // "registre", "revocations", "licence", "activations"
    bool          encrypted{false};
    std::string   sha256;        // du contenu EN CLAIR
    std::uint64_t number{0};     // registre, revocations
    Seconds       date{0};       // la date de signature (issued_at)
    std::string   licenseId;     // une licence
    int           lines{0};      // le registre des activations
    std::string   stored;        // le SHA-256 du texte depose (chiffre) : un fichier change hors de l'outil se voit
    std::string   local;         // le fichier du poste (vide : pas sur le poste)
};

// Ce que dit un fichier signe : son genre, son numero ou sa licence, sa date ; `verified` : signe par une cle de confiance.
bool describeSigned(std::string_view bytes, const std::vector<crypto::PublicKey>& trusted, Entry& e, bool& verified, std::string* why) {
    std::vector<licensing::SignedBlock> blocks;
    if (!licensing::readBlocks(bytes, blocks, why)) return false;
    const licensing::SignedBlock* main = nullptr;
    int licences = 0;
    for (const auto& b : blocks)
        if (b.kind == DocKind::License) {
            main = &b;
            ++licences;
        }
    if (licences > 1) {
        if (why) *why = "le fichier contient plusieurs licences";
        return false;
    }
    if (!main) {
        for (const auto& b : blocks)
            if (b.kind == DocKind::Request) {
                if (why) *why = "une demande d'activation ne se range pas dans l'archive (enregistrer-demande la note dans le registre des activations)";
                return false;
            }
        if (blocks.size() != 1) {
            if (why) *why = "le fichier doit contenir un registre, une liste de r\xC3\xA9vocation ou une licence";
            return false;
        }
        main = &blocks[0];
    }
    verified = false;
    for (const auto& k : trusted)
        if (licensing::verifyBlock(*main, k)) verified = true;
    if (main->kind == DocKind::License) {
        licensing::License l;
        if (!licensing::parseLicense(main->document, l, why)) return false;
        e.kind = "licence";
        e.licenseId = l.licenseId;
        e.date = l.issuedAt;
        e.encrypted = true;
        e.path = "licences/" + safeName(l.licenseId) + ".xpglic.chiffre";
    } else if (main->kind == DocKind::Registry) {
        licensing::Registry r;
        if (!licensing::parseRegistry(main->document, r, why)) return false;
        e.kind = "registre";
        e.number = r.number;
        e.date = r.issuedAt;
        e.path = "registre/registre.xpgreg";
    } else {
        licensing::RevocationList r;
        if (!licensing::parseRevocations(main->document, r, why)) return false;
        e.kind = "revocations";
        e.number = r.number;
        e.date = r.issuedAt;
        e.encrypted = true;   // une raison en texte libre peut nommer un client
        e.path = "revocations/revocations.xpgrev.chiffre";
    }
    e.sha256 = crypto::sha256Hex(bytes);
    return true;
}

// Le fichier de l'archive locale pour une entree du depot.
std::string localPathOf(const std::string& folder, const Entry& e) {
    if (e.kind == "activations") return activationsPath(folder);
    std::string rel = e.path;
    const std::string sealedExt = ".chiffre";
    if (rel.size() > sealedExt.size() && rel.compare(rel.size() - sealedExt.size(), sealedExt.size(), sealedExt) == 0)
        rel.resize(rel.size() - sealedExt.size());
    return deploy::joinPath(archiveFolder(folder), rel);
}

int countLines(std::string_view text) {
    int n = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty()) ++n;
        pos = end + 1;
    }
    return n;
}

// Ce qui est sur le poste : l'archive et le registre des activations. `warnings` : les fichiers illisibles.
std::vector<Entry> localEntries(const std::string& folder, std::vector<std::string>& warnings) {
    std::vector<Entry> out;
    std::vector<std::string> files;
    const std::string arch = archiveFolder(folder);
    for (const char* f : {"registre/registre.xpgreg", "revocations/revocations.xpgrev"})
        if (isFile(deploy::joinPath(arch, f))) files.push_back(deploy::joinPath(arch, f));
    std::error_code ec;
    const fs::path lic = P(deploy::joinPath(arch, "licences"));
    if (fs::is_directory(lic, ec))
        for (const auto& it : fs::directory_iterator(lic, ec))
            if (it.is_regular_file(ec) && it.path().extension() == ".xpglic") files.push_back(U(it.path()));
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        std::string bytes, w;
        Entry e;
        bool verified = false;
        if (!readText(f, bytes, &w) || !describeSigned(bytes, {}, e, verified, &w)) {
            warnings.push_back("archive : " + f + " ignor\xC3\xA9 (" + w + ")");
            continue;
        }
        e.local = f;
        // Le nom range doit etre celui que l'archive donnerait (une licence renommee a la main : ignoree).
        if (localPathOf(folder, e) != f) {
            warnings.push_back("archive : " + f + " ignor\xC3\xA9 (il devrait s'appeler " + localPathOf(folder, e) + ")");
            continue;
        }
        out.push_back(std::move(e));
    }
    const std::string act = activationsPath(folder);
    if (isFile(act)) {
        std::string text, w;
        if (readText(act, text, &w) && countLines(text) > 0) {
            Entry e;
            e.kind = "activations";
            e.encrypted = true;
            e.path = "activations/activations.jsonl.chiffre";
            e.sha256 = crypto::sha256Hex(text);
            e.lines = countLines(text);
            e.local = act;
            out.push_back(std::move(e));
        } else if (!w.empty()) {
            warnings.push_back("le registre des activations ne se lit pas (" + w + ")");
        }
    }
    return out;
}

std::string describeEntry(const Entry& e) {
    if (e.kind == "registre") return "registre n\xC2\xB0 " + std::to_string(e.number) + (e.date ? " du " + licensing::isoDate(e.date) : std::string());
    if (e.kind == "revocations")
        return "liste de r\xC3\xA9vocation n\xC2\xB0 " + std::to_string(e.number) + (e.date ? " du " + licensing::isoDate(e.date) : std::string());
    if (e.kind == "licence") return "licence " + e.licenseId + (e.date ? " (sign\xC3\xA9" "e le " + licensing::isoDate(e.date) + ")" : std::string());
    return "registre des activations (" + std::to_string(e.lines) + " ligne(s))";
}

// Le plus recent : >0 si `a` l'est, <0 si `b`, 0 a egalite.
int newer(const Entry& a, const Entry& b) {
    if (a.kind == "registre" || a.kind == "revocations") {
        if (a.number != b.number) return a.number > b.number ? 1 : -1;
    }
    if (a.date != b.date) return a.date > b.date ? 1 : -1;
    return 0;
}

} // namespace

bool archiveSigned(const std::string& folder, std::string_view bytes, const std::vector<crypto::PublicKey>& trusted, std::string* what,
                   std::string* why) {
    if (trusted.empty()) {
        if (why) *why = "aucune cl\xC3\xA9 d'\xC3\xA9" "diteur sur ce poste pour v\xC3\xA9rifier la signature";
        return false;
    }
    Entry e;
    bool verified = false;
    if (!describeSigned(bytes, trusted, e, verified, why)) return false;
    if (!verified) {
        if (why) *why = "la signature n'est pas celle de la cl\xC3\xA9 de l'\xC3\xA9" "diteur";
        return false;
    }
    const std::string path = localPathOf(folder, e);
    if (isFile(path)) {
        std::string old, w;
        Entry o;
        bool v = false;
        const bool readable = readText(path, old, &w) && describeSigned(old, {}, o, v, &w);
        if (readable) {
            if (old == bytes) {
                if (what) *what = "d\xC3\xA9j\xC3\xA0 dans l'archive";
                return true;
            }
            if (newer(e, o) <= 0) {
                if (what) *what = "plus ancien que celui de l'archive (" + describeEntry(o) + ") : l'archive garde le sien";
                return true;
            }
        }
        if (!writeFileAt(path, bytes, why)) return false;
        if (what) *what = readable ? "remplace " + describeEntry(o) + " dans l'archive" : "remplace un fichier illisible de l'archive";
        return true;
    }
    if (!writeFileAt(path, bytes, why)) return false;
    if (what) *what = "rang\xC3\xA9 dans l'archive (" + describeEntry(e) + ")";
    return true;
}

std::vector<std::string> missingLines(std::string_view local, std::string_view remote) {
    std::set<std::string> have;
    const auto each = [](std::string_view text, const auto& fn) {
        std::size_t pos = 0;
        while (pos < text.size()) {
            std::size_t end = text.find('\n', pos);
            if (end == std::string_view::npos) end = text.size();
            auto line = text.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (!line.empty()) fn(std::string(line));
            pos = end + 1;
        }
    };
    each(local, [&](const std::string& l) { have.insert(l); });
    std::vector<std::string> out;
    each(remote, [&](const std::string& l) {
        if (have.insert(l).second) out.push_back(l);
    });
    return out;
}

// ============================================================================
