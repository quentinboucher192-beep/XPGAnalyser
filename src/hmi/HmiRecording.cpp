// =============================================================================
//  hmi/HmiRecording.cpp - 1.14.0 (M4) : les enregistrements (voir le .hpp)
// -----------------------------------------------------------------------------
//  LA PISTE, ENREGISTREMENT PAR ENREGISTREMENT. Chacun commence par un octet (son
//  genre) ; les nombres sont des entiers variables (7 bits par octet), les
//  instants des millisecondes depuis le debut, les textes leur longueur puis
//  leurs octets (UTF-8), une valeur son type (sim::Type) puis son contenu.
//      1 cle        rang, genre, cle, nom lu, identifiant de la variable, type declare
//      2 valeurs    instant, nombre, (rang, valeur)...
//      3 image      instant, nombre, (rang, valeur)...     toutes les valeurs connues
//      4 ecran      instant, vue, parametres, popups (vue, parametres, x, y), langue, theme
//      5 proprietes instant, nombre, (vue, objet, cle, valeur)...
//      6 alarmes    instant, nombre, (nom, message, groupe, priorite, etats, heures, qui)...
//      7 pointeur   instant, vue, x, y, dedans, boutons
//      8 evenement  instant, genre, a, b, x, y
//      9 fin        instant
//
//  UN BLOC : "XRB1", ses drapeaux (1 compresse, 2 chiffre), son numero, l'instant
//  de sa fin (ms), la longueur des enregistrements, la longueur stockee, le CRC-32
//  de ce qui est stocke ; puis ce qui est stocke.
//
//  LE FICHIER (format 2) : [en-tete local entete.json][JSON complete de blancs]
//  [en-tete local piste.bin][blocs][repertoire du zip][fin du repertoire]. Le
//  .part en cours a deja les deux en-tetes locaux (la piste a 0 octet) : l'arret
//  ecrit l'en-tete final a sa place, la taille et le CRC de la piste, puis le
//  repertoire a la fin. Le format 1 (le .part "XRECPART", la piste chiffree par
//  le mot de passe lui-meme) se relit encore.
// =============================================================================
#include "HmiRecording.hpp"

#include "HmiCrypto.hpp"
#include "../../third_party/miniz.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <new>
#include <set>
#include <sstream>

#if defined(_WIN32)
#  include <fcntl.h>
#  include <io.h>
#  include <sys/stat.h>
#else
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

namespace hmi::rec {

namespace fs = std::filesystem;

namespace {

constexpr char        kBlockMagic[4] = {'X', 'R', 'B', '1'};
constexpr std::size_t kBlockHead = 4 + 1 + 4 + 4 + 4 + 4 + 4;
constexpr char        kPartMagic[8] = {'X', 'R', 'E', 'C', 'P', 'A', 'R', 'T'};
constexpr std::uint8_t kDeflated = 1, kCiphered = 2;
constexpr std::uint32_t kMaxBlock = 64u << 20;     // 64 Mio : au-dela, un bloc est faux
constexpr std::size_t   kHeaderRoom = 16u << 10;   // la place de l'en-tete au debut du fichier (completee de blancs)
constexpr std::uint64_t kMaxHeader = 1u << 20;     // 1 Mio : au-dela, un en-tete est faux
constexpr std::uint64_t kMaxFile = 0xFFF00000ull;  // un zip sans zip64 : un peu moins de 4 Gio

std::atomic<std::uint64_t> g_opens{0}, g_bytes{0};
std::atomic<std::uint64_t> g_maxRaw{1ull << 30}, g_maxItems{12'000'000};

enum Rec : std::uint8_t { RKey = 1, RValues = 2, RKeyframe = 3, RScreen = 4, ROverrides = 5, RAlarms = 6, RPointer = 7, REvent = 8, REnd = 9 };

// ---- les octets ----------------------------------------------------------------
void putVar(std::string& o, std::uint64_t v) {
    while (v >= 0x80) {
        o.push_back(static_cast<char>((v & 0x7F) | 0x80));
        v >>= 7;
    }
    o.push_back(static_cast<char>(v));
}
void putZig(std::string& o, std::int64_t v) { putVar(o, (static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63)); }
void putF64(std::string& o, double d) {
    std::uint64_t b = 0;
    std::memcpy(&b, &d, 8);
    for (int i = 0; i < 8; ++i) o.push_back(static_cast<char>((b >> (8 * i)) & 0xFF));
}
void putStr(std::string& o, std::string_view s) {
    putVar(o, s.size());
    o.append(s.data(), s.size());
}
void put32(std::string& o, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void put16(std::string& o, std::uint32_t v) {
    o.push_back(static_cast<char>(v & 0xFF));
    o.push_back(static_cast<char>((v >> 8) & 0xFF));
}
std::uint32_t get32(const char* p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(p[i])) << (8 * i);
    return v;
}
std::uint32_t get16(const char* p) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(p[0])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 8);
}
// Un chemin en UTF-8 <-> std::filesystem (C++20 : u8path est deprecie, u8string rend un std::u8string).
fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::uint64_t ms(double t) { return t <= 0 ? 0 : static_cast<std::uint64_t>(std::llround(t * 1000.0)); }

void putValue(std::string& o, const sim::Value& v) {
    o.push_back(static_cast<char>(v.type()));
    switch (v.type()) {
        case sim::Type::Unknown: break;
        case sim::Type::Bool: o.push_back(v.isTruthy() ? 1 : 0); break;
        case sim::Type::Real: putF64(o, v.asReal()); break;
        case sim::Type::String: putStr(o, v.asString()); break;
        default: putZig(o, v.asInteger()); break;
    }
}

struct In {
    const char* p;
    const char* e;
    bool        ok{true};
    In(const char* a, const char* b) : p(a), e(b) {}
    [[nodiscard]] bool more() const { return ok && p < e; }
    std::uint8_t u8() {
        if (p >= e) { ok = false; return 0; }
        return static_cast<std::uint8_t>(*p++);
    }
    std::uint64_t var() {
        std::uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (p >= e) { ok = false; return 0; }
            const auto b = static_cast<std::uint8_t>(*p++);
            v |= static_cast<std::uint64_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) return v;
        }
        ok = false;
        return 0;
    }
    std::int64_t zig() {
        const std::uint64_t u = var();
        return static_cast<std::int64_t>(u >> 1) ^ -static_cast<std::int64_t>(u & 1);
    }
    double f64() {
        if (e - p < 8) { ok = false; p = e; return 0; }
        std::uint64_t b = 0;
        for (int i = 0; i < 8; ++i) b |= static_cast<std::uint64_t>(static_cast<unsigned char>(p[i])) << (8 * i);
        p += 8;
        double d = 0;
        std::memcpy(&d, &b, 8);
        return d;
    }
    std::string str() {
        const std::uint64_t n = var();
        if (!ok || n > static_cast<std::uint64_t>(e - p)) { ok = false; p = e; return {}; }
        std::string s(p, static_cast<std::size_t>(n));
        p += n;
        return s;
    }
    double time() { return static_cast<double>(var()) / 1000.0; }
    sim::Value value() {
        const auto t = static_cast<sim::Type>(u8());
        switch (t) {
            case sim::Type::Unknown: return {};
            case sim::Type::Bool: return sim::Value::boolean(u8() != 0);
            case sim::Type::Real: return sim::Value::real(f64());
            case sim::Type::String: return sim::Value::text(str());
            case sim::Type::Time: return sim::Value::time(zig());
            default:
                if (static_cast<int>(t) > static_cast<int>(sim::Type::LWord)) { ok = false; return {}; }
                return sim::Value::integer(t, zig());
        }
    }
};

// Deux valeurs pareilles : meme type, meme contenu (un reel par ses bits : NaN egal a NaN).
bool sameValue(const sim::Value& a, const sim::Value& b) {
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case sim::Type::Unknown: return true;
        case sim::Type::Bool: return a.isTruthy() == b.isTruthy();
        case sim::Type::Real: {
            const double x = a.asReal(), y = b.asReal();
            return std::memcmp(&x, &y, sizeof x) == 0;
        }
        case sim::Type::String: return a.asString() == b.asString();
        default: return a.asInteger() == b.asInteger();
    }
}

// ---- compresser, chiffrer --------------------------------------------------------
std::string deflate(const std::string& raw) {
    if (raw.empty()) return {};
    std::size_t outLen = 0;
    void* p = tdefl_compress_mem_to_heap(raw.data(), raw.size(), &outLen, TDEFL_DEFAULT_MAX_PROBES);
    if (!p) return {};
    std::string out(static_cast<const char*>(p), outLen);
    mz_free(p);
    return out;
}
// Ce que deflate peut donner au plus a partir de `stored` octets (1032 pour 1, plus un peu) : une taille annoncee
// au-dela est fausse - on ne l'alloue pas.
std::uint64_t inflateBound(std::uint64_t stored) { return stored * 1032u + 1024u; }
bool inflate(const std::string& z, std::size_t rawLen, std::string& out) {
    if (rawLen > inflateBound(z.size())) return false;
    out.assign(rawLen, '\0');
    if (rawLen == 0) return true;
    const std::size_t n = tinfl_decompress_mem_to_mem(out.data(), rawLen, z.data(), z.size(), 0);
    return n == rawLen;
}
std::uint32_t crcOf(const std::string& data) {
    return static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(data.data()), data.size()));
}
// La cle tiree du mot de passe : SHA-256 itere (comme un mot de passe d'utilisateur) du mot de passe et du sel de la
// cle. Format 1 : elle chiffre la piste ; format 2 : elle chiffre la cle du fichier.
std::string deriveKey(const Header& h, const std::string& password) {
    if (password.empty() || h.keySalt.empty()) return {};
    return fromHex(passwordHash(h.keySalt, password));
}
// La cle du fichier chiffree (ou dechiffree) : XOR avec HMAC-SHA-256(cle du mot de passe, nombre unique | "cle").
std::string wrapKey(const std::string& fileKey, const std::string& passwordKey, const std::string& nonce) {
    const Digest d = hmacSha256(passwordKey, nonce + "|cle");
    std::string out = fileKey;
    for (std::size_t i = 0; i < out.size() && i < d.size(); ++i) out[i] = static_cast<char>(out[i] ^ d[i]);
    return out;
}
// Le flux : HMAC-SHA-256(cle, nombre unique | numero du bloc | compteur), 32 octets a la fois, XOR.
void xorStream(std::string& data, const std::string& key, const std::string& nonce, std::uint32_t seq) {
    if (key.empty()) return;
    std::string msg = nonce;
    msg.push_back('|');
    put32(msg, seq);
    const std::size_t at = msg.size();
    for (std::uint64_t ctr = 0, off = 0; off < data.size(); ++ctr, off += 32) {
        msg.resize(at);
        for (int i = 0; i < 8; ++i) msg.push_back(static_cast<char>((ctr >> (8 * i)) & 0xFF));
        const Digest d = hmacSha256(key, msg);
        for (std::size_t k = 0; k < 32 && off + k < data.size(); ++k) data[off + k] = static_cast<char>(data[off + k] ^ d[k]);
    }
}

std::string makeBlock(const std::string& raw, std::uint32_t seq, double tEnd, const std::string& key, const std::string& nonce) {
    std::string stored = deflate(raw);
    std::uint8_t flags = kDeflated;
    if (stored.empty() && !raw.empty()) {
        stored = raw;
        flags = 0;
    }
    if (!key.empty()) {
        xorStream(stored, key, nonce, seq);
        flags |= kCiphered;
    }
    std::string out(kBlockMagic, 4);
    out.push_back(static_cast<char>(flags));
    put32(out, seq);
    put32(out, static_cast<std::uint32_t>(std::min<std::uint64_t>(ms(tEnd), 0xFFFFFFFFu)));
    put32(out, static_cast<std::uint32_t>(raw.size()));
    put32(out, static_cast<std::uint32_t>(stored.size()));
    put32(out, crcOf(stored));
    return out + stored;
}

struct BlockView {
    std::uint8_t  flags{0};
    std::uint32_t seq{0}, tEnd{0}, rawLen{0};
    std::string   stored;
    std::size_t   size{0};   // tete comprise
};
// Le bloc qui commence a `at` ; faux : abime, coupe, ou rien.
bool blockAt(const std::string& data, std::size_t at, BlockView& b) {
    if (data.size() < at + kBlockHead || std::memcmp(data.data() + at, kBlockMagic, 4) != 0) return false;
    const char* p = data.data() + at;
    b.flags = static_cast<std::uint8_t>(p[4]);
    b.seq = get32(p + 5);
    b.tEnd = get32(p + 9);
    b.rawLen = get32(p + 13);
    const std::uint32_t storedLen = get32(p + 17);
    const std::uint32_t crc = get32(p + 21);
    if (b.rawLen > kMaxBlock || storedLen > kMaxBlock || data.size() < at + kBlockHead + storedLen) return false;
    b.stored.assign(p + kBlockHead, storedLen);
    if (crcOf(b.stored) != crc) return false;
    b.size = kBlockHead + storedLen;
    return true;
}
// Les enregistrements d'un bloc (dechiffre, decompresse) ; faux : la cle ne va pas, ou abime (une longueur que
// deflate ne peut pas donner n'est pas allouee).
bool openBlock(const BlockView& b, const std::string& key, const std::string& nonce, std::string& raw) {
    std::string stored = b.stored;
    if (b.flags & kCiphered) {
        if (key.empty()) return false;
        xorStream(stored, key, nonce, b.seq);
    }
    if (b.flags & kDeflated) return inflate(stored, b.rawLen, raw);
    raw = std::move(stored);
    return raw.size() == b.rawLen;
}
// La tete d'un bloc (kBlockHead octets) : faux si ce n'en est pas une.
bool blockHead(const char* p, BlockView& b, std::uint32_t& storedLen, std::uint32_t& crc) {
    if (std::memcmp(p, kBlockMagic, 4) != 0) return false;
    b.flags = static_cast<std::uint8_t>(p[4]);
    b.seq = get32(p + 5);
    b.tEnd = get32(p + 9);
    b.rawLen = get32(p + 13);
    storedLen = get32(p + 17);
    crc = get32(p + 21);
    if (b.rawLen > kMaxBlock || storedLen > kMaxBlock) return false;
    if ((b.flags & kDeflated) && b.rawLen > inflateBound(storedLen)) return false;
    return true;
}
// Le meme bloc, chiffre autrement (ou plus).
std::string recipher(const BlockView& b, const std::string& oldKey, const std::string& oldNonce, const std::string& newKey,
                     const std::string& newNonce, bool* ok) {
    std::string stored = b.stored;
    std::uint8_t flags = b.flags;
    if (flags & kCiphered) {
        if (oldKey.empty()) { if (ok) *ok = false; return {}; }
        xorStream(stored, oldKey, oldNonce, b.seq);
        flags = static_cast<std::uint8_t>(flags & ~kCiphered);
    }
    if (!newKey.empty()) {
        xorStream(stored, newKey, newNonce, b.seq);
        flags |= kCiphered;
    }
    std::string out(kBlockMagic, 4);
    out.push_back(static_cast<char>(flags));
    put32(out, b.seq);
    put32(out, b.tEnd);
    put32(out, b.rawLen);
    put32(out, static_cast<std::uint32_t>(stored.size()));
    put32(out, crcOf(stored));
    if (ok) *ok = true;
    return out + stored;
}

