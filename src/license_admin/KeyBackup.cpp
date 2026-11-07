// =============================================================================
//  license_admin/KeyBackup.cpp - 1.32.0 : la sauvegarde chiffree de la cle de
//  l'editeur (voir KeyBackup.hpp). JAMAIS LIVRE A UN CLIENT.
// =============================================================================
#include "KeyBackup.hpp"

#include "Admin.hpp"

#include "../core/Json.hpp"
#include "../deploy/Crypto.hpp"
#include "../licensing/SignedFile.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <vector>

namespace license_admin {

// L'acces a la partie secrete d'une cle (ami de licensing::SigningKey) : ici seulement.
struct KeyBackupAccess {
    static const licensing::crypto::SecretKey& secret(const licensing::SigningKey& k) noexcept { return k.secret_; }
    static std::unique_ptr<licensing::SigningKey> create(const std::string& path, const licensing::KeyFileSpec& spec, const std::string& name, bool test,
                                                         const licensing::crypto::PublicKey& pub, const licensing::crypto::SecretKey& secret,
                                                         std::string* why) {
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(std::u8string(path.begin(), path.end())), ec)) {
            if (why) *why = spec.what + " existe d\xC3\xA9j\xC3\xA0 (" + path + ") : elle n'est jamais remplac\xC3\xA9" "e";
            return nullptr;
        }
        std::unique_ptr<licensing::SigningKey> key(new licensing::SigningKey());
        key->path_ = path;
        key->name_ = name;
        key->test_ = test;
        key->pub_ = pub;
        key->secret_ = secret;
        if (!key->save(spec, why)) return nullptr;
        return licensing::SigningKey::load(path, spec, why);   // relue du disque : c'est elle qui fait foi
    }
};

