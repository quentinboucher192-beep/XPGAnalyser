// tests/fileview_test.cpp - 1.21.0 : l'apercu de tous les fichiers, sans ecran (app/FileView).
//
//   fileview_test
//
//  1. reconnaitre : l'extension, puis la signature (un PNG nomme .jpg, un .txt qui est un zip, un exe, un binaire
//     inconnu, un fichier sans extension) ;
//  2. le texte borne et ses encodages : UTF-8, UTF-8 avec BOM, UTF-16 LE / BE, Windows-1252 ; les fins de ligne,
//     les tabulations, une ligne trop longue, un texte coupe a N lignes ;
//  3. CSV : le separateur devine, les guillemets, les lignes comptees au-dela de la borne ;
//  4. le zip : ses entrees (taille, compresse, dossiers) ; un zip abime ;
//  5. le vidage hexadecimal ;
//  6. un classeur XLSX fabrique ici : ses onglets, ses cellules (chaines partagees), le masque, les macros ;
//  7. un DOCX fabrique ici : les titres (par le nom du style, par son identifiant), les listes, les tableaux ;
//  8. JSON et XML indentes, repliables ; mal formes : dits ;
//  9. load() sur le disque : texte, CSV, image, zip, dossier, introuvable, fichier vide ; une entree lue a la demande
//     (fetch) ; une ressource en memoire ; ce qui va sur le fil de fond (slow) ;
// 10. les fichiers d'essai de l'IHM (tests/fixtures/ihm, s'il est donne) : une police et son echantillon, un son et son
//     onde, un GIF anime et ses images, un SVG, un classeur, une video, une base SQLite, un TSV.
#include "../src/app/FileView.hpp"
#include "../third_party/miniz.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

namespace fs = std::filesystem;
namespace fv = app::fileview;

std::string u8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

void put(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << data;
}

const std::uint8_t* U(const std::string& s) { return reinterpret_cast<const std::uint8_t*>(s.data()); }

// Un zip en memoire, ses entrees (nom, contenu) ; un nom finissant par / : un dossier.
std::string zipOf(const std::vector<std::pair<std::string, std::string>>& entries) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) return {};
    for (const auto& [name, data] : entries)
        (void)mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
    void* buf = nullptr;
    size_t size = 0;
    (void)mz_zip_writer_finalize_heap_archive(&zip, &buf, &size);
    std::string out(static_cast<const char*>(buf), size);
    mz_free(buf);
    mz_zip_writer_end(&zip);
    return out;
}

// Une image PNG de 2 x 1 pixels (rouge, bleu), ecrite a la main : signature, IHDR, IDAT (zlib stocke), IEND.
std::string tinyPng() {
    const auto be32 = [](std::uint32_t v) {
        std::string s(4, '\0');
        s[0] = static_cast<char>(v >> 24);
        s[1] = static_cast<char>(v >> 16);
        s[2] = static_cast<char>(v >> 8);
        s[3] = static_cast<char>(v);
        return s;
    };
    const auto chunk = [&](const std::string& type, const std::string& data) {
        const std::string td = type + data;
        const mz_ulong crc = mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(td.data()), td.size());
        return be32(static_cast<std::uint32_t>(data.size())) + td + be32(static_cast<std::uint32_t>(crc));
    };
    std::string ihdr = be32(2) + be32(1) + std::string("\x08\x02\x00\x00\x00", 5);
    const std::string raw = std::string("\x00\xFF\x00\x00\x00\x00\xFF", 7);   // filtre 0, rouge, bleu
    mz_ulong len = mz_compressBound(static_cast<mz_ulong>(raw.size()));
    std::string z(len, '\0');
    (void)mz_compress(reinterpret_cast<unsigned char*>(z.data()), &len, reinterpret_cast<const unsigned char*>(raw.data()),
                      static_cast<mz_ulong>(raw.size()));
    z.resize(len);
    return std::string("\x89PNG\r\n\x1A\n", 8) + chunk("IHDR", ihdr) + chunk("IDAT", z) + chunk("IEND", "");
}

