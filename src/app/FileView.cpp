// =============================================================================
//  app/FileView.cpp - 1.21.0 : l'apercu de tous les fichiers, sans ecran.
//                     Voir FileView.hpp.
// =============================================================================
#include "FileView.hpp"

#include "FilePreview.hpp"
#include "../hmi/HmiDocView.hpp"
#include "../hmi/HmiVideo.hpp"
#include "../import/XmlReader.hpp"
#include "../ui/FileKinds.hpp"
#include "../xls/Workbook.hpp"
#include "../../third_party/miniz.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace app::fileview {

namespace {

namespace fs = std::filesystem;

constexpr const char* kEllipsis = "\xE2\x80\xA6";

fs::path pathOf(std::string_view utf8) {
    const std::u8string u8(utf8.begin(), utf8.end());
    return fs::path(u8);
}

std::string leafOf(const std::string& path) {
    std::string p = path;
    while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    const auto cut = p.find_last_of("/\\");
    return cut == std::string::npos ? p : p.substr(cut + 1);
}

std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string upper(std::string s) {
    for (auto& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

bool cancelled(const Limits& lim) { return lim.cancel && lim.cancel->load(std::memory_order_relaxed); }

std::FILE* openRead(const std::string& path) {
#if defined(_WIN32)
    return _wfopen(pathOf(path).wstring().c_str(), L"rb");
#else
    return std::fopen(pathOf(path).string().c_str(), "rb");
#endif
}

// Le debut d'un fichier (au plus `limit` octets) ; faux : illisible.
bool readHead(const std::string& path, std::size_t limit, std::string& out) {
    std::ifstream in(pathOf(path), std::ios::binary);
    if (!in) return false;
    out.assign(limit, '\0');
    in.read(out.data(), static_cast<std::streamsize>(limit));
    out.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, in.gcount())));
    return true;
}

std::int64_t modifiedOf(const fs::path& p) {
    std::error_code ec;
    const auto ft = fs::last_write_time(p, ec);
    if (ec) return 0;
    const auto sys = std::chrono::system_clock::now()
                   + std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now());
    return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count());
}

std::string sizeText(std::uint64_t n) { return hmi::explorer::sizeText(n); }

// Les milliers separes : 12 345.
std::string thousands(std::uint64_t n) {
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), " ");
    return s;
}

std::string plural(std::uint64_t n, const char* one, const char* many) { return thousands(n) + " " + (n > 1 ? many : one); }

// ---- l'UTF-8 ----------------------------------------------------------------------------
void putCode(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// La longueur d'une sequence UTF-8 valide en `i` ; 0 : invalide ; -1 : coupee par la fin.
int sequenceAt(std::string_view s, std::size_t i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) return 1;
    int len = 0;
    if (c >= 0xC2 && c < 0xE0) len = 2;
    else if (c >= 0xE0 && c < 0xF0) len = 3;
    else if (c >= 0xF0 && c < 0xF5) len = 4;
    else return 0;
    for (int k = 1; k < len; ++k) {
        if (i + static_cast<std::size_t>(k) >= s.size()) return -1;
        const auto d = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
        if ((d & 0xC0) != 0x80) return 0;
        if (k == 1 && c == 0xE0 && d < 0xA0) return 0;   // trop long
        if (k == 1 && c == 0xED && d > 0x9F) return 0;   // demi-paire UTF-16
        if (k == 1 && c == 0xF0 && d < 0x90) return 0;
        if (k == 1 && c == 0xF4 && d > 0x8F) return 0;
    }
    return len;
}

bool validUtf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const int n = sequenceAt(s, i);
        if (n == -1) return true;   // coupee par la fin du morceau lu : pas une faute
        if (n == 0) return false;
        i += static_cast<std::size_t>(n);
    }
    return true;
}

constexpr std::uint16_t kCp1252[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                       0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                       0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};

// Le nombre de caracteres (points de code) d'un texte UTF-8.
std::size_t codepoints(std::string_view s) {
    std::size_t n = 0;
    for (const char c : s) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return n;
}

// Une ligne montrable : tabulations deployees (colonnes de 4), controles en espaces, coupee a `maxChars`.
std::string cleanLine(std::string_view s, std::size_t maxChars) {
    std::string out;
    std::size_t col = 0;
    for (std::size_t i = 0; i < s.size();) {
        if (col >= maxChars) {
            out += kEllipsis;
            break;
        }
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == '\t') {
            const std::size_t to = (col / 4 + 1) * 4;
            out.append(to - col, ' ');
            col = to;
            ++i;
            continue;
        }
        if (c < 0x20 || c == 0x7F) {
            out += ' ';
            ++col;
            ++i;
            continue;
        }
        std::size_t n = 1;
        if (c >= 0x80) {
            const int k = sequenceAt(s, i);
            n = k > 0 ? static_cast<std::size_t>(k) : 1;
        }
        out.append(s.substr(i, n));
        i += n;
        ++col;
    }
    return out;
}

// Un nom d'entree de zip montrable (ses controles en '?').
std::string cleanName(std::string_view s) {
    std::string out;
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        out += c < 0x20 || c == 0x7F ? '?' : ch;
    }
    return validUtf8(out) ? out : toUtf8(out, Encoding::Windows1252);
}

// ---- un zip en memoire ou sur le disque (miniz) ----------------------------------------------
class Zip {
public:
    Zip() { std::memset(&z_, 0, sizeof z_); }
    ~Zip() {
        if (ok_) mz_zip_reader_end(&z_);
        if (file_) std::fclose(file_);
    }
    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;
    bool openMemory(std::string_view b) {
        ok_ = b.size() >= 22 && mz_zip_reader_init_mem(&z_, b.data(), b.size(), 0) != 0;
        return ok_;
    }
    bool openFile(const std::string& path) {
        file_ = openRead(path);
        if (!file_) return false;
        ok_ = mz_zip_reader_init_cfile(&z_, file_, 0, 0) != 0;
        return ok_;
    }
    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] std::size_t count() { return ok_ ? mz_zip_reader_get_num_files(&z_) : 0; }
    bool stat(std::size_t i, mz_zip_archive_file_stat& st) { return ok_ && mz_zip_reader_file_stat(&z_, static_cast<mz_uint>(i), &st); }
    [[nodiscard]] int locate(const char* name) { return ok_ ? mz_zip_reader_locate_file(&z_, name, nullptr, 0) : -1; }
    // Le debut d'une entree (au plus `max` octets, decompresses au fil : une bombe ne passe pas). `whole` : tout lu.
    bool readPrefix(const char* name, std::size_t max, std::string& out, bool* whole = nullptr, const Limits* lim = nullptr) {
        out.clear();
        if (whole) *whole = false;
        const int at = locate(name);
        if (at < 0) return false;
        mz_zip_archive_file_stat st;
        if (!stat(static_cast<std::size_t>(at), st) || st.m_is_directory) return false;
        mz_zip_reader_extract_iter_state* it = mz_zip_reader_extract_iter_new(&z_, static_cast<mz_uint>(at), 0);
        if (!it) return false;
        std::vector<char> buf(64 * 1024);
        bool ended = false;
        while (out.size() < max) {
            const std::size_t want = std::min(buf.size(), max - out.size());
            const std::size_t got = mz_zip_reader_extract_iter_read(it, buf.data(), want);
            if (got == 0) {
                ended = true;
                break;
            }
            out.append(buf.data(), got);
            if (lim && cancelled(*lim)) break;
        }
        (void)mz_zip_reader_extract_iter_free(it);
        if (whole) *whole = ended || out.size() >= st.m_uncomp_size;
        return true;
    }

private:
    mz_zip_archive z_{};
    std::FILE*     file_{nullptr};
    bool           ok_{false};
};

std::string_view localName(std::string_view n) {
    const auto colon = n.rfind(':');
    return colon == std::string_view::npos ? n : n.substr(colon + 1);
}

// Un attribut par son nom local ("val" pour w:val), decode.
std::string attrLocal(const importer::XmlReader& r, std::string_view local) {
    for (const auto& a : r.attributes())
        if (localName(a.name) == local) return importer::XmlReader::decodeEntities(a.rawValue);
    return {};
}

// Le texte d'un element lu tel quel (readElementRaw) : entre sa balise ouvrante et sa fermante, decode.
std::string elementText(std::string_view raw) {
    const auto open = raw.find('>');
    const auto close = raw.rfind('<');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open) return {};
    return importer::XmlReader::decodeEntities(raw.substr(open + 1, close - open - 1));
}

// ---- une image ramenee a un cote (la moyenne des pixels couverts) ----------------------------
hmi::Rgba shrink(const hmi::Rgba& src, int maxSide) {
    const int big = std::max(src.width, src.height);
    if (maxSide <= 0 || big <= maxSide) return src;
    hmi::Rgba img;
    const int w = std::max(1, static_cast<int>(static_cast<long long>(src.width) * maxSide / big));
    const int h = std::max(1, static_cast<int>(static_cast<long long>(src.height) * maxSide / big));
    img.width = w;
    img.height = h;
    img.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 0);
    const auto W = static_cast<std::size_t>(src.width);
    for (int y = 0; y < h; ++y) {
        const int y0 = static_cast<int>(static_cast<long long>(y) * src.height / h);
        const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<long long>(y + 1) * src.height / h));
        for (int x = 0; x < w; ++x) {
            const int x0 = static_cast<int>(static_cast<long long>(x) * src.width / w);
            const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<long long>(x + 1) * src.width / w));
            std::uint64_t acc[4] = {0, 0, 0, 0};
            for (int yy = y0; yy < y1; ++yy) {
                const std::uint8_t* row = src.pixels.data() + static_cast<std::size_t>(yy) * W * 4u;
                for (int xx = x0; xx < x1; ++xx)
                    for (std::size_t c = 0; c < 4; ++c) acc[c] += row[static_cast<std::size_t>(xx) * 4u + c];
            }
            const auto n = static_cast<std::uint64_t>(y1 - y0) * static_cast<std::uint64_t>(x1 - x0);
            std::uint8_t* out = img.pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u;
            for (std::size_t c = 0; c < 4; ++c) out[c] = static_cast<std::uint8_t>(acc[c] / n);
        }
    }
    return img;
}

std::string dims(int w, int h) { return std::to_string(w) + " \xC3\x97 " + std::to_string(h); }

bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }

// Le debut d'un texte apres sa marque d'ordre et ses blancs.
std::string_view trimmedHead(std::string_view s) {
    if (startsWith(s, "\xEF\xBB\xBF")) s.remove_prefix(3);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    return s;
}

