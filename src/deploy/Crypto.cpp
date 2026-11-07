// =============================================================================
//  deploy/Crypto.cpp - 1.28.0 : la facade sur libsodium (voir Crypto.hpp)
// =============================================================================
#include "Crypto.hpp"

#include <sodium.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace deploy::crypto {

namespace {
std::filesystem::path P(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
} // namespace

bool init() noexcept {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] { ok = sodium_init() >= 0; });
    return ok;
}

Digest sha256(const void* data, std::size_t size) noexcept {
    Digest d{};
    crypto_hash_sha256(d.data(), static_cast<const unsigned char*>(data), static_cast<unsigned long long>(size));
    return d;
}

Digest sha256(std::string_view data) noexcept { return sha256(data.data(), data.size()); }

std::string sha256Hex(std::string_view data) { return hex(sha256(data)); }

bool sha256File(const std::string& path, std::string& hexDigest, std::uint64_t* size, std::string* why) {
    std::ifstream in(P(path), std::ios::binary);
    if (!in) {
        if (why) *why = "impossible de lire " + path;
        return false;
    }
    crypto_hash_sha256_state st;
    crypto_hash_sha256_init(&st);
    std::vector<char> buf(1u << 16);
    std::uint64_t total = 0;
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto got = in.gcount();
        if (got <= 0) break;
        crypto_hash_sha256_update(&st, reinterpret_cast<const unsigned char*>(buf.data()), static_cast<unsigned long long>(got));
        total += static_cast<std::uint64_t>(got);
    }
    if (in.bad()) {
        if (why) *why = "lecture interrompue : " + path;
        return false;
    }
    Digest d{};
    crypto_hash_sha256_final(&st, d.data());
    hexDigest = hex(d);
    if (size) *size = total;
    return true;
}

std::string hex(const void* data, std::size_t size) {
    static const char* digits = "0123456789abcdef";
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(digits[p[i] >> 4]);
        out.push_back(digits[p[i] & 15]);
    }
    return out;
}

bool unhex(std::string_view text, Bytes& out, std::size_t expected) {
    if (text.size() % 2 != 0 || (expected && text.size() != expected * 2)) return false;
    const auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    Bytes r;
    r.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int hi = val(text[i]), lo = val(text[i + 1]);
        if (hi < 0 || lo < 0) return false;
        r.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
    }
    out = std::move(r);
    return true;
}

std::string fingerprintText(const Digest& d, std::size_t groups) {
    std::string out;
    const auto add = [&](std::size_t i) {
        if (!out.empty()) out += ':';
        char b[3];
        std::snprintf(b, sizeof b, "%02X", d[i]);
        out += b;
    };
    if (groups == 0 || groups * 2 >= d.size()) {
        for (std::size_t i = 0; i < d.size(); ++i) add(i);
        return out;
    }
    for (std::size_t i = 0; i < groups; ++i) add(i);
    out += ":\xE2\x80\xA6";
    for (std::size_t i = d.size() - groups; i < d.size(); ++i) add(i);
    return out;
}

SignKeys newSignKeys() {
    SignKeys k;
    crypto_sign_ed25519_keypair(k.pub.data(), k.secret.data());
    return k;
}

Signature sign(const SecretKey& secret, std::string_view message) noexcept {
    Signature s{};
    crypto_sign_ed25519_detached(s.data(), nullptr, reinterpret_cast<const unsigned char*>(message.data()),
                                 static_cast<unsigned long long>(message.size()), secret.data());
    return s;
}

bool verify(const PublicKey& pub, std::string_view message, const Signature& sig) noexcept {
    return crypto_sign_ed25519_verify_detached(sig.data(), reinterpret_cast<const unsigned char*>(message.data()),
                                               static_cast<unsigned long long>(message.size()), pub.data()) == 0;
}

