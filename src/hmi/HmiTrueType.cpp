// =============================================================================
//  hmi/HmiTrueType.cpp - 1.26.0 : lire, modifier et ecrire une police TrueType
// -----------------------------------------------------------------------------
//  Voir HmiTrueType.hpp. Les tables, leurs champs et leurs decalages suivent la
//  specification OpenType (tables communes a TrueType). Tout se lit avec des bornes :
//  un fichier tronque ou faux est refuse, jamais lu au-dela.
// =============================================================================
#include "HmiTrueType.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>

namespace hmi::ttf {

namespace {

// ================================================================== lecture ===
class Reader {
public:
    explicit Reader(const Bytes& b) : b_(b) {}
    [[nodiscard]] bool has(std::size_t at, std::size_t n) const noexcept { return at <= b_.size() && n <= b_.size() - at; }
    std::uint8_t u8(std::size_t at) {
        if (!has(at, 1)) { ok = false; return 0; }
        return b_[at];
    }
    std::uint16_t u16(std::size_t at) {
        if (!has(at, 2)) { ok = false; return 0; }
        return static_cast<std::uint16_t>((b_[at] << 8) | b_[at + 1]);
    }
    std::int16_t s16(std::size_t at) { return static_cast<std::int16_t>(u16(at)); }
    std::uint32_t u32(std::size_t at) {
        if (!has(at, 4)) { ok = false; return 0; }
        return (static_cast<std::uint32_t>(b_[at]) << 24) | (static_cast<std::uint32_t>(b_[at + 1]) << 16)
             | (static_cast<std::uint32_t>(b_[at + 2]) << 8) | b_[at + 3];
    }
    std::int64_t s64(std::size_t at) {
        const std::uint64_t hi = u32(at), lo = u32(at + 4);
        return static_cast<std::int64_t>((hi << 32) | lo);
    }
    double fixed(std::size_t at) { return static_cast<std::int32_t>(u32(at)) / 65536.0; }
    double f2dot14(std::size_t at) { return s16(at) / 16384.0; }
    [[nodiscard]] const Bytes& bytes() const noexcept { return b_; }
    bool ok{true};

private:
    const Bytes& b_;
};

struct Table {
    std::uint32_t offset{0}, length{0};
};

std::string tagText(std::uint32_t tag) {
    std::string t(4, ' ');
    for (int i = 0; i < 4; ++i) t[static_cast<std::size_t>(i)] = static_cast<char>((tag >> (24 - 8 * i)) & 0xFF);
    return t;
}

void appendUtf8(std::string& out, std::uint32_t c) {
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
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

std::vector<std::uint32_t> utf8Codes(std::string_view s) {
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = c;
        std::size_t n = 1;
        if (c >= 0xF0 && i + 3 < s.size()) { cp = ((c & 0x07u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu); n = 4; }
        else if (c >= 0xE0 && i + 2 < s.size()) { cp = ((c & 0x0Fu) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu); n = 3; }
        else if (c >= 0xC0 && i + 1 < s.size()) { cp = ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu); n = 2; }
        out.push_back(cp);
        i += n;
    }
    return out;
}

// Les composants a points apparies (rares) : resolus apres la lecture de tous les glyphes.
struct PendingMatch {
    int glyph, component, parentPoint, childPoint;
};

bool readGlyph(Reader& r, std::size_t at, std::size_t len, Glyph& g, int index, std::vector<PendingMatch>& pending) {
    if (len == 0) return true;
    if (!r.has(at, len) || len < 10) return false;
    const int n = r.s16(at);
    if (n >= 0) {
        std::vector<int> ends(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) ends[static_cast<std::size_t>(i)] = r.u16(at + 10 + 2 * static_cast<std::size_t>(i));
        const int count = n == 0 ? 0 : ends.back() + 1;
        if (count < 0 || count > 65535) return false;
        std::size_t p = at + 10 + 2 * static_cast<std::size_t>(n);
        const std::size_t instr = r.u16(p);
        p += 2 + instr;
        std::vector<std::uint8_t> flags;
        flags.reserve(static_cast<std::size_t>(count));
        while (static_cast<int>(flags.size()) < count) {
            if (p >= at + len) return false;
            const std::uint8_t f = r.u8(p++);
            flags.push_back(f);
            if (f & 8) {
                const int rep = r.u8(p++);
                for (int k = 0; k < rep && static_cast<int>(flags.size()) < count; ++k) flags.push_back(f);
            }
        }
        std::vector<int> xs(static_cast<std::size_t>(count)), ys(static_cast<std::size_t>(count));
        int x = 0;
        for (int i = 0; i < count; ++i) {
            const auto f = flags[static_cast<std::size_t>(i)];
            if (f & 2) { const int d = r.u8(p++); x += (f & 16) ? d : -d; }
            else if (!(f & 16)) { x += r.s16(p); p += 2; }
            xs[static_cast<std::size_t>(i)] = x;
        }
        int y = 0;
        for (int i = 0; i < count; ++i) {
            const auto f = flags[static_cast<std::size_t>(i)];
            if (f & 4) { const int d = r.u8(p++); y += (f & 32) ? d : -d; }
            else if (!(f & 32)) { y += r.s16(p); p += 2; }
            ys[static_cast<std::size_t>(i)] = y;
        }
        if (!r.ok || p > at + len + 3) return false;   // un bourrage de 0 a 3 octets est admis
        int start = 0;
        for (int e : ends) {
            if (e < start - 1 || e >= count) return false;
            Contour c;
            for (int i = start; i <= e; ++i)
                c.points.push_back({xs[static_cast<std::size_t>(i)], ys[static_cast<std::size_t>(i)], (flags[static_cast<std::size_t>(i)] & 1) != 0});
            if (!c.points.empty()) g.contours.push_back(std::move(c));
            start = e + 1;
        }
        return true;
    }
    // Un compose.
    std::size_t p = at + 10;
    for (int guard = 0; guard < 256; ++guard) {
        const std::uint16_t f = r.u16(p);
        Component c;
        c.glyph = r.u16(p + 2);
        p += 4;
        int a1 = 0, a2 = 0;
        if (f & 0x0001) { a1 = (f & 0x0002) ? r.s16(p) : r.u16(p); a2 = (f & 0x0002) ? r.s16(p + 2) : r.u16(p + 2); p += 4; }
        else {
            a1 = (f & 0x0002) ? static_cast<std::int8_t>(r.u8(p)) : r.u8(p);
            a2 = (f & 0x0002) ? static_cast<std::int8_t>(r.u8(p + 1)) : r.u8(p + 1);
            p += 2;
        }
        if (f & 0x0008) { c.xx = c.yy = r.f2dot14(p); p += 2; }
        else if (f & 0x0040) { c.xx = r.f2dot14(p); c.yy = r.f2dot14(p + 2); p += 4; }
        else if (f & 0x0080) { c.xx = r.f2dot14(p); c.xy = r.f2dot14(p + 2); c.yx = r.f2dot14(p + 4); c.yy = r.f2dot14(p + 6); p += 8; }
        c.useMyMetrics = (f & 0x0200) != 0;
        c.roundXY = (f & 0x0004) != 0;
        if (f & 0x0002) { c.dx = a1; c.dy = a2; }
        else pending.push_back({index, static_cast<int>(g.components.size()), a1, a2});
        g.components.push_back(c);
        if (!r.ok || p > at + len) return false;
        if (!(f & 0x0020)) break;
    }
    return true;
}

// ---- cmap ----
void readCmapSub(Reader& r, std::size_t at, std::vector<Glyph>& glyphs) {
    const auto put = [&glyphs](std::uint32_t code, std::uint32_t g) {
        if (g == 0 || g >= glyphs.size()) return;
        glyphs[g].codes.push_back(code);
    };
    const int format = r.u16(at);
    if (format == 4) {
        const std::size_t segX2 = r.u16(at + 6);
        const std::size_t ends = at + 14, starts = ends + segX2 + 2, deltas = starts + segX2, ranges = deltas + segX2;
        for (std::size_t s = 0; s < segX2 / 2 && r.ok; ++s) {
            const std::uint32_t e = r.u16(ends + 2 * s), b = r.u16(starts + 2 * s);
            const std::uint16_t delta = r.u16(deltas + 2 * s), ro = r.u16(ranges + 2 * s);
            if (b > e) continue;
            for (std::uint32_t c = b; c <= e && c != 0xFFFF; ++c) {
                std::uint32_t g = 0;
                if (ro == 0) g = (c + delta) & 0xFFFF;
                else {
                    const std::size_t where = ranges + 2 * s + ro + 2 * (c - b);
                    g = r.u16(where);
                    if (g != 0) g = (g + delta) & 0xFFFF;
                }
                put(c, g);
            }
        }
    } else if (format == 12) {
        const std::uint32_t n = r.u32(at + 12);
        for (std::uint32_t i = 0; i < n && r.ok && i < 100000; ++i) {
            const std::size_t p = at + 16 + 12 * static_cast<std::size_t>(i);
            const std::uint32_t b = r.u32(p), e = r.u32(p + 4), g0 = r.u32(p + 8);
            if (e < b || e - b > 0x10FFFF) continue;
            for (std::uint32_t c = b; c <= e; ++c) put(c, g0 + (c - b));
        }
    } else if (format == 6) {
        const std::uint32_t first = r.u16(at + 6), n = r.u16(at + 8);
        for (std::uint32_t i = 0; i < n && r.ok; ++i) put(first + i, r.u16(at + 10 + 2 * static_cast<std::size_t>(i)));
    } else if (format == 0) {
        for (std::uint32_t c = 0; c < 256 && r.ok; ++c) put(c, r.u8(at + 6 + c));
    }
}

void readCmap(Reader& r, const Table& t, std::vector<Glyph>& glyphs) {
    const std::size_t n = r.u16(t.offset + 2);
    // Le meilleur sous-tableau : Windows UCS-4 (3,10), Unicode complet (0,4 ou 0,6), Windows BMP (3,1), Unicode BMP (0,x),
    // symboles (3,0).
    int best = -1;
    std::size_t bestAt = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t rec = t.offset + 4 + 8 * i;
        const int pf = r.u16(rec), en = r.u16(rec + 2);
        const std::size_t at = t.offset + r.u32(rec + 4);
        const int fmt = r.u16(at);
        if (fmt != 0 && fmt != 4 && fmt != 6 && fmt != 12) continue;
        int score = 0;
        if (pf == 3 && en == 10) score = 5;
        else if (pf == 0 && (en == 4 || en == 6)) score = 4;
        else if (pf == 3 && en == 1) score = 3;
        else if (pf == 0) score = 2;
        else if (pf == 3 && en == 0) score = 1;
        if (score > best) {
            best = score;
            bestAt = at;
        }
    }
    if (best >= 0) readCmapSub(r, bestAt, glyphs);
    for (auto& g : glyphs) {
        std::sort(g.codes.begin(), g.codes.end());
        g.codes.erase(std::unique(g.codes.begin(), g.codes.end()), g.codes.end());
    }
}

// ---- name ----
std::string nameText(Reader& r, const Table& t, int id) {
    const std::size_t count = r.u16(t.offset + 2), strings = t.offset + r.u16(t.offset + 4);
    std::string mac;
    for (std::size_t i = 0; i < count && r.ok; ++i) {
        const std::size_t rec = t.offset + 6 + 12 * i;
        const int pf = r.u16(rec), en = r.u16(rec + 2), lang = r.u16(rec + 4), nid = r.u16(rec + 6);
        const std::size_t len = r.u16(rec + 8), off = r.u16(rec + 10);
        if (nid != id) continue;
        if (pf == 3 && (en == 1 || en == 10 || en == 0) && lang == 0x409) {
            std::string out;
            for (std::size_t k = 0; k + 1 < len; k += 2) {
                std::uint32_t c = r.u16(strings + off + k);
                if (c >= 0xD800 && c < 0xDC00 && k + 3 < len) {
                    const std::uint32_t lo = r.u16(strings + off + k + 2);
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    k += 2;
                }
                appendUtf8(out, c);
            }
            return out;
        }
        if (pf == 1 && en == 0 && mac.empty())
            for (std::size_t k = 0; k < len; ++k) {
                const auto c = r.u8(strings + off + k);
                if (c >= 0x20 && c < 0x7F) mac += static_cast<char>(c);
            }
    }
    return mac;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Les noms reserves : ceux que la licence nomme (<< Reserved Font Name "Carlito" >>), ceux de la licence Bitstream Vera,
// et le premier mot de la famille (une copie se distingue de sa source).
std::vector<std::string> reservedOf(const Font& f) {
    std::vector<std::string> out;
    const auto add = [&out](std::string w) {
        while (!w.empty() && (w.back() == '.' || w.back() == ',' || w.back() == '"' || w.back() == ' ')) w.pop_back();
        while (!w.empty() && (w.front() == '"' || w.front() == ' ')) w.erase(w.begin());
        if (w.size() < 3) return;
        for (const auto& o : out)
            if (lower(o) == lower(w)) return;
        out.push_back(std::move(w));
    };
    const std::string text = f.copyright + "\n" + f.license + "\n" + f.trademark;
    for (std::size_t at = text.find("Reserved Font Name"); at != std::string::npos; at = text.find("Reserved Font Name", at + 1)) {
        std::size_t p = at + 18;
        while (p < text.size() && (text[p] == ' ' || text[p] == 's' || text[p] == ':')) ++p;
        std::string w;
        if (p < text.size() && text[p] == '"') {
            const auto q = text.find('"', p + 1);
            if (q != std::string::npos) w = text.substr(p + 1, q - p - 1);
        } else {
            while (p < text.size() && text[p] != '.' && text[p] != ',' && text[p] != '\n' && text[p] != ' ') w += text[p++];
        }
        add(w);
    }
    if (lower(text).find("bitstream") != std::string::npos) {
        add("Bitstream");
        add("Vera");
    }
    std::string first;
    for (char c : f.family) {
        if (c == ' ') break;
        first += c;
    }
    add(first);
    return out;
}

// ================================================================== ecriture ===
class Writer {
public:
    void u8(std::uint32_t v) { b.push_back(static_cast<std::uint8_t>(v)); }
    void u16(std::uint32_t v) { u8(v >> 8); u8(v); }
    void s16(int v) { u16(static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(v)))); }
    void u32(std::uint32_t v) { u16(v >> 16); u16(v & 0xFFFF); }
    void s64(std::int64_t v) { u32(static_cast<std::uint32_t>(static_cast<std::uint64_t>(v) >> 32)); u32(static_cast<std::uint32_t>(v)); }
    void fixed(double v) { u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(v * 65536.0)))); }
    void f2dot14(double v) { s16(static_cast<int>(std::lround(std::clamp(v, -2.0, 1.99993896484375) * 16384.0))); }
    void tag(std::string_view t) { for (int i = 0; i < 4; ++i) u8(i < static_cast<int>(t.size()) ? static_cast<unsigned char>(t[static_cast<std::size_t>(i)]) : ' '); }
    void pad4() { while (b.size() % 4) u8(0); }
    void put16(std::size_t at, std::uint32_t v) { b[at] = static_cast<std::uint8_t>(v >> 8); b[at + 1] = static_cast<std::uint8_t>(v); }
    void put32(std::size_t at, std::uint32_t v) { put16(at, v >> 16); put16(at + 2, v & 0xFFFF); }
    Bytes b;
};