// Le libelle du type : celui de l'explorateur de l'appli quand il en sait plus que "Fichier XYZ".
std::string labelFor(std::string_view name, Kind kind, const std::string& format) {
    const std::string ext = extensionOf(name);
    const std::string known = ui::files::typeLabel(name, false);
    if (!ext.empty() && known.rfind("Fichier", 0) != 0) return known;
    switch (kind) {
        case Kind::Folder: return "Dossier";
        case Kind::Image: return "Image " + (format.empty() ? upper(ext) : format);
        case Kind::Video: return "Vid\xC3\xA9o " + (format.empty() ? upper(ext) : format);
        case Kind::Sound: return "Son " + (format.empty() ? upper(ext) : format);
        case Kind::Pdf: return "Document PDF";
        case Kind::Text: return ext.empty() ? std::string("Texte") : "Texte (" + upper(ext) + ")";
        case Kind::Csv: return ext == "tsv" ? std::string("Tableau TSV") : std::string("Tableau CSV");
        case Kind::Workbook: return "Classeur Excel";
        case Kind::Json: return "Donn\xC3\xA9" "es JSON";
        case Kind::Xml: return "Document XML";
        case Kind::Docx: return "Document Word";
        case Kind::Zip: return "Archive zip";
        case Kind::Font: return "Police " + (format.empty() ? upper(ext) : format);
        case Kind::ControlExpert: return known;
        case Kind::Binary: return ext.empty() ? std::string("Fichier binaire") : "Fichier " + upper(ext);
        case Kind::Missing: break;
    }
    return known;
}

} // namespace

// =============================================================== reconnaitre ==
std::string_view kindLabel(Kind k) noexcept {
    switch (k) {
        case Kind::Missing: return "Introuvable";
        case Kind::Folder: return "Dossier";
        case Kind::Image: return "Image";
        case Kind::Video: return "Vid\xC3\xA9o";
        case Kind::Sound: return "Son";
        case Kind::Pdf: return "PDF";
        case Kind::Text: return "Texte";
        case Kind::Csv: return "Tableau";
        case Kind::Workbook: return "Classeur";
        case Kind::Json: return "JSON";
        case Kind::Xml: return "XML";
        case Kind::Docx: return "Document Word";
        case Kind::Zip: return "Archive zip";
        case Kind::Font: return "Police";
        case Kind::ControlExpert: return "Control Expert";
        case Kind::Binary: return "Binaire";
    }
    return "Binaire";
}

std::string extensionOf(std::string_view name) {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos) name.remove_prefix(slash + 1);
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 >= name.size()) {
        // ".gitignore" : son extension est son nom (du texte)
        if (dot == 0 && name.size() > 1) return lower(std::string(name.substr(1)));
        return {};
    }
    return lower(std::string(name.substr(dot + 1)));
}

Kind kindOfName(std::string_view name) {
    const std::string e = extensionOf(name);
    static const std::map<std::string, Kind, std::less<>> table = [] {
        std::map<std::string, Kind, std::less<>> t;
        for (const char* x : {"png", "jpg", "jpeg", "jpe", "jfif", "bmp", "dib", "gif", "ico", "cur", "svg", "webp", "tif", "tiff"})
            t[x] = Kind::Image;
        for (const char* x : {"mp4", "m4v", "mov", "avi", "wmv", "webm", "mkv", "mpg", "mpeg"}) t[x] = Kind::Video;
        for (const char* x : {"wav", "mp3", "ogg", "oga", "flac", "m4a", "aac", "wma"}) t[x] = Kind::Sound;
        t["pdf"] = Kind::Pdf;
        for (const char* x : {"csv", "tsv"}) t[x] = Kind::Csv;
        for (const char* x : {"xlsx", "xlsm", "xltx", "xltm"}) t[x] = Kind::Workbook;
        for (const char* x : {"json", "geojson", "jsonl", "xpgproj", "xpgtheme", "xpgvues", "xpglayout"}) t[x] = Kind::Json;
        for (const char* x : {"xml", "xsd", "xsl", "xslt", "xef", "xaml", "plist", "resx", "csproj", "vcxproj", "filters", "manifest"})
            t[x] = Kind::Xml;
        for (const char* x : {"docx", "docm", "dotx", "dotm"}) t[x] = Kind::Docx;
        for (const char* x : {"zip", "xihm", "xapi", "jar", "apk", "nupkg", "zef"}) t[x] = Kind::Zip;
        for (const char* x : {"ttf", "otf", "ttc"}) t[x] = Kind::Font;
        for (const char* x : {"xpg", "xhw"}) t[x] = Kind::ControlExpert;
        for (const char* x : {"txt", "log", "st", "il", "ld", "fbd", "sfc", "scl", "awl", "stl", "c", "cc", "cpp", "cxx", "h", "hh",
                              "hpp", "hxx", "inl", "py", "pyw", "js", "mjs", "cjs", "ts", "tsx", "jsx", "java", "cs", "go", "rs",
                              "rb", "php", "pl", "lua", "sh", "bash", "zsh", "ps1", "psm1", "bat", "cmd", "vbs", "bas", "vb", "md",
                              "markdown", "rst", "ini", "cfg", "conf", "config", "toml", "yaml", "yml", "properties", "env",
                              "gitignore", "gitattributes", "editorconfig", "sql", "css", "scss", "less", "html", "htm", "cmake",
                              "mk", "make", "makefile", "tex", "lst", "inc", "asm", "s", "diff", "patch", "srt", "nfo", "rtf",
                              "readme", "license", "lic", "out", "err", "trace", "dat", "reg", "inf", "kt", "swift", "m", "r",
                              "jl", "dart", "vue", "svelte", "gradle", "pro", "pri", "qml", "glsl", "hlsl", "ino", "pde", "nsi",
                              "iss", "lsp", "scm", "clj", "ex", "exs", "erl", "hrl", "f", "for", "f90", "pas", "dpr", "ada",
                              "adb", "ads", "v", "sv", "vhd", "vhdl", "tcl", "awk", "sed", "bak", "tmp", "text"})
            t[x] = Kind::Text;
        return t;
    }();
    if (e.empty()) {
        // Sans extension : LISEZMOI, Makefile, README - la signature dira.
        return Kind::Binary;
    }
    const auto it = table.find(e);
    return it == table.end() ? Kind::Binary : it->second;
}