std::string base64(const void* data, std::size_t size) {
    if (!init()) return {};
    std::string out(sodium_base64_ENCODED_LEN(size, sodium_base64_VARIANT_ORIGINAL), '\0');
    sodium_bin2base64(out.data(), out.size(), static_cast<const unsigned char*>(data), size, sodium_base64_VARIANT_ORIGINAL);
    out.resize(std::strlen(out.c_str()));
    return out;
}

bool unbase64(std::string_view text, Bytes& out) {
    out.clear();
    if (!init()) return false;
    std::string clean;
    clean.reserve(text.size());
    for (char c : text)
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') clean.push_back(c);
    out.resize(clean.size() / 4 * 3 + 3);
    std::size_t len = 0;
    const char* end = nullptr;
    if (sodium_base642bin(out.data(), out.size(), clean.data(), clean.size(), nullptr, &len, &end, sodium_base64_VARIANT_ORIGINAL) != 0 ||
        end != clean.data() + clean.size()) {
        out.clear();
        return false;
    }
    out.resize(len);
    return true;
}

bool seal(const PublicKey& pub, std::string_view plain, Bytes& out) {
    out.clear();
    if (!init()) return false;
    unsigned char x[crypto_box_PUBLICKEYBYTES];
    if (crypto_sign_ed25519_pk_to_curve25519(x, pub.data()) != 0) return false;
    out.resize(plain.size() + crypto_box_SEALBYTES);
    if (crypto_box_seal(out.data(), reinterpret_cast<const unsigned char*>(plain.data()), static_cast<unsigned long long>(plain.size()), x) != 0) {
        out.clear();
        return false;
    }
    return true;
}

bool openSeal(const SecretKey& secret, const Bytes& box, std::string& plain) {
    plain.clear();
    if (!init() || box.size() < crypto_box_SEALBYTES) return false;
    unsigned char xs[crypto_box_SECRETKEYBYTES];
    unsigned char xp[crypto_box_PUBLICKEYBYTES];
    // La partie publique est dans les 32 derniers octets de la cle secrete Ed25519 de libsodium.
    if (crypto_sign_ed25519_sk_to_curve25519(xs, secret.data()) != 0 || crypto_sign_ed25519_pk_to_curve25519(xp, secret.data() + 32) != 0) {
        sodium_memzero(xs, sizeof xs);
        return false;
    }
    std::string out(box.size() - crypto_box_SEALBYTES, '\0');
    const int rc = crypto_box_seal_open(reinterpret_cast<unsigned char*>(out.data()), box.data(), static_cast<unsigned long long>(box.size()), xp, xs);
    sodium_memzero(xs, sizeof xs);
    if (rc != 0) {
        sodium_memzero(out.data(), out.size());
        return false;
    }
    plain = std::move(out);
    return true;
}

void randomBytes(void* out, std::size_t size) noexcept { randombytes_buf(out, size); }

std::string randomDigits(int count) {
    std::string out;
    for (int i = 0; i < count; ++i) out.push_back(static_cast<char>('0' + randombytes_uniform(10)));
    return out;
}

void wipe(void* data, std::size_t size) noexcept { sodium_memzero(data, size); }