// ---- le zip ------------------------------------------------------------------------
struct DosStamp { std::uint32_t time{0}, date{(1 << 5) | 1}; };
DosStamp dosNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    DosStamp d;
    d.time = static_cast<std::uint32_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
    d.date = static_cast<std::uint32_t>(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
    return d;
}
// Un en-tete local d'entree stockee (UTF-8).
std::string localHeader(std::string_view name, std::uint32_t crc, std::uint32_t size, const DosStamp& d) {
    std::string out;
    put32(out, 0x04034b50u);
    put16(out, 20); put16(out, 0x0800); put16(out, 0);
    put16(out, d.time); put16(out, d.date);
    put32(out, crc); put32(out, size); put32(out, size);
    put16(out, static_cast<std::uint32_t>(name.size())); put16(out, 0);
    out.append(name.data(), name.size());
    return out;
}
// Le repertoire du zip (une ligne par entree : son nom, son CRC, sa taille, la place de son en-tete local) et sa fin.
struct CentralItem {
    std::string   name;
    std::uint32_t crc{0}, size{0}, offset{0};
};
std::string centralAndEnd(const std::vector<CentralItem>& items, std::uint32_t at, const DosStamp& d) {
    std::string central;
    for (const auto& it : items) {
        put32(central, 0x02014b50u);
        put16(central, 20); put16(central, 20); put16(central, 0x0800); put16(central, 0);
        put16(central, d.time); put16(central, d.date);
        put32(central, it.crc); put32(central, it.size); put32(central, it.size);
        put16(central, static_cast<std::uint32_t>(it.name.size())); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0); put32(central, 0); put32(central, it.offset);
        central += it.name;
    }
    std::string out = central;
    put32(out, 0x06054b50u);
    put16(out, 0); put16(out, 0);
    put16(out, static_cast<std::uint32_t>(items.size())); put16(out, static_cast<std::uint32_t>(items.size()));
    put32(out, static_cast<std::uint32_t>(central.size())); put32(out, at);
    put16(out, 0);
    return out;
}
std::string padded(const std::string& json, std::size_t room) { return json.size() >= room ? json : json + std::string(room - json.size(), ' '); }

// Un zip a entrees stockees (la piste est deja compressee bloc par bloc) - le format 1 (un .part d'avant).
std::string zipStored(const std::vector<std::pair<std::string, std::string>>& files) {
    const DosStamp d = dosNow();
    std::string out, central;
    for (const auto& [name, data] : files) {
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        const std::uint32_t crc = crcOf(data);
        put32(out, 0x04034b50u);
        put16(out, 20); put16(out, 0x0800); put16(out, 0);          // version, UTF-8, stocke
        put16(out, d.time); put16(out, d.date);
        put32(out, crc); put32(out, static_cast<std::uint32_t>(data.size())); put32(out, static_cast<std::uint32_t>(data.size()));
        put16(out, static_cast<std::uint32_t>(name.size())); put16(out, 0);
        out += name;
        out += data;
        put32(central, 0x02014b50u);
        put16(central, 20); put16(central, 20); put16(central, 0x0800); put16(central, 0);
        put16(central, d.time); put16(central, d.date);
        put32(central, crc); put32(central, static_cast<std::uint32_t>(data.size())); put32(central, static_cast<std::uint32_t>(data.size()));
        put16(central, static_cast<std::uint32_t>(name.size())); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0); put32(central, 0); put32(central, offset);
        central += name;
    }
    const std::uint32_t at = static_cast<std::uint32_t>(out.size());
    out += central;
    put32(out, 0x06054b50u);
    put16(out, 0); put16(out, 0);
    put16(out, static_cast<std::uint32_t>(files.size())); put16(out, static_cast<std::uint32_t>(files.size()));
    put32(out, static_cast<std::uint32_t>(central.size())); put32(out, at);
    put16(out, 0);
    return out;
}
bool readAll(const std::string& path, std::string& out) {
    std::ifstream in(P(path), std::ios::binary);
    if (!in) return false;
    ++g_opens;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    g_bytes += out.size();
    return true;
}
bool writeAll(const std::string& path, const std::string& data, std::string* why) {
    std::error_code ec;
    const fs::path target = P(path);
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    const fs::path tmp = P(path + ".tmp");
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) {
            if (why) *why = "impossible d'\xC3\xA9" "crire " + path;
            return false;
        }
        o.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!o) {
            if (why) *why = "\xC3\xA9" "criture interrompue : " + path;
            return false;
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
    }
    if (ec) {
        if (why) *why = "impossible de remplacer " + path + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

// ---- les fichiers, par morceaux --------------------------------------------------------
std::FILE* openFile(const std::string& path, const char* mode) {
#if defined(_WIN32)
    std::wstring m;
    for (const char* c = mode; *c; ++c) m.push_back(static_cast<wchar_t>(*c));
    return _wfopen(P(path).wstring().c_str(), m.c_str());
#else
    return std::fopen(P(path).string().c_str(), mode);
#endif
}
bool seekTo(std::FILE* f, std::uint64_t at) {
#if defined(_WIN32)
    return _fseeki64(f, static_cast<long long>(at), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(at), SEEK_SET) == 0;
#endif
}
bool writeAt(std::FILE* f, std::uint64_t at, const std::string& data) {
    return seekTo(f, at) && std::fwrite(data.data(), 1, data.size(), f) == data.size();
}
std::string le32(std::uint32_t v) {
    std::string o;
    put32(o, v);
    return o;
}

// Un fichier lu par morceaux (rien n'est lu en entier) ; fileStats le compte.
class InFile {
public:
    explicit InFile(const std::string& path) : in_(P(path), std::ios::binary) {
        if (!in_) return;
        ++g_opens;
        in_.seekg(0, std::ios::end);
        const auto end = in_.tellg();
        size_ = end < 0 ? 0 : static_cast<std::uint64_t>(end);
        ok_ = true;
    }
    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
    bool read(std::uint64_t at, std::uint64_t n, std::string& out) {
        if (!ok_ || at > size_ || n > size_ - at) return false;
        out.resize(static_cast<std::size_t>(n));
        in_.clear();
        in_.seekg(static_cast<std::streamoff>(at));
        in_.read(out.data(), static_cast<std::streamsize>(n));
        const bool done = static_cast<std::uint64_t>(in_.gcount()) == n;
        g_bytes += static_cast<std::uint64_t>(in_.gcount());
        return done;
    }

private:
    std::ifstream in_;
    std::uint64_t size_{0};
    bool          ok_{false};
};

// Une entree du zip : ou sont ses donnees.
struct ZipEntry {
    std::string   name;
    std::uint32_t method{0}, flags{0}, crc{0};
    std::uint64_t csize{0}, usize{0}, local{0}, data{0};
};
// L'en-tete local a `at` (et son nom) ; faux : ce n'en est pas un.
bool localAt(InFile& f, std::uint64_t at, ZipEntry& e) {
    std::string h;
    if (!f.read(at, 30, h) || get32(h.data()) != 0x04034b50u) return false;
    e.flags = get16(h.data() + 6);
    e.method = get16(h.data() + 8);
    e.crc = get32(h.data() + 14);
    e.csize = get32(h.data() + 18);
    e.usize = get32(h.data() + 22);
    const std::uint32_t nlen = get16(h.data() + 26), xlen = get16(h.data() + 28);
    if (!f.read(at + 30, nlen, e.name)) return false;
    e.local = at;
    e.data = at + 30 + nlen + xlen;
    return true;
}
// Ou sont l'en-tete et la piste. LES NOTRES : entete.json en premier, piste.bin juste apres (deux petites lectures) ;
// un zip refait par un autre outil : son repertoire (a la fin). `cut` : la piste depasse la fin du fichier.
struct Layout {
    ZipEntry header, track;
    bool     hasHeader{false}, hasTrack{false}, cut{false}, fast{false};
    std::uint64_t centralAt{0};     // le repertoire (fast : derriere la piste)
};
bool layoutOf(InFile& f, Layout& L, std::string* why) {
    const auto fail = [why](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    ZipEntry first;
    if (localAt(f, 0, first) && first.name == kHeaderEntry && (first.flags & 0x08) == 0) {
        L.header = first;
        L.hasHeader = true;
        L.fast = true;
        ZipEntry second;
        const std::uint64_t at = first.data + first.csize;
        if (at <= f.size() && localAt(f, at, second) && second.name == kTrackEntry && (second.flags & 0x08) == 0) {
            L.track = second;
            L.hasTrack = true;
            L.cut = second.data + second.csize > f.size();
            L.centralAt = second.data + second.csize;
        } else {
            L.cut = true;
        }
        if (first.data + first.csize > f.size()) return fail("en-t\xC3\xAAte coup\xC3\xA9");
        return true;
    }
    // Le repertoire : la fin du repertoire dans les 64 derniers Kio.
    const std::uint64_t tail = std::min<std::uint64_t>(f.size(), 22 + 65535);
    std::string t;
    if (tail < 22 || !f.read(f.size() - tail, tail, t)) return fail("pas un enregistrement (.xrec)");
    std::size_t eocd = std::string::npos;
    for (std::size_t i = t.size() - 22 + 1; i-- > 0;)
        if (get32(t.data() + i) == 0x06054b50u) { eocd = i; break; }
    if (eocd == std::string::npos) return fail("pas un enregistrement (.xrec)");
    const std::uint32_t count = get16(t.data() + eocd + 10);
    const std::uint64_t cdSize = get32(t.data() + eocd + 12), cdAt = get32(t.data() + eocd + 16);
    if (cdSize > kMaxHeader || cdAt + cdSize > f.size()) return fail("r\xC3\xA9pertoire du zip faux");
    std::string cd;
    if (!f.read(cdAt, cdSize, cd)) return fail("r\xC3\xA9pertoire du zip illisible");
    std::size_t p = 0;
    for (std::uint32_t k = 0; k < count && p + 46 <= cd.size(); ++k) {
        if (get32(cd.data() + p) != 0x02014b50u) break;
        ZipEntry e;
        e.flags = get16(cd.data() + p + 8);
        e.method = get16(cd.data() + p + 10);
        e.crc = get32(cd.data() + p + 16);
        e.csize = get32(cd.data() + p + 20);
        e.usize = get32(cd.data() + p + 24);
        const std::uint32_t nlen = get16(cd.data() + p + 28), xlen = get16(cd.data() + p + 30), clen = get16(cd.data() + p + 32);
        e.local = get32(cd.data() + p + 42);
        if (p + 46 + nlen > cd.size()) break;
        e.name.assign(cd.data() + p + 46, nlen);
        p += 46 + nlen + xlen + clen;
        if (e.name != kHeaderEntry && e.name != kTrackEntry) continue;
        ZipEntry local;
        if (!localAt(f, e.local, local)) continue;
        e.data = local.data;
        if (e.name == kHeaderEntry) {
            L.header = e;
            L.hasHeader = true;
        } else {
            L.track = e;
            L.hasTrack = true;
            L.cut = e.data + e.csize > f.size();
        }
    }
    if (!L.hasHeader) return fail("sans en-t\xC3\xAAte");
    if (L.header.data + L.header.csize > f.size()) return fail("en-t\xC3\xAAte coup\xC3\xA9");
    return true;
}
// Le texte de l'en-tete (1 Mio au plus : un en-tete annonce plus gros est faux, rien n'est alloue).
bool headerText(InFile& f, const ZipEntry& e, std::string& out, std::string* why) {
    const auto fail = [why](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (e.csize > kMaxHeader || (e.method == 8 && e.usize > kMaxHeader))
        return fail("en-t\xC3\xAAte trop grand (plus de 1 Mio annonc\xC3\xA9) : ce n'est pas un enregistrement d'IHM Studio");
    std::string stored;
    if (!f.read(e.data, e.csize, stored)) return fail("en-t\xC3\xAAte illisible");
    if (e.method == 0) {
        out = std::move(stored);
        return true;
    }
    if (e.method != 8 || !inflate(stored, static_cast<std::size_t>(e.usize), out)) return fail("en-t\xC3\xAAte illisible (compression)");
    return true;
}

// CE QUE LA PISTE PEUT FAIRE ALLOUER : les octets decompresses et les elements (valeurs, images, evenements...).
struct Budget {
    std::uint64_t raw{0}, items{0};
    std::uint64_t maxRaw{g_maxRaw.load()}, maxItems{g_maxItems.load()};
    std::string   why;
    bool takeRaw(std::uint64_t n) {
        raw += n;
        if (raw > maxRaw && why.empty())
            why = "piste trop grande : plus de " + std::to_string(maxRaw >> 20) + " Mio d\xC3\xA9" "compress\xC3\xA9s - fichier refus\xC3\xA9";
        return why.empty();
    }
    bool take(std::uint64_t n = 1) {
        items += n;
        if (items > maxItems && why.empty())
            why = "piste trop grande : plus de " + std::to_string(maxItems) + " valeurs et \xC3\xA9v\xC3\xA9nements - fichier refus\xC3\xA9 "
                  "(d\xC3\xA9" "coupe les longs enregistrements : Dur\xC3\xA9" "e maximale)";
        return why.empty();
    }
};

// ---- ecrire : l'en-tete final a sa place, le repertoire a la fin -----------------------------
// `f` : le fichier ouvert en ecriture (l'en-tete local de entete.json a 0, sa place `room`, celui de piste.bin a
// `trackLocalAt`, les blocs de `trackDataAt` a + `trackSize`). Ecrit l'en-tete et le repertoire, ferme, coupe ce
// qui depasse, renomme `from` en `to` (vide : garde le nom). L'en-tete trop long pour sa place : tout est recopie
// (par morceaux) dans un fichier neuf.
bool finalizeFile(std::FILE* f, const std::string& from, const std::string& to, const std::string& json, std::size_t room,
                  std::uint64_t trackLocalAt, std::uint64_t trackDataAt, std::uint64_t trackSize, std::uint32_t trackCrc,
                  const DosStamp& d, std::string* why);
bool copyRange(std::FILE* out, InFile& in, std::uint64_t at, std::uint64_t n, std::uint32_t* crc) {
    std::string chunk;
    while (n > 0) {
        const std::uint64_t k = std::min<std::uint64_t>(n, 1u << 20);
        if (!in.read(at, k, chunk)) return false;
        if (crc) *crc = static_cast<std::uint32_t>(mz_crc32(*crc, reinterpret_cast<const unsigned char*>(chunk.data()), chunk.size()));
        if (std::fwrite(chunk.data(), 1, chunk.size(), out) != chunk.size()) return false;
        at += k;
        n -= k;
    }
    return true;
}
bool replaceFile(const std::string& tmp, const std::string& target, std::string* why) {
    std::error_code ec;
    fs::rename(P(tmp), P(target), ec);
    if (ec) {
        fs::remove(P(target), ec);
        fs::rename(P(tmp), P(target), ec);
    }
    if (ec) {
        if (why) *why = "impossible de remplacer " + target + " (" + ec.message() + ")";
        fs::remove(P(tmp), ec);
        return false;
    }
    return true;
}
// Un enregistrement neuf a `target` (un .tmp renomme) : l'en-tete que `jsonOf` donne (la taille de la piste et si
// elle est coupee, connues a la fin), puis la piste que `track` ecrit (elle rend faux si elle echoue) ; `crc`,
// `size` : ce qu'elle a ecrit ; `cut` : elle s'est arretee sur un bloc abime.
using TrackWriter = std::function<bool(std::FILE*, std::uint32_t& crc, std::uint64_t& size, bool& cut)>;
using JsonOf = std::function<std::string(std::uint64_t size, bool cut)>;
bool writeRecording(const std::string& target, const JsonOf& jsonOf, const TrackWriter& track, std::string* why) {
    std::error_code ec;
    if (P(target).has_parent_path()) fs::create_directories(P(target).parent_path(), ec);
    const std::string tmp = target + ".tmp";
    std::FILE* f = openFile(tmp, "wb");
    if (!f) {
        if (why) *why = "impossible d'\xC3\xA9" "crire " + target;
        return false;
    }
    const DosStamp d = dosNow();
    const std::string first = jsonOf(0, false);
    const std::size_t room = std::max<std::size_t>(kHeaderRoom, first.size() + 1024);
    const std::string head = localHeader(kHeaderEntry, crcOf(padded(first, room)), static_cast<std::uint32_t>(room), d) + padded(first, room);
    const std::uint64_t trackLocalAt = head.size();
    const std::string trackHead = localHeader(kTrackEntry, 0, 0, d);
    const std::uint64_t trackDataAt = trackLocalAt + trackHead.size();
    std::uint32_t crc = 0;
    std::uint64_t size = 0;
    bool cut = false;
    if (std::fwrite(head.data(), 1, head.size(), f) != head.size() || std::fwrite(trackHead.data(), 1, trackHead.size(), f) != trackHead.size()
        || !track(f, crc, size, cut) || trackDataAt + size > kMaxFile) {
        std::fclose(f);
        fs::remove(P(tmp), ec);
        if (why && why->empty()) *why = "\xC3\xA9" "criture interrompue : " + target;
        return false;
    }
    return finalizeFile(f, tmp, target, jsonOf(size, cut), room, trackLocalAt, trackDataAt, size, crc, d, why);
}
bool finalizeFile(std::FILE* f, const std::string& from, const std::string& to, const std::string& json, std::size_t room,
                  std::uint64_t trackLocalAt, std::uint64_t trackDataAt, std::uint64_t trackSize, std::uint32_t trackCrc,
                  const DosStamp& d, std::string* why) {
    std::error_code ec;
    if (json.size() > room) {
        // L'en-tete ne tient pas a sa place (une description tres longue) : un fichier neuf, la piste recopiee.
        std::fflush(f);
        std::fclose(f);
        const std::string target = to.empty() ? from : to;
        bool ok = false;
        {
            InFile in(from);                         // ferme avant de retirer `from` (Windows)
            ok = in.ok() && writeRecording(target + ".neuf", [&](std::uint64_t, bool) { return json; },
                                           [&](std::FILE* out, std::uint32_t& crc, std::uint64_t& size, bool&) {
                                               size = trackSize;
                                               return copyRange(out, in, trackDataAt, trackSize, &crc);
                                           }, why);
        }
        if (!ok) return false;
        fs::remove(P(from), ec);
        return replaceFile(target + ".neuf", target, why);
    }
    const std::string head = padded(json, room);
    const std::uint32_t headCrc = crcOf(head);
    const std::uint64_t end = trackDataAt + trackSize;
    const std::string central = centralAndEnd({{std::string(kHeaderEntry), headCrc, static_cast<std::uint32_t>(room), 0},
                                               {std::string(kTrackEntry), trackCrc, static_cast<std::uint32_t>(trackSize),
                                                static_cast<std::uint32_t>(trackLocalAt)}},
                                              static_cast<std::uint32_t>(end), d);
    bool ok = writeAt(f, 30 + kHeaderEntry.size(), head) && writeAt(f, 14, le32(headCrc))
           && writeAt(f, trackLocalAt + 14, le32(trackCrc) + le32(static_cast<std::uint32_t>(trackSize)) + le32(static_cast<std::uint32_t>(trackSize)))
           && writeAt(f, end, central);
    ok = std::fflush(f) == 0 && ok;
    std::fclose(f);
    if (!ok) {
        if (why) *why = "\xC3\xA9" "criture interrompue : " + from;
        return false;
    }
    // Ce qui depassait (un .part coupe au milieu d'un bloc) part.
    if (fs::file_size(P(from), ec) > end + central.size()) fs::resize_file(P(from), end + central.size(), ec);
    if (to.empty() || to == from) return true;
    fs::rename(P(from), P(to), ec);
    if (ec) {
        if (why) *why = "impossible de renommer en " + to + " (" + ec.message() + ")";
        return false;
    }
    return true;
}
// L'en-tete d'un enregistrement change : sur place s'il tient (le CRC local et celui du repertoire suivent), sinon un
// fichier neuf (la piste recopiee par morceaux).
bool rewriteHeader(const std::string& path, const Header& h, std::string* why) {
    const std::string json = toJson(h);
    Layout L;
    {
        InFile in(path);
        if (!in.ok()) {
            if (why) *why = "introuvable : " + path;
            return false;
        }
        if (!layoutOf(in, L, why)) return false;
        if (L.fast && L.header.method == 0 && json.size() <= L.header.csize && !L.cut) {
            // Le repertoire : la ligne de entete.json (son CRC).
            std::uint64_t crcAt = 0;
            std::string tail;
            const std::uint64_t n = std::min<std::uint64_t>(in.size(), 22 + 65535);
            if (in.read(in.size() - n, n, tail))
                for (std::size_t i = tail.size() >= 22 ? tail.size() - 22 + 1 : 0; i-- > 0;)
                    if (get32(tail.data() + i) == 0x06054b50u) {
                        const std::uint64_t cdAt = get32(tail.data() + i + 16);
                        std::string row;
                        if (in.read(cdAt, 46 + kHeaderEntry.size(), row) && get32(row.data()) == 0x02014b50u
                            && row.compare(46, kHeaderEntry.size(), kHeaderEntry) == 0 && get32(row.data() + 42) == 0)
                            crcAt = cdAt + 16;
                        break;
                    }
            if (crcAt > 0) {
                std::FILE* f = openFile(path, "r+b");
                if (!f) {
                    if (why) *why = "impossible d'\xC3\xA9" "crire " + path;
                    return false;
                }
                const std::string head = padded(json, static_cast<std::size_t>(L.header.csize));
                const std::uint32_t crc = crcOf(head);
                bool ok = writeAt(f, L.header.data, head) && writeAt(f, L.header.local + 14, le32(crc)) && writeAt(f, crcAt, le32(crc));
                ok = std::fflush(f) == 0 && ok;
                std::fclose(f);
                if (!ok && why) *why = "\xC3\xA9" "criture interrompue : " + path;
                return ok;
            }
        }
    }
    // Un fichier neuf : la piste recopiee telle quelle (une piste compressee par un autre outil : decompressee).
    {
    InFile in(path);
    if (!in.ok()) {
        if (why) *why = "introuvable : " + path;
        return false;
    }
    std::string inflated;
    if (L.hasTrack && L.track.method == 8) {
        std::string z;
        if (L.track.usize > g_maxRaw.load() || !in.read(L.track.data, L.track.csize, z) || !inflate(z, static_cast<std::size_t>(L.track.usize), inflated)) {
            if (why) *why = "piste illisible : " + path;
            return false;
        }
    }
    const std::uint64_t avail = L.hasTrack ? std::min<std::uint64_t>(L.track.csize, in.size() - std::min(in.size(), L.track.data)) : 0;
    if (!writeRecording(path + ".neuf", [&](std::uint64_t, bool) { return json; }, [&](std::FILE* out, std::uint32_t& crc, std::uint64_t& size, bool&) {
            if (L.hasTrack && L.track.method == 8) {
                size = inflated.size();
                crc = crcOf(inflated);
                return std::fwrite(inflated.data(), 1, inflated.size(), out) == inflated.size();
            }
            size = avail;
            return copyRange(out, in, L.track.data, avail, &crc);
        }, why))
        return false;
    }
    return replaceFile(path + ".neuf", path, why);
}

// Les blocs de [at, end) du fichier, un par un, jusqu'au premier abime ; `next` : la fin du dernier bloc entier.
// Rend faux si `fn` a dit d'arreter.
bool blocksIn(InFile& f, std::uint64_t at, std::uint64_t end, const std::function<bool(BlockView&)>& fn, std::uint64_t& next) {
    std::string head;
    next = at;
    while (at + kBlockHead <= end) {
        BlockView b;
        std::uint32_t storedLen = 0, crc = 0;
        if (!f.read(at, kBlockHead, head) || !blockHead(head.data(), b, storedLen, crc) || at + kBlockHead + storedLen > end) break;
        if (!f.read(at + kBlockHead, storedLen, b.stored) || crcOf(b.stored) != crc) break;
        b.size = kBlockHead + storedLen;
        at += b.size;
        next = at;
        if (!fn(b)) return false;
    }
    return true;
}
// Les blocs de la piste d'un enregistrement (stockee : lue par morceaux ; compressee par un autre outil : sa taille
// bornee, puis decompressee). 1 : lus (truncated : un bloc abime, ou coupe) ; 0 : `fn` a arrete ; -1 : refusee (why).
int eachTrackBlock(InFile& f, const Layout& L, const Header& h, const std::function<bool(BlockView&)>& fn, bool& truncated, std::string* why) {
    if (!L.hasTrack) {
        truncated = true;
        return 1;
    }
    if (L.track.method == 0) {
        const std::uint64_t end = std::min<std::uint64_t>(L.track.data + L.track.csize, f.size());
        std::uint64_t next = L.track.data;
        if (!blocksIn(f, L.track.data, end, fn, next)) return 0;
        truncated = truncated || L.cut || next < end;
        return 1;
    }
    if (L.track.method != 8) {
        if (why) *why = "piste : compression inconnue (" + std::to_string(L.track.method) + ")";
        return -1;
    }
    if (L.track.usize > g_maxRaw.load() || L.track.usize > inflateBound(L.track.csize) || (h.trackBytes > 0 && L.track.usize > h.trackBytes)) {
        if (why) *why = "piste annonc\xC3\xA9" "e trop grande (" + std::to_string(L.track.usize >> 20) + " Mio) : fichier refus\xC3\xA9";
        return -1;
    }
    std::string z, track;
    if (!f.read(L.track.data, L.track.csize, z) || !inflate(z, static_cast<std::size_t>(L.track.usize), track)) {
        truncated = true;
        return 1;
    }
    z.clear();
    std::size_t at = 0;
    BlockView b;
    while (blockAt(track, at, b)) {
        at += b.size;
        if (!fn(b)) return 0;
        b = BlockView{};
    }
    truncated = truncated || at < track.size();
    return 1;
}

// ---- l'en-tete JSON -----------------------------------------------------------------
std::string jsonString(std::string_view s) {
    std::string o = "\"";
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (u < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", u);
            o += buf;
        } else o += c;
    }
    return o + "\"";
}
std::string number(double v) {
    char buf[64];
    if (std::floor(v) == v && std::fabs(v) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", v);
    else std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}
void appendUtf8(std::string& o, std::uint32_t cp) {
    if (cp < 0x80) o.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        o.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}
// Un objet JSON a plat : cle -> texte (un nombre, true/false/null : tels quels).
bool parseFlat(std::string_view s, std::map<std::string, std::string>& out, std::string* why) {
    std::size_t i = 0;
    const auto ws = [&] { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; };
    const auto fail = [&](const char* w) {
        if (why) *why = std::string("en-t\xC3\xAAte illisible : ") + w;
        return false;
    };
    const auto str = [&](std::string& v) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                const char e = s[i + 1];
                i += 2;
                if (e == 'n') v += '\n';
                else if (e == 'r') v += '\r';
                else if (e == 't') v += '\t';
                else if (e == 'b') v += '\b';
                else if (e == 'f') v += '\f';
                else if (e == 'u' && i + 4 <= s.size()) {
                    appendUtf8(v, static_cast<std::uint32_t>(std::strtoul(std::string(s.substr(i, 4)).c_str(), nullptr, 16)));
                    i += 4;
                } else v += e;
            } else {
                v += s[i++];
            }
        }
        if (i >= s.size()) return false;
        ++i;
        return true;
    };
    ws();
    if (i >= s.size() || s[i] != '{') return fail("pas un objet");
    ++i;
    for (;;) {
        ws();
        if (i < s.size() && s[i] == '}') return true;
        std::string k, v;
        if (!str(k)) return fail("une cl\xC3\xA9");
        ws();
        if (i >= s.size() || s[i] != ':') return fail("':' attendu");
        ++i;
        ws();
        if (i < s.size() && s[i] == '"') {
            if (!str(v)) return fail("un texte");
        } else {
            while (i < s.size() && s[i] != ',' && s[i] != '}' && !std::isspace(static_cast<unsigned char>(s[i]))) v += s[i++];
        }
        out[k] = v;
        ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') return true;
        return fail("',' ou '}' attendu");
    }
}