Signature signatureOf(const std::uint8_t* d, std::size_t n) {
    const auto has = [&](std::size_t at, std::string_view sig) {
        return n >= at + sig.size() && std::memcmp(d + at, sig.data(), sig.size()) == 0;
    };
    Signature s;
    const auto set = [&](const char* format, const char* label, Kind kind) {
        s.format = format;
        s.label = label;
        s.kind = kind;
        return s;
    };
    if (!d || n == 0) return s;
    if (has(0, std::string_view("\x89PNG\r\n\x1A\n", 8))) return set("PNG", "Image PNG", Kind::Image);
    if (has(0, "\xFF\xD8\xFF")) return set("JPEG", "Image JPEG", Kind::Image);
    if (has(0, "GIF87a") || has(0, "GIF89a")) return set("GIF", "Image GIF", Kind::Image);
    if (has(0, "BM") && n >= 18) {
        const std::uint32_t hdr = static_cast<std::uint32_t>(d[14]) | (static_cast<std::uint32_t>(d[15]) << 8)
                                | (static_cast<std::uint32_t>(d[16]) << 16) | (static_cast<std::uint32_t>(d[17]) << 24);
        if (hdr == 12 || hdr == 40 || hdr == 52 || hdr == 56 || hdr == 64 || hdr == 108 || hdr == 124) return set("BMP", "Image BMP", Kind::Image);
    }
    if (n >= 6 && d[0] == 0 && d[1] == 0 && (d[2] == 1 || d[2] == 2) && d[3] == 0 && (d[4] | (d[5] << 8)) > 0 && (d[4] | (d[5] << 8)) <= 256)
        return d[2] == 1 ? set("ICO", "Ic\xC3\xB4ne", Kind::Image) : set("ICO", "Curseur", Kind::Image);
    if (has(0, "RIFF") && has(8, "WEBP")) return set("WEBP", "Image WebP", Kind::Image);
    if (has(0, std::string_view("II*\0", 4)) || has(0, std::string_view("MM\0*", 4))) return set("TIFF", "Image TIFF", Kind::Image);
    if (has(0, "RIFF") && has(8, "WAVE")) return set("WAV", "Son WAV", Kind::Sound);
    if (has(0, "RIFF") && has(8, "AVI ")) return set("AVI", "Vid\xC3\xA9o AVI", Kind::Video);
    if (has(4, "ftyp")) {
        if (has(8, "qt  ")) return set("MOV", "Vid\xC3\xA9o QuickTime (MOV)", Kind::Video);
        if (has(8, "M4A ")) return set("M4A", "Son AAC (M4A)", Kind::Sound);
        return set("MP4", "Vid\xC3\xA9o MP4", Kind::Video);
    }
    if (has(0, "\x1A\x45\xDF\xA3")) return set("WEBM", "Vid\xC3\xA9o WebM / Matroska", Kind::Video);
    if (has(0, "\x30\x26\xB2\x75\x8E\x66\xCF\x11")) return set("WMV", "Vid\xC3\xA9o Windows Media (ASF)", Kind::Video);
    if (has(0, "ID3") || (n >= 2 && d[0] == 0xFF && (d[1] & 0xE6) == 0xE2)) return set("MP3", "Son MP3", Kind::Sound);
    if (has(0, "OggS")) return set("OGG", "Son Ogg", Kind::Sound);
    if (has(0, "fLaC")) return set("FLAC", "Son FLAC", Kind::Sound);
    // Une police : la version, puis un nombre de tables plausible (un texte qui commence par "true" n'en est pas une).
    if ((has(0, std::string_view("\x00\x01\x00\x00", 4)) || has(0, "true")) && n >= 12 && d[4] == 0 && d[5] > 0 && d[5] < 64)
        return set("TTF", "Police TrueType", Kind::Font);
    if (has(0, "OTTO")) return set("OTF", "Police OpenType", Kind::Font);
    if (has(0, "ttcf")) return set("TTF", "Collection de polices TrueType", Kind::Font);
    if (has(0, "wOFF") || has(0, "wOF2")) return set("WOFF", "Police web (WOFF)", Kind::Binary);
    if (has(0, "PK\x03\x04") || has(0, "PK\x05\x06")) {
        // Un paquet Office : ses noms d'entrees sont en clair au debut du zip.
        const std::string_view head(reinterpret_cast<const char*>(d), std::min<std::size_t>(n, 64 * 1024));
        if (head.find("word/") != std::string_view::npos) return set("DOCX", "Document Word", Kind::Docx);
        if (head.find("xl/") != std::string_view::npos) return set("XLSX", "Classeur Excel", Kind::Workbook);
        return set("ZIP", "Archive zip", Kind::Zip);
    }
    if (has(0, "\x37\x7A\xBC\xAF\x27\x1C")) return set("7Z", "Archive 7-Zip", Kind::Binary);
    if (has(0, "Rar!\x1A\x07")) return set("RAR", "Archive RAR", Kind::Binary);
    if (has(0, "\x1F\x8B")) return set("GZ", "Archive gzip", Kind::Binary);
    if (has(0, "BZh")) return set("BZ2", "Archive bzip2", Kind::Binary);
    if (has(0, "\xFD" "7zXZ")) return set("XZ", "Archive xz", Kind::Binary);
    if (has(0, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1")) return set("OLE", "Document Office 97-2003 (OLE)", Kind::Binary);
    if (has(0, std::string_view("SQLite format 3\0", 16))) return set("SQLITE", "Base SQLite", Kind::Binary);
    if (has(0, "MZ") && n >= 64) {
        // Un programme : l'en-tete PE la ou l'en-tete DOS le dit, ou au moins des octets nuls (un texte "MZ..." n'en est pas un).
        const std::uint32_t pe = static_cast<std::uint32_t>(d[0x3C]) | (static_cast<std::uint32_t>(d[0x3D]) << 8)
                               | (static_cast<std::uint32_t>(d[0x3E]) << 16) | (static_cast<std::uint32_t>(d[0x3F]) << 24);
        if (pe + 4 <= n && std::memcmp(d + pe, "PE\0\0", 4) == 0) return set("EXE", "Programme Windows (EXE ou DLL)", Kind::Binary);
        if (std::memchr(d, 0, 64) != nullptr) return set("EXE", "Programme DOS ou Windows (MZ)", Kind::Binary);
    }
    if (has(0, "\x7F" "ELF")) return set("ELF", "Programme Linux (ELF)", Kind::Binary);
    if (has(0, "\xCF\xFA\xED\xFE") || has(0, "\xCE\xFA\xED\xFE") || has(0, "\xFE\xED\xFA\xCF") || has(0, "\xFE\xED\xFA\xCE"))
        return set("MACHO", "Programme macOS (Mach-O)", Kind::Binary);
    if (has(0, "\xCA\xFE\xBA\xBE")) return set("CLASS", "Classe Java (ou programme macOS universel)", Kind::Binary);
    if (has(0, "MSCF")) return set("CAB", "Archive CAB", Kind::Binary);
    if (has(0, "%!PS")) return set("PS", "Document PostScript", Kind::Text);
    if (has(0, "{\\rtf")) return set("RTF", "Document RTF", Kind::Text);
    {
        const std::string_view head(reinterpret_cast<const char*>(d), std::min<std::size_t>(n, 1024));
        if (head.find("%PDF-") != std::string_view::npos) return set("PDF", "Document PDF", Kind::Pdf);
        const std::string_view t = trimmedHead(std::string_view(reinterpret_cast<const char*>(d), std::min<std::size_t>(n, 4096)));
        if (startsWith(t, "<svg") || (startsWith(t, "<?xml") && t.find("<svg") != std::string_view::npos)) return set("SVG", "Image SVG", Kind::Image);
        if (startsWith(t, "<?xml")) return set("XML", "Document XML", Kind::Xml);
    }
    return s;
}

Recognition recognize(std::string_view name, const std::uint8_t* head, std::size_t size) {
    Recognition r;
    const Kind byName = kindOfName(name);
    const Signature sig = signatureOf(head, size);
    const std::string ext = extensionOf(name);
    const std::string_view bytes(reinterpret_cast<const char*>(head), head ? size : 0);
    // Les genres qu'une signature confirme (un en-tete binaire) ; les autres sont du texte.
    const auto binaryKind = [](Kind k) {
        return k == Kind::Image || k == Kind::Video || k == Kind::Sound || k == Kind::Pdf || k == Kind::Workbook || k == Kind::Docx
            || k == Kind::Zip || k == Kind::Font;
    };
    r.kind = byName;
    r.format = upper(ext);
    if (size == 0) {
        r.label = labelFor(name, byName, r.format);
        return r;
    }
    if (sig.format.empty()) {
        if (byName == Kind::Image && ext == "svg") {
            r.kind = Kind::Image;
            r.format = "SVG";
        } else if (binaryKind(byName)) {
            // Un .png sans en-tete PNG : du texte, ou un binaire inconnu.
            r.mismatch = true;
            r.kind = looksBinary(bytes) ? Kind::Binary : Kind::Text;
            r.note = "ce n'est pas un vrai fichier " + upper(ext) + " (son en-t\xC3\xAAte manque) : " +
                     (r.kind == Kind::Text ? std::string("montr\xC3\xA9 comme du texte") : std::string("montr\xC3\xA9 en hexad\xC3\xA9" "cimal"));
        } else if (byName == Kind::Binary) {
            r.kind = looksBinary(bytes) ? Kind::Binary : Kind::Text;
        } else if ((byName == Kind::Text || byName == Kind::Json || byName == Kind::Xml || byName == Kind::Csv || byName == Kind::ControlExpert)
                   && looksBinary(bytes)) {
            // Un .log, un .dat qui n'est pas du texte : ses octets en hexadecimal.
            r.mismatch = true;
            r.kind = Kind::Binary;
            r.note = "le contenu n'est pas du texte : montr\xC3\xA9 en hexad\xC3\xA9" "cimal";
        }
        r.label = labelFor(name, r.kind, r.format);
        if (r.mismatch) r.label = r.kind == Kind::Text ? "Texte" : "Fichier binaire";
        return r;
    }
    // Une signature connue.
    r.format = sig.format;
    const bool zipFamily = byName == Kind::Zip || byName == Kind::Docx || byName == Kind::Workbook;
    if (sig.kind == byName) {
        r.kind = byName;
        // Une image d'un autre format que son extension (un PNG nomme .jpg) : elle se lit, et c'est dit.
        const std::string said = hmi::formatFromExtension(name);
        if (byName == Kind::Image && !said.empty() && said != sig.format) {
            r.mismatch = true;
            r.note = "l'extension ." + ext + " ne dit pas ce que sont les octets : en fait, " + lower(sig.label.substr(0, 1)) + sig.label.substr(1);
        }
    } else if (zipFamily && (sig.kind == Kind::Zip || sig.kind == Kind::Docx || sig.kind == Kind::Workbook)) {
        r.kind = byName;                    // un .xihm, un .xlsm : un zip, lu selon son extension
        r.format = upper(ext);
    } else if (sig.kind == Kind::Xml && (byName == Kind::Xml || byName == Kind::ControlExpert || byName == Kind::Text || byName == Kind::Json)) {
        r.kind = byName;                    // un .XPG est du XML
        r.format = upper(ext);
    } else if (sig.kind == Kind::Image && sig.format == "SVG" && (byName == Kind::Xml || byName == Kind::Text)) {
        r.kind = byName;                    // un .xml qui est un SVG : son texte
        r.format = upper(ext);
    } else if (sig.kind == Kind::Text && (byName == Kind::Text || byName == Kind::Binary)) {
        r.kind = Kind::Text;                // RTF, PostScript : du texte
    } else if (byName == Kind::Binary && ext.empty()) {
        r.kind = sig.kind;                  // sans extension : la signature dit tout
    } else if (byName == Kind::Binary) {
        r.kind = sig.kind;                  // .exe, .dll, .bin : la signature dit ce qu'ils sont
    } else {
        r.kind = sig.kind;
        r.mismatch = true;
        r.note = "l'extension ." + ext + " ne dit pas ce que sont les octets : en fait, " + lower(sig.label.substr(0, 1)) + sig.label.substr(1);
    }
    // Le libelle de l'extension quand il dit plus que "Fichier XYZ" ; sinon celui de la signature.
    const std::string base = labelFor(name, r.kind, r.format);
    r.label = r.mismatch || base.rfind("Fichier", 0) == 0 ? sig.label : base;
    if (r.label.empty()) r.label = base;
    return r;
}

// ================================================================== le texte ==
std::string_view encodingLabel(Encoding e) noexcept {
    switch (e) {
        case Encoding::Utf8: return "UTF-8";
        case Encoding::Utf8Bom: return "UTF-8 (avec BOM)";
        case Encoding::Utf16Le: return "UTF-16 LE";
        case Encoding::Utf16Be: return "UTF-16 BE";
        case Encoding::Windows1252: return "Windows-1252 (Europe occidentale)";
    }
    return "UTF-8";
}

Encoding detectEncoding(std::string_view b) {
    if (startsWith(b, "\xEF\xBB\xBF")) return Encoding::Utf8Bom;
    if (startsWith(b, "\xFF\xFE")) return Encoding::Utf16Le;
    if (startsWith(b, "\xFE\xFF")) return Encoding::Utf16Be;
    // L'UTF-16 sans marque : un octet sur deux est nul (du texte latin).
    const std::size_t n = std::min<std::size_t>(b.size(), 4096) & ~static_cast<std::size_t>(1);
    if (n >= 8) {
        std::size_t evenZero = 0, oddZero = 0;
        for (std::size_t i = 0; i < n; i += 2) {
            evenZero += b[i] == '\0';
            oddZero += b[i + 1] == '\0';
        }
        const std::size_t pairs = n / 2;
        if (oddZero * 10 > pairs * 4 && evenZero * 20 < pairs) return Encoding::Utf16Le;
        if (evenZero * 10 > pairs * 4 && oddZero * 20 < pairs) return Encoding::Utf16Be;
    }
    return validUtf8(b) ? Encoding::Utf8 : Encoding::Windows1252;
}

bool looksBinary(std::string_view b) {
    const Encoding e = detectEncoding(b);
    if (e == Encoding::Utf16Le || e == Encoding::Utf16Be) return false;
    const std::string_view s = b.substr(0, std::min<std::size_t>(b.size(), 8192));
    if (s.empty()) return false;
    std::size_t control = 0;
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == 0) return true;
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\v' && c != 0x1B) ++control;
    }
    return control * 20 > s.size();
}

