// =============================================================================
//  deploy/TlsIdentity.cpp - 1.28.0 : le certificat de la machine (voir TlsIdentity.hpp)
// =============================================================================
#include "TlsIdentity.hpp"

#include "Paths.hpp"
#include "Tls.hpp"

#include <mbedtls/ecp.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

#include <filesystem>
#include <mutex>
#include <string>

namespace deploy {

namespace {

std::filesystem::path P(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

// Un nom de certificat sur : lettres, chiffres, tiret, point, espace (une virgule ou un << = >>
// changeraient le sens de "CN=...").
std::string safeName(const std::string& s) {
    std::string out;
    for (const char c : s) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'
                        || c == '.' || c == ' ';
        out.push_back(ok ? c : '_');
        if (out.size() >= 60) break;
    }
    return out.empty() ? std::string("XPG") : out;
}

// La paire et le certificat, sous le verrou de mbedTLS.
bool makeIdentity(const std::string& commonName, crypto::Bytes& certDer, crypto::Bytes& keyDer, std::string* why) {
    if (!tls::ready(why)) return false;
    std::lock_guard<std::mutex> lock(tls::libraryLock());
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    mbedtls_x509write_cert crt;
    mbedtls_x509write_crt_init(&crt);
    std::vector<unsigned char> buf(4096);
    bool ok = false;
    std::string err;
    do {
        int rc = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
        if (rc == 0) rc = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), tls::randomCallback, nullptr);
        if (rc != 0) {
            err = "cl\xC3\xA9 ECDSA P-256 : " + tls::errorText(rc);
            break;
        }
        unsigned char serial[16];
        crypto::randomBytes(serial, sizeof serial);
        serial[0] = static_cast<unsigned char>((serial[0] & 0x7F) | 0x01);   // positif, jamais nul
        const std::string name = "CN=" + safeName(commonName) + ",O=XPG";
        mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&crt, &key);
        mbedtls_x509write_crt_set_issuer_key(&crt, &key);
        rc = mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof serial);
        if (rc == 0) rc = mbedtls_x509write_crt_set_subject_name(&crt, name.c_str());
        if (rc == 0) rc = mbedtls_x509write_crt_set_issuer_name(&crt, name.c_str());
        // La date ne compte pas (l'empreinte est epinglee) : une validite large, sans echeance a gerer.
        if (rc == 0) rc = mbedtls_x509write_crt_set_validity(&crt, "20250101000000", "20991231235959");
        if (rc == 0) rc = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
        if (rc != 0) {
            err = "certificat : " + tls::errorText(rc);
            break;
        }
        int len = mbedtls_x509write_crt_der(&crt, buf.data(), buf.size(), tls::randomCallback, nullptr);
        if (len <= 0) {
            err = "certificat : " + tls::errorText(len);
            break;
        }
        certDer.assign(buf.end() - len, buf.end());   // ecrit a la fin du tampon
        len = mbedtls_pk_write_key_der(&key, buf.data(), buf.size());
        if (len <= 0) {
            err = "cl\xC3\xA9 : " + tls::errorText(len);
            break;
        }
        keyDer.assign(buf.end() - len, buf.end());
        ok = true;
    } while (false);
    crypto::wipe(buf.data(), buf.size());
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    return ok || fail(why, "impossible de cr\xC3\xA9" "er l'identit\xC3\xA9 TLS de ce PC (" + err + ")");
}

// Le certificat se lit, la cle se lit, et ils vont ensemble.
bool checkPair(const crypto::Bytes& certDer, const crypto::Bytes& keyDer, std::string* why) {
    if (!tls::ready(why)) return false;
    std::lock_guard<std::mutex> lock(tls::libraryLock());
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    int rc = mbedtls_x509_crt_parse_der(&crt, certDer.data(), certDer.size());
    std::string err;
    if (rc != 0) err = "certificat illisible (" + tls::errorText(rc) + ")";
    if (rc == 0) {
        rc = mbedtls_pk_parse_key(&key, keyDer.data(), keyDer.size(), nullptr, 0, tls::randomCallback, nullptr);
        if (rc != 0) err = "cl\xC3\xA9 illisible (" + tls::errorText(rc) + ")";
    }
    if (rc == 0) {
        rc = mbedtls_pk_check_pair(&crt.pk, &key, tls::randomCallback, nullptr);
        if (rc != 0) err = "la cl\xC3\xA9 ne va pas avec le certificat";
    }
    mbedtls_pk_free(&key);
    mbedtls_x509_crt_free(&crt);
    return rc == 0 || fail(why, err);
}

} // namespace