// ---- la piste : lire les enregistrements ------------------------------------------------
// `budget` : ce que la piste a deja fait allouer ; depasse, la lecture s'arrete (budget->why dit pourquoi).
void parseRecords(const std::string& raw, Track& tr, Budget& budget) {
    In in(raw.data(), raw.data() + raw.size());
    while (in.more()) {
        if (!budget.take()) return;
        const std::uint8_t kind = in.u8();
        switch (kind) {
            case RKey: {
                KeyDef k;
                (void)in.var();                                   // le rang : celui de la liste
                k.kind = static_cast<KeyKind>(in.u8());
                k.key = in.str();
                k.shown = in.str();
                k.variable = static_cast<Id>(in.var());
                k.declared = in.str();
                if (in.ok) tr.keys.push_back(std::move(k));
                break;
            }
            case RValues:
            case RKeyframe: {
                Frame f;
                f.t = in.time();
                f.keyframe = kind == RKeyframe;
                const std::uint64_t n = in.var();
                for (std::uint64_t k = 0; k < n && in.ok; ++k) {
                    if (!budget.take()) return;
                    Change c;
                    c.key = static_cast<std::uint32_t>(in.var());
                    c.value = in.value();
                    if (c.key < tr.keys.size()) f.changes.push_back(std::move(c));
                }
                if (in.ok) {
                    tr.end = std::max(tr.end, f.t);
                    tr.frames.push_back(std::move(f));
                }
                break;
            }
            case RScreen: {
                Screen sc;
                sc.t = in.time();
                sc.view = in.str();
                sc.arguments = in.str();
                const std::uint64_t n = in.var();
                for (std::uint64_t k = 0; k < n && in.ok; ++k) {
                    if (!budget.take()) return;
                    PopupState p;
                    p.view = in.str();
                    p.arguments = in.str();
                    p.x = in.f64();
                    p.y = in.f64();
                    sc.popups.push_back(std::move(p));
                }
                sc.language = in.str();
                sc.theme = in.str();
                if (in.ok) {
                    tr.end = std::max(tr.end, sc.t);
                    tr.screens.push_back(std::move(sc));
                }
                break;
            }
            case ROverrides: {
                Overrides o;
                o.t = in.time();
                const std::uint64_t n = in.var();
                for (std::uint64_t k = 0; k < n && in.ok; ++k) {
                    if (!budget.take()) return;
                    Override e;
                    e.view = in.str();
                    e.object = in.str();
                    e.key = in.str();
                    e.value = in.str();
                    o.list.push_back(std::move(e));
                }
                if (in.ok) tr.overrides.push_back(std::move(o));
                break;
            }
            case RAlarms: {
                Alarms a;
                a.t = in.time();
                const std::uint64_t n = in.var();
                for (std::uint64_t k = 0; k < n && in.ok; ++k) {
                    if (!budget.take()) return;
                    Alarm x;
                    x.name = in.str();
                    x.message = in.str();
                    x.group = in.str();
                    x.priority = static_cast<int>(in.var());
                    const std::uint8_t flags = in.u8();
                    x.active = (flags & 1) != 0;
                    x.acked = (flags & 2) != 0;
                    x.ackRequired = (flags & 4) != 0;
                    x.appeared = in.str();
                    x.ackedAt = in.str();
                    x.ackedBy = in.str();
                    x.cleared = in.str();
                    a.list.push_back(std::move(x));
                }
                if (in.ok) {
                    tr.end = std::max(tr.end, a.t);
                    tr.alarms.push_back(std::move(a));
                }
                break;
            }
            case RPointer: {
                Pointer p;
                p.t = in.time();
                p.view = in.str();
                p.x = in.f64();
                p.y = in.f64();
                p.inside = in.u8() != 0;
                p.buttons = in.u8();
                if (in.ok) tr.pointers.push_back(std::move(p));
                break;
            }
            case REvent: {
                Event e;
                e.t = in.time();
                e.kind = static_cast<EventKind>(in.u8());
                e.a = in.str();
                e.b = in.str();
                e.x = in.f64();
                e.y = in.f64();
                if (in.ok) {
                    tr.end = std::max(tr.end, e.t);
                    tr.events.push_back(std::move(e));
                }
                break;
            }
            case REnd:
                tr.end = std::max(tr.end, in.time());
                if (in.ok) tr.closed = true;
                break;
            default:
                in.ok = false;
                break;
        }
    }
}

