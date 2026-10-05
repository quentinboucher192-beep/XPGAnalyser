#include "Table.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>

namespace project {

namespace {

// ---------------------------------------------------------------------------
//  Encoding.
//
//  Excel writes the system code page. On a French Windows that is CP1252, and
//  "Détoxal" arrives as 0x44 0xE9 ... - a lone 0xE9, which is not valid UTF-8.
//  Reading the file as UTF-8 would put mojibake into the designations that end
//  up printed on the drawings.
//
//  So: if the bytes are valid UTF-8 they are left exactly alone (a file that was
//  already UTF-8 must not be mangled by a conversion it did not need), and only
//  otherwise are they read as CP1252.
// ---------------------------------------------------------------------------
bool isValidUtf8(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        int extra = 0;
        if (c < 0x80)            extra = 0;
        else if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return false;
        if (i + static_cast<std::size_t>(extra) >= s.size() && extra > 0) return false;
        for (int k = 1; k <= extra; ++k)
            if ((static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0xC0) != 0x80)
                return false;
        i += static_cast<std::size_t>(extra) + 1;
    }
    return true;
}

// The sixteen CP1252 bytes that are not Latin-1. The rest map to the code point
// of the same value, which is what makes this short.
const std::array<std::uint16_t, 32> kCp1252High = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

void appendUtf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

} // namespace

std::string toUtf8(const std::string& bytes) {
    std::string body = bytes;
    // A UTF-8 BOM. Excel writes one when asked for "CSV UTF-8", and it would
    // otherwise become part of the first column's name.
    if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF
        && static_cast<unsigned char>(body[1]) == 0xBB
        && static_cast<unsigned char>(body[2]) == 0xBF)
        body.erase(0, 3);

    if (isValidUtf8(body)) return body;

    std::string out;
    out.reserve(body.size() + body.size() / 4);
    for (char raw : body) {
        const auto c = static_cast<unsigned char>(raw);
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0xA0) appendUtf8(out, kCp1252High[c - 0x80u]);
        else appendUtf8(out, c);
    }
    return out;
}

// ---------------------------------------------------------------------------
//  Name matching. "Eng Max", "engmax" and "EngMax" are the same column; a
//  genuinely different name is a genuinely different column.
// ---------------------------------------------------------------------------
std::string normaliseKey(std::string_view name) {
    static const struct { const char* from; char to; } kFolds[] = {
        {"\xC3\xA0", 'a'}, {"\xC3\xA2", 'a'}, {"\xC3\xA4", 'a'},
        {"\xC3\xA7", 'c'},
        {"\xC3\xA8", 'e'}, {"\xC3\xA9", 'e'}, {"\xC3\xAA", 'e'}, {"\xC3\xAB", 'e'},
        {"\xC3\xAE", 'i'}, {"\xC3\xAF", 'i'},
        {"\xC3\xB4", 'o'}, {"\xC3\xB6", 'o'},
        {"\xC3\xB9", 'u'}, {"\xC3\xBB", 'u'}, {"\xC3\xBC", 'u'},
    };

    std::string out;
    out.reserve(name.size());
    for (std::size_t i = 0; i < name.size();) {
        bool folded = false;
        for (const auto& fold : kFolds) {
            const std::size_t n = std::char_traits<char>::length(fold.from);
            if (name.compare(i, n, fold.from) == 0) {
                out.push_back(fold.to);
                i += n;
                folded = true;
                break;
            }
        }
        if (folded) continue;
        const auto c = static_cast<unsigned char>(name[i]);
        // Spaces, underscores and hyphens carry no meaning in a column name, and
        // they are exactly what differs between two people typing the same one.
        if (!(c == ' ' || c == '_' || c == '-' || c == '\t'))
            out.push_back(static_cast<char>(std::tolower(c)));
        ++i;
    }
    return out;
}

