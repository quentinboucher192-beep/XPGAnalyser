#include "HmiExport.hpp"
#include "HmiHistory.hpp"
#include "HmiModel.hpp"
#include "../../third_party/miniz.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

namespace hmi {

namespace {

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool endsWith(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() && s.substr(s.size() - tail.size()) == tail;
}

// Un nombre au sens du tableur : "12.5", "-3", "1e3" (pas "007", ni "12 bar").
bool numericCell(std::string_view s, double& out) {
    if (s.empty() || s.size() > 24) return false;
    if (s.size() > 1 && s[0] == '0' && s[1] != '.') return false;
    if (s.size() > 2 && s[0] == '-' && s[1] == '0' && s[2] != '.') return false;
    return parseNumber(s, out) && std::isfinite(out);
}

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
                // Les caracteres de controle n'ont pas leur place dans le XML.
                if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r') out += ' ';
                else out += c;
        }
    }
    return out;
}

// "A", "B"... "Z", "AA"...
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

// ---- un zip "stored", en memoire -------------------------------------------------------
void put16(Bytes& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
void put32(Bytes& out, std::uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, (v >> 16) & 0xFFFF);
}
void putText(Bytes& out, std::string_view s) { out.insert(out.end(), s.begin(), s.end()); }

Bytes storedZip(const std::vector<std::pair<std::string, std::string>>& items) {
    Bytes out, central;
    // Une date fixe (1er janvier 2026) : le meme contenu donne le meme fichier.
    const std::uint16_t time = 0, date = static_cast<std::uint16_t>(((2026 - 1980) << 9) | (1 << 5) | 1);
    for (const auto& [name, data] : items) {
        const auto offset = static_cast<std::uint32_t>(out.size());
        const auto crc = static_cast<std::uint32_t>(
            mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(data.data()), data.size()));
        const auto size = static_cast<std::uint32_t>(data.size());
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, 0x0800);
        put16(out, 0);
        put16(out, time);
        put16(out, date);
        put32(out, crc);
        put32(out, size);
        put32(out, size);
        put16(out, static_cast<std::uint32_t>(name.size()));
        put16(out, 0);
        putText(out, name);
        putText(out, data);
        put32(central, 0x02014b50);
        put16(central, 20);
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, 0);
        put16(central, time);
        put16(central, date);
        put32(central, crc);
        put32(central, size);
        put32(central, size);
        put16(central, static_cast<std::uint32_t>(name.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, offset);
        putText(central, name);
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

// ---- les chasses d'Helvetica (1/1000 d'em), 32 a 126 --------------------------------------
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
    // Les lettres accentuees : la chasse de leur lettre de base, a peu pres.
    if ((c >= 0xEC && c <= 0xEF) || (c >= 0xCC && c <= 0xCF)) return bold ? 278 : 250;
    if (c >= 0xC0 && c <= 0xDE) return 722;
    if (c == 0x85) return 1000;
    return 556;
}

double textWidth(std::string_view winAnsi, double size, bool bold) {
    double w = 0;
    for (const char c : winAnsi) w += charWidth(static_cast<unsigned char>(c), bold);
    return w * size / 1000.0;
}

// Ce qui tient dans `maxWidth` points, avec "..." (le caractere 0x85) s'il faut couper.
std::string fitWinAnsi(const std::string& s, double size, bool bold, double maxWidth) {
    if (textWidth(s, size, bold) <= maxWidth) return s;
    const double ell = charWidth(0x85, bold) * size / 1000.0;
    std::string out;
    double w = 0;
    for (const char c : s) {
        const double cw = charWidth(static_cast<unsigned char>(c), bold) * size / 1000.0;
        if (w + cw + ell > maxWidth) break;
        out += c;
        w += cw;
    }
    return out + '\x85';
}

// Une chaine PDF litterale : ( ) \ echappes, les octets hauts en octal.
std::string pdfString(std::string_view s) {
    std::string out = "(";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '(' || c == ')' || c == '\\') { out += '\\'; out += ch; }
        else if (c < 32 || c > 126) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\%03o", c);
            out += buf;
        } else out += ch;
    }
    return out + ")";
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s.empty() || s == "-0" ? std::string("0") : s;
}

} // namespace