// Les blocs d'une piste, dans l'ordre, jusqu'au premier abime ; `stop` : l'endroit (et s'il reste quelque chose).
std::vector<BlockView> blocksOf(const std::string& track, std::size_t from, bool* truncated) {
    std::vector<BlockView> out;
    std::size_t at = from;
    BlockView b;
    while (blockAt(track, at, b)) {
        at += b.size;
        out.push_back(std::move(b));
        b = BlockView{};
    }
    if (truncated) *truncated = at < track.size();
    return out;
}

// Le .part : "XRECPART", la longueur de l'en-tete, l'en-tete, puis les blocs.
bool readPart(const std::string& path, Header& h, std::string& blocks, std::string* why) {
    std::string data;
    if (!readAll(path, data)) {
        if (why) *why = "illisible : " + path;
        return false;
    }
    if (data.size() < 12 || std::memcmp(data.data(), kPartMagic, 8) != 0) {
        if (why) *why = "pas un enregistrement en cours : " + path;
        return false;
    }
    const std::uint32_t n = get32(data.data() + 8);
    if (data.size() < 12u + n) {
        if (why) *why = "en-t\xC3\xAAte coup\xC3\xA9 : " + path;
        return false;
    }
    if (!fromJson(std::string_view(data).substr(12, n), h, why)) return false;
    blocks = data.substr(12 + n);
    return true;
}

std::string stamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool endsWith(std::string_view s, std::string_view e) { return s.size() >= e.size() && lower(std::string(s.substr(s.size() - e.size()))) == lower(std::string(e)); }

std::mutex&            activeLock() { static std::mutex m; return m; }
std::set<std::string>& activeParts() { static std::set<std::string> s; return s; }

std::int64_t mtimeOf(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    const auto sys = std::chrono::time_point_cast<std::chrono::seconds>(t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return static_cast<std::int64_t>(sys.time_since_epoch().count());
}

} // namespace

// ======================================================================= le dossier ==
std::string folderOf(const RecordingSettings& s, const std::string& projectFolder) {
    std::string f = s.folder;
    while (!f.empty() && std::isspace(static_cast<unsigned char>(f.back()))) f.pop_back();
    while (!f.empty() && std::isspace(static_cast<unsigned char>(f.front()))) f.erase(0, 1);
    if (!f.empty()) {
        const fs::path p = P(f);
        if (p.is_absolute()) return f;
        if (projectFolder.empty()) return {};
        return U((P(projectFolder) / p).lexically_normal());
    }
    if (projectFolder.empty()) return {};
    return U((P(projectFolder) / P(std::string(kNativeFolder))).lexically_normal());
}

namespace {
// Un nom reserve de Windows (CON, NUL.essai, com1...) : ce qui precede le premier point, blancs de fin retires.
std::size_t reservedStem(std::string_view t) {
    std::size_t n = t.find('.');
    if (n == std::string_view::npos) n = t.size();
    std::size_t e = n;
    while (e > 0 && t[e - 1] == ' ') --e;
    std::string stem(t.substr(0, e));
    for (auto& c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return e;
    if (stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) && stem[3] >= '0' && stem[3] <= '9') return e;
    return 0;
}
} // namespace

bool validName(std::string_view name, std::string* why) {
    const auto no = [why](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    std::string t(name);
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(0, 1);
    if (t.empty()) return no("le nom est vide");
    if (t.size() > 120) return no("le nom est trop long (120 caract\xC3\xA8res au plus)");
    for (const char c : t)
        if (static_cast<unsigned char>(c) < 0x20 || std::strchr("/\\:*?\"<>|", c))
            return no(std::string("le nom ne peut pas contenir ") + (static_cast<unsigned char>(c) < 0x20 ? std::string("de caract\xC3\xA8re de contr\xC3\xB4le") : std::string(1, c)));
    if (t.back() == '.') return no("le nom ne finit pas par un point");
    if (reservedStem(t) > 0)
        return no("\xC2\xAB " + t + " \xC2\xBB est un nom r\xC3\xA9serv\xC3\xA9 de Windows (CON, PRN, AUX, NUL, COM1 \xC3\xA0 COM9, LPT1 \xC3\xA0 LPT9)");
    return true;
}

std::string cleanName(std::string_view text) {
    std::string out;
    for (const char c : text) out += (static_cast<unsigned char>(c) < 0x20 || std::strchr("/\\:*?\"<>|", c)) ? '_' : c;
    while (!out.empty() && (std::isspace(static_cast<unsigned char>(out.back())) || out.back() == '.')) out.pop_back();
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front()))) out.erase(0, 1);
    if (out.size() > 120) out.resize(120);
    if (const std::size_t at = reservedStem(out); at > 0) out.insert(at, "_");   // CON -> CON_
    return out.empty() ? std::string("Enregistrement") : out;
}

std::string pathOf(const std::string& folder, std::string_view name) {
    return U(P(folder) / P(std::string(name) + std::string(kExtension)));
}

std::string nameOfPath(std::string_view path) {
    std::string leaf = U(P(std::string(path)).filename());
    if (endsWith(leaf, std::string(kExtension) + std::string(kPartSuffix))) leaf.resize(leaf.size() - kExtension.size() - kPartSuffix.size());
    else if (endsWith(leaf, kExtension)) leaf.resize(leaf.size() - kExtension.size());
    return leaf;
}

// ======================================================================= l'en-tete ==
std::string toJson(const Header& h) {
    std::string o = "{\n";
    const auto s = [&o](const char* k, std::string_view v) { o += "  " + jsonString(k) + ": " + jsonString(v) + ",\n"; };
    const auto n = [&o](const char* k, double v) { o += "  " + jsonString(k) + ": " + number(v) + ",\n"; };
    const auto b = [&o](const char* k, bool v) { o += "  " + jsonString(k) + ": " + (v ? "true" : "false") + ",\n"; };
    n("format", h.format);
    s("application", h.application);
    s("nom", h.name);
    s("description", h.description);
    s("date", h.date);
    n("duree_ms", static_cast<double>(ms(h.duration)));
    s("projet", h.project);
    s("version_projet", h.projectVersion);
    s("empreinte_projet", h.fingerprint);
    s("vue_depart", h.startView);
    s("ou", h.where);
    s("par", h.by);
    n("frequence", h.rateHz);
    n("image_complete_s", h.keyframeEvery);
    b("protege", h.locked());
    s("sel", h.salt);
    s("empreinte", h.hash);
    s("sel_cle", h.keySalt);
    s("nonce", h.nonce);
    s("cle", h.fileKey);
    n("images", static_cast<double>(h.frames));
    n("cles", static_cast<double>(h.keys));
    n("clics", static_cast<double>(h.clicks));
    n("alarmes", static_cast<double>(h.alarms));
    n("vues", static_cast<double>(h.views));
    n("sons", static_cast<double>(h.sounds));
    n("journal", static_cast<double>(h.journal));
    n("evenements", static_cast<double>(h.events));
    s("vues_montrees", h.viewsShown);
    s("ecran_40", h.at40);
    b("coupe", h.cut);
    b("sons_gardes", h.soundsKept);   // 1.14.1 (N3)
    o += "  " + jsonString("taille_piste") + ": " + number(static_cast<double>(h.trackBytes)) + "\n}\n";
    return o;
}

bool fromJson(std::string_view text, Header& h, std::string* why) {
    std::map<std::string, std::string> m;
    if (!parseFlat(text, m, why)) return false;
    const auto get = [&m](const char* k) -> std::string {
        const auto it = m.find(k);
        return it == m.end() ? std::string{} : it->second;
    };
    const auto num = [&](const char* k, double d) {
        const std::string v = get(k);
        if (v.empty()) return d;
        char* end = nullptr;
        const double x = std::strtod(v.c_str(), &end);
        return end && *end == '\0' ? x : d;
    };
    h = Header{};
    h.format = static_cast<int>(num("format", kFormat));
    if (h.format > kFormat) {
        if (why) *why = "un enregistrement d'une version plus r\xC3\xA9" "cente (format " + std::to_string(h.format) + ")";
        return false;
    }
    h.application = get("application");
    h.name = get("nom");
    h.description = get("description");
    h.date = get("date");
    h.duration = num("duree_ms", 0) / 1000.0;
    h.project = get("projet");
    h.projectVersion = get("version_projet");
    h.fingerprint = get("empreinte_projet");
    h.startView = get("vue_depart");
    h.where = get("ou");
    h.by = get("par");
    h.rateHz = static_cast<int>(num("frequence", 10));
    h.keyframeEvery = num("image_complete_s", kKeyframeEvery);
    h.salt = get("sel");
    h.hash = get("empreinte");
    h.keySalt = get("sel_cle");
    h.nonce = get("nonce");
    h.fileKey = get("cle");
    h.frames = static_cast<std::size_t>(num("images", 0));
    h.keys = static_cast<std::size_t>(num("cles", 0));
    h.clicks = static_cast<std::size_t>(num("clics", 0));
    h.alarms = static_cast<std::size_t>(num("alarmes", 0));
    h.views = static_cast<std::size_t>(num("vues", 0));
    h.sounds = static_cast<std::size_t>(num("sons", 0));
    h.journal = static_cast<std::size_t>(num("journal", 0));
    h.events = static_cast<std::size_t>(num("evenements", 0));
    h.viewsShown = get("vues_montrees");
    h.at40 = get("ecran_40");
    h.cut = get("coupe") == "true";
    h.soundsKept = get("sons_gardes") != "false";   // 1.14.1 (N3) : absente (un fichier d'avant) : vrai
    h.trackBytes = static_cast<std::uint64_t>(num("taille_piste", 0));
    return true;
}

void protect(Header& h, const std::string& password, const std::string& fileKey) {
    if (h.nonce.empty()) h.nonce = randomHex(16);
    if (password.empty()) {
        h.salt.clear();
        h.hash.clear();
        h.keySalt.clear();
        h.fileKey = toHex(reinterpret_cast<const std::uint8_t*>(fileKey.data()), fileKey.size());
        return;
    }
    h.salt = randomHex(16);
    h.hash = passwordHash(h.salt, password);
    h.keySalt = randomHex(16);
    const std::string wrapped = wrapKey(fileKey, deriveKey(h, password), h.nonce);
    h.fileKey = toHex(reinterpret_cast<const std::uint8_t*>(wrapped.data()), wrapped.size());
}

bool passwordOk(const Header& h, std::string_view password) {
    if (!h.locked()) return true;
    return passwordMatches(h.salt, h.hash, password);
}

std::string trackKey(const Header& h, const std::string& password) {
    if (h.fileKey.empty()) return deriveKey(h, password);              // format 1 : la cle du mot de passe ; en clair : rien
    const std::string stored = fromHex(h.fileKey);
    if (!h.locked()) return stored;
    if (password.empty() || !passwordOk(h, password)) return {};
    return wrapKey(stored, deriveKey(h, password), h.nonce);
}

// ======================================================================= enregistrer ==
Recorder::~Recorder() {
    if (file_) {
        std::fclose(static_cast<std::FILE*>(file_));     // un .part laisse tel quel : la prochaine liste le relit
        file_ = nullptr;
    }
    if (open_) markActive(options_.partPath, false);
}

bool Recorder::open(RecorderOptions o, std::string* why) {
    options_ = std::move(o);
    if (options_.header.application.empty()) options_.header.application = "IHM Studio";
    if (options_.header.date.empty()) options_.header.date = stamp();
    options_.header.format = kFormat;
    // LA CLE DU FICHIER, tiree au sort : elle chiffre chaque bloc. Le mot de passe (s'il y en a un) la chiffre dans
    // l'en-tete - le donner, le changer ou le retirer a l'arret ne touche que l'en-tete.
    key_ = fromHex(randomHex(32));
    options_.header.nonce = randomHex(16);
    protect(options_.header, options_.password, key_);
    std::error_code ec;
    const fs::path part = P(options_.partPath);
    if (part.has_parent_path()) fs::create_directories(part.parent_path(), ec);
    std::FILE* f = openFile(options_.partPath, "wb");
    if (!f) {
        error_ = "impossible d'\xC3\xA9" "crire " + options_.partPath;
        if (why) *why = error_;
        return false;
    }
#if !defined(_WIN32)
    // Un autre programme qui liste le dossier ne le prend pas pour un .part abandonne (Windows : un fichier ouvert ne
    // se renomme pas, c'est le meme verrou).
    (void)::flock(::fileno(f), LOCK_EX | LOCK_NB);
#endif
    file_ = f;
    const DosStamp d = dosNow();
    stampTime_ = d.time;
    stampDate_ = d.date;
    const std::string json = toJson(options_.header);
    room_ = std::max<std::size_t>(kHeaderRoom, json.size() + 4096);
    const std::string head = localHeader(kHeaderEntry, crcOf(padded(json, room_)), static_cast<std::uint32_t>(room_), d) + padded(json, room_);
    trackLocalAt_ = head.size();
    const std::string trackHead = localHeader(kTrackEntry, 0, 0, d);
    trackDataAt_ = trackLocalAt_ + trackHead.size();
    trackCrc_ = 0;
    written_ = 0;
    const bool ok = std::fwrite(head.data(), 1, head.size(), f) == head.size() && std::fwrite(trackHead.data(), 1, trackHead.size(), f) == trackHead.size()
                 && std::fflush(f) == 0;
    if (!ok) {
        std::fclose(f);
        file_ = nullptr;
        fs::remove(part, ec);
        error_ = "\xC3\xA9" "criture interrompue : " + options_.partPath;
        if (why) *why = error_;
        return false;
    }
    open_ = true;
    markActive(options_.partPath, true);
    return true;
}

void Recorder::record(std::string bytes) { pending_ += bytes; }

std::uint32_t Recorder::keyOf(std::string_view key, KeyKind kind, std::string_view shown, Id variable, std::string_view declared) {
    const std::string k(key);
    if (const auto it = index_.find(k); it != index_.end()) return it->second;
    const auto i = static_cast<std::uint32_t>(keys_.size());
    KeyDef d;
    d.key = k;
    d.shown = shown.empty() ? k : std::string(shown);
    d.kind = kind;
    d.variable = variable;
    d.declared = std::string(declared);
    std::string r(1, static_cast<char>(RKey));
    putVar(r, i);
    r.push_back(static_cast<char>(kind));
    putStr(r, d.key);
    putStr(r, d.shown);
    putVar(r, d.variable);
    putStr(r, d.declared);
    record(std::move(r));
    keys_.push_back(std::move(d));
    values_.emplace_back();
    known_.push_back(false);
    touched_.push_back(false);
    index_.emplace(k, i);
    return i;
}

