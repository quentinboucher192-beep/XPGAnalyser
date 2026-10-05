// =============================================================================
//  xls/XlsmSource.cpp
// -----------------------------------------------------------------------------
//  Trois couches, de bas en haut :
//
//    1. INFLATE   le DEFLATE de la RFC 1951, ecrit ici parce qu'ajouter zlib a
//                 un projet Visual Studio coute plus cher que 200 lignes.
//    2. ZIP       le repertoire central, parce que les tailles qu'il donne sont
//                 fiables meme quand l'en-tete local est a zero (descripteur
//                 differe : Excel en met).
//    3. XML       juste assez pour `workbook.xml`, `sharedStrings.xml` et une
//                 feuille. Pas un parseur XML : un lecteur des cinq balises
//                 qu'Excel ecrit. Il refuse ce qu'il ne comprend pas au lieu de
//                 deviner.
//
//  Puis la partie qui a du sens metier : trouver la ligne d'en-tete.
// =============================================================================
#include "XlsmSource.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace xls {
namespace {

// =============================================================================
// 1. INFLATE  (RFC 1951)
// =============================================================================

class BitReader {
public:
    BitReader(const unsigned char* p, std::size_t n) noexcept : p_(p), n_(n) {}

    bool bits(unsigned count, std::uint32_t& out) noexcept {
        std::uint32_t v = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (bit_ >= 8) { ++byte_; bit_ = 0; }
            if (byte_ >= n_) return false;
            v |= static_cast<std::uint32_t>((p_[byte_] >> bit_) & 1u) << i;
            ++bit_;
        }
        out = v;
        return true;
    }

    void align() noexcept { if (bit_ > 0) { ++byte_; bit_ = 0; } }

    // Lecture directe, pour un bloc non compresse.
    bool copy(std::size_t count, std::string& out) {
        if (byte_ + count > n_) return false;
        out.append(reinterpret_cast<const char*>(p_ + byte_), count);
        byte_ += count;
        return true;
    }
    bool raw16(std::uint32_t& out) noexcept {
        if (byte_ + 2 > n_) return false;
        out = static_cast<std::uint32_t>(p_[byte_]) |
              (static_cast<std::uint32_t>(p_[byte_ + 1]) << 8);
        byte_ += 2;
        return true;
    }

private:
    const unsigned char* p_;
    std::size_t          n_;
    std::size_t          byte_{0};
    unsigned             bit_{0};
};

// Arbre de Huffman canonique. L'algorithme est celui de `puff` (zlib) : compter
// les longueurs, en deduire les premiers codes, descendre bit a bit. Lent
// compare a une table, mais un classeur fait un megaoctet et ce code s'execute
// une fois par import.
struct Huffman {
    std::array<std::uint16_t, 16> count{};
    std::vector<std::uint16_t>    symbol;

    bool build(const unsigned char* lengths, std::size_t n) {
        count.fill(0);
        for (std::size_t i = 0; i < n; ++i) ++count[lengths[i]];
        if (count[0] == n) return true;          // aucun code : legal (arbre vide)

        int left = 1;
        for (int len = 1; len <= 15; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) return false;          // sur-souscrit : flux corrompu
        }
        std::array<std::uint16_t, 16> offs{};
        for (int len = 1; len < 15; ++len)
            offs[static_cast<std::size_t>(len) + 1] =
                static_cast<std::uint16_t>(offs[static_cast<std::size_t>(len)] + count[static_cast<std::size_t>(len)]);

        symbol.assign(n, 0);
        for (std::size_t i = 0; i < n; ++i)
            if (lengths[i] != 0) symbol[offs[lengths[i]]++] = static_cast<std::uint16_t>(i);
        return true;
    }

    [[nodiscard]] int decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            std::uint32_t b = 0;
            if (!br.bits(1, b)) return -1;
            code |= static_cast<int>(b);
            const int cnt = count[static_cast<std::size_t>(len)];
            if (code - cnt < first) return symbol[static_cast<std::size_t>(index + (code - first))];
            index += cnt;
            first += cnt;
            first <<= 1;
            code  <<= 1;
        }
        return -1;
    }
};

constexpr std::array<std::uint16_t, 29> kLenBase = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<std::uint8_t, 29> kLenExtra = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<std::uint16_t, 30> kDistBase = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, 30> kDistExtra = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflateBlockBody(BitReader& br, const Huffman& lit, const Huffman& dist,
                      std::string& out, std::size_t cap) {
    for (;;) {
        const int sym = lit.decode(br);
        if (sym < 0) return false;
        if (sym < 256) {
            if (out.size() >= cap) return false;
            out.push_back(static_cast<char>(sym));
            continue;
        }
        if (sym == 256) return true;                       // fin du bloc
        const std::size_t li = static_cast<std::size_t>(sym) - 257;
        if (li >= kLenBase.size()) return false;
        std::uint32_t extra = 0;
        if (!br.bits(kLenExtra[li], extra)) return false;
        const std::size_t length = kLenBase[li] + extra;

        const int dsym = dist.decode(br);
        if (dsym < 0 || static_cast<std::size_t>(dsym) >= kDistBase.size()) return false;
        if (!br.bits(kDistExtra[static_cast<std::size_t>(dsym)], extra)) return false;
        const std::size_t distance = kDistBase[static_cast<std::size_t>(dsym)] + extra;
        if (distance > out.size()) return false;           // pointe avant le debut
        if (out.size() + length > cap) return false;

        // Recopie octet par octet : les distances inferieures a la longueur
        // sont legales et doivent lire ce qu'on vient d'ecrire.
        std::size_t src = out.size() - distance;
        for (std::size_t i = 0; i < length; ++i) out.push_back(out[src + i]);
    }
}