std::string toWinAnsi(std::string_view u) {
    std::string out;
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
            case 0x20AC: out += '\x80'; break;   // euro
            case 0x201A: out += '\x82'; break;
            case 0x201E: out += '\x84'; break;
            case 0x2026: out += '\x85'; break;   // points de suite
            case 0x2030: out += '\x89'; break;
            case 0x0152: out += '\x8C'; break;   // OE
            case 0x2018: out += '\x91'; break;
            case 0x2019: out += '\x92'; break;
            case 0x201C: out += '\x93'; break;
            case 0x201D: out += '\x94'; break;
            case 0x2022: out += '\x95'; break;
            case 0x2013: out += '\x96'; break;
            case 0x2014: out += '\x97'; break;
            case 0x2122: out += '\x99'; break;
            case 0x0153: out += '\x9C'; break;   // oe
            case 0x0178: out += '\x9F'; break;
            case 0x2192: out += "->"; break;     // fleche
            case 0x2190: out += "<-"; break;
            case 0x2264: out += "<="; break;
            case 0x2265: out += ">="; break;
            case 0x2260: out += "<>"; break;
            case 0x00B3: out += '\xB3'; break;
            default: out += '?'; break;
        }
    }
    return out;
}

std::optional<ExportFormat> exportFormatFrom(std::string_view s) {
    const std::string l = lowerAscii(s);
    if (l == "csv" || endsWith(l, ".csv") || endsWith(l, ".txt")) return ExportFormat::Csv;
    if (l == "excel" || l == "xlsx" || endsWith(l, ".xlsx") || endsWith(l, ".xls")) return ExportFormat::Excel;
    if (l == "pdf" || endsWith(l, ".pdf")) return ExportFormat::Pdf;
    return std::nullopt;
}

std::string_view exportFormatLabel(ExportFormat f) noexcept {
    switch (f) {
        case ExportFormat::Csv: return "CSV";
        case ExportFormat::Excel: return "Excel";
        case ExportFormat::Pdf: return "PDF";
    }
    return "CSV";
}

std::string_view exportExtension(ExportFormat f) noexcept {
    switch (f) {
        case ExportFormat::Csv: return "csv";
        case ExportFormat::Excel: return "xlsx";
        case ExportFormat::Pdf: return "pdf";
    }
    return "csv";
}

std::string exportFileName(std::string_view wanted, ExportFormat f) {
    std::string out;
    for (const char c : wanted) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 32 || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            out += '-';
        else out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && (out.front() == ' ' || out.front() == '.')) out.erase(out.begin());
    // Une extension connue s'en va : c'est celle du format qui compte.
    const std::string l = lowerAscii(out);
    for (const char* ext : {".csv", ".txt", ".xlsx", ".xls", ".pdf"})
        if (endsWith(l, ext)) { out.resize(out.size() - std::char_traits<char>::length(ext)); break; }
    if (out.empty()) out = "export";
    return out + "." + std::string(exportExtension(f));
}

// ================================================================== CSV ====
std::string exportCsv(const ExportTable& t) {
    std::string out = "\xEF\xBB\xBF";      // le BOM : Excel lit l'UTF-8
    const auto line = [&](const std::vector<std::string>& cells) {
        for (std::size_t k = 0; k < cells.size(); ++k) {
            if (k) out += ';';
            out += csvField(cells[k]);
        }
        out += "\r\n";
    };
    line(t.headers);
    for (const auto& r : t.rows) line(r);
    if (!t.notes.empty()) out += "\r\n";
    for (const auto& n : t.notes) line({n});
    return out;
}