void reconnaitre() {
    std::printf("1. reconnaitre : l'extension, puis la signature\n");
    const std::string png = tinyPng();
    auto r = fv::recognize("logo.png", U(png), png.size());
    check(r.kind == fv::Kind::Image && r.format == "PNG" && !r.mismatch, "logo.png : une image PNG (" + r.label + ")");
    r = fv::recognize("photo.jpg", U(png), png.size());
    check(r.kind == fv::Kind::Image && r.format == "PNG" && r.mismatch && r.label == "Image PNG", "photo.jpg qui est un PNG : dit (" + r.note + ")");
    const std::string zip = zipOf({{"a.txt", "bonjour"}});
    r = fv::recognize("notes.txt", U(zip), zip.size());
    check(r.kind == fv::Kind::Zip && r.mismatch, "notes.txt qui est un zip : une archive (" + r.note + ")");
    r = fv::recognize("paquet.xihm", U(zip), zip.size());
    check(r.kind == fv::Kind::Zip && !r.mismatch, "paquet.xihm : un zip, son extension gardee");
    const std::string docx = zipOf({{"[Content_Types].xml", "<Types/>"}, {"word/document.xml", "<w:document/>"}});
    r = fv::recognize("rapport", U(docx), docx.size());
    check(r.kind == fv::Kind::Docx, "un DOCX sans extension : reconnu a ses noms d'entrees (" + r.label + ")");
    std::string exe(256, '\0');
    exe[0] = 'M';
    exe[1] = 'Z';
    exe[0x3C] = static_cast<char>(0x80);
    std::memcpy(exe.data() + 0x80, "PE\0\0", 4);
    r = fv::recognize("outil.exe", U(exe), exe.size());
    check(r.kind == fv::Kind::Binary && r.label == "Application" && fv::signatureOf(U(exe), exe.size()).format == "EXE",
          "outil.exe : un binaire, son libelle (" + r.label + "), sa signature PE");
    r = fv::recognize("inconnu.bin", U(exe), exe.size());
    check(r.kind == fv::Kind::Binary && r.label.find("Programme Windows") == 0, "inconnu.bin : la signature dit un programme (" + r.label + ")");
    const std::string text = "Bonjour\nle monde\n";
    r = fv::recognize("LISEZMOI", U(text), text.size());
    check(r.kind == fv::Kind::Text, "LISEZMOI (sans extension) : du texte");
    const std::string mz = "MZ-12 n'est pas un programme, c'est une note assez longue pour avoir soixante-quatre octets au moins.";
    check(fv::recognize("note.txt", U(mz), mz.size()).kind == fv::Kind::Text, "un texte qui commence par MZ reste un texte");
    const std::string tru = "true\nfalse\n";
    check(fv::recognize("vrai.txt", U(tru), tru.size()).kind == fv::Kind::Text, "un texte qui commence par true n'est pas une police");
    const std::string bin = std::string("\x01\x02\x00\x03\x04", 5) + std::string(100, '\x05');
    r = fv::recognize("trace.log", U(bin), bin.size());
    check(r.kind == fv::Kind::Binary && r.mismatch, "trace.log qui n'est pas du texte : en hexad\xC3\xA9" "cimal (" + r.note + ")");
    r = fv::recognize("image.png", U(text), text.size());
    check(r.kind == fv::Kind::Text && r.mismatch, "image.png qui est du texte : dit, montr\xC3\xA9 comme du texte");
    const std::string pdf = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    check(fv::recognize("notice.pdf", U(pdf), pdf.size()).kind == fv::Kind::Pdf, "notice.pdf : un PDF");
    const std::string svg = "<?xml version=\"1.0\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"/>";
    check(fv::recognize("pompe.svg", U(svg), svg.size()).kind == fv::Kind::Image, "pompe.svg : une image");
    const std::string xpg = "<?xml version=\"1.0\"?>\n<FEFExchangeFile/>";
    check(fv::recognize("MAST.XPG", U(xpg), xpg.size()).kind == fv::Kind::ControlExpert, "MAST.XPG : un export Control Expert (du XML)");
    check(fv::kindOfName("script.ps1") == fv::Kind::Text && fv::kindOfName("main.cpp") == fv::Kind::Text && fv::kindOfName("a.ST") == fv::Kind::Text
              && fv::kindOfName("x.tsv") == fv::Kind::Csv && fv::kindOfName("x.xlsm") == fv::Kind::Workbook && fv::kindOfName("x.otf") == fv::Kind::Font,
          "les extensions : PS1, CPP, ST (texte), TSV, XLSM, OTF");
    const std::string wav = std::string("RIFF\x24\x00\x00\x00WAVEfmt ", 16);
    check(fv::signatureOf(U(wav), wav.size()).kind == fv::Kind::Sound, "RIFF/WAVE : un son");
}

