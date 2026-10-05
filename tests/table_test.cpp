// =============================================================================
//  tests/table_test.cpp — reading a sheet that came out of Excel
// -----------------------------------------------------------------------------
//  The workbook is filled in by hand and exported with "Save as CSV". That file
//  is not the clean CSV a parser would like, and every case below is something
//  a real export actually does:
//
//    the header is on line 7, under logos, a title and a group band;
//    the separator is ';' on a French machine and ',' on an English one;
//    the Module caption is four lines inside one quoted field;
//    the bytes are CP1252, so an accented designation is not valid UTF-8;
//    three hundred formatted but empty rows follow the data.
//
//  Each of those, read wrong, produces a table that still LOOKS like a table.
//  That is why they are tested rather than eyeballed.
// =============================================================================
#include "../src/project/Table.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

using namespace project;

namespace {

std::string read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

TableOptions ioOptions() {
    TableOptions o;
    o.anchors = {"Carte", "Designation", "Adresse"};
    o.descriptionRows = 1;          // our sheets carry one; a plain CSV carries none
    return o;
}

TableOptions plainOptions() {
    TableOptions o;
    o.anchors = {"Carte", "Designation", "Adresse"};
    o.descriptionRows = 0;
    return o;
}

} // namespace

int main(int argc, char** argv) {
    // ---- normalisation ------------------------------------------------------
    //
    //  Two people typing the same column name do not type the same characters.
    assert(normaliseKey("EngMax") == normaliseKey("Eng Max"));
    assert(normaliseKey("EngMax") == normaliseKey("eng_max"));
    assert(normaliseKey("EngMax") == normaliseKey(" ENG-MAX "));
    assert(normaliseKey("Unite") == normaliseKey("Unit\xC3\xA9")
           && "an accent is not a different column");
    assert(normaliseKey("Designation") == normaliseKey("D\xC3\xA9signation"));
    assert(normaliseKey("RawMin") != normaliseKey("RawMax")
           && "but a different name is a different column");

    // ---- encoding -----------------------------------------------------------
    {
        // Valid UTF-8 must come back byte for byte. Converting a file that did
        // not need it is how you turn "é" into "Ã©" on the second save.
        const std::string utf8 = "D\xC3\xA9toxal";
        assert(toUtf8(utf8) == utf8);

        // CP1252: a lone 0xE9 is not valid UTF-8, and Excel writes it.
        const std::string cp1252 = "D\xE9toxal";
        assert(toUtf8(cp1252) == utf8 && "an accented designation must survive the export");

        // The BOM Excel writes for "CSV UTF-8" must not become part of the first
        // column's name.
        //
        // The literal is split on purpose: \x eats as many hex digits as it can,
        // and the C of "Carte" is one of them. Written in one piece this is
        // \xBFC, which does not fit in a char and is not what anyone meant.
        assert(toUtf8("\xEF\xBB\xBF" "Carte") == "Carte");

        // The sixteen CP1252 bytes that are not Latin-1.
        assert(toUtf8("\x92") == "\xE2\x80\x99" && "a curly apostrophe is one of them");
    }

    // ---- a description row is DECLARED, not detected ------------------------
    //
    //  It cannot be recognised by looking at it: "liste Config | calcule |
    //  libelle" is as non-empty as any channel. A reader that guesses here
    //  invents a channel or loses one, and both happen in silence - so the
    //  caller says how many there are, and the reader reports what it skipped.
    {
        const std::string csv =
            "Carte;Designation;Adresse\n"
            "liste Config;libelle;calcule\n"
            "AI_R0S10;PT1_A;%IW0.10.0\n";
        assert(Table::parse(csv, ioOptions()).rowCount() == 1);
        assert(Table::parse(csv, plainOptions()).rowCount() == 2);
        // Bound to a name on purpose: warnings() hands back a reference into the
        // Table, and iterating over a temporary's reference reads memory that
        // died at the end of the expression. ASan caught it; the compiler did not.
        const auto skipped = Table::parse(csv, ioOptions());
        bool said = false;
        for (const auto& w : skipped.warnings())
            if (w.find("description") != std::string::npos) said = true;
        assert(said && "a skipped row must be named, never dropped in silence");
    }

    // ---- the shape of a real export ----------------------------------------
    {
        const std::string csv =
            ";ENTREES ANALOGIQUES;;;;;;;\r\n"
            ";;;;;;;;\r\n"
            "Affaire;2024_06_264;;;Client;ALI;;;\r\n"
            "Indice;A;;;Le;12/09/2026;;;\r\n"
            "Identification;;;;;;;;\r\n"
            "Carte;Module;Designation;Voie;Adresse;Tableau;Index;Unite\r\n"
            "liste Config;calcule;libelle;Z;calcule;calcule;calcule;bars\r\n"
            "AI_R0S10;\"M340\nvoie 10\nAMI 0810\n%IW\";PT1_A;0;%IW0.10.0;CarteAI;0;bars\r\n"
            "AI_R0S10;;PT3_A;1;%IW0.10.1;CarteAI;1;bars\r\n"
            ";;;;;;;\r\n"
            ";;;;;;;\r\n";

        const auto t = Table::parse(csv, ioOptions());
        assert(t.headerLine() == 6 && "the header is not the first line, and is found");
        assert(t.separator() == ';');
        assert(t.columnCount() == 8);

        // The description row under the headers is not a channel.
        assert(t.rowCount() == 2 && "two channels, not four, and not three");

        // The four-line caption did not misalign anything after it.
        assert(t.cell(0, "Designation") == "PT1_A");
        assert(t.cell(0, "Adresse") == "%IW0.10.0");
        assert(t.cell(1, "Designation") == "PT3_A"
               && "the row after a multi-line field must still line up");
        assert(t.cell(1, "Adresse") == "%IW0.10.1");
        assert(t.cell(0, "Module").find("AMI 0810") != std::string::npos
               && "and the caption itself is kept whole");

        // Columns by name, whatever the caller types.
        assert(t.cell(0, "unite") == "bars");
        assert(t.cell(0, "Unit\xC3\xA9") == "bars");
        assert(!t.has("EngMax") && "a column that is not there is not there");
        assert(t.cell(0, "EngMax").empty() && "and asking for it answers, rather than crashing");
    }

    // ---- an English machine -------------------------------------------------
    {
        const std::string csv =
            "Carte,Designation,Adresse\n"
            "AI_R0S10,\"PT1, amont\",%IW0.10.0\n";
        const auto t = Table::parse(csv, plainOptions());
        assert(t.separator() == ',' && "the separator is detected, not assumed");
        assert(t.rowCount() == 1);
        assert(t.cell(0, "Designation") == "PT1, amont"
               && "a separator inside quotes is text, not a new column");
    }

    // ---- quoting, in full ---------------------------------------------------
    {
        const std::string csv =
            "Carte;Designation;Adresse\n"
            "A;\"il a dit \"\"non\"\"\";%I0.0.0\n"
            "B;\"deux\nlignes\";%I0.0.1\n";
        const auto t = Table::parse(csv, plainOptions());
        assert(t.rowCount() == 2);
        assert(t.cell(0, "Designation") == "il a dit \"non\"");
        assert(t.cell(1, "Designation") == "deux\nlignes");
        assert(t.cell(1, "Adresse") == "%I0.0.1");
    }

    // ---- the columns were reordered ----------------------------------------
    //
    //  Somebody exports a sheet whose columns were moved, or inserts one. Reading
    //  by position would then attach every designation to the wrong address -
    //  silently, because the result is still a well-formed table.
    {
        const std::string csv =
            "Adresse;Unite;Carte;Designation\n"
            "%IW0.10.0;bars;AI_R0S10;PT1_A\n";
        const auto t = Table::parse(csv, plainOptions());
        assert(t.cell(0, "Carte") == "AI_R0S10");
        assert(t.cell(0, "Designation") == "PT1_A");
        assert(t.cell(0, "Adresse") == "%IW0.10.0");
    }

    // ---- things that are wrong are reported, never guessed -----------------
    {
        const auto missing = Table::parse("a;b;c\n1;2;3\n", ioOptions());
        assert(missing.rowCount() == 0);
        assert(!missing.warnings().empty()
               && "a file whose header cannot be found must say so");

        const std::string duplicated =
            "Carte;Designation;Adresse;Designation\n"
            "A;premier;%I0.0.0;second\n";
        const auto twice = Table::parse(duplicated, plainOptions());
        assert(twice.cell(0, "Designation") == "premier" && "the first one wins");
        assert(!twice.warnings().empty() && "and it is reported");

        const auto empty = Table::parse("", ioOptions());
        assert(empty.rowCount() == 0 && !empty.warnings().empty());

        // No anchors at all: the header is guessed, and the guess is declared.
        const auto guessed = Table::parse("x;y\n1;2\n", TableOptions{});
        assert(!guessed.warnings().empty() && "a guess must announce itself");
    }

    // ---- the real file, when one was given ---------------------------------
    if (argc > 1) {
        const auto bytes = read(argv[1]);
        assert(!bytes.empty() && "the export could not be read");
        const auto t = Table::parse(bytes, ioOptions());
        std::printf("export: separateur '%c', en-tete ligne %zu, %zu colonnes, %zu lignes\n",
                    t.separator(), t.headerLine(), t.columnCount(), t.rowCount());
        for (const auto& w : t.warnings()) std::printf("  ! %s\n", w.c_str());

        assert(t.rowCount() > 0 && "a real export must yield channels");
        assert(t.has("Adresse") && t.has("EngMax") && t.has("S0_Val"));
        for (std::size_t r = 0; r < t.rowCount(); ++r) {
            const auto address = t.cell(r, "Adresse");
            assert(address.rfind("%IW", 0) == 0
                   && "every analogue input row must carry a %IW address");
            assert(!t.cell(r, "Carte").empty());
        }
        std::printf("  premiere voie : %s  %s  %s\n",
                    t.cell(0, "Carte").c_str(), t.cell(0, "Adresse").c_str(),
                    t.cell(0, "Designation").c_str());
    }

    std::printf("table_test: ok\n");
    return 0;
}