std::string toUtf8(std::string_view b, Encoding e) {
    std::string out;
    switch (e) {
        case Encoding::Utf8:
        case Encoding::Utf8Bom: {
            if (startsWith(b, "\xEF\xBB\xBF")) b.remove_prefix(3);
            out.reserve(b.size());
            for (std::size_t i = 0; i < b.size();) {
                const int n = sequenceAt(b, i);
                if (n == -1) break;   // coupee par la fin du morceau lu
                if (n == 0) {
                    out += "\xEF\xBF\xBD";
                    ++i;
                    continue;
                }
                out.append(b.substr(i, static_cast<std::size_t>(n)));
                i += static_cast<std::size_t>(n);
            }
            return out;
        }
        case Encoding::Utf16Le:
        case Encoding::Utf16Be: {
            const bool le = e == Encoding::Utf16Le;
            std::size_t i = 0;
            if (b.size() >= 2) {
                const auto a = static_cast<unsigned char>(b[0]), c = static_cast<unsigned char>(b[1]);
                if ((le && a == 0xFF && c == 0xFE) || (!le && a == 0xFE && c == 0xFF)) i = 2;
            }
            out.reserve(b.size());
            const auto unit = [&](std::size_t k) -> std::uint32_t {
                const auto x = static_cast<unsigned char>(b[k]), y = static_cast<unsigned char>(b[k + 1]);
                return le ? (static_cast<std::uint32_t>(y) << 8) | x : (static_cast<std::uint32_t>(x) << 8) | y;
            };
            while (i + 1 < b.size()) {
                std::uint32_t u = unit(i);
                i += 2;
                if (u >= 0xD800 && u <= 0xDBFF) {
                    if (i + 1 >= b.size()) break;   // coupee par la fin
                    const std::uint32_t v = unit(i);
                    if (v >= 0xDC00 && v <= 0xDFFF) {
                        u = 0x10000 + ((u - 0xD800) << 10) + (v - 0xDC00);
                        i += 2;
                    } else {
                        u = 0xFFFD;
                    }
                } else if (u >= 0xDC00 && u <= 0xDFFF) {
                    u = 0xFFFD;
                }
                putCode(out, u);
            }
            return out;
        }
        case Encoding::Windows1252: {
            out.reserve(b.size() + b.size() / 8);
            for (const char ch : b) {
                const auto c = static_cast<unsigned char>(ch);
                if (c < 0x80) out += ch;
                else if (c < 0xA0) putCode(out, kCp1252[c - 0x80]);
                else putCode(out, c);
            }
            return out;
        }
    }
    return std::string(b);
}

TextView readText(std::string_view bytes, const Limits& lim, bool complete) {
    TextView tv;
    tv.encoding = detectEncoding(bytes);
    const std::string text = toUtf8(bytes, tv.encoding);
    std::size_t crlf = 0, lf = 0, cr = 0;
    std::string_view v(text);
    std::size_t start = 0;
    for (std::size_t i = 0; i <= v.size(); ++i) {
        const bool end = i == v.size();
        if (!end && v[i] != '\n' && v[i] != '\r') continue;
        if (tv.lines.size() >= lim.textLines) {
            tv.truncated = true;
            break;
        }
        if (end && start == v.size() && !tv.lines.empty()) break;   // le fichier finit par une fin de ligne
        tv.lines.push_back(cleanLine(v.substr(start, i - start), lim.lineChars));
        if (end) break;
        if (v[i] == '\r' && i + 1 < v.size() && v[i + 1] == '\n') {
            ++crlf;
            ++i;
        } else if (v[i] == '\r') {
            ++cr;
        } else {
            ++lf;
        }
        start = i + 1;
        if (lim.cancel && (tv.lines.size() & 1023) == 0 && cancelled(lim)) break;
    }
    if (!complete) tv.truncated = true;
    if (crlf + lf + cr == 0) tv.endings.clear();
    else if (lf == 0 && cr == 0) tv.endings = "CRLF (Windows)";
    else if (crlf == 0 && cr == 0) tv.endings = "LF (Unix)";
    else if (crlf == 0 && lf == 0) tv.endings = "CR (ancien Mac)";
    else tv.endings = "m\xC3\xA9lang\xC3\xA9" "es";
    return tv;
}

// ================================================================= les grilles ==
char guessSeparator(std::string_view text) {
    const char candidates[] = {';', ',', '\t', '|'};
    std::vector<std::vector<std::size_t>> counts(4);
    std::size_t lines = 0;
    bool quoted = false;
    std::size_t current[4] = {0, 0, 0, 0};
    for (std::size_t i = 0; i < text.size() && lines < 20; ++i) {
        const char c = text[i];
        if (c == '"') quoted = !quoted;
        if (quoted) continue;
        if (c == '\n') {
            for (int k = 0; k < 4; ++k) {
                counts[static_cast<std::size_t>(k)].push_back(current[k]);
                current[k] = 0;
            }
            ++lines;
            continue;
        }
        for (int k = 0; k < 4; ++k)
            if (c == candidates[k]) ++current[k];
    }
    if (lines < 20)
        for (int k = 0; k < 4; ++k)
            if (current[k] > 0) counts[static_cast<std::size_t>(k)].push_back(current[k]);
    char best = ';';
    std::size_t bestScore = 0;
    for (int k = 0; k < 4; ++k) {
        const auto& v = counts[static_cast<std::size_t>(k)];
        if (v.empty()) continue;
        std::map<std::size_t, std::size_t> freq;
        for (const auto n : v)
            if (n > 0) ++freq[n];
        std::size_t mode = 0, modeCount = 0;
        for (const auto& [n, f] : freq)
            if (f > modeCount) {
                mode = n;
                modeCount = f;
            }
        const std::size_t score = modeCount * 1000 + std::min<std::size_t>(mode, 999);
        if (score > bestScore) {
            bestScore = score;
            best = candidates[k];
        }
    }
    return best;
}

Sheet parseCsv(std::string_view s, char sep, const Limits& lim) {
    Sheet sh;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false, any = false;
    const auto endField = [&] {
        if (row.size() < lim.gridCols) row.push_back(cleanLine(field, 400));
        field.clear();
    };
    const auto endRow = [&] {
        endField();
        ++sh.totalRows;
        if (sh.rows.size() < lim.gridRows) {
            sh.columns = std::max(sh.columns, row.size());
            sh.rows.push_back(std::move(row));
        }
        row.clear();
        any = false;
    };
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < s.size() && s[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c == '\n' || c == '\r' ? ' ' : c;
            }
            continue;
        }
        if (c == '"' && field.empty()) {
            quoted = true;
            any = true;
        } else if (c == sep) {
            endField();
            any = true;
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ++i;
            endRow();
            if (lim.cancel && (sh.totalRows & 1023) == 0 && cancelled(lim)) break;
        } else {
            field += c;
            any = true;
        }
    }
    if (any || !field.empty()) endRow();
    sh.truncated = sh.totalRows > sh.rows.size();
    return sh;
}

bool readWorkbook(std::string_view bytes, std::vector<Sheet>& sheets, bool& macros, std::string& why, const Limits& lim) {
    sheets.clear();
    macros = false;
    Zip zip;
    if (!zip.openMemory(bytes)) {
        why = "ce n'est pas une archive zip valable (un classeur .xls ancien ne se lit pas ici)";
        return false;
    }
    std::string wb, rels, sst;
    if (!zip.readPrefix("xl/workbook.xml", 16u << 20, wb)) {
        why = "xl/workbook.xml absent : l'archive s'ouvre mais ce n'est pas un classeur";
        return false;
    }
    macros = zip.locate("xl/vbaProject.bin") >= 0;
    (void)zip.readPrefix("xl/_rels/workbook.xml.rels", 4u << 20, rels);
    (void)zip.readPrefix("xl/sharedStrings.xml", 64u << 20, sst, nullptr, &lim);
    std::map<std::string, std::string> targets;
    {
        importer::XmlReader r(rels);
        while (auto ev = r.next()) {
            if (*ev == importer::XmlEvent::EndOfDocument) break;
            if (*ev != importer::XmlEvent::StartElement || localName(r.name()) != "Relationship") continue;
            std::string t = r.attr("Target");
            if (!t.empty() && t[0] == '/') t.erase(0, 1);
            else if (!t.empty()) t = "xl/" + t;
            targets[r.attr("Id")] = t;
        }
    }
    // La table commune : un <si> par chaine, ses <t> mis bout a bout (les morceaux d'un texte enrichi).
    std::vector<std::string> strings;
    {
        importer::XmlReader r(sst);
        bool inSi = false, inT = false, inPhonetic = false;
        std::string cur;
        while (auto ev = r.next()) {
            if (*ev == importer::XmlEvent::EndOfDocument) break;
            if (*ev == importer::XmlEvent::StartElement) {
                const auto n = localName(r.name());
                if (n == "si") {
                    inSi = true;
                    cur.clear();
                    if (r.selfClosing()) {
                        strings.emplace_back();
                        inSi = false;
                    }
                } else if (n == "rPh") {
                    inPhonetic = true;
                } else if (n == "t" && inSi && !inPhonetic) {
                    inT = !r.selfClosing();
                }
            } else if (*ev == importer::XmlEvent::Text) {
                if (inT) cur += importer::XmlReader::decodeEntities(r.text());
            } else if (*ev == importer::XmlEvent::EndElement) {
                const auto n = localName(r.name());
                if (n == "t") inT = false;
                else if (n == "rPh") inPhonetic = false;
                else if (n == "si" && inSi) {
                    strings.push_back(cur);
                    inSi = false;
                }
            }
        }
    }
    struct Declared {
        std::string name, rid;
        bool        hidden{false};
    };
    std::vector<Declared> declared;
    {
        importer::XmlReader r(wb);
        while (auto ev = r.next()) {
            if (*ev == importer::XmlEvent::EndOfDocument) break;
            if (*ev != importer::XmlEvent::StartElement || localName(r.name()) != "sheet") continue;
            Declared d;
            d.name = r.attr("name");
            d.rid = attrLocal(r, "id");
            const std::string state = r.attr("state");
            d.hidden = state == "hidden" || state == "veryHidden";
            declared.push_back(std::move(d));
        }
    }
    if (declared.empty()) {
        why = "le classeur ne d\xC3\xA9" "clare aucun onglet";
        return false;
    }
    for (std::size_t i = 0; i < declared.size() && i < lim.sheets; ++i) {
        if (cancelled(lim)) break;
        Sheet sh;
        sh.name = declared[i].name;
        sh.hidden = declared[i].hidden;
        std::string path = targets.count(declared[i].rid) ? targets[declared[i].rid] : std::string{};
        if (path.empty()) path = "xl/worksheets/sheet" + std::to_string(i + 1) + ".xml";
        std::string xml;
        bool whole = false;
        // Le debut de la feuille suffit aux premieres lignes : 8 Mo decompresses au plus.
        if (!zip.readPrefix(path.c_str(), 8u << 20, xml, &whole, &lim)) {
            sh.name += " (illisible)";
            sheets.push_back(std::move(sh));
            continue;
        }
        // <dimension ref="A1:K5000"/> : le nombre de lignes annonce.
        if (const auto at = xml.find("<dimension"); at != std::string::npos && at < 4096) {
            const auto ref = xml.find("ref=\"", at);
            if (ref != std::string::npos) {
                const auto end = xml.find('"', ref + 5);
                const std::string spec = xml.substr(ref + 5, end == std::string::npos ? 0 : end - ref - 5);
                const auto colon = spec.find(':');
                sh.totalRows = xls::rowOf(colon == std::string::npos ? spec : spec.substr(colon + 1)) + 1;
            }
        }
        auto rows = xls::parseSheet(xml, strings, lim.gridRows);
        for (auto& row : rows) {
            if (row.size() > lim.gridCols) row.resize(lim.gridCols);
            for (auto& cell : row) cell = cleanLine(cell, 400);
            sh.columns = std::max(sh.columns, row.size());
        }
        // Les lignes vides du bas (une mise en forme qui deborde) ne comptent pas.
        while (!rows.empty() && std::all_of(rows.back().begin(), rows.back().end(), [](const std::string& c) { return c.empty(); })) rows.pop_back();
        sh.rows = std::move(rows);
        if (sh.totalRows < sh.rows.size()) sh.totalRows = sh.rows.size();
        sh.truncated = sh.totalRows > sh.rows.size() || (!whole && sh.rows.size() >= lim.gridRows);
        sheets.push_back(std::move(sh));
    }
    return true;
}