std::uint32_t checksum(const Bytes& b, std::size_t at, std::size_t len) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < len; i += 4) {
        std::uint32_t v = 0;
        for (std::size_t k = 0; k < 4; ++k) v = (v << 8) | (at + i + k < b.size() && i + k < len ? b[at + i + k] : 0u);
        sum += v;
    }
    return sum;
}

int clamp16(long v) { return static_cast<int>(std::clamp<long>(v, -32768, 32767)); }

struct Expanded {
    std::vector<Contour> contours;
    int depth{0};
};

void expand(const Font& f, int g, double xx, double xy, double yx, double yy, int dx, int dy, int depth, std::vector<Contour>& out,
            int& maxDepth) {
    if (g < 0 || g >= static_cast<int>(f.glyphs.size()) || depth > 8) return;
    maxDepth = std::max(maxDepth, depth);
    const Glyph& gl = f.glyphs[static_cast<std::size_t>(g)];
    if (!gl.composite()) {
        for (const auto& c : gl.contours) {
            Contour t;
            t.points.reserve(c.points.size());
            for (const auto& p : c.points) {
                const double x = p.x * xx + p.y * yx + dx, y = p.x * xy + p.y * yy + dy;
                t.points.push_back({static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)), p.on});
            }
            out.push_back(std::move(t));
        }
        return;
    }
    for (const auto& c : gl.components) {
        // La transformation du composant, puis celle du parent.
        const double nxx = c.xx * xx + c.xy * yx, nxy = c.xx * xy + c.xy * yy;
        const double nyx = c.yx * xx + c.yy * yx, nyy = c.yx * xy + c.yy * yy;
        const int ndx = static_cast<int>(std::lround(c.dx * xx + c.dy * yx + dx));
        const int ndy = static_cast<int>(std::lround(c.dx * xy + c.dy * yy + dy));
        expand(f, c.glyph, nxx, nxy, nyx, nyy, ndx, ndy, depth + 1, out, maxDepth);
    }
}

