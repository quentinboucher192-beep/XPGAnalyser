// =============================================================================
//  project/Table.hpp — reading a sheet that came out of Excel
// -----------------------------------------------------------------------------
//  The workbook is filled in by hand and exported with "Save as CSV". That one
//  sentence sets every requirement here, because Excel's CSV is not the clean
//  file a parser would like:
//
//    THE HEADER IS NOT THE FIRST LINE. The sheet has logos, a title, an affair
//    block and a group band above it - eight lines before the column names. A
//    parser that assumes line 1 reads "ENTREES ANALOGIQUES" as a column name and
//    everything after it is nonsense.
//
//    THE SEPARATOR DEPENDS ON THE MACHINE. A French Excel writes ';', an English
//    one ','. Hard-coding either makes the tool work on one colleague's PC and
//    not the next one's, which is the worst kind of bug to diagnose remotely.
//
//    A FIELD CAN CONTAIN NEWLINES. The Module caption is "M340 / voie 10 / AMI
//    0810 / %IW" on four lines, quoted. Splitting the file on '\n' misaligns
//    every row after it - silently, because the result is still a table.
//
//    THE BYTES ARE NOT UTF-8. Excel writes the system code page; on Windows in
//    France that is CP1252. "Détoxal" arrives as two bytes that are not valid
//    UTF-8, and a reader that assumes UTF-8 produces mojibake in the designations
//    that end up on the drawings.
//
//  COLUMNS ARE FOUND BY NAME, NEVER BY POSITION. Somebody will insert a column,
//  or export a sheet whose columns were reordered. The name is matched loosely -
//  case, accents, spaces and underscores are ignored - so "Eng Max", "engmax"
//  and "EngMax" are the same column, while a genuinely missing one is reported
//  rather than guessed at.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace project {

struct TableOptions {
    // The columns that identify the header row. The first row holding all of
    // them is the header; without this the reader would have to guess, and a
    // guess about which line the table starts on is a guess about everything.
    std::vector<std::string> anchors;
    char separator{'\0'};      // 0 = detect it

    // How many rows under the header are commentary rather than data. Our sheets
    // carry one - the italic line explaining each column.
    //
    // This is a NUMBER THE CALLER STATES, not something the reader detects. The
    // first version tried to recognise the row by asking whether its cells were
    // empty; they are not ("liste Config", "libelle", "calcule"), so it kept it
    // and produced one channel too many. A description row is not distinguishable
    // from data by looking at it, and a reader that guesses here either invents a
    // channel or loses one - both silently. So it is declared, and the reader
    // says in its warnings which row it skipped.
    std::size_t descriptionRows{1};

    // Combien de lignes VIDES d'affilee terminent le tableau. 0 desactive.
    //
    // POURQUOI CA EXISTE. Une feuille porte souvent une note sous ses donnees -
    // "lignes generees pour chaque carte declaree..." - et cette note est dans
    // la premiere colonne, qui est justement une colonne d'ancrage. Elle devient
    // donc une ligne de donnees dont la carte s'appelle "Lignes generees pour
    // chaque carte declaree...".
    //
    // La regle est : le bloc de donnees s'arrete ou les blancs commencent. Elle
    // est SUR pour une feuille dont les lignes se suivent, et elle tronquerait
    // une feuille trouee - d'ou le reglage plutot qu'une decision en dur, et
    // d'ou le defaut a 0, qui ne change rien a ce qui existait.
    std::size_t stopAfterBlankRows{0};
};

class Table {
public:
    // Never fails. A file that makes no sense yields an empty table and a list
    // of reasons - a macro that stops with "could not read the table" and no
    // explanation is a macro nobody can fix.
    [[nodiscard]] static Table parse(const std::string& bytes, const TableOptions& = {});

    [[nodiscard]] std::size_t rowCount() const noexcept { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const noexcept { return headers_.size(); }
    [[nodiscard]] const std::vector<std::string>& headers() const noexcept { return headers_; }
    [[nodiscard]] char separator() const noexcept { return separator_; }
    [[nodiscard]] std::size_t headerLine() const noexcept { return headerLine_; }

    // The column with this name, or npos. Matching ignores case, accents,
    // spaces, underscores and hyphens.
    [[nodiscard]] std::size_t column(std::string_view name) const;
    [[nodiscard]] bool has(std::string_view name) const { return column(name) != npos; }

    // Empty for a row or column that is not there. A macro asking for a column
    // that does not exist gets "" and a warning, not a crash.
    [[nodiscard]] std::string cell(std::size_t row, std::string_view name) const;
    [[nodiscard]] std::string cell(std::size_t row, std::size_t column) const;

    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return warnings_; }

    // Rows whose anchor columns are all empty are dropped: an Excel sheet is
    // padded with hundreds of formatted but empty rows, and every one of them
    // would otherwise become a channel.
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

private:
    std::vector<std::string>              headers_;
    std::vector<std::string>              keys_;      // normalised, for lookup
    std::vector<std::vector<std::string>> rows_;
    std::vector<std::string>              warnings_;
    char                                  separator_{';'};
    std::size_t                           headerLine_{0};
};

// Exposed because they are worth testing on their own, and because a macro
// function will want the same normalisation when it matches a name.
[[nodiscard]] std::string normaliseKey(std::string_view);
[[nodiscard]] std::string toUtf8(const std::string& bytes);   // CP1252 when not UTF-8

} // namespace project