// ================================================================= Excel ===
namespace {

// Le nom d'un onglet : 31 caracteres au plus, sans [ ] : * ? / \ ; unique.
std::string sheetName(const std::string& title, std::vector<std::string>& taken) {
    std::string tab;
    for (const char c : title.empty() ? std::string("Export") : title)
        if (c != '[' && c != ']' && c != ':' && c != '*' && c != '?' && c != '/' && c != '\\') tab += c;
    const auto cutAt = [](std::string text, std::size_t chars) {
        std::size_t cps = 0, cut = text.size();
        for (std::size_t i = 0; i < text.size(); ++i)
            if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80 && ++cps > chars) { cut = i; break; }
        text.resize(cut);
        return text;
    };
    std::string unique = cutAt(tab, 31);
    if (unique.empty()) unique = tab = "Export";
    // Deux feuilles du meme nom : "Alarmes 2" (le nom raccourci pour tenir).
    for (int k = 2; std::find(taken.begin(), taken.end(), unique) != taken.end(); ++k) unique = cutAt(tab, 28) + " " + std::to_string(k);
    taken.push_back(unique);
    return unique;
}

// Une feuille : le titre, le sous-titre, l'en-tete (gras, fond gris, fige,
// filtrable), les lignes (les nombres en nombres), les notes.
template <typename Sid>
std::string sheetXml(const ExportTable& t, Sid&& sid) {
    std::size_t ncol = t.headers.size();
    for (const auto& r : t.rows) ncol = std::max(ncol, r.size());
    ncol = std::max<std::size_t>(1, ncol);
    // La largeur de chaque colonne, en caracteres (le texte le plus long, borne).
    std::vector<double> widths(ncol, 8);
    const auto measure = [&](std::size_t k, const std::string& s) {
        std::size_t cps = 0;
        for (const char c : s) cps += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
        widths[k] = std::clamp(static_cast<double>(cps) + 2.0, widths[k], 60.0);
    };
    for (std::size_t k = 0; k < t.headers.size(); ++k) measure(k, t.headers[k]);
    for (const auto& r : t.rows)
        for (std::size_t k = 0; k < r.size(); ++k) measure(k, r[k]);

    std::string sheet = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";
    const std::size_t headerRow = 3;
    sheet += "<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"" + std::to_string(headerRow)
           + "\" topLeftCell=\"A" + std::to_string(headerRow + 1) + "\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews>";
    sheet += "<cols>";
    for (std::size_t k = 0; k < ncol; ++k)
        sheet += "<col min=\"" + std::to_string(k + 1) + "\" max=\"" + std::to_string(k + 1) + "\" width=\"" + num(widths[k])
               + "\" customWidth=\"1\"/>";
    sheet += "</cols><sheetData>";
    const auto cell = [&](std::size_t row, std::size_t col, const std::string& value, int style, bool allowNumber) {
        const std::string ref = columnName(col) + std::to_string(row);
        std::string s = "<c r=\"" + ref + "\"" + (style ? " s=\"" + std::to_string(style) + "\"" : std::string{});
        double x = 0;
        if (allowNumber && numericCell(value, x)) return s + "><v>" + num(x) + "</v></c>";
        return s + " t=\"s\"><v>" + std::to_string(sid(value)) + "</v></c>";
    };
    sheet += "<row r=\"1\">" + cell(1, 0, t.title.empty() ? std::string("Export") : t.title, 2, false) + "</row>";
    if (!t.subtitle.empty()) sheet += "<row r=\"2\">" + cell(2, 0, t.subtitle, 0, false) + "</row>";
    sheet += "<row r=\"" + std::to_string(headerRow) + "\">";
    for (std::size_t k = 0; k < t.headers.size(); ++k) sheet += cell(headerRow, k, t.headers[k], 1, false);
    sheet += "</row>";
    for (std::size_t i = 0; i < t.rows.size(); ++i) {
        const std::size_t r = headerRow + 1 + i;
        sheet += "<row r=\"" + std::to_string(r) + "\">";
        for (std::size_t k = 0; k < t.rows[i].size(); ++k)
            if (!t.rows[i][k].empty()) sheet += cell(r, k, t.rows[i][k], 0, true);
        sheet += "</row>";
    }
    for (std::size_t i = 0; i < t.notes.size(); ++i) {           // lot 13 : sous le tableau, une ligne vide avant
        const std::size_t r = headerRow + t.rows.size() + 2 + i;
        sheet += "<row r=\"" + std::to_string(r) + "\">" + cell(r, 0, t.notes[i], 0, false) + "</row>";
    }
    sheet += "</sheetData>";
    if (!t.headers.empty())
        sheet += "<autoFilter ref=\"A" + std::to_string(headerRow) + ":" + columnName(ncol - 1)
               + std::to_string(headerRow + std::max<std::size_t>(1, t.rows.size())) + "\"/>";
    sheet += "</worksheet>";
    return sheet;
}

} // namespace

