#include "HmiCrypto.hpp"

#include <cmath>
#include <cstring>
#include <random>

namespace hmi {

namespace {

constexpr std::uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Sha256 {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint8_t  block[64]{};
    std::size_t   used{0};
    std::uint64_t total{0};

    void compress(const std::uint8_t* b) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(b[i * 4]) << 24) | (std::uint32_t(b[i * 4 + 1]) << 16)
                 | (std::uint32_t(b[i * 4 + 2]) << 8) | std::uint32_t(b[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + S1 + ch + kK[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & bb) ^ (a & c) ^ (bb & c);
            const std::uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const std::uint8_t* data, std::size_t size) {
        total += size;
        while (size > 0) {
            const std::size_t take = std::min(size, sizeof block - used);
            std::memcpy(block + used, data, take);
            used += take;
            data += take;
            size -= take;
            if (used == sizeof block) { compress(block); used = 0; }
        }
    }
    Digest finish() {
        const std::uint64_t bits = total * 8;
        const std::uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (used != 56) update(&zero, 1);
        std::uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        update(len, 8);
        Digest out{};
        for (int i = 0; i < 8; ++i)
            for (int k = 0; k < 4; ++k) out[static_cast<std::size_t>(i * 4 + k)] = static_cast<std::uint8_t>(h[i] >> (24 - 8 * k));
        return out;
    }
};

} // namespace

Digest sha256(const std::uint8_t* data, std::size_t size) {
    Sha256 s;
    s.update(data, size);
    return s.finish();
}

Digest sha256(std::string_view text) {
    return sha256(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

Digest hmacSha256(std::string_view key, std::string_view message) {
    std::uint8_t k[64]{};
    if (key.size() > 64) {
        const auto d = sha256(key);
        std::memcpy(k, d.data(), d.size());
    } else {
        std::memcpy(k, key.data(), key.size());
    }
    std::uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    Sha256 inner;
    inner.update(ipad, 64);
    inner.update(reinterpret_cast<const std::uint8_t*>(message.data()), message.size());
    const auto innerDigest = inner.finish();
    Sha256 outer;
    outer.update(opad, 64);
    outer.update(innerDigest.data(), innerDigest.size());
    return outer.finish();
}

std::string toHex(const std::uint8_t* data, std::size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}

std::string toHex(const Digest& d) { return toHex(d.data(), d.size()); }

std::string fromHex(std::string_view hex) {
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (hex.size() % 2) return {};
    std::string out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int a = nibble(hex[i]), b = nibble(hex[i + 1]);
        if (a < 0 || b < 0) return {};
        out += static_cast<char>((a << 4) | b);
    }
    return out;
}

std::string randomHex(std::size_t bytes) {
    std::random_device rd;
    std::vector<std::uint8_t> buf(bytes);
    for (auto& b : buf) b = static_cast<std::uint8_t>(rd() & 0xFF);
    return toHex(buf.data(), buf.size());
}

std::string passwordHash(std::string_view saltHex, std::string_view password) {
    const std::string salt = fromHex(saltHex);
    Digest d = sha256(salt + std::string(password));
    for (int i = 1; i < kPasswordRounds; ++i) {
        std::string round(reinterpret_cast<const char*>(d.data()), d.size());
        round += salt;
        round += password;
        d = sha256(round);
    }
    return toHex(d);
}

bool passwordMatches(std::string_view saltHex, std::string_view hashHex, std::string_view password) {
    if (hashHex.empty()) return false;
    const std::string computed = passwordHash(saltHex, password);
    // Comparaison en temps constant : pas de fuite par la duree.
    if (computed.size() != hashHex.size()) return false;
    unsigned diff = 0;
    for (std::size_t i = 0; i < computed.size(); ++i)
        diff |= static_cast<unsigned>(static_cast<unsigned char>(computed[i]) ^ static_cast<unsigned char>(hashHex[i]));
    return diff == 0;
}

std::string dynamicCode(std::string_view secretHex, double unixSeconds, int periodS, int digits, int step) {
    const std::string key = fromHex(secretHex);
    if (key.empty() || periodS <= 0) return {};
    digits = std::max(4, std::min(9, digits));
    const long long counter = static_cast<long long>(std::floor(unixSeconds / periodS)) + step;
    std::string msg(8, '\0');
    for (int i = 0; i < 8; ++i) msg[static_cast<std::size_t>(i)] = static_cast<char>((static_cast<unsigned long long>(counter) >> (56 - 8 * i)) & 0xFF);
    const auto mac = hmacSha256(key, msg);
    const int offset = mac[mac.size() - 1] & 0x0F;
    const std::uint32_t bin = (std::uint32_t(mac[static_cast<std::size_t>(offset)] & 0x7F) << 24)
                            | (std::uint32_t(mac[static_cast<std::size_t>(offset + 1)]) << 16)
                            | (std::uint32_t(mac[static_cast<std::size_t>(offset + 2)]) << 8)
                            | std::uint32_t(mac[static_cast<std::size_t>(offset + 3)]);
    std::uint32_t mod = 1;
    for (int i = 0; i < digits; ++i) mod *= 10;
    std::string code = std::to_string(bin % mod);
    while (static_cast<int>(code.size()) < digits) code.insert(code.begin(), '0');
    return code;
}

bool dynamicCodeMatches(std::string_view secretHex, std::string_view code, double unixSeconds, int periodS, int digits) {
    std::string typed;
    for (char c : code) if (c != ' ') typed += c;
    for (int step = -1; step <= 1; ++step)
        if (!typed.empty() && dynamicCode(secretHex, unixSeconds, periodS, digits, step) == typed) return true;
    return false;
}

int dynamicCodeRemaining(double unixSeconds, int periodS) {
    if (periodS <= 0) return 0;
    const double into = std::fmod(unixSeconds, static_cast<double>(periodS));
    return static_cast<int>(std::ceil(periodS - into));
}

} // namespace hmi
