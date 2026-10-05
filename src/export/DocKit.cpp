// =============================================================================
//  export/DocKit.cpp - 1.8.0 : voir DocKit.hpp
// =============================================================================
#include "DocKit.hpp"

#if defined(XPG_HAVE_MINIZ)
#include "miniz.h"
#endif

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace exporter::doc {

namespace {

void put16(Bytes& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
void put32(Bytes& out, std::uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, (v >> 16) & 0xFFFF);
}
void putBytes(Bytes& out, std::string_view s) { out.insert(out.end(), s.begin(), s.end()); }

// CRC-32 (celui du zip), sans table globale a initialiser.
std::uint32_t zipCrc(std::string_view data) {
#if defined(XPG_HAVE_MINIZ)
    return static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(data.data()), data.size()));
#else
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (const char ch : data) c = table[(c ^ static_cast<unsigned char>(ch)) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
#endif
}

// Deflate brut (le zip) ; vide : pas de compresseur, ou rien de gagne.
std::string deflateRaw(std::string_view data) {
#if defined(XPG_HAVE_MINIZ)
    std::size_t outLen = 0;
    void* p = tdefl_compress_mem_to_heap(data.data(), data.size(), &outLen, 256);
    if (!p) return {};
    std::string out(static_cast<const char*>(p), outLen);
    mz_free(p);
    return out;
#else
    (void)data;
    return {};
#endif
}

constexpr short kHelvetica[95] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556, 556,
    556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278, 556,
    556, 222, 222, 500, 222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584};
constexpr short kHelveticaBold[95] = {
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556, 556,
    556, 556, 333, 333, 584, 584, 584, 611, 975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556, 333, 556, 611, 556, 611, 556, 333, 611,
    611, 278, 278, 556, 278, 889, 611, 611, 611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584};

double charWidth(unsigned char c, bool bold) {
    const short* t = bold ? kHelveticaBold : kHelvetica;
    if (c >= 32 && c <= 126) return t[c - 32];
    if (c >= 0xE0 && c <= 0xE5) return 556;                  // a accentues
    if (c >= 0xE8 && c <= 0xEB) return 556;                  // e accentues
    if ((c >= 0xEC && c <= 0xEF) || (c >= 0xCC && c <= 0xCF)) return bold ? 278 : 250;
    if (c == 0xE7) return 500;                               // c cedille
    if (c >= 0xC0 && c <= 0xDE) return 722;
    if (c == 0x85) return 1000;
    if (c == 0xAB || c == 0xBB) return 556;
    if (c == 0x96) return 556;
    if (c == 0x97) return 1000;
    return 556;
}

} // namespace

std::string xmlEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r') out += ' ';
                else out += c;
        }
    }
    return out;
}

std::string columnName(std::size_t k) {
    std::string out;
    ++k;
    while (k > 0) {
        const std::size_t r = (k - 1) % 26;
        out.insert(out.begin(), static_cast<char>('A' + r));
        k = (k - 1) / 26;
    }
    return out;
}

Bytes zip(const std::vector<std::pair<std::string, std::string>>& items, bool compress) {
    Bytes out, central;
    const std::uint16_t time = 0, date = static_cast<std::uint16_t>(((2026 - 1980) << 9) | (1 << 5) | 1);
    for (const auto& [name, data] : items) {
        const auto offset = static_cast<std::uint32_t>(out.size());
        const auto crc = zipCrc(data);
        std::string packed = compress ? deflateRaw(data) : std::string{};
        const bool deflated = !packed.empty() && packed.size() < data.size();
        const std::string_view body = deflated ? std::string_view(packed) : std::string_view(data);
        const auto method = static_cast<std::uint32_t>(deflated ? 8 : 0);
        const auto size = static_cast<std::uint32_t>(data.size());
        const auto csize = static_cast<std::uint32_t>(body.size());
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, 0x0800);
        put16(out, method);
        put16(out, time);
        put16(out, date);
        put32(out, crc);
        put32(out, csize);
        put32(out, size);
        put16(out, static_cast<std::uint32_t>(name.size()));
        put16(out, 0);
        putBytes(out, name);
        putBytes(out, body);
        put32(central, 0x02014b50);
        put16(central, 20);
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, method);
        put16(central, time);
        put16(central, date);
        put32(central, crc);
        put32(central, csize);
        put32(central, size);
        put16(central, static_cast<std::uint32_t>(name.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, offset);
        putBytes(central, name);
    }
    const auto centralAt = static_cast<std::uint32_t>(out.size());
    out.insert(out.end(), central.begin(), central.end());
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<std::uint32_t>(items.size()));
    put16(out, static_cast<std::uint32_t>(items.size()));
    put32(out, static_cast<std::uint32_t>(central.size()));
    put32(out, centralAt);
    put16(out, 0);
    return out;
}