// ---------------------------------------------------------------------------
namespace {

// RFC 4180, including the part everyone forgets: a quoted field may contain the
// separator, a doubled quote, AND a newline. The Module caption is four lines in
// one cell, and a reader that splits on '\n' first misaligns every row after it.
std::vector<std::vector<std::string>> split(const std::string& text, char separator) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') { field.push_back('"'); ++i; }
                else quoted = false;
            } else {
                field.push_back(c);
            }
            continue;
        }
        if (c == '"' && field.empty()) { quoted = true; continue; }
        if (c == separator) { row.push_back(field); field.clear(); continue; }
        if (c == '\r') continue;
        if (c == '\n') {
            row.push_back(field);
            field.clear();
            rows.push_back(row);
            row.clear();
            continue;
        }
        field.push_back(c);
    }
    if (!field.empty() || !row.empty()) {
        row.push_back(field);
        rows.push_back(row);
    }
    return rows;
}

// Which separator this file uses. Counted outside quotes, over the first lines,
// because a French Excel writes ';' and an English one ','. Hard-coding either
// makes the tool work on one colleague's machine and not the next one's.
char detectSeparator(const std::string& text) {
    const char candidates[] = {';', ',', '\t', '|'};
    char best = ';';
    std::size_t bestCount = 0;
    for (char candidate : candidates) {
        std::size_t count = 0, lines = 0;
        bool quoted = false;
        for (char c : text) {
            if (c == '"') { quoted = !quoted; continue; }
            if (quoted) continue;
            if (c == candidate) ++count;
            if (c == '\n') { ++lines; if (lines > 40) break; }
        }
        if (count > bestCount) { bestCount = count; best = candidate; }
    }
    return best;
}

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

} // namespace