void Recorder::beginSample(double t) {
    sampleAt_ = std::max(t, last_);
    changes_.clear();
    inSample_ = true;
}

void Recorder::value(std::string_view key, const sim::Value& v, KeyKind kind, std::string_view shown, Id variable, std::string_view declared) {
    if (!open_) return;
    const std::uint32_t i = keyOf(key, kind, shown, variable, declared);
    if (known_[i] && sameValue(values_[i], v)) return;
    values_[i] = v;
    known_[i] = true;
    if (!touched_[i]) {
        touched_[i] = true;
        changes_.push_back(Change{i, v});
    } else {
        for (auto& c : changes_)
            if (c.key == i) c.value = v;
    }
}

void Recorder::endSample() {
    if (!open_ || !inSample_) return;
    inSample_ = false;
    for (const auto& c : changes_) touched_[c.key] = false;
    const bool full = sampleAt_ - lastKeyframe_ >= options_.keyframeEvery - 1e-9;
    if (!full && changes_.empty()) {
        last_ = std::max(last_, sampleAt_);
        return;
    }
    std::string r(1, static_cast<char>(full ? RKeyframe : RValues));
    putVar(r, ms(sampleAt_));
    if (full) {
        std::size_t n = 0;
        for (std::size_t i = 0; i < keys_.size(); ++i) n += known_[i] ? 1 : 0;
        putVar(r, n);
        for (std::size_t i = 0; i < keys_.size(); ++i) {
            if (!known_[i]) continue;
            putVar(r, i);
            putValue(r, values_[i]);
        }
        lastKeyframe_ = sampleAt_;
    } else {
        putVar(r, changes_.size());
        for (const auto& c : changes_) {
            putVar(r, c.key);
            putValue(r, c.value);
        }
    }
    record(std::move(r));
    ++frames_;
    last_ = std::max(last_, sampleAt_);
    changes_.clear();
}

void Recorder::screen(Screen s) {
    if (!open_ || (screen_ && screen_->same(s))) return;
    if (!s.view.empty() && std::find(views_.begin(), views_.end(), s.view) == views_.end()) views_.push_back(s.view);
    for (const auto& p : s.popups)
        if (!p.view.empty() && std::find(views_.begin(), views_.end(), p.view) == views_.end()) views_.push_back(p.view);
    std::string r(1, static_cast<char>(RScreen));
    putVar(r, ms(s.t));
    putStr(r, s.view);
    putStr(r, s.arguments);
    putVar(r, s.popups.size());
    for (const auto& p : s.popups) {
        putStr(r, p.view);
        putStr(r, p.arguments);
        putF64(r, p.x);
        putF64(r, p.y);
    }
    putStr(r, s.language);
    putStr(r, s.theme);
    record(std::move(r));
    last_ = std::max(last_, s.t);
    std::string shown = s.view;
    for (const auto& p : s.popups) shown += " + " + p.view;
    if (shown_.empty() || shown_.back().second != shown) shown_.emplace_back(s.t, std::move(shown));
    screen_ = std::move(s);
}

void Recorder::overrides(Overrides o) {
    if (!open_ || (overrides_ ? overrides_->list == o.list : o.list.empty())) return;
    std::string r(1, static_cast<char>(ROverrides));
    putVar(r, ms(o.t));
    putVar(r, o.list.size());
    for (const auto& e : o.list) {
        putStr(r, e.view);
        putStr(r, e.object);
        putStr(r, e.key);
        putStr(r, e.value);
    }
    record(std::move(r));
    overrides_ = std::move(o);
}

void Recorder::alarms(Alarms a) {
    if (!open_) return;
    if (alarms_ ? alarms_->list == a.list : a.list.empty()) return;
    // Les evenements : ce qui apparait, ce qui est acquitte, ce qui disparait.
    const std::vector<Alarm> none;
    const auto& before = alarms_ ? alarms_->list : none;
    const auto find = [](const std::vector<Alarm>& l, const std::string& name) -> const Alarm* {
        for (const auto& x : l)
            if (x.name == name) return &x;
        return nullptr;
    };
    for (const auto& x : a.list) {
        const Alarm* was = find(before, x.name);
        if ((!was && x.active) || (was && !was->active && x.active)) event(Event{a.t, EventKind::AlarmAppear, x.name, x.message, 0, 0});
        if (was && !was->acked && x.acked) event(Event{a.t, EventKind::AlarmAck, x.name, x.ackedBy, 0, 0});
        if (was && was->active && !x.active) event(Event{a.t, EventKind::AlarmClear, x.name, {}, 0, 0});
    }
    for (const auto& x : before)
        if (x.active && !find(a.list, x.name)) event(Event{a.t, EventKind::AlarmClear, x.name, {}, 0, 0});
    std::string r(1, static_cast<char>(RAlarms));
    putVar(r, ms(a.t));
    putVar(r, a.list.size());
    for (const auto& x : a.list) {
        putStr(r, x.name);
        putStr(r, x.message);
        putStr(r, x.group);
        putVar(r, static_cast<std::uint64_t>(std::max(0, x.priority)));
        r.push_back(static_cast<char>((x.active ? 1 : 0) | (x.acked ? 2 : 0) | (x.ackRequired ? 4 : 0)));
        putStr(r, x.appeared);
        putStr(r, x.ackedAt);
        putStr(r, x.ackedBy);
        putStr(r, x.cleared);
    }
    record(std::move(r));
    last_ = std::max(last_, a.t);
    alarms_ = std::move(a);
}

void Recorder::pointer(Pointer p) {
    if (!open_ || (pointer_ && pointer_->same(p))) return;
    std::string r(1, static_cast<char>(RPointer));
    putVar(r, ms(p.t));
    putStr(r, p.view);
    putF64(r, p.x);
    putF64(r, p.y);
    r.push_back(p.inside ? 1 : 0);
    r.push_back(static_cast<char>(p.buttons));
    record(std::move(r));
    pointer_ = std::move(p);
}

void Recorder::event(Event e) {
    if (!open_) return;
    e.t = std::max(e.t, 0.0);
    std::string r(1, static_cast<char>(REvent));
    putVar(r, ms(e.t));
    r.push_back(static_cast<char>(e.kind));
    putStr(r, e.a);
    putStr(r, e.b);
    putF64(r, e.x);
    putF64(r, e.y);
    record(std::move(r));
    ++events_;
    if (e.kind == EventKind::Click || e.kind == EventKind::LongPress || e.kind == EventKind::DoubleClick) ++clicks_;
    if (e.kind == EventKind::AlarmAppear) ++alarmEvents_;
    if (e.kind == EventKind::Sound) ++sounds_;
    if (e.kind == EventKind::Journal) ++journal_;
    last_ = std::max(last_, e.t);
}

bool Recorder::flush(double t, bool force) {
    if (!open_ || !file_) return false;
    if (!force && t - lastBlock_ < options_.blockEvery) return true;
    lastBlock_ = t;
    if (pending_.empty()) return true;
    const std::string block = makeBlock(pending_, seq_++, std::max(t, last_), key_, options_.header.nonce);
    auto* f = static_cast<std::FILE*>(file_);
    const bool ok = std::fwrite(block.data(), 1, block.size(), f) == block.size() && std::fflush(f) == 0;
    if (!ok) error_ = "\xC3\xA9" "criture interrompue : " + options_.partPath;
    trackCrc_ = static_cast<std::uint32_t>(mz_crc32(trackCrc_, reinterpret_cast<const unsigned char*>(block.data()), block.size()));
    written_ += block.size();
    pending_.clear();
    return ok;
}

bool Recorder::close(double t, bool keep, const std::string& finalPath, const std::string& name, const std::string& description,
                     const std::string& password, std::string* why, Header* written) {
    if (!open_) {
        if (why) *why = "aucun enregistrement ouvert";
        return false;
    }
    last_ = std::max(last_, t);
    if (keep) {
        std::string end(1, static_cast<char>(REnd));
        putVar(end, ms(last_));
        record(std::move(end));
    }
    (void)flush(last_, true);
    auto* f = static_cast<std::FILE*>(file_);
    file_ = nullptr;
    open_ = false;
    markActive(options_.partPath, false);
    std::error_code ec;
    if (!keep) {
        std::fclose(f);
        fs::remove(P(options_.partPath), ec);
        return true;
    }
    // L'en-tete final : la duree, les comptes, le nom et la description, le mot de passe donne a l'arret (seule la
    // cle de l'en-tete change : la piste deja ecrite reste telle quelle).
    Header h = options_.header;
    h.name = name.empty() ? h.name : name;
    h.description = description;
    h.duration = last_;
    h.frames = frames_;
    h.keys = keys_.size();
    h.clicks = clicks_;
    h.alarms = alarmEvents_;
    h.sounds = sounds_;
    h.journal = journal_;
    h.events = events_;
    h.views = views_.size();
    h.viewsShown.clear();
    for (const auto& v : views_) h.viewsShown += (h.viewsShown.empty() ? "" : ";") + v;
    h.at40.clear();
    for (const auto& [at, shown] : shown_) {
        if (at > last_ * 0.4 + 1e-9 && !h.at40.empty()) break;
        h.at40 = shown;
    }
    h.cut = false;
    protect(h, password, key_);
    h.trackBytes = written_;
    if (trackDataAt_ + written_ > kMaxFile) {
        std::fclose(f);
        if (why) *why = "enregistrement trop gros pour un fichier .xrec (4 Go)";
        return false;
    }
    DosStamp d;
    d.time = stampTime_;
    d.date = stampDate_;
    if (!finalizeFile(f, options_.partPath, finalPath, toJson(h), room_, trackLocalAt_, trackDataAt_, written_, trackCrc_, d, why)) return false;
    if (written) *written = h;
    return true;
}

// ======================================================================= relire ==
Player::Player(std::shared_ptr<const Track> t) : track_(std::move(t)) {
    const Track& tr = *track_;
    first_.assign(tr.keys.size(), sim::Value{});
    std::vector<bool> seen(tr.keys.size(), false);
    for (std::size_t i = 0; i < tr.keys.size(); ++i) index_.emplace(tr.keys[i].key, static_cast<std::uint32_t>(i));
    for (std::size_t f = 0; f < tr.frames.size(); ++f) {
        if (tr.frames[f].keyframe) keyframes_.push_back(f);
        for (const auto& c : tr.frames[f].changes)
            if (c.key < seen.size() && !seen[c.key]) {
                seen[c.key] = true;
                first_[c.key] = c.value;
            }
    }
    seek(0);
}

void Player::seek(double t) {
    pos_ = std::clamp(t, 0.0, std::max(0.0, track_->end));
    current_ = first_;
    const auto& frames = track_->frames;
    // L'image complete d'avant, puis les changements.
    std::size_t start = 0;
    auto it = std::upper_bound(keyframes_.begin(), keyframes_.end(), pos_,
                               [&frames](double p, std::size_t f) { return p < frames[f].t; });
    if (it != keyframes_.begin()) start = *std::prev(it);
    cursor_ = start;
    while (cursor_ < frames.size() && frames[cursor_].t <= pos_ + 1e-9) {
        for (const auto& c : frames[cursor_].changes)
            if (c.key < current_.size()) current_[c.key] = c.value;
        ++cursor_;
    }
}

void Player::advanceTo(double t) {
    t = std::clamp(t, 0.0, std::max(0.0, track_->end));
    if (t < pos_) {
        seek(t);
        return;
    }
    pos_ = t;
    const auto& frames = track_->frames;
    while (cursor_ < frames.size() && frames[cursor_].t <= pos_ + 1e-9) {
        for (const auto& c : frames[cursor_].changes)
            if (c.key < current_.size()) current_[c.key] = c.value;
        ++cursor_;
    }
}