void texte() {
    std::printf("2. le texte borne, ses encodages\n");
    const std::string utf8 = "Temp\xC3\xA9rature\r\nD\xC3\xA9" "bit\r\n";
    auto tv = fv::readText(utf8);
    check(tv.encoding == fv::Encoding::Utf8 && tv.lines.size() == 2 && tv.lines[0] == "Temp\xC3\xA9rature" && tv.endings == "CRLF (Windows)",
          "UTF-8, CRLF : deux lignes");
    tv = fv::readText("\xEF\xBB\xBF" "abc\n");
    check(tv.encoding == fv::Encoding::Utf8Bom && tv.lines.size() == 1 && tv.lines[0] == "abc" && tv.endings == "LF (Unix)", "UTF-8 avec BOM : la marque retiree");
    const std::string cp = "Temp\xE9rature 21\xB0" "C \x80\n";
    tv = fv::readText(cp);
    check(tv.encoding == fv::Encoding::Windows1252 && tv.lines[0] == "Temp\xC3\xA9rature 21\xC2\xB0" "C \xE2\x82\xAC",
          "Windows-1252 : \xC3\xA9, \xC2\xB0 et \xE2\x82\xAC convertis (" + (tv.lines.empty() ? std::string() : tv.lines[0]) + ")");
    std::string le = "\xFF\xFE";
    for (const char c : std::string("Ligne 1\nLigne 2\n")) {
        le += c;
        le += '\0';
    }
    tv = fv::readText(le);
    check(tv.encoding == fv::Encoding::Utf16Le && tv.lines.size() == 2 && tv.lines[1] == "Ligne 2", "UTF-16 LE (avec BOM)");
    std::string be;
    for (const char c : std::string("Ab\xE9")) {   // sans BOM : A, b, e accent aigu (U+00E9)
        be += '\0';
        be += c;
    }
    check(fv::detectEncoding(be + std::string("\0x\0y\0z", 6)) == fv::Encoding::Utf16Be && fv::toUtf8(be, fv::Encoding::Utf16Be) == "Ab\xC3\xA9",
          "UTF-16 BE sans BOM : devin\xC3\xA9");
    check(!fv::looksBinary(le) && fv::looksBinary(std::string("ab\0cd", 5)), "UTF-16 n'est pas binaire ; un octet nul l'est");
    tv = fv::readText("a\tb\n\tc\n");
    check(tv.lines[0] == "a   b" && tv.lines[1] == "    c", "les tabulations : colonnes de 4");
    fv::Limits lim;
    lim.textLines = 3;
    lim.lineChars = 10;
    tv = fv::readText("1\n2\n3\n4\n5\n" + std::string(50, 'x') + "\n", lim);
    check(tv.truncated && tv.lines.size() == 3, "coup\xC3\xA9 \xC3\xA0 3 lignes (et dit)");
    tv = fv::readText(std::string(50, 'x'), lim);
    check(tv.lines.size() == 1 && tv.lines[0] == std::string(10, 'x') + "\xE2\x80\xA6", "une ligne trop longue : coup\xC3\xA9" "e, des points de suite");
    tv = fv::readText("partiel\n\xC3", {}, false);
    check(tv.truncated && tv.lines.size() >= 1 && tv.encoding == fv::Encoding::Utf8, "un morceau coup\xC3\xA9 au milieu d'un caract\xC3\xA8re : toujours de l'UTF-8");
    tv = fv::readText("a\r\nb\nc\n");
    check(tv.endings.find("lang") != std::string::npos, "fins de ligne m\xC3\xA9lang\xC3\xA9" "es : dites");
}

void csv() {
    std::printf("3. CSV : le separateur, les guillemets, la borne\n");
    check(fv::guessSeparator("a;b;c\n1;2;3\n") == ';' && fv::guessSeparator("a,b,c\n1,2,3\n") == ',' && fv::guessSeparator("a\tb\n1\t2\n") == '\t'
              && fv::guessSeparator("a|b\n1|2\n") == '|',
          "le s\xC3\xA9parateur devin\xC3\xA9 : ; , tabulation |");
    check(fv::guessSeparator("nom;valeur\n\"1,5\";\"a, b\"\n") == ';', "des virgules dans les guillemets ne trompent pas");
    auto sh = fv::parseCsv("Nom;Valeur\n\"Pompe; P1\";\"dit \"\"oui\"\"\"\n\"sur\ndeux lignes\";3\n", ';');
    check(sh.rows.size() == 3 && sh.rows[1][0] == "Pompe; P1" && sh.rows[1][1] == "dit \"oui\"" && sh.rows[2][0] == "sur deux lignes" && sh.columns == 2,
          "guillemets, \"\" et retour \xC3\xA0 la ligne dans une cellule");
    fv::Limits lim;
    lim.gridRows = 10;
    std::string big = "A;B\n";
    for (int i = 0; i < 100; ++i) big += std::to_string(i) + ";x\n";
    sh = fv::parseCsv(big, ';', lim);
    check(sh.rows.size() == 10 && sh.totalRows == 101 && sh.truncated, "101 lignes, 10 gard\xC3\xA9" "es, toutes compt\xC3\xA9" "es");
}