bool inflateFixed(BitReader& br, std::string& out, std::size_t cap) {
    static Huffman lit, dist;
    static bool built = false;
    if (!built) {
        std::array<unsigned char, 288> l{};
        for (std::size_t i = 0;   i < 144; ++i) l[i] = 8;
        for (std::size_t i = 144; i < 256; ++i) l[i] = 9;
        for (std::size_t i = 256; i < 280; ++i) l[i] = 7;
        for (std::size_t i = 280; i < 288; ++i) l[i] = 8;
        std::array<unsigned char, 30> d{};
        d.fill(5);
        if (!lit.build(l.data(), l.size()) || !dist.build(d.data(), d.size())) return false;
        built = true;
    }
    return inflateBlockBody(br, lit, dist, out, cap);
}

bool inflateDynamic(BitReader& br, std::string& out, std::size_t cap) {
    static constexpr std::array<std::uint8_t, 19> kOrder = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    std::uint32_t hlit = 0, hdist = 0, hclen = 0;
    if (!br.bits(5, hlit) || !br.bits(5, hdist) || !br.bits(4, hclen)) return false;
    const std::size_t nlen  = hlit + 257;
    const std::size_t ndist = hdist + 1;
    const std::size_t ncode = hclen + 4;
    if (nlen > 286 || ndist > 30) return false;

    std::array<unsigned char, 19> clen{};
    for (std::size_t i = 0; i < ncode; ++i) {
        std::uint32_t v = 0;
        if (!br.bits(3, v)) return false;
        clen[kOrder[i]] = static_cast<unsigned char>(v);
    }
    Huffman code;
    if (!code.build(clen.data(), clen.size())) return false;

    std::array<unsigned char, 316> lengths{};
    std::size_t i = 0;
    while (i < nlen + ndist) {
        const int sym = code.decode(br);
        if (sym < 0) return false;
        if (sym < 16) {
            lengths[i++] = static_cast<unsigned char>(sym);
        } else {
            unsigned char value = 0;
            std::uint32_t repeat = 0;
            if (sym == 16) {
                if (i == 0) return false;
                value = lengths[i - 1];
                if (!br.bits(2, repeat)) return false;
                repeat += 3;
            } else if (sym == 17) {
                if (!br.bits(3, repeat)) return false;
                repeat += 3;
            } else {
                if (!br.bits(7, repeat)) return false;
                repeat += 11;
            }
            if (i + repeat > nlen + ndist) return false;
            for (std::uint32_t k = 0; k < repeat; ++k) lengths[i++] = value;
        }
    }
    if (lengths[256] == 0) return false;                   // pas de code de fin

    Huffman lit, dist;
    if (!lit.build(lengths.data(), nlen)) return false;
    if (!dist.build(lengths.data() + nlen, ndist)) return false;
    return inflateBlockBody(br, lit, dist, out, cap);
}

// DEFLATE brut, sans en-tete zlib : c'est ce que contient un zip.
bool inflateRaw(const unsigned char* src, std::size_t n, std::size_t expected, std::string& out) {
    BitReader br(src, n);
    out.clear();
    out.reserve(expected);
    const std::size_t cap = expected != 0 ? expected : (n * 1024 + 4096);

    for (;;) {
        std::uint32_t last = 0, type = 0;
        if (!br.bits(1, last) || !br.bits(2, type)) return false;
        if (type == 0) {
            br.align();
            std::uint32_t len = 0, nlen = 0;
            if (!br.raw16(len) || !br.raw16(nlen)) return false;
            if ((len ^ 0xFFFFu) != nlen) return false;
            if (out.size() + len > cap) return false;
            if (!br.copy(len, out)) return false;
        } else if (type == 1) {
            if (!inflateFixed(br, out, cap)) return false;
        } else if (type == 2) {
            if (!inflateDynamic(br, out, cap)) return false;
        } else {
            return false;                                   // type 3 : reserve
        }
        if (last) break;
    }
    return expected == 0 || out.size() == expected;
}

// =============================================================================
// 2. ZIP
// =============================================================================

std::uint32_t le32(const unsigned char* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint16_t le16(const unsigned char* p) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(p[0]) |
                                      (static_cast<std::uint32_t>(p[1]) << 8));
}

struct ZipEntry {
    std::string   name;
    std::uint32_t method{0};
    std::uint32_t compressed{0};
    std::uint32_t uncompressed{0};
    std::uint32_t localOffset{0};
};

