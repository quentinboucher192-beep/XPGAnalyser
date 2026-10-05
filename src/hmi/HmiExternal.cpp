#include "HmiExternal.hpp"
#include "HmiMedia.hpp"
#include "../import/XmlReader.hpp"
#include "../project/Table.hpp"
#include "../xls/Workbook.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace hmi {

namespace {

// Un nombre tel qu'Excel le montre : 15 chiffres significatifs. Le classeur
// garde 9.199999999999999 pour 9,2 (le double le plus proche, ecrit en 17
// chiffres) ; la cellule affiche 9.2, l'apercu aussi.
std::string excelShown(const std::string& cell) {
    if (cell.empty() || cell.size() < 16) return cell;
    char* end = nullptr;
    const double v = std::strtod(cell.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(v)) return cell;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.15g", v);
    return buf;
}

bool readFile(const std::string& path, std::string& out, std::size_t limit = 64u * 1024 * 1024) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.resize(static_cast<std::size_t>(std::min<std::uintmax_t>(size, limit)));
    if (!out.empty()) in.read(out.data(), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(in) || in.eof();
}

std::vector<std::string> firstLines(const std::string& text, std::size_t count) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < text.size() && out.size() < count) {
        const auto eol = text.find('\n', pos);
        std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    return out;
}

std::size_t countLines(const std::string& text) {
    if (text.empty()) return 0;
    std::size_t n = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
    if (text.back() != '\n') ++n;
    return n;
}

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ======================================================================= JSON ==
struct JValue {
    enum Type : std::uint8_t { Null, Bool, Number, String, Array, Object } type{Null};
    std::string text;                                   // nombre (tel qu'ecrit), chaine, booleen
    std::vector<JValue> items;                          // tableau
    std::vector<std::pair<std::string, JValue>> fields; // objet, dans l'ordre
};

class JsonParser {
public:
    explicit JsonParser(std::string_view t) : t_(t) {}
    bool parse(JValue& out) {
        skip();
        if (!value(out, 0)) return false;
        skip();
        if (i_ != t_.size()) return fail("contenu apr\xC3\xA8s la fin du document");
        return true;
    }
    std::string error;
    std::size_t line{1}, column{1};
private:
    bool fail(std::string why) {
        if (error.empty()) {
            error = std::move(why);
            line = 1;
            column = 1;
            for (std::size_t k = 0; k < i_ && k < t_.size(); ++k) {
                if (t_[k] == '\n') { ++line; column = 1; } else ++column;
            }
        }
        return false;
    }
    void skip() { while (i_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[i_]))) ++i_; }
    bool literal(const char* word, JValue& out, JValue::Type type, std::string text) {
        const std::size_t n = std::strlen(word);
        if (t_.substr(i_, n) != word) return fail("valeur inattendue");
        i_ += n;
        out.type = type;
        out.text = std::move(text);
        return true;
    }
    bool string(std::string& out) {
        if (i_ >= t_.size() || t_[i_] != '"') return fail("guillemet attendu");
        ++i_;
        while (i_ < t_.size()) {
            const char c = t_[i_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (i_ >= t_.size()) break;
                const char e = t_[i_++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (i_ + 4 > t_.size()) return fail("\\u incomplet");
                        for (std::size_t h = 0; h < 4; ++h)
                            if (!std::isxdigit(static_cast<unsigned char>(t_[i_ + h]))) return fail("\\u : 4 chiffres hexad\xC3\xA9" "cimaux attendus");
                        const unsigned cp = static_cast<unsigned>(std::strtoul(std::string(t_.substr(i_, 4)).c_str(), nullptr, 16));
                        i_ += 4;
                        if (cp < 0x80) out += static_cast<char>(cp);
                        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                        else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: out += e; break;
                }
            } else if (static_cast<unsigned char>(c) < 0x20) {
                --i_;
                return fail("caract\xC3\xA8re de contr\xC3\xB4le dans une cha\xC3\xAEne");
            } else {
                out += c;
            }
        }
        return fail("cha\xC3\xAEne non ferm\xC3\xA9" "e");
    }
    bool value(JValue& out, int depth) {
        if (depth > 200) return fail("imbrication trop profonde");
        skip();
        if (i_ >= t_.size()) return fail("valeur attendue");
        const char c = t_[i_];
        if (c == '{') {
            ++i_;
            out.type = JValue::Object;
            skip();
            if (i_ < t_.size() && t_[i_] == '}') { ++i_; return true; }
            while (true) {
                skip();
                std::string key;
                if (!string(key)) return false;
                skip();
                if (i_ >= t_.size() || t_[i_] != ':') return fail("':' attendu apr\xC3\xA8s la cl\xC3\xA9");
                ++i_;
                JValue v;
                if (!value(v, depth + 1)) return false;
                out.fields.emplace_back(std::move(key), std::move(v));
                skip();
                if (i_ < t_.size() && t_[i_] == ',') { ++i_; continue; }
                if (i_ < t_.size() && t_[i_] == '}') { ++i_; return true; }
                return fail("',' ou '}' attendu");
            }
        }
        if (c == '[') {
            ++i_;
            out.type = JValue::Array;
            skip();
            if (i_ < t_.size() && t_[i_] == ']') { ++i_; return true; }
            while (true) {
                JValue v;
                if (!value(v, depth + 1)) return false;
                out.items.push_back(std::move(v));
                skip();
                if (i_ < t_.size() && t_[i_] == ',') { ++i_; continue; }
                if (i_ < t_.size() && t_[i_] == ']') { ++i_; return true; }
                return fail("',' ou ']' attendu");
            }
        }
        if (c == '"') { out.type = JValue::String; return string(out.text); }
        if (c == 't') return literal("true", out, JValue::Bool, "TRUE");
        if (c == 'f') return literal("false", out, JValue::Bool, "FALSE");
        if (c == 'n') return literal("null", out, JValue::Null, "");
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            const std::size_t from = i_;
            ++i_;
            while (i_ < t_.size() && (std::isdigit(static_cast<unsigned char>(t_[i_])) || t_[i_] == '.' || t_[i_] == 'e'
                                      || t_[i_] == 'E' || t_[i_] == '+' || t_[i_] == '-'))
                ++i_;
            out.type = JValue::Number;
            out.text = std::string(t_.substr(from, i_ - from));
            return true;
        }
        return fail(std::string("caract\xC3\xA8re inattendu '") + c + "'");
    }
    std::string_view t_;
    std::size_t      i_{0};
};