void zips() {
    std::printf("4. le zip : ses entrees\n");
    const std::string zip = zipOf({{"lisezmoi.txt", std::string(1000, 'a')}, {"donnees/", ""}, {"donnees/mesures.csv", "a;b\n1;2\n"}});
    std::vector<fv::ZipItem> items;
    std::size_t total = 0;
    std::string why;
    check(fv::listZip(zip, items, total, why) && total == 3 && items.size() == 3, "trois entr\xC3\xA9" "es");
    check(items.size() == 3 && items[0].name == "lisezmoi.txt" && items[0].size == 1000 && items[0].packed < 1000 && items[1].folder,
          "nom, taille, compress\xC3\xA9, le dossier");
    fv::Limits lim;
    lim.zipEntries = 2;
    check(fv::listZip(zip, items, total, why, lim) && items.size() == 2 && total == 3, "born\xC3\xA9 \xC3\xA0 2, les 3 compt\xC3\xA9" "es");
    const bool broken = !fv::listZip("PK\x03\x04 pas un zip du tout, vraiment pas", items, total, why);
    check(broken && !why.empty(), "un zip ab\xC3\xAEm\xC3\xA9 : " + why);
}

void hexa() {
    std::printf("5. le vidage hexadecimal\n");
    const auto lines = fv::hexDump(std::string("MZ\x90\x00", 4) + "ABCDEFGHIJKLMNOPQRSTU", 4096);
    check(lines.size() == 2 && lines[0].rfind("00000000  4D 5A 90 00 41 42 43 44  45 46", 0) == 0 && lines[0].find("|MZ..ABCDEFGHIJKL|") != std::string::npos,
          "la premi\xC3\xA8re ligne : " + (lines.empty() ? std::string() : lines[0]));
    check(lines.size() == 2 && lines[1].rfind("00000010  4D 4E", 0) == 0, "la deuxi\xC3\xA8me ligne, \xC3\xA0 0x10");
    check(fv::hexDump(std::string(10000, 'x'), 64).size() == 4, "born\xC3\xA9 : 64 octets, 4 lignes");
}

std::string workbook() {
    const std::string wb =
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>"
        "<sheet name=\"Seuils\" sheetId=\"1\" r:id=\"rId1\"/><sheet name=\"Cach\xC3\xA9\" sheetId=\"2\" state=\"hidden\" r:id=\"rId2\"/>"
        "</sheets></workbook>";
    const std::string rels =
        "<Relationships><Relationship Id=\"rId1\" Target=\"worksheets/sheet1.xml\"/><Relationship Id=\"rId2\" Target=\"/xl/worksheets/s2.xml\"/></Relationships>";
    const std::string sst = "<sst><si><t>Capteur</t></si><si><t>Seuil</t></si><si><r><t>Temp</t></r><r><t>\xC3\xA9rature</t></r></si></sst>";
    const std::string s1 =
        "<worksheet><dimension ref=\"A1:B3\"/><sheetData>"
        "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c></row>"
        "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>2</v></c><c r=\"B2\"><v>21.5</v></c></row>"
        "<row r=\"3\"><c r=\"B3\"><f>B2*2</f><v>43</v></c></row>"
        "</sheetData></worksheet>";
    const std::string s2 = "<worksheet><sheetData><row r=\"1\"><c r=\"A1\" t=\"inlineStr\"><is><t>secret</t></is></c></row></sheetData></worksheet>";
    return zipOf({{"[Content_Types].xml", "<Types/>"}, {"xl/workbook.xml", wb}, {"xl/_rels/workbook.xml.rels", rels}, {"xl/sharedStrings.xml", sst},
                  {"xl/worksheets/sheet1.xml", s1}, {"xl/worksheets/s2.xml", s2}, {"xl/vbaProject.bin", "VBA"}});
}

