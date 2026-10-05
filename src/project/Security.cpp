#include "Security.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <random>

namespace project {
namespace {

// --- SHA-256, FIPS 180-4 ---------------------------------------------------
// Written out rather than pulled in: a hash has no key to protect, is fully
// specified, and has published test vectors - which Security's own test checks
// against. This is the one piece of cryptography it is reasonable to implement
// by hand; a cipher would not be.
constexpr std::uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void transform(std::uint32_t state[8], const std::uint8_t block[64]) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24)
             | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16)
             | (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8)
             |  static_cast<std::uint32_t>(block[i * 4 + 3]);
    for (int i = 16; i < 64; ++i) {
        const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; ++i) {
        const auto S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const auto ch = (e & f) ^ (~e & g);
        const auto t1 = h + S1 + ch + kK[i] + w[i];
        const auto S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const auto maj = (a & b) ^ (a & c) ^ (b & c);
        const auto t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

} // namespace

Digest sha256(std::string_view data) {
    std::uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                              0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
    std::size_t i = 0;
    for (; i + 64 <= data.size(); i += 64) transform(state, bytes + i);

    // Padding: 0x80, zeroes, then the length in bits as a big-endian 64-bit.
    std::uint8_t tail[128] = {};
    const auto rest = data.size() - i;
    std::memcpy(tail, bytes + i, rest);
    tail[rest] = 0x80;
    const std::size_t tailLen = (rest < 56) ? 64 : 128;
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    for (int b = 0; b < 8; ++b)
        tail[tailLen - 1 - b] = static_cast<std::uint8_t>((bits >> (8 * b)) & 0xFF);
    for (std::size_t off = 0; off < tailLen; off += 64) transform(state, tail + off);

    Digest out{};
    for (int w = 0; w < 8; ++w)
        for (int b = 0; b < 4; ++b)
            out[static_cast<std::size_t>(w * 4 + b)] =
                static_cast<std::uint8_t>((state[w] >> (24 - 8 * b)) & 0xFF);
    return out;
}

std::string toHex(const Digest& d) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (auto b : d) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

bool fromHex(std::string_view hex, Digest& out) {
    if (hex.size() != 64) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < 32; ++i) {
        const int hi = nibble(hex[i * 2]), lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return true;
}

std::string deriveKey(std::string_view secret, std::string_view salt, int iterations) {
    // Iterated so that guessing a password costs real time. 200 000 rounds is
    // roughly 100 ms here, which is unnoticeable when opening a project and
    // expensive when trying a dictionary.
    std::string acc;
    acc.reserve(salt.size() + secret.size() + 8);
    acc.append(salt);
    acc.append(secret);
    Digest d = sha256(acc);
    for (int i = 1; i < std::max(1, iterations); ++i) {
        std::string round(reinterpret_cast<const char*>(d.data()), d.size());
        round.append(salt);
        d = sha256(round);
    }
    return toHex(d);
}

std::string makeSalt() {
    std::random_device rd;
    std::mt19937_64 gen(rd() ^ static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<std::uint64_t> dist;
    Digest seed{};
    const auto a = dist(gen), b = dist(gen), c = dist(gen), e = dist(gen);
    std::memcpy(seed.data(), &a, 8);
    std::memcpy(seed.data() + 8, &b, 8);
    std::memcpy(seed.data() + 16, &c, 8);
    std::memcpy(seed.data() + 24, &e, 8);
    return toHex(seed);
}

// ---------------------------------------------------------------------------
std::string_view toString(State s) noexcept {
    switch (s) {
        case State::New:    return "NEW";
        case State::Dev:    return "DEV";
        case State::Finish: return "FINISH";
        case State::Lock:   return "LOCK";
    }
    return "NEW";
}

State stateFromString(std::string_view s) noexcept {
    if (s == "DEV")    return State::Dev;
    if (s == "FINISH") return State::Finish;
    if (s == "LOCK")   return State::Lock;
    return State::New;
}

LockRecord makeLock(std::string_view password, std::string_view masterKey) {
    LockRecord r;
    r.salt         = makeSalt();
    r.passwordHash = deriveKey(password, r.salt, r.iterations);
    if (!masterKey.empty()) r.masterHash = deriveKey(masterKey, r.salt, r.iterations);
    return r;
}

namespace {
// Comparison that does not leak how many characters matched through timing.
bool equalsConstantTime(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}
} // namespace

bool checkPassword(const LockRecord& r, std::string_view password) {
    if (!r.valid()) return true;                 // an invalid record locks nothing
    return equalsConstantTime(deriveKey(password, r.salt, r.iterations), r.passwordHash);
}

bool checkMaster(const LockRecord& r, std::string_view masterKey) {
    if (r.masterHash.empty() || masterKey.empty()) return false;
    return equalsConstantTime(deriveKey(masterKey, r.salt, r.iterations), r.masterHash);
}

// ---------------------------------------------------------------------------
std::string MasterKey::normaliseMac(std::string_view mac) {
    std::string out;
    for (char c : mac) {
        if (c == ':' || c == '-' || c == '.' || c == ' ') continue;
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

bool MasterKey::looksLikeMac(std::string_view mac) {
    const auto n = normaliseMac(mac);
    if (n.size() != 12) return false;
    return std::all_of(n.begin(), n.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
    });
}

void MasterKey::setFromMac(std::string_view mac) {
    // A fixed salt here on purpose: the same MAC must produce the same key on
    // every machine and in every project, or it could not act as a master.
    stored_ = deriveKey(normaliseMac(mac), "xpg-analyzer-master-v1");
}

bool MasterKey::matches(std::string_view mac) const {
    if (stored_.empty()) return false;
    return equalsConstantTime(deriveKey(normaliseMac(mac), "xpg-analyzer-master-v1"), stored_);
}

} // namespace project