// =========================================================== JSON, XML indentes ==
Tree prettyJson(std::string_view s, std::size_t maxLines) {
    Tree t;
    std::string cur;
    int depth = 0, curDepth = 0;
    std::vector<char> stack;
    std::size_t lineNo = 1;
    const auto flush = [&] {
        if (cur.empty()) return;
        if (t.lines.size() >= maxLines) {
            t.truncated = true;
            cur.clear();
            return;
        }
        t.lines.push_back(cleanLine(cur, 2000));
        t.depth.push_back(curDepth);
        cur.clear();
    };
    const auto fail = [&](const std::string& what) {
        t.ok = false;
        t.error = "ligne " + std::to_string(lineNo) + " : " + what;
    };
    const auto nextSignificant = [&](std::size_t i) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        return i;
    };
    if (startsWith(s, "\xEF\xBB\xBF")) s.remove_prefix(3);
    for (std::size_t i = 0; i < s.size() && t.ok && !t.truncated; ++i) {
        const char c = s[i];
        if (c == '\n') ++lineNo;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (cur.empty()) curDepth = depth;
        if (c == '"') {
            std::size_t j = i + 1;
            for (; j < s.size(); ++j) {
                if (s[j] == '\\') {
                    ++j;
                    continue;
                }
                if (s[j] == '"' || s[j] == '\n') break;
            }
            if (j >= s.size() || s[j] != '"') {
                fail("cha\xC3\xAEne non ferm\xC3\xA9" "e");
                break;
            }
            cur.append(s.substr(i, j - i + 1));
            i = j;
        } else if (c == '{' || c == '[') {
            const std::size_t k = nextSignificant(i + 1);
            const char close = c == '{' ? '}' : ']';
            if (k < s.size() && s[k] == close) {
                cur += c;
                cur += close;
                i = k;
                continue;
            }
            cur += c;
            flush();
            stack.push_back(close);
            ++depth;
        } else if (c == '}' || c == ']') {
            if (stack.empty() || stack.back() != c) {
                fail(std::string("\xC2\xAB ") + c + " \xC2\xBB inattendu");
                break;
            }
            stack.pop_back();
            flush();
            --depth;
            curDepth = depth;
            cur += c;
        } else if (c == ',') {
            cur += ',';
            flush();
        } else if (c == ':') {
            cur += ": ";
        } else {
            std::size_t j = i;
            while (j < s.size() && std::string_view(",:{}[] \t\r\n\"").find(s[j]) == std::string_view::npos) ++j;
            const std::string_view word = s.substr(i, j - i);
            const bool number = !word.empty() && (word[0] == '-' || (word[0] >= '0' && word[0] <= '9'));
            if (!number && word != "true" && word != "false" && word != "null") {
                fail("\xC2\xAB " + std::string(word.substr(0, 24)) + " \xC2\xBB n'est pas du JSON");
                break;
            }
            cur.append(word);
            i = j - 1;
        }
    }
    flush();
    if (t.ok && !stack.empty() && !t.truncated) {
        t.ok = false;
        t.error = "le texte s'arr\xC3\xAAte avant la fin (" + std::to_string(stack.size()) + " bloc(s) non ferm\xC3\xA9(s))";
    }
    return t;
}

Tree prettyXml(std::string_view s, std::size_t maxLines) {
    Tree t;
    if (startsWith(s, "\xEF\xBB\xBF")) s.remove_prefix(3);
    struct Token {
        enum Type { Open, Close, Empty, Text, Other } type;
        std::string_view text;
        std::string_view name;
    };
    std::vector<Token> tokens;
    std::size_t lineNo = 1;
    const auto lineAt = [&](std::size_t upto) {
        return lineNo + static_cast<std::size_t>(std::count(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(std::min(upto, s.size())), '\n'));
    };
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] != '<') {
            const auto lt = s.find('<', i);
            const std::string_view raw = s.substr(i, lt == std::string_view::npos ? std::string_view::npos : lt - i);
            std::size_t a = 0, b = raw.size();
            while (a < b && std::isspace(static_cast<unsigned char>(raw[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(raw[b - 1]))) --b;
            if (b > a) tokens.push_back({Token::Text, raw.substr(a, b - a), {}});
            if (lt == std::string_view::npos) break;
            i = lt;
            continue;
        }
        std::string_view end = ">";
        Token::Type type = Token::Open;
        if (startsWith(s.substr(i), "<!--")) {
            end = "-->";
            type = Token::Other;
        } else if (startsWith(s.substr(i), "<![CDATA[")) {
            end = "]]>";
            type = Token::Text;
        } else if (startsWith(s.substr(i), "<?")) {
            end = "?>";
            type = Token::Other;
        } else if (startsWith(s.substr(i), "<!")) {
            type = Token::Other;
        } else if (startsWith(s.substr(i), "</")) {
            type = Token::Close;
        }
        // Un > dans une valeur d'attribut entre guillemets ne ferme pas la balise.
        std::size_t close = std::string_view::npos;
        if (end == ">" && type != Token::Other) {
            char quote = 0;
            for (std::size_t j = i + 1; j < s.size(); ++j) {
                if (quote) {
                    if (s[j] == quote) quote = 0;
                } else if (s[j] == '"' || s[j] == '\'') {
                    quote = s[j];
                } else if (s[j] == '>') {
                    close = j;
                    break;
                }
            }
        } else {
            close = s.find(end, i);
            if (close != std::string_view::npos) close += end.size() - 1;
        }
        if (close == std::string_view::npos) {
            t.truncated = true;   // coupee par la fin du morceau lu (ou jamais fermee)
            break;
        }
        const std::string_view tag = s.substr(i, close - i + 1);
        Token tk{type, tag, {}};
        if (type == Token::Open && tag.size() >= 2 && tag[tag.size() - 2] == '/') tk.type = Token::Empty;
        if (tk.type == Token::Open || tk.type == Token::Empty || tk.type == Token::Close) {
            std::size_t a = tk.type == Token::Close ? 2 : 1, b = a;
            while (b < tag.size() && !std::isspace(static_cast<unsigned char>(tag[b])) && tag[b] != '>' && tag[b] != '/') ++b;
            tk.name = tag.substr(a, b - a);
        }
        tokens.push_back(tk);
        i = close + 1;
        if (tokens.size() > maxLines * 3 + 16) {
            t.truncated = true;
            break;
        }
    }
    int depth = 0;
    std::vector<std::string_view> open;
    const auto emit = [&](std::string line, int d) {
        if (t.lines.size() >= maxLines) {
            t.truncated = true;
            return false;
        }
        t.lines.push_back(cleanLine(line, 2000));
        t.depth.push_back(std::max(0, d));
        return true;
    };
    for (std::size_t k = 0; k < tokens.size(); ++k) {
        const Token& tk = tokens[k];
        if (tk.type == Token::Open) {
            // <a>texte</a> sur une ligne.
            if (k + 2 < tokens.size() && tokens[k + 1].type == Token::Text && tokens[k + 2].type == Token::Close
                && tokens[k + 2].name == tk.name && codepoints(tokens[k + 1].text) <= 120) {
                if (!emit(std::string(tk.text) + std::string(tokens[k + 1].text) + std::string(tokens[k + 2].text), depth)) break;
                k += 2;
                continue;
            }
            if (k + 1 < tokens.size() && tokens[k + 1].type == Token::Close && tokens[k + 1].name == tk.name) {
                if (!emit(std::string(tk.text) + std::string(tokens[k + 1].text), depth)) break;
                ++k;
                continue;
            }
            if (!emit(std::string(tk.text), depth)) break;
            open.push_back(tk.name);
            ++depth;
        } else if (tk.type == Token::Close) {
            if (open.empty() || open.back() != tk.name) {
                if (t.ok) {
                    t.ok = false;
                    t.error = "ligne " + std::to_string(lineAt(static_cast<std::size_t>(tk.text.data() - s.data()))) + " : </"
                            + std::string(tk.name) + "> ne ferme pas " + (open.empty() ? std::string("de balise ouverte") : "<" + std::string(open.back()) + ">");
                }
            } else {
                open.pop_back();
            }
            depth = std::max(0, depth - 1);
            if (!emit(std::string(tk.text), depth)) break;
        } else {
            if (!emit(std::string(tk.text), depth)) break;
        }
    }
    if (t.ok && !open.empty() && !t.truncated) {
        t.ok = false;
        t.error = "<" + std::string(open.back()) + "> n'est jamais ferm\xC3\xA9" "e";
    }
    (void)lineNo;
    return t;
}

bool opensBlock(const std::vector<int>& depth, std::size_t line) { return line + 1 < depth.size() && depth[line + 1] > depth[line]; }

std::vector<std::size_t> visibleLines(const std::vector<int>& depth, const std::vector<bool>& folded) {
    std::vector<std::size_t> out;
    out.reserve(depth.size());
    for (std::size_t i = 0; i < depth.size(); ++i) {
        out.push_back(i);
        if (i < folded.size() && folded[i] && opensBlock(depth, i)) {
            std::size_t j = i + 1;
            while (j < depth.size() && depth[j] > depth[i]) ++j;
            i = j - 1;
        }
    }
    return out;
}