void classeur() {
    std::printf("6. un classeur XLSX fabrique ici\n");
    std::vector<fv::Sheet> sheets;
    bool macros = false;
    std::string why;
    const bool read = fv::readWorkbook(workbook(), sheets, macros, why);
    check(read, "lu " + why);
    check(sheets.size() == 2 && sheets[0].name == "Seuils" && sheets[1].name == "Cach\xC3\xA9" && sheets[1].hidden && macros,
          "deux onglets (le second masqu\xC3\xA9), des macros");
    check(!sheets.empty() && sheets[0].rows.size() == 3 && sheets[0].rows[0] == std::vector<std::string>{"Capteur", "Seuil"}
              && sheets[0].rows[1][0] == "Temp\xC3\xA9rature" && sheets[0].rows[1][1] == "21.5" && sheets[0].rows[2].size() == 2
              && sheets[0].rows[2][1] == "43" && sheets[0].totalRows == 3,
          "les cellules : cha\xC3\xAEnes partag\xC3\xA9" "es (un texte enrichi recoll\xC3\xA9), nombres, le r\xC3\xA9sultat d'une formule, la colonne B");
    check(sheets.size() == 2 && !sheets[1].rows.empty() && sheets[1].rows[0][0] == "secret", "une cha\xC3\xAEne en ligne (inlineStr)");
    const bool notBook = !fv::readWorkbook(zipOf({{"a.txt", "x"}}), sheets, macros, why);
    check(notBook && why.find("workbook.xml") != std::string::npos, "un zip qui n'est pas un classeur : " + why);
}

void docx() {
    std::printf("7. un DOCX fabrique ici\n");
    const std::string styles =
        "<w:styles xmlns:w=\"w\"><w:style w:type=\"paragraph\" w:styleId=\"Titre1\"><w:name w:val=\"heading 1\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"MonPlan\"><w:name w:val=\"Mon plan\"/><w:pPr><w:outlineLvl w:val=\"2\"/></w:pPr></w:style></w:styles>";
    const std::string doc =
        "<w:document xmlns:w=\"w\"><w:body>"
        "<w:p><w:pPr><w:pStyle w:val=\"Titre1\"/></w:pPr><w:r><w:t>Mise en route</w:t></w:r></w:p>"
        "<w:p><w:r><w:t xml:space=\"preserve\">Ouvrir la vanne </w:t></w:r><w:r><w:t>V-101</w:t></w:r><w:r><w:t> &amp; attendre.</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:pStyle w:val=\"Heading2\"/></w:pPr><w:r><w:t>D\xC3\xA9" "fauts</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:pStyle w:val=\"MonPlan\"/></w:pPr><w:r><w:t>Niveau 3</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"0\"/></w:numPr></w:pPr><w:r><w:t>Un point</w:t></w:r></w:p>"
        "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>Code</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>Rem\xC3\xA8" "de</w:t></w:r></w:p></w:tc></w:tr>"
        "<w:tr><w:tc><w:p><w:r><w:t>E1</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>Relancer</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"
        "</w:body></w:document>";
    std::vector<fv::Paragraph> ps;
    std::string why;
    const bool ok = fv::readDocx(zipOf({{"word/document.xml", doc}, {"word/styles.xml", styles}}), ps, why);
    check(ok && ps.size() == 7, "sept paragraphes (" + std::to_string(ps.size()) + ") " + why);
    if (ps.size() == 7) {
        check(ps[0].level == 1 && ps[0].text == "Mise en route", "Titre1 (style \xC2\xAB heading 1 \xC2\xBB) : un titre de niveau 1");
        check(ps[1].level == 0 && ps[1].text == "Ouvrir la vanne V-101 & attendre.", "un texte : ses morceaux et une espace gard\xC3\xA9" "e, &amp; d\xC3\xA9" "cod\xC3\xA9");
        check(ps[2].level == 2 && ps[3].level == 3, "Heading2 (par son identifiant) ; un niveau de plan (3)");
        check(ps[4].item && ps[4].text == "Un point", "un \xC3\xA9l\xC3\xA9ment de liste");
        check(ps[5].table && ps[5].text == "Code | Rem\xC3\xA8" "de" && ps[6].text == "E1 | Relancer", "un tableau : une ligne par rang\xC3\xA9" "e");
    }
    const bool notDoc = !fv::readDocx("pas un zip", ps, why);
    check(notDoc && !why.empty(), "pas un zip : " + why);
}