Bytes exportXlsx(const ExportTable& t) { return exportXlsxSheets({t}); }

// Lot 14 : plusieurs feuilles (un rapport periodique : une par section).
Bytes exportXlsxSheets(const std::vector<ExportTable>& tables) {
    // Les chaines, une fois chacune (sharedStrings), pour toutes les feuilles.
    std::vector<std::string> strings;
    std::map<std::string, std::size_t> index;
    const auto sid = [&](const std::string& s) {
        const auto it = index.find(s);
        if (it != index.end()) return it->second;
        index.emplace(s, strings.size());
        strings.push_back(s);
        return strings.size() - 1;
    };
    const std::vector<ExportTable> list = tables.empty() ? std::vector<ExportTable>{ExportTable{}} : tables;
    std::vector<std::string> sheets, names;
    for (const auto& t : list) {
        sheets.push_back(sheetXml(t, sid));
        (void)sheetName(t.title, names);
    }

    std::string sst = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                      "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\""
                    + std::to_string(strings.size()) + "\" uniqueCount=\"" + std::to_string(strings.size()) + "\">";
    for (const auto& s : strings) sst += "<si><t xml:space=\"preserve\">" + xmlEscape(s) + "</t></si>";
    sst += "</sst>";

    std::string types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>";
    for (std::size_t i = 0; i < sheets.size(); ++i)
        types += "<Override PartName=\"/xl/worksheets/sheet" + std::to_string(i + 1)
               + ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
    types +=
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
        "<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/>"
        "</Types>";
    const std::string rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"xl/workbook.xml\"/></Relationships>";
    std::string workbook =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>";
    for (std::size_t i = 0; i < sheets.size(); ++i)
        workbook += "<sheet name=\"" + xmlEscape(names[i]) + "\" sheetId=\"" + std::to_string(i + 1) + "\" r:id=\"rId" + std::to_string(i + 1) + "\"/>";
    workbook += "</sheets></workbook>";
    std::string workbookRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    for (std::size_t i = 0; i < sheets.size(); ++i)
        workbookRels += "<Relationship Id=\"rId" + std::to_string(i + 1)
                      + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet"
                      + std::to_string(i + 1) + ".xml\"/>";
    const std::size_t n = sheets.size();
    workbookRels += "<Relationship Id=\"rId" + std::to_string(n + 1)
                  + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
                    "<Relationship Id=\"rId" + std::to_string(n + 2)
                  + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"sharedStrings.xml\"/>"
                    "</Relationships>";
    const std::string styles =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"3\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font><font><b/><sz val=\"14\"/><name val=\"Calibri\"/></font></fonts>"
        "<fills count=\"3\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFD9E1F2\"/><bgColor indexed=\"64\"/></patternFill></fill></fills>"
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"3\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/></cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>";
    std::vector<std::pair<std::string, std::string>> items{{"[Content_Types].xml", types},
                                                           {"_rels/.rels", rels},
                                                           {"xl/workbook.xml", workbook},
                                                           {"xl/_rels/workbook.xml.rels", workbookRels},
                                                           {"xl/styles.xml", styles},
                                                           {"xl/sharedStrings.xml", sst}};
    for (std::size_t i = 0; i < sheets.size(); ++i) items.emplace_back("xl/worksheets/sheet" + std::to_string(i + 1) + ".xml", sheets[i]);
    return storedZip(items);
}