std::string jsonCell(const JValue& v) {
    switch (v.type) {
        case JValue::Null:   return {};
        case JValue::Array:  return "[" + std::to_string(v.items.size()) + "]";
        case JValue::Object: return "{" + std::to_string(v.fields.size()) + "}";
        default:             return v.text;
    }
}

// ================================================================== SQLite ==
struct SqliteReader {
    const std::vector<std::uint8_t>& f;
    std::uint32_t pageSize{0}, usable{0}, pageCount{0};
    [[nodiscard]] std::size_t pageAt(std::uint32_t page) const { return static_cast<std::size_t>(page - 1) * pageSize; }
    [[nodiscard]] bool has(std::size_t at, std::size_t n) const { return at <= f.size() && n <= f.size() - at; }
    [[nodiscard]] std::uint32_t be16(std::size_t at) const { return has(at, 2) ? (static_cast<std::uint32_t>(f[at]) << 8) | f[at + 1] : 0u; }
    [[nodiscard]] std::uint32_t be32(std::size_t at) const {
        return has(at, 4) ? (static_cast<std::uint32_t>(f[at]) << 24) | (static_cast<std::uint32_t>(f[at + 1]) << 16)
                                | (static_cast<std::uint32_t>(f[at + 2]) << 8) | f[at + 3]
                          : 0u;
    }
    bool varint(std::size_t at, std::uint64_t& v, std::size_t& len) const {
        v = 0;
        for (len = 1; len <= 9; ++len) {
            if (!has(at + len - 1, 1)) return false;
            const std::uint8_t b = f[at + len - 1];
            if (len == 9) { v = (v << 8) | b; return true; }
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) return true;
        }
        return false;
    }
    // La charge utile complete d'une cellule, debordements compris.
    bool payload(std::size_t at, std::uint64_t size, std::vector<std::uint8_t>& out) const {
        const std::uint64_t x = usable - 35;
        std::uint64_t local = size;
        if (size > x) {
            const std::uint64_t m = ((static_cast<std::uint64_t>(usable) - 12) * 32 / 255) - 23;
            const std::uint64_t k = m + ((size - m) % (usable - 4));
            local = k <= x ? k : m;
        }
        if (!has(at, static_cast<std::size_t>(local))) return false;
        out.assign(f.begin() + static_cast<std::ptrdiff_t>(at), f.begin() + static_cast<std::ptrdiff_t>(at + local));
        std::uint32_t next = local < size ? be32(at + static_cast<std::size_t>(local)) : 0u;
        int guard = 0;
        while (out.size() < size && next != 0 && guard++ < 100000) {
            if (next > pageCount) return false;
            const std::size_t pa = pageAt(next);
            const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(usable - 4, size - out.size()));
            if (!has(pa + 4, take)) return false;
            out.insert(out.end(), f.begin() + static_cast<std::ptrdiff_t>(pa + 4), f.begin() + static_cast<std::ptrdiff_t>(pa + 4 + take));
            next = be32(pa);
        }
        return out.size() == size;
    }
    // Les champs d'un enregistrement, en texte.
    static std::vector<std::string> record(const std::vector<std::uint8_t>& p) {
        std::vector<std::string> out;
        SqliteReader tmp{p, 0, 0, 0};
        std::uint64_t headerSize = 0;
        std::size_t len = 0;
        if (!tmp.varint(0, headerSize, len)) return out;
        std::vector<std::uint64_t> types;
        std::size_t at = len;
        while (at < headerSize && at < p.size()) {
            std::uint64_t t = 0;
            if (!tmp.varint(at, t, len)) break;
            types.push_back(t);
            at += len;
        }
        std::size_t data = static_cast<std::size_t>(headerSize);
        for (const auto t : types) {
            std::string v;
            auto intOf = [&](std::size_t n) {
                std::int64_t x = 0;
                for (std::size_t k = 0; k < n && data + k < p.size(); ++k) x = (x << 8) | p[data + k];
                if (n > 0 && n < 8 && (p.size() > data) && (p[data] & 0x80)) x -= (std::int64_t{1} << (8 * n));
                data += n;
                return std::to_string(x);
            };
            switch (t) {
                case 0: break;
                case 1: v = intOf(1); break;
                case 2: v = intOf(2); break;
                case 3: v = intOf(3); break;
                case 4: v = intOf(4); break;
                case 5: v = intOf(6); break;
                case 6: v = intOf(8); break;
                case 7: {
                    std::uint64_t bits = 0;
                    for (std::size_t k = 0; k < 8 && data + k < p.size(); ++k) bits = (bits << 8) | p[data + k];
                    data += 8;
                    double d;
                    std::memcpy(&d, &bits, 8);
                    char b[64];
                    std::snprintf(b, sizeof b, "%g", d);
                    v = b;
                    break;
                }
                case 8: v = "0"; break;
                case 9: v = "1"; break;
                default:
                    if (t >= 12) {
                        const std::size_t n = static_cast<std::size_t>((t - (t % 2 ? 13 : 12)) / 2);
                        if (t % 2 && data + n <= p.size()) v.assign(reinterpret_cast<const char*>(p.data() + data), n);
                        else v = "<" + std::to_string(n) + " octets>";
                        data += n;
                    }
                    break;
            }
            out.push_back(std::move(v));
        }
        return out;
    }
    // Parcourir un arbre de table : chaque feuille rend (rowid, enregistrement).
    template <class Fn>
    void walk(std::uint32_t page, Fn&& fn, int depth, std::set<std::uint32_t>& seen) const {
        if (page == 0 || page > pageCount || depth > 40 || !seen.insert(page).second) return;
        const std::size_t base = pageAt(page), hdr = base + (page == 1 ? 100 : 0);
        if (!has(hdr, 8)) return;
        const std::uint8_t type = f[hdr];
        const std::uint32_t cells = be16(hdr + 3);
        if (type == 0x05) {
            for (std::uint32_t c = 0; c < cells; ++c) {
                const std::size_t cellAt = base + be16(hdr + 12 + c * 2);
                walk(be32(cellAt), fn, depth + 1, seen);
                if (!fn.more()) return;
            }
            walk(be32(hdr + 8), fn, depth + 1, seen);
        } else if (type == 0x0D) {
            for (std::uint32_t c = 0; c < cells; ++c) {
                std::size_t at = base + be16(hdr + 8 + c * 2), len = 0;
                std::uint64_t size = 0, rowid = 0;
                if (!varint(at, size, len)) return;
                at += len;
                if (!varint(at, rowid, len)) return;
                at += len;
                std::vector<std::uint8_t> p;
                if (!payload(at, size, p)) return;
                fn.row(rowid, record(p));
                if (!fn.more()) return;
            }
        }
    }
};