void arbres() {
    std::printf("8. JSON et XML indentes, repliables\n");
    auto t = fv::prettyJson("{\"a\":1,\"b\":[1,2,{}],\"c\":{\"d\":\"x\\\"y\"}}");
    check(t.ok && t.lines.size() == 11 && t.lines[0] == "{" && t.lines[1] == "\"a\": 1," && t.depth[1] == 1 && t.lines[2] == "\"b\": [" && t.depth[3] == 2
              && t.lines[5] == "{}" && t.lines[6] == "]," && t.lines[8] == "\"d\": \"x\\\"y\"" && t.lines[10] == "}" && t.depth[10] == 0,
          "JSON indent\xC3\xA9 (" + std::to_string(t.lines.size()) + " lignes)");
    check(fv::opensBlock(t.depth, 2) && !fv::opensBlock(t.depth, 1), "\"b\": [ ouvre un bloc, \"a\": 1 non");
    std::vector<bool> folded(t.lines.size(), false);
    folded[2] = true;
    const auto vis = fv::visibleLines(t.depth, folded);
    check(vis.size() == 8 && vis[2] == 2 && vis[3] == 6, "repli\xC3\xA9 : ses trois lignes cach\xC3\xA9" "es");
    t = fv::prettyJson("{\"a\": [1, 2}");
    check(!t.ok && t.error.find("inattendu") != std::string::npos, "JSON mal form\xC3\xA9 : " + t.error);
    check(!fv::prettyJson("{\"a\": vrai}").ok, "un mot qui n'est pas du JSON");
    auto x = fv::prettyXml("<?xml version=\"1.0\"?><a x=\"1>2\"><b>texte</b><c/><d><e>1</e></d></a>");
    check(x.ok && x.lines.size() == 8 && x.lines[1] == "<a x=\"1>2\">" && x.lines[2] == "<b>texte</b>" && x.depth[2] == 1 && x.lines[3] == "<c/>"
              && x.lines[5] == "<e>1</e>" && x.depth[5] == 2 && x.lines[6] == "</d>" && x.lines[7] == "</a>" && x.depth[7] == 0,
          "XML indent\xC3\xA9 (" + std::to_string(x.lines.size()) + " lignes)");
    x = fv::prettyXml("<a><b></a>");
    check(!x.ok && x.error.find("</a>") != std::string::npos, "XML mal form\xC3\xA9 : " + x.error);
    x = fv::prettyXml("<a><b>1</b><c");
    check(x.truncated, "XML coup\xC3\xA9 en plein milieu : dit coup\xC3\xA9, pas faux");
}