std::optional<std::uint32_t> Player::keyIndex(std::string_view upperKey) const {
    const auto it = index_.find(std::string(upperKey));
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

const sim::Value* Player::value(std::uint32_t key) const {
    if (key >= current_.size() || current_[key].type() == sim::Type::Unknown) return nullptr;
    return &current_[key];
}

const sim::Value* Player::valueOf(std::string_view upperKey) const {
    const auto k = keyIndex(upperKey);
    return k ? value(*k) : nullptr;
}

template <class T>
const T* Player::at(const std::vector<T>& list) const {
    auto it = std::upper_bound(list.begin(), list.end(), pos_ + 1e-9, [](double p, const T& x) { return p < x.t; });
    if (it == list.begin()) return list.empty() ? nullptr : &list.front();
    return &*std::prev(it);
}

const Screen*    Player::screen() const { return at(track_->screens); }
const Overrides* Player::overrides() const {
    const auto& l = track_->overrides;
    auto it = std::upper_bound(l.begin(), l.end(), pos_ + 1e-9, [](double p, const Overrides& x) { return p < x.t; });
    return it == l.begin() ? nullptr : &*std::prev(it);
}
const Alarms* Player::alarms() const {
    const auto& l = track_->alarms;
    auto it = std::upper_bound(l.begin(), l.end(), pos_ + 1e-9, [](double p, const Alarms& x) { return p < x.t; });
    return it == l.begin() ? nullptr : &*std::prev(it);
}
const Pointer* Player::pointer() const {
    const auto& l = track_->pointers;
    auto it = std::upper_bound(l.begin(), l.end(), pos_ + 1e-9, [](double p, const Pointer& x) { return p < x.t; });
    return it == l.begin() ? nullptr : &*std::prev(it);
}

std::vector<const Event*> Player::eventsIn(double a, double b) const {
    std::vector<const Event*> out;
    const auto& l = track_->events;
    auto it = std::upper_bound(l.begin(), l.end(), a + 1e-9, [](double p, const Event& x) { return p < x.t; });
    for (; it != l.end() && it->t <= b + 1e-9; ++it) out.push_back(&*it);
    return out;
}

std::vector<Player::Marker> Player::markers() const {
    std::vector<Marker> out;
    for (const auto& e : track_->events) {
        if (e.kind == EventKind::AlarmAppear) out.push_back({e.t, 'a', e.a + (e.b.empty() ? std::string{} : " : " + e.b)});
        else if (e.kind == EventKind::Click || e.kind == EventKind::LongPress || e.kind == EventKind::DoubleClick)
            out.push_back({e.t, 'c', e.a});
    }
    std::string last;
    for (const auto& s : track_->screens) {
        if (s.view != last && !last.empty()) out.push_back({s.t, 'v', s.view});
        last = s.view;
    }
    std::stable_sort(out.begin(), out.end(), [](const Marker& a, const Marker& b) { return a.t < b.t; });
    return out;
}

// ======================================================================= les fichiers ==
std::string_view statusText(Status s) noexcept {
    switch (s) {
        case Status::Ok: return "ok";
        case Status::NotFound: return "introuvable";
        case Status::Unreadable: return "illisible";
        case Status::PasswordNeeded: return "prot\xC3\xA9g\xC3\xA9 par un mot de passe";
        case Status::BadPassword: return "mot de passe incorrect";
    }
    return {};
}

namespace {
// LES EN-TETES DEJA LUS : par chemin, avec la taille et la date de modification du fichier - la liste du dossier
// (toutes les 2 s a l'ecran, le Lecteur, IHM_ENREG_EXISTE) ne relit un en-tete que s'il a change.
struct CachedHeader {
    std::uint64_t bytes{0};
    long long     stamp{0};
    Header        header;
    Status        status{Status::Ok};
    std::string   problem;
};
std::mutex&                           cacheLock() { static std::mutex m; return m; }
std::map<std::string, CachedHeader>& headerCache() { static std::map<std::string, CachedHeader> c; return c; }
long long stampOf(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    return ec ? 0 : static_cast<long long>(t.time_since_epoch().count());
}
Status cachedHeader(const std::string& path, std::uint64_t bytes, long long stamp, Header& out, std::string& problem) {
    {
        std::lock_guard<std::mutex> lock(cacheLock());
        const auto it = headerCache().find(path);
        if (it != headerCache().end() && it->second.bytes == bytes && it->second.stamp == stamp) {
            out = it->second.header;
            problem = it->second.problem;
            return it->second.status;
        }
    }
    CachedHeader c;
    c.bytes = bytes;
    c.stamp = stamp;
    std::string why;
    c.status = readHeader(path, c.header, &why);
    if (c.status != Status::Ok) c.problem = why.empty() ? std::string("illisible") : why;
    out = c.header;
    problem = c.problem;
    std::lock_guard<std::mutex> lock(cacheLock());
    headerCache()[path] = std::move(c);
    return problem.empty() ? Status::Ok : Status::Unreadable;
}
} // namespace

Limits limits() noexcept {
    Limits l;
    l.trackRaw = g_maxRaw.load();
    l.items = g_maxItems.load();
    return l;
}
void setLimits(const Limits& l) noexcept {
    g_maxRaw = l.trackRaw;
    g_maxItems = l.items;
}
FileStats fileStats() noexcept {
    FileStats s;
    s.opens = g_opens.load();
    s.bytes = g_bytes.load();
    return s;
}

Status readHeader(const std::string& path, Header& out, std::string* why) {
    try {
        std::error_code ec;
        if (!fs::is_regular_file(P(path), ec)) {
            if (why) *why = "introuvable : " + path;
            return Status::NotFound;
        }
        InFile f(path);
        if (!f.ok()) {
            if (why) *why = "illisible : " + path;
            return Status::Unreadable;
        }
        Layout L;
        std::string w, json;
        if (!layoutOf(f, L, &w) || !headerText(f, L.header, json, &w) || !fromJson(json, out, &w)) {
            if (why) *why = (w.empty() ? std::string("illisible") : w) + " : " + path;
            return Status::Unreadable;
        }
        if (L.cut) out.cut = true;
        return Status::Ok;
    } catch (const std::bad_alloc&) {
        if (why) *why = "m\xC3\xA9moire insuffisante pour lire l'en-t\xC3\xAAte : " + path;
        return Status::Unreadable;
    }
}

Status readTrack(const std::string& path, const std::string& password, Header& header, Track& out, std::string* why) {
    try {
        std::error_code ec;
        if (!fs::is_regular_file(P(path), ec)) {
            if (why) *why = "introuvable : " + path;
            return Status::NotFound;
        }
        InFile f(path);
        if (!f.ok()) {
            if (why) *why = "illisible : " + path;
            return Status::Unreadable;
        }
        Layout L;
        std::string w, json;
        if (!layoutOf(f, L, &w) || !headerText(f, L.header, json, &w) || !fromJson(json, header, &w)) {
            if (why) *why = (w.empty() ? std::string("illisible") : w) + " : " + path;
            return Status::Unreadable;
        }
        // Le mot de passe d'abord : un refus ne lit rien de la piste.
        if (header.locked()) {
            if (password.empty()) {
                if (why) *why = "\xC2\xAB " + header.name + " \xC2\xBB est prot\xC3\xA9g\xC3\xA9 par un mot de passe";
                return Status::PasswordNeeded;
            }
            if (!passwordOk(header, password)) {
                if (why) *why = "mot de passe incorrect";
                return Status::BadPassword;
            }
        }
        const std::string key = trackKey(header, password);
        out = Track{};
        Budget budget;
        bool truncated = false;
        const int r = eachTrackBlock(f, L, header, [&](BlockView& b) {
            std::string raw;
            if (!openBlock(b, key, header.nonce, raw)) {
                truncated = true;
                return false;
            }
            if (!budget.takeRaw(raw.size())) return false;
            parseRecords(raw, out, budget);
            if (!budget.why.empty()) return false;
            ++out.blocks;
            out.end = std::max(out.end, b.tEnd / 1000.0);
            return true;
        }, truncated, &w);
        if (r < 0 || !budget.why.empty()) {
            out = Track{};
            if (why) *why = (r < 0 ? w : budget.why) + " : " + path;
            return Status::Unreadable;
        }
        out.truncated = truncated;
        if (out.truncated) header.cut = true;
        return Status::Ok;
    } catch (const std::bad_alloc&) {
        out = Track{};
        if (why) *why = "m\xC3\xA9moire insuffisante pour relire la piste : " + path;
        return Status::Unreadable;
    }
}

void markActive(const std::string& partPath, bool active) {
    std::lock_guard<std::mutex> lock(activeLock());
    if (active) activeParts().insert(partPath);
    else activeParts().erase(partPath);
}

namespace {
// Le format 1 : "XRECPART", la longueur de l'en-tete, l'en-tete, puis les blocs ; recopie dans un zip.
bool recoverLegacy(const std::string& claimed, const std::string& partPath, std::string* finalPath, std::string* why) {
    Header h;
    std::string blocks;
    if (!readPart(claimed, h, blocks, why)) return false;
    bool truncated = false;
    const auto list = blocksOf(blocks, 0, &truncated);
    std::string kept;
    double end = 0;
    for (const auto& b : list) {
        kept += std::string(kBlockMagic, 4);
        kept.push_back(static_cast<char>(b.flags));
        put32(kept, b.seq);
        put32(kept, b.tEnd);
        put32(kept, b.rawLen);
        put32(kept, static_cast<std::uint32_t>(b.stored.size()));
        put32(kept, crcOf(b.stored));
        kept += b.stored;
        end = std::max(end, b.tEnd / 1000.0);
    }
    h.duration = end;
    h.cut = true;
    h.trackBytes = kept.size();
    const std::string folder = U(P(partPath).parent_path());
    std::string name = nameOfPath(partPath);
    std::error_code ec;
    if (fs::exists(P(pathOf(folder, name)), ec)) name = uniqueName(folder, name);
    h.name = name;
    const std::string target = pathOf(folder, name);
    if (!writeAll(target, zipStored({{std::string(kHeaderEntry), toJson(h)}, {std::string(kTrackEntry), kept}}), why)) return false;
    fs::remove(P(claimed), ec);
    if (finalPath) *finalPath = target;
    return true;
}
} // namespace

bool recoverPart(const std::string& partPath, std::string* finalPath, std::string* why) {
    const auto fail = [why](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    std::error_code ec;
    // PRENDRE le .part d'abord : ouvert par un autre programme (un autre IHM Studio qui enregistre), on n'y touche pas ;
    // renomme, une autre fenetre qui liste le meme dossier ne le trouve plus (deux .xrec du meme enregistrement).
    const std::string claimed = partPath + ".recup-" + randomHex(4);
    std::FILE* f = nullptr;
#if defined(_WIN32)
    fs::rename(P(partPath), P(claimed), ec);           // un fichier ouvert ailleurs ne se renomme pas
    if (ec) return fail("en cours d'\xC3\xA9" "criture par un autre programme, ou introuvable (" + ec.message() + ")");
    f = openFile(claimed, "r+b");
    if (!f) {
        fs::rename(P(claimed), P(partPath), ec);
        return fail("illisible : " + partPath);
    }
#else
    f = openFile(partPath, "r+b");
    if (!f) return fail("introuvable : " + partPath);
    if (::flock(::fileno(f), LOCK_EX | LOCK_NB) != 0) {
        std::fclose(f);
        return fail("en cours d'\xC3\xA9" "criture par un autre programme");
    }
    fs::rename(P(partPath), P(claimed), ec);
    if (ec) {
        std::fclose(f);
        return fail("d\xC3\xA9j\xC3\xA0 pris par une autre fen\xC3\xAAtre (" + ec.message() + ")");
    }
#endif
    const auto giveBack = [&](std::string w) {
        if (f) std::fclose(f);
        f = nullptr;
        fs::rename(P(claimed), P(partPath), ec);
        return fail(std::move(w));
    };
    // Le format 1.
    {
        char magic[8] = {};
        if (std::fread(magic, 1, 8, f) == 8 && std::memcmp(magic, kPartMagic, 8) == 0) {
            std::fclose(f);
            f = nullptr;
            if (!recoverLegacy(claimed, partPath, finalPath, why)) {
                fs::rename(P(claimed), P(partPath), ec);
                return false;
            }
            return true;
        }
    }
    Header h;
    ZipEntry first, second;
    std::uint64_t validEnd = 0;
    std::uint32_t crc = 0;
    Track tr;
    bool counted = false;
    double end = 0;
    {
        InFile in(claimed);
        std::string json, w;
        if (!in.ok() || !localAt(in, 0, first) || first.name != kHeaderEntry || !headerText(in, first, json, &w) || !fromJson(json, h, &w))
            return giveBack("pas un enregistrement en cours lisible : " + partPath + (w.empty() ? std::string{} : " (" + w + ")"));
        if (!localAt(in, first.data + first.csize, second) || second.name != kTrackEntry)
            return giveBack("piste introuvable : " + partPath);
        // Les blocs entiers, jusqu'au premier abime (la fin d'un bloc a moitie ecrit) ; les comptes, la piste en clair.
        const std::string key = trackKey(h, {});
        Budget budget;
        validEnd = second.data;
        (void)blocksIn(in, second.data, in.size(), [&](BlockView& b) {
            std::string whole(kBlockMagic, 4);
            whole.push_back(static_cast<char>(b.flags));
            put32(whole, b.seq);
            put32(whole, b.tEnd);
            put32(whole, b.rawLen);
            put32(whole, static_cast<std::uint32_t>(b.stored.size()));
            put32(whole, crcOf(b.stored));
            whole += b.stored;
            crc = static_cast<std::uint32_t>(mz_crc32(crc, reinterpret_cast<const unsigned char*>(whole.data()), whole.size()));
            end = std::max(end, b.tEnd / 1000.0);
            std::string raw;
            if (budget.why.empty() && openBlock(b, key, h.nonce, raw) && budget.takeRaw(raw.size())) {
                parseRecords(raw, tr, budget);
                counted = budget.why.empty();
            }
            return true;
        }, validEnd);
    }
    if (counted) {
        h.frames = tr.frames.size();
        h.keys = tr.keys.size();
        h.events = tr.events.size();
        h.clicks = static_cast<std::size_t>(std::count_if(tr.events.begin(), tr.events.end(), [](const Event& e) {
            return e.kind == EventKind::Click || e.kind == EventKind::LongPress || e.kind == EventKind::DoubleClick;
        }));
        h.alarms = static_cast<std::size_t>(std::count_if(tr.events.begin(), tr.events.end(), [](const Event& e) { return e.kind == EventKind::AlarmAppear; }));
        std::vector<std::string> views;
        for (const auto& sc : tr.screens)
            if (std::find(views.begin(), views.end(), sc.view) == views.end()) views.push_back(sc.view);
        h.views = views.size();
        h.viewsShown.clear();
        for (const auto& v : views) h.viewsShown += (h.viewsShown.empty() ? "" : ";") + v;
        end = std::max(end, tr.end);
        for (const auto& sc : tr.screens) {
            if (sc.t > end * 0.4 + 1e-9 && !h.at40.empty()) break;
            h.at40 = sc.view;
            for (const auto& pp : sc.popups) h.at40 += " + " + pp.view;
        }
    }
    h.duration = end;
    h.cut = true;
    h.trackBytes = validEnd - second.data;
    const std::string folder = U(P(partPath).parent_path());
    std::string name = nameOfPath(partPath);
    if (fs::exists(P(pathOf(folder, name)), ec)) name = uniqueName(folder, name);
    h.name = name;
    const std::string target = pathOf(folder, name);
    std::FILE* out = f;
    f = nullptr;
    if (!finalizeFile(out, claimed, target, toJson(h), static_cast<std::size_t>(first.csize), second.local, second.data, validEnd - second.data, crc,
                      dosNow(), why)) {
        fs::rename(P(claimed), P(partPath), ec);
        return false;
    }
    if (finalPath) *finalPath = target;
    return true;
}

std::vector<Item> listFolder(const std::string& folder, std::vector<std::string>* notes) {
    std::vector<Item> out;
    std::error_code ec;
    const fs::path dir = P(folder);
    if (folder.empty() || !fs::is_directory(dir, ec)) return out;
    // Les .part abandonnes d'abord : ils deviennent des .xrec.
    std::vector<fs::path> parts;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string leaf = U(it->path().filename());
        if (endsWith(leaf, std::string(kExtension) + std::string(kPartSuffix))) parts.push_back(it->path());
    }
    for (const auto& p : parts) {
        {
            std::lock_guard<std::mutex> lock(activeLock());
            if (activeParts().count(U(p))) continue;
        }
        const std::int64_t age = static_cast<std::int64_t>(std::time(nullptr)) - mtimeOf(p);
        if (age < 10) continue;
        std::string target, why;
        if (recoverPart(U(p), &target, &why)) {
            if (notes) notes->push_back("enregistrement coup\xC3\xA9 net relu jusqu'au dernier bloc : " + nameOfPath(target));
        } else if (notes && why.find("autre programme") == std::string::npos && why.find("autre fen") == std::string::npos) {
            notes->push_back("enregistrement en cours abandonn\xC3\xA9, illisible : " + U(p.filename()) + " (" + why + ")");
        }
    }
    ec.clear();
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string leaf = U(it->path().filename());
        if (!endsWith(leaf, kExtension)) continue;
        Item item;
        item.path = U(it->path());
        item.name = nameOfPath(item.path);
        item.bytes = static_cast<std::uint64_t>(it->file_size(ec));
        item.modified = mtimeOf(it->path());
        // Seul l'en-tete, et seulement s'il a change (taille, date du fichier).
        (void)cachedHeader(item.path, item.bytes, stampOf(it->path()), item.header, item.problem);
        if (item.header.name.empty()) item.header.name = item.name;
        out.push_back(std::move(item));
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) {
        if (a.header.date != b.header.date) return a.header.date > b.header.date;
        return a.name < b.name;
    });
    return out;
}