class Zip {
public:
    [[nodiscard]] bool load(const std::string& path, std::string& why) {
        std::ifstream in(path, std::ios::binary);
        if (!in) { why = "fichier illisible"; return false; }
        blob_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (blob_.size() < 22) { why = "fichier trop court pour un classeur"; return false; }

        const auto* base = reinterpret_cast<const unsigned char*>(blob_.data());

        // Le commentaire final peut faire 64 Ko ; on cherche la signature de
        // fin de repertoire central en remontant.
        std::size_t eocd = 0;
        bool        found = false;
        const std::size_t limit = std::min<std::size_t>(blob_.size(), 66000);
        for (std::size_t back = 22; back <= limit; ++back) {
            const std::size_t at = blob_.size() - back;
            if (le32(base + at) == 0x06054b50u) { eocd = at; found = true; break; }
        }
        if (!found) { why = "ce n'est pas un classeur (fin de zip absente)"; return false; }

        const std::uint32_t count  = le16(base + eocd + 10);
        const std::uint32_t cdSize = le32(base + eocd + 12);
        const std::uint32_t cdAt   = le32(base + eocd + 16);
        if (cdAt == 0xFFFFFFFFu || cdSize == 0xFFFFFFFFu) {
            why = "classeur au format zip64, non pris en charge";
            return false;
        }
        if (static_cast<std::size_t>(cdAt) + cdSize > blob_.size()) {
            why = "repertoire central hors du fichier";
            return false;
        }

        std::size_t at = cdAt;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (at + 46 > blob_.size() || le32(base + at) != 0x02014b50u) {
                why = "entree de repertoire central invalide";
                return false;
            }
            ZipEntry e;
            e.method       = le16(base + at + 10);
            e.compressed   = le32(base + at + 20);
            e.uncompressed = le32(base + at + 24);
            const std::uint32_t nameLen  = le16(base + at + 28);
            const std::uint32_t extraLen = le16(base + at + 30);
            const std::uint32_t cmtLen   = le16(base + at + 32);
            e.localOffset = le32(base + at + 42);
            if (at + 46 + nameLen > blob_.size()) { why = "nom d'entree tronque"; return false; }
            e.name.assign(blob_.data() + at + 46, nameLen);
            entries_.push_back(std::move(e));
            at += 46u + nameLen + extraLen + cmtLen;
        }
        return true;
    }

    // Rend false quand l'entree n'existe pas OU qu'elle ne se decompresse pas.
    // Les deux sont des erreurs ici : on ne lit que des fichiers qu'Excel a
    // ecrits, et un .xlsm dont `xl/workbook.xml` est illisible est casse.
    [[nodiscard]] bool read(std::string_view name, std::string& out, std::string& why) const {
        const auto it = std::find_if(entries_.begin(), entries_.end(),
                                     [&](const ZipEntry& e) { return e.name == name; });
        if (it == entries_.end()) { why = "entree absente : " + std::string(name); return false; }

        const auto* base = reinterpret_cast<const unsigned char*>(blob_.data());
        const std::size_t lo = it->localOffset;
        if (lo + 30 > blob_.size() || le32(base + lo) != 0x04034b50u) {
            why = "en-tete local invalide : " + it->name;
            return false;
        }
        // Les longueurs de l'en-tete LOCAL, pas celles du repertoire central :
        // le champ `extra` differe entre les deux, et c'est lui qui decale le
        // debut des donnees.
        const std::size_t nameLen  = le16(base + lo + 26);
        const std::size_t extraLen = le16(base + lo + 28);
        const std::size_t data     = lo + 30 + nameLen + extraLen;
        if (data + it->compressed > blob_.size()) { why = "donnees tronquees : " + it->name; return false; }

        if (it->method == 0) {                              // stocke tel quel
            out.assign(blob_.data() + data, it->uncompressed);
            return true;
        }
        if (it->method != 8) {
            why = "compression inconnue dans " + it->name;
            return false;
        }
        if (!inflateRaw(base + data, it->compressed, it->uncompressed, out)) {
            why = "decompression impossible : " + it->name;
            return false;
        }
        return true;
    }

    [[nodiscard]] bool has(std::string_view name) const {
        return std::any_of(entries_.begin(), entries_.end(),
                           [&](const ZipEntry& e) { return e.name == name; });
    }

private:
    std::string           blob_;
    std::vector<ZipEntry> entries_;
};

// =============================================================================
// 3. XML — le strict necessaire
// =============================================================================

std::string decodeEntities(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size();) {
        // XML 1.0 §2.11 : un parseur DOIT rendre CRLF et CR seul comme un LF.
        // Excel ecrit CRLF dans une cellule multiligne (`M340\r\nvoie 4\r\n...`),
        // et sans cette normalisation le texte compare inegal a lui-meme selon
        // qu'il vient du classeur ou d'ailleurs.
        if (in[i] == '\r') {
            out.push_back('\n');
            i += (i + 1 < in.size() && in[i + 1] == '\n') ? 2 : 1;
            continue;
        }
        if (in[i] != '&') { out.push_back(in[i++]); continue; }
        const std::size_t semi = in.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10) { out.push_back(in[i++]); continue; }
        const std::string_view name = in.substr(i + 1, semi - i - 1);
        if      (name == "amp")  out.push_back('&');
        else if (name == "lt")   out.push_back('<');
        else if (name == "gt")   out.push_back('>');
        else if (name == "quot") out.push_back('"');
        else if (name == "apos") out.push_back('\'');
        else if (!name.empty() && name[0] == '#') {
            const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
            const unsigned long cp =
                std::strtoul(std::string(name.substr(hex ? 2 : 1)).c_str(), nullptr, hex ? 16 : 10);
            // UTF-8. Les classeurs d'Excel sont en UTF-8 et les accents y sont
            // deja encodes ; ce chemin ne sert qu'aux caracteres echappes.
            if (cp < 0x80) out.push_back(static_cast<char>(cp));
            else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        } else {
            out.append(in.substr(i, semi - i + 1));         // entite inconnue : telle quelle
            i = semi + 1;
            continue;
        }
        i = semi + 1;
    }
    return out;
}

// Valeur d'un attribut dans le texte d'une balise ouvrante.
std::string attribute(std::string_view tag, std::string_view name) {
    std::size_t at = 0;
    while ((at = tag.find(name, at)) != std::string_view::npos) {
        // Doit etre precede d'une espace et suivi de `=` : sinon `r` trouverait
        // le `r` de `ref`, et `t` le `t` de `count`.
        const bool leftOk = at > 0 && (tag[at - 1] == ' ' || tag[at - 1] == '\t');
        std::size_t after = at + name.size();
        while (after < tag.size() && tag[after] == ' ') ++after;
        if (!leftOk || after >= tag.size() || tag[after] != '=') { at += name.size(); continue; }
        ++after;
        while (after < tag.size() && tag[after] == ' ') ++after;
        if (after >= tag.size() || (tag[after] != '"' && tag[after] != '\'')) return {};
        const char quote = tag[after++];
        const std::size_t end = tag.find(quote, after);
        if (end == std::string_view::npos) return {};
        return decodeEntities(tag.substr(after, end - after));
    }
    return {};
}