TlsIdentity::~TlsIdentity() {
    if (!key_.empty()) crypto::wipe(key_.data(), key_.size());
}

std::string TlsIdentity::machineId() const { return machineIdOf(fp_); }

std::string machineIdOf(const crypto::Digest& fp) {
    static const char* digits = "0123456789ABCDEF";
    std::string out = "M-";
    for (int i = 0; i < 2; ++i) {
        out.push_back(digits[fp[static_cast<std::size_t>(i)] >> 4]);
        out.push_back(digits[fp[static_cast<std::size_t>(i)] & 15]);
    }
    return out;
}

std::string machineIdOfHex(const std::string& fingerprintHex) {
    crypto::Bytes b;
    if (!crypto::unhex(fingerprintHex, b, 32)) return {};
    crypto::Digest d{};
    std::copy(b.begin(), b.end(), d.begin());
    return machineIdOf(d);
}

bool TlsIdentity::generate(const std::string& commonName, TlsIdentity& out, std::string* why) {
    TlsIdentity id;
    if (!makeIdentity(commonName, id.cert_, id.key_, why)) return false;
    id.fp_ = crypto::sha256(id.cert_.data(), id.cert_.size());
    out = std::move(id);
    return true;
}

bool TlsIdentity::loadOrCreate(const std::string& dir, const std::string& baseName, bool machineScope,
                               const std::string& commonName, TlsIdentity& out, std::string* why) {
    namespace fs = std::filesystem;
    if (!crypto::init()) return fail(why, "le hasard du syst\xC3\xA8me est indisponible (libsodium)");
    const std::string certPath = joinPath(dir, baseName + ".cer");
    const std::string keyPath = joinPath(dir, baseName + ".cle");
    std::error_code ec;
    const bool haveCert = fs::exists(P(certPath), ec);
    const bool haveKey = fs::exists(P(keyPath), ec);
    TlsIdentity id;
    if (!haveCert && !haveKey) {
        // Le premier lancement : la paire, le certificat, la cle protegee.
        if (!makeIdentity(commonName, id.cert_, id.key_, why)) return false;
        crypto::Bytes sealed;
        std::string w;
        if (!crypto::protect(id.key_, sealed, machineScope, &w)) return fail(why, "cl\xC3\xA9 TLS non prot\xC3\xA9g\xC3\xA9" "e : " + w);
        fs::create_directories(P(dir), ec);
        if (!crypto::writePrivateFile(keyPath, sealed, &w)) return fail(why, w);
        if (!crypto::writePrivateFile(certPath, id.cert_, &w)) return fail(why, w);
    } else {
        const std::string advice = " : efface " + certPath + " et " + keyPath
                                   + " pour en cr\xC3\xA9" "er une nouvelle (les PC appair\xC3\xA9s devront l'\xC3\xAA" "tre de nouveau)";
        if (!haveCert || !haveKey)
            return fail(why, "l'identit\xC3\xA9 TLS de ce PC est incompl\xC3\xA8te (" + std::string(haveCert ? "cl\xC3\xA9 absente" : "certificat absent")
                                 + ")" + advice);
        crypto::Bytes sealed;
        std::string w;
        if (!crypto::readFile(certPath, id.cert_, &w, 64 * 1024) || !crypto::readFile(keyPath, sealed, &w, 64 * 1024))
            return fail(why, w);
        if (!crypto::unprotect(sealed, id.key_, &w)) return fail(why, "l'identit\xC3\xA9 TLS de ce PC est illisible (" + w + ")" + advice);
        if (!checkPair(id.cert_, id.key_, &w)) return fail(why, "l'identit\xC3\xA9 TLS de ce PC est invalide (" + w + ")" + advice);
    }
    id.fp_ = crypto::sha256(id.cert_.data(), id.cert_.size());
    out = std::move(id);
    return true;
}

} // namespace deploy
