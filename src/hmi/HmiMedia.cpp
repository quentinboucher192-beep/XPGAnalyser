#include "HmiMedia.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

// -----------------------------------------------------------------------------
//  Les bibliotheques d'un seul fichier. Leurs implementations ne sont compilees
//  qu'ici, en STATIQUE : stb_truetype l'est aussi dans FontAtlas.cpp (la police
//  de l'interface), et deux definitions exportees se heurteraient a l'edition
//  des liens. Leurs avertissements ne sont pas les notres.
// -----------------------------------------------------------------------------
#if defined(_MSC_VER)
#  pragma warning(push, 0)
#  pragma warning(disable : 4244 4245 4456 4457 4701 4702 4706 4996)
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wpragmas"
#  pragma GCC diagnostic ignored "-Wunknown-warning-option"
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wunused-variable"
#  pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wshadow"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#  pragma GCC diagnostic ignored "-Wtype-limits"
#  pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#  pragma GCC diagnostic ignored "-Wconversion"
#  pragma GCC diagnostic ignored "-Wdouble-promotion"
#  pragma GCC diagnostic ignored "-Wmisleading-indentation"
#  pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "../../third_party/stb_image.h"
#define NANOSVG_IMPLEMENTATION
#include "../../third_party/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../../third_party/nanosvgrast.h"
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "../../third_party/minimp3.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb_truetype.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

namespace hmi {

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// ---- lecture d'entiers dans un tampon, sans jamais depasser --------------
struct Reader {
    const std::uint8_t* p{nullptr};
    std::size_t         n{0};
    [[nodiscard]] bool has(std::size_t at, std::size_t len) const noexcept { return at <= n && len <= n - at; }
    [[nodiscard]] std::uint32_t be16(std::size_t at) const noexcept {
        return has(at, 2) ? (static_cast<std::uint32_t>(p[at]) << 8) | p[at + 1] : 0u;
    }
    [[nodiscard]] std::uint32_t be32(std::size_t at) const noexcept {
        return has(at, 4) ? (static_cast<std::uint32_t>(p[at]) << 24) | (static_cast<std::uint32_t>(p[at + 1]) << 16)
                                | (static_cast<std::uint32_t>(p[at + 2]) << 8) | p[at + 3]
                          : 0u;
    }
    [[nodiscard]] std::uint64_t be64(std::size_t at) const noexcept {
        return (static_cast<std::uint64_t>(be32(at)) << 32) | be32(at + 4);
    }
    [[nodiscard]] std::uint32_t le16(std::size_t at) const noexcept {
        return has(at, 2) ? static_cast<std::uint32_t>(p[at]) | (static_cast<std::uint32_t>(p[at + 1]) << 8) : 0u;
    }
    [[nodiscard]] std::uint32_t le32(std::size_t at) const noexcept {
        return has(at, 4) ? static_cast<std::uint32_t>(p[at]) | (static_cast<std::uint32_t>(p[at + 1]) << 8)
                                | (static_cast<std::uint32_t>(p[at + 2]) << 16) | (static_cast<std::uint32_t>(p[at + 3]) << 24)
                          : 0u;
    }
    [[nodiscard]] bool tag(std::size_t at, const char* t) const noexcept {
        const std::size_t len = std::strlen(t);
        return has(at, len) && std::memcmp(p + at, t, len) == 0;
    }
};

bool isPng(const Bytes& d) {
    static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return d.size() >= 8 && std::memcmp(d.data(), sig, 8) == 0;
}
bool isJpeg(const Bytes& d) { return d.size() >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF; }
bool isGifData(const Bytes& d) {
    return d.size() >= 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F' && d[3] == '8' && (d[4] == '7' || d[4] == '9') && d[5] == 'a';
}
bool isBmp(const Bytes& d) { return d.size() >= 26 && d[0] == 'B' && d[1] == 'M'; }
bool isIco(const Bytes& d) {
    return d.size() >= 6 && d[0] == 0 && d[1] == 0 && d[2] == 1 && d[3] == 0 && (d[4] | d[5]) != 0;
}
bool looksLikeSvg(const Bytes& d) {
    const std::size_t n = std::min<std::size_t>(d.size(), 4096);
    const std::string head(reinterpret_cast<const char*>(d.data()), n);
    return head.find("<svg") != std::string::npos;
}

// ---- ICO : un repertoire d'images, PNG ou DIB ---------------------------------
struct IcoEntry { int width{0}, height{0}, bpp{0}; std::uint32_t size{0}, offset{0}; };

std::vector<IcoEntry> icoEntries(const Bytes& d) {
    std::vector<IcoEntry> out;
    Reader r{d.data(), d.size()};
    const std::uint32_t count = r.le16(4);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t at = 6 + static_cast<std::size_t>(i) * 16;
        if (!r.has(at, 16)) break;
        IcoEntry e;
        e.width = d[at] == 0 ? 256 : d[at];
        e.height = d[at + 1] == 0 ? 256 : d[at + 1];
        e.bpp = static_cast<int>(r.le16(at + 6));
        e.size = r.le32(at + 8);
        e.offset = r.le32(at + 12);
        if (r.has(e.offset, e.size) && e.size > 0) out.push_back(e);
    }
    return out;
}

const IcoEntry* largestIco(const std::vector<IcoEntry>& es) {
    const IcoEntry* best = nullptr;
    for (const auto& e : es)
        if (!best || e.width * e.height > best->width * best->height
            || (e.width * e.height == best->width * best->height && e.bpp > best->bpp))
            best = &e;
    return best;
}

// Une image d'icone en DIB n'a pas d'en-tete de fichier BMP, et sa hauteur
// compte deux fois (l'image puis le masque). On lui refait un fichier BMP que
// stb_image sait lire.
Bytes icoDibAsBmp(const Bytes& d, const IcoEntry& e) {
    Reader r{d.data(), d.size()};
    const std::uint32_t headerSize = r.le32(e.offset);
    if (headerSize < 40 || !r.has(e.offset, headerSize)) return {};
    const std::uint32_t bpp = r.le16(e.offset + 14);
    std::uint32_t colors = r.le32(e.offset + 32);
    if (colors == 0 && bpp <= 8) colors = 1u << bpp;
    Bytes bmp(14 + e.size);
    const std::uint32_t fileSize = static_cast<std::uint32_t>(bmp.size());
    const std::uint32_t dataOffset = 14 + headerSize + colors * 4;
    bmp[0] = 'B';
    bmp[1] = 'M';
    for (int k = 0; k < 4; ++k) bmp[2 + k] = static_cast<std::uint8_t>(fileSize >> (8 * k));
    for (int k = 0; k < 4; ++k) bmp[10 + k] = static_cast<std::uint8_t>(dataOffset >> (8 * k));
    std::memcpy(bmp.data() + 14, d.data() + e.offset, e.size);
    // La hauteur utile est la moitie de celle du DIB.
    const std::int32_t h = static_cast<std::int32_t>(r.le32(e.offset + 8)) / 2;
    for (int k = 0; k < 4; ++k) bmp[14 + 8 + k] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(h) >> (8 * k));
    return bmp;
}