namespace {

namespace crypto = deploy::crypto;
using core::json::Value;

constexpr unsigned long long kMinOps = 2, kMaxOps = 16;
constexpr std::size_t kMinMem = 64u * 1024u * 1024u, kMaxMem = 1024u * 1024u * 1024u;
constexpr const char* kPayloadFormat = "xpg-sauvegarde-cle/1";

std::size_t utf8Length(std::string_view s) {
    std::size_t n = 0;
    for (char c : s)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

struct Header {
    std::string keyId;
    unsigned long long ops{0};
    std::size_t mem{0};
    std::array<unsigned char, crypto_pwhash_SALTBYTES> salt{};
    std::string text;      // les trois lignes, telles quelles (les donnees associees)
    std::string body;      // le base64
};

bool parseHeader(std::string_view backup, Header& h, std::string* why) {
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos < backup.size() && lines.size() < 3) {
        std::size_t end = backup.find('\n', pos);
        if (end == std::string_view::npos) break;
        std::string line(backup.substr(pos, end - pos));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        pos = end + 1;
    }
    if (lines.size() != 3 || lines[0] != std::string(kKeyBackupMarker) + " 1" || lines[1].rfind("cle=", 0) != 0 || lines[2].rfind("argon2id ", 0) != 0) {
        if (why) *why = "ce n'est pas une sauvegarde de cl\xC3\xA9 de l'outil (" + std::string(kKeyBackupMarker) + " 1)";
        return false;
    }
    h.keyId = lines[1].substr(4);
    unsigned long long ops = 0, mem = 0;
    char saltHex[64] = {};
    if (std::sscanf(lines[2].c_str(), "argon2id ops=%llu mem=%llu sel=%40s", &ops, &mem, saltHex) != 3) {
        if (why) *why = "la ligne des param\xC3\xA8tres de la sauvegarde est illisible";
        return false;
    }
    crypto::Bytes salt;
    if (!crypto::unhex(saltHex, salt, h.salt.size())) {
        if (why) *why = "le sel de la sauvegarde est illisible";
        return false;
    }
    std::copy(salt.begin(), salt.end(), h.salt.begin());
    if (ops < kMinOps || ops > kMaxOps || mem < kMinMem || mem > kMaxMem) {
        if (why) *why = "les param\xC3\xA8tres de la sauvegarde sont refus\xC3\xA9s (trop faibles ou d\xC3\xA9mesur\xC3\xA9s) : " + lines[2];
        return false;
    }
    h.ops = ops;
    h.mem = static_cast<std::size_t>(mem);
    h.text = lines[0] + "\n" + lines[1] + "\n" + lines[2] + "\n";
    h.body = std::string(backup.substr(pos));
    return true;
}

bool deriveKey(std::string_view phrase, const Header& h, std::array<unsigned char, 32>& out, std::string* why) {
    if (crypto_pwhash(out.data(), out.size(), phrase.data(), phrase.size(), h.salt.data(), h.ops, h.mem, crypto_pwhash_ALG_ARGON2ID13) != 0) {
        if (why) *why = "pas assez de m\xC3\xA9moire pour d\xC3\xA9river la cl\xC3\xA9 de la phrase (" + std::to_string(h.mem / (1024 * 1024)) + " Mio)";
        return false;
    }
    return true;
}

// Ouvrir : la charge utile (JSON) ou faux.
bool openBackup(std::string_view backup, std::string_view phrase, Header& h, Value& payload, std::string* why) {
    if (!crypto::init()) {
        if (why) *why = "libsodium ne d\xC3\xA9marre pas";
        return false;
    }
    if (!parseHeader(backup, h, why)) return false;
    std::string body;
    for (char c : h.body)
        if (c != '\n' && c != '\r' && c != ' ') body += c;
    std::string box(body.size(), '\0');
    std::size_t len = 0;
    if (sodium_base642bin(reinterpret_cast<unsigned char*>(box.data()), box.size(), body.data(), body.size(), nullptr, &len, nullptr,
                          sodium_base64_VARIANT_ORIGINAL) != 0
        || len < crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + crypto_aead_xchacha20poly1305_ietf_ABYTES) {
        if (why) *why = "la sauvegarde est ab\xC3\xAEm\xC3\xA9" "e";
        return false;
    }
    box.resize(len);
    std::array<unsigned char, 32> k{};
    if (!deriveKey(phrase, h, k, why)) return false;
    const auto* nonce = reinterpret_cast<const unsigned char*>(box.data());
    const std::size_t clen = box.size() - crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    std::string plain(clen, '\0');
    unsigned long long plen = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(reinterpret_cast<unsigned char*>(plain.data()), &plen, nullptr,
                                                              nonce + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, clen,
                                                              reinterpret_cast<const unsigned char*>(h.text.data()), h.text.size(), nonce, k.data());
    sodium_memzero(k.data(), k.size());
    if (rc != 0) {
        sodium_memzero(plain.data(), plain.size());
        if (why) *why = "la phrase de passe est fausse, ou la sauvegarde a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9" "e";
        return false;
    }
    plain.resize(static_cast<std::size_t>(plen));
    std::string err;
    const bool parsed = core::json::parse(plain, payload, &err) && payload.isObject() && payload.str("format") == kPayloadFormat;
    sodium_memzero(plain.data(), plain.size());
    if (!parsed) {
        if (why) *why = "le contenu de la sauvegarde est illisible";
        return false;
    }
    return true;
}

// La paire de la charge utile, verifiee (la secrete porte la publique, et signe pour elle ; l'identifiant concorde).
bool pairOf(const Value& payload, const Header& h, crypto::PublicKey& pub, crypto::SecretKey& secret, std::string* why) {
    crypto::Bytes p, s;
    std::string sh = payload.str("secrete");
    const bool ok = crypto::unhex(payload.str("publique"), p, 32) && crypto::unhex(sh, s, 64);
    sodium_memzero(sh.data(), sh.size());
    if (!ok) {
        if (!s.empty()) sodium_memzero(s.data(), s.size());
        if (why) *why = "la cl\xC3\xA9 de la sauvegarde est illisible";
        return false;
    }
    std::copy(p.begin(), p.end(), pub.begin());
    std::copy(s.begin(), s.end(), secret.begin());
    sodium_memzero(s.data(), s.size());
    static constexpr std::string_view kProbe = "xpg-sauvegarde-controle";
    const bool consistent = std::equal(pub.begin(), pub.end(), secret.begin() + 32)
                            && crypto::verify(pub, kProbe, crypto::sign(secret, kProbe)) && licensing::keyIdOf(pub) == h.keyId;
    if (!consistent) {
        sodium_memzero(secret.data(), secret.size());
        if (why) *why = "la cl\xC3\xA9 de la sauvegarde ne se tient pas (publique, secr\xC3\xA8te ou identifiant)";
        return false;
    }
    return true;
}

} // namespace