// ---------------------------------------------------------------------------
Table Table::parse(const std::string& bytes, const TableOptions& options) {
    Table table;
    const std::string text = toUtf8(bytes);
    table.separator_ = options.separator ? options.separator : detectSeparator(text);

    const auto raw = split(text, table.separator_);
    if (raw.empty()) {
        table.warnings_.emplace_back("the file is empty");
        return table;
    }

    // ---- where the table actually starts --------------------------------
    std::vector<std::string> anchorKeys;
    for (const auto& a : options.anchors) anchorKeys.push_back(normaliseKey(a));

    std::size_t header = Table::npos;
    if (!anchorKeys.empty()) {
        for (std::size_t r = 0; r < raw.size() && header == Table::npos; ++r) {
            std::size_t found = 0;
            for (const auto& want : anchorKeys)
                for (const auto& cell : raw[r])
                    if (normaliseKey(trim(cell)) == want) { ++found; break; }
            if (found == anchorKeys.size()) header = r;
        }
        if (header == Table::npos) {
            // WHICH SHEET IS THIS, THEN? "Save as CSV" exports the ACTIVE SHEET
            // and nothing else, so the commonest mistake by far is exporting the
            // workbook with Config on screen. Answering "no columns found" makes
            // the reader check their columns, which are fine. Naming the sheet
            // they actually exported ends it in one sentence.
            std::string looksLike;
            auto sawAnywhere = [&](std::string_view what) {
                const auto key = normaliseKey(what);
                for (const auto& row : raw)
                    for (const auto& cell : row)
                        if (normaliseKey(trim(cell)) == key) return true;
                return false;
            };
            if (sawAnywhere("TailleDFB") || sawAnywhere("Module_txt")
                || sawAnywhere("Objet de la modification"))
                looksLike = "on dirait l'onglet Config";
            else if (sawAnywhere("S0_Val") || sawAnywhere("RawMin"))
                looksLike = "les colonnes d'E/S sont la, mais pas toutes celles attendues";

            std::string wanted;
            for (const auto& a : options.anchors)
                wanted += (wanted.empty() ? "" : ", ") + a;

            table.warnings_.emplace_back(
                "aucune ligne ne porte toutes les colonnes attendues (" + wanted + ")."
                + (looksLike.empty() ? std::string{} : " " + looksLike + ".")
                + " Rappel : Enregistrer sous CSV n'exporte QUE la feuille active - "
                  "ouvrez l'onglet Entrees TOR, Sorties TOR, Entrees ANA ou Sorties ANA, "
                  "puis exportez.");
            return table;
        }
    } else {
        // No anchors: the widest row with the most distinct non-empty cells. A
        // guess, and it says so.
        std::size_t bestScore = 0;
        for (std::size_t r = 0; r < raw.size() && r < 40; ++r) {
            std::size_t score = 0;
            for (const auto& cell : raw[r]) if (!trim(cell).empty()) ++score;
            if (score > bestScore) { bestScore = score; header = r; }
        }
        if (header == Table::npos) header = 0;
        table.warnings_.emplace_back(
            "no anchor columns were given, so the header row was guessed (line "
            + std::to_string(header + 1) + ")");
    }

    table.headerLine_ = header + 1;
    for (const auto& cell : raw[header]) table.headers_.push_back(trim(cell));
    for (const auto& name : table.headers_) table.keys_.push_back(normaliseKey(name));

    // Two columns with the same name is a question with no right answer, so it
    // is reported and the first one wins.
    //
    // The first version also blanked the duplicate key, which reads like a
    // safeguard and is not one: column() scans from the start and returns on the
    // first match, so the duplicate was already unreachable. A mutation run
    // showed the line could be deleted with no effect on anything observable,
    // which is the definition of dead code.
    for (std::size_t i = 0; i < table.keys_.size(); ++i) {
        if (table.keys_[i].empty()) continue;
        for (std::size_t j = i + 1; j < table.keys_.size(); ++j)
            if (table.keys_[i] == table.keys_[j])
                table.warnings_.emplace_back("two columns are called '" + table.headers_[i]
                                             + "'; the first one is used");
    }

    // ---- the rows --------------------------------------------------------
    std::size_t from = header + 1;
    for (std::size_t i = 0; i < options.descriptionRows && from < raw.size(); ++i) {
        // Said out loud. A row skipped in silence is a channel that vanishes
        // without anybody being able to see where it went.
        std::string shown;
        for (std::size_t c = 0; c < raw[from].size() && c < 4; ++c) {
            if (!shown.empty()) shown += " | ";
            shown += trim(raw[from][c]);
        }
        table.warnings_.emplace_back("line " + std::to_string(from + 1)
                                     + " treated as a description row: " + shown);
        ++from;
    }

    std::size_t ragged = 0, blancs = 0;
    for (std::size_t r = from; r < raw.size(); ++r) {
        // A sheet is padded with hundreds of formatted but empty rows. Every one
        // of them would otherwise become a channel.
        bool empty = true;
        for (const auto& want : anchorKeys) {
            const auto at = std::find(table.keys_.begin(), table.keys_.end(), want);
            if (at == table.keys_.end()) continue;
            const auto column = static_cast<std::size_t>(at - table.keys_.begin());
            if (column < raw[r].size() && !trim(raw[r][column]).empty()) empty = false;
        }
        if (anchorKeys.empty()) {
            empty = true;
            for (const auto& cell : raw[r]) if (!trim(cell).empty()) { empty = false; break; }
        }
        if (empty) {
            // Le bloc de donnees s'arrete ou les blancs commencent - quand
            // l'appelant l'a demande.
            if (options.stopAfterBlankRows > 0
                && ++blancs >= options.stopAfterBlankRows) {
                std::size_t restantes = 0;
                for (std::size_t k = r + 1; k < raw.size(); ++k)
                    for (const auto& cell : raw[k])
                        if (!trim(cell).empty()) { ++restantes; break; }
                if (restantes > 0)
                    table.warnings_.emplace_back(
                        "arret a la ligne " + std::to_string(r + 1)
                        + " (ligne vide) ; " + std::to_string(restantes)
                        + " ligne(s) non vides suivent et ont ete ignorees");
                break;
            }
            continue;
        }
        blancs = 0;

        if (raw[r].size() != table.headers_.size()) ++ragged;
        table.rows_.push_back(raw[r]);
    }
    if (ragged > 0)
        table.warnings_.emplace_back(std::to_string(ragged)
                                     + " row(s) do not have as many fields as the header");
    return table;
}

std::size_t Table::column(std::string_view name) const {
    const auto key = normaliseKey(name);
    for (std::size_t i = 0; i < keys_.size(); ++i)
        if (!keys_[i].empty() && keys_[i] == key) return i;
    return npos;
}

std::string Table::cell(std::size_t row, std::size_t column) const {
    if (row >= rows_.size() || column >= rows_[row].size()) return {};
    return trim(rows_[row][column]);
}

std::string Table::cell(std::size_t row, std::string_view name) const {
    const auto column = this->column(name);
    if (column == npos) return {};
    return cell(row, column);
}

} // namespace project