Box boxOf(const std::vector<Contour>& cs) {
    Box b;
    for (const auto& c : cs)
        for (const auto& p : c.points) {
            if (b.empty) {
                b = {p.x, p.y, p.x, p.y, false};
                continue;
            }
            b.xMin = std::min(b.xMin, p.x);
            b.yMin = std::min(b.yMin, p.y);
            b.xMax = std::max(b.xMax, p.x);
            b.yMax = std::max(b.yMax, p.y);
        }
    return b;
}

// Un contour a ecrire : au moins un point (un contour vide se perd).
std::vector<const Contour*> writable(const Glyph& g) {
    std::vector<const Contour*> out;
    for (const auto& c : g.contours)
        if (!c.points.empty()) out.push_back(&c);
    return out;
}

void writeSimple(Writer& w, const Glyph& g, const Box& b) {
    const auto cs = writable(g);
    w.s16(static_cast<int>(cs.size()));
    w.s16(b.xMin); w.s16(b.yMin); w.s16(b.xMax); w.s16(b.yMax);
    int end = -1;
    for (const auto* c : cs) {
        end += static_cast<int>(c->points.size());
        w.u16(static_cast<std::uint32_t>(end));
    }
    w.u16(0);   // pas d'instructions
    std::vector<std::uint8_t> flags;
    Writer xs, ys;
    int px = 0, py = 0;
    for (const auto* c : cs)
        for (const auto& p : c->points) {
            std::uint8_t f = p.on ? 1 : 0;
            const int dx = p.x - px, dy = p.y - py;
            if (dx == 0) f |= 16;
            else if (dx >= -255 && dx <= 255) { f |= 2; if (dx > 0) f |= 16; xs.u8(static_cast<std::uint32_t>(std::abs(dx))); }
            else xs.s16(dx);
            if (dy == 0) f |= 32;
            else if (dy >= -255 && dy <= 255) { f |= 4; if (dy > 0) f |= 32; ys.u8(static_cast<std::uint32_t>(std::abs(dy))); }
            else ys.s16(dy);
            flags.push_back(f);
            px = p.x;
            py = p.y;
        }
    for (std::size_t i = 0; i < flags.size();) {
        std::size_t run = 1;
        while (i + run < flags.size() && flags[i + run] == flags[i] && run < 256) ++run;
        if (run > 1) {
            w.u8(flags[i] | 8u);
            w.u8(static_cast<std::uint32_t>(run - 1));
        } else w.u8(flags[i]);
        i += run;
    }
    w.b.insert(w.b.end(), xs.b.begin(), xs.b.end());
    w.b.insert(w.b.end(), ys.b.begin(), ys.b.end());
}

void writeComposite(Writer& w, const Glyph& g, const Box& b) {
    w.s16(-1);
    w.s16(b.xMin); w.s16(b.yMin); w.s16(b.xMax); w.s16(b.yMax);
    for (std::size_t i = 0; i < g.components.size(); ++i) {
        const auto& c = g.components[i];
        std::uint16_t f = 0x0002;   // ARGS_ARE_XY_VALUES
        const bool words = c.dx < -128 || c.dx > 127 || c.dy < -128 || c.dy > 127;
        if (words) f |= 0x0001;
        if (c.roundXY) f |= 0x0004;
        const bool identity = c.xx == 1 && c.xy == 0 && c.yx == 0 && c.yy == 1;
        if (!identity) {
            if (c.xy == 0 && c.yx == 0) f |= (c.xx == c.yy) ? 0x0008 : 0x0040;
            else f |= 0x0080;
        }
        if (i + 1 < g.components.size()) f |= 0x0020;
        if (c.useMyMetrics) f |= 0x0200;
        w.u16(f);
        w.u16(static_cast<std::uint32_t>(c.glyph));
        if (words) { w.s16(c.dx); w.s16(c.dy); }
        else { w.u8(static_cast<std::uint32_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(c.dx)))); w.u8(static_cast<std::uint32_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(c.dy)))); }
        if (f & 0x0008) w.f2dot14(c.xx);
        else if (f & 0x0040) { w.f2dot14(c.xx); w.f2dot14(c.yy); }
        else if (f & 0x0080) { w.f2dot14(c.xx); w.f2dot14(c.xy); w.f2dot14(c.yx); w.f2dot14(c.yy); }
    }
}

void utf16(Writer& w, std::string_view s) {
    for (std::uint32_t c : utf8Codes(s)) {
        if (c >= 0x10000) {
            c -= 0x10000;
            w.u16(0xD800 + (c >> 10));
            w.u16(0xDC00 + (c & 0x3FF));
        } else w.u16(c);
    }
}

std::string englishStyle(const Font& f) {
    if (f.bold && f.italic) return "Bold Italic";
    if (f.bold) return "Bold";
    if (f.italic) return "Italic";
    return "Regular";
}

} // namespace