bool acceptablePassphrase(std::string_view phrase, std::string* why) {
    if (utf8Length(phrase) < 12) {
        if (why) *why = "la phrase de passe doit avoir 12 caract\xC3\xA8res au moins (une phrase de quelques mots est le plus s\xC3\xBBr)";
        return false;
    }
    std::set<char> distinct(phrase.begin(), phrase.end());
    const bool digitsOnly = std::all_of(phrase.begin(), phrase.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (distinct.size() < 5 || digitsOnly) {
        if (why) *why = "la phrase de passe est trop simple (des caract\xC3\xA8res vari\xC3\xA9s, pas seulement des chiffres)";
        return false;
    }
    return true;
}

bool writeKeyBackup(const licensing::SigningKey& key, std::string_view phrase, std::string& out, std::string* why, const KeyBackupOptions& options) {
    if (!crypto::init()) {
        if (why) *why = "libsodium ne d\xC3\xA9marre pas";
        return false;
    }
    if (!acceptablePassphrase(phrase, why)) return false;
    Header h;
    h.keyId = key.keyId();
    h.ops = options.opsLimit ? options.opsLimit : crypto_pwhash_OPSLIMIT_MODERATE;
    h.mem = options.memLimit ? options.memLimit : crypto_pwhash_MEMLIMIT_MODERATE;
    if (h.ops < kMinOps || h.ops > kMaxOps || h.mem < kMinMem || h.mem > kMaxMem) {
        if (why) *why = "param\xC3\xA8tres Argon2id hors des bornes";
        return false;
    }
    crypto::randomBytes(h.salt.data(), h.salt.size());
    h.text = std::string(kKeyBackupMarker) + " 1\ncle=" + h.keyId + "\nargon2id ops=" + std::to_string(h.ops) + " mem=" + std::to_string(h.mem)
             + " sel=" + crypto::hex(h.salt.data(), h.salt.size()) + "\n";
    std::array<unsigned char, 32> k{};
    if (!deriveKey(phrase, h, k, why)) return false;
    Value payload = Value::object();
    payload.set("format", std::string(kPayloadFormat));
    payload.set("nom", key.name());
    payload.set("essai", key.test());
    payload.set("publique", key.publicKeyHex());
    const auto& secret = KeyBackupAccess::secret(key);
    std::string secretHex = crypto::hex(secret.data(), secret.size());
    payload.set("secrete", secretHex);
    sodium_memzero(secretHex.data(), secretHex.size());
    std::string plain = core::json::write(payload, false);
    if (Value* s = payload.find("secrete")) {
        std::string& v = const_cast<std::string&>(s->asString());
        sodium_memzero(v.data(), v.size());
    }
    std::string box(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES, '\0');
    auto* nonce = reinterpret_cast<unsigned char*>(box.data());
    crypto::randomBytes(nonce, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    unsigned long long clen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(nonce + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, &clen, reinterpret_cast<const unsigned char*>(plain.data()),
                                               plain.size(), reinterpret_cast<const unsigned char*>(h.text.data()), h.text.size(), nullptr, nonce, k.data());
    sodium_memzero(k.data(), k.size());
    sodium_memzero(plain.data(), plain.size());
    box.resize(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + static_cast<std::size_t>(clen));
    std::string b64(sodium_base64_ENCODED_LEN(box.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
    sodium_bin2base64(b64.data(), b64.size(), reinterpret_cast<const unsigned char*>(box.data()), box.size(), sodium_base64_VARIANT_ORIGINAL);
    b64.resize(std::strlen(b64.c_str()));
    out = h.text;
    for (std::size_t i = 0; i < b64.size(); i += 76) out += b64.substr(i, 76) + "\n";
    return true;
}

bool describeKeyBackup(std::string_view backup, std::string& keyId, std::string& text, std::string* why) {
    Header h;
    if (!parseHeader(backup, h, why)) return false;
    keyId = h.keyId;
    text = "sauvegarde de la cl\xC3\xA9 " + h.keyId + " (Argon2id : " + std::to_string(h.ops) + " passes, " + std::to_string(h.mem / (1024 * 1024)) + " Mio)";
    return true;
}

bool checkKeyBackup(std::string_view backup, std::string_view phrase, std::string& keyId, std::string& name, std::string* why) {
    Header h;
    Value payload;
    if (!openBackup(backup, phrase, h, payload, why)) return false;
    crypto::PublicKey pub{};
    crypto::SecretKey secret{};
    const bool ok = pairOf(payload, h, pub, secret, why);
    sodium_memzero(secret.data(), secret.size());
    if (!ok) return false;
    keyId = h.keyId;
    name = payload.str("nom");
    return true;
}

std::unique_ptr<licensing::SigningKey> restoreKeyBackup(const std::string& folder, std::string_view backup, std::string_view phrase, std::string* why) {
    Header h;
    Value payload;
    if (!openBackup(backup, phrase, h, payload, why)) return nullptr;
    crypto::PublicKey pub{};
    crypto::SecretKey secret{};
    if (!pairOf(payload, h, pub, secret, why)) return nullptr;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(std::u8string(folder.begin(), folder.end())), ec);
    auto key = KeyBackupAccess::create(publisherKeyPath(folder), publisherKeySpec(), payload.str("nom"), payload.flag("essai"), pub, secret, why);
    sodium_memzero(secret.data(), secret.size());
    return key;
}

} // namespace license_admin