// Position de la balise ouvrante `<name` a partir de `from`, en refusant les
// noms plus longs : `<t` ne doit pas accrocher `<text>`.
std::size_t findOpenTag(std::string_view xml, std::string_view name, std::size_t from) {
    for (;;) {
        const std::size_t open = xml.find(name, from);
        if (open == std::string_view::npos || open == 0 || xml[open - 1] != '<')
            { if (open == std::string_view::npos) return open; from = open + 1; continue; }
        const std::size_t after = open + name.size();
        if (after >= xml.size()) return std::string_view::npos;
        const char c = xml[after];
        if (c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\r' || c == '\n')
            return open - 1;                                // position du '<'
        from = open + 1;
    }
}

// Texte du premier element <name ...>...</name>. Les attributs comptent : Excel
// ecrit `<v xml:space="preserve">` des que la valeur se termine par une espace,
// et chercher litteralement `"<v>"` perd la cellule entiere — silencieusement,
// puisqu'une cellule absente est une cellule vide.
bool elementText(std::string_view xml, std::string_view name, std::string& out) {
    const std::size_t open = findOpenTag(xml, name, 0);
    if (open == std::string_view::npos) return false;
    const std::size_t gt = xml.find('>', open);
    if (gt == std::string_view::npos) return false;
    if (xml[gt - 1] == '/') { out.clear(); return true; }    // <v/> : present mais vide
    const std::string close = "</" + std::string(name) + ">";
    const std::size_t end = xml.find(close, gt);
    if (end == std::string_view::npos) return false;
    out = decodeEntities(xml.substr(gt + 1, end - gt - 1));
    return true;
}

// Texte de TOUS les <t> : un <si> est decoupe en plusieurs <r> quand une partie
// de la cellule est en gras, et la valeur est leur concatenation.
std::string collectText(std::string_view xml) {
    std::string out;
    std::size_t at = 0;
    for (;;) {
        const std::size_t open = findOpenTag(xml, "t", at);
        if (open == std::string_view::npos) break;
        const std::size_t gt = xml.find('>', open);
        if (gt == std::string_view::npos) break;
        if (xml[gt - 1] == '/') { at = gt + 1; continue; }    // <t/> : vide
        const std::size_t close = xml.find("</t>", gt);
        if (close == std::string_view::npos) break;
        out += decodeEntities(xml.substr(gt + 1, close - gt - 1));
        at = close + 4;
    }
    return out;
}

// ---- adresse de cellule -----------------------------------------------------
// "AB12" -> colonne 27 (0-based), ligne 11 (0-based). Rend false sur autre chose.
bool splitReference(std::string_view ref, std::size_t& column, std::size_t& line) {
    std::size_t i = 0;
    std::size_t col = 0;
    while (i < ref.size() && ref[i] >= 'A' && ref[i] <= 'Z') {
        col = col * 26 + static_cast<std::size_t>(ref[i] - 'A' + 1);
        ++i;
    }
    if (i == 0 || i >= ref.size()) return false;
    std::size_t row = 0;
    for (; i < ref.size(); ++i) {
        if (ref[i] < '0' || ref[i] > '9') return false;
        row = row * 10 + static_cast<std::size_t>(ref[i] - '0');
    }
    if (row == 0) return false;
    column = col - 1;
    line   = row - 1;
    return true;
}

// ---- valeur d'une cellule, rendue en texte ----------------------------------
// Deux regles qui viennent du contrat et pas du format :
//   - un entier stocke en REAL sort `16`, pas `16.0` : les macros font
//     STRING_TO_INT dessus ;
//   - une cellule vide rend la chaine vide, jamais `0` ni `NULL`.
// ---- dates ------------------------------------------------------------------
// Excel ne stocke pas de date : il stocke `46277` et un format d'affichage.
// Rendre le nombre serait rendre une valeur fausse a l'ecran d'une macro, et
// personne ne reconnait `46277`. On lit donc `styles.xml` pour savoir quelles
// cellules sont des dates, et on rend ce qu'Excel affiche.
struct DateStyles {
    std::vector<char> isDate;      // indexe par l'attribut `s` de la cellule
    bool              epoch1904{false};
};

// Jour civil depuis le 1er janvier 1970 (algorithme de Howard Hinnant).
void civilFromDays(long long z, int& year, unsigned& month, unsigned& day) {
    z += 719468;
    const long long  era = (z >= 0 ? z : z - 146096) / 146097;
    const auto       doe = static_cast<unsigned long long>(z - era * 146097);
    const auto       yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long  y   = static_cast<long long>(yoe) + era * 400;
    const auto       doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const auto       mp  = (5 * doy + 2) / 153;
    day   = static_cast<unsigned>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<unsigned>(mp < 10 ? mp + 3 : mp - 9);
    year  = static_cast<int>(y + (month <= 2 ? 1 : 0));
}

std::string renderDate(double serial, bool epoch1904) {
    double whole = std::floor(serial);
    double frac  = serial - whole;
    auto   days  = static_cast<long long>(whole);

    // Le bogue de 1900 : Excel croit que 1900 etait bissextile, donc le serial
    // 60 designe un 29 fevrier qui n'a jamais existe. Au-dessous de 60 les
    // dates sont decalees d'un jour.
    if (!epoch1904 && days < 60) ++days;
    days += epoch1904 ? -24107 : -25569;

    // Arrondi a la seconde, en reportant la retenue sur le jour : 23:59:59,7
    // ne doit pas devenir 24:00:00.
    auto seconds = static_cast<long long>(std::floor(frac * 86400.0 + 0.5));
    if (seconds >= 86400) { seconds -= 86400; ++days; }

    int      y = 0;
    unsigned m = 0, d = 0;
    civilFromDays(days, y, m, d);

    char buf[40];
    if (seconds == 0)
        std::snprintf(buf, sizeof buf, "%02u/%02u/%04d", d, m, y);
    else
        std::snprintf(buf, sizeof buf, "%02u/%02u/%04d %02lld:%02lld:%02lld", d, m, y,
                      seconds / 3600, (seconds / 60) % 60, seconds % 60);
    return buf;
}

// Un code de format est une date s'il contient `y`, `d` ou `h` HORS des parties
// litterales : `"jours"#,##0` n'est pas une date, `[$-40C]jj/mm/aaaa` en est une.
bool formatCodeIsDate(std::string_view code) {
    bool inQuote = false, inBracket = false;
    for (std::size_t i = 0; i < code.size(); ++i) {
        const char c = code[i];
        if (c == '\\') { ++i; continue; }
        if (c == '"') { inQuote = !inQuote; continue; }
        if (!inQuote && c == '[') { inBracket = true; continue; }
        if (!inQuote && c == ']') { inBracket = false; continue; }
        if (inQuote || inBracket) continue;
        const char l = static_cast<char>(c | 0x20);
        if (l == 'y' || l == 'd' || l == 'h') return true;
    }
    return false;
}

bool builtinIsDate(long id) {
    return (id >= 14 && id <= 22) || (id >= 45 && id <= 47);
}

DateStyles parseStyles(std::string_view xml, bool epoch1904) {
    DateStyles out;
    out.epoch1904 = epoch1904;

    std::vector<std::pair<long, bool>> custom;
    std::size_t at = 0;
    for (;;) {
        const std::size_t open = findOpenTag(xml, "numFmt", at);
        if (open == std::string_view::npos) break;
        const std::size_t gt = xml.find('>', open);
        if (gt == std::string_view::npos) break;
        const std::string_view tag = xml.substr(open, gt - open);
        const std::string id   = attribute(tag, "numFmtId");
        const std::string code = attribute(tag, "formatCode");
        if (!id.empty())
            custom.emplace_back(std::strtol(id.c_str(), nullptr, 10), formatCodeIsDate(code));
        at = gt + 1;
    }

    // `<cellXfs>` seulement : `<cellStyleXfs>` le precede et ne decrit pas les
    // cellules. Prendre les deux decalerait tous les index d'un cran.
    const std::size_t begin = xml.find("<cellXfs");
    if (begin == std::string_view::npos) return out;
    const std::size_t end = xml.find("</cellXfs>", begin);
    const std::string_view body = xml.substr(begin, (end == std::string_view::npos ? xml.size() : end) - begin);

    at = body.find('>');
    if (at == std::string_view::npos) return out;
    for (;;) {
        const std::size_t open = findOpenTag(body, "xf", at);
        if (open == std::string_view::npos) break;
        const std::size_t gt = body.find('>', open);
        if (gt == std::string_view::npos) break;
        const std::string id = attribute(body.substr(open, gt - open), "numFmtId");
        const long n = id.empty() ? 0 : std::strtol(id.c_str(), nullptr, 10);
        bool isDate = builtinIsDate(n);
        for (const auto& c : custom)
            if (c.first == n) { isDate = c.second; break; }
        out.isDate.push_back(isDate ? 1 : 0);
        at = gt + 1;
    }
    return out;
}

std::string renderNumber(std::string_view raw) {
    // Un entier ecrit sans decimale sort tel quel : ne pas le faire passer par
    // un double evite d'inventer une perte de precision la ou il n'y en a pas.
    if (raw.find_first_of(".eE") == std::string_view::npos) return std::string(raw);

    const std::string copy(raw);
    char*             end = nullptr;
    const double      d   = std::strtod(copy.c_str(), &end);
    if (end == copy.c_str()) return copy;                   // pas un nombre : tel quel

    // `%.15g` suffit a lui seul : il rend `16` pour 16.0 (ce que les macros
    // attendent avant STRING_TO_INT), `0.5` pour 0.5, et `16` encore pour le
    // 16.000000000000002 que rend parfois une formule. Une branche separee
    // "si c'est un entier" a ete ecrite puis retiree : une mutation a montre
    // qu'elle ne changeait aucune sortie, et une branche que rien ne distingue
    // est une branche que rien ne verifie.
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.15g", d);
    return buf;
}

} // namespace