// ================================================================== read ===
bool read(const Bytes& data, Font& out, std::string* why) {
    const auto fail = [why](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    Reader r(data);
    if (data.size() < 12) return fail("fichier trop court pour une police");
    const std::uint32_t ver = r.u32(0);
    if (ver == 0x4F54544Fu) return fail("une OpenType a contours CFF : elle s'emploie, mais ne se modifie pas");
    if (ver == 0x74746366u) return fail("une collection de polices (TTC) : choisis une police seule (TTF)");
    if (ver != 0x00010000u && ver != 0x74727565u) return fail("pas une police TrueType");
    std::map<std::string, Table> tables;
    const std::size_t numTables = r.u16(4);
    for (std::size_t i = 0; i < numTables; ++i) {
        const std::size_t rec = 12 + 16 * i;
        Table t{r.u32(rec + 8), r.u32(rec + 12)};
        if (!r.ok || !r.has(t.offset, t.length)) return fail("table illisible : " + tagText(r.u32(rec)));
        tables[tagText(r.u32(rec))] = t;
    }
    for (const char* need : {"head", "hhea", "maxp", "hmtx", "loca", "glyf", "cmap"})
        if (!tables.count(need)) return fail(std::string("table ") + need + " absente");
    Font f;
    f.glyphs.clear();
    const Table head = tables["head"], hhea = tables["hhea"], maxp = tables["maxp"];
    f.unitsPerEm = r.u16(head.offset + 18);
    if (f.unitsPerEm < 16 || f.unitsPerEm > 16384) return fail("unites par cadratin invalides");
    {
        char v[16];
        std::snprintf(v, sizeof v, "%.3f", r.fixed(head.offset + 4));
        f.version = v;
    }
    f.created = r.s64(head.offset + 20);
    f.modified = r.s64(head.offset + 28);
    const int macStyle = r.u16(head.offset + 44);
    const int locFormat = r.s16(head.offset + 50);
    f.bold = (macStyle & 1) != 0;
    f.italic = (macStyle & 2) != 0;
    f.ascent = r.s16(hhea.offset + 4);
    f.descent = r.s16(hhea.offset + 6);
    f.lineGap = r.s16(hhea.offset + 8);
    const std::size_t numHM = r.u16(hhea.offset + 34);
    const std::size_t numGlyphs = r.u16(maxp.offset + 4);
    if (numGlyphs == 0 || numHM == 0) return fail("police sans glyphes");
    f.glyphs.resize(numGlyphs);
    const Table hmtx = tables["hmtx"], loca = tables["loca"], glyf = tables["glyf"];
    int lastAdv = 0;
    for (std::size_t i = 0; i < numGlyphs; ++i) {
        if (i < numHM) lastAdv = r.u16(hmtx.offset + 4 * i);
        f.glyphs[i].advance = lastAdv;
    }
    std::vector<PendingMatch> pending;
    for (std::size_t i = 0; i < numGlyphs; ++i) {
        std::size_t a = 0, b = 0;
        if (locFormat == 0) { a = 2u * r.u16(loca.offset + 2 * i); b = 2u * r.u16(loca.offset + 2 * (i + 1)); }
        else { a = r.u32(loca.offset + 4 * i); b = r.u32(loca.offset + 4 * (i + 1)); }
        if (!r.ok || b < a || b > glyf.length) return fail("glyphe " + std::to_string(i) + " hors de la table glyf");
        if (!readGlyph(r, glyf.offset + a, b - a, f.glyphs[i], static_cast<int>(i), pending))
            return fail("glyphe " + std::to_string(i) + " illisible");
    }
    for (auto& g : f.glyphs)
        for (auto& c : g.components)
            if (c.glyph < 0 || c.glyph >= static_cast<int>(numGlyphs)) c.glyph = 0;
    // Les points apparies : le decalage qui pose le point `childPoint` du composant sur le point `parentPoint` des
    // composants precedents.
    for (const auto& m : pending) {
        Glyph& g = f.glyphs[static_cast<std::size_t>(m.glyph)];
        std::vector<Contour> before;
        int depth = 0;
        for (int k = 0; k < m.component; ++k) {
            const auto& c = g.components[static_cast<std::size_t>(k)];
            expand(f, c.glyph, c.xx, c.xy, c.yx, c.yy, c.dx, c.dy, 1, before, depth);
        }
        std::vector<Contour> child;
        auto& c = g.components[static_cast<std::size_t>(m.component)];
        expand(f, c.glyph, c.xx, c.xy, c.yx, c.yy, 0, 0, 1, child, depth);
        const auto nth = [](const std::vector<Contour>& cs, int n) -> const Point* {
            for (const auto& ct : cs) {
                if (n < static_cast<int>(ct.points.size())) return &ct.points[static_cast<std::size_t>(n)];
                n -= static_cast<int>(ct.points.size());
            }
            return nullptr;
        };
        const Point* pp = nth(before, m.parentPoint);
        const Point* cp = nth(child, m.childPoint);
        if (pp && cp) {
            c.dx = pp->x - cp->x;
            c.dy = pp->y - cp->y;
        }
    }
    readCmap(r, tables["cmap"], f.glyphs);
    if (tables.count("name")) {
        const Table t = tables["name"];
        const std::string typo = nameText(r, t, 16);
        f.family = typo.empty() ? nameText(r, t, 1) : typo;
        f.copyright = nameText(r, t, 0);
        f.trademark = nameText(r, t, 7);
        f.manufacturer = nameText(r, t, 8);
        f.designer = nameText(r, t, 9);
        f.description = nameText(r, t, 10);
        f.license = nameText(r, t, 13);
        f.licenseUrl = nameText(r, t, 14);
        const std::string v = nameText(r, t, 5);
        if (!v.empty()) {
            // "Version 2.00.1" -> "2.00.1" ; garde la premiere suite chiffres/points.
            std::string num;
            for (char c : v) {
                if ((c >= '0' && c <= '9') || (c == '.' && !num.empty())) num += c;
                else if (!num.empty()) break;
            }
            if (!num.empty()) f.version = num;
        }
    }
    if (f.family.empty()) f.family = "Police";
    if (tables.count("OS/2")) {
        const Table t = tables["OS/2"];
        f.os2.assign(data.begin() + static_cast<std::ptrdiff_t>(t.offset), data.begin() + static_cast<std::ptrdiff_t>(t.offset + t.length));
        if (t.length >= 78) {
            f.weight = r.u16(t.offset + 4);
            const int sel = r.u16(t.offset + 62);
            f.italic = f.italic || (sel & 1) != 0;
            f.bold = f.bold || (sel & 32) != 0;
            if (sel & 128) {   // USE_TYPO_METRICS
                f.ascent = r.s16(t.offset + 68);
                f.descent = r.s16(t.offset + 70);
                f.lineGap = r.s16(t.offset + 72);
            }
        }
        if (t.length >= 96 && r.u16(t.offset) >= 2) {
            f.xHeight = r.s16(t.offset + 86);
            f.capHeight = r.s16(t.offset + 88);
        }
    }
    // Pas de hauteurs dans OS/2 : celles du << x >> et du << H >>.
    if (f.xHeight <= 0 || f.capHeight <= 0 || f.os2.size() < 96) {
        if (const int x = glyphOf(f, 'x'); x >= 0) f.xHeight = bounds(f, x).yMax;
        if (const int h = glyphOf(f, 'H'); h >= 0) f.capHeight = bounds(f, h).yMax;
    }
    if (tables.count("post")) {
        const Table t = tables["post"];
        f.italicAngle = r.fixed(t.offset + 4);
        f.underlinePosition = r.s16(t.offset + 8);
        f.underlineThickness = r.s16(t.offset + 10);
        f.fixedPitch = r.u32(t.offset + 12) != 0;
    }
    for (const char* k : {"GDEF", "GPOS", "GSUB", "kern", "gasp"})
        if (tables.count(k)) {
            const Table t = tables[k];
            f.keep.emplace_back(k, Bytes(data.begin() + static_cast<std::ptrdiff_t>(t.offset),
                                         data.begin() + static_cast<std::ptrdiff_t>(t.offset + t.length)));
        }
    if (!r.ok) return fail("police tronquee");
    f.reserved = reservedOf(f);
    out = std::move(f);
    return true;
}

// ================================================================== write ===
Bytes write(const Font& f) {
    const int n = static_cast<int>(f.glyphs.size());
    // ---- glyf, loca, les mesures ----
    Writer glyf;
    std::vector<std::uint32_t> loca;
    std::vector<Box> boxes(static_cast<std::size_t>(n));
    std::vector<int> advances(static_cast<std::size_t>(n));
    int maxPoints = 0, maxContours = 0, maxCPoints = 0, maxCContours = 0, maxElements = 0, maxDepth = 0;
    for (int i = 0; i < n; ++i) {
        loca.push_back(static_cast<std::uint32_t>(glyf.b.size()));
        const Glyph& g = f.glyphs[static_cast<std::size_t>(i)];
        advances[static_cast<std::size_t>(i)] = std::max(0, advanceOf(f, i));
        std::vector<Contour> all;
        int depth = 0;
        expand(f, i, 1, 0, 0, 1, 0, 0, 0, all, depth);
        Box b = boxOf(all);
        boxes[static_cast<std::size_t>(i)] = b;
        if (b.empty) continue;   // un glyphe vide : 0 octet
        int pts = 0;
        for (const auto& c : all) pts += static_cast<int>(c.points.size());
        if (g.composite()) {
            maxCPoints = std::max(maxCPoints, pts);
            maxCContours = std::max(maxCContours, static_cast<int>(all.size()));
            maxElements = std::max(maxElements, static_cast<int>(g.components.size()));
            maxDepth = std::max(maxDepth, depth);
            writeComposite(glyf, g, b);
        } else {
            maxPoints = std::max(maxPoints, pts);
            maxContours = std::max(maxContours, static_cast<int>(writable(g).size()));
            writeSimple(glyf, g, b);
        }
        glyf.pad4();
    }
    loca.push_back(static_cast<std::uint32_t>(glyf.b.size()));
    Box all;
    int advMax = 0, minLsb = 32767, minRsb = 32767, maxExtent = -32768;
    long sumAdv = 0;
    int counted = 0;
    for (int i = 0; i < n; ++i) {
        const Box& b = boxes[static_cast<std::size_t>(i)];
        const int adv = advances[static_cast<std::size_t>(i)];
        advMax = std::max(advMax, adv);
        if (adv > 0) {
            sumAdv += adv;
            ++counted;
        }
        if (b.empty) continue;
        if (all.empty) all = b;
        else {
            all.xMin = std::min(all.xMin, b.xMin);
            all.yMin = std::min(all.yMin, b.yMin);
            all.xMax = std::max(all.xMax, b.xMax);
            all.yMax = std::max(all.yMax, b.yMax);
        }
        minLsb = std::min(minLsb, b.xMin);
        minRsb = std::min(minRsb, adv - b.xMax);
        maxExtent = std::max(maxExtent, b.xMax);
    }
    if (all.empty) {
        all = {0, f.descent, f.unitsPerEm / 2, f.ascent, false};
        minLsb = minRsb = 0;
        maxExtent = 0;
    }
    // ---- les caracteres ----
    std::map<std::uint32_t, int> map;
    for (int i = 1; i < n; ++i)
        for (std::uint32_t c : f.glyphs[static_cast<std::size_t>(i)].codes)
            if (c <= 0x10FFFF) map.emplace(c, i);   // un caractere a deux glyphes : le premier
    const std::uint32_t firstChar = map.empty() ? 0x20 : std::min<std::uint32_t>(map.begin()->first, 0xFFFF);
    const std::uint32_t lastChar = map.empty() ? 0x20 : std::min<std::uint32_t>(map.rbegin()->first, 0xFFFF);
    const int winAscent = std::max(f.ascent, all.yMax), winDescent = std::max(-f.descent, -all.yMin);

    std::vector<std::pair<std::string, Bytes>> tbl;
    // ---- head ----
    {
        Writer w;
        w.u32(0x00010000);
        w.fixed(std::atof(f.version.c_str()));
        w.u32(0);              // checkSumAdjustment, ecrit a la fin
        w.u32(0x5F0F3CF5);
        w.u16(0x000B);         // ligne de base a y=0, lsb a x=0, tailles entieres
        w.u16(static_cast<std::uint32_t>(f.unitsPerEm));
        w.s64(f.created);
        w.s64(f.modified);
        w.s16(all.xMin); w.s16(all.yMin); w.s16(all.xMax); w.s16(all.yMax);
        w.u16((f.bold ? 1u : 0u) | (f.italic ? 2u : 0u));
        w.u16(8);              // lowestRecPPEM
        w.s16(2);              // fontDirectionHint
        w.s16(1);              // loca longue
        w.s16(0);
        tbl.emplace_back("head", std::move(w.b));
    }
    // ---- hhea ----
    {
        Writer w;
        w.u32(0x00010000);
        w.s16(f.ascent); w.s16(f.descent); w.s16(f.lineGap);
        w.u16(static_cast<std::uint32_t>(advMax));
        w.s16(minLsb); w.s16(minRsb); w.s16(maxExtent);
        w.s16(1); w.s16(0); w.s16(0);   // le curseur droit
        for (int k = 0; k < 4; ++k) w.s16(0);
        w.s16(0);
        w.u16(static_cast<std::uint32_t>(n));
        tbl.emplace_back("hhea", std::move(w.b));
    }
    // ---- maxp ----
    {
        Writer w;
        w.u32(0x00010000);
        w.u16(static_cast<std::uint32_t>(n));
        w.u16(static_cast<std::uint32_t>(maxPoints)); w.u16(static_cast<std::uint32_t>(maxContours));
        w.u16(static_cast<std::uint32_t>(maxCPoints)); w.u16(static_cast<std::uint32_t>(maxCContours));
        w.u16(2);
        for (int k = 0; k < 6; ++k) w.u16(0);   // pas d'instructions
        w.u16(static_cast<std::uint32_t>(maxElements)); w.u16(static_cast<std::uint32_t>(maxDepth));
        tbl.emplace_back("maxp", std::move(w.b));
    }
    // ---- OS/2 : celle d'origine (version >= 2), ses mesures reecrites ; sinon une neuve (version 4) ----
    {
        Writer w;
        const bool reuse = f.os2.size() >= 96 && ((f.os2[0] << 8) | f.os2[1]) >= 2;
        if (reuse) {
            w.b = f.os2;
            w.put16(0, std::max(4, (f.os2[0] << 8) | f.os2[1]));   // 2 et 3 ont la forme de la 4 ; la 4 dit USE_TYPO_METRICS
        }
        else {
            w.u16(4);
            w.s16(0); w.u16(400); w.u16(5); w.u16(0);
            const int u = f.unitsPerEm;
            w.s16(u * 65 / 100); w.s16(u * 60 / 100); w.s16(0); w.s16(u * 7 / 100);       // indices
            w.s16(u * 65 / 100); w.s16(u * 60 / 100); w.s16(0); w.s16(u * 48 / 100);      // exposants
            w.s16(u * 5 / 100); w.s16(u * 26 / 100);                                      // barre
            w.s16(0);
            for (int k = 0; k < 10; ++k) w.u8(0);                                         // panose
            w.u32(0x00000003); w.u32(0); w.u32(0); w.u32(0);                              // latin, latin-1
            w.tag("NONE");
            w.u16(0); w.u16(0); w.u16(0);
            w.s16(0); w.s16(0); w.s16(0); w.u16(0); w.u16(0);
            w.u32(0x00000001); w.u32(0);                                                  // page de code 1252
            w.s16(0); w.s16(0); w.u16(0); w.u16(32); w.u16(0);
        }
        w.put16(2, static_cast<std::uint32_t>(counted ? sumAdv / counted : 0));
        w.put16(4, static_cast<std::uint32_t>(f.weight));
        std::uint32_t sel = static_cast<std::uint32_t>((w.b[62] << 8) | w.b[63]) & ~0x0061u;
        if (f.italic) sel |= 1u;
        if (f.bold) sel |= 32u;
        if (!f.italic && !f.bold) sel |= 64u;
        sel |= 128u;   // USE_TYPO_METRICS : les mesures ci-dessous font foi
        w.put16(62, sel);
        w.put16(64, firstChar);
        w.put16(66, lastChar);
        w.put16(68, static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(f.ascent))));
        w.put16(70, static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(f.descent))));
        w.put16(72, static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(f.lineGap))));
        w.put16(74, static_cast<std::uint32_t>(clamp16(winAscent) & 0xFFFF));
        w.put16(76, static_cast<std::uint32_t>(clamp16(winDescent) & 0xFFFF));
        w.put16(86, static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(f.xHeight))));
        w.put16(88, static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(f.capHeight))));
        tbl.emplace_back("OS/2", std::move(w.b));
    }
    // ---- name (Windows, Unicode, anglais) ----
    {
        const std::string style = englishStyle(f);
        const std::string full = style == "Regular" ? f.family : f.family + " " + style;
        std::vector<std::pair<int, std::string>> names = {
            {0, f.copyright}, {1, f.family}, {2, style}, {3, "IHM Studio: " + full + ": " + f.version}, {4, full},
            {5, "Version " + f.version}, {6, postScriptName(f)}, {7, f.trademark}, {8, f.manufacturer}, {9, f.designer},
            {10, f.description}, {13, f.license}, {14, f.licenseUrl}};
        names.erase(std::remove_if(names.begin(), names.end(), [](const auto& p) { return p.second.empty(); }), names.end());
        Writer strings;
        Writer w;
        w.u16(0);
        w.u16(static_cast<std::uint32_t>(names.size()));
        w.u16(static_cast<std::uint32_t>(6 + 12 * names.size()));
        for (const auto& [id, text] : names) {
            const std::size_t at = strings.b.size();
            utf16(strings, text);
            w.u16(3); w.u16(1); w.u16(0x409); w.u16(static_cast<std::uint32_t>(id));
            w.u16(static_cast<std::uint32_t>(strings.b.size() - at));
            w.u16(static_cast<std::uint32_t>(at));
        }
        w.b.insert(w.b.end(), strings.b.begin(), strings.b.end());
        tbl.emplace_back("name", std::move(w.b));
    }
    // ---- cmap : (3,1) format 4 pour le plan de base, (3,10) format 12 pour tout ----
    {
        // Format 4 : des segments de caracteres consecutifs dont les glyphes se suivent (idDelta).
        struct Seg {
            std::uint32_t start, end;
            int delta;
        };
        std::vector<Seg> segs;
        for (const auto& [c, g] : map) {
            if (c > 0xFFFE) break;
            if (!segs.empty() && segs.back().end + 1 == c && static_cast<int>(c) + segs.back().delta == g) segs.back().end = c;
            else segs.push_back({c, c, g - static_cast<int>(c)});
        }
        segs.push_back({0xFFFF, 0xFFFF, 1});
        Writer f4;
        const std::size_t segCount = segs.size();
        std::size_t sr = 1, es = 0;
        while (sr * 2 <= segCount) { sr *= 2; ++es; }
        f4.u16(4);
        f4.u16(static_cast<std::uint32_t>(16 + 8 * segCount));
        f4.u16(0);
        f4.u16(static_cast<std::uint32_t>(segCount * 2));
        f4.u16(static_cast<std::uint32_t>(sr * 2));
        f4.u16(static_cast<std::uint32_t>(es));
        f4.u16(static_cast<std::uint32_t>(segCount * 2 - sr * 2));
        for (const auto& s : segs) f4.u16(s.end);
        f4.u16(0);
        for (const auto& s : segs) f4.u16(s.start);
        for (const auto& s : segs) f4.u16(static_cast<std::uint32_t>(s.delta) & 0xFFFF);
        for (std::size_t k = 0; k < segCount; ++k) f4.u16(0);
        // Format 12 : des groupes.
        struct Grp {
            std::uint32_t start, end, glyph;
        };
        std::vector<Grp> grps;
        for (const auto& [c, g] : map) {
            if (!grps.empty() && grps.back().end + 1 == c && grps.back().glyph + (c - grps.back().start) == static_cast<std::uint32_t>(g))
                grps.back().end = c;
            else grps.push_back({c, c, static_cast<std::uint32_t>(g)});
        }
        Writer f12;
        f12.u16(12);
        f12.u16(0);
        f12.u32(static_cast<std::uint32_t>(16 + 12 * grps.size()));
        f12.u32(0);
        f12.u32(static_cast<std::uint32_t>(grps.size()));
        for (const auto& g : grps) { f12.u32(g.start); f12.u32(g.end); f12.u32(g.glyph); }
        Writer w;
        w.u16(0);
        w.u16(2);
        w.u16(3); w.u16(1); w.u32(4 + 16);
        w.u16(3); w.u16(10); w.u32(static_cast<std::uint32_t>(4 + 16 + f4.b.size()));
        w.b.insert(w.b.end(), f4.b.begin(), f4.b.end());
        w.b.insert(w.b.end(), f12.b.begin(), f12.b.end());
        tbl.emplace_back("cmap", std::move(w.b));
    }
    // ---- post (format 3 : sans noms de glyphes) ----
    {
        Writer w;
        w.u32(0x00030000);
        w.fixed(f.italicAngle);
        w.s16(f.underlinePosition);
        w.s16(f.underlineThickness);
        w.u32(f.fixedPitch ? 1u : 0u);
        for (int k = 0; k < 4; ++k) w.u32(0);
        tbl.emplace_back("post", std::move(w.b));
    }
    // ---- loca, hmtx, glyf ----
    {
        Writer w;
        for (auto v : loca) w.u32(v);
        tbl.emplace_back("loca", std::move(w.b));
    }
    {
        Writer w;
        for (int i = 0; i < n; ++i) {
            w.u16(static_cast<std::uint32_t>(advances[static_cast<std::size_t>(i)]));
            const Box& b = boxes[static_cast<std::size_t>(i)];
            w.s16(b.empty ? 0 : b.xMin);
        }
        tbl.emplace_back("hmtx", std::move(w.b));
    }
    tbl.emplace_back("glyf", std::move(glyf.b));
    for (const auto& [k, v] : f.keep) tbl.emplace_back(k, v);
    std::sort(tbl.begin(), tbl.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    // ---- le fichier : l'en-tete, le repertoire, les tables alignees sur 4 octets ----
    Writer out;
    const std::size_t nt = tbl.size();
    std::size_t sr = 1, es = 0;
    while (sr * 2 <= nt) { sr *= 2; ++es; }
    out.u32(0x00010000);
    out.u16(static_cast<std::uint32_t>(nt));
    out.u16(static_cast<std::uint32_t>(sr * 16));
    out.u16(static_cast<std::uint32_t>(es));
    out.u16(static_cast<std::uint32_t>(nt * 16 - sr * 16));
    const std::size_t dir = out.b.size();
    out.b.resize(dir + 16 * nt, 0);
    std::size_t headAt = 0;
    for (std::size_t i = 0; i < nt; ++i) {
        const auto& [tag, data] = tbl[i];
        out.pad4();
        const std::size_t at = out.b.size();
        if (tag == "head") headAt = at;
        out.b.insert(out.b.end(), data.begin(), data.end());
        Writer rec;
        rec.tag(tag);
        rec.u32(checksum(out.b, at, data.size()));
        rec.u32(static_cast<std::uint32_t>(at));
        rec.u32(static_cast<std::uint32_t>(data.size()));
        std::copy(rec.b.begin(), rec.b.end(), out.b.begin() + static_cast<std::ptrdiff_t>(dir + 16 * i));
    }
    out.pad4();
    out.put32(headAt + 8, 0xB1B0AFBAu - checksum(out.b, 0, out.b.size()));
    return out.b;
}