std::vector<Item> withPrefix(const std::vector<Item>& items, std::string_view prefix) {
    const auto upper = [](std::string_view s) {
        std::string out(s);
        for (auto& ch : out) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return out;
    };
    std::size_t a = 0, b = prefix.size();
    while (a < b && std::isspace(static_cast<unsigned char>(prefix[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(prefix[b - 1]))) --b;
    const std::string pre = upper(prefix.substr(a, b - a));
    if (pre.empty()) return items;
    std::vector<Item> out;
    for (const auto& it : items)
        if (upper(it.name).rfind(pre, 0) == 0) out.push_back(it);
    return out;
}

std::vector<std::string> fileNames(const std::string& folder) {
    std::vector<std::string> out;
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(P(folder), ec)) return out;
    for (fs::directory_iterator it(P(folder), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && endsWith(U(it->path().filename()), kExtension)) out.push_back(nameOfPath(U(it->path())));
    return out;
}

std::string uniqueName(const std::string& folder, std::string_view wanted) {
    const std::string base = cleanName(wanted);
    std::error_code ec;
    if (!fs::exists(P(pathOf(folder, base)), ec)) return base;
    for (int n = 2; n < 10000; ++n) {
        const std::string candidate = base + " (" + std::to_string(n) + ")";
        if (!fs::exists(P(pathOf(folder, candidate)), ec)) return candidate;
    }
    return base + " (" + std::to_string(std::time(nullptr)) + ")";
}

bool importFile(const std::string& source, const std::string& folder, std::string& name, std::string* why) {
    Header h;
    if (const Status st = readHeader(source, h, why); st != Status::Ok) return false;
    if (folder.empty()) {
        if (why) *why = "pas de dossier des enregistrements (le projet n'est pas enregistr\xC3\xA9)";
        return false;
    }
    std::string wanted = name.empty() ? nameOfPath(source) : name;
    name = uniqueName(folder, wanted);
    const std::string target = pathOf(folder, name);
    std::error_code ec;
    fs::create_directories(P(folder), ec);
    fs::copy_file(P(source), P(target + ".tmp"), fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (why) *why = "copie impossible : " + ec.message();
        fs::remove(P(target + ".tmp"), ec);
        return false;
    }
    return replaceFile(target + ".tmp", target, why);
}

bool exportFile(const std::string& folder, std::string_view name, const std::string& destination, std::string* written, std::string* why,
                const ExportOptions& options) {
    const auto fail = [&](std::string w, Status st) {
        if (why) *why = std::move(w);
        if (options.status) *options.status = st;
        return false;
    };
    if (options.exists) *options.exists = false;
    if (options.status) *options.status = Status::Ok;
    std::string w;
    if (!validName(name, &w)) return fail("nom d'enregistrement refus\xC3\xA9 : " + w, Status::NotFound);
    const std::string from = pathOf(folder, name);
    std::error_code ec;
    if (!fs::is_regular_file(P(from), ec)) return fail("enregistrement introuvable : " + std::string(name), Status::NotFound);
    const fs::path dest = P(destination);
    const bool isFolder = destination.empty() || destination.back() == '/' || destination.back() == '\\' || fs::is_directory(dest, ec)
                       || !endsWith(destination, kExtension);
    fs::path target = isFolder ? dest / P(std::string(name) + std::string(kExtension)) : dest;
    if (fs::exists(target, ec)) {
        if (fs::equivalent(target, P(from), ec)) return fail("c'est le fichier lui-m\xC3\xAAme : " + U(target), Status::Unreadable);
        if (options.ifExists == ExportOptions::IfExists::Refuse) {
            if (options.exists) *options.exists = true;
            return fail("\xC2\xAB " + U(target.filename()) + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0 dans " + U(target.parent_path()), Status::Unreadable);
        }
        if (options.ifExists == ExportOptions::IfExists::FreeName) {
            const std::string stem = nameOfPath(U(target));
            for (int n = 2; n < 10000 && fs::exists(target, ec); ++n)
                target = target.parent_path() / P(stem + " (" + std::to_string(n) + ")" + std::string(kExtension));
        }
    }
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    const std::string out = U(target);
    fs::copy_file(P(from), P(out + ".tmp"), fs::copy_options::overwrite_existing, ec);
    if (ec) {
        fs::remove(P(out + ".tmp"), ec);
        return fail("copie impossible : " + ec.message(), Status::Unreadable);
    }
    // Sans le mot de passe dans la copie : elle est rechiffree sans protection (il faut le bon).
    if (!options.unprotect.empty()) {
        Header h;
        if (readHeader(out + ".tmp", h) == Status::Ok && h.locked()) {
            const Status st = setPassword(out + ".tmp", options.unprotect, {}, &w);
            if (st != Status::Ok) {
                fs::remove(P(out + ".tmp"), ec);
                return fail(w, st);
            }
        }
    }
    if (!replaceFile(out + ".tmp", out, why)) return false;
    if (written) *written = out;
    return true;
}

bool renameFile(const std::string& folder, std::string_view from, std::string_view to, std::string* why) {
    std::string w;
    if (!validName(from, &w)) {
        if (why) *why = "nom d'enregistrement refus\xC3\xA9 : " + w;
        return false;
    }
    if (!validName(to, why)) return false;
    const std::string a = pathOf(folder, from), b = pathOf(folder, to);
    std::error_code ec;
    if (!fs::is_regular_file(P(a), ec)) {
        if (why) *why = "enregistrement introuvable : " + std::string(from);
        return false;
    }
    if (a != b && fs::exists(P(b), ec) && lower(a) != lower(b)) {
        if (why) *why = "\xC2\xAB " + std::string(to) + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0";
        return false;
    }
    // Le nom de l'en-tete suit le fichier (sur place : rien n'est recopie).
    Header h;
    if (readHeader(a, h) == Status::Ok && !h.cut) {
        h.name = std::string(to);
        if (!rewriteHeader(a, h, why)) return false;
    }
    if (a == b) return true;
    fs::rename(P(a), P(b), ec);
    if (ec) {
        if (why) *why = "renommage impossible : " + ec.message();
        return false;
    }
    return true;
}

bool removeFile(const std::string& folder, std::string_view name, const std::string& trash, std::string* why) {
    std::string w;
    if (!validName(name, &w)) {
        if (why) *why = "nom d'enregistrement refus\xC3\xA9 : " + w;
        return false;
    }
    const fs::path p = P(pathOf(folder, name));
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) {
        if (why) *why = "enregistrement introuvable : " + std::string(name);
        return false;
    }
    if (!trash.empty()) {
        fs::create_directories(P(trash), ec);
        fs::path target = P(trash) / p.filename();
        for (int n = 2; fs::exists(target, ec) && n < 10000; ++n)
            target = P(trash) / P(std::string(name) + " (" + std::to_string(n) + ")" + std::string(kExtension));
        fs::rename(p, target, ec);
        if (!ec) return true;
        ec.clear();
        fs::copy_file(p, target, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            fs::remove(p, ec);
            return !ec;
        }
    }
    fs::remove(p, ec);
    if (ec) {
        if (why) *why = "suppression impossible : " + ec.message();
        return false;
    }
    return true;
}

Status setPassword(const std::string& path, const std::string& current, const std::string& fresh, std::string* why) {
    try {
        Header h, n;
        std::string w;
        bool bad = false;
        {
            std::error_code ec;
            if (!fs::is_regular_file(P(path), ec)) {
                if (why) *why = "introuvable : " + path;
                return Status::NotFound;
            }
            InFile in(path);
            Layout L;
            std::string json;
            if (!in.ok() || !layoutOf(in, L, &w) || !headerText(in, L.header, json, &w) || !fromJson(json, h, &w)) {
                if (why) *why = (w.empty() ? std::string("illisible") : w) + " : " + path;
                return Status::Unreadable;
            }
            if (h.locked()) {
                if (current.empty()) {
                    if (why) *why = "le mot de passe actuel est demand\xC3\xA9";
                    return Status::PasswordNeeded;
                }
                if (!passwordOk(h, current)) {
                    if (why) *why = "mot de passe actuel incorrect";
                    return Status::BadPassword;
                }
            }
            // UNE CLE NEUVE : qui avait l'ancienne (l'ancien mot de passe) ne lit pas la piste rechiffree.
            const std::string oldKey = trackKey(h, current);
            const std::string oldNonce = h.nonce;
            n = h;
            n.format = kFormat;
            n.nonce = randomHex(16);
            const std::string newKey = fromHex(randomHex(32));
            protect(n, fresh, newKey);
            int r = 1;
            const bool ok = writeRecording(path + ".neuf", [&](std::uint64_t size, bool cut) {
                n.trackBytes = size;
                if (cut) n.cut = true;
                return toJson(n);
            }, [&](std::FILE* out, std::uint32_t& crc, std::uint64_t& size, bool& cut) {
                bool truncated = false;
                r = eachTrackBlock(in, L, h, [&](BlockView& b) {
                    bool good = true;
                    const std::string block = recipher(b, oldKey, oldNonce, newKey, n.nonce, &good);
                    if (!good) {
                        bad = true;
                        return false;
                    }
                    crc = static_cast<std::uint32_t>(mz_crc32(crc, reinterpret_cast<const unsigned char*>(block.data()), block.size()));
                    size += block.size();
                    return std::fwrite(block.data(), 1, block.size(), out) == block.size();
                }, truncated, &w);
                cut = truncated;
                return r > 0 && !bad;
            }, &w);
            if (!ok) {
                if (why) *why = bad ? std::string("la piste ne se rechiffre pas") : (w.empty() ? std::string("\xC3\xA9" "criture impossible : ") + path : w);
                return Status::Unreadable;
            }
        }
        if (!replaceFile(path + ".neuf", path, why)) return Status::Unreadable;
        return Status::Ok;
    } catch (const std::bad_alloc&) {
        if (why) *why = "m\xC3\xA9moire insuffisante : " + path;
        return Status::Unreadable;
    }
}

bool setDescription(const std::string& path, const std::string& description, std::string* why) {
    Header h;
    if (readHeader(path, h, why) != Status::Ok) return false;
    h.description = description;
    return rewriteHeader(path, h, why);
}

std::uint64_t folderBytes(const std::string& folder) {
    std::uint64_t total = 0;
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(P(folder), ec)) return 0;
    for (fs::directory_iterator it(P(folder), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && endsWith(U(it->path().filename()), kExtension)) total += it->file_size(ec);
    return total;
}

std::vector<std::string> prune(const std::string& folder, std::uint64_t maxBytes, std::string_view keep) {
    std::vector<std::string> removed;
    std::uint64_t total = folderBytes(folder);
    if (total <= maxBytes) return removed;
    // LES PLUS ANCIENS PAR LEUR DATE D'ENREGISTREMENT (l'en-tete) : une description changee touche le fichier, pas
    // son age. Un en-tete illisible : la date du fichier.
    struct File { fs::path path; std::string date, name; std::uint64_t bytes; };
    std::vector<File> files;
    std::error_code ec;
    for (fs::directory_iterator it(P(folder), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || !endsWith(U(it->path().filename()), kExtension)) continue;
        File f;
        f.path = it->path();
        f.bytes = static_cast<std::uint64_t>(it->file_size(ec));
        f.name = nameOfPath(U(f.path));
        Header h;
        std::string problem;
        (void)cachedHeader(U(f.path), f.bytes, stampOf(f.path), h, problem);
        f.date = h.date;
        if (f.date.empty()) {
            const std::time_t t = static_cast<std::time_t>(mtimeOf(f.path));
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char buf[32];
            std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
            f.date = buf;
        }
        files.push_back(std::move(f));
    }
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.date != b.date ? a.date < b.date : a.name < b.name; });
    for (const auto& f : files) {
        if (total <= maxBytes) break;
        if (!keep.empty() && f.name == keep) continue;
        fs::remove(f.path, ec);
        if (ec) continue;
        total -= std::min(total, f.bytes);
        removed.push_back(f.name);
    }
    return removed;
}

// ======================================================================= les noms en dur ==
namespace {
// Les appels qui nomment un enregistrement en premier argument.
bool namingCall(std::string_view upperName) {
    return upperName == "IHM_REJOUER" || upperName == "IHM_ENREG_EXISTE" || upperName == "IHM_ENREG_DUREE" || upperName == "IHM_ENREG_EXPORTER"
        || upperName == "IHM_ENREG_SUPPRIMER";
}
// Chaque litteral nommant un enregistrement : sa place (du guillemet ouvrant au fermant inclus), son texte, son
// guillemet. ST : 'texte' ('' pour une apostrophe) ; C et C++ (1.14.0, relecture M4b) : "texte" (\" et \\).
struct Literal { std::size_t at, end; std::string text; int line; char quote; };
std::vector<Literal> literals(std::string_view code) {
    std::vector<Literal> out;
    int line = 1;
    // Une chaine qui commence a `i` (son guillemet) : l'indice de son guillemet fermant (ou la fin de la ligne).
    const auto skipString = [&](std::size_t i) {
        const char q = code[i];
        std::size_t j = i + 1;
        while (j < code.size() && code[j] != '\n') {
            if (q == '"' && code[j] == '\\' && j + 1 < code.size()) { j += 2; continue; }
            if (code[j] == q) {
                if (q == '\'' && j + 1 < code.size() && code[j + 1] == '\'') { j += 2; continue; }
                break;
            }
            ++j;
        }
        return j;
    };
    for (std::size_t i = 0; i < code.size(); ++i) {
        const char c = code[i];
        if (c == '\n') { ++line; continue; }
        if (c == '\'' || c == '"') {                       // une chaine hors d'un appel : sautee
            i = skipString(i);
            continue;
        }
        if (c == '(' && i + 1 < code.size() && code[i + 1] == '*') {     // (* un commentaire *)
            std::size_t j = i + 2;
            while (j + 1 < code.size() && !(code[j] == '*' && code[j + 1] == ')')) {
                if (code[j] == '\n') ++line;
                ++j;
            }
            i = j + 1;
            continue;
        }
        if (c == '/' && i + 1 < code.size() && code[i + 1] == '*') {     // /* un commentaire C */
            std::size_t j = i + 2;
            while (j + 1 < code.size() && !(code[j] == '*' && code[j + 1] == '/')) {
                if (code[j] == '\n') ++line;
                ++j;
            }
            i = j + 1;
            continue;
        }
        if (c == '/' && i + 1 < code.size() && code[i + 1] == '/') {     // // jusqu'a la fin de la ligne
            while (i + 1 < code.size() && code[i + 1] != '\n') ++i;
            continue;
        }
        if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_') || (i > 0 && (std::isalnum(static_cast<unsigned char>(code[i - 1])) || code[i - 1] == '_')))
            continue;
        std::size_t j = i;
        while (j < code.size() && (std::isalnum(static_cast<unsigned char>(code[j])) || code[j] == '_')) ++j;
        std::string word(code.substr(i, j - i));
        for (auto& ch : word) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        i = j - 1;
        if (!namingCall(word)) continue;
        std::size_t k = j;
        while (k < code.size() && (code[k] == ' ' || code[k] == '\t')) ++k;
        if (k >= code.size() || code[k] != '(') continue;
        ++k;
        while (k < code.size() && (code[k] == ' ' || code[k] == '\t')) ++k;
        if (k >= code.size() || (code[k] != '\'' && code[k] != '"')) continue;
        const char q = code[k];
        std::size_t e = k + 1;
        std::string text;
        while (e < code.size() && code[e] != '\n') {
            if (q == '"' && code[e] == '\\' && e + 1 < code.size()) {
                text += code[e + 1];
                e += 2;
                continue;
            }
            if (code[e] == q) {
                if (q == '\'' && e + 1 < code.size() && code[e + 1] == '\'') { text += '\''; e += 2; continue; }
                break;
            }
            text += code[e++];
        }
        if (e >= code.size() || code[e] != q) continue;
        // Une chaine qui continue (concatenee : 'Essai ' + Ligne) n'est pas un nom en dur.
        std::size_t after = e + 1;
        while (after < code.size() && (code[after] == ' ' || code[after] == '\t')) ++after;
        if (after < code.size() && code[after] == '+') continue;
        out.push_back(Literal{k, e + 1, text, line, q});
        i = e;
    }
    return out;
}
bool sameName(std::string_view a, std::string_view b) { return lower(std::string(a)) == lower(std::string(b)); }
} // namespace

std::vector<NameUse> namesInCode(std::string_view code) {
    std::vector<NameUse> out;
    for (const auto& l : literals(code)) out.push_back(NameUse{l.text, l.line});
    return out;
}