// =============================================================================
// Lecture d'une feuille
// =============================================================================
namespace {

struct SheetXml {
    std::vector<std::vector<std::string>> grid;
    std::vector<std::uint32_t>            mergedLines;
};

SheetXml parseSheet(std::string_view xml, const std::vector<std::string>& shared,
                    const DateStyles& styles) {
    SheetXml out;

    // --- les fusions d'abord : elles servent a reconnaitre le bandeau -------
    {
        std::size_t at = 0;
        for (;;) {
            const std::size_t open = findOpenTag(xml, "mergeCell", at);
            if (open == std::string_view::npos) break;
            const std::size_t gt = xml.find('>', open);
            if (gt == std::string_view::npos) break;
            const std::string ref = attribute(xml.substr(open, gt - open), "ref");
            const std::size_t colon = ref.find(':');
            if (colon != std::string::npos) {
                std::size_t c1 = 0, l1 = 0, c2 = 0, l2 = 0;
                if (splitReference(std::string_view(ref).substr(0, colon), c1, l1) &&
                    splitReference(std::string_view(ref).substr(colon + 1), c2, l2)) {
                    for (std::size_t l = std::min(l1, l2); l <= std::max(l1, l2); ++l)
                        out.mergedLines.push_back(static_cast<std::uint32_t>(l));
                }
            }
            at = gt + 1;
        }
        std::sort(out.mergedLines.begin(), out.mergedLines.end());
        out.mergedLines.erase(std::unique(out.mergedLines.begin(), out.mergedLines.end()),
                              out.mergedLines.end());
    }

    // --- les cellules -------------------------------------------------------
    std::size_t at = 0;
    std::size_t implicitLine = 0;
    for (;;) {
        const std::size_t rowOpen = findOpenTag(xml, "row", at);
        if (rowOpen == std::string_view::npos) break;
        const std::size_t rowGt = xml.find('>', rowOpen);
        if (rowGt == std::string_view::npos) break;
        const std::string_view rowTag = xml.substr(rowOpen, rowGt - rowOpen);
        const std::string      rAttr  = attribute(rowTag, "r");

        std::size_t line = implicitLine;
        if (!rAttr.empty()) {
            const long n = std::strtol(rAttr.c_str(), nullptr, 10);
            if (n > 0) line = static_cast<std::size_t>(n - 1);
        }
        implicitLine = line + 1;

        if (xml[rowGt - 1] == '/') { at = rowGt + 1; continue; }   // <row .../> vide
        const std::size_t rowEnd = xml.find("</row>", rowGt);
        const std::size_t stop   = rowEnd == std::string_view::npos ? xml.size() : rowEnd;

        std::size_t ci = rowGt + 1;
        while (ci < stop) {
            const std::size_t cOpen = findOpenTag(xml, "c", ci);
            if (cOpen == std::string_view::npos || cOpen >= stop) break;
            const std::size_t cGt = xml.find('>', cOpen);
            if (cGt == std::string_view::npos) break;
            const std::string_view cTag = xml.substr(cOpen, cGt - cOpen);

            std::size_t column = 0, dummy = 0;
            const std::string ref = attribute(cTag, "r");
            if (!splitReference(ref, column, dummy)) { ci = cGt + 1; continue; }

            std::string value;
            if (xml[cGt - 1] != '/') {
                const std::size_t cEnd = xml.find("</c>", cGt);
                const std::size_t body = cEnd == std::string_view::npos ? stop : cEnd;
                const std::string_view inner = xml.substr(cGt + 1, body - cGt - 1);
                const std::string      type  = attribute(cTag, "t");

                std::string raw;
                if (type == "inlineStr") {
                    value = collectText(inner);
                } else if (type == "s") {
                    if (elementText(inner, "v", raw)) {
                        const long idx = std::strtol(raw.c_str(), nullptr, 10);
                        if (idx >= 0 && static_cast<std::size_t>(idx) < shared.size())
                            value = shared[static_cast<std::size_t>(idx)];
                    }
                } else if (elementText(inner, "v", raw)) {
                    if (type == "str" || type == "e") {
                        value = raw;
                    } else if (type == "b") {
                        value = raw == "1" ? "1" : "0";
                    } else {
                        const std::string sAttr = attribute(cTag, "s");
                        const long style = sAttr.empty() ? -1 : std::strtol(sAttr.c_str(), nullptr, 10);
                        const bool isDate =
                            style >= 0 && static_cast<std::size_t>(style) < styles.isDate.size() &&
                            styles.isDate[static_cast<std::size_t>(style)] != 0;
                        if (isDate) {
                            char* fin = nullptr;
                            const double serial = std::strtod(raw.c_str(), &fin);
                            value = (fin != raw.c_str() && serial > 0.0)
                                        ? renderDate(serial, styles.epoch1904)
                                        : renderNumber(raw);
                        } else {
                            value = renderNumber(raw);
                        }
                    }
                }
                ci = (cEnd == std::string_view::npos ? stop : cEnd + 4);
            } else {
                ci = cGt + 1;
            }

            if (!value.empty()) {
                if (out.grid.size() <= line) out.grid.resize(line + 1);
                auto& row = out.grid[line];
                if (row.size() <= column) row.resize(column + 1);
                row[column] = std::move(value);
            }
        }
        at = (rowEnd == std::string_view::npos ? stop : rowEnd + 6);
    }
    return out;
}

std::vector<std::string> parseSharedStrings(std::string_view xml) {
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        const std::size_t open = findOpenTag(xml, "si", at);
        if (open == std::string_view::npos) break;
        const std::size_t gt = xml.find('>', open);
        if (gt == std::string_view::npos) break;
        if (xml[gt - 1] == '/') { out.emplace_back(); at = gt + 1; continue; }
        const std::size_t close = xml.find("</si>", gt);
        if (close == std::string_view::npos) break;
        out.push_back(collectText(xml.substr(gt + 1, close - gt - 1)));
        at = close + 5;
    }
    return out;
}