// ======================================================================= DOCX ==
bool readDocx(std::string_view bytes, std::vector<Paragraph>& out, std::string& why, const Limits& lim) {
    out.clear();
    Zip zip;
    if (!zip.openMemory(bytes)) {
        why = "ce n'est pas une archive zip valable (un .doc ancien ne se lit pas ici)";
        return false;
    }
    std::string xml, styles;
    bool whole = false;
    if (!zip.readPrefix("word/document.xml", 32u << 20, xml, &whole, &lim)) {
        why = "word/document.xml absent : ce n'est pas un document Word";
        return false;
    }
    (void)zip.readPrefix("word/styles.xml", 8u << 20, styles);
    // Les styles de titre : leur nom anglais ("heading 1", "Title") ou leur niveau de plan.
    std::map<std::string, int> levels;
    {
        importer::XmlReader r(styles);
        std::string id;
        while (auto ev = r.next()) {
            if (*ev == importer::XmlEvent::EndOfDocument) break;
            if (*ev != importer::XmlEvent::StartElement) continue;
            const auto n = localName(r.name());
            if (n == "style") {
                id = attrLocal(r, "styleId");
            } else if (n == "name" && !id.empty()) {
                const std::string v = lower(attrLocal(r, "val"));
                if (v == "title" || v == "titre") levels[id] = 1;
                for (const char* p : {"heading ", "titre "})
                    if (v.rfind(p, 0) == 0) {
                        const int lv = std::atoi(v.c_str() + std::strlen(p));
                        if (lv >= 1 && lv <= 9) levels[id] = lv;
                    }
            } else if (n == "outlineLvl" && !id.empty() && !levels.count(id)) {
                const int lv = std::atoi(attrLocal(r, "val").c_str());
                if (lv >= 0 && lv <= 8) levels[id] = lv + 1;
            }
        }
    }
    const auto styleLevel = [&](const std::string& id) {
        if (const auto it = levels.find(id); it != levels.end()) return it->second;
        const std::string v = lower(id);
        if (v == "title" || v == "titre") return 1;
        for (const char* p : {"heading", "titre", "berschrift"})
            if (const auto at = v.find(p); at != std::string::npos) {
                const int lv = std::atoi(v.c_str() + at + std::strlen(p));
                if (lv >= 1 && lv <= 9) return lv;
            }
        return 0;
    };
    importer::XmlReader r(xml);
    Paragraph para;
    bool inPara = false;
    int tableDepth = 0;
    std::string row;
    const auto pushPara = [&](Paragraph p) {
        while (!p.text.empty() && (p.text.back() == ' ' || p.text.back() == '\n')) p.text.pop_back();
        if (p.text.empty() && (out.empty() || out.back().text.empty())) return;   // pas deux vides de suite
        if (out.size() < lim.paragraphs) out.push_back(std::move(p));
    };
    while (auto ev = r.next()) {
        if (*ev == importer::XmlEvent::EndOfDocument) break;
        if (out.size() >= lim.paragraphs || cancelled(lim)) break;
        const auto n = localName(r.name());
        if (*ev == importer::XmlEvent::StartElement) {
            if (n == "p") {
                para = Paragraph{};
                inPara = !r.selfClosing();
                if (!inPara && tableDepth == 0) pushPara(Paragraph{});
            } else if (n == "pStyle" && inPara) {
                para.level = styleLevel(attrLocal(r, "val"));
            } else if (n == "outlineLvl" && inPara) {
                const int lv = std::atoi(attrLocal(r, "val").c_str());
                if (lv >= 0 && lv <= 8) para.level = lv + 1;
            } else if (n == "numPr" && inPara) {
                para.item = true;
            } else if (n == "t" && !r.selfClosing()) {
                if (auto raw = r.readElementRaw()) para.text += elementText(*raw);
            } else if (n == "tab") {
                para.text += "    ";
            } else if ((n == "br" || n == "cr") && inPara) {
                para.text += '\n';
            } else if (n == "tbl") {
                ++tableDepth;
            } else if (n == "tr") {
                row.clear();
            }
        } else if (*ev == importer::XmlEvent::EndElement) {
            if (n == "p" && inPara) {
                inPara = false;
                if (tableDepth > 0) {
                    if (!para.text.empty()) row += (row.empty() || row.back() == ' ' ? "" : " ") + para.text;
                } else {
                    pushPara(std::move(para));
                }
                para = Paragraph{};
            } else if (n == "tc" && tableDepth > 0) {
                row += " | ";
            } else if (n == "tr" && tableDepth > 0) {
                while (row.size() >= 3 && row.compare(row.size() - 3, 3, " | ") == 0) row.resize(row.size() - 3);
                Paragraph p;
                p.table = true;
                p.text = row;
                if (!row.empty()) pushPara(std::move(p));
                row.clear();
            } else if (n == "tbl") {
                tableDepth = std::max(0, tableDepth - 1);
            }
        }
    }
    if (inPara && !para.text.empty()) pushPara(std::move(para));
    if (!whole) why = "document coup\xC3\xA9 : seul son d\xC3\xA9" "but est lu";
    return true;
}

// ======================================================================== ZIP ==
namespace {
bool listOpened(Zip& zip, std::vector<ZipItem>& out, std::size_t& total, const Limits& lim) {
    out.clear();
    total = zip.count();
    for (std::size_t i = 0; i < total && out.size() < lim.zipEntries; ++i) {
        if ((i & 1023) == 0 && cancelled(lim)) break;
        mz_zip_archive_file_stat st;
        if (!zip.stat(i, st)) continue;
        ZipItem it;
        it.name = cleanName(st.m_filename);
        it.size = st.m_uncomp_size;
        it.packed = st.m_comp_size;
        it.folder = st.m_is_directory != 0;
        it.encrypted = st.m_is_encrypted != 0;
        out.push_back(std::move(it));
    }
    return true;
}
} // namespace

bool listZip(std::string_view bytes, std::vector<ZipItem>& out, std::size_t& total, std::string& why, const Limits& lim) {
    Zip zip;
    if (!zip.openMemory(bytes)) {
        why = "archive zip ab\xC3\xAEm\xC3\xA9" "e (sa table des mati\xC3\xA8res ne se lit pas)";
        return false;
    }
    return listOpened(zip, out, total, lim);
}

bool listZipFile(const std::string& path, std::vector<ZipItem>& out, std::size_t& total, std::string& why, const Limits& lim) {
    Zip zip;
    if (!zip.openFile(path)) {
        why = "archive zip ab\xC3\xAEm\xC3\xA9" "e (sa table des mati\xC3\xA8res ne se lit pas)";
        return false;
    }
    return listOpened(zip, out, total, lim);
}

// ===================================================== le vidage hexadecimal ==
std::vector<std::string> hexDump(std::string_view b, std::size_t maxBytes, std::uint64_t offset) {
    std::vector<std::string> out;
    const std::size_t n = std::min(b.size(), maxBytes);
    static const char* hex = "0123456789ABCDEF";
    for (std::size_t at = 0; at < n; at += 16) {
        char head[24];
        std::snprintf(head, sizeof head, "%08llX  ", static_cast<unsigned long long>(offset + at));
        std::string line = head;
        std::string ascii;
        for (std::size_t k = 0; k < 16; ++k) {
            if (k == 8) line += ' ';
            if (at + k < n) {
                const auto c = static_cast<unsigned char>(b[at + k]);
                line += hex[c >> 4];
                line += hex[c & 15];
                line += ' ';
                ascii += c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '.';
            } else {
                line += "   ";
            }
        }
        line += " |" + ascii + "|";
        out.push_back(std::move(line));
    }
    return out;
}

// ================================================================== la source ==
std::string Source::key() const {
    std::string k = name + "\n" + path + "\n" + shownPath + "\n";
    if (data) k += "blob:" + std::to_string(reinterpret_cast<std::uintptr_t>(data.get())) + ":" + std::to_string(data->size());
    if (fetch) k += "fetch";
    if (!path.empty() && !data) {
        std::error_code ec;
        const fs::path p = pathOf(path);
        k += ":" + std::to_string(modifiedOf(p));
        if (fs::is_regular_file(p, ec)) k += ":" + std::to_string(fs::file_size(p, ec));
    }
    return k;
}

Source fromPath(const std::string& path) {
    Source s;
    s.path = path;
    s.name = leafOf(path);
    return s;
}

Source fromBytes(std::string name, hmi::BlobPtr data, std::string origin) {
    Source s;
    s.name = std::move(name);
    s.data = std::move(data);
    s.origin = std::move(origin);
    return s;
}

std::string Preview::fact(std::string_view label) const {
    for (const auto& [k, v] : facts)
        if (k == label) return v;
    return {};
}