// ---- WAV -----------------------------------------------------------------
struct WavFormat {
    std::uint32_t format{0}, channels{0}, sampleRate{0}, bits{0};
    std::size_t   dataAt{0}, dataSize{0};
    bool          ok{false};
};
WavFormat readWav(const Bytes& d) {
    WavFormat w;
    Reader r{d.data(), d.size()};
    if (!r.tag(0, "RIFF") || !r.tag(8, "WAVE")) return w;
    std::size_t at = 12;
    bool fmt = false;
    while (r.has(at, 8)) {
        const std::uint32_t size = r.le32(at + 4);
        if (r.tag(at, "fmt ") && r.has(at + 8, 16)) {
            w.format = r.le16(at + 8);
            w.channels = r.le16(at + 10);
            w.sampleRate = r.le32(at + 12);
            w.bits = r.le16(at + 22);
            if (w.format == 0xFFFE && r.has(at + 8, 26)) w.format = r.le16(at + 8 + 24);   // WAVE_FORMAT_EXTENSIBLE
            fmt = true;
        } else if (r.tag(at, "data")) {
            w.dataAt = at + 8;
            w.dataSize = std::min<std::size_t>(size, d.size() - std::min(d.size(), w.dataAt));
            break;
        }
        at += 8 + size + (size & 1u);
    }
    w.ok = fmt && w.dataAt != 0 && w.channels > 0 && w.sampleRate > 0 && w.bits > 0;
    return w;
}

// ---- MP3 : minimp3, trame par trame ------------------------------------------
std::size_t skipId3(const Bytes& d) {
    if (d.size() >= 10 && d[0] == 'I' && d[1] == 'D' && d[2] == '3') {
        const std::size_t size = (static_cast<std::size_t>(d[6] & 0x7F) << 21) | (static_cast<std::size_t>(d[7] & 0x7F) << 14)
                               | (static_cast<std::size_t>(d[8] & 0x7F) << 7) | static_cast<std::size_t>(d[9] & 0x7F);
        return std::min(d.size(), 10 + size + ((d[5] & 0x10) ? 10u : 0u));
    }
    return 0;
}

struct Mp3Scan { std::size_t samples{0}; int hz{0}, channels{0}, kbps{0}, layer{0}; bool ok{false}; };
Mp3Scan scanMp3(const Bytes& d) {
    Mp3Scan s;
    mp3dec_t dec;
    mp3dec_init(&dec);
    std::size_t at = skipId3(d);
    int guard = 0;
    while (at < d.size() && guard++ < 2000000) {
        mp3dec_frame_info_t info{};
        const int samples = mp3dec_decode_frame(&dec, d.data() + at, static_cast<int>(d.size() - at), nullptr, &info);
        if (info.frame_bytes <= 0) break;
        if (samples > 0) {
            if (!s.ok) { s.hz = info.hz; s.channels = info.channels; s.kbps = info.bitrate_kbps; s.layer = info.layer; }
            s.ok = true;
            s.samples += static_cast<std::size_t>(samples);
        }
        at += static_cast<std::size_t>(info.frame_bytes);
    }
    return s;
}

// ---- MP4 : des boites imbriquees -------------------------------------------------
struct Mp4Info { double seconds{0}; int width{0}, height{0}; std::string codec; bool ok{false}; };

void mp4Walk(const Reader& r, std::size_t from, std::size_t to, Mp4Info& info, int depth, bool& inVideoTrack) {
    std::size_t at = from;
    while (at + 8 <= to && r.has(at, 8) && depth < 12) {
        std::uint64_t size = r.be32(at);
        std::size_t header = 8;
        if (size == 1) { size = r.be64(at + 8); header = 16; }
        else if (size == 0) size = to - at;
        if (size < header || at + size > to) break;
        const std::size_t body = at + header, end = at + static_cast<std::size_t>(size);
        if (r.tag(at + 4, "mvhd")) {
            const std::uint32_t version = r.has(body, 1) ? r.p[body] : 0u;
            const std::uint32_t scale = version == 1 ? r.be32(body + 20) : r.be32(body + 12);
            const std::uint64_t duration = version == 1 ? r.be64(body + 24) : r.be32(body + 16);
            if (scale > 0) info.seconds = static_cast<double>(duration) / scale;
            info.ok = true;
        } else if (r.tag(at + 4, "tkhd")) {
            const std::uint32_t version = r.has(body, 1) ? r.p[body] : 0u;
            const std::size_t dims = body + (version == 1 ? 88 : 76);
            const int w = static_cast<int>(r.be32(dims) >> 16), h = static_cast<int>(r.be32(dims + 4) >> 16);
            inVideoTrack = w > 0 && h > 0;
            if (inVideoTrack && info.width == 0) { info.width = w; info.height = h; }
        } else if (r.tag(at + 4, "stsd")) {
            // version/flags (4), nombre d'entrees (4), puis la premiere : taille (4) + format (4)
            if (inVideoTrack && info.codec.empty() && r.has(body + 12, 4))
                info.codec.assign(reinterpret_cast<const char*>(r.p + body + 12), 4);
        } else if (r.tag(at + 4, "moov") || r.tag(at + 4, "trak") || r.tag(at + 4, "mdia") || r.tag(at + 4, "minf")
                   || r.tag(at + 4, "stbl")) {
            mp4Walk(r, body, end, info, depth + 1, inVideoTrack);
        }
        at = end;
    }
}

// ---- WEBM / Matroska : EBML ----------------------------------------------------------
struct Ebml { std::uint64_t id{0}, size{0}; std::size_t body{0}; bool unknownSize{false}; bool ok{false}; };