std::vector<std::string> columnsOf(const std::string& sql, int& rowidAlias) {
    std::vector<std::string> cols;
    rowidAlias = -1;
    const auto open = sql.find('('), close = sql.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) return cols;
    std::vector<std::string> parts;
    int depth = 0;
    std::string cur;
    for (std::size_t i = open + 1; i < close; ++i) {
        const char c = sql[i];
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (c == ',' && depth == 0) { parts.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    parts.push_back(cur);
    for (auto part : parts) {
        std::size_t k = 0;
        while (k < part.size() && std::isspace(static_cast<unsigned char>(part[k]))) ++k;
        part.erase(0, k);
        const std::string up = [&] { std::string u = part; for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); return u; }();
        if (up.rfind("CONSTRAINT", 0) == 0 || up.rfind("PRIMARY", 0) == 0 || up.rfind("UNIQUE", 0) == 0
            || up.rfind("CHECK", 0) == 0 || up.rfind("FOREIGN", 0) == 0)
            continue;
        std::string name;
        if (!part.empty() && (part[0] == '"' || part[0] == '[' || part[0] == '`')) {
            const char end = part[0] == '[' ? ']' : part[0];
            const auto e = part.find(end, 1);
            name = part.substr(1, e == std::string::npos ? std::string::npos : e - 1);
        } else {
            std::size_t e = 0;
            while (e < part.size() && !std::isspace(static_cast<unsigned char>(part[e]))) ++e;
            name = part.substr(0, e);
        }
        if (up.find("INTEGER") != std::string::npos && up.find("PRIMARY KEY") != std::string::npos)
            rowidAlias = static_cast<int>(cols.size());
        cols.push_back(name);
    }
    return cols;
}