// ================================================================== charger ==
namespace {

// Les octets, en memoire (bornes) : ceux de la source, ou le fichier lu.
bool bytesOf(const Source& src, const Preview& p, std::uint64_t max, hmi::BlobPtr& out, std::string& why) {
    if (src.data) {
        if (src.data->size() > max) {
            why = "trop gros pour l'aper\xC3\xA7u (" + sizeText(src.data->size()) + ", " + sizeText(max) + " au plus)";
            return false;
        }
        out = src.data;
        return true;
    }
    if (p.size > max) {
        why = "trop gros pour l'aper\xC3\xA7u (" + sizeText(p.size) + ", " + sizeText(max) + " au plus) : Ouvrir avec le programme du syst\xC3\xA8me";
        return false;
    }
    std::ifstream in(pathOf(src.path), std::ios::binary);
    if (!in) {
        why = "illisible (droits, fichier verrouill\xC3\xA9 ?)";
        return false;
    }
    auto bytes = std::make_shared<hmi::Bytes>(static_cast<std::size_t>(p.size));
    in.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    bytes->resize(static_cast<std::size_t>(std::max<std::streamsize>(0, in.gcount())));
    out = std::move(bytes);
    return true;
}

// Le debut (au plus `max` octets) ; `complete` : c'est tout le fichier.
bool headOf(const Source& src, const Preview& p, std::size_t max, std::string& out, bool& complete) {
    if (src.data) {
        out.assign(reinterpret_cast<const char*>(src.data->data()), std::min(src.data->size(), max));
        complete = src.data->size() <= max;
        return true;
    }
    complete = p.size <= max;
    return readHead(src.path, std::min<std::uint64_t>(max, p.size + 1), out);
}

void loadText(const Source& src, Preview& p, const Limits& lim) {
    std::string head;
    bool complete = true;
    if (!headOf(src, p, lim.textBytes, head, complete)) {
        p.error = "illisible (droits, fichier verrouill\xC3\xA9 ?)";
        return;
    }
    const TextView tv = readText(head, lim, complete);
    p.encoding = tv.encoding;
    p.lines = tv.lines;
    p.truncated = tv.truncated;
    p.facts.emplace_back("Encodage", std::string(encodingLabel(tv.encoding)));
    if (!tv.endings.empty()) p.facts.emplace_back("Fins de ligne", tv.endings);
    p.facts.emplace_back("Lignes", (tv.truncated ? std::string("au moins ") : std::string{}) + thousands(tv.lines.size()));
    if (tv.truncated) p.notes.push_back("seul le d\xC3\xA9" "but est montr\xC3\xA9 (" + plural(tv.lines.size(), "ligne", "lignes") + ")");
}

void loadTree(const Source& src, Preview& p, const Limits& lim, bool json) {
    std::string head;
    bool complete = true;
    if (!headOf(src, p, lim.textBytes, head, complete)) {
        p.error = "illisible (droits, fichier verrouill\xC3\xA9 ?)";
        return;
    }
    const Encoding enc = detectEncoding(head);
    const std::string text = toUtf8(head, enc);
    Tree t = json ? prettyJson(text, lim.textLines) : prettyXml(text, lim.textLines);
    p.encoding = enc;
    p.facts.emplace_back("Encodage", std::string(encodingLabel(enc)));
    if (!t.ok && complete) {
        // Mal forme : le texte tel quel, et ou il casse.
        p.notes.push_back(std::string(json ? "JSON" : "XML") + " mal form\xC3\xA9 (" + t.error + ") : montr\xC3\xA9 tel quel");
        const TextView tv = readText(head, lim, complete);
        p.lines = tv.lines;
        p.depth.assign(p.lines.size(), 0);
        p.truncated = tv.truncated;
        p.facts.emplace_back("Lignes", thousands(tv.lines.size()));
        return;
    }
    p.lines = std::move(t.lines);
    p.depth = std::move(t.depth);
    p.truncated = t.truncated || !complete;
    p.facts.emplace_back("Lignes (indent\xC3\xA9" "es)", (p.truncated ? std::string("au moins ") : std::string{}) + thousands(p.lines.size()));
    if (p.truncated) p.notes.push_back("seul le d\xC3\xA9" "but est montr\xC3\xA9 (le fichier est long)");
}

void loadCsv(const Source& src, Preview& p, const Limits& lim) {
    std::string head;
    bool complete = true;
    if (!headOf(src, p, lim.textBytes, head, complete)) {
        p.error = "illisible (droits, fichier verrouill\xC3\xA9 ?)";
        return;
    }
    const Encoding enc = detectEncoding(head);
    const std::string text = toUtf8(head, enc);
    const char sep = extensionOf(p.name) == "tsv" ? '\t' : guessSeparator(text);
    Sheet sh = parseCsv(text, sep, lim);
    sh.name = p.name;
    if (!complete) sh.truncated = true;
    p.encoding = enc;
    const char* sepName = sep == ';' ? "point-virgule (;)" : sep == ',' ? "virgule (,)" : sep == '\t' ? "tabulation" : "barre (|)";
    p.facts.emplace_back("S\xC3\xA9parateur", sepName);
    p.facts.emplace_back("Encodage", std::string(encodingLabel(enc)));
    p.facts.emplace_back("Lignes", (complete ? std::string{} : std::string("au moins ")) + thousands(sh.totalRows));
    p.facts.emplace_back("Colonnes", std::to_string(sh.columns));
    if (sh.truncated) p.notes.push_back("les " + thousands(sh.rows.size()) + " premi\xC3\xA8res lignes sont montr\xC3\xA9" "es");
    p.truncated = sh.truncated;
    p.sheets.push_back(std::move(sh));
}

void loadBinary(const Source& src, Preview& p, const Limits& lim, const Signature& sig) {
    std::string head;
    bool complete = true;
    if (!headOf(src, p, lim.hexBytes, head, complete)) {
        p.error = "illisible (droits, fichier verrouill\xC3\xA9 ?)";
        return;
    }
    p.facts.emplace_back("Reconnu", sig.format.empty() ? std::string("aucune signature connue") : sig.label);
    p.lines = hexDump(head, lim.hexBytes);
    p.truncated = !complete;
    if (!complete) p.notes.push_back("les " + sizeText(std::min<std::uint64_t>(head.size(), lim.hexBytes)) + " premiers octets, en hexad\xC3\xA9" "cimal");
}

void loadMedia(const Source& src, Preview& p, const Limits& lim) {
    hmi::BlobPtr blob;
    std::string why;
    if (!bytesOf(src, p, lim.memoryBytes, blob, why)) {
        p.notes.push_back(why);
        return;
    }
    p.bytes = blob;
    p.media = hmi::inspectMedia(*blob, p.name);
    const auto& m = p.media;
    if (!m.format.empty()) p.format = m.format;
    switch (p.kind) {
        case Kind::Image: {
            if (m.width > 0 && m.height > 0) p.facts.emplace_back("Dimensions", dims(m.width, m.height) + " pixels");
            if (m.images > 1) p.facts.emplace_back("Images", std::to_string(m.images) + (p.format == "GIF" ? " (anim\xC3\xA9)" : ""));
            if (m.seconds > 0 && p.format == "GIF") p.facts.emplace_back("Dur\xC3\xA9" "e d'un tour", hmi::formatDuration(m.seconds));
            const std::uint64_t pixels = static_cast<std::uint64_t>(std::max(0, m.width)) * static_cast<std::uint64_t>(std::max(0, m.height));
            if (pixels > lim.imagePixels) {
                p.notes.push_back("image trop grande pour l'aper\xC3\xA7u (" + dims(m.width, m.height) + ")");
                return;
            }
            hmi::Rgba rgba;
            std::string err;
            const std::string format = p.format == "SVG" || p.format == "ICO" ? p.format : std::string{};
            if (!hmi::decodeImage(*blob, format, rgba, lim.imageSide, &err)) {
                p.error = "image illisible : " + (err.empty() ? std::string("format non d\xC3\xA9" "cod\xC3\xA9 ici") : err);
                return;
            }
            if (m.width <= 0 && rgba.width > 0) p.facts.emplace_back("Dimensions", dims(rgba.width, rgba.height) + " pixels");
            p.image = std::make_shared<const hmi::Rgba>(shrink(rgba, lim.imageSide));
            // Un GIF anime : ses images, si elles tiennent (96 Mo de pixels, 120 images au plus) ; sinon la premiere.
            if (p.format == "GIF" && m.images > 1 && !cancelled(lim)) {
                const std::uint64_t one = static_cast<std::uint64_t>(std::max(1, m.width)) * static_cast<std::uint64_t>(std::max(1, m.height)) * 4u;
                const int count = std::min(m.images, 120);
                if (one * static_cast<std::uint64_t>(count) <= (96ull << 20)) {
                    hmi::GifFrames gif;
                    if (hmi::decodeGif(*blob, gif, nullptr, count) && gif.frames.size() > 1) {
                        for (std::size_t i = 0; i < gif.frames.size(); ++i) {
                            p.frames.push_back(std::make_shared<const hmi::Rgba>(shrink(gif.frames[i], lim.imageSide)));
                            p.frameMs.push_back(i < gif.timing.delaysMs.size() ? std::max(20, gif.timing.delaysMs[i]) : 100);
                        }
                    }
                } else {
                    p.notes.push_back("animation trop lourde pour l'aper\xC3\xA7u : la premi\xC3\xA8re image seulement");
                }
            }
            return;
        }
        case Kind::Video:
            if (m.seconds > 0) p.facts.emplace_back("Dur\xC3\xA9" "e", hmi::formatDuration(m.seconds));
            if (m.width > 0) p.facts.emplace_back("Dimensions", dims(m.width, m.height));
            if (!m.codec.empty()) p.facts.emplace_back("Codec", m.codec);
            if (!m.ok()) p.notes.push_back(m.error);
            else if (!hmi::video::decodedHere(m.format, m.codec)) {
                const std::string advice = hmi::video::decodeAdvice(m.format, m.codec);
                p.notes.push_back(advice.empty() ? std::string("cette vid\xC3\xA9o ne se d\xC3\xA9" "code pas ici : sa premi\xC3\xA8re image manque") : advice);
            }
            return;
        case Kind::Sound:
            if (m.seconds > 0) p.facts.emplace_back("Dur\xC3\xA9" "e", hmi::formatDuration(m.seconds));
            if (m.channels > 0) p.facts.emplace_back("Canaux", m.channels == 1 ? std::string("mono") : m.channels == 2 ? std::string("st\xC3\xA9r\xC3\xA9o") : std::to_string(m.channels));
            if (m.sampleRate > 0) p.facts.emplace_back("Fr\xC3\xA9quence", std::to_string(m.sampleRate) + " Hz");
            if (!m.codec.empty()) p.facts.emplace_back("Codec", m.codec);
            if (!m.ok()) {
                p.notes.push_back(m.error);
                return;
            }
            if (!hmi::soundEnvelope(*blob, m.format, 256, p.envelope)) {
                p.envelope.clear();
                p.notes.push_back("ce son ne se d\xC3\xA9" "code pas ici (WAV et MP3 seulement)");
            }
            return;
        case Kind::Font: {
            if (!m.family.empty()) p.facts.emplace_back("Famille", m.family);
            if (!m.style.empty()) p.facts.emplace_back("Style", m.style);
            if (m.glyphs > 0) p.facts.emplace_back("Glyphes", thousands(static_cast<std::uint64_t>(m.glyphs)));
            if (!m.ok()) {
                p.error = "police illisible : " + m.error;
                return;
            }
            const std::pair<const char*, float> samples[] = {{"Aa Bb Cc 0123456789", 36.f},
                                                             {"Temp\xC3\xA9rature : 21,5 \xC2\xB0" "C", 26.f},
                                                             {"ABCDEFGHIJKLMNOPQRSTUVWXYZ", 18.f},
                                                             {"abcdefghijklmnopqrstuvwxyz \xC3\xA9\xC3\xA8\xC3\xA0\xC3\xA7", 18.f}};
            for (const auto& [text, px] : samples) {
                hmi::Rgba img;
                if (!hmi::renderFontText(*blob, text, px, img) || img.width <= 0) continue;
                p.fontSamples.push_back(std::make_shared<const hmi::Rgba>(std::move(img)));
                p.fontSampleTexts.emplace_back(text);
            }
            if (p.fontSamples.empty()) p.notes.push_back("aucun texte ne s'\xC3\xA9" "crit avec cette police");
            return;
        }
        case Kind::Pdf: {
            const std::string_view head(reinterpret_cast<const char*>(blob->data()), std::min<std::size_t>(blob->size(), 1024));
            const auto at = head.find("%PDF-");
            if (at == std::string_view::npos) {
                p.error = "pas un PDF lisible : l'en-t\xC3\xAAte %PDF manque";
                return;
            }
            std::string version(head.substr(at + 5, 3));
            if (!version.empty()) p.facts.emplace_back("Version", "PDF " + version);
            const std::string_view tail(reinterpret_cast<const char*>(blob->data()) + (blob->size() > 4096 ? blob->size() - 4096 : 0),
                                        std::min<std::size_t>(blob->size(), 4096));
            if (tail.find("/Encrypt") != std::string_view::npos) p.notes.push_back("PDF prot\xC3\xA9g\xC3\xA9 (chiffr\xC3\xA9)");
            return;
        }
        default:
            return;
    }
}

void loadZipLike(const Source& src, Preview& p, const Limits& lim) {
    std::string why;
    if (p.kind == Kind::Zip) {
        bool ok = false;
        if (!src.data && !src.path.empty()) {
            ok = listZipFile(src.path, p.entries, p.entryCount, why, lim);
        } else {
            hmi::BlobPtr blob;
            if (!bytesOf(src, p, lim.memoryBytes, blob, why)) {
                p.notes.push_back(why);
                return;
            }
            ok = listZip(std::string_view(reinterpret_cast<const char*>(blob->data()), blob->size()), p.entries, p.entryCount, why, lim);
        }
        if (!ok) {
            p.error = why;
            return;
        }
        std::uint64_t total = 0;
        std::size_t folders = 0, locked = 0;
        for (const auto& e : p.entries) {
            total += e.size;
            folders += e.folder;
            locked += e.encrypted;
        }
        p.facts.emplace_back("Entr\xC3\xA9" "es", thousands(p.entryCount) + (folders ? " (dont " + plural(folders, "dossier", "dossiers") + ")" : std::string{}));
        p.facts.emplace_back("D\xC3\xA9" "compress\xC3\xA9", sizeText(total) + (p.entryCount > p.entries.size() ? std::string(" (les entr\xC3\xA9" "es montr\xC3\xA9" "es)") : std::string{}));
        if (locked) p.notes.push_back(plural(locked, "entr\xC3\xA9" "e chiffr\xC3\xA9" "e", "entr\xC3\xA9" "es chiffr\xC3\xA9" "es") + " (mot de passe)");
        if (p.entryCount > p.entries.size()) {
            p.truncated = true;
            p.notes.push_back("les " + thousands(p.entries.size()) + " premi\xC3\xA8res entr\xC3\xA9" "es sont montr\xC3\xA9" "es");
        }
        const std::string ext = extensionOf(p.name);
        if (ext == "xihm" || ext == "xapi") p.notes.push_back("un paquet d'\xC3\xA9" "change de l'appli (une archive zip)");
        return;
    }
    hmi::BlobPtr blob;
    if (!bytesOf(src, p, lim.memoryBytes, blob, why)) {
        p.notes.push_back(why);
        return;
    }
    const std::string_view bytes(reinterpret_cast<const char*>(blob->data()), blob->size());
    if (p.kind == Kind::Workbook) {
        bool macros = false;
        if (!readWorkbook(bytes, p.sheets, macros, why, lim)) {
            p.error = "classeur illisible : " + why;
            return;
        }
        p.facts.emplace_back("Onglets", std::to_string(p.sheets.size()));
        if (macros) p.facts.emplace_back("Macros", "oui (VBA, non ex\xC3\xA9" "cut\xC3\xA9" "es)");
        for (const auto& s : p.sheets)
            if (s.truncated) {
                p.truncated = true;
                p.notes.push_back("chaque onglet montre ses " + thousands(lim.gridRows) + " premi\xC3\xA8res lignes");
                break;
            }
        return;
    }
    if (p.kind == Kind::Docx) {
        if (!readDocx(bytes, p.paragraphs, why, lim)) {
            p.error = "document illisible : " + why;
            return;
        }
        if (!why.empty()) p.notes.push_back(why);
        std::size_t titles = 0, rows = 0;
        for (const auto& para : p.paragraphs) {
            titles += para.level > 0;
            rows += para.table;
        }
        p.facts.emplace_back("Paragraphes", thousands(p.paragraphs.size()));
        if (titles) p.facts.emplace_back("Titres", thousands(titles));
        if (rows) p.facts.emplace_back("Lignes de tableau", thousands(rows));
        if (p.paragraphs.size() >= lim.paragraphs) {
            p.truncated = true;
            p.notes.push_back("seul le d\xC3\xA9" "but du document est montr\xC3\xA9");
        }
    }
}

} // namespace