// ================================================================== creer, dupliquer ===
Font blank(const std::string& family) {
    Font f;
    f.family = family.empty() ? std::string("Ma police") : family;
    f.unitsPerEm = 1000;
    f.ascent = 800;
    f.descent = -200;
    f.capHeight = 700;
    f.xHeight = 500;
    f.copyright = "";
    Glyph notdef;
    notdef.advance = 500;
    notdef.contours.push_back({{{50, 0, true}, {50, 700, true}, {450, 700, true}, {450, 0, true}}});
    notdef.contours.push_back({{{100, 50, true}, {400, 50, true}, {400, 650, true}, {100, 650, true}}});
    f.glyphs.push_back(std::move(notdef));
    Glyph space;
    space.advance = 250;
    space.codes = {0x20};
    f.glyphs.push_back(space);
    space.codes = {0xA0};
    f.glyphs.push_back(space);
    const auto add = [&f](std::uint32_t c) {
        Glyph g;
        g.advance = 500;
        g.codes = {c};
        f.glyphs.push_back(std::move(g));
    };
    for (std::uint32_t c = 0x21; c <= 0x7E; ++c) add(c);
    for (std::uint32_t c = 0xA1; c <= 0xFF; ++c)
        if (c != 0xAD) add(c);
    add(0x20AC);
    return f;
}