// =================================================================== PDF ===
Bytes exportPdf(const ExportTable& t) {
    constexpr double W = 842, H = 595, M = 36;              // A4 paysage, marges d'un demi-pouce
    constexpr double fs = 9, rowH = 15, headH = 18;
    const std::string title = toWinAnsi(t.title.empty() ? std::string("Export") : t.title);
    const std::string subtitle = toWinAnsi(t.subtitle);
    std::size_t ncol = t.headers.size();
    for (const auto& r : t.rows) ncol = std::max(ncol, r.size());
    ncol = std::max<std::size_t>(1, ncol);
    std::vector<std::string> heads(ncol);
    for (std::size_t k = 0; k < t.headers.size(); ++k) heads[k] = toWinAnsi(t.headers[k]);
    std::vector<std::vector<std::string>> rows;
    rows.reserve(t.rows.size());
    for (const auto& r : t.rows) {
        std::vector<std::string> row(ncol);
        for (std::size_t k = 0; k < r.size(); ++k) row[k] = toWinAnsi(r[k]);
        rows.push_back(std::move(row));
    }
    // Les colonnes : a la largeur de leur contenu (bornee), puis ramenees a la page.
    const double avail = W - 2 * M;
    std::vector<double> widths(ncol, 30);
    std::vector<bool> numeric(ncol, true);
    for (std::size_t k = 0; k < ncol; ++k) {
        widths[k] = std::max(widths[k], textWidth(heads[k], fs, true) + 10);
        bool any = false;
        for (const auto& r : rows) {
            widths[k] = std::max(widths[k], std::min(avail * 0.45, textWidth(r[k], fs, false) + 10));
            double x = 0;
            if (!r[k].empty()) { any = true; numeric[k] = numeric[k] && numericCell(r[k], x); }
        }
        numeric[k] = numeric[k] && any;
    }
    double total = 0;
    for (double w : widths) total += w;
    if (total > avail) for (auto& w : widths) w *= avail / total;
    const double tableTop = H - M - 20 - (subtitle.empty() ? 0 : 14) - 10;
    // Lot 13 : les notes sous le tableau - leur place gardee en bas de chaque page.
    std::vector<std::string> notes;
    for (const auto& n : t.notes) notes.push_back(toWinAnsi(n));
    const double notesH = notes.empty() ? 0.0 : 14.0 + 22.0 * static_cast<double>(notes.size());
    const double bottom = M + 18 + notesH;
    const auto perPage = static_cast<std::size_t>(std::max(1.0, std::floor((tableTop - headH - bottom) / rowH)));
    const std::size_t pages = std::max<std::size_t>(1, (rows.size() + perPage - 1) / perPage);

    std::vector<std::string> streams;
    for (std::size_t p = 0; p < pages; ++p) {
        std::string s;
        const auto textAt = [&](double x, double y, const std::string& str, double size, bool bold, double gray) {
            s += "BT /" + std::string(bold ? "F2 " : "F1 ") + num(size) + " Tf " + num(gray) + " g " + num(x) + " " + num(y) + " Td "
               + pdfString(str) + " Tj ET\n";
        };
        if (p == 0) {
            textAt(M, H - M - 15, fitWinAnsi(title, 15, true, avail), 15, true, 0.1);
            if (!subtitle.empty()) textAt(M, H - M - 31, fitWinAnsi(subtitle, 9, false, avail), 9, false, 0.4);
        } else {
            textAt(M, H - M - 15, fitWinAnsi(title + " (suite)", 11, true, avail), 11, true, 0.35);
        }
        // L'en-tete du tableau, sur fond gris.
        double y = tableTop;
        double tableW = 0;
        for (double w : widths) tableW += w;
        s += "0.85 0.88 0.93 rg " + num(M) + " " + num(y - headH) + " " + num(tableW) + " " + num(headH) + " re f\n";
        double x = M;
        for (std::size_t k = 0; k < ncol; ++k) {
            const std::string cellText = fitWinAnsi(heads[k], fs, true, widths[k] - 8);
            const double tx = numeric[k] ? x + widths[k] - 4 - textWidth(cellText, fs, true) : x + 4;
            textAt(tx, y - headH + 5.5, cellText, fs, true, 0.1);
            x += widths[k];
        }
        y -= headH;
        const std::size_t first = p * perPage, last = std::min(rows.size(), first + perPage);
        for (std::size_t i = first; i < last; ++i) {
            if ((i - first) % 2 == 1) s += "0.96 0.97 0.98 rg " + num(M) + " " + num(y - rowH) + " " + num(tableW) + " " + num(rowH) + " re f\n";
            x = M;
            for (std::size_t k = 0; k < ncol; ++k) {
                const std::string cellText = fitWinAnsi(rows[i][k], fs, false, widths[k] - 8);
                const double tx = numeric[k] ? x + widths[k] - 4 - textWidth(cellText, fs, false) : x + 4;
                textAt(tx, y - rowH + 4.5, cellText, fs, false, 0.15);
                x += widths[k];
            }
            y -= rowH;
        }
        // Les filets : sous l'en-tete et en bas du tableau.
        s += "0.6 G 0.6 w " + num(M) + " " + num(tableTop - headH) + " m " + num(M + tableW) + " " + num(tableTop - headH) + " l S\n";
        s += num(M) + " " + num(y) + " m " + num(M + tableW) + " " + num(y) + " l S\n";
        if (rows.empty()) textAt(M + 4, y - rowH + 4.5, toWinAnsi("(aucune ligne)"), fs, false, 0.45);
        if (p + 1 == pages)
            for (std::size_t k = 0; k < notes.size(); ++k)
                textAt(M, y - (rows.empty() ? rowH : 0.0) - 20 - 22 * static_cast<double>(k), fitWinAnsi(notes[k], fs + 1, false, avail), fs + 1, false, 0.2);
        // Le pied : la page.
        const std::string foot = "Page " + std::to_string(p + 1) + " / " + std::to_string(pages);
        textAt(W - M - textWidth(foot, 8, false), M - 4, foot, 8, false, 0.45);
        textAt(M, M - 4, toWinAnsi("XpgAnalyzer - IHM"), 8, false, 0.45);
        streams.push_back(std::move(s));
    }

    // Les objets : 1 catalogue, 2 pages, 3 et 4 les polices, puis page et contenu.
    std::vector<std::string> objects;
    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids;
    for (std::size_t p = 0; p < pages; ++p) kids += std::to_string(5 + 2 * p) + " 0 R ";
    objects.push_back("<< /Type /Pages /Kids [ " + kids + "] /Count " + std::to_string(pages) + " >>");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
    for (std::size_t p = 0; p < pages; ++p) {
        objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(W) + " " + num(H)
                          + "] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents " + std::to_string(6 + 2 * p) + " 0 R >>");
        objects.push_back("<< /Length " + std::to_string(streams[p].size()) + " >>\nstream\n" + streams[p] + "endstream");
    }
    std::string pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const std::size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const auto off : offsets) {
        char buf[24];
        std::snprintf(buf, sizeof buf, "%010zu 00000 n \n", off);
        pdf += buf;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return Bytes(pdf.begin(), pdf.end());
}

// ---- lot 13 : les outils du dossier de l'IHM (HmiDossier) ------------------------------------
Bytes zipStored(const std::vector<std::pair<std::string, std::string>>& items) { return storedZip(items); }
double pdfTextWidth(std::string_view winAnsi, double size, bool bold) { return textWidth(winAnsi, size, bold); }
std::string pdfLiteral(std::string_view winAnsi) { return pdfString(winAnsi); }
std::string pdfNumber(double v) { return num(v); }

Bytes exportBytes(const ExportTable& t, ExportFormat f) {
    switch (f) {
        case ExportFormat::Excel: return exportXlsx(t);
        case ExportFormat::Pdf: return exportPdf(t);
        case ExportFormat::Csv: break;
    }
    const std::string csv = exportCsv(t);
    return Bytes(csv.begin(), csv.end());
}

} // namespace hmi