bool slow(const Source& s) {
    if (s.fetch) return true;
    if (!s.data) return !s.path.empty();
    if (s.data->size() > (2u << 20)) return true;
    const Kind k = kindOfName(s.name);
    return k == Kind::Sound || k == Kind::Workbook;
}

Preview load(const Source& src, const Limits& lim) {
    Preview p;
    p.name = src.name.empty() ? leafOf(src.path) : src.name;
    p.path = src.path;
    p.shownPath = src.shownPath.empty() ? src.path : src.shownPath;
    p.origin = src.origin;
    p.modified = src.modified;
    try {
        Source local = src;
        // Sur le disque : la, un dossier, un fichier ?
        if (!src.data && !src.fetch) {
            if (src.path.empty()) {
                p.kind = Kind::Missing;
                return p;
            }
            const fs::path fp = pathOf(src.path);
            std::error_code ec;
            const auto st = fs::status(fp, ec);
            if (ec || !fs::exists(st)) {
                p.kind = Kind::Missing;
                p.typeLabel = labelFor(p.name, kindOfName(p.name), {});
                p.error = "introuvable : " + src.path;
                return p;
            }
            p.onDisk = true;
            if (p.modified == 0) p.modified = modifiedOf(fp);
            if (fs::is_directory(st)) {
                p.kind = Kind::Folder;
                p.typeLabel = "Dossier";
                const ui::FilePreviewInfo info = preview::describe(src.path, {});
                p.facts = info.facts;
                p.notes = info.notes;
                p.error = info.error;
                return p;
            }
            p.size = fs::file_size(fp, ec);
        } else if (src.fetch && !src.data) {
            hmi::Bytes b;
            std::string why;
            if (!src.fetch(b, why)) {
                p.kind = kindOfName(p.name);
                p.typeLabel = labelFor(p.name, p.kind, {});
                p.error = "illisible : " + why;
                return p;
            }
            local.data = std::make_shared<const hmi::Bytes>(std::move(b));
        }
        if (local.data) p.size = local.data->size();
        if (cancelled(lim)) return p;
        // Reconnaitre : l'extension, puis les premiers octets.
        std::string head;
        bool complete = true;
        if (!headOf(local, p, 64u * 1024u, head, complete)) {
            p.kind = kindOfName(p.name);
            p.typeLabel = labelFor(p.name, p.kind, {});
            p.error = "illisible (droits, fichier verrouill\xC3\xA9 ?)";
            return p;
        }
        const auto* hd = reinterpret_cast<const std::uint8_t*>(head.data());
        const Recognition rec = recognize(p.name, hd, head.size());
        p.kind = rec.kind;
        p.typeLabel = rec.label;
        p.format = rec.format;
        if (rec.mismatch && !rec.note.empty()) p.notes.push_back(rec.note);
        if (p.size == 0) {
            p.notes.push_back("fichier vide");
            if (p.kind != Kind::Text) p.kind = Kind::Binary;
            return p;
        }
        switch (p.kind) {
            case Kind::Image:
            case Kind::Video:
            case Kind::Sound:
            case Kind::Font:
            case Kind::Pdf: loadMedia(local, p, lim); break;
            case Kind::Text: loadText(local, p, lim); break;
            case Kind::Json: loadTree(local, p, lim, true); break;
            case Kind::Xml: loadTree(local, p, lim, false); break;
            case Kind::Csv: loadCsv(local, p, lim); break;
            case Kind::Workbook:
            case Kind::Docx:
            case Kind::Zip: loadZipLike(local, p, lim); break;
            case Kind::ControlExpert:
                if (p.onDisk) {
                    const ui::FilePreviewInfo info = preview::describe(src.path, {});
                    p.facts = info.facts;
                    for (const auto& n : info.notes) p.notes.push_back(n);
                    if (!info.error.empty()) p.notes.push_back(info.error);
                }
                {
                    Limits head2 = lim;
                    head2.textLines = std::min<std::size_t>(lim.textLines, 400);
                    head2.textBytes = std::min<std::size_t>(lim.textBytes, 256u * 1024u);
                    loadTree(local, p, head2, false);
                }
                break;
            case Kind::Binary: loadBinary(local, p, lim, signatureOf(hd, head.size())); break;
            case Kind::Folder:
            case Kind::Missing: break;
        }
        // Des octets deja en memoire (une ressource, une entree de zip lue) : gardes, pour Ouvrir avec le programme du
        // systeme (une copie temporaire).
        if (!p.bytes && local.data) p.bytes = local.data;
    } catch (const std::exception& e) {
        p.error = std::string("illisible : ") + e.what();
    } catch (...) {
        p.error = "illisible : erreur inattendue en lisant le fichier";
    }
    return p;
}

std::vector<std::string> describeLines(const Preview& p) {
    std::vector<std::string> out;
    out.push_back("Nom : " + p.name);
    out.push_back("Type : " + p.typeLabel);
    if (p.kind != Kind::Folder && p.kind != Kind::Missing) out.push_back("Taille : " + sizeText(p.size));
    if (p.modified > 0) out.push_back("Modifi\xC3\xA9 : " + hmi::explorer::dateText(p.modified));
    if (!p.origin.empty()) out.push_back("Origine : " + p.origin);
    for (const auto& [k, v] : p.facts) out.push_back(k + " : " + v);
    for (const auto& n : p.notes) out.push_back("Note : " + n);
    if (!p.error.empty()) out.push_back("Illisible : " + p.error);
    if (p.image) out.push_back("Image d\xC3\xA9" "cod\xC3\xA9" "e : " + dims(p.image->width, p.image->height));
    if (!p.envelope.empty()) out.push_back("Onde : " + std::to_string(p.envelope.size() / 2) + " colonnes");
    for (std::size_t i = 0; i < p.lines.size() && i < 3; ++i) out.push_back("Ligne " + std::to_string(i + 1) + " : " + p.lines[i]);
    for (const auto& s : p.sheets) {
        out.push_back("Onglet " + s.name + " : " + plural(s.rows.size(), "ligne", "lignes") + " montr\xC3\xA9" "e(s)");
        if (!s.rows.empty()) {
            std::string first;
            for (const auto& c : s.rows.front()) first += (first.empty() ? "" : " | ") + c;
            out.push_back("  " + first);
        }
    }
    for (std::size_t i = 0; i < p.paragraphs.size() && i < 4; ++i)
        out.push_back((p.paragraphs[i].level > 0 ? std::string(static_cast<std::size_t>(p.paragraphs[i].level), '#') + " " : std::string("  ")) + p.paragraphs[i].text);
    for (std::size_t i = 0; i < p.entries.size() && i < 4; ++i) out.push_back("Entr\xC3\xA9" "e : " + p.entries[i].name + " (" + sizeText(p.entries[i].size) + ")");
    if (!p.fontSamples.empty()) out.push_back("\xC3\x89" "chantillon : " + std::to_string(p.fontSamples.size()) + " lignes");
    return out;
}

} // namespace app::fileview