Font duplicate(const Font& source, const std::string& family) {
    Font f = source;
    f.family = family;
    // La licence et les droits suivent la copie ; les noms reserves de la source restent interdits a ses copies.
    for (const auto& r : source.reserved)
        if (std::find(f.reserved.begin(), f.reserved.end(), r) == f.reserved.end()) f.reserved.push_back(r);
    if (f.copyright.find("IHM Studio") == std::string::npos)
        f.copyright += (f.copyright.empty() ? "" : "\n") + std::string("Modified with IHM Studio from ") + source.family + ".";
    f.trademark.clear();   // une marque n'est pas transmise
    return f;
}

bool nameAllowed(const Font& source, std::string_view family, std::string* why) {
    std::string t(family);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    while (!t.empty() && t.front() == ' ') t.erase(t.begin());
    if (t.empty()) {
        if (why) *why = "donne un nom a la police";
        return false;
    }
    if (t.size() > 63) {
        if (why) *why = "nom trop long (63 caract\xC3\xA8res au plus)";
        return false;
    }
    const std::string lt = lower(t);
    if (lt == lower(source.family)) {
        if (why) *why = "une copie porte un autre nom que sa source (" + source.family + ")";
        return false;
    }
    std::vector<std::string> res = source.reserved.empty() ? reservedOf(source) : source.reserved;
    for (const auto& r : res)
        if (lt.find(lower(r)) != std::string::npos) {
            if (why) *why = "\xC2\xAB " + r + " \xC2\xBB est un nom r\xC3\xA9serv\xC3\xA9 par la licence de " + source.family
                         + " : une copie ne peut pas le porter";
            return false;
        }
    return true;
}