std::string renameInCode(std::string_view code, std::string_view from, std::string_view to, std::size_t* count) {
    std::string out;
    std::size_t last = 0, n = 0;
    // Le nouveau nom, ecrit comme l'ancien : 'ST' (apostrophe doublee) ou "C" (\" et \\).
    const auto quoted = [&to](char q) {
        std::string s(1, q);
        for (const char c : to) {
            if (q == '\'' && c == '\'') s += '\'';
            if (q == '"' && (c == '"' || c == '\\')) s += '\\';
            s += c;
        }
        return s + q;
    };
    for (const auto& l : literals(code)) {
        if (!sameName(l.text, from)) continue;
        out.append(code.substr(last, l.at - last));
        out += quoted(l.quote);
        last = l.end;
        ++n;
    }
    out.append(code.substr(last));
    if (count) *count += n;
    return out;
}

bool literalName(std::string_view text) noexcept {
    std::size_t a = 0;
    while (a < text.size() && std::isspace(static_cast<unsigned char>(text[a]))) ++a;
    if (a >= text.size() || text[a] == '=') return false;
    return text.find('{') == std::string_view::npos && text.find('$') == std::string_view::npos;
}

std::size_t renameReferences(Project& p, std::string_view from, std::string_view to) {
    std::size_t n = 0;
    const auto actions = [&](std::vector<Action>& list) {
        for (auto& a : list) {
            if (operationNamesRecording(a.operation) && literalName(a.target)) {
                std::string t = a.target;
                while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
                while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(0, 1);
                if (sameName(t, from)) {
                    a.target = std::string(to);
                    ++n;
                }
            }
            if (a.operation == Operation::RunScript) a.value = renameInCode(a.value, from, to, &n);
        }
    };
    for (auto& sc : p.programs.scripts) sc.body = renameInCode(sc.body, from, to, &n);
    for (auto& f : p.programs.functions) f.body = renameInCode(f.body, from, to, &n);
    for (auto& v : p.views) {
        for (auto& sc : v.scripts) sc.body = renameInCode(sc.body, from, to, &n);
        for (auto& f : v.functions) f.body = renameInCode(f.body, from, to, &n);
        actions(v.actions);
        for (auto& o : v.objects) {
            actions(o.actions);
            if (o.kind == Kind::RecordingPlayer)
                if (Prop* fp = o.find("fixed"); fp && fp->expr.empty() && literalName(fp->value) && sameName(fp->value, from)) {
                    fp->value = std::string(to);
                    ++n;
                }
        }
    }
    return n;
}

// ======================================================================= les objets ==
RecorderLayout recorderLayout(double w, double h, double fontSize, bool titled) {
    RecorderLayout l;
    const double fs = std::clamp(fontSize, 8.0, 40.0);
    const double pad = std::max(6.0, fs * 0.6);
    const double titleH = titled ? fs * 1.5 : 0.0;
    l.title = Area{pad, pad * 0.5, std::max(0.0, w - 2 * pad), titleH};
    const double top = (titled ? pad * 0.5 + titleH : 0.0);
    const double avail = std::max(10.0, h - top - pad * 0.5);
    l.radius = std::max(6.0, std::min({avail / 2.0 - 2.0, fs * 1.4, w / 6.0}));
    l.cx = pad + l.radius;
    l.cy = top + avail / 2.0;
    l.button = Area{l.cx - l.radius, l.cy - l.radius, 2 * l.radius, 2 * l.radius};
    const double tx = l.cx + l.radius + pad;
    l.label = Area{tx, l.cy - fs * 1.15, std::max(0.0, w - tx - pad), fs * 1.2};
    l.status = Area{tx, l.cy + fs * 0.1, std::max(0.0, w - tx - pad), fs * 1.1};
    return l;
}

PlayerLayout playerLayout(double w, double h, double fontSize, std::size_t rows, bool fixed, bool titled) {
    PlayerLayout l;
    const double fs = std::clamp(fontSize, 8.0, 40.0);
    const double pad = std::max(6.0, fs * 0.5);
    const double titleH = titled ? fs * 1.8 : 0.0;
    l.title = Area{pad, 0, std::max(0.0, w - 2 * pad), titleH};
    l.list = Area{0, titleH, w, std::max(0.0, h - titleH)};
    if (fixed) {
        const double bh = std::min(l.list.h - 2 * pad, std::max(32.0, fs * 2.6));
        l.fixedButton = Area{pad, titleH + (l.list.h - bh) / 2.0, std::max(0.0, w - 2 * pad), std::max(0.0, bh)};
        return l;
    }
    l.rowHeight = std::max(28.0, fs * 2.2);
    l.visible = static_cast<std::size_t>(std::max(0.0, std::floor(l.list.h / l.rowHeight)));
    const double play = std::max(32.0, l.rowHeight);
    for (std::size_t i = 0; i < rows && i < l.visible; ++i) {
        const double y = titleH + l.rowHeight * static_cast<double>(i);
        l.rows.push_back(Area{0, y, w, l.rowHeight});
        l.plays.push_back(Area{std::max(0.0, w - play - pad * 0.5), y + (l.rowHeight - std::min(play, l.rowHeight)) / 2.0, play, std::min(play, l.rowHeight)});
    }
    return l;
}

std::string playerHit(const PlayerLayout& l, double x, double y) {
    if (l.fixedButton.w > 0) return l.fixedButton.contains(x, y) ? std::string("jouer") : std::string{};
    for (std::size_t i = 0; i < l.rows.size(); ++i) {
        if (l.plays[i].contains(x, y)) return "jouer:" + std::to_string(i);
        if (l.rows[i].contains(x, y)) return "ligne:" + std::to_string(i);
    }
    return {};
}

// ======================================================================= la ligne de commande ==
std::optional<double> parseDuration(std::string_view text) {
    std::string t;
    for (const char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t.empty()) return std::nullopt;
    // 00:10:00, 10:00
    if (t.find(':') != std::string::npos) {
        double total = 0;
        std::size_t at = 0;
        int parts = 0;
        while (at <= t.size()) {
            const auto colon = t.find(':', at);
            const std::string piece = t.substr(at, colon == std::string::npos ? std::string::npos : colon - at);
            if (piece.empty() || piece.find_first_not_of("0123456789.") != std::string::npos) return std::nullopt;
            total = total * 60 + std::strtod(piece.c_str(), nullptr);
            ++parts;
            if (colon == std::string::npos) break;
            at = colon + 1;
        }
        if (parts > 3) return std::nullopt;
        return total;
    }
    // 1h30m, 10m, 90s, 600, 1,5h
    for (auto& c : t)
        if (c == ',') c = '.';
    double total = 0;
    std::size_t i = 0;
    bool any = false;
    while (i < t.size()) {
        std::size_t j = i;
        while (j < t.size() && (std::isdigit(static_cast<unsigned char>(t[j])) || t[j] == '.')) ++j;
        if (j == i) return std::nullopt;
        const double v = std::strtod(t.substr(i, j - i).c_str(), nullptr);
        std::size_t k = j;
        while (k < t.size() && std::isalpha(static_cast<unsigned char>(t[k]))) ++k;
        const std::string unit = t.substr(j, k - j);
        double scale = 0;
        if (unit.empty() || unit == "s" || unit == "sec") scale = 1;
        else if (unit == "m" || unit == "min" || unit == "mn") scale = 60;
        else if (unit == "h") scale = 3600;
        else if (unit == "ms") scale = 0.001;
        else return std::nullopt;
        if (unit.empty() && k < t.size()) return std::nullopt;
        total += v * scale;
        any = true;
        i = k;
    }
    if (!any) return std::nullopt;
    return total;
}

Command parseCommand(const std::vector<std::string>& args) {
    Command c;
    const auto value = [&](std::size_t& i, const char* what) -> std::string {
        if (i + 1 >= args.size() || (args[i + 1].rfind("--", 0) == 0 && args[i + 1].size() > 2)) {
            if (c.error.empty()) c.error = std::string(what) + " : une valeur est attendue";
            return {};
        }
        return args[++i];
    };
    const auto kind = [&](Command::Kind k, const std::string& option) {
        if (c.kind != Command::Kind::None && c.kind != k && c.error.empty()) c.error = option + " : une seule commande d'enregistrement \xC3\xA0 la fois";
        c.kind = k;
    };
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--enregistrer") {
            kind(Command::Kind::Record, a);
            c.name = value(i, "--enregistrer");
        } else if (a == "--arreter-enregistrement") {
            kind(Command::Kind::Stop, a);
        } else if (a == "--rejouer") {
            kind(Command::Kind::Replay, a);
            c.name = value(i, "--rejouer");
            if (c.name.empty() && c.error.empty()) c.error = "--rejouer : le nom de l'enregistrement est attendu";
        } else if (a == "--quitter-relecture") {
            kind(Command::Kind::QuitReplay, a);
        } else if (a == "--duree") {
            const std::string v = value(i, "--duree");
            if (const auto d = parseDuration(v); d && *d > 0) c.duration = *d;
            else if (c.error.empty()) c.error = "--duree : dur\xC3\xA9" "e illisible (" + v + ") - 10m, 90s, 1h30m";
        } else if (a == "--vitesse") {
            const std::string v = value(i, "--vitesse");
            std::string t = v;
            for (auto& ch : t)
                if (ch == ',') ch = '.';
            char* end = nullptr;
            const double s = std::strtod(t.c_str(), &end);
            if (!t.empty() && end && *end == '\0' && s > 0) c.speed = s;
            else if (c.error.empty()) c.error = "--vitesse : vitesse illisible (" + v + ") - 0,25 0,5 1 2 4 8";
        } else if (a == "--mot-de-passe") {
            c.password = value(i, "--mot-de-passe");
        } else if (a == "--jeter") {
            c.discard = true;
        } else if (a == "--reponse") {
            c.reply = value(i, "--reponse");
        }
    }
    if (c.kind == Command::Kind::None && c.error.empty()
        && (c.discard || c.duration > 0 || !c.password.empty() || c.speed != 1.0))
        c.error = "--duree, --vitesse, --mot-de-passe et --jeter accompagnent --enregistrer, --rejouer ou --arreter-enregistrement";
    return c;
}

std::vector<std::string> commandArgs(const Command& c) {
    std::vector<std::string> out;
    switch (c.kind) {
        case Command::Kind::Record:
            out = {"--enregistrer", c.name};
            if (c.duration > 0) out.insert(out.end(), {"--duree", number(c.duration)});
            break;
        case Command::Kind::Stop:
            out = {"--arreter-enregistrement"};
            if (c.discard) out.push_back("--jeter");
            break;
        case Command::Kind::Replay:
            out = {"--rejouer", c.name};
            if (c.speed != 1) out.insert(out.end(), {"--vitesse", number(c.speed)});
            break;
        case Command::Kind::QuitReplay: out = {"--quitter-relecture"}; break;
        case Command::Kind::None: break;
    }
    if (!c.password.empty()) out.insert(out.end(), {"--mot-de-passe", c.password});
    if (!c.reply.empty()) out.insert(out.end(), {"--reponse", c.reply});
    return out;
}

namespace {
constexpr std::string_view kReplyEnd = "\n#fin\n";            // une reponse entiere finit ainsi
constexpr std::string_view kReplyPrefix = "xpg-enregistrement-";
} // namespace

std::string replyText(int code, std::string_view message) { return std::to_string(code) + "\n" + std::string(message) + std::string(kReplyEnd); }

bool readReply(std::string_view text, int& code, std::string& message) {
    // Pas encore fini d'ecrire (la fin n'y est pas) : rien.
    if (text.size() < kReplyEnd.size() || text.substr(text.size() - kReplyEnd.size()) != kReplyEnd) return false;
    text.remove_suffix(kReplyEnd.size());
    const auto nl = text.find('\n');
    const std::string head(text.substr(0, nl));
    if (head.empty() || head.find_first_not_of("0123456789") != std::string::npos) return false;
    code = std::atoi(head.c_str());
    message = nl == std::string_view::npos ? std::string{} : std::string(text.substr(nl + 1));
    return true;
}

std::string newReplyPath() {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string digits = std::to_string(stamp < 0 ? -stamp : stamp);
    digits += std::to_string(std::strtoull(randomHex(4).c_str(), nullptr, 16));   // deux commandes dans la meme tranche
    return U(dir / P(std::string(kReplyPrefix) + digits + ".txt"));
}

bool replyPathOk(const std::string& path, std::string* why) {
    const auto no = [why](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    const fs::path p = P(path);
    const std::string leaf = U(p.filename());
    // Le nom : xpg-enregistrement-<chiffres>.txt, rien d'autre.
    if (leaf.size() <= kReplyPrefix.size() + 4 || leaf.compare(0, kReplyPrefix.size(), kReplyPrefix) != 0 || !endsWith(leaf, ".txt"))
        return no("--reponse : seulement un fichier xpg-enregistrement-<chiffres>.txt du dossier temporaire");
    const std::string digits = leaf.substr(kReplyPrefix.size(), leaf.size() - kReplyPrefix.size() - 4);
    if (digits.empty() || digits.size() > 40 || digits.find_first_not_of("0123456789") != std::string::npos)
        return no("--reponse : seulement un fichier xpg-enregistrement-<chiffres>.txt du dossier temporaire");
    // Le dossier : le dossier temporaire lui-meme (pas un lien, pas un sous-dossier).
    std::error_code ec;
    const fs::path temp = fs::temp_directory_path(ec);
    if (ec || !p.has_parent_path() || !fs::equivalent(p.parent_path(), temp, ec) || ec)
        return no("--reponse : seulement un fichier du dossier temporaire (" + U(temp) + ")");
    // Un fichier NEUF : rien ne doit y etre deja (ni fichier, ni lien).
    if (fs::exists(fs::symlink_status(p, ec))) return no("--reponse : le fichier existe d\xC3\xA9j\xC3\xA0 (" + path + ")");
    return true;
}

bool writeReply(const std::string& path, int code, std::string_view message, std::string* why) {
    if (!replyPathOk(path, why)) return false;
    const std::string text = replyText(code, message);
    // Cree en exclusivite : deja la (un autre l'a cree entre-temps), refuse.
#if defined(_WIN32)
    const int fd = _wopen(P(path).wstring().c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) {
        if (why) *why = "--reponse : cr\xC3\xA9" "ation impossible (" + path + ")";
        return false;
    }
    const bool ok = _write(fd, text.data(), static_cast<unsigned>(text.size())) == static_cast<int>(text.size());
    _close(fd);
#else
    const int fd = ::open(P(path).string().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) {
        if (why) *why = "--reponse : cr\xC3\xA9" "ation impossible (" + path + ")";
        return false;
    }
    const bool ok = ::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
    ::close(fd);
#endif
    if (!ok && why) *why = "--reponse : \xC3\xA9" "criture interrompue (" + path + ")";
    return ok;
}

std::string clock(double seconds) {
    const long long s = static_cast<long long>(std::floor(std::max(0.0, seconds)));
    char buf[32];
    if (s >= 3600) std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(buf, sizeof buf, "%02lld:%02lld", s / 60, s % 60);
    return buf;
}

std::string sizeText(std::uint64_t bytes) {
    char buf[32];
    if (bytes < 1024) std::snprintf(buf, sizeof buf, "%llu o", static_cast<unsigned long long>(bytes));
    else if (bytes < 1024ull * 1024) std::snprintf(buf, sizeof buf, "%llu Ko", static_cast<unsigned long long>((bytes + 512) / 1024));
    else if (bytes < 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f Mo", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof buf, "%.2f Go", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    std::string s = buf;
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}

} // namespace hmi::rec