bool openSqlite(const std::vector<std::uint8_t>& file, SqliteReader& r, std::string& error) {
    static const char magic[] = "SQLite format 3";
    if (file.size() < 100 || std::memcmp(file.data(), magic, 16) != 0) {
        error = "ce n'est pas une base SQLite 3";
        return false;
    }
    r.pageSize = r.be16(16);
    if (r.pageSize == 1) r.pageSize = 65536;
    if (r.pageSize < 512) { error = "taille de page invalide"; return false; }
    r.usable = r.pageSize - file[20];
    r.pageCount = r.be32(28);
    const auto byFile = static_cast<std::uint32_t>(file.size() / r.pageSize);
    if (r.pageCount == 0 || r.pageCount > byFile) r.pageCount = byFile;
    return true;
}

// ==================================================================== XML ==
ExternalData readXml(const std::string& text, std::size_t maxRows) {
    ExternalData d;
    importer::XmlReader x(text);
    std::string root;
    std::map<std::string, std::size_t> childCount;     // enfants directs de la racine
    std::string rowName;
    std::vector<std::string> headers;
    std::vector<std::map<std::string, std::string>> rows;
    while (true) {
        auto ev = x.next();
        if (!ev) { d.error = ev.error().message(); break; }
        if (*ev == importer::XmlEvent::EndOfDocument) break;
        if (*ev != importer::XmlEvent::StartElement) continue;
        if (root.empty()) { root = std::string(x.name()); continue; }
        if (x.depth() == 2 || (x.depth() == 1 && !root.empty())) {
            const std::string n(x.name());
            ++childCount[n];
            if (rowName.empty()) rowName = n;
            if (n == rowName) {
                std::map<std::string, std::string> row;
                for (const auto& a : x.attributes()) {
                    const std::string key(a.name);
                    if (std::find(headers.begin(), headers.end(), key) == headers.end()) headers.push_back(key);
                    row[key] = importer::XmlReader::decodeEntities(a.rawValue);
                }
                if (!x.selfClosing()) {
                    auto body = x.readElementText();
                    if (body && !body->empty()) {
                        std::string t = *body;
                        t.erase(0, t.find_first_not_of(" \r\n\t"));
                        if (const auto e = t.find_last_not_of(" \r\n\t"); e != std::string::npos) t.resize(e + 1);
                        if (!t.empty()) {
                            if (std::find(headers.begin(), headers.end(), "texte") == headers.end()) headers.push_back("texte");
                            row["texte"] = t;
                        }
                    }
                }
                rows.push_back(std::move(row));
            } else if (!x.selfClosing()) {
                (void)x.skipElement();
            }
        }
    }
    d.totalRows = rows.size();
    d.headers = headers;
    for (std::size_t i = 0; i < rows.size() && i < maxRows; ++i) {
        std::vector<std::string> line;
        for (const auto& h : headers) line.push_back(rows[i].count(h) ? rows[i].at(h) : std::string{});
        d.rows.push_back(std::move(line));
    }
    std::string kids;
    for (const auto& [n, c] : childCount) kids += (kids.empty() ? "" : ", ") + n + " x" + std::to_string(c);
    d.summary = "XML valide : racine <" + root + ">" + (kids.empty() ? "" : ", " + kids);
    if (!d.ok()) d.summary = "XML invalide : " + d.error;
    d.lines = firstLines(text, 40);
    return d;
}

} // namespace