std::string styleLabel(const Font& f) {
    if (f.bold && f.italic) return "Gras italique";
    if (f.bold) return "Gras";
    if (f.italic) return "Italique";
    return "Normal";
}

std::string postScriptName(const Font& f) {
    std::string out;
    for (char c : f.family)
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
    if (out.empty()) out = "Police";
    const std::string style = englishStyle(f);
    std::string st;
    for (char c : style)
        if (c != ' ') st += c;
    out += "-" + st;
    if (out.size() > 63) out.resize(63);
    return out;
}

// ================================================================== glyphes ===
int glyphOf(const Font& f, std::uint32_t code) {
    for (std::size_t i = 1; i < f.glyphs.size(); ++i)
        if (std::binary_search(f.glyphs[i].codes.begin(), f.glyphs[i].codes.end(), code)) return static_cast<int>(i);
    return -1;
}

int ensureGlyph(Font& f, std::uint32_t code, int advance) {
    if (const int g = glyphOf(f, code); g >= 0) return g;
    if (f.glyphs.empty()) f.glyphs.push_back(Glyph{});
    Glyph g;
    g.codes = {code};
    g.advance = advance >= 0 ? advance : f.unitsPerEm / 2;
    f.glyphs.push_back(std::move(g));
    return static_cast<int>(f.glyphs.size() - 1);
}

std::string codeText(std::uint32_t code) {
    const bool shown = (code > 0x20 && code < 0x7F) || (code > 0xA0 && code != 0xAD && code < 0x2000) || code == 0x20AC;
    if (shown) {
        std::string s;
        appendUtf8(s, code);
        return s;
    }
    char b[16];
    std::snprintf(b, sizeof b, "U+%04X", code);
    return b;
}

int advanceOf(const Font& f, int glyph) {
    for (int depth = 0; depth < 8; ++depth) {
        if (glyph < 0 || glyph >= static_cast<int>(f.glyphs.size())) return 0;
        const Glyph& g = f.glyphs[static_cast<std::size_t>(glyph)];
        int next = -1;
        for (const auto& c : g.components)
            if (c.useMyMetrics) next = c.glyph;
        if (next < 0) return g.advance;
        glyph = next;
    }
    return 0;
}

std::vector<Contour> outline(const Font& f, int glyph) {
    std::vector<Contour> out;
    int depth = 0;
    expand(f, glyph, 1, 0, 0, 1, 0, 0, 0, out, depth);
    return out;
}

Box bounds(const Font& f, int glyph) { return boxOf(outline(f, glyph)); }

std::vector<int> usersOf(const Font& f, int glyph) {
    std::vector<int> out;
    for (std::size_t i = 0; i < f.glyphs.size(); ++i)
        for (const auto& c : f.glyphs[i].components)
            if (c.glyph == glyph) {
                out.push_back(static_cast<int>(i));
                break;
            }
    return out;
}

void decompose(Font& f, int glyph) {
    if (glyph < 0 || glyph >= static_cast<int>(f.glyphs.size())) return;
    const int adv = advanceOf(f, glyph);
    auto cs = outline(f, glyph);
    Glyph& g = f.glyphs[static_cast<std::size_t>(glyph)];
    g.contours = std::move(cs);
    g.components.clear();
    g.advance = adv;
}

void copyGlyph(Font& into, int glyph, const Font& from, int fromGlyph) {
    if (glyph < 0 || glyph >= static_cast<int>(into.glyphs.size())) return;
    const double s = static_cast<double>(into.unitsPerEm) / std::max(1, from.unitsPerEm);
    Glyph& g = into.glyphs[static_cast<std::size_t>(glyph)];
    g.components.clear();
    g.contours.clear();
    for (const auto& c : outline(from, fromGlyph)) {
        Contour t;
        for (const auto& p : c.points) t.points.push_back({static_cast<int>(std::lround(p.x * s)), static_cast<int>(std::lround(p.y * s)), p.on});
        g.contours.push_back(std::move(t));
    }
    g.advance = static_cast<int>(std::lround(advanceOf(from, fromGlyph) * s));
}

// ================================================================== contours ===
namespace {
// Les points du contour, les points implicites (le milieu de deux controles) inseres ; `src` : le rang dans le contour
// (-1 : implicite). Le premier est sur la courbe.
struct Ex {
    double x, y;
    bool   on;
    int    src;
};
std::vector<Ex> expandPoints(const Contour& c) {
    std::vector<Ex> e;
    const int n = static_cast<int>(c.points.size());
    for (int i = 0; i < n; ++i) {
        const Point& p = c.points[static_cast<std::size_t>(i)];
        const Point& q = c.points[static_cast<std::size_t>((i + 1) % n)];
        e.push_back({static_cast<double>(p.x), static_cast<double>(p.y), p.on, i});
        if (!p.on && !q.on) e.push_back({(p.x + q.x) / 2.0, (p.y + q.y) / 2.0, true, -1});
    }
    const auto first = std::find_if(e.begin(), e.end(), [](const Ex& x) { return x.on; });
    if (first != e.end()) std::rotate(e.begin(), first, e.end());
    return e;
}
} // namespace

std::vector<Segment> segments(const Contour& c) {
    std::vector<Segment> out;
    if (c.points.size() < 2) return out;
    const auto e = expandPoints(c);
    const std::size_t m = e.size();
    for (std::size_t j = 0; j < m;) {
        const Ex& a = e[j];
        const Ex& b = e[(j + 1) % m];
        if (b.on) {
            out.push_back({a.x, a.y, a.x, a.y, b.x, b.y, false, a.src});
            j += 1;
        } else {
            const Ex& d = e[(j + 2) % m];
            out.push_back({a.x, a.y, b.x, b.y, d.x, d.y, true, b.src});
            j += 2;
        }
    }
    return out;
}

