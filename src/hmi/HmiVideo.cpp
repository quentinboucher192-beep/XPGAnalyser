// =============================================================================
//  hmi/HmiVideo.cpp - 1.12.12 (lot H) : la video decodee (voir HmiVideo.hpp)
// -----------------------------------------------------------------------------
//  Le conteneur AVI (RIFF) : des morceaux [code 4 octets][taille 4 octets, en
//  petit-boutiste][donnees, completees a un nombre pair]. Les listes (RIFF,
//  LIST) portent un type (AVI , hdrl, strl, movi, rec ) puis leurs morceaux.
//  Les images d'un flux sont les morceaux « nndc » (compresse) ou « nndb »
//  (non compresse), nn le rang du flux video ; un AVI de plus d'1 Go continue
//  dans des listes RIFF AVIX (OpenDML), lues de meme.
// =============================================================================
#include "HmiVideo.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hmi::video {

namespace {

std::uint32_t le32(const Bytes& d, std::size_t at) noexcept {
    if (at > d.size() || d.size() - at < 4) return 0;
    return static_cast<std::uint32_t>(d[at]) | (static_cast<std::uint32_t>(d[at + 1]) << 8) | (static_cast<std::uint32_t>(d[at + 2]) << 16)
         | (static_cast<std::uint32_t>(d[at + 3]) << 24);
}
bool tagAt(const Bytes& d, std::size_t at, const char* t) noexcept {
    return at <= d.size() && d.size() - at >= 4 && std::memcmp(d.data() + at, t, 4) == 0;
}
std::string fourcc(const Bytes& d, std::size_t at) {
    std::string s;
    for (std::size_t k = 0; k < 4 && at + k < d.size(); ++k) {
        const char c = static_cast<char>(d[at + k]);
        if (std::isprint(static_cast<unsigned char>(c)) && c != ' ') s += c;
    }
    return s;
}
std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Les tables de Huffman de la norme JPEG (ISO/IEC 10918-1, annexe K.3), celles que stb_image_write
// ecrit aussi : un AVI Motion-JPEG de camera les omet (format AVI1) et compte sur elles.
constexpr std::uint8_t kDcLumCounts[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
constexpr std::uint8_t kDcChromCounts[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
constexpr std::uint8_t kDcValues[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::uint8_t kAcLumCounts[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
constexpr std::uint8_t kAcLumValues[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81,
    0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
    0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5,
    0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
constexpr std::uint8_t kAcChromCounts[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
constexpr std::uint8_t kAcChromValues[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08,
    0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25,
    0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4,
    0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

// Le segment DHT des quatre tables (FF C4, sa longueur, chaque table : classe et numero, 16 comptes, les valeurs).
const Bytes& standardHuffman() {
    static const Bytes segment = [] {
        Bytes s = {0xFF, 0xC4, 0, 0};
        const auto table = [&](std::uint8_t id, const std::uint8_t* counts, const std::uint8_t* values, std::size_t n) {
            s.push_back(id);
            s.insert(s.end(), counts, counts + 16);
            s.insert(s.end(), values, values + n);
        };
        table(0x00, kDcLumCounts, kDcValues, sizeof kDcValues);
        table(0x10, kAcLumCounts, kAcLumValues, sizeof kAcLumValues);
        table(0x01, kDcChromCounts, kDcValues, sizeof kDcValues);
        table(0x11, kAcChromCounts, kAcChromValues, sizeof kAcChromValues);
        const std::size_t len = s.size() - 2;
        s[2] = static_cast<std::uint8_t>(len >> 8);
        s[3] = static_cast<std::uint8_t>(len & 0xFF);
        return s;
    }();
    return segment;
}

// L'image a-t-elle ses tables de Huffman (un segment DHT avant les donnees, SOS) ?
bool hasHuffman(const std::uint8_t* d, std::size_t n) noexcept {
    std::size_t at = 2;
    while (at + 4 <= n && d[at] == 0xFF) {
        const std::uint8_t marker = d[at + 1];
        if (marker == 0xC4) return true;
        if (marker == 0xDA || marker == 0xD9) return false;
        if (marker == 0xFF) { ++at; continue; }   // un octet de bourrage
        at += 2 + ((static_cast<std::size_t>(d[at + 2]) << 8) | d[at + 3]);
    }
    return false;
}

// Une liste de morceaux [from, to) : les images du flux `prefix` (00dc, 00db), les sous-listes rec suivies.
void moviChunks(const Bytes& d, std::size_t from, std::size_t to, const std::string& prefix, AviInfo& out, int depth) {
    std::size_t at = from;
    while (at + 8 <= to && depth < 4) {
        const std::uint32_t size = le32(d, at + 4);
        const std::size_t body = at + 8;
        if (size > to - body) break;   // coupe : on garde ce qui est entier
        if (tagAt(d, at, "LIST")) {
            moviChunks(d, body + 4, body + size, prefix, out, depth + 1);   // LIST rec : un groupe de morceaux
        } else if (d[at] == static_cast<std::uint8_t>(prefix[0]) && d[at + 1] == static_cast<std::uint8_t>(prefix[1]) && d[at + 2] == 'd'
                   && (d[at + 3] == 'c' || d[at + 3] == 'b')) {
            out.frames.emplace_back(body, static_cast<std::size_t>(size));
        }
        at = body + size + (size & 1u);
    }
}

bool mjpegFourcc(std::string_view codec) {
    const std::string c = upper(codec);
    return c == "MJPG" || c == "AVRN" || c == "AVDJ" || c == "DMB1" || c == "JPEG" || c == "JPGL" || c == "MJPA" || c == "LJPG";
}

// Le codec d'une ressource (son detail : « Motion-JPEG (MJPG) », « H264 »...) est-il du Motion-JPEG ?
bool mjpegCodec(std::string_view codec) {
    const std::string c = upper(codec);
    return c.find("MOTION-JPEG") != std::string::npos || c.find("(MJPG)") != std::string::npos || mjpegFourcc(codec);
}

} // namespace

bool AviInfo::mjpeg() const noexcept { return mjpegFourcc(codec); }

bool isAvi(const Bytes& d) noexcept { return tagAt(d, 0, "RIFF") && tagAt(d, 8, "AVI "); }

bool parseAvi(const Bytes& d, AviInfo& out, std::string* error) {
    const auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    out = AviInfo{};
    if (!isAvi(d)) return fail("pas un AVI (RIFF AVI)");
    // L'en-tete : hdrl (avih, puis un strl par flux : strh, strf).
    double usPerFrame = 0;
    int videoStream = -1, stream = 0;
    std::size_t top = 0;
    // Les listes RIFF qui se suivent : AVI , puis AVIX (OpenDML, au-dela d'1 Go).
    while (top + 12 <= d.size() && tagAt(d, top, "RIFF")) {
        const std::uint32_t riffSize = le32(d, top + 4);
        const std::size_t riffEnd = std::min(d.size(), top + 8 + static_cast<std::size_t>(riffSize));
        std::size_t at = top + 12;
        while (at + 8 <= riffEnd) {
            const std::uint32_t size = le32(d, at + 4);
            const std::size_t body = at + 8, end = std::min(riffEnd, body + static_cast<std::size_t>(size));
            if (tagAt(d, at, "LIST") && tagAt(d, body, "hdrl")) {
                std::size_t h = body + 4;
                while (h + 8 <= end) {
                    const std::uint32_t hs = le32(d, h + 4);
                    const std::size_t hb = h + 8, he = std::min(end, hb + static_cast<std::size_t>(hs));
                    if (tagAt(d, h, "avih") && hs >= 40) {
                        usPerFrame = le32(d, hb);
                        if (out.width == 0) out.width = static_cast<int>(le32(d, hb + 32));
                        if (out.height == 0) out.height = static_cast<int>(le32(d, hb + 36));
                    } else if (tagAt(d, h, "LIST") && tagAt(d, hb, "strl")) {
                        // Un flux : strh (son genre, son codec, sa cadence), strf (pour une video : BITMAPINFOHEADER).
                        bool video = false;
                        std::size_t s = hb + 4;
                        while (s + 8 <= he) {
                            const std::uint32_t ss = le32(d, s + 4);
                            const std::size_t sb = s + 8;
                            if (tagAt(d, s, "strh") && ss >= 36 && tagAt(d, sb, "vids") && videoStream < 0) {
                                video = true;
                                videoStream = stream;
                                out.codec = fourcc(d, sb + 4);
                                const std::uint32_t scale = le32(d, sb + 20), rate = le32(d, sb + 24);
                                if (scale > 0 && rate > 0) out.fps = static_cast<double>(rate) / scale;
                            } else if (tagAt(d, s, "strf") && video && ss >= 20) {
                                const auto bw = static_cast<std::int32_t>(le32(d, sb + 4)), bh = static_cast<std::int32_t>(le32(d, sb + 8));
                                if (bw > 0) out.width = bw;
                                if (bh != 0) out.height = bh < 0 ? -bh : bh;
                                // Le codec de l'image (biCompression) quand l'en-tete du flux n'en dit pas un connu.
                                const std::string compression = fourcc(d, sb + 16);
                                if (!compression.empty() && !mjpegFourcc(out.codec) && (out.codec.empty() || mjpegFourcc(compression)))
                                    out.codec = compression;
                            }
                            s = sb + ss + (ss & 1u);
                        }
                        ++stream;
                    }
                    h = hb + hs + (hs & 1u);
                }
            } else if (tagAt(d, at, "LIST") && tagAt(d, body, "movi") && videoStream >= 0) {
                char prefix[8];
                std::snprintf(prefix, sizeof prefix, "%02d", videoStream % 100);
                moviChunks(d, body + 4, end, prefix, out, 0);
            }
            if (size > riffEnd - std::min(riffEnd, body)) break;
            at = body + size + (size & 1u);
        }
        top = riffEnd + (riffSize & 1u);
    }
    if (videoStream < 0) return fail("AVI sans flux vid\xC3\xA9o");
    if (out.fps <= 0 && usPerFrame > 0) out.fps = 1e6 / usPerFrame;
    if (out.fps <= 0) out.fps = 25;
    if (out.frames.empty()) return fail("AVI sans image (liste movi vide ou coup\xC3\xA9" "e)");
    // Un codec que rien ne dit, mais des images JPEG : du Motion-JPEG.
    if (!out.mjpeg() && out.frames.front().second >= 3 && d[out.frames.front().first] == 0xFF && d[out.frames.front().first + 1] == 0xD8
        && (out.codec.empty() || upper(out.codec) == "DIB"))
        out.codec = "MJPG";
    return true;
}

bool decodeMjpegFrame(const std::uint8_t* data, std::size_t size, Rgba& out, std::string* error) {
    if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        if (error) *error = "image JPEG illisible";
        return false;
    }
    if (hasHuffman(data, size)) return decodeJpeg(data, size, out, error);
    // Sans tables de Huffman : celles de la norme, juste apres le debut (SOI).
    const Bytes& dht = standardHuffman();
    Bytes fixed;
    fixed.reserve(size + dht.size());
    fixed.insert(fixed.end(), data, data + 2);
    fixed.insert(fixed.end(), dht.begin(), dht.end());
    fixed.insert(fixed.end(), data + 2, data + size);
    return decodeJpeg(fixed.data(), fixed.size(), out, error);
}

bool decodeAviFrame(const Bytes& d, const AviInfo& info, std::size_t index, Rgba& out, std::string* error) {
    if (info.frames.empty()) {
        if (error) *error = "aucune image";
        return false;
    }
    index = std::min(index, info.frames.size() - 1);
    // Une image vide (un AVI qui repete l'image precedente) : la precedente qui en a une.
    std::size_t k = index;
    while (k > 0 && info.frames[k].second == 0) --k;
    const auto [at, size] = info.frames[k];
    if (size == 0 || at > d.size() || d.size() - at < size) {
        if (error) *error = "image absente du fichier";
        return false;
    }
    return decodeMjpegFrame(d.data() + at, size, out, error);
}

Bytes writeMjpegAvi(int width, int height, double fps, const std::vector<Bytes>& jpegFrames) {
    std::vector<const Bytes*> frames;
    frames.reserve(jpegFrames.size());
    for (const auto& f : jpegFrames) frames.push_back(&f);
    return writeMjpegAvi(width, height, fps, frames);
}

Bytes writeMjpegAvi(int width, int height, double fps, const std::vector<const Bytes*>& jpegFramePtrs) {
    // 1.21.0 : la taille finale reservee d'avance (pas de reallocation qui double la memoire en pointe)
    static const Bytes kNone;
    std::size_t total = 512 + jpegFramePtrs.size() * 26;
    for (const Bytes* f : jpegFramePtrs) total += (f ? f->size() : 0) + 1;
    Bytes out;
    out.reserve(total);
    const auto frameAt = [&](std::size_t i) -> const Bytes& { return jpegFramePtrs[i] ? *jpegFramePtrs[i] : kNone; };
    const auto u32 = [&](std::uint32_t v) {
        for (int k = 0; k < 4; ++k) out.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    };
    const auto u16 = [&](std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto tag = [&](const char* t) { out.insert(out.end(), t, t + 4); };
    const auto patch = [&](std::size_t at, std::uint32_t v) {
        for (int k = 0; k < 4; ++k) out[at + static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(v >> (8 * k));
    };
    // La cadence en fraction : rate / scale (25 : 25 / 1 ; 29,97 : 30000 / 1001).
    std::uint32_t scale = 1, rate = static_cast<std::uint32_t>(std::lround(fps));
    if (std::fabs(fps - std::round(fps)) > 1e-6) {
        scale = 1000;
        rate = static_cast<std::uint32_t>(std::lround(fps * 1000.0));
    }
    std::uint32_t biggest = 0;
    for (std::size_t i = 0; i < jpegFramePtrs.size(); ++i) biggest = std::max(biggest, static_cast<std::uint32_t>(frameAt(i).size()));
    const auto n = static_cast<std::uint32_t>(jpegFramePtrs.size());
    tag("RIFF");
    u32(0);
    tag("AVI ");
    tag("LIST");
    const std::size_t hdrlSize = out.size();
    u32(0);
    tag("hdrl");
    tag("avih");
    u32(56);
    u32(fps > 0 ? static_cast<std::uint32_t>(std::lround(1e6 / fps)) : 40000);   // dwMicroSecPerFrame
    u32(biggest * static_cast<std::uint32_t>(std::max(1.0, fps)));               // dwMaxBytesPerSec
    u32(0);                                                                    // dwPaddingGranularity
    u32(0x10);                                                                 // dwFlags : AVIF_HASINDEX
    u32(n);                                                                    // dwTotalFrames
    u32(0);                                                                    // dwInitialFrames
    u32(1);                                                                    // dwStreams
    u32(biggest);                                                              // dwSuggestedBufferSize
    u32(static_cast<std::uint32_t>(width));
    u32(static_cast<std::uint32_t>(height));
    for (int k = 0; k < 4; ++k) u32(0);
    tag("LIST");
    const std::size_t strlSize = out.size();
    u32(0);
    tag("strl");
    tag("strh");
    u32(56);
    tag("vids");
    tag("MJPG");
    u32(0);         // dwFlags
    u16(0);         // wPriority
    u16(0);         // wLanguage
    u32(0);         // dwInitialFrames
    u32(scale);
    u32(rate);
    u32(0);         // dwStart
    u32(n);         // dwLength
    u32(biggest);   // dwSuggestedBufferSize
    u32(0xFFFFFFFFu);   // dwQuality
    u32(0);         // dwSampleSize
    u16(0);
    u16(0);
    u16(static_cast<std::uint16_t>(width));
    u16(static_cast<std::uint16_t>(height));
    tag("strf");
    u32(40);
    u32(40);        // biSize
    u32(static_cast<std::uint32_t>(width));
    u32(static_cast<std::uint32_t>(height));
    u16(1);         // biPlanes
    u16(24);        // biBitCount
    tag("MJPG");    // biCompression
    u32(static_cast<std::uint32_t>(width * height * 3));
    u32(0);
    u32(0);
    u32(0);
    u32(0);
    patch(strlSize, static_cast<std::uint32_t>(out.size() - strlSize - 4));
    patch(hdrlSize, static_cast<std::uint32_t>(out.size() - hdrlSize - 4));
    tag("LIST");
    const std::size_t moviSize = out.size();
    u32(0);
    tag("movi");
    std::vector<std::pair<std::uint32_t, std::uint32_t>> index;
    index.reserve(jpegFramePtrs.size());
    for (std::size_t i = 0; i < jpegFramePtrs.size(); ++i) {
        const Bytes& f = frameAt(i);
        index.emplace_back(static_cast<std::uint32_t>(out.size() - (moviSize + 4)), static_cast<std::uint32_t>(f.size()));
        tag("00dc");
        u32(static_cast<std::uint32_t>(f.size()));
        out.insert(out.end(), f.begin(), f.end());
        if (f.size() & 1u) out.push_back(0);
    }
    patch(moviSize, static_cast<std::uint32_t>(out.size() - moviSize - 4));
    tag("idx1");
    u32(n * 16);
    for (const auto& [offset, size] : index) {
        tag("00dc");
        u32(0x10);      // AVIIF_KEYFRAME
        u32(offset);
        u32(size);
    }
    patch(4, static_cast<std::uint32_t>(out.size() - 8));
    return out;
}

// ---- ce que la plateforme decode ----------------------------------------------------------------
std::string decodeAdvice(std::string_view format, std::string_view codec) {
    const std::string f = upper(format);
    if (f == "AVI" && mjpegCodec(codec)) return {};
    if (f.empty()) return {};
    const std::string what = f == "AVI" && !codec.empty() ? "AVI (" + std::string(codec) + ")" : f;
    // Sous Windows sans Media Foundation (une edition N) : ce qui manque, et le moyen de s'en passer.
    if (const std::string missing = mediaFoundationMissing(); !missing.empty()) return what + " : " + missing;
    if (f == "WEBM")
        return what + " : d\xC3\xA9" "cod\xC3\xA9 sous Windows seulement, avec les extensions vid\xC3\xA9o web ; ailleurs, convertir en Motion-JPEG AVI";
    return what + " : d\xC3\xA9" "cod\xC3\xA9 sous Windows seulement ; ailleurs, convertir en Motion-JPEG AVI";
}

bool decodedHere(std::string_view format, std::string_view codec) {
    if (upper(format) == "AVI" && mjpegCodec(codec)) return true;
#if defined(_WIN32)
    return !format.empty() && mediaFoundationAvailable();
#else
    return false;
#endif
}

// ---- le Motion-JPEG : une image decodee quand elle se montre --------------------------------------
namespace {
class MjpegSource final : public FrameSource {
public:
    MjpegSource(BlobPtr data, AviInfo info) : data_(std::move(data)), info_(std::move(info)) {}
    const Rgba* frameAt(double seconds, std::uint64_t& serial) override {
        if (failed_ || info_.frames.empty()) return nullptr;
        const double s = std::max(0.0, seconds);
        const auto index = std::min(info_.frames.size() - 1, static_cast<std::size_t>(std::floor(s * info_.fps + 1e-6)));
        if (!shown_ || index != index_) {
            Rgba next;
            std::string why;
            if (!decodeAviFrame(*data_, info_, index, next, &why)) {
                // Une image abimee : on garde la precedente ; aucune encore : la source echoue.
                if (!shown_) {
                    failed_ = true;
                    error_ = why;
                    return nullptr;
                }
            } else {
                frame_ = std::move(next);
                shown_ = true;
            }
            index_ = index;
        }
        serial = index_ + 1;
        return &frame_;
    }
    bool failed(std::string* error) const override {
        if (failed_ && error) *error = error_;
        return failed_;
    }
    double duration() const override { return info_.seconds(); }

private:
    BlobPtr     data_;
    AviInfo     info_;
    Rgba        frame_;
    std::size_t index_{0};
    bool        shown_{false};
    bool        failed_{false};
    std::string error_;
};
} // namespace

std::unique_ptr<FrameSource> openFrameSource(const BlobPtr& data, std::string_view format, std::string* why) {
    if (!data || data->empty()) {
        if (why) *why = "fichier vide";
        return nullptr;
    }
    if (isAvi(*data)) {
        AviInfo info;
        std::string error;
        if (parseAvi(*data, info, &error) && info.mjpeg()) return std::make_unique<MjpegSource>(data, std::move(info));
        if (!error.empty() && info.frames.empty() && !info.width) {
            if (why) *why = error;
            return nullptr;
        }
        // Un AVI d'un autre codec : Media Foundation, sous Windows.
        if (auto mf = openMediaFoundation(data, "AVI", why)) return mf;
        if (why && why->empty()) *why = decodeAdvice("AVI", info.codec);
        return nullptr;
    }
    if (auto mf = openMediaFoundation(data, format, why)) return mf;
    if (why && why->empty()) *why = decodeAdvice(format, {});
    return nullptr;
}

// ---- la lecture ---------------------------------------------------------------------------------
PlaybackSettings settingsOf(std::string_view start, std::string_view repeat) {
    PlaybackSettings s;
    const std::string st = upper(start), rp = upper(repeat);
    s.onCondition = st.find("CONDITION") != std::string::npos;
    s.loop = rp.find("UNE") == std::string::npos;   // « une fois » ; vide, « en boucle » : en boucle
    return s;
}

void openView(Playback& pb, const PlaybackSettings& s, double now) {
    pb = Playback{};
    pb.lastNow = now;
    pb.started = !s.onCondition;
}

void step(Playback& pb, const PlaybackSettings& s, double now, bool play, bool condition, double duration) {
    const double dt = pb.lastNow < 0 ? 0.0 : std::max(0.0, now - pb.lastNow);
    pb.lastNow = now;
    bool departing = false;
    if (s.onCondition) {
        // Sur condition : elle part du debut quand la condition devient vraie, revient a l'image d'attente
        // quand elle tombe.
        if (condition && !pb.conditionWas) {
            pb.started = true;
            pb.ended = false;
            pb.position = 0;
            pb.loops = 0;
            departing = true;
        } else if (!condition && pb.conditionWas) {
            pb.started = false;
            pb.ended = false;
            pb.position = 0;
            pb.loops = 0;
        }
        pb.conditionWas = condition;
    }
    // L'avance : partie, pas en pause, pas finie. Le pas qui fait partir n'avance pas encore.
    if (pb.started && !pb.ended && play && !departing) pb.position += dt;
    if (duration > 0 && pb.position >= duration) {
        if (s.loop) {
            pb.loops += static_cast<long long>(pb.position / duration);
            pb.position = std::fmod(pb.position, duration);
        } else {
            pb.position = duration;
            pb.ended = true;
        }
    }
    pb.playing = pb.started && !pb.ended && play;
}

} // namespace hmi::video