bool ebmlVarint(const Reader& r, std::size_t at, std::uint64_t& value, std::size_t& len, bool keepMarker) {
    if (!r.has(at, 1)) return false;
    const std::uint8_t first = r.p[at];
    len = 1;
    while (len <= 8 && !(first & (0x80u >> (len - 1)))) ++len;
    if (len > 8 || !r.has(at, len)) return false;
    value = keepMarker ? first : static_cast<std::uint64_t>(first & (0xFFu >> len));
    for (std::size_t k = 1; k < len; ++k) value = (value << 8) | r.p[at + k];
    return true;
}
Ebml ebmlAt(const Reader& r, std::size_t at) {
    Ebml e;
    std::size_t idLen = 0, sizeLen = 0;
    if (!ebmlVarint(r, at, e.id, idLen, true)) return e;
    if (!ebmlVarint(r, at + idLen, e.size, sizeLen, false)) return e;
    e.unknownSize = e.size == ((1ull << (7 * sizeLen)) - 1);
    e.body = at + idLen + sizeLen;
    e.ok = true;
    return e;
}
double ebmlFloat(const Reader& r, std::size_t at, std::uint64_t size) {
    if (size == 4 && r.has(at, 4)) {
        const std::uint32_t bits = r.be32(at);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    if (size == 8 && r.has(at, 8)) {
        const std::uint64_t bits = r.be64(at);
        double v;
        std::memcpy(&v, &bits, 8);
        return v;
    }
    return 0;
}
std::uint64_t ebmlUint(const Reader& r, std::size_t at, std::uint64_t size) {
    std::uint64_t v = 0;
    for (std::uint64_t k = 0; k < size && k < 8 && r.has(at + k, 1); ++k) v = (v << 8) | r.p[at + k];
    return v;
}

struct WebmInfo { double duration{0}; std::uint64_t scale{1000000}; int width{0}, height{0}; std::string codec; bool ebml{false}; };

void webmWalk(const Reader& r, std::size_t from, std::size_t to, WebmInfo& info, int depth) {
    std::size_t at = from;
    int guard = 0;
    while (at < to && depth < 8 && guard++ < 100000) {
        const Ebml e = ebmlAt(r, at);
        if (!e.ok) break;
        const std::size_t end = e.unknownSize ? to : std::min<std::size_t>(to, e.body + static_cast<std::size_t>(e.size));
        switch (e.id) {
            case 0x18538067:   // Segment
            case 0x1549A966:   // Info
            case 0x1654AE6B:   // Tracks
            case 0xAE:         // TrackEntry
            case 0xE0:         // Video
                webmWalk(r, e.body, end, info, depth + 1);
                break;
            case 0x2AD7B1: info.scale = ebmlUint(r, e.body, e.size); break;
            case 0x4489: info.duration = ebmlFloat(r, e.body, e.size); break;
            case 0x86:   // CodecID
                if (info.codec.empty() && r.has(e.body, static_cast<std::size_t>(e.size)) && e.size < 64) {
                    std::string id(reinterpret_cast<const char*>(r.p + e.body), static_cast<std::size_t>(e.size));
                    if (id.rfind("V_", 0) == 0) info.codec = id.substr(2);
                }
                break;
            case 0xB0: if (info.width == 0) info.width = static_cast<int>(ebmlUint(r, e.body, e.size)); break;
            case 0xBA: if (info.height == 0) info.height = static_cast<int>(ebmlUint(r, e.body, e.size)); break;
            case 0x1F43B675: return;   // Cluster : les donnees commencent, l'en-tete est lu
            default: break;
        }
        if (end <= at) break;
        at = end;
    }
}

// ---- polices : la table 'name' ----------------------------------------------------------
std::string utf16beToUtf8(const std::uint8_t* p, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i + 1 < n; i += 2) {
        std::uint32_t c = (static_cast<std::uint32_t>(p[i]) << 8) | p[i + 1];
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) {
            const std::uint32_t lo = (static_cast<std::uint32_t>(p[i + 2]) << 8) | p[i + 3];
            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        }
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

bool readFont(const Bytes& d, MediaInfo& info) {
    Reader r{d.data(), d.size()};
    std::size_t base = 0;
    if (r.tag(0, "ttcf")) base = r.be32(12);                 // une collection : la premiere police
    const std::uint32_t version = r.be32(base);
    if (version != 0x00010000u && !r.tag(base, "OTTO") && !r.tag(base, "true")) return false;
    info.format = r.tag(base, "OTTO") ? "OTF" : "TTF";
    const std::uint32_t tables = r.be16(base + 4);
    std::size_t nameAt = 0, maxpAt = 0;
    for (std::uint32_t i = 0; i < tables; ++i) {
        const std::size_t rec = base + 12 + static_cast<std::size_t>(i) * 16;
        if (!r.has(rec, 16)) return false;
        if (r.tag(rec, "name")) nameAt = r.be32(rec + 8);
        if (r.tag(rec, "maxp")) maxpAt = r.be32(rec + 8);
    }
    if (maxpAt && r.has(maxpAt, 6)) info.glyphs = static_cast<int>(r.be16(maxpAt + 4));
    if (!nameAt || !r.has(nameAt, 6)) return true;
    const std::uint32_t count = r.be16(nameAt + 2), strings = nameAt + r.be16(nameAt + 4);
    int bestFamily = -1, bestStyle = -1;   // la meilleure langue trouvee : 2 anglais Windows, 1 Windows, 0 Mac
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t rec = nameAt + 6 + static_cast<std::size_t>(i) * 12;
        if (!r.has(rec, 12)) break;
        const std::uint32_t platform = r.be16(rec), language = r.be16(rec + 4), nameId = r.be16(rec + 6);
        const std::uint32_t len = r.be16(rec + 8), off = r.be16(rec + 10);
        if ((nameId != 1 && nameId != 2) || !r.has(strings + off, len)) continue;
        int score = -1;
        std::string text;
        if (platform == 3) { score = language == 0x409 ? 2 : 1; text = utf16beToUtf8(d.data() + strings + off, len); }
        else if (platform == 1) { score = 0; text.assign(reinterpret_cast<const char*>(d.data() + strings + off), len); }
        else continue;
        int& best = nameId == 1 ? bestFamily : bestStyle;
        if (score > best) { best = score; (nameId == 1 ? info.family : info.style) = text; }
    }
    return true;
}

} // namespace

// ------------------------------------------------------------------ public ----
std::string_view mediaKindLabel(MediaKind k) noexcept {
    switch (k) {
        case MediaKind::Image:   return "Image";
        case MediaKind::Sound:   return "Son";
        case MediaKind::Video:   return "Vid\xC3\xA9o";
        case MediaKind::Font:    return "Police";
        case MediaKind::Document: return "Document";      // lot API 8
        case MediaKind::Unknown: break;
    }
    return "Autre";
}

std::string formatFromExtension(std::string_view fileName) {
    const auto dot = fileName.rfind('.');
    if (dot == std::string_view::npos) return {};
    const std::string ext = upper(fileName.substr(dot + 1));
    if (ext == "JPG" || ext == "JPEG") return "JPEG";
    if (ext == "GIF") return "GIF";                                                   // lot 16
    if (ext == "PNG" || ext == "BMP" || ext == "SVG" || ext == "ICO" || ext == "WAV" || ext == "MP3" || ext == "MP4"
        || ext == "WEBM" || ext == "TTF" || ext == "OTF")
        return ext;
    if (ext == "TTC") return "TTF";
    return {};
}