int insertPoint(Contour& c, int seg, double t) {
    const auto segs = segments(c);
    if (seg < 0 || seg >= static_cast<int>(segs.size())) return -1;
    t = std::clamp(t, 0.0, 1.0);
    const Segment s = segs[static_cast<std::size_t>(seg)];
    const auto lerp = [](double a, double b, double u) { return a + (b - a) * u; };
    const auto R = [](double v) { return static_cast<int>(std::lround(v)); };
    const int n = static_cast<int>(c.points.size());
    if (!s.curve) {
        // Une droite part d'un point sur la courbe (s.start) vers le point suivant : le nouveau point vient entre eux.
        const int pos = s.start + 1;
        c.points.insert(c.points.begin() + pos, Point{R(lerp(s.x0, s.x1, t)), R(lerp(s.y0, s.y1, t)), true});
        return pos;
    }
    // Une courbe : son controle (s.start) devient deux controles et un point sur la courbe entre eux (de Casteljau). Les
    // extremites implicites (le milieu de deux controles) sont d'abord rendues explicites : la forme voisine ne bouge pas.
    int k = s.start;
    if (!c.points[static_cast<std::size_t>((k + 1) % n)].on)
        c.points.insert(c.points.begin() + k + 1, Point{R(s.x1), R(s.y1), true});
    if (!c.points[static_cast<std::size_t>((k - 1 + n) % n)].on) {
        c.points.insert(c.points.begin() + k, Point{R(s.x0), R(s.y0), true});
        ++k;
    }
    const double c1x = lerp(s.x0, s.cx, t), c1y = lerp(s.y0, s.cy, t);
    const double c2x = lerp(s.cx, s.x1, t), c2y = lerp(s.cy, s.y1, t);
    c.points[static_cast<std::size_t>(k)] = Point{R(c1x), R(c1y), false};
    c.points.insert(c.points.begin() + k + 1, {Point{R(lerp(c1x, c2x, t)), R(lerp(c1y, c2y, t)), true}, Point{R(c2x), R(c2y), false}});
    return k + 1;
}

void removePoint(Contour& c, int index) {
    if (index >= 0 && index < static_cast<int>(c.points.size())) c.points.erase(c.points.begin() + index);
}

void addRectangle(Glyph& g, int x0, int y0, int x1, int y1) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    // Dans le sens des aiguilles d'une montre (y vers le haut) : un contour plein.
    g.contours.push_back({{{x0, y0, true}, {x0, y1, true}, {x1, y1, true}, {x1, y0, true}}});
}

void addEllipse(Glyph& g, int cx, int cy, int rx, int ry) {
    rx = std::abs(rx);
    ry = std::abs(ry);
    // Quatre points sur la courbe (0, 90, 180, 270 degres) et deux controles entre chaque : huit arcs de 45 degres, le
    // milieu de deux controles tombant sur l'ellipse. Sens des aiguilles d'une montre.
    constexpr double kPi = 3.14159265358979323846;
    const double k = 1.0 / std::cos(kPi / 8);
    Contour c;
    for (int q = 0; q < 4; ++q) {
        const double a = kPi / 2 - q * kPi / 2;   // 90, 0, -90, -180
        c.points.push_back({static_cast<int>(std::lround(cx + rx * std::cos(a))), static_cast<int>(std::lround(cy + ry * std::sin(a))), true});
        for (int h = 1; h <= 2; ++h) {
            const double b = a - (2 * h - 1) * kPi / 8;
            c.points.push_back({static_cast<int>(std::lround(cx + rx * k * std::cos(b))), static_cast<int>(std::lround(cy + ry * k * std::sin(b))), false});
        }
    }
    g.contours.push_back(std::move(c));
}

void reverse(Contour& c) { std::reverse(c.points.begin(), c.points.end()); }

Hit hitPoint(const std::vector<Contour>& cs, double x, double y, double radius) {
    Hit best;
    double bd = radius;
    for (std::size_t ci = 0; ci < cs.size(); ++ci)
        for (std::size_t pi = 0; pi < cs[ci].points.size(); ++pi) {
            const auto& p = cs[ci].points[pi];
            const double d = std::hypot(p.x - x, p.y - y);
            if (d <= bd) {
                bd = d;
                best.contour = static_cast<int>(ci);
                best.point = static_cast<int>(pi);
                best.distance = d;
            }
        }
    return best;
}

Hit hitSegment(const std::vector<Contour>& cs, double x, double y, double radius) {
    Hit best;
    double bd = radius;
    for (std::size_t ci = 0; ci < cs.size(); ++ci) {
        const auto segs = segments(cs[ci]);
        for (std::size_t si = 0; si < segs.size(); ++si) {
            const auto& s = segs[si];
            for (int k = 0; k <= 32; ++k) {
                const double t = k / 32.0, u = 1 - t;
                const double px = s.curve ? u * u * s.x0 + 2 * u * t * s.cx + t * t * s.x1 : s.x0 + (s.x1 - s.x0) * t;
                const double py = s.curve ? u * u * s.y0 + 2 * u * t * s.cy + t * t * s.y1 : s.y0 + (s.y1 - s.y0) * t;
                const double d = std::hypot(px - x, py - y);
                if (d <= bd) {
                    bd = d;
                    best.contour = static_cast<int>(ci);
                    best.segment = static_cast<int>(si);
                    best.t = t;
                    best.distance = d;
                }
            }
        }
    }
    return best;
}

// ================================================================== dessiner ===
std::vector<std::uint8_t> raster(const std::vector<Contour>& cs, double scale, double originX, double baselineY, int w, int h) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(std::max(0, w)) * static_cast<std::size_t>(std::max(0, h)), 0);
    if (w <= 0 || h <= 0) return out;
    struct Edge {
        double x0, y0, x1, y1;
        int    dir;
    };
    std::vector<Edge> edges;
    const auto line = [&](double ax, double ay, double bx, double by) {
        const double X0 = originX + ax * scale, Y0 = baselineY - ay * scale, X1 = originX + bx * scale, Y1 = baselineY - by * scale;
        if (Y0 == Y1) return;
        if (Y0 < Y1) edges.push_back({X0, Y0, X1, Y1, 1});
        else edges.push_back({X1, Y1, X0, Y0, -1});
    };
    for (const auto& c : cs)
        for (const auto& s : segments(c)) {
            if (!s.curve) {
                line(s.x0, s.y0, s.x1, s.y1);
                continue;
            }
            const double len = (std::hypot(s.cx - s.x0, s.cy - s.y0) + std::hypot(s.x1 - s.cx, s.y1 - s.cy)) * scale;
            const int steps = std::clamp(static_cast<int>(len / 3), 2, 64);
            double px = s.x0, py = s.y0;
            for (int k = 1; k <= steps; ++k) {
                const double t = static_cast<double>(k) / steps, u = 1 - t;
                const double qx = u * u * s.x0 + 2 * u * t * s.cx + t * t * s.x1, qy = u * u * s.y0 + 2 * u * t * s.cy + t * t * s.y1;
                line(px, py, qx, qy);
                px = qx;
                py = qy;
            }
        }
    constexpr int kSub = 4;
    std::vector<float> acc(static_cast<std::size_t>(w) + 1);
    std::vector<std::pair<double, int>> xs;
    for (int row = 0; row < h; ++row) {
        std::fill(acc.begin(), acc.end(), 0.f);
        for (int sub = 0; sub < kSub; ++sub) {
            const double sy = row + (sub + 0.5) / kSub;
            xs.clear();
            for (const auto& e : edges)
                if (sy >= e.y0 && sy < e.y1) xs.emplace_back(e.x0 + (sy - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0), e.dir);
            std::sort(xs.begin(), xs.end());
            int wind = 0;
            for (std::size_t k = 0; k < xs.size(); ++k) {
                wind += xs[k].second;
                if (wind == 0 || k + 1 >= xs.size()) continue;
                double a = std::clamp(xs[k].first, 0.0, static_cast<double>(w)), b = std::clamp(xs[k + 1].first, 0.0, static_cast<double>(w));
                if (b <= a) continue;
                int ia = static_cast<int>(a), ib = static_cast<int>(b);
                if (ia == ib) {
                    acc[static_cast<std::size_t>(ia)] += static_cast<float>(b - a);
                    continue;
                }
                acc[static_cast<std::size_t>(ia)] += static_cast<float>(ia + 1 - a);
                for (int p = ia + 1; p < ib; ++p) acc[static_cast<std::size_t>(p)] += 1.f;
                if (ib < w) acc[static_cast<std::size_t>(ib)] += static_cast<float>(b - ib);
            }
        }
        for (int x = 0; x < w; ++x)
            out[static_cast<std::size_t>(row) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]
                = static_cast<std::uint8_t>(std::clamp(acc[static_cast<std::size_t>(x)] / kSub * 255.f, 0.f, 255.f));
    }
    return out;
}

} // namespace hmi::ttf