// =================================================================== public ==
JsonCheck checkJson(std::string_view text) {
    JsonCheck c;
    JsonParser p(text);
    JValue v;
    c.ok = p.parse(v);
    if (!c.ok) { c.error = p.error; c.line = p.line; c.column = p.column; }
    return c;
}

SqliteFile readSqliteSchema(const std::vector<std::uint8_t>& file) {
    SqliteFile s;
    SqliteReader r{file};
    if (!openSqlite(file, r, s.error)) return s;
    s.pageSize = r.pageSize;
    s.pageCount = r.pageCount;
    struct Collect {
        SqliteFile& s;
        void row(std::uint64_t, const std::vector<std::string>& v) {
            if (v.size() >= 5 && v[0] == "table" && v[1].rfind("sqlite_", 0) != 0)
                s.tables.push_back({v[1], v[4], static_cast<std::uint32_t>(std::strtoul(v[3].c_str(), nullptr, 10))});
        }
        [[nodiscard]] bool more() const { return true; }
    } collect{s};
    std::set<std::uint32_t> seen;
    r.walk(1, collect, 0, seen);
    return s;
}

bool readSqliteRows(const std::vector<std::uint8_t>& file, const SqliteTable& t, std::size_t maxRows,
                    std::vector<std::string>& headers, std::vector<std::vector<std::string>>& rows, std::size_t* total) {
    SqliteReader r{file};
    std::string error;
    if (!openSqlite(file, r, error)) return false;
    int alias = -1;
    headers = columnsOf(t.sql, alias);
    struct Collect {
        std::vector<std::vector<std::string>>& rows;
        std::size_t max, count{0};
        int alias;
        void row(std::uint64_t rowid, std::vector<std::string> v) {
            ++count;
            if (rows.size() >= max) return;
            if (alias >= 0 && static_cast<std::size_t>(alias) < v.size() && v[static_cast<std::size_t>(alias)].empty())
                v[static_cast<std::size_t>(alias)] = std::to_string(rowid);
            rows.push_back(std::move(v));
        }
        [[nodiscard]] bool more() const { return true; }   // on compte tout, on ne garde que `max`
    } collect{rows, maxRows, 0, alias};
    std::set<std::uint32_t> seen;
    r.walk(t.rootPage, collect, 0, seen);
    if (total) *total = collect.count;
    for (auto& row : rows) row.resize(std::max(row.size(), headers.size()));
    return true;
}