void charger(const fs::path& root) {
    std::printf("9. load() : le disque, la memoire, le fil de fond\n");
    put(root / "notes.txt", "Ligne 1\r\nLigne 2\r\n");
    put(root / "mesures.csv", "Date;Debit\n2026-10-30;12,5\n2026-10-31;13\n");
    put(root / "logo.png", tinyPng());
    put(root / "archive.zip", zipOf({{"a.txt", "aaa"}, {"b/", ""}, {"b/c.txt", "c"}}));
    put(root / "classeur.xlsx", workbook());
    put(root / "config.json", "{\"port\": 502, \"ip\": \"10.0.0.1\"}");
    put(root / "vide.dat", "");
    put(root / "programme.bin", std::string("\x7F" "ELF\x02\x01\x01", 7) + std::string(200, '\0'));
    put(root / "sous" / "x.txt", "x");

    auto p = fv::load(fv::fromPath(u8(root / "notes.txt")));
    check(p.kind == fv::Kind::Text && p.error.empty() && p.lines.size() == 2 && p.size == 18 && p.modified > 0 && p.onDisk && p.fact("Encodage") == "UTF-8"
              && p.fact("Fins de ligne") == "CRLF (Windows)",
          "notes.txt : texte, 2 lignes, 18 octets, sa date, UTF-8, CRLF");
    p = fv::load(fv::fromPath(u8(root / "mesures.csv")));
    check(p.kind == fv::Kind::Csv && p.sheets.size() == 1 && p.sheets[0].rows.size() == 3 && p.sheets[0].rows[1][1] == "12,5"
              && p.fact("S\xC3\xA9parateur") == "point-virgule (;)",
          "mesures.csv : une grille (12,5 reste une cellule)");
    p = fv::load(fv::fromPath(u8(root / "logo.png")));
    check(p.kind == fv::Kind::Image && p.image && p.image->width == 2 && p.image->height == 1 && p.image->pixels.size() == 8 && p.image->pixels[0] == 255
              && p.image->pixels[6] == 255 && p.fact("Dimensions").rfind("2 \xC3\x97 1", 0) == 0,
          "logo.png : d\xC3\xA9" "cod\xC3\xA9" "e (2 \xC3\x97 1, rouge puis bleu)");
    p = fv::load(fv::fromPath(u8(root / "archive.zip")));
    check(p.kind == fv::Kind::Zip && p.entries.size() == 3 && p.entryCount == 3 && p.entries[2].name == "b/c.txt", "archive.zip : ses trois entr\xC3\xA9" "es");
    p = fv::load(fv::fromPath(u8(root / "classeur.xlsx")));
    check(p.kind == fv::Kind::Workbook && p.sheets.size() == 2 && p.fact("Onglets") == "2" && !p.fact("Macros").empty(), "classeur.xlsx : deux onglets, des macros");
    p = fv::load(fv::fromPath(u8(root / "config.json")));
    check(p.kind == fv::Kind::Json && p.lines.size() == 4 && p.depth[1] == 1, "config.json : indent\xC3\xA9");
    p = fv::load(fv::fromPath(u8(root / "vide.dat")));
    check(p.error.empty() && p.size == 0 && !p.notes.empty() && p.notes[0] == "fichier vide", "un fichier vide : dit");
    p = fv::load(fv::fromPath(u8(root / "programme.bin")));
    check(p.kind == fv::Kind::Binary && p.fact("Reconnu") == "Programme Linux (ELF)" && !p.lines.empty() && p.lines[0].rfind("00000000  7F 45 4C 46", 0) == 0,
          "programme.bin : ELF reconnu, le vidage hexad\xC3\xA9" "cimal");
    p = fv::load(fv::fromPath(u8(root)));
    check(p.kind == fv::Kind::Folder && !p.fact("Contenu").empty(), "le dossier : son contenu r\xC3\xA9sum\xC3\xA9 (" + p.fact("Contenu") + ")");
    p = fv::load(fv::fromPath(u8(root / "absent.txt")));
    check(p.kind == fv::Kind::Missing && p.error.find("introuvable") == 0, "introuvable : dit");
    // En memoire (une ressource du projet) ; lue a la demande (une entree de zip).
    const std::string png = tinyPng();
    auto blob = std::make_shared<const hmi::Bytes>(png.begin(), png.end());
    const auto mem = fv::fromBytes("logo.png", blob, "Ressource du projet");
    check(!fv::slow(mem) && fv::slow(fv::fromPath(u8(root / "logo.png"))), "en m\xC3\xA9moire et petite : tout de suite ; sur le disque : le fil de fond");
    p = fv::load(mem);
    check(p.kind == fv::Kind::Image && p.image && p.size == png.size() && !p.onDisk && p.origin == "Ressource du projet", "une ressource en m\xC3\xA9moire");
    fv::Source lazy;
    lazy.name = "c.txt";
    lazy.shownPath = "archive.zip\\b\\c.txt";
    lazy.fetch = [](hmi::Bytes& out, std::string&) {
        out.assign({'s', 'a', 'l', 'u', 't'});
        return true;
    };
    check(fv::slow(lazy), "lue \xC3\xA0 la demande : le fil de fond");
    p = fv::load(lazy);
    check(p.kind == fv::Kind::Text && p.lines.size() == 1 && p.lines[0] == "salut" && p.shownPath == "archive.zip\\b\\c.txt", "une entr\xC3\xA9" "e lue \xC3\xA0 la demande");
    lazy.fetch = [](hmi::Bytes&, std::string& why) {
        why = "entr\xC3\xA9" "e ab\xC3\xAEm\xC3\xA9" "e";
        return false;
    };
    p = fv::load(lazy);
    check(p.error.find("illisible") == 0, "une lecture qui \xC3\xA9" "choue : illisible, pourquoi (" + p.error + ")");
    // Abandonner (un autre fichier est choisi) : rien ne casse.
    std::atomic<bool> stop{true};
    fv::Limits lim;
    lim.cancel = &stop;
    p = fv::load(fv::fromPath(u8(root / "mesures.csv")), lim);
    check(p.error.empty(), "abandonn\xC3\xA9 : pas d'erreur, pas d'exception");
    const auto lines = fv::describeLines(fv::load(fv::fromPath(u8(root / "notes.txt"))));
    check(lines.size() > 4 && lines[0] == "Nom : notes.txt" && lines[1].rfind("Type : ", 0) == 0, "describeLines : le nom, le type...");
}