MediaKind kindOfFormat(std::string_view f) noexcept {
    if (f == "PNG" || f == "JPEG" || f == "BMP" || f == "SVG" || f == "ICO" || f == "GIF") return MediaKind::Image;
    if (f == "WAV" || f == "MP3") return MediaKind::Sound;
    if (f == "MP4" || f == "WEBM") return MediaKind::Video;
    if (f == "TTF" || f == "OTF") return MediaKind::Font;
    // Lot API 8 : un autre format (l'extension d'un document) : un document.
    return f.empty() ? MediaKind::Unknown : MediaKind::Document;
}

std::string_view acceptedExtensions() noexcept {
    return "PNG, JPG, JPEG, BMP, SVG, ICO, GIF, WAV, MP3, MP4, WEBM, TTF, OTF";
}

// ---- Lot API 8 : glisser de fichiers, 2e partie ----
std::string documentFormat(std::string_view fileName) {
    const auto slash = fileName.find_last_of("/\\");
    const std::string_view leaf = slash == std::string_view::npos ? fileName : fileName.substr(slash + 1);
    const auto dot = leaf.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= leaf.size() || leaf.size() - dot - 1 > 8) return "FICHIER";
    std::string ext;
    for (const char c : leaf.substr(dot + 1)) {
        const auto u = static_cast<unsigned char>(c);
        if (!((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9'))) return "FICHIER";
        ext += static_cast<char>(std::toupper(u));
    }
    return ext;
}
// ---- fin Lot API 8 : glisser de fichiers, 2e partie ----

MediaInfo inspectMedia(const Bytes& d, std::string_view fileName) {
    MediaInfo info;
    const std::string byName = formatFromExtension(fileName);
    Reader r{d.data(), d.size()};
    if (d.empty()) {
        info.format = byName;
        info.kind = kindOfFormat(byName);
        info.error = "fichier vide";
        return info;
    }
    // Images matricielles : le contenu decide.
    if (isPng(d) || isJpeg(d) || isBmp(d)) {
        info.kind = MediaKind::Image;
        info.format = isPng(d) ? "PNG" : isJpeg(d) ? "JPEG" : "BMP";
        int w = 0, h = 0, comp = 0;
        if (stbi_info_from_memory(d.data(), static_cast<int>(d.size()), &w, &h, &comp)) {
            info.width = w;
            info.height = h;
        } else {
            info.error = std::string(stbi_failure_reason() ? stbi_failure_reason() : "illisible");
        }
        return info;
    }
    // Lot 16 : le GIF (anime ou non) - ses images, leurs durees, sa boucle.
    if (isGifData(d)) {
        info.kind = MediaKind::Image;
        info.format = "GIF";
        GifTiming g;
        std::string why;
        if (gifTiming(d, g, &why)) {
            info.width = g.width;
            info.height = g.height;
            info.images = static_cast<int>(g.frames());
            info.seconds = g.totalMs / 1000.0;
            info.loop = g.fileLoop;
        } else {
            info.error = why;
        }
        return info;
    }
    if (isIco(d) && byName != "MP3") {
        info.kind = MediaKind::Image;
        info.format = "ICO";
        const auto entries = icoEntries(d);
        info.images = static_cast<int>(entries.size());
        if (const auto* e = largestIco(entries)) { info.width = e->width; info.height = e->height; }
        else info.error = "ic\xC3\xB4ne sans image lisible";
        return info;
    }
    if (r.tag(0, "RIFF") && r.tag(8, "WAVE")) {
        info.kind = MediaKind::Sound;
        info.format = "WAV";
        const auto w = readWav(d);
        if (!w.ok) { info.error = "en-t\xC3\xAAte WAV incomplet"; return info; }
        info.channels = static_cast<int>(w.channels);
        info.sampleRate = static_cast<int>(w.sampleRate);
        info.bitsPerSample = static_cast<int>(w.bits);
        info.codec = w.format == 1 ? "PCM" : w.format == 3 ? "PCM flottant" : "compress\xC3\xA9 (" + std::to_string(w.format) + ")";
        const double bytesPerSecond = static_cast<double>(w.sampleRate) * w.channels * (w.bits / 8.0);
        if (bytesPerSecond > 0) info.seconds = static_cast<double>(w.dataSize) / bytesPerSecond;
        info.bitrateKbps = static_cast<int>(bytesPerSecond * 8 / 1000);
        return info;
    }
    if (r.tag(4, "ftyp")) {
        info.kind = MediaKind::Video;
        info.format = "MP4";
        Mp4Info m;
        bool video = false;
        mp4Walk(r, 0, d.size(), m, 0, video);
        if (!m.ok) { info.error = "bo\xC3\xAEte 'moov' absente : fichier coup\xC3\xA9 ?"; return info; }
        info.seconds = m.seconds;
        info.width = m.width;
        info.height = m.height;
        info.codec = m.codec == "avc1" || m.codec == "avc3" ? "H.264 (" + m.codec + ")"
                   : m.codec == "hvc1" || m.codec == "hev1" ? "H.265 (" + m.codec + ")"
                   : m.codec;
        return info;
    }
    if (r.be32(0) == 0x1A45DFA3u) {
        info.kind = MediaKind::Video;
        info.format = "WEBM";
        WebmInfo w;
        webmWalk(r, 0, d.size(), w, 0);
        info.width = w.width;
        info.height = w.height;
        info.codec = w.codec;
        info.seconds = w.duration * static_cast<double>(w.scale) / 1e9;
        if (w.width == 0) info.error = "piste vid\xC3\xA9o introuvable";
        return info;
    }
    {
        MediaInfo f;
        if (readFont(d, f)) {
            f.kind = MediaKind::Font;
            return f;
        }
    }
    if (looksLikeSvg(d)) {
        info.kind = MediaKind::Image;
        info.format = "SVG";
        std::string text(reinterpret_cast<const char*>(d.data()), d.size());
        if (NSVGimage* img = nsvgParse(text.data(), "px", 96.0f)) {
            info.width = static_cast<int>(std::lround(img->width));
            info.height = static_cast<int>(std::lround(img->height));
            if (info.width <= 0 || info.height <= 0) info.error = "SVG sans dimensions";
            nsvgDelete(img);
        } else {
            info.error = "SVG illisible";
        }
        return info;
    }
    if (byName == "MP3" || skipId3(d) > 0 || (d.size() > 1 && d[0] == 0xFF && (d[1] & 0xE0) == 0xE0)) {
        info.kind = MediaKind::Sound;
        info.format = "MP3";
        const auto s = scanMp3(d);
        if (!s.ok) { info.error = "aucune trame MP3"; return info; }
        info.channels = s.channels;
        info.sampleRate = s.hz;
        info.bitrateKbps = s.kbps;
        info.codec = "MPEG couche " + std::string(s.layer == 3 ? "III" : s.layer == 2 ? "II" : "I");
        if (s.hz > 0) info.seconds = static_cast<double>(s.samples) / s.hz;
        return info;
    }
    info.format = byName;
    info.kind = kindOfFormat(byName);
    info.error = byName.empty() ? "format non reconnu (accept\xC3\xA9s : " + std::string(acceptedExtensions()) + ")"
                                : "le contenu n'est pas un " + byName;
    return info;
}

bool decodeImage(const Bytes& d, std::string_view format, Rgba& out, int maxSide, std::string* error) {
    auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    auto fromStb = [&](const std::uint8_t* data, std::size_t size) {
        int w = 0, h = 0, comp = 0;
        stbi_uc* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
        if (!px) return fail(stbi_failure_reason() ? stbi_failure_reason() : "image illisible");
        out.width = w;
        out.height = h;
        out.pixels.assign(px, px + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
        stbi_image_free(px);
        return true;
    };
    if (d.empty()) return fail("fichier vide");
    if (isPng(d) || isJpeg(d) || isBmp(d) || isGifData(d)) return fromStb(d.data(), d.size());   // un GIF : sa premiere image
    if (isIco(d) || format == "ICO") {
        const auto entries = icoEntries(d);
        const auto* e = largestIco(entries);
        if (!e) return fail("ic\xC3\xB4ne sans image");
        static const std::uint8_t sig[4] = {0x89, 'P', 'N', 'G'};
        if (e->size >= 8 && std::memcmp(d.data() + e->offset, sig, 4) == 0) return fromStb(d.data() + e->offset, e->size);
        const Bytes bmp = icoDibAsBmp(d, *e);
        if (bmp.empty()) return fail("image d'ic\xC3\xB4ne illisible");
        return fromStb(bmp.data(), bmp.size());
    }
    if (format == "SVG" || looksLikeSvg(d)) {
        std::string text(reinterpret_cast<const char*>(d.data()), d.size());
        NSVGimage* img = nsvgParse(text.data(), "px", 96.0f);
        if (!img) return fail("SVG illisible");
        if (img->width <= 0 || img->height <= 0) { nsvgDelete(img); return fail("SVG sans dimensions"); }
        const float side = std::max(img->width, img->height);
        const float scale = side > static_cast<float>(maxSide) ? static_cast<float>(maxSide) / side
                          : side < 256.f ? 256.f / side : 1.f;   // un petit pictogramme est agrandi, net
        const int w = std::max(1, static_cast<int>(std::lround(img->width * scale)));
        const int h = std::max(1, static_cast<int>(std::lround(img->height * scale)));
        NSVGrasterizer* rast = nsvgCreateRasterizer();
        if (!rast) { nsvgDelete(img); return fail("rasteriseur SVG indisponible"); }
        out.width = w;
        out.height = h;
        out.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        nsvgRasterize(rast, img, 0, 0, scale, out.pixels.data(), w, h, w * 4);
        nsvgDeleteRasterizer(rast);
        nsvgDelete(img);
        return true;
    }
    return fail("ce n'est pas une image");
}

// ================================================================ lot 10 : SVG ===
bool isSvgImage(const Bytes& d, std::string_view format) {
    return format == "SVG" || (!isPng(d) && !isJpeg(d) && !isBmp(d) && !isIco(d) && looksLikeSvg(d));
}

namespace {
std::uint32_t nsvgOf(std::uint32_t rgb) {   // 0xRRGGBB -> 0x00BBGGRR (nanosvg)
    return ((rgb >> 16) & 0xFFu) | (rgb & 0xFF00u) | ((rgb & 0xFFu) << 16);
}
std::uint32_t rgbOf(unsigned int nsvg) {     // 0xAABBGGRR -> 0xRRGGBB
    return ((nsvg & 0xFFu) << 16) | (nsvg & 0xFF00u) | ((nsvg >> 16) & 0xFFu);
}
bool nearColor(std::uint32_t a, std::uint32_t b) {
    for (int shift : {0, 8, 16})
        if (std::abs(static_cast<int>((a >> shift) & 0xFFu) - static_cast<int>((b >> shift) & 0xFFu)) > 8) return false;
    return true;
}
void recolorPaint(NSVGpaint& p, bool fill, const SvgRecolor& rc) {
    if (p.type == NSVG_PAINT_NONE || p.type == NSVG_PAINT_UNDEF) return;
    if (rc.mode == "remplissages" && !fill) return;
    if (rc.mode == "contours" && fill) return;
    if (rc.mode == "une couleur" && (p.type != NSVG_PAINT_COLOR || !nearColor(rgbOf(p.color), rc.from))) return;
    std::uint32_t alpha = 0xFF000000u;
    if (p.type == NSVG_PAINT_COLOR) alpha = p.color & 0xFF000000u;
    else std::free(p.gradient);           // un degrade devient une couleur pleine
    p.type = NSVG_PAINT_COLOR;
    p.color = alpha | nsvgOf(rc.rgb);
}
void scaleGradient(NSVGpaint& p, float sx, float sy) {
    if (p.type != NSVG_PAINT_LINEAR_GRADIENT && p.type != NSVG_PAINT_RADIAL_GRADIENT) return;
    // Le degrade lit l'espace de l'image : il suit l'etirement des points.
    p.gradient->xform[0] /= sx;
    p.gradient->xform[1] /= sx;
    p.gradient->xform[2] /= sy;
    p.gradient->xform[3] /= sy;
}
} // namespace

bool rasterizeSvg(const Bytes& d, int width, int height, Rgba& out, const SvgRecolor* rc, std::string* error) {
    const auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (width <= 0 || height <= 0) return fail("taille nulle");
    width = std::min(width, 4096);
    height = std::min(height, 4096);
    std::string text(reinterpret_cast<const char*>(d.data()), d.size());
    NSVGimage* img = nsvgParse(text.data(), "px", 96.0f);
    if (!img) return fail("SVG illisible");
    if (img->width <= 0 || img->height <= 0) {
        nsvgDelete(img);
        return fail("SVG sans dimensions");
    }
    const float sx = static_cast<float>(width) / img->width, sy = static_cast<float>(height) / img->height;
    const float sw = std::sqrt(sx * sy);
    for (NSVGshape* s = img->shapes; s; s = s->next) {
        for (NSVGpath* p = s->paths; p; p = p->next) {
            for (int i = 0; i < p->npts; ++i) {
                p->pts[2 * i] *= sx;
                p->pts[2 * i + 1] *= sy;
            }
            p->bounds[0] *= sx; p->bounds[2] *= sx; p->bounds[1] *= sy; p->bounds[3] *= sy;
        }
        s->bounds[0] *= sx; s->bounds[2] *= sx; s->bounds[1] *= sy; s->bounds[3] *= sy;
        s->strokeWidth *= sw;
        s->strokeDashOffset *= sw;
        for (int k = 0; k < s->strokeDashCount; ++k) s->strokeDashArray[k] *= sw;
        scaleGradient(s->fill, sx, sy);
        scaleGradient(s->stroke, sx, sy);
        if (rc && rc->active) {
            recolorPaint(s->fill, true, *rc);
            recolorPaint(s->stroke, false, *rc);
        }
    }
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(img);
        return fail("rasteriseur SVG indisponible");
    }
    out.width = width;
    out.height = height;
    out.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    nsvgRasterize(rast, img, 0, 0, 1.0f, out.pixels.data(), width, height, width * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);
    return true;
}

bool soundEnvelope(const Bytes& d, std::string_view format, int buckets, std::vector<float>& minMax) {
    minMax.assign(static_cast<std::size_t>(std::max(0, buckets)) * 2, 0.f);
    if (buckets <= 0 || d.empty()) return false;
    auto feed = [&](std::size_t index, std::size_t total, float v) {
        const std::size_t b = std::min<std::size_t>(static_cast<std::size_t>(buckets) - 1,
                                                    total ? index * static_cast<std::size_t>(buckets) / total : 0);
        minMax[b * 2] = std::min(minMax[b * 2], v);
        minMax[b * 2 + 1] = std::max(minMax[b * 2 + 1], v);
    };
    Reader r{d.data(), d.size()};
    if (format == "WAV" || (r.tag(0, "RIFF") && r.tag(8, "WAVE"))) {
        const auto w = readWav(d);
        if (!w.ok || (w.format != 1 && w.format != 3)) return false;
        const std::size_t bytes = w.bits / 8, frame = bytes * w.channels;
        if (bytes == 0 || frame == 0) return false;
        const std::size_t frames = w.dataSize / frame;
        for (std::size_t i = 0; i < frames; ++i) {
            const std::size_t at = w.dataAt + i * frame;   // premier canal
            float v = 0;
            if (w.format == 3 && bytes == 4) { std::uint32_t b = r.le32(at); std::memcpy(&v, &b, 4); }
            else if (bytes == 1) v = (static_cast<float>(d[at]) - 128.f) / 128.f;
            else if (bytes == 2) v = static_cast<float>(static_cast<std::int16_t>(r.le16(at))) / 32768.f;
            else if (bytes == 3) {
                std::int32_t s = static_cast<std::int32_t>((r.le32(at) & 0xFFFFFFu) << 8) >> 8;
                v = static_cast<float>(s) / 8388608.f;
            } else if (bytes == 4) v = static_cast<float>(static_cast<std::int32_t>(r.le32(at))) / 2147483648.f;
            feed(i, frames, std::clamp(v, -1.f, 1.f));
        }
        return frames > 0;
    }
    if (format == "MP3") {
        const auto scan = scanMp3(d);
        if (!scan.ok || scan.samples == 0) return false;
        mp3dec_t dec;
        mp3dec_init(&dec);
        std::vector<mp3d_sample_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
        std::size_t at = skipId3(d), index = 0;
        int guard = 0;
        while (at < d.size() && guard++ < 2000000) {
            mp3dec_frame_info_t info{};
            const int samples = mp3dec_decode_frame(&dec, d.data() + at, static_cast<int>(d.size() - at), pcm.data(), &info);
            if (info.frame_bytes <= 0) break;
            for (int k = 0; k < samples; ++k)
                feed(index++, scan.samples, static_cast<float>(pcm[static_cast<std::size_t>(k * std::max(1, info.channels))]) / 32768.f);
            at += static_cast<std::size_t>(info.frame_bytes);
        }
        return index > 0;
    }
    return false;
}

bool decodePcm(const Bytes& d, std::string_view format, Pcm& out) {
    out = Pcm{};
    if (d.empty()) return false;
    Reader r{d.data(), d.size()};
    if (format == "WAV" || (r.tag(0, "RIFF") && r.tag(8, "WAVE"))) {
        const auto w = readWav(d);
        if (!w.ok || (w.format != 1 && w.format != 3) || w.channels > 8) return false;
        const std::size_t bytes = w.bits / 8, frame = bytes * w.channels;
        if (bytes == 0 || frame == 0) return false;
        const std::size_t frames = w.dataSize / frame;
        out.rate = static_cast<int>(w.sampleRate);
        out.channels = static_cast<int>(w.channels);
        out.samples.reserve(frames * w.channels);
        for (std::size_t i = 0; i < frames * w.channels; ++i) {
            const std::size_t at = w.dataAt + i * bytes;
            float v = 0;
            if (w.format == 3 && bytes == 4) { std::uint32_t b = r.le32(at); std::memcpy(&v, &b, 4); }
            else if (bytes == 1) v = (static_cast<float>(d[at]) - 128.f) / 128.f;
            else if (bytes == 2) v = static_cast<float>(static_cast<std::int16_t>(r.le16(at))) / 32768.f;
            else if (bytes == 3) v = static_cast<float>(static_cast<std::int32_t>((r.le32(at) & 0xFFFFFFu) << 8) >> 8) / 8388608.f;
            else if (bytes == 4) v = static_cast<float>(static_cast<std::int32_t>(r.le32(at))) / 2147483648.f;
            out.samples.push_back(static_cast<std::int16_t>(std::lround(std::clamp(v, -1.f, 1.f) * 32767.f)));
        }
        return !out.samples.empty();
    }
    if (format == "MP3") {
        mp3dec_t dec;
        mp3dec_init(&dec);
        std::vector<mp3d_sample_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
        std::size_t at = skipId3(d);
        int guard = 0;
        while (at < d.size() && guard++ < 2000000) {
            mp3dec_frame_info_t info{};
            const int samples = mp3dec_decode_frame(&dec, d.data() + at, static_cast<int>(d.size() - at), pcm.data(), &info);
            if (info.frame_bytes <= 0) break;
            if (samples > 0) {
                // Un flux ne change pas de format en route ; si c'est le cas, on
                // garde le premier (les trames suivantes seraient mal lues).
                if (out.rate == 0) { out.rate = info.hz; out.channels = info.channels; }
                if (info.hz == out.rate && info.channels == out.channels)
                    out.samples.insert(out.samples.end(), pcm.begin(), pcm.begin() + samples * info.channels);
            }
            at += static_cast<std::size_t>(info.frame_bytes);
        }
        return out.rate > 0 && !out.samples.empty();
    }
    return false;
}

bool renderFontText(const Bytes& font, std::string_view utf8, float pixelHeight, Rgba& out) {
    if (font.empty() || utf8.empty() || pixelHeight <= 1.f) return false;
    stbtt_fontinfo info;
    const int offset = stbtt_GetFontOffsetForIndex(font.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&info, font.data(), offset)) return false;
    const float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
    // Les points de code, puis la largeur totale.
    std::vector<int> cps;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        int cp = c, len = 1;
        if (c >= 0xF0 && i + 3 < utf8.size()) { cp = ((c & 0x07) << 18) | ((utf8[i + 1] & 0x3F) << 12) | ((utf8[i + 2] & 0x3F) << 6) | (utf8[i + 3] & 0x3F); len = 4; }
        else if (c >= 0xE0 && i + 2 < utf8.size()) { cp = ((c & 0x0F) << 12) | ((utf8[i + 1] & 0x3F) << 6) | (utf8[i + 2] & 0x3F); len = 3; }
        else if (c >= 0xC0 && i + 1 < utf8.size()) { cp = ((c & 0x1F) << 6) | (utf8[i + 1] & 0x3F); len = 2; }
        cps.push_back(cp);
        i += static_cast<std::size_t>(len);
    }
    float width = 0;
    for (std::size_t k = 0; k < cps.size(); ++k) {
        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, cps[k], &advance, &lsb);
        width += static_cast<float>(advance) * scale;
        if (k + 1 < cps.size()) width += static_cast<float>(stbtt_GetCodepointKernAdvance(&info, cps[k], cps[k + 1])) * scale;
    }
    const int w = std::max(1, static_cast<int>(std::ceil(width)) + 2);
    const int h = std::max(1, static_cast<int>(std::ceil(static_cast<float>(ascent - descent) * scale)) + 2);
    if (static_cast<long long>(w) * h > 16LL * 1024 * 1024) return false;
    std::vector<std::uint8_t> cover(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    const int baseline = static_cast<int>(std::lround(static_cast<float>(ascent) * scale)) + 1;
    float x = 1;
    for (std::size_t k = 0; k < cps.size(); ++k) {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&info, cps[k], scale, scale, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0, gh = y1 - y0;
        const int px = static_cast<int>(std::floor(x)) + x0, py = baseline + y0;
        if (gw > 0 && gh > 0 && px >= 0 && py >= 0 && px + gw <= w && py + gh <= h)
            stbtt_MakeCodepointBitmap(&info, cover.data() + static_cast<std::size_t>(py) * static_cast<std::size_t>(w) + static_cast<std::size_t>(px),
                                      gw, gh, w, scale, scale, cps[k]);
        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, cps[k], &advance, &lsb);
        x += static_cast<float>(advance) * scale;
        if (k + 1 < cps.size()) x += static_cast<float>(stbtt_GetCodepointKernAdvance(&info, cps[k], cps[k + 1])) * scale;
    }
    out.width = w;
    out.height = h;
    out.pixels.assign(cover.size() * 4, 255);
    for (std::size_t i = 0; i < cover.size(); ++i) out.pixels[i * 4 + 3] = cover[i];
    return true;
}