ExternalData readExternal(ExternalKind kind, const std::string& path, std::string_view part, std::size_t maxRows) {
    ExternalData d;
    if (kind == ExternalKind::Database) {
        // "Driver={ODBC Driver 17};Server=srv;Database=prod;" ou "postgresql://user@srv:5432/prod"
        std::string s = path;
        if (const auto scheme = s.find("://"); scheme != std::string::npos) {
            d.headers = {"Param\xC3\xA8tre", "Valeur"};
            d.rows.push_back({"protocole", s.substr(0, scheme)});
            std::string rest = s.substr(scheme + 3);
            const auto at = rest.find('@');
            if (at != std::string::npos) { d.rows.push_back({"utilisateur", rest.substr(0, at)}); rest = rest.substr(at + 1); }
            const auto slash = rest.find('/');
            d.rows.push_back({"serveur", rest.substr(0, slash)});
            if (slash != std::string::npos) d.rows.push_back({"base", rest.substr(slash + 1)});
        } else {
            d.headers = {"Param\xC3\xA8tre", "Valeur"};
            std::stringstream ss(s);
            std::string item;
            while (std::getline(ss, item, ';')) {
                const auto eq = item.find('=');
                if (eq == std::string::npos) continue;
                std::string key = item.substr(0, eq), value = item.substr(eq + 1);
                if (lowerAscii(key).find("pwd") != std::string::npos || lowerAscii(key).find("password") != std::string::npos)
                    value = "********";
                d.rows.push_back({key, value});
            }
        }
        d.totalRows = d.rows.size();
        d.summary = "cha\xC3\xAEne de connexion, " + std::to_string(d.rows.size())
                  + " param\xC3\xA8tre(s) ; la connexion demande un pilote que l'outil n'embarque pas";
        return d;
    }
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) { d.error = "fichier absent : " + path; return d; }
    if (kind == ExternalKind::Excel) {
        const auto info = xls::inspect(path);
        for (const auto& sh : info.sheets) d.parts.push_back(sh.name);
        xls::ReadOptions o;
        o.sheet = std::string(part);
        o.table.descriptionRows = 0;
        o.maxRows = 20000;
        const auto r = xls::read(path, o);
        if (!r.ok) {
            d.error = r.warnings.empty() ? "classeur illisible" : r.warnings.front();
            return d;
        }
        d.partUsed = r.sheetUsed;
        d.headers = r.table.headers();
        d.totalRows = r.table.rowCount();
        for (std::size_t i = 0; i < r.table.rowCount() && i < maxRows; ++i) {
            std::vector<std::string> row;
            for (std::size_t c = 0; c < r.table.columnCount(); ++c) row.push_back(excelShown(r.table.cell(i, c)));
            d.rows.push_back(std::move(row));
        }
        d.summary = "classeur : " + std::to_string(d.parts.size()) + " onglet(s), onglet \xC2\xAB " + d.partUsed + " \xC2\xBB : "
                  + std::to_string(d.totalRows) + " ligne(s), " + std::to_string(d.headers.size()) + " colonne(s)";
        return d;
    }
    // ---- Lot API 8 : glisser de fichiers, 2e partie ----
    //  Un document (PDF, DOCX, ZIP, image...) ne se lit pas : son genre et sa
    //  taille ; l'apercu propose de l'ouvrir avec le programme du systeme.
    if (kind == ExternalKind::Document) {           // (absent : dit plus haut)
        const auto size = fs::file_size(fs::path(path), ec);
        d.summary = "document " + documentFormat(path) + ", " + formatBytes(ec ? 0u : static_cast<std::uint64_t>(size))
                  + " : l'IHM le cite sans le lire";
        return d;
    }
    // ---- fin Lot API 8 : glisser de fichiers, 2e partie ----
    if (kind == ExternalKind::Sqlite) {
        std::string bytes;
        if (!readFile(path, bytes, 512u * 1024 * 1024)) { d.error = "lecture impossible"; return d; }
        const std::vector<std::uint8_t> file(bytes.begin(), bytes.end());
        const auto schema = readSqliteSchema(file);
        if (!schema.error.empty()) { d.error = schema.error; return d; }
        for (const auto& t : schema.tables) d.parts.push_back(t.name);
        const SqliteTable* chosen = nullptr;
        for (const auto& t : schema.tables)
            if (part.empty() ? chosen == nullptr : t.name == part) chosen = &t;
        if (!chosen) {
            d.summary = "base SQLite : " + std::to_string(schema.tables.size()) + " table(s)";
            if (!part.empty()) d.error = "table '" + std::string(part) + "' introuvable";
            return d;
        }
        d.partUsed = chosen->name;
        if (!readSqliteRows(file, *chosen, maxRows, d.headers, d.rows, &d.totalRows)) d.error = "table illisible";
        d.summary = "base SQLite : " + std::to_string(schema.tables.size()) + " table(s), pages de "
                  + std::to_string(schema.pageSize) + " o ; table \xC2\xAB " + d.partUsed + " \xC2\xBB : "
                  + std::to_string(d.totalRows) + " ligne(s)";
        d.lines.push_back(chosen->sql);
        return d;
    }
    std::string text;
    if (!readFile(path, text)) { d.error = "lecture impossible"; return d; }
    switch (kind) {
        case ExternalKind::Csv: {
            project::TableOptions o;
            o.descriptionRows = 0;
            const auto t = project::Table::parse(text, o);
            d.headers = t.headers();
            d.totalRows = t.rowCount();
            for (std::size_t i = 0; i < t.rowCount() && i < maxRows; ++i) {
                std::vector<std::string> row;
                for (std::size_t c = 0; c < t.columnCount(); ++c) row.push_back(t.cell(i, c));
                d.rows.push_back(std::move(row));
            }
            d.summary = "CSV : " + std::to_string(d.totalRows) + " ligne(s), " + std::to_string(d.headers.size())
                      + " colonne(s), s\xC3\xA9parateur '" + std::string(1, t.separator()) + "'";
            d.lines = firstLines(text, 40);
            return d;
        }
        case ExternalKind::Text:
            d.lines = firstLines(text, 200);
            d.totalRows = countLines(text);
            d.headers = {"Ligne", "Texte"};
            for (std::size_t i = 0; i < d.lines.size() && i < maxRows; ++i) d.rows.push_back({std::to_string(i + 1), d.lines[i]});
            d.summary = "texte : " + std::to_string(d.totalRows) + " ligne(s)";
            return d;
        case ExternalKind::Json: {
            JsonParser p(text);
            JValue v;
            if (!p.parse(v)) {
                d.error = "JSON invalide ligne " + std::to_string(p.line) + ", colonne " + std::to_string(p.column) + " : " + p.error;
                d.summary = d.error;
                d.lines = firstLines(text, 40);
                return d;
            }
            if (v.type == JValue::Array) {
                for (const auto& item : v.items)
                    if (item.type == JValue::Object)
                        for (const auto& [k, fv] : item.fields)
                            if (std::find(d.headers.begin(), d.headers.end(), k) == d.headers.end()) d.headers.push_back(k);
                d.totalRows = v.items.size();
                for (std::size_t i = 0; i < v.items.size() && i < maxRows; ++i) {
                    const auto& item = v.items[i];
                    std::vector<std::string> row;
                    if (item.type == JValue::Object) {
                        for (const auto& h : d.headers) {
                            std::string cell;
                            for (const auto& [k, fv] : item.fields) if (k == h) { cell = jsonCell(fv); break; }
                            row.push_back(cell);
                        }
                    } else {
                        if (d.headers.empty()) d.headers = {"Valeur"};
                        row.push_back(jsonCell(item));
                    }
                    d.rows.push_back(std::move(row));
                }
                d.summary = "JSON valide : tableau de " + std::to_string(v.items.size()) + " \xC3\xA9l\xC3\xA9ment(s)";
            } else if (v.type == JValue::Object) {
                d.headers = {"Cl\xC3\xA9", "Valeur"};
                for (const auto& [k, fv] : v.fields) d.rows.push_back({k, jsonCell(fv)});
                d.totalRows = d.rows.size();
                d.summary = "JSON valide : objet de " + std::to_string(v.fields.size()) + " cl\xC3\xA9(s)";
            } else {
                d.headers = {"Valeur"};
                d.rows.push_back({jsonCell(v)});
                d.totalRows = 1;
                d.summary = "JSON valide : une valeur";
            }
            d.lines = firstLines(text, 40);
            return d;
        }
        case ExternalKind::Xml:
            return readXml(text, maxRows);
        default:
            break;
    }
    return d;
}

} // namespace hmi