std::string zlibCompress(std::string_view data) {
#if defined(XPG_HAVE_MINIZ)
    std::size_t outLen = 0;
    void* p = tdefl_compress_mem_to_heap(data.data(), data.size(), &outLen, 256 | TDEFL_WRITE_ZLIB_HEADER);
    if (!p) return {};
    std::string out(static_cast<const char*>(p), outLen);
    mz_free(p);
    return out;
#else
    (void)data;
    return {};
#endif
}

std::string toWinAnsi(std::string_view u) {
    std::string out;
    out.reserve(u.size());
    for (std::size_t i = 0; i < u.size();) {
        const auto c = static_cast<unsigned char>(u[i]);
        std::uint32_t cp = 0;
        std::size_t len = 1;
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0 && i + 1 < u.size()) { cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(u[i + 1]) & 0x3Fu); len = 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < u.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(u[i + 1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(u[i + 2]) & 0x3Fu);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < u.size()) { cp = 0xFFFD; len = 4; }
        else { cp = '?'; }
        i += len;
        if (cp < 0x80) { out += static_cast<char>(cp); continue; }
        if (cp >= 0xA0 && cp <= 0xFF) { out += static_cast<char>(cp); continue; }
        switch (cp) {
            case 0x20AC: out += '\x80'; break;
            case 0x201A: out += '\x82'; break;
            case 0x201E: out += '\x84'; break;
            case 0x2026: out += '\x85'; break;
            case 0x2030: out += '\x89'; break;
            case 0x0152: out += '\x8C'; break;
            case 0x2018: out += '\x91'; break;
            case 0x2019: out += '\x92'; break;
            case 0x201C: out += '\x93'; break;
            case 0x201D: out += '\x94'; break;
            case 0x2022: out += '\x95'; break;
            case 0x2013: out += '\x96'; break;
            case 0x2014: out += '\x97'; break;
            case 0x2122: out += '\x99'; break;
            case 0x0153: out += '\x9C'; break;
            case 0x0178: out += '\x9F'; break;
            case 0x202F: out += ' '; break;      // espace fine insecable
            case 0x2192: out += "->"; break;
            case 0x2190: out += "<-"; break;
            case 0x2194: out += "<->"; break;
            case 0x2264: out += "<="; break;
            case 0x2265: out += ">="; break;
            case 0x2260: out += "<>"; break;
            case 0x25B8: out += '>'; break;
            default: out += '?'; break;
        }
    }
    return out;
}

double helveticaWidth(std::string_view winAnsi, double size, bool bold) {
    double w = 0;
    for (const char c : winAnsi) w += charWidth(static_cast<unsigned char>(c), bold);
    return w * size / 1000.0;
}

std::string pdfLiteral(std::string_view s) {
    std::string out = "(";
    for (const char c : s) {
        if (c == '(' || c == ')' || c == '\\') out += '\\';
        if (c == '\r' || c == '\n') {
            out += ' ';
            continue;
        }
        out += c;
    }
    out += ')';
    return out;
}

std::string pdfNumber(double v) {
    if (std::fabs(v) < 0.0005) return "0";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    std::string s(buf);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string thousands(std::size_t n) {
    std::string d = std::to_string(n);
    std::string out;
    const std::size_t len = d.size();
    for (std::size_t i = 0; i < len; ++i) {
        out += d[i];
        const std::size_t left = len - 1 - i;
        if (left > 0 && left % 3 == 0) out += ' ';
    }
    return out;
}

} // namespace exporter::doc