std::string formatBytes(std::uint64_t b) {
    char buf[64];
    if (b < 1024) std::snprintf(buf, sizeof buf, "%llu o", static_cast<unsigned long long>(b));
    else if (b < 1024ull * 1024) std::snprintf(buf, sizeof buf, "%.1f Ko", static_cast<double>(b) / 1024.0);
    else if (b < 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f Mo", static_cast<double>(b) / 1024.0 / 1024.0);
    else std::snprintf(buf, sizeof buf, "%.2f Go", static_cast<double>(b) / 1024.0 / 1024.0 / 1024.0);
    std::string s(buf);
    if (const auto dot = s.find('.'); dot != std::string::npos) s[dot] = ',';
    return s;
}

std::string formatDuration(double seconds) {
    if (seconds <= 0) return "-";
    char buf[64];
    if (seconds < 60) std::snprintf(buf, sizeof buf, "%.1f s", seconds);
    else {
        const long total = std::lround(seconds);
        std::snprintf(buf, sizeof buf, "%ld min %02ld s", total / 60, total % 60);
    }
    std::string s(buf);
    if (const auto dot = s.find('.'); dot != std::string::npos) s[dot] = ',';
    return s;
}

void applyGain(Pcm& pcm, double gain) {
    const double g = std::clamp(gain, 0.0, 1.0);
    if (g >= 1.0) return;
    for (auto& v : pcm.samples) v = static_cast<std::int16_t>(std::lround(static_cast<double>(v) * g));
}


// ============================================================ lot 16 : GIF ===
bool isGif(const Bytes& data) noexcept { return isGifData(data); }

std::size_t GifTiming::frameAt(double ms) const noexcept {
    if (delaysMs.empty()) return 0;
    double acc = 0;
    for (std::size_t i = 0; i < delaysMs.size(); ++i) {
        acc += delaysMs[i];
        if (ms < acc) return i;
    }
    return delaysMs.size() - 1;
}

bool gifTiming(const Bytes& d, GifTiming& out, std::string* error) {
    const auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    out = GifTiming{};
    if (!isGifData(d) || d.size() < 13) return fail("pas un GIF");
    const auto u16 = [&](std::size_t at) { return at + 1 < d.size() ? d[at] | (d[at + 1] << 8) : 0; };
    out.width = u16(6);
    out.height = u16(8);
    std::size_t pos = 13;
    if (d[10] & 0x80) pos += 3u * (1u << ((d[10] & 0x07) + 1));      // la palette globale
    int pendingDelay = -1;
    // Les sous-blocs : une longueur, des octets... jusqu'a 0.
    const auto skipBlocks = [&](std::size_t& at) {
        while (at < d.size()) {
            const std::size_t n = d[at];
            at += 1;
            if (n == 0) return true;
            at += n;
        }
        return false;
    };
    while (pos < d.size()) {
        const std::uint8_t tag = d[pos];
        if (tag == 0x3B) break;                                       // la fin
        if (tag == 0x21 && pos + 1 < d.size()) {                      // une extension
            const std::uint8_t label = d[pos + 1];
            std::size_t at = pos + 2;
            if (label == 0xF9 && at + 4 < d.size() && d[at] >= 4) {   // le controle graphique : la duree
                pendingDelay = u16(at + 2) * 10;
            } else if (label == 0xFF && at + 11 < d.size() && d[at] == 11
                       && std::memcmp(d.data() + at + 1, "NETSCAPE2.0", 11) == 0) {
                const std::size_t sub = at + 12;
                if (sub + 3 < d.size() && d[sub] >= 3 && d[sub + 1] == 1) {
                    // Le nombre de REPETITIONS apres le premier tour (0 : sans fin), comme les navigateurs.
                    const int repeat = u16(sub + 2);
                    out.fileLoop = repeat == 0 ? 0 : repeat + 1;
                }
            }
            if (!skipBlocks(at)) return fail("GIF coup\xC3\xA9");
            pos = at;
            continue;
        }
        if (tag == 0x2C) {                                            // une image
            if (pos + 10 > d.size()) return fail("GIF coup\xC3\xA9");
            const std::uint8_t flags = d[pos + 9];
            std::size_t at = pos + 10;
            if (flags & 0x80) at += 3u * (1u << ((flags & 0x07) + 1));  // sa palette
            at += 1;                                                  // la taille LZW
            if (!skipBlocks(at)) return fail("GIF coup\xC3\xA9");
            const int delay = pendingDelay <= 10 ? 100 : pendingDelay;  // 0 ou 10 ms : 100 ms (les navigateurs)
            out.delaysMs.push_back(delay);
            out.totalMs += delay;
            pendingDelay = -1;
            pos = at;
            continue;
        }
        return fail("GIF illisible (bloc inconnu)");
    }
    if (out.delaysMs.empty()) return fail("GIF sans image");
    return true;
}

bool decodeGif(const Bytes& d, GifFrames& out, std::string* error, int maxFrames) {
    const auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    out = GifFrames{};
    if (!gifTiming(d, out.timing, error)) return false;
    int* delays = nullptr;
    int w = 0, h = 0, z = 0, comp = 0;
    stbi_uc* px = stbi_load_gif_from_memory(d.data(), static_cast<int>(d.size()), &delays, &w, &h, &z, &comp, 4);
    if (!px) return fail(stbi_failure_reason() ? stbi_failure_reason() : "GIF illisible");
    const std::size_t frameBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
    const std::size_t budget = 256ull * 1024 * 1024;
    const int keep = std::max(1, std::min({z, maxFrames, static_cast<int>(budget / std::max<std::size_t>(frameBytes, 1))}));
    out.frames.reserve(static_cast<std::size_t>(keep));
    for (int i = 0; i < keep; ++i) {
        Rgba f;
        f.width = w;
        f.height = h;
        f.pixels.assign(px + static_cast<std::size_t>(i) * frameBytes, px + static_cast<std::size_t>(i + 1) * frameBytes);
        out.frames.push_back(std::move(f));
    }
    stbi_image_free(px);
    if (delays) STBI_FREE(delays);
    // Les durees : celles des blocs (lues sans decoder), ramenees aux images gardees.
    out.timing.width = w;
    out.timing.height = h;
    if (out.timing.delaysMs.size() > out.frames.size()) {
        out.timing.delaysMs.resize(out.frames.size());
        out.timing.totalMs = 0;
        for (const int ms : out.timing.delaysMs) out.timing.totalMs += ms;
    }
    while (out.timing.delaysMs.size() < out.frames.size()) {
        out.timing.delaysMs.push_back(100);
        out.timing.totalMs += 100;
    }
    return true;
}

// ---- lot 16 : ecrire un GIF anime ----------------------------------------------------
//  Le codage LZW le plus simple qui soit : chaque pixel est ecrit tel quel (son
//  index), et un code "clear" revient avant que la table ne grandisse - la taille
//  des codes ne change jamais. Le fichier est plus gros qu'un GIF optimise, mais
//  tout lecteur le lit (stb_image, les navigateurs) et le code tient en quelques
//  lignes : c'est ce qu'il faut aux exemples, aux essais et au didacticiel.
Bytes encodeGif(int width, int height, const std::vector<std::uint32_t>& palette, const std::vector<GifImage>& images, int loop,
                int transparent) {
    Bytes out;
    if (width <= 0 || height <= 0 || width > 0xFFFF || height > 0xFFFF || images.empty()) return out;
    // La palette : une puissance de deux, de 2 a 256 couleurs.
    int bits = 1;
    while ((1 << bits) < static_cast<int>(palette.size()) && bits < 8) ++bits;
    const int colors = 1 << bits;
    const auto u8 = [&](int v) { out.push_back(static_cast<std::uint8_t>(v & 0xFF)); };
    const auto u16 = [&](int v) {
        u8(v);
        u8(v >> 8);
    };
    for (const char c : std::string_view("GIF89a")) u8(c);
    u16(width);
    u16(height);
    u8(0x80 | ((bits - 1) << 4) | (bits - 1));      // palette globale, sa taille
    u8(0);                                          // fond : l'index 0
    u8(0);                                          // pixels carres
    for (int i = 0; i < colors; ++i) {
        const std::uint32_t rgb = i < static_cast<int>(palette.size()) ? palette[static_cast<std::size_t>(i)] : 0u;
        u8(static_cast<int>(rgb >> 16));
        u8(static_cast<int>(rgb >> 8));
        u8(static_cast<int>(rgb));
    }
    if (loop >= 0) {
        // NETSCAPE2.0 : 0 sans fin, sinon le nombre de REPETITIONS apres le premier tour.
        u8(0x21);
        u8(0xFF);
        u8(11);
        for (const char c : std::string_view("NETSCAPE2.0")) u8(c);
        u8(3);
        u8(1);
        u16(loop == 0 ? 0 : std::max(0, loop - 1));
        u8(0);
    }
    const int minCode = std::max(2, bits);
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (const auto& img : images) {
        // Le bloc de controle : la duree (en centiemes), la transparence, et
        // l'image effacee avant la suivante quand il y a de la transparence.
        u8(0x21);
        u8(0xF9);
        u8(4);
        u8((transparent >= 0 ? (2 << 2) | 1 : (1 << 2)));
        u16(std::max(1, (img.delayMs + 5) / 10));
        u8(transparent >= 0 ? transparent : 0);
        u8(0);
        // L'image, toute la surface.
        u8(0x2C);
        u16(0);
        u16(0);
        u16(width);
        u16(height);
        u8(0);
        u8(minCode);
        // Les codes, sur minCode + 1 bits, du bit faible au bit fort.
        const int clear = 1 << minCode;
        const int size = minCode + 1;
        const int perClear = clear - 2;                  // avant que la table ne change la taille des codes
        std::vector<std::uint8_t> stream;
        std::uint32_t acc = 0;
        int accBits = 0;
        const auto put = [&](int code) {
            acc |= static_cast<std::uint32_t>(code) << accBits;
            accBits += size;
            while (accBits >= 8) {
                stream.push_back(static_cast<std::uint8_t>(acc & 0xFF));
                acc >>= 8;
                accBits -= 8;
            }
        };
        int run = 0;
        put(clear);
        for (std::size_t i = 0; i < pixels; ++i) {
            if (run == perClear) {
                put(clear);
                run = 0;
            }
            const int index = i < img.indices.size() ? img.indices[i] : 0;
            put(std::min(index, colors - 1));
            ++run;
        }
        put(clear + 1);                                  // fin
        if (accBits > 0) stream.push_back(static_cast<std::uint8_t>(acc & 0xFF));
        // Par sous-blocs de 255 octets au plus.
        for (std::size_t at = 0; at < stream.size(); at += 255) {
            const std::size_t n = std::min<std::size_t>(255, stream.size() - at);
            u8(static_cast<int>(n));
            out.insert(out.end(), stream.begin() + static_cast<std::ptrdiff_t>(at), stream.begin() + static_cast<std::ptrdiff_t>(at + n));
        }
        u8(0);
    }
    u8(0x3B);
    return out;
}

} // namespace hmi