bool protectedHere() noexcept {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool protect(const Bytes& plain, Bytes& out, bool machine, std::string* why) {
#ifdef _WIN32
    DATA_BLOB in{static_cast<DWORD>(plain.size()), const_cast<BYTE*>(plain.data())};
    DATA_BLOB res{};
    const DWORD flags = CRYPTPROTECT_UI_FORBIDDEN | (machine ? CRYPTPROTECT_LOCAL_MACHINE : 0);
    if (!CryptProtectData(&in, L"XPG", nullptr, nullptr, nullptr, flags, &res)) {
        if (why) *why = "DPAPI refuse de proteger la cle (erreur " + std::to_string(GetLastError()) + ")";
        return false;
    }
    out.assign(res.pbData, res.pbData + res.cbData);
    LocalFree(res.pbData);
    return true;
#else
    (void)machine;
    (void)why;
    out = plain;
    return true;
#endif
}

bool unprotect(const Bytes& in, Bytes& plain, std::string* why) {
#ifdef _WIN32
    DATA_BLOB blob{static_cast<DWORD>(in.size()), const_cast<BYTE*>(in.data())};
    DATA_BLOB res{};
    if (!CryptUnprotectData(&blob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &res)) {
        if (why) *why = "DPAPI ne rend pas la cle : elle a ete protegee sur un autre PC ou pour un autre compte (erreur "
                        + std::to_string(GetLastError()) + ")";
        return false;
    }
    plain.assign(res.pbData, res.pbData + res.cbData);
    SecureZeroMemory(res.pbData, res.cbData);
    LocalFree(res.pbData);
    return true;
#else
    (void)why;
    plain = in;
    return true;
#endif
}

bool replaceFile(const std::string& path, std::string_view data, bool ownerOnly, std::string* why) {
    namespace fs = std::filesystem;
    const auto fail = [why](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    if (!init()) return fail("le hasard du syst\xC3\xA8me est indisponible (libsodium)");
    const fs::path target = P(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    // Un nom que personne ne peut prevoir, cree s'il n'existe pas encore : un fichier ou un lien pose
    // d'avance sous ce nom fait echouer l'ecriture au lieu d'etre suivi.
    unsigned char r[8];
    randomBytes(r, sizeof r);
    fs::path tmp = target;
    tmp += P("." + hex(r, sizeof r) + ".tmp");
#ifdef _WIN32
    (void)ownerOnly;   // sous Windows : les droits du dossier (herites)
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return fail("impossible d'\xC3\xA9" "crire " + path + " (erreur " + std::to_string(GetLastError()) + ")");
    bool written = true;
    for (std::size_t done = 0; done < data.size() && written;) {
        const DWORD part = static_cast<DWORD>(std::min<std::size_t>(data.size() - done, 1u << 30));
        DWORD n = 0;
        written = WriteFile(h, data.data() + done, part, &n, nullptr) && n > 0;
        done += n;
    }
    CloseHandle(h);
    if (!written) {
        DeleteFileW(tmp.c_str());
        return fail("\xC3\xA9" "criture incompl\xC3\xA8te : " + path + " (disque plein ?)");
    }
    // Une seule operation : MoveFileEx remplace la cible.
    if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD e = GetLastError();
        DeleteFileW(tmp.c_str());
        return fail("impossible de remplacer " + path + " (erreur " + std::to_string(e) + ")");
    }
    return true;
#else
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, ownerOnly ? 0600 : 0666);
    if (fd < 0) return fail("impossible d'\xC3\xA9" "crire " + path + " (" + std::strerror(errno) + ")");
    bool written = true;
    for (std::size_t done = 0; done < data.size() && written;) {
        const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
        if (n < 0 && errno == EINTR) continue;
        written = n > 0;
        if (written) done += static_cast<std::size_t>(n);
    }
    if (::close(fd) != 0) written = false;
    if (!written) {
        fs::remove(tmp, ec);
        return fail("\xC3\xA9" "criture incompl\xC3\xA8te : " + path + " (disque plein ?)");
    }
    fs::rename(tmp, target, ec);   // rename(2) remplace la cible d'un coup
    if (ec) {
        const std::string m = ec.message();
        fs::remove(tmp, ec);
        return fail("impossible de remplacer " + path + " (" + m + ")");
    }
    return true;
#endif
}

bool writePrivateFile(const std::string& path, const Bytes& data, std::string* why) {
    return replaceFile(path, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), true, why);
}

bool readFile(const std::string& path, Bytes& out, std::string* why, std::uint64_t maxSize) {
    std::ifstream in(P(path), std::ios::binary);
    if (!in) {
        if (why) *why = "impossible de lire " + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::uint64_t>(in.tellg());
    if (size > maxSize) {
        if (why) *why = path + " : trop gros";
        return false;
    }
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size) in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    if (!in) {
        if (why) *why = "lecture incomplete : " + path;
        return false;
    }
    return true;
}

} // namespace deploy::crypto
