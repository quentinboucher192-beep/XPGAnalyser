// =============================================================================
//  license_admin/GitHubSync.cpp - 1.31.0 : la synchro avec un depot GitHub prive
//  (voir GitHubSync.hpp). JAMAIS LIVRE A UN CLIENT.
// =============================================================================
#include "GitHubSync.hpp"

#include "Admin.hpp"

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
#include <thread>
#include <chrono>

namespace license_admin::github {

namespace fs = std::filesystem;
namespace crypto = deploy::crypto;
using core::json::Value;
using licensing::DocKind;

namespace {

constexpr const char* kSyncFormat = "xpg-synchro-licences/1";
constexpr const char* kConfigFormat = "xpg-github-licences/1";
constexpr const char* kTokenFormat = "xpg-jeton-github/1";
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

// ---- un secret garde sur ce poste (le jeton, la cle de synchro) ----

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

const SecretSpec& tokenSpec() {
    static const SecretSpec s{kTokenFormat, "Le jeton GitHub de l'outil de l'editeur (XPG-License-Admin), protege par DPAPI pour ce compte."};
    return s;
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

// ============================================================================
//  la configuration
// ============================================================================

bool validRepo(std::string_view repo) noexcept {
    const auto slash = repo.find('/');
    if (slash == std::string_view::npos || slash == 0 || slash > 39 || repo.find('/', slash + 1) != std::string_view::npos) return false;
    const auto owner = repo.substr(0, slash), name = repo.substr(slash + 1);
    if (name.empty() || name.size() > 100 || name == "." || name == "..") return false;
    for (char c : owner)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') return false;
    for (char c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') return false;
    return true;
}

bool validBranch(std::string_view b) noexcept {
    if (b.empty() || b.size() > 200 || b.front() == '/' || b.back() == '/' || b.find("..") != std::string_view::npos
        || b.find("//") != std::string_view::npos || (b.size() >= 5 && b.substr(b.size() - 5) == ".lock"))
        return false;
    for (char c : b)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.' && c != '/') return false;
    return true;
}

bool validRoot(std::string_view r) noexcept {
    if (r.empty()) return true;
    if (r.size() > 200 || r.front() == '/' || r.back() == '/' || r.find("//") != std::string_view::npos) return false;
    std::size_t pos = 0;
    while (pos <= r.size()) {
        std::size_t end = r.find('/', pos);
        if (end == std::string_view::npos) end = r.size();
        const auto seg = r.substr(pos, end - pos);
        if (seg.empty() || seg == "." || seg == "..") return false;
        for (char c : seg)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') return false;
        pos = end + 1;
    }
    return true;
}

std::string configPath(const std::string& folder) { return deploy::joinPath(folder, kConfigFile); }
bool configured(const std::string& folder) { return isFile(configPath(folder)); }

bool readConfig(const std::string& folder, Config& out, std::string* why) {
    const std::string path = configPath(folder);
    if (!isFile(path)) {
        if (why) *why = "la synchro GitHub n'est pas configur\xC3\xA9" "e sur ce poste (XPG-License-Admin github-config --depot PROPRIETAIRE/NOM)";
        return false;
    }
    std::string text, err;
    Value v;
    if (!readText(path, text, why)) return false;
    if (!core::json::parse(text, v, &err) || !v.isObject() || v.str("format") != kConfigFormat) {
        if (why) *why = path + " est illisible (" + (err.empty() ? std::string("pas le bon format") : err) + ")";
        return false;
    }
    out.repo = v.str("depot");
    out.branch = v.str("branche", "main");
    out.root = v.str("racine");
    out.api = v.str("api", kDefaultApi);
    while (!out.api.empty() && out.api.back() == '/') out.api.pop_back();
    app::ai::Url u;
    std::string w;
    if (!validRepo(out.repo) || !validBranch(out.branch) || !validRoot(out.root) || !app::ai::parseUrl(out.api, u, &w)) {
        if (why) *why = path + " : d\xC3\xA9p\xC3\xB4t, branche, racine ou adresse refus\xC3\xA9s" + (w.empty() ? std::string() : " (" + w + ")");
        return false;
    }
    return true;
}

bool writeConfig(const std::string& folder, const Config& c, std::string* why) {
    app::ai::Url u;
    std::string w;
    if (!validRepo(c.repo)) {
        if (why) *why = "le d\xC3\xA9p\xC3\xB4t s'\xC3\xA9" "crit PROPRIETAIRE/NOM (par exemple mon-compte/licences-xpg) : " + cite(c.repo) + " est refus\xC3\xA9";
        return false;
    }
    if (!validBranch(c.branch)) {
        if (why) *why = "nom de branche refus\xC3\xA9 : " + cite(c.branch);
        return false;
    }
    if (!validRoot(c.root)) {
        if (why) *why = "le dossier dans le d\xC3\xA9p\xC3\xB4t (--racine) est refus\xC3\xA9 : " + cite(c.root) + " (lettres, chiffres, . _ - et /)";
        return false;
    }
    if (!app::ai::parseUrl(c.api, u, &w)) {
        if (why) *why = "adresse de l'API refus\xC3\xA9" "e : " + w;
        return false;
    }
    Value v = Value::object();
    v.set("format", std::string(kConfigFormat));
    v.set("depot", c.repo);
    v.set("branche", c.branch);
    v.set("racine", c.root);
    v.set("api", c.api);
    return writeFileAt(configPath(folder), core::json::write(v, true) + "\n", why);
}

// ============================================================================
//  le jeton
// ============================================================================

namespace {
std::string tokenPath(const std::string& folder) { return deploy::joinPath(folder, kTokenFile); }
std::string syncKeyPath(const std::string& folder) { return deploy::joinPath(folder, kSyncKeyFile); }
} // namespace

bool hasToken(const std::string& folder) { return isFile(tokenPath(folder)); }

bool plausibleToken(std::string_view t) noexcept {
    if (t.size() < 20 || t.size() > 255) return false;
    for (char c : t)
        if (static_cast<unsigned char>(c) <= 0x20 || static_cast<unsigned char>(c) >= 0x7F) return false;
    return true;
}

std::string tokenKind(std::string_view t) {
    if (t.rfind("github_pat_", 0) == 0) return "jeton \xC3\xA0 grain fin";
    if (t.rfind("ghp_", 0) == 0) return "jeton classique";
    return {};
}

bool saveToken(const std::string& folder, const std::string& token, std::string* why) {
    if (!plausibleToken(token)) {
        if (why) *why = "ce n'est pas un jeton GitHub (20 \xC3\xA0 255 caract\xC3\xA8res, sans espace)";
        return false;
    }
    crypto::Bytes plain(token.begin(), token.end());
    const bool ok = saveSecret(tokenPath(folder), tokenSpec(), plain, why);
    crypto::wipe(plain.data(), plain.size());
    return ok;
}

bool loadToken(const std::string& folder, std::string& token, std::string* why) {
    if (!hasToken(folder)) {
        if (why) *why = "pas de jeton GitHub sur ce poste (XPG-License-Admin github-jeton)";
        return false;
    }
    crypto::Bytes plain;
    if (!loadSecret(tokenPath(folder), tokenSpec(), plain, why)) return false;
    token.assign(plain.begin(), plain.end());
    crypto::wipe(plain.data(), plain.size());
    if (!plausibleToken(token)) {
        if (why) *why = "le jeton enregistr\xC3\xA9 est ab\xC3\xAEm\xC3\xA9 : enregistre-le de nouveau (github-jeton)";
        return false;
    }
    return true;
}

bool eraseToken(const std::string& folder, std::string* why) {
    std::error_code ec;
    if (!hasToken(folder)) return true;
    if (!fs::remove(P(tokenPath(folder)), ec) || ec) {
        if (why) *why = "impossible de supprimer " + tokenPath(folder);
        return false;
    }
    return true;
}

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
        if (why) *why = "pas de cl\xC3\xA9 de synchro sur ce poste (github-config la cr\xC3\xA9" "e ; github-config --code-secours la redonne)";
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
    std::string   blob;          // l'identifiant Git du fichier publie (l'index le note : un fichier change hors de l'outil se voit)
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
//  GitHub (l'API REST : le depot, les references, les objets Git)
// ============================================================================

namespace {

struct Reply {
    int         status{0};
    std::string body;
    std::string error;         // une panne du reseau (en francais) ; vide : une reponse est arrivee
    std::string rateLeft;      // x-ratelimit-remaining
    Value       json;
    [[nodiscard]] std::string message() const {
        std::string m = json.isObject() ? json.str("message") : std::string();
        std::string clean;
        for (char c : m)
            if (static_cast<unsigned char>(c) >= 0x20) clean += c;
        return clean.substr(0, 300);
    }
};

class Api {
public:
    Api(Config config, std::string token) : config_(std::move(config)), token_(std::move(token)) {}
    ~Api() { sodium_memzero(token_.data(), token_.size()); }
    Api(const Api&) = delete;
    Api& operator=(const Api&) = delete;

    // `path` : apres /repos/PROPRIETAIRE/NOM ("" : le depot lui-meme).
    Reply call(const std::string& method, const std::string& path, const std::string& body = {}) {
        Reply r;
        app::ai::HttpRequest req;
        req.method = method;
        req.url = config_.api + "/repos/" + config_.repo + path;
        req.headers = {{"Authorization", "Bearer " + token_},
                       {"Accept", "application/vnd.github+json"},
                       {"X-GitHub-Api-Version", "2022-11-28"}};
        if (!body.empty() || method != "GET") req.headers.emplace_back("Content-Type", "application/json; charset=utf-8");
        req.body = body;
        req.connectTimeoutSeconds = 20;
        req.idleTimeoutSeconds = 60;
        app::ai::HttpSink sink;
        bool tooBig = false;
        sink.onData = [&](std::string_view chunk) {
            if (r.body.size() + chunk.size() > kMaxReply) {
                tooBig = true;
                return;
            }
            r.body.append(chunk);
        };
        auto client = app::ai::makeHttpClient();
        const auto resp = client->perform(req, sink);
        // Le jeton ne reste pas dans la requete (elle va etre detruite) : efface.
        for (auto& [k, v] : req.headers) sodium_memzero(v.data(), v.size());
        if (!resp.ok()) {
            app::ai::Url u;
            (void)app::ai::parseUrl(config_.api, u, nullptr);
            r.error = resp.error.empty() ? app::ai::failureText(resp.failure, u.host) : resp.error;
            return r;
        }
        if (tooBig) {
            r.error = "la r\xC3\xA9ponse de GitHub est trop grande";
            return r;
        }
        r.status = resp.status;
        r.rateLeft = resp.header("x-ratelimit-remaining");
        (void)core::json::parse(r.body, r.json, nullptr);
        return r;
    }

    // Une reponse refusee, en francais : le jeton n'y est jamais.
    [[nodiscard]] std::string explain(const Reply& r, const std::string& what) const {
        if (!r.error.empty()) return "pas de r\xC3\xA9ponse de GitHub pour " + what + " : " + r.error;
        const std::string m = r.message().empty() ? std::string() : " (GitHub : " + r.message() + ")";
        switch (r.status) {
            case 401:
                return "GitHub refuse le jeton (expir\xC3\xA9, r\xC3\xA9voqu\xC3\xA9 ou mal copi\xC3\xA9) : enregistre-le de nouveau (XPG-License-Admin github-jeton)" + m;
            case 403:
                if (r.rateLeft == "0") return "GitHub limite les requ\xC3\xAAtes de ce jeton pour l'instant : r\xC3\xA9" "essaie dans une heure" + m;
                return "le jeton n'a pas le droit de " + what
                       + " (il lui faut, sur ce seul d\xC3\xA9p\xC3\xB4t : Contents en lecture et \xC3\xA9" "criture)" + m;
            case 404:
                return "introuvable sur GitHub pour " + what + " : le d\xC3\xA9p\xC3\xB4t " + config_.repo
                       + " n'existe pas, ou le jeton ne le voit pas (un jeton \xC3\xA0 grain fin doit nommer ce d\xC3\xA9p\xC3\xB4t)" + m;
            case 301:
            case 302:
            case 307:
            case 308:
                return "GitHub renvoie ailleurs pour " + what + " : le d\xC3\xA9p\xC3\xB4t a \xC3\xA9t\xC3\xA9 renomm\xC3\xA9 ou transf\xC3\xA9r\xC3\xA9 ; mets son nouveau nom (github-config --depot PROPRIETAIRE/NOM)" + m;
            default:
                return "GitHub a r\xC3\xA9pondu " + std::to_string(r.status) + " pour " + what + m;
        }
    }

    [[nodiscard]] const Config& config() const noexcept { return config_; }

private:
    Config      config_;
    std::string token_;
};

std::string under(const Config& c, const std::string& rel) { return c.root.empty() ? rel : c.root + "/" + rel; }

struct Remote {
    bool                               privateRepo{false};
    std::string                        defaultBranch;
    bool                               empty{false};     // aucun commit
    std::string                        head, tree;       // le commit de la branche, son arbre
    std::string                        headDate;
    std::map<std::string, std::string> blobs;            // chemin complet -> sha du blob
    bool                               hasIndex{false};
    std::vector<Entry>                 index;
    std::string                        publishedAt, publishedBy;
};

bool getBlob(Api& api, const std::string& sha, std::string& out, std::string* why) {
    const Reply r = api.call("GET", "/git/blobs/" + sha);
    if (r.status != 200) {
        if (why) *why = api.explain(r, "lire un fichier du d\xC3\xA9p\xC3\xB4t");
        return false;
    }
    if (r.json.str("encoding") != "base64" || !fromBase64(r.json.str("content"), out)) {
        if (why) *why = "GitHub a rendu un fichier illisible (" + sha + ")";
        return false;
    }
    return true;
}

// Un chemin relatif sans "." ni ".." ni segment vide.
bool safeRelPath(std::string_view p) {
    if (p.empty() || p.front() == '/' || p.back() == '/' || p.find('\\') != std::string_view::npos) return false;
    std::size_t pos = 0;
    while (pos <= p.size()) {
        std::size_t end = p.find('/', pos);
        if (end == std::string_view::npos) end = p.size();
        const auto seg = p.substr(pos, end - pos);
        if (seg.empty() || seg == "." || seg == "..") return false;
        pos = end + 1;
    }
    return true;
}
bool knownKind(const std::string& k) { return k == "registre" || k == "revocations" || k == "licence" || k == "activations"; }

// L'index : "XPG-INDEX 1 <cle> <code>\n" puis le JSON. Faux : `why` ; `foreignKey` : l'index vient d'une autre cle.
bool readIndex(const SyncKey& key, std::string_view text, Remote& out, bool& foreignKey, std::string* why) {
    foreignKey = false;
    const auto nl = text.find('\n');
    if (nl == std::string_view::npos) {
        if (why) *why = "l'index du d\xC3\xA9p\xC3\xB4t est illisible";
        return false;
    }
    const std::string head(text.substr(0, nl));
    const std::string_view body = text.substr(nl + 1);
    std::vector<std::string> parts;
    std::size_t pos = 0;
    while (pos <= head.size()) {
        std::size_t end = head.find(' ', pos);
        if (end == std::string::npos) end = head.size();
        parts.push_back(head.substr(pos, end - pos));
        pos = end + 1;
    }
    if (parts.size() != 4 || parts[0] != "XPG-INDEX" || parts[1] != "1") {
        if (why) *why = "l'index du d\xC3\xA9p\xC3\xB4t n'est pas du bon format";
        return false;
    }
    if (parts[2] != syncKeyId(key)) {
        foreignKey = true;
        if (why)
            *why = "ce d\xC3\xA9p\xC3\xB4t a \xC3\xA9t\xC3\xA9 publi\xC3\xA9 avec une AUTRE cl\xC3\xA9 de synchro (" + parts[2] + ") que celle de ce poste (" + syncKeyId(key)
                   + ") : redonne-lui la bonne avec github-config --code-secours CODE --remplacer (le code not\xC3\xA9 au premier github-config)";
        return false;
    }
    const std::string mac = indexMac(key, body);
    if (parts[3].size() != mac.size() || sodium_memcmp(parts[3].data(), mac.data(), mac.size()) != 0) {
        if (why) *why = "l'index du d\xC3\xA9p\xC3\xB4t a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9 hors de l'outil (son code d'authentification est faux) : rien n'est re\xC3\xA7u ni envoy\xC3\xA9";
        return false;
    }
    Value v;
    std::string err;
    if (!core::json::parse(body, v, &err) || !v.isObject() || v.str("format") != kSyncFormat) {
        if (why) *why = "l'index du d\xC3\xA9p\xC3\xB4t est illisible (" + err + ")";
        return false;
    }
    out.publishedAt = v.str("publie_le");
    out.publishedBy = v.str("publie_par");
    if (const Value* files = v.find("fichiers"); files && files->isArray())
        for (const auto& f : files->items()) {
            Entry e;
            e.path = f.str("chemin");
            e.kind = f.str("genre");
            e.encrypted = f.flag("chiffre");
            e.sha256 = f.str("sha256");
            e.number = static_cast<std::uint64_t>(f.integer("numero"));
            (void)licensing::parseIsoTime(f.str("date"), e.date);
            e.licenseId = f.str("licence");
            e.lines = static_cast<int>(f.integer("lignes"));
            e.blob = f.str("blob");
            if (!safeRelPath(e.path) || e.sha256.size() != 64) continue;
            out.index.push_back(std::move(e));   // un genre inconnu (une version plus recente) : garde tel quel, jamais traite
        }
    out.hasIndex = true;
    return true;
}

std::string writeIndex(const SyncKey& key, const std::vector<Entry>& entries, Seconds now, const std::string& by) {
    Value v = Value::object();
    v.set("format", std::string(kSyncFormat));
    v.set("cle_synchro", syncKeyId(key));
    v.set("publie_le", licensing::isoTime(now));
    v.set("publie_par", by);
    Value files = Value::array();
    for (const auto& e : entries) {
        Value f = Value::object();
        f.set("chemin", e.path);
        f.set("genre", e.kind);
        f.set("chiffre", e.encrypted);
        f.set("sha256", e.sha256);
        if (e.kind == "registre" || e.kind == "revocations") f.set("numero", static_cast<std::uint64_t>(e.number));
        if (e.date) f.set("date", licensing::isoTime(e.date));
        if (!e.licenseId.empty()) f.set("licence", e.licenseId);
        if (e.kind == "activations") f.set("lignes", e.lines);
        if (!e.blob.empty()) f.set("blob", e.blob);
        files.push(std::move(f));
    }
    v.set("fichiers", std::move(files));
    const std::string body = core::json::write(v, true) + "\n";
    return "XPG-INDEX 1 " + syncKeyId(key) + " " + indexMac(key, body) + "\n" + body;
}

std::string readmeText(const Config& c) {
    return "# Licences XPG : la source centrale de l'\xC3\xA9" "diteur\n\n"
           "Ce d\xC3\xA9p\xC3\xB4t PRIV\xC3\x89 est tenu par l'outil de l'\xC3\xA9" "diteur (`XPG-License-Admin github-synchroniser`). "
           "Ne le modifie pas \xC3\xA0 la main : l'outil refuse un fichier chang\xC3\xA9 hors de lui.\n\n"
           "- `registre/registre.xpgreg` : les \xC3\xA9" "ditions et les fonctionnalit\xC3\xA9s, sign\xC3\xA9" "es (en clair).\n"
           "- `revocations/revocations.xpgrev.chiffre` : la liste de r\xC3\xA9vocation, sign\xC3\xA9" "e, **chiffr\xC3\xA9" "e** (une raison peut nommer un client).\n"
           "- `licences/*.xpglic.chiffre` : chaque licence sign\xC3\xA9" "e, **chiffr\xC3\xA9" "e** (elle nomme le client et ses PC).\n"
           "- `activations/activations.jsonl.chiffre` : le registre des activations, **chiffr\xC3\xA9**.\n"
           "- `index.xpgsync` : la liste des fichiers et leurs empreintes, authentifi\xC3\xA9" "e.\n\n"
           "**N'y mets jamais** la cl\xC3\xA9 priv\xC3\xA9" "e de l'\xC3\xA9" "diteur, le jeton GitHub ou le code de secours de la cl\xC3\xA9 de synchro.\n\n"
           "Un poste Admin neuf : `github-config --depot " + c.repo + " --code-secours XPGS1-...`, `github-jeton`, "
           "puis `github-synchroniser --recevoir-seulement`.\n";
}

// Lire le depot. Faux : `why` (et `foreignKey` si l'index vient d'une autre cle de synchro).
bool readRemote(Api& api, const SyncKey& key, Remote& out, bool& foreignKey, std::string* why) {
    out = Remote{};
    foreignKey = false;
    const Config& c = api.config();
    Reply r = api.call("GET", "");
    if (r.status != 200) {
        if (why) *why = api.explain(r, "lire le d\xC3\xA9p\xC3\xB4t");
        return false;
    }
    out.privateRepo = r.json.flag("private");
    out.defaultBranch = r.json.str("default_branch");
    if (!out.privateRepo) {
        if (why)
            *why = "le d\xC3\xA9p\xC3\xB4t " + c.repo
                   + " est PUBLIC : l'outil n'y publie rien. Rends-le priv\xC3\xA9 (sur GitHub : Settings > General > Change visibility) ou choisis un d\xC3\xA9p\xC3\xB4t priv\xC3\xA9";
        return false;
    }
    r = api.call("GET", "/git/ref/heads/" + c.branch);
    if (r.status == 409) {   // "Git Repository is empty."
        out.empty = true;
        return true;
    }
    if (r.status == 404) {
        if (why)
            *why = "la branche " + c.branch + " n'existe pas dans " + c.repo
                   + (out.defaultBranch.empty() ? std::string() : " (sa branche par d\xC3\xA9" "faut : " + out.defaultBranch + " ; github-config --branche " + out.defaultBranch + ")");
        return false;
    }
    if (r.status != 200) {
        if (why) *why = api.explain(r, "lire la branche " + c.branch);
        return false;
    }
    const Value* obj = r.json.find("object");
    out.head = obj ? obj->str("sha") : std::string();
    if (out.head.empty()) {
        if (why) *why = "GitHub a rendu une branche illisible";
        return false;
    }
    r = api.call("GET", "/git/commits/" + out.head);
    if (r.status != 200) {
        if (why) *why = api.explain(r, "lire le dernier commit");
        return false;
    }
    const Value* tree = r.json.find("tree");
    out.tree = tree ? tree->str("sha") : std::string();
    if (const Value* a = r.json.find("author")) out.headDate = a->str("date");
    r = api.call("GET", "/git/trees/" + out.tree + "?recursive=1");
    if (r.status != 200) {
        if (why) *why = api.explain(r, "lire la liste des fichiers");
        return false;
    }
    if (r.json.flag("truncated")) {
        if (why) *why = "le d\xC3\xA9p\xC3\xB4t a trop de fichiers pour \xC3\xAAtre lu d'un coup (GitHub tronque la liste) : utilise un d\xC3\xA9p\xC3\xB4t r\xC3\xA9serv\xC3\xA9 aux licences";
        return false;
    }
    if (const Value* items = r.json.find("tree"); items && items->isArray())
        for (const auto& it : items->items())
            if (it.str("type") == "blob") out.blobs[it.str("path")] = it.str("sha");
    const auto idx = out.blobs.find(under(c, kIndexName));
    if (idx == out.blobs.end()) return true;   // jamais synchronise : rien a recevoir
    std::string text;
    if (!getBlob(api, idx->second, text, why)) return false;
    return readIndex(key, text, out, foreignKey, why);
}

} // namespace

// ============================================================================
//  la synchro
// ============================================================================

std::string SyncReport::text() const {
    std::string t;
    if (!remoteInfo.empty()) t += "D\xC3\xA9p\xC3\xB4t : " + remoteInfo + "\n";
    if (!ok) {
        if (!received.empty()) {
            t += "Re\xC3\xA7u (d\xC3\xA9j\xC3\xA0 \xC3\xA9" "crit sur ce poste) :\n";
            for (const auto& r : received) t += "  - " + r + "\n";
        }
        return t + "Refus\xC3\xA9 : " + why + "\n";
    }
    if (initialized) t += "Le d\xC3\xA9p\xC3\xB4t \xC3\xA9tait vide : son premier commit (LISEZ-MOI.md) est fait.\n";
    if (!received.empty()) {
        t += preview ? "\xC3\x80 recevoir :\n" : "Re\xC3\xA7u :\n";
        for (const auto& r : received) t += "  - " + r + "\n";
    }
    if (!sent.empty()) {
        t += commit.empty() ? "\xC3\x80 envoyer :\n" : "Envoy\xC3\xA9 (commit " + commit.substr(0, 12) + ") :\n";
        for (const auto& s : sent) t += "  - " + s + "\n";
    }
    if (!warnings.empty()) {
        t += "Attention :\n";
        for (const auto& w : warnings) t += "  - " + w + "\n";
    }
    if (upToDate) t += "D\xC3\xA9j\xC3\xA0 \xC3\xA0 jour : le poste et le d\xC3\xA9p\xC3\xB4t ont les m\xC3\xAAmes fichiers.\n";
    return t;
}

SyncReport synchronize(const std::string& folder, const licensing::SigningKey* key, const SyncOptions& opt) {
    SyncReport rep;
    Config config;
    std::string token;
    SyncKey skey{};
    if (!crypto::init()) {
        rep.why = "libsodium ne d\xC3\xA9marre pas";
        return rep;
    }
    if (!readConfig(folder, config, &rep.why) || !loadToken(folder, token, &rep.why) || !loadSyncKey(folder, skey, &rep.why)) return rep;
    struct Wipe {
        SyncKey& k;
        ~Wipe() { sodium_memzero(k.data(), k.size()); }
    } wipeKey{skey};
    Api api(config, std::move(token));
    const auto trusted = trustedKeys(key);
    const std::string by = (opt.tool.empty() ? std::string("XPG-License-Admin") : opt.tool) + (key ? " ; cl\xC3\xA9 " + key->keyId() : std::string());
    const bool writeLocal = !opt.dryRun;
    const bool writeRemote = !opt.dryRun && !opt.receiveOnly;

    bool raced = false;
    rep.preview = opt.dryRun;
    for (int attempt = 0; attempt < 4; ++attempt) {
        rep.attempts = attempt + 1;
        if (opt.dryRun) rep.received.clear();   // sinon : ce qui a ete recu a un essai precedent est ecrit, et reste dit
        rep.sent.clear();
        rep.warnings.clear();
        if (raced) rep.warnings.push_back("un autre poste a publi\xC3\xA9 pendant l'envoi : l'outil a relu le d\xC3\xA9p\xC3\xB4t, fusionn\xC3\xA9, puis recommenc\xC3\xA9");
        Remote remote;
        bool foreignKey = false;
        if (!readRemote(api, skey, remote, foreignKey, &rep.why)) return rep;
        rep.remoteInfo = config.repo + " (priv\xC3\xA9), branche " + config.branch + (config.root.empty() ? std::string() : ", dossier " + config.root);
        if (remote.empty) rep.remoteInfo += " : vide";
        else if (!remote.publishedAt.empty()) rep.remoteInfo += ", dernier envoi le " + remote.publishedAt + (remote.publishedBy.empty() ? "" : " par " + remote.publishedBy);
        else rep.remoteInfo += " : jamais synchronis\xC3\xA9";

        // ---- recevoir ----
        std::vector<std::string> localWarnings;
        std::vector<Entry> local = localEntries(folder, localWarnings);
        const auto findLocal = [&](const std::string& path) -> const Entry* {
            for (const auto& e : local)
                if (e.path == path) return &e;
            return nullptr;
        };
        std::set<std::string> keepLocal;   // les conflits : la version du poste reste, et n'est pas envoyee
        std::set<std::string> rejected;    // refuses : l'entree de l'index reste telle quelle, le poste republie la sienne
        std::set<std::string> untrusted;   // signes par une cle que ce poste ne connait pas : ni recus, ni ecrases par plus ancien
        std::string remoteActivations;     // le registre des activations du depot, dechiffre (s'il a ete lu)
        bool remoteActivationsRead = false;
        for (const auto& e : remote.index) {
            if (!knownKind(e.kind)) continue;   // d'une version plus recente de l'outil : laisse tel quel
            const Entry* l = findLocal(e.path);
            const auto blob = remote.blobs.find(under(config, e.path));
            if (blob == remote.blobs.end()) {
                rep.warnings.push_back(e.path + " : dans l'index mais absent du d\xC3\xA9p\xC3\xB4t" + (l ? " (ce poste le republie)" : ""));
                rejected.insert(e.path);
                continue;
            }
            if (!e.blob.empty() && blob->second != e.blob) {
                rep.warnings.push_back(e.path + " refus\xC3\xA9 : il a \xC3\xA9t\xC3\xA9 chang\xC3\xA9 dans le d\xC3\xA9p\xC3\xB4t hors de l'outil" + (l ? " (ce poste republie le sien)" : ""));
                rejected.insert(e.path);
                continue;
            }
            if (l && l->sha256 == e.sha256) continue;
            if (l && e.kind != "activations" && newer(e, *l) < 0) continue;   // le poste a plus recent (l'index le dit) : il l'enverra
            // Le registre des activations : on ne recoit que les lignes qui manquent ; plus recent ou non, peu importe.
            std::string raw, plain, w;
            if (!getBlob(api, blob->second, raw, &rep.why)) return rep;
            if (e.encrypted ? !openSealed(skey, e.path, raw, plain, &w) : (plain = raw, false)) {
                rep.warnings.push_back(e.path + " refus\xC3\xA9 : " + w);
                rejected.insert(e.path);
                continue;
            }
            if (crypto::sha256Hex(plain) != e.sha256) {
                rep.warnings.push_back(e.path + " refus\xC3\xA9 : son contenu ne correspond pas \xC3\xA0 l'index (modifi\xC3\xA9 hors de l'outil)");
                rejected.insert(e.path);
                continue;
            }
            if (e.kind == "activations") {
                remoteActivations = plain;
                remoteActivationsRead = true;
                std::string localText;
                if (l && !readText(l->local, localText, &rep.why)) return rep;
                const auto missing = missingLines(localText, plain);
                if (missing.empty()) continue;
                if (writeLocal) {
                    std::error_code ec;
                    fs::create_directories(P(folder), ec);
                    std::string add;
                    if (!localText.empty() && localText.back() != '\n') add += "\n";
                    for (const auto& line : missing) add += line + "\n";
                    std::ofstream out(P(activationsPath(folder)), std::ios::binary | std::ios::app);   // a la fin : jamais reecrit
                    out.write(add.data(), static_cast<std::streamsize>(add.size()));
                    out.flush();
                    if (!out) {
                        rep.why = "impossible d'\xC3\xA9" "crire " + activationsPath(folder);
                        return rep;
                    }
                }
                rep.received.push_back("registre des activations : " + std::to_string(missing.size()) + " ligne(s) d'un autre poste");
                continue;
            }
            Entry got;
            bool verified = false;
            if (!describeSigned(plain, trusted, got, verified, &w) || got.path != e.path || got.kind != e.kind) {
                rep.warnings.push_back(e.path + " refus\xC3\xA9 : " + (w.empty() ? std::string("il ne contient pas ce que dit l'index") : w));
                rejected.insert(e.path);
                continue;
            }
            if (!trusted.empty() && !verified) {
                rep.warnings.push_back(describeEntry(got) + " pas re\xC3\xA7u : sign\xC3\xA9 par une cl\xC3\xA9 d'\xC3\xA9" "diteur que ce poste ne conna\xC3\xAEt pas (une autre cl\xC3\xA9 ? exporter-cle --garder)");
                untrusted.insert(e.path);
                continue;
            }
            if (l) {
                const int cmp = newer(got, *l);
                if (cmp < 0) continue;   // le poste a plus recent : il l'enverra
                if (cmp == 0) {
                    rep.warnings.push_back(describeEntry(got) + " : le d\xC3\xA9p\xC3\xB4t et ce poste en ont deux versions diff\xC3\xA9rentes de m\xC3\xAAme date ; ce poste garde la sienne");
                    keepLocal.insert(e.path);
                    continue;
                }
            }
            if (writeLocal && !writeFileAt(localPathOf(folder, got), plain, &rep.why)) return rep;
            rep.received.push_back(describeEntry(got) + (trusted.empty() ? " (signature non v\xC3\xA9rifi\xC3\xA9" "e : pas de cl\xC3\xA9 d'\xC3\xA9" "diteur sur ce poste)" : ""));
        }
        if (writeLocal) {
            localWarnings.clear();
            local = localEntries(folder, localWarnings);
        }
        for (const auto& w : localWarnings) rep.warnings.push_back(w);

        // ---- envoyer : ce que le depot n'a pas ----
        std::vector<const Entry*> toSend;
        for (const auto& l : local) {
            if (keepLocal.count(l.path)) continue;
            const Entry* r = nullptr;
            for (const auto& e : remote.index)
                if (e.path == l.path) r = &e;
            if (r && untrusted.count(l.path) && newer(l, *r) <= 0) continue;   // jamais un plus ancien par-dessus
            if (r && r->sha256 == l.sha256 && !rejected.count(l.path)) continue;
            if (r && l.kind == "activations" && !rejected.count(l.path)) {
                // Envoyer seulement si le poste a des lignes que le depot n'a pas (l'ordre seul ne compte pas).
                std::string mine;
                if (!readText(l.local, mine, &rep.why)) return rep;
                if (remoteActivationsRead && missingLines(remoteActivations, mine).empty()) continue;
            }
            if (r && !rejected.count(l.path) && l.kind != "activations" && newer(l, *r) < 0) continue;   // le depot a plus recent (refuse plus haut ?)
            toSend.push_back(&l);
        }
        const bool readmeMissing = !remote.empty && remote.blobs.find(under(config, kReadmeName)) == remote.blobs.end();
        for (const auto* l : toSend) rep.sent.push_back(describeEntry(*l));
        if (toSend.empty() && remote.hasIndex) {
            rep.upToDate = rep.received.empty() && rep.warnings.empty();
            rep.ok = true;
            return rep;
        }
        if (toSend.empty() && !remote.hasIndex && local.empty()) {
            rep.ok = true;
            rep.warnings.push_back("rien \xC3\xA0 publier : l'archive de ce poste est vide (signer-registre, signer-licence, archiver)");
            return rep;
        }
        if (!writeRemote) {
            rep.ok = true;
            return rep;
        }
        if (remote.empty) {
            // Le premier commit d'un depot vide : le LISEZ-MOI (l'API Contents cree la branche).
            Value body = Value::object();
            body.set("message", std::string("XPG-License-Admin : le d\xC3\xA9p\xC3\xB4t des licences"));
            body.set("content", base64Of(readmeText(config)));
            body.set("branch", config.branch);
            const Reply r = api.call("PUT", "/contents/" + under(config, kReadmeName), core::json::write(body, false));
            const bool someoneElse = r.status == 409 || r.status == 422;   // un autre poste vient de le creer
            if (r.status != 201 && r.status != 200 && !someoneElse) {
                rep.why = api.explain(r, "cr\xC3\xA9" "er le premier fichier du d\xC3\xA9p\xC3\xB4t vide")
                          + " ; tu peux aussi cr\xC3\xA9" "er le d\xC3\xA9p\xC3\xB4t sur GitHub avec \xC2\xAB Add a README file \xC2\xBB coch\xC3\xA9";
                return rep;
            }
            rep.initialized = !someoneElse;
            // GitHub peut mettre un instant a montrer la branche neuve : jusqu'a 10 s.
            for (int i = 0; i < 10; ++i) {
                if (api.call("GET", "/git/ref/heads/" + config.branch).status == 200) break;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;   // relire : le depot a maintenant sa branche
        }

        // Les blobs, puis l'arbre, le commit, la branche.
        std::vector<std::pair<std::string, std::string>> treeItems;   // chemin complet -> sha
        const auto postBlob = [&](const std::string& fullPath, const std::string& content) -> bool {
            Value b = Value::object();
            b.set("content", base64Of(content));
            b.set("encoding", std::string("base64"));
            const Reply r = api.call("POST", "/git/blobs", core::json::write(b, false));
            if (r.status != 201 || r.json.str("sha").empty()) {
                rep.why = api.explain(r, "\xC3\xA9" "crire dans le d\xC3\xA9p\xC3\xB4t");
                return false;
            }
            treeItems.emplace_back(fullPath, r.json.str("sha"));
            return true;
        };
        std::vector<Entry> newIndex;
        std::set<std::string> sentPaths;
        for (const auto* l : toSend) {
            std::string plain;
            if (!readText(l->local, plain, &rep.why)) return rep;
            const std::string content = l->encrypted ? seal(skey, l->path, plain) : plain;
            if (!postBlob(under(config, l->path), content)) return rep;
            sentPaths.insert(l->path);
        }
        std::map<std::string, std::string> blobOf(treeItems.begin(), treeItems.end());
        for (const auto& l : local)
            if (sentPaths.count(l.path)) {
                Entry e = l;
                e.blob = blobOf[under(config, l.path)];
                newIndex.push_back(std::move(e));
            }
        for (const auto& e : remote.index)
            if (!sentPaths.count(e.path)) newIndex.push_back(e);
        std::sort(newIndex.begin(), newIndex.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });
        if (!postBlob(under(config, kIndexName), writeIndex(skey, newIndex, opt.now, by))) return rep;
        if (readmeMissing && !postBlob(under(config, kReadmeName), readmeText(config))) return rep;

        Value tree = Value::object();
        tree.set("base_tree", remote.tree);
        Value items = Value::array();
        for (const auto& [path, sha] : treeItems) {
            Value it = Value::object();
            it.set("path", path);
            it.set("mode", std::string("100644"));
            it.set("type", std::string("blob"));
            it.set("sha", sha);
            items.push(std::move(it));
        }
        tree.set("tree", std::move(items));
        Reply r = api.call("POST", "/git/trees", core::json::write(tree, false));
        if (r.status != 201 || r.json.str("sha").empty()) {
            rep.why = api.explain(r, "\xC3\xA9" "crire dans le d\xC3\xA9p\xC3\xB4t");
            return rep;
        }
        // Le message : des comptes et des identifiants de licence, jamais un nom de client.
        std::string summary;
        int licences = 0;
        std::string ids;
        for (const auto* l : toSend) {
            if (l->kind == "licence") {
                ++licences;
                if (licences <= 5) ids += (ids.empty() ? "" : ", ") + l->licenseId;
            } else {
                summary += (summary.empty() ? "" : " ; ") + describeEntry(*l);
            }
        }
        if (licences) summary += (summary.empty() ? "" : " ; ") + std::to_string(licences) + " licence(s) : " + ids + (licences > 5 ? "..." : "");
        if (summary.empty()) summary = "l'index";
        Value commit = Value::object();
        commit.set("message", "XPG-License-Admin : " + summary + "\n\nPubli\xC3\xA9 par " + by + " le " + licensing::isoTime(opt.now) + ".");
        commit.set("tree", r.json.str("sha"));
        Value parents = Value::array();
        parents.push(remote.head);
        commit.set("parents", std::move(parents));
        r = api.call("POST", "/git/commits", core::json::write(commit, false));
        if (r.status != 201 || r.json.str("sha").empty()) {
            rep.why = api.explain(r, "\xC3\xA9" "crire dans le d\xC3\xA9p\xC3\xB4t");
            return rep;
        }
        const std::string commitSha = r.json.str("sha");
        Value ref = Value::object();
        ref.set("sha", commitSha);
        ref.set("force", false);
        r = api.call("PATCH", "/git/refs/heads/" + config.branch, core::json::write(ref, false));
        if (r.status == 200) {
            rep.commit = commitSha;
            rep.ok = true;
            return rep;
        }
        if (r.status == 422 || r.status == 409) {
            // Un autre poste a publie entre-temps (la tete de la branche a change) : relire, fusionner, recommencer.
            // Sinon (une branche protegee, une regle du depot) : le dire, avec le message de GitHub.
            const Reply now = api.call("GET", "/git/ref/heads/" + config.branch);
            const Value* o = now.json.find("object");
            const std::string head = now.status == 200 && o ? o->str("sha") : std::string();
            if (!head.empty() && head != remote.head) {
                raced = true;
                continue;
            }
            rep.why = api.explain(r, "avancer la branche " + config.branch) + " ; la branche est-elle prot\xC3\xA9g\xC3\xA9" "e (Settings > Branches, Rules) ?";
            return rep;
        }
        rep.why = api.explain(r, "avancer la branche " + config.branch);
        return rep;
    }
    rep.why = "le d\xC3\xA9p\xC3\xB4t change sans arr\xC3\xAAt (d'autres postes y publient) : r\xC3\xA9" "essaie dans un moment";
    return rep;
}

} // namespace license_admin::github
