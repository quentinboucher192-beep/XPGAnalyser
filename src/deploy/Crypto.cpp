// =============================================================================
//  deploy/Crypto.cpp - 1.28.0 : la facade sur libsodium (voir Crypto.hpp)
// =============================================================================
#include "Crypto.hpp"

#include <sodium.h>

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
#include <sys/stat.h>
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

bool writePrivateFile(const std::string& path, const Bytes& data, std::string* why) {
    namespace fs = std::filesystem;
    const fs::path target = P(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    const fs::path tmp = target.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (why) *why = "impossible d'ecrire " + path;
            return false;
        }
#ifndef _WIN32
        ::chmod(tmp.c_str(), 0600);
#endif
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) {
            if (why) *why = "ecriture incomplete : " + path;
            return false;
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
    }
    if (ec) {
        if (why) *why = "impossible de remplacer " + path + " : " + ec.message();
        return false;
    }
    return true;
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