std::string trimmed(std::string_view s) {
    std::size_t b = 0, e = s.size();
    const auto space = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
    };
    while (b < e && space(s[b])) ++b;
    while (e > b && space(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

// La table onglet -> colonne temoin du contrat. Elle rend la recherche d'en-tete
// explicite la ou on connait l'onglet ; ailleurs on retombe sur la forme.
std::string witnessFor(std::string_view sheet) {
    struct Pair { const char* sheet; const char* column; };
    // LES ONGLETS AJOUTES POUR LES MACROS DE CONTROLE ONT LEUR TEMOIN AUSSI.
    // Sans lui, la recherche de repli prend la premiere ligne de trois libelles
    // - et le bandeau en porte quatre en ligne 3 : « Affaire », le numero,
    // « Client », son nom. L'onglet Reglages avait alors pour colonnes
    // `Affaire` et `Client`, et aucune ligne de donnees.
    // LES ONGLETS GRAFCET (ImporterGrafcet) ont pour temoin la colonne
    // Grafcet : chaque ligne dit a quel grafcet elle appartient.
    static constexpr std::array<Pair, 19> kTable = {{
        {"Cartes API",  "Nom"},
        {"ES",          "Famille"},
        {"Equipements", "Nom"},
        {"Cablage",     "Equipement"},
        {"Liens",       "Equipement"},
        {"Catalogue",   "Famille"},
        {"Entrees TOR", "Carte"},
        {"Sorties TOR", "Carte"},
        {"Entrees ANA", "Carte"},
        {"Reglages",    "Equipement"},
        {"Rotations",   "Groupe"},
        {"Paliers",     "Groupe"},
        {"Modes",       "Mode"},
        {"Interverrouillages", "Groupe"},
        {"Modbus",      "Liaison"},
        {"Grafcets",            "Grafcet"},
        {"Grafcet Etapes",      "Grafcet"},
        {"Grafcet Transitions", "Grafcet"},
        {"Grafcet Actions",     "Grafcet"},
    }};
    for (const auto& p : kTable)
        if (sheet == p.sheet) return p.column;
    return {};
}

} // namespace

// =============================================================================
// Sheet
// =============================================================================

bool Sheet::lineHasMergedCell(std::size_t line) const noexcept {
    return std::binary_search(mergedMask_.begin(), mergedMask_.end(),
                              static_cast<std::uint32_t>(line));
}

std::string Sheet::raw(std::size_t line, std::size_t column) const {
    if (line >= grid_.size()) return {};
    const auto& row = grid_[line];
    if (column >= row.size()) return {};
    return row[column];
}

std::size_t Sheet::columnIndex(std::string_view column) const noexcept {
    for (std::size_t i = 0; i < columns_.size(); ++i)
        if (columns_[i] == column) return i;
    return static_cast<std::size_t>(-1);
}

bool Sheet::hasColumn(std::string_view column) const noexcept {
    return columnIndex(column) != static_cast<std::size_t>(-1);
}

std::string Sheet::cell(std::size_t row, std::string_view column) const {
    const std::size_t c = columnIndex(column);
    if (c == static_cast<std::size_t>(-1)) return {};
    return cell(row, c);
}

std::string Sheet::cell(std::size_t row, std::size_t column) const {
    if (row >= rows_.size()) return {};
    return raw(rows_[row], column);
}

// -----------------------------------------------------------------------------
// LA RECHERCHE DE L'EN-TETE
//
// Ecrire "en-tetes = ligne 7" marcherait aujourd'hui et casserait le jour ou
// quelqu'un insere une ligne dans le bandeau — silencieusement, en important un
// programme plausible et faux. Deux regles, dans cet ordre :
//
//   1. si l'onglet est connu, la premiere des quinze premieres lignes qui
//      contient sa colonne temoin ;
//   2. sinon, la premiere ligne portant au moins trois cellules non vides et
//      AUCUNE cellule fusionnee — le bandeau et la bande de groupes ont les
//      leurs fusionnees, les en-tetes non.
//
// Puis la ligne d'exemples (`libre`, `0..63`, `O/N`) : elle suit l'en-tete
// quand une bande de groupes le precede. C'est un signe de forme, pas une
// liste de mots a reconnaitre — un equipement peut s'appeler `liste`.
//
// Et la fin des donnees : la premiere ligne dont la colonne temoin est vide.
// Ces classeurs portent des notes en clair vingt lignes sous le tableau ; lues
// comme des donnees elles fabriquent un equipement nomme "Une ligne par
// report...".
// -----------------------------------------------------------------------------
void Sheet::buildTable() {
    columns_.clear();
    rows_.clear();
    headerLine_ = firstDataLine_ = 0;
    hintSkipped_ = false;
    ignoredAfter_ = 0;

    const std::size_t limit = std::min<std::size_t>(grid_.size(), 15);
    std::size_t header = static_cast<std::size_t>(-1);

    witness_ = witnessFor(name_);
    if (!witness_.empty()) {
        for (std::size_t l = 0; l < limit && header == static_cast<std::size_t>(-1); ++l) {
            // La bande de groupes porte des libelles comme `Equipement` ou
            // `Voie`, et l'un d'eux peut etre le mot temoin. Elle est fusionnee,
            // un en-tete ne l'est pas : c'est ce qui les separe. Sans ce saut,
            // l'onglet Cablage prenait sa bande de groupes pour ses en-tetes et
            // lisait ses propres libelles comme une premiere ligne de donnees.
            if (lineHasMergedCell(l)) continue;
            for (const auto& c : grid_[l])
                if (trimmed(c) == witness_) { header = l; break; }
        }
    }
    if (header == static_cast<std::size_t>(-1)) {
        for (std::size_t l = 0; l < limit; ++l) {
            if (lineHasMergedCell(l)) continue;
            std::size_t filled = 0;
            bool        numeric = false;
            for (const auto& c : grid_[l]) {
                const std::string t = trimmed(c);
                if (t.empty()) continue;
                ++filled;
                // Un en-tete est fait de libelles. Une ligne qui porte un
                // nombre est une ligne de donnees : sans ce refus, un onglet
                // sans bandeau prend sa premiere ligne de donnees pour ses
                // en-tetes, et les colonnes s'appellent alors `Armoire A
                // demande basculement`.
                char* stop = nullptr;
                std::strtod(t.c_str(), &stop);
                if (stop != nullptr && *stop == '\0') numeric = true;
            }
            if (!numeric && filled >= 3) { header = l; break; }
        }
    }
    if (header == static_cast<std::size_t>(-1)) return;      // pas un tableau (Config)

    headerLine_ = static_cast<std::uint32_t>(header + 1);
    for (const auto& c : grid_[header]) columns_.push_back(trimmed(c));
    while (!columns_.empty() && columns_.back().empty()) columns_.pop_back();

    // La colonne temoin decide ou les donnees s'arretent. Sans table, c'est la
    // premiere colonne nommee.
    std::size_t witnessCol = witness_.empty() ? static_cast<std::size_t>(-1)
                                              : columnIndex(witness_);
    if (witnessCol == static_cast<std::size_t>(-1)) {
        for (std::size_t i = 0; i < columns_.size(); ++i)
            if (!columns_[i].empty()) { witnessCol = i; witness_ = columns_[i]; break; }
    }
    if (witnessCol == static_cast<std::size_t>(-1)) return;

    std::size_t first = header + 1;
    if (header > 0 && lineHasMergedCell(header - 1) && first < grid_.size()) {
        ++first;                                             // la ligne d'exemples
        hintSkipped_ = true;
    }
    firstDataLine_ = static_cast<std::uint32_t>(first + 1);

    for (std::size_t l = first; l < grid_.size(); ++l) {
        if (trimmed(raw(l, witnessCol)).empty()) {
            ignoredAfter_ = grid_.size() - l;
            break;
        }
        rows_.push_back(l);
    }
}

// =============================================================================
// Workbook
// =============================================================================

core::Result<Workbook> Workbook::open(const std::string& path) {
    Zip         zip;
    std::string why;
    if (!zip.load(path, why))
        return core::fail(core::ErrorCode::FileUnreadable, why, path);

    std::string workbookXml;
    if (!zip.read("xl/workbook.xml", workbookXml, why))
        return core::fail(core::ErrorCode::XmlMalformed, why, path);

    std::string relsXml;
    if (!zip.read("xl/_rels/workbook.xml.rels", relsXml, why))
        return core::fail(core::ErrorCode::XmlMalformed, why, path);

    // rId -> cible. Les cibles sont relatives a `xl/`, parfois absolues.
    std::vector<std::pair<std::string, std::string>> targets;
    {
        std::size_t at = 0;
        for (;;) {
            const std::size_t open = findOpenTag(relsXml, "Relationship", at);
            if (open == std::string_view::npos) break;
            const std::size_t gt = relsXml.find('>', open);
            if (gt == std::string::npos) break;
            const std::string_view tag(relsXml.data() + open, gt - open);
            std::string id     = attribute(tag, "Id");
            std::string target = attribute(tag, "Target");
            if (!id.empty() && !target.empty()) {
                if (target.rfind("/xl/", 0) == 0)      target = target.substr(1);
                else if (target.rfind("/", 0) == 0)    target = target.substr(1);
                else if (target.rfind("xl/", 0) != 0)  target = "xl/" + target;
                targets.emplace_back(std::move(id), std::move(target));
            }
            at = gt + 1;
        }
    }

    // Les styles disent quelles cellules sont des dates. Leur absence n'est pas
    // une erreur : il n'y a alors simplement pas de date.
    DateStyles styles;
    {
        const bool epoch1904 = attribute(workbookXml, "date1904") == "1" ||
                               attribute(workbookXml, "date1904") == "true";
        std::string s;
        if (zip.has("xl/styles.xml") && zip.read("xl/styles.xml", s, why))
            styles = parseStyles(s, epoch1904);
        else
            styles.epoch1904 = epoch1904;
    }

    std::vector<std::string> shared;
    if (zip.has("xl/sharedStrings.xml")) {
        std::string s;
        if (!zip.read("xl/sharedStrings.xml", s, why))
            return core::fail(core::ErrorCode::XmlMalformed, why, path);
        shared = parseSharedStrings(s);
    }

    Workbook wb;
    wb.path_ = path;

    std::size_t at = 0;
    for (;;) {
        const std::size_t open = findOpenTag(workbookXml, "sheet", at);
        if (open == std::string_view::npos) break;
        const std::size_t gt = workbookXml.find('>', open);
        if (gt == std::string::npos) break;
        const std::string_view tag(workbookXml.data() + open, gt - open);
        const std::string name = attribute(tag, "name");
        std::string rid = attribute(tag, "r:id");
        if (rid.empty()) rid = attribute(tag, "id");
        at = gt + 1;
        if (name.empty() || rid.empty()) continue;

        const auto t = std::find_if(targets.begin(), targets.end(),
                                    [&](const auto& p) { return p.first == rid; });
        if (t == targets.end() || !zip.has(t->second)) continue;

        std::string sheetXml;
        if (!zip.read(t->second, sheetXml, why))
            return core::fail(core::ErrorCode::XmlMalformed, why, path);

        SheetXml parsed = parseSheet(sheetXml, shared, styles);

        Sheet s;
        s.name_       = name;
        s.grid_       = std::move(parsed.grid);
        s.mergedMask_ = std::move(parsed.mergedLines);
        s.buildTable();
        wb.sheets_.push_back(std::move(s));
    }

    if (wb.sheets_.empty())
        return core::fail(core::ErrorCode::IncompleteProject,
                          "aucun onglet lisible dans le classeur", path);
    return wb;
}

std::vector<std::string> Workbook::sheetNames() const {
    std::vector<std::string> out;
    out.reserve(sheets_.size());
    for (const auto& s : sheets_) out.push_back(s.name());
    return out;
}

const Sheet* Workbook::sheet(std::string_view name) const {
    for (const auto& s : sheets_)
        if (s.name() == name) return &s;
    return nullptr;
}

// La valeur est la PREMIERE CELLULE NON VIDE A DROITE de la cle, et non la
// colonne C. Le contrat disait "cle en B, valeur en C" parce que c'est ce que
// fait `automation.xlsm` ; d'autres classeurs de la meme famille mettent la cle
// en A. Chercher a droite marche dans les deux, et saute la colonne de
// commentaire qui suit la valeur.
std::string Workbook::setting(std::string_view key) const {
    const Sheet* cfg = sheet("Config");
    if (cfg == nullptr) return {};
    for (std::size_t l = 0; l < cfg->rawLineCount(); ++l) {
        // Une ligne de formulaire est etroite : la cle, sa valeur, parfois un
        // commentaire. Une ligne large est l'en-tete ou une ligne d'un tableau
        // pose sur le meme onglet, et `Automate` y designe une colonne, pas un
        // reglage : sa "valeur a droite" serait le libelle de la colonne
        // suivante.
        std::size_t filled = 0;
        for (std::size_t c = 0; c < 24; ++c)
            if (!trimmed(cfg->raw(l, c)).empty()) ++filled;
        if (filled >= 5) continue;

        for (std::size_t c = 0; c < 16; ++c) {
            if (trimmed(cfg->raw(l, c)) != key) continue;
            for (std::size_t v = c + 1; v < c + 8; ++v) {
                const std::string value = trimmed(cfg->raw(l, v));
                if (!value.empty()) return value;
            }
            return {};                                       // cle presente, valeur vide
        }
    }
    return {};
}

} // namespace xls