void essais(const fs::path& dir) {
    std::printf("10. les fichiers d'essai de l'IHM (%s)\n", u8(dir).c_str());
    auto p = fv::load(fv::fromPath(u8(dir / "industrie_bold.ttf")));
    check(p.kind == fv::Kind::Font && p.fontSamples.size() == 4 && !p.fact("Famille").empty() && p.fontSamples[0]->width > 0,
          "une police : sa famille (" + p.fact("Famille") + "), quatre lignes d'\xC3\xA9" "chantillon");
    p = fv::load(fv::fromPath(u8(dir / "bip_alarme.wav")));
    check(p.kind == fv::Kind::Sound && p.envelope.size() == 512 && !p.fact("Dur\xC3\xA9" "e").empty() && p.bytes,
          "un son : sa dur\xC3\xA9" "e (" + p.fact("Dur\xC3\xA9" "e") + "), son onde (256 colonnes), ses octets pour Lire");
    p = fv::load(fv::fromPath(u8(dir / "convoyeur.gif")));
    check(p.kind == fv::Kind::Image && p.image && p.frames.size() > 1 && p.frameMs.size() == p.frames.size(),
          "un GIF anim\xC3\xA9 : " + std::to_string(p.frames.size()) + " images et leurs dur\xC3\xA9" "es");
    p = fv::load(fv::fromPath(u8(dir / "pompe.svg")));
    check(p.kind == fv::Kind::Image && p.image && p.image->width > 0, "un SVG : rasteris\xC3\xA9");
    p = fv::load(fv::fromPath(u8(dir / "parametres.xlsx")));
    check(p.kind == fv::Kind::Workbook && !p.sheets.empty() && !p.sheets[0].rows.empty(),
          "parametres.xlsx : " + std::to_string(p.sheets.size()) + " onglet(s), le premier : " + (p.sheets.empty() ? std::string() : p.sheets[0].name));
    p = fv::load(fv::fromPath(u8(dir / "presentation.mp4")));
    check(p.kind == fv::Kind::Video && !p.fact("Dimensions").empty() && p.bytes, "une vid\xC3\xA9o : " + p.fact("Dimensions") + ", " + p.fact("Dur\xC3\xA9" "e"));
    p = fv::load(fv::fromPath(u8(dir / "historique.db")));
    check(p.kind == fv::Kind::Binary && p.fact("Reconnu") == "Base SQLite" && !p.lines.empty(), "historique.db : une base SQLite, en hexad\xC3\xA9" "cimal");
    p = fv::load(fv::fromPath(u8(dir / "liste-io-armoire.tsv")));
    check(p.kind == fv::Kind::Csv && !p.sheets.empty() && p.sheets[0].columns > 1 && p.fact("S\xC3\xA9parateur") == "tabulation",
          "un TSV : s\xC3\xA9par\xC3\xA9 par des tabulations (" + std::to_string(p.sheets.empty() ? 0 : p.sheets[0].columns) + " colonnes)");
    p = fv::load(fv::fromPath(u8(dir / "photo_armoire.jpg")));
    check(p.kind == fv::Kind::Image && p.image && p.image->width == 640 && p.image->height == 400, "photo_armoire.jpg : 640 \xC3\x97 400");
}

} // namespace

int main(int argc, char** argv) {
    std::printf("fileview_test : l'apercu de tous les fichiers (sans \xC3\xA9" "cran)\n");
    const fs::path root = fs::temp_directory_path() / ("fileview_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    reconnaitre();
    texte();
    csv();
    zips();
    hexa();
    classeur();
    docx();
    arbres();
    charger(root);
    if (argc > 1 && fs::is_directory(fs::path(argv[1]))) essais(fs::path(argv[1]));
    else std::printf("10. (les fichiers d'essai de l'IHM : non donn\xC3\xA9s)\n");
    fs::remove_all(root, ec);
    std::printf("%s (%d \xC3\xA9" "chec(s))\n", failures == 0 ? "OK" : "ECHEC", failures);
    return failures == 0 ? 0 : 1;
}
