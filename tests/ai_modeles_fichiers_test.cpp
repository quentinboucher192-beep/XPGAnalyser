// tests/ai_modeles_fichiers_test.cpp - 1.21.0 : LES MODELES DE FICHIERS DE L'ASSISTANT (une liste d'E/S, un fichier reseau, des
// reports %MW/%M joints en .xlsx / .xlsm).
//
//   ai_modeles_fichiers_test
//
//  Sans ecran ni fournisseur, sur un dossier IA/ jetable, avec des classeurs SYNTHETIQUES fabriques ici (aucune donnee de
//  client) :
//  1. lire un classeur : les cases a leur place (une ligne absente du fichier), les cases fusionnees, un .xlsm ;
//  2. detecter les trois genres et << autre >> (francais et anglais), la confiance, la ligne d'en-tete, la correspondance ;
//  3. enregistrer un modele dans la memoire de l'IA, le reconnaitre sur un nouveau fichier (meme en-tete, autres lignes,
//     un titre ajoute) ; un en-tete different n'est pas reconnu ; renommer, compter, supprimer ;
//  4. le .xia : aller-retour avec les modeles, un ancien .xia sans modeles, la version gardee a 1 ;
//  5. les propositions : variables (et ce qui est laisse de cote), types IHM (les familles), equipements, adresses - sans
//     rien inventer ; l'IA integree les comprend ; une autre demande n'est pas prise ;
//  6. l'outil des lignes par pages, les consignes des fournisseurs, le bloc compact ;
//  7. l'analyse d'une piece jointe (le modele reconnu), le choix garde avec la piece, l'Assistant de bout en bout (la
//     fenetre << Modele... >>, une demande a l'IA integree, ce qui part chez un fournisseur).
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiAttachments.hpp"
#include "../src/app/ai/AiContextPane.hpp"
#include "../src/app/ai/AiFileModelDialog.hpp"
#include "../src/app/ai/AiFileModels.hpp"
#include "../src/app/ai/AiKnowledge.hpp"
#include "../src/app/ai/AiLearningFile.hpp"
#include "../src/app/ai/AiLocal.hpp"
#include "../src/app/ai/AiMemoryJson.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPrompt.hpp"
#include "../src/app/ai/AiStore.hpp"
#include "../src/app/ai/AiWorkbook.hpp"
#include "../src/export/DocKit.hpp"
#include "../src/hmi/HmiDocument.hpp"
#include "../src/hmi/HmiModel.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace sh = app::ai::sheets;
namespace mem = app::ai::memory;
namespace lf = app::ai::learning;
namespace at = app::ai::attach;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    static const bool all = [] {
        const char* e = std::getenv("AI_MODELES_TOUT");
        return e && *e == '1';
    }();
    if (!ok) {
        ++failures;
        std::printf("ECHEC : %s\n", what.c_str());
    } else if (all) {
        std::printf("  ok : %s\n", what.c_str());
    }
}

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

fs::path scratch() {
    static const fs::path p = fs::temp_directory_path()
                              / ("xpg_ai_modeles_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007));
    return p;
}

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

fs::path writeFile(const fs::path& p, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return p;
}

void useRoot(const std::string& name) {
    const fs::path r = scratch() / name;
    fs::create_directories(r);
    ai::setRoot(r.string());
}

// ---- un classeur fabrique ici : des feuilles, leurs lignes (numerotees : une ligne peut manquer), leurs fusions -------
struct TSheet {
    std::string                                                  name;
    std::vector<std::pair<std::size_t, std::vector<std::string>>> rows;   // (ligne 1.., cases)
    std::vector<std::string>                                     merges;  // "A3:A4"
    bool                                                         hidden{false};
};

std::string esc(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
    return out;
}

bool number(const std::string& s) {
    if (s.empty() || s.size() > 12) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || c == '.'; }) && s.front() != '.';
}

std::string xlsxOf(const std::vector<TSheet>& sheets, bool macros = false) {
    std::vector<std::pair<std::string, std::string>> items;
    items.emplace_back("[Content_Types].xml", "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"/>");
    std::string wb = "<?xml version=\"1.0\"?><workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                     "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>";
    std::string rels = "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    for (std::size_t i = 0; i < sheets.size(); ++i) {
        const std::string n = std::to_string(i + 1);
        wb += "<sheet name=\"" + esc(sheets[i].name) + "\" sheetId=\"" + n + "\" r:id=\"rId" + n + "\"" + (sheets[i].hidden ? " state=\"hidden\"" : "") + "/>";
        rels += "<Relationship Id=\"rId" + n + "\" Type=\"worksheet\" Target=\"worksheets/sheet" + n + ".xml\"/>";
        std::string xml = "<?xml version=\"1.0\"?><worksheet><sheetData>";
        for (const auto& [line, cells] : sheets[i].rows) {
            xml += "<row r=\"" + std::to_string(line) + "\">";
            for (std::size_t c = 0; c < cells.size(); ++c) {
                if (cells[c].empty()) continue;
                const std::string ref = sh::columnLetter(c) + std::to_string(line);
                if (number(cells[c])) xml += "<c r=\"" + ref + "\"><v>" + cells[c] + "</v></c>";
                else xml += "<c r=\"" + ref + "\" t=\"inlineStr\"><is><t>" + esc(cells[c]) + "</t></is></c>";
            }
            xml += "</row>";
        }
        xml += "</sheetData>";
        if (!sheets[i].merges.empty()) {
            xml += "<mergeCells count=\"" + std::to_string(sheets[i].merges.size()) + "\">";
            for (const auto& m : sheets[i].merges) xml += "<mergeCell ref=\"" + m + "\"/>";
            xml += "</mergeCells>";
        }
        xml += "</worksheet>";
        items.emplace_back("xl/worksheets/sheet" + n + ".xml", xml);
    }
    wb += "</sheets></workbook>";
    rels += "</Relationships>";
    items.emplace_back("xl/workbook.xml", wb);
    items.emplace_back("xl/_rels/workbook.xml.rels", rels);
    if (macros) items.emplace_back("xl/vbaProject.bin", std::string("\xD0\xCF\x11\xE0 macros (jamais lues)", 24));
    const auto bytes = exporter::doc::zip(items);
    return std::string(bytes.begin(), bytes.end());
}

using Cells = std::vector<std::string>;

// LA LISTE D'E/S : un titre, l'en-tete en ligne 3, des lignes (la ligne 11 absente du fichier), une sans repere, un doublon,
// un repere a ajuster (K301.COMMANDE).
const Cells kIoHeader = {"Rep\xC3\xA8re", "D\xC3\xA9signation", "Type", "Adresse", "Rack", "Emplacement", "Voie", "\xC3\x89" "chelle min",
                         "\xC3\x89" "chelle max", "Unit\xC3\xA9"};
TSheet ioSheet(const std::string& name, std::size_t headerLine, const std::vector<Cells>& rows, const Cells& header = kIoHeader) {
    TSheet s;
    s.name = name;
    for (std::size_t i = 1; i < headerLine; ++i) s.rows.push_back({i, {i == 1 ? "Liste des entr\xC3\xA9" "es / sorties" : "Armoire A1 - indice B"}});
    s.rows.push_back({headerLine, header});
    std::size_t line = headerLine + 1;
    for (const auto& r : rows) {
        if (r.empty()) {   // une ligne absente du fichier
            ++line;
            continue;
        }
        s.rows.push_back({line++, r});
    }
    return s;
}
const std::vector<Cells> kIoRows = {
    {"P101_MARCHE", "Pompe 101 en marche", "DI", "%I0.1.0", "0", "1", "0"},
    {"P101_DEFAUT", "Pompe 101 en d\xC3\xA9" "faut", "DI", "%I0.1.1", "0", "1", "1"},
    {"P101_VITESSE", "Pompe 101 vitesse", "AI", "%IW0.3.0", "0", "3", "0", "0", "1500", "tr/min"},
    {"P102_MARCHE", "Pompe 102 en marche", "DI", "%I0.1.2", "0", "1", "2"},
    {"P102_DEFAUT", "Pompe 102 en d\xC3\xA9" "faut", "DI", "%I0.1.3", "0", "1", "3"},
    {"P102_VITESSE", "Pompe 102 vitesse", "AI", "%IW0.3.1", "0", "3", "1", "0", "1500", "tr/min"},
    {"V201_OUVERT", "Vanne 201 ouverte", "DI", "%I0.2.0", "0", "2", "0"},
    {},
    {"", "R\xC3\xA9serve", "DI", "%I0.2.1", "0", "2", "1"},
    {"P101_MARCHE", "Doublon de saisie", "DI", "%I0.2.2", "0", "2", "2"},
    {"K301.COMMANDE", "Contacteur 301", "DO", "%Q0.4.0", "0", "4", "0"},
};

std::string ioXlsx() { return xlsxOf({ioSheet("Liste E/S", 3, kIoRows)}); }

// LE FICHIER RESEAU : une IP vide, une illisible, un nom en double, un Modbus RTU (pas pris en charge).
std::string networkXlsx() {
    TSheet s;
    s.name = "R\xC3\xA9seau";
    s.rows = {
        {1, {"\xC3\x89quipement", "Adresse IP", "Masque", "Passerelle", "VLAN", "Protocole", "Port", "Adresse MAC", "Commentaire"}},
        {2, {"API_1", "192.168.10.10", "255.255.255.0", "192.168.10.1", "10", "Modbus TCP", "502", "00:1A:2B:3C:4D:01", "Automate principal"}},
        {3, {"Variateur_P101", "192.168.10.21", "255.255.255.0", "192.168.10.1", "10", "Modbus TCP", "502", "00:1A:2B:3C:4D:02"}},
        {4, {"Switch_1", "192.168.10.2", "255.255.255.0", "192.168.10.1", "1", "SNMP", "", "00:1A:2B:3C:4D:03"}},
        {5, {"Pupitre_1", "", "255.255.255.0", "", "10", "Modbus TCP"}},
        {6, {"Camera_1", "192.168.10.300", "255.255.255.0", "192.168.10.1", "20", "RTSP", "554"}},
        {7, {"Variateur_P101", "192.168.10.22", "255.255.255.0", "192.168.10.1", "10", "Modbus TCP", "502"}},
        {8, {"Passerelle_RTU", "192.168.10.30", "255.255.255.0", "192.168.10.1", "10", "Modbus RTU"}},
    };
    return xlsxOf({s});
}

// LES REPORTS : un titre (<< Table d'echange %MW >>), l'en-tete en ligne 2 ; une feuille jumelle (le meme en-tete) ; une
// feuille de bits ou la designation est fusionnee sur deux lignes.
const Cells kExHeader = {"Mn\xC3\xA9monique", "D\xC3\xA9signation", "Type", "Adresse", "Sens"};
std::string reportsXlsx() {
    TSheet a;
    a.name = "Reports API";
    a.rows = {{1, {"Table d'\xC3\xA9" "change %MW"}},
              {2, kExHeader},
              {3, {"ETAT_POMPE_1", "\xC3\x89tat pompe 1", "INT", "%MW100", "Lecture"}},
              {4, {"CMD_POMPE_1", "Commande pompe 1", "INT", "%MW101", "\xC3\x89" "criture"}},
              {5, {"DEFAUT_GENERAL", "D\xC3\xA9" "faut g\xC3\xA9n\xC3\xA9ral", "BOOL", "%M10", "Lecture"}},
              {6, {"NIVEAU_CUVE", "Niveau cuve", "REAL", "%MF200", "Lecture"}}};
    TSheet b;
    b.name = "Reports API 2";
    b.rows = {{1, {"Table d'\xC3\xA9" "change %MW (suite)"}}, {2, kExHeader}, {3, {"ETAT_POMPE_2", "\xC3\x89tat pompe 2", "INT", "%MW110", "Lecture"}}};
    TSheet c;
    c.name = "Bits";
    c.rows = {{1, {"D\xC3\xA9signation", "Mn\xC3\xA9monique", "Bit %M", "Mot", "Bit"}},
              {2, {"Alarmes pompe 1", "ALM_P1_TH", "%M500", "%MW50", "0"}},
              {3, {"", "ALM_P1_DEF", "%M501", "%MW50", "1"}}};
    c.merges = {"A2:A3"};
    return xlsxOf({a, b, c});
}

std::string exchangeEnXlsx() {
    TSheet s;
    s.name = "Exchange";
    s.rows = {{1, {"Tag name", "Description", "Data type", "Address", "Access"}},
              {2, {"PUMP_STATE", "Pump state", "INT", "%MW300", "Read"}},
              {3, {"PUMP_CMD", "Pump command", "INT", "%MW301", "Write"}}};
    return xlsxOf({s});
}

std::string otherXlsx() {
    TSheet s;
    s.name = "Contacts";
    s.rows = {{1, {"Nom", "Pr\xC3\xA9nom", "T\xC3\xA9l\xC3\xA9phone"}}, {2, {"Dupont", "Jean", "0601020304"}}, {3, {"Martin", "Anne", "0605060708"}}};
    return xlsxOf({s});
}

sh::Workbook wbOf(const std::string& bytes) {
    sh::Workbook wb;
    std::string why;
    check(sh::readWorkbook(bytes, wb, &why), "le classeur se lit (" + why + ")");
    return wb;
}

std::string roleAt(const sh::Layout& L, std::size_t col) {
    for (const auto& c : L.columns)
        if (c.col == col) return c.role;
    return {};
}

sh::Attached attached(const std::string& name, const std::string& bytes, const sh::Layout& L, const std::string& model = {}) {
    sh::Attached f;
    f.id = "p-essai-" + name;
    f.name = name;
    f.path = (scratch() / "fichiers" / name).string();
    writeFile(f.path, bytes);
    auto wb = std::make_shared<sh::Workbook>(wbOf(bytes));
    f.workbook = wb;
    f.hasLayout = true;
    f.layout = L;
    f.modelName = model;
    f.confidence = 0.9;
    return f;
}

const ai::Operation* opOf(const std::vector<ai::Operation>& ops, ai::Operation::Action a) {
    for (const auto& o : ops)
        if (o.action == a) return &o;
    return nullptr;
}

std::string prop(const ai::Operation& o, const std::string& key) {
    for (const auto& [k, v] : o.props)
        if (k == key) return v;
    return {};
}

std::vector<ai::Operation::VariableSpec> allVariables(const std::vector<ai::Operation>& ops) {
    std::vector<ai::Operation::VariableSpec> out;
    for (const auto& o : ops)
        if (o.action == ai::Operation::Action::Variables) out.insert(out.end(), o.variables.begin(), o.variables.end());
    return out;
}

const ai::Operation::VariableSpec* varNamed(const std::vector<ai::Operation::VariableSpec>& v, const std::string& name) {
    for (const auto& x : v)
        if (x.name == name) return &x;
    return nullptr;
}

// ================================================================================== 1. lire ===
void testRead() {
    std::printf("== 1. lire un classeur ==\n");
    const sh::Workbook io = wbOf(ioXlsx());
    check(io.sheets.size() == 1 && io.sheets[0].name == "Liste E/S", "une feuille, son nom");
    const auto& s = io.sheets[0];
    check(s.cell(2, 0) == "Rep\xC3\xA8re" && s.cell(3, 0) == "P101_MARCHE", "l'en-tete en ligne 3, la premiere ligne en ligne 4");
    check(s.cell(10, 0).empty() && s.cell(11, 1) == "R\xC3\xA9serve", "la ligne 11 absente du fichier reste vide : la suivante a sa place");
    check(s.cell(3, 4) == "0" && s.cell(5, 8) == "1500", "un nombre : sa valeur");
    const sh::Workbook ex = wbOf(reportsXlsx());
    check(ex.sheets.size() == 3 && ex.sheets[2].merges.size() == 1 && ex.sheets[2].merges[0].r0 == 1 && ex.sheets[2].merges[0].r1 == 2,
          "la fusion A2:A3 lue");
    sh::Workbook xm;
    std::string why;
    check(sh::readWorkbook(xlsxOf({ioSheet("IO", 1, {{"A_1", "Un", "DI", "%I0.0"}, {"A_2", "Deux", "DO", "%Q0.0"}})}, true), xm, &why) && xm.macros,
          "un .xlsm : lu, ses macros signalees (jamais executees)");
    check(!sh::readWorkbook("pas une archive", xm, &why) && has(why, "archive"), "pas une archive : refuse, et dit pourquoi");
    check(sh::workbookExt("XLSM") && sh::workbookExt("xlsx") && !sh::workbookExt("xls"), "les extensions : xlsx, xlsm (pas l'ancien .xls)");
    // les valeurs
    check(sh::isIoAddress("%I0.1.0") && sh::isIoAddress("%IW0.3.2") && sh::isIoAddress("I0.7") && sh::isIoAddress("PEW256") && !sh::isIoAddress("A12")
              && !sh::isIoAddress("%MW100"),
          "les adresses d'E/S");
    check(sh::isMemoryAddress("%MW100") && sh::isMemoryAddress("%M10") && sh::isMemoryAddress("%MW10.3") && sh::isMemoryAddress("MW10")
              && sh::isMemoryAddress("DB1.DBX0.0") && !sh::isMemoryAddress("M1") && !sh::isMemoryAddress("%I0.1"),
          "les adresses memoire");
    check(sh::isIpv4("192.168.1.10") && sh::isIpv4("10.0.0.0/8") && !sh::isIpv4("192.168.1.300") && !sh::isIpv4("1.2.3"), "les adresses IP");
    check(sh::signalOf("DI") == "DI" && sh::signalOf("E TOR") == "DI" && sh::signalOf("ana") == "ANA" && sh::signalOf("Pompe").empty(), "les types de signal");
    check(sh::typeOfAddress("%MW10") == "INT" && sh::typeOfAddress("%M10") == "BOOL" && sh::typeOfAddress("%MF2") == "REAL"
              && sh::typeOfAddress("%MD4") == "DINT" && sh::typeOfAddress("%IW0.3.0") == "INT" && sh::typeOfAddress("%I0.1") == "BOOL",
          "le type d'une adresse");
    check(sh::normalizeHeader("Mn\xC3\xA9moniques API") == "mnemonique api" && sh::normalizeHeader("@IP") == "ip" && sh::normalizeHeader("Canaux") == "canal",
          "un intitule normalise (accents, casse, pluriel)");
}

// ============================================================================== 2. detecter ===
void testDetect() {
    std::printf("== 2. detecter ==\n");
    {
        const sh::Detection d = sh::detect(wbOf(ioXlsx()));
        const auto* s = d.chosen();
        check(s && s->genre == sh::Genre::IoList && s->confidence >= 0.8, "une liste d'E/S, sure (" + std::to_string(s ? s->confidence : 0) + ")");
        check(s && s->layout.headerRow == 2, "son en-tete : la ligne 3 (sous le titre)");
        if (s) {
            const auto& L = s->layout;
            check(roleAt(L, 0) == "repere" && roleAt(L, 1) == "designation" && roleAt(L, 2) == "type" && roleAt(L, 3) == "adresse" && roleAt(L, 4) == "rack"
                      && roleAt(L, 5) == "slot" && roleAt(L, 6) == "voie" && roleAt(L, 7) == "min" && roleAt(L, 8) == "max" && roleAt(L, 9) == "unite",
                  "la correspondance : " + ai::sheets::mappingText(L));
            check(!s->reasons.empty() && has(s->reasons.front(), "ligne 3"), "pourquoi : " + (s->reasons.empty() ? std::string() : s->reasons.front()));
        }
    }
    {
        const sh::Detection d = sh::detect(wbOf(networkXlsx()));
        const auto* s = d.chosen();
        check(s && s->genre == sh::Genre::Network && s->confidence >= 0.8, "un fichier reseau");
        check(s && s->layout.colOf("equipement") == 0 && s->layout.colOf("ip") == 1 && s->layout.colOf("masque") == 2 && s->layout.colOf("passerelle") == 3
                  && s->layout.colOf("vlan") == 4 && s->layout.colOf("protocole") == 5 && s->layout.colOf("port") == 6 && s->layout.colOf("mac") == 7,
              "ses colonnes : " + (s ? sh::mappingText(s->layout) : std::string()));
    }
    {
        const sh::Detection d = sh::detect(wbOf(reportsXlsx()));
        const auto* s = d.chosen();
        check(s && s->genre == sh::Genre::Exchange && s->sheet == "Reports API" && s->layout.headerRow == 1, "des reports %MW (le titre au-dessus)");
        check(s && s->layout.colOf("repere") == 0 && s->layout.colOf("adresse") == 3 && s->layout.colOf("sens") == 4 && s->layout.colOf("type") == 2,
              "leurs colonnes : " + (s ? sh::mappingText(s->layout) : std::string()));
        check(d.sheets.size() == 3 && d.sheets[2].genre == sh::Genre::Exchange && d.sheets[2].layout.colOf("adresse") == 2 && d.sheets[2].layout.colOf("mot") == 3,
              "la feuille de bits : Bit %M = l'adresse, Mot = le mot");
    }
    {
        const sh::Detection d = sh::detect(wbOf(exchangeEnXlsx()));
        const auto* s = d.chosen();
        check(s && s->genre == sh::Genre::Exchange && s->layout.colOf("repere") == 0 && s->layout.colOf("sens") == 4, "des intitules anglais (Tag name, Access)");
    }
    {
        const sh::Detection d = sh::detect(wbOf(otherXlsx()));
        const auto* s = d.chosen();
        check(s && s->genre == sh::Genre::Other, "un carnet d'adresses : << autre >>");
    }
    // une liste d'E/S sans adresses mais avec rack, emplacement, voie : reconnue a ses intitules
    {
        const sh::Detection d = sh::detect(wbOf(xlsxOf({ioSheet("IO", 1, {{"X_1", "Un", "", "", "0", "1", "0"}, {"X_2", "Deux", "", "", "0", "1", "1"}})})));
        const auto* s = d.chosen();
        check(s && s->genre == sh::Genre::IoList, "une liste d'E/S sans adresses : ses intitules suffisent");
    }
}

// ======================================================================== 3. les modeles ===
void testModels() {
    std::printf("== 3. les modeles de fichiers ==\n");
    useRoot("pc1");
    const sh::Workbook wb = wbOf(ioXlsx());
    sh::Recognition r = sh::recognize(wb, sh::loadModels());
    check(r.hasLayout && r.modelName.empty() && r.genre == sh::Genre::IoList && has(r.summary, "D\xC3\xA9tection"), "sans modele : la detection (" + r.summary + ")");
    check(r.rows == 10 && r.empty == 1, "10 lignes lues, la ligne absente comptee vide (" + std::to_string(r.rows) + ")");
    sh::FileModel m = sh::makeModel(wb, r.layout, "Liste E/S essai");
    std::string why;
    check(sh::saveModel(m, &why) && m.id.rfind("fichier.liste_e_s_essai.", 0) == 0, "enregistre : " + m.id + " " + why);
    sh::FileModel same = sh::makeModel(wb, r.layout, "liste e/s ESSAI");
    check(!sh::saveModel(same, &why) && has(why, "d\xC3\xA9j\xC3\xA0"), "un second modele du meme nom : refuse");
    const auto models = sh::loadModels();
    check(models.size() == 1 && models[0].signature.size() == 10 && models[0].layout.columns.size() == 10, "relu de la memoire : sa signature, ses colonnes");
    mem::Registry reg;
    reg.load();
    const auto* k = reg.find(m.id);
    check(k && k->category == "modele-fichier" && k->source == "fichier" && k->active && has(k->content, "Correspondance")
              && mem::hereState(*k, mem::Context{}) == mem::Here::Active,
          "une connaissance du registre (categorie modele-fichier), active : elle part avec les demandes");
    check(mem::categoryLabel("modele-fichier") == "Mod\xC3\xA8les de fichiers" && std::string(std::end(mem::kCategories)[-1].key) == "metier",
          "la categorie << Modeles de fichiers >> (metier reste la derniere)");
    // un nouveau fichier : le meme en-tete, d'autres lignes, un titre de plus au-dessus, une autre feuille
    const sh::Workbook other = wbOf(xlsxOf({ioSheet("Liste E/S v2", 4, {{"P301_MARCHE", "Pompe 301", "DI", "%I1.0.0", "1", "0", "0"},
                                                                        {"P301_DEFAUT", "Pompe 301 d\xC3\xA9" "faut", "DI", "%I1.0.1", "1", "0", "1"}})}));
    r = sh::recognize(other, sh::loadModels());
    check(r.modelName == "Liste E/S essai" && r.modelId == m.id && r.layout.headerRow == 3 && r.layout.sheet == "Liste E/S v2" && r.rows == 2,
          "reconnu sur un nouveau fichier (" + r.summary + ")");
    check(r.summary == "Mod\xC3\xA8le reconnu : Liste E/S essai \xE2\x80\x94 2 lignes lues", "le resume : " + r.summary);
    // un en-tete different (Voie -> Canal) : pas reconnu (les intitules a l'identique), la detection prend le relais
    Cells changed = kIoHeader;
    changed[6] = "Canal";
    r = sh::recognize(wbOf(xlsxOf({ioSheet("IO", 3, kIoRows, changed)})), sh::loadModels());
    check(r.modelName.empty() && r.genre == sh::Genre::IoList, "un intitule change : pas le modele, la detection");
    // les utilisations, renommer, desactiver, supprimer
    check(sh::noteUse(m.id) && sh::noteUse(m.id) && sh::findModel(m.id)->uses == 2, "deux utilisations comptees");
    check(sh::renameModel(m.id, "Liste E/S armoire A1", &why) && sh::findModel(m.id)->name == "Liste E/S armoire A1", "renomme");
    reg.load();
    check(reg.setActive(m.id, false, &why) && sh::loadModels().empty() && sh::loadModels(true).size() == 1, "desactive (Memoire et apprentissage) : plus utilise");
    check(!sh::recognize(other, sh::loadModels()).modelId.size(), "desactive : un fichier n'est plus reconnu par lui");
    reg.load();
    check(reg.setActive(m.id, true, &why), "reactive");
    // ce que recoit N'IMPORTE QUEL fournisseur : la description, dans les connaissances actives des consignes
    reg.load();
    hmi::Project project;
    ai::PromptInput in;
    in.project = &project;
    in.knowledge = &reg;
    const ai::PromptParts parts = ai::promptParts(in);
    check(has(parts.stable, "Mod\xC3\xA8le de fichier \xC2\xAB Liste E/S armoire A1 \xC2\xBB") && has(parts.stable, "Correspondance : Rep\xC3\xA8re = A"),
          "la description du modele part dans les consignes (Anthropic comme compatible OpenAI)");
    // un second modele (reports) pour la suite ; puis supprimer et remettre le premier
    const sh::Workbook ex = wbOf(reportsXlsx());
    sh::FileModel mx = sh::makeModel(ex, sh::detect(ex).chosen()->layout, "Reports API essai");
    check(sh::saveModel(mx, &why), "un modele de reports");
    sh::FileModel copy = *sh::findModel(m.id);
    check(sh::removeModel(m.id, &why) && !sh::findModel(m.id), "supprime");
    copy.id.clear();
    copy.uses = 0;
    // l'identifiant : fichier.<le nom>.<l'empreinte de la signature> - le meme nom et le meme en-tete donnent le meme
    check(sh::saveModel(copy, &why) && copy.id.rfind("fichier.liste_e_s_armoire_a1.", 0) == 0
              && copy.id.substr(copy.id.rfind('.')) == m.id.substr(m.id.rfind('.')),
          "enregistre de nouveau : l'identifiant suit le nom et l'empreinte de son en-tete (" + copy.id + ")");
    sh::FileModel again = *sh::findModel(copy.id);
    again.id.clear();
    check(!sh::saveModel(again, &why), "le meme nom une seconde fois : refuse (pas de doublon)");
}

// ============================================================================== 4. le .xia ===
void testXia() {
    std::printf("== 4. le .xia ==\n");
    useRoot("pc1");   // les modeles de testModels
    const std::string file = (scratch() / "avec_modeles.xia").string();
    std::string why;
    check(lf::exportTo(file, {}, &why), "exporter (" + why + ")");
    mem::Json j;
    check(mem::parseJson(readAll(file), j) && j.num("version") == 1 && j.num("revision") == 2 && j.num("modelesFichiers") == 2,
          "le fichier : version 1 (une version precedente le lit), revision 2, 2 modeles de fichiers");
    bool asKnowledge = false;
    if (const auto* list = j.get("connaissances"); list && list->isArray())
        for (const auto& k : list->items)
            asKnowledge = asKnowledge || (k.str("categorie") == "modele-fichier" && !k.str("id").empty() && !k.str("contenu").empty() && k.get("donnees"));
    check(asKnowledge, "un modele voyage comme une connaissance (id, nom, contenu lisible, donnees) : une version precedente le garde en texte");
    // sans les modeles
    const std::string without = (scratch() / "sans_modeles.xia").string();
    lf::FileOptions opt;
    opt.fileModels = false;
    check(lf::exportTo(without, opt, &why) && !has(readAll(without), "modele-fichier"), "exporter sans les modeles");
    // les modeles seuls
    const std::string only = (scratch() / "modeles_seuls.xia").string();
    opt = {};
    opt.knowledge = false;
    check(lf::exportTo(only, opt, &why) && has(readAll(only), "modele-fichier"), "exporter les modeles seuls");
    // un autre PC
    useRoot("pc2");
    lf::ImportReport pv;
    check(lf::peek(file, pv, &why) && pv.added == 2 && pv.models == 2, "peek : 2 modeles a ajouter");
    check(std::any_of(pv.lines.begin(), pv.lines.end(), [](const std::string& l) { return has(l, "(mod\xC3\xA8le de fichier)"); }), "dit comme modele de fichier");
    lf::ImportReport r;
    check(lf::importFrom(file, r, false, &why) && r.models == 2, "importer : 2 modeles (" + why + ")");
    const auto models = sh::loadModels();
    check(models.size() == 2, "les modeles sur le PC 2");
    // reconnu sur ce PC
    const sh::Recognition rec = sh::recognize(wbOf(ioXlsx()), models);
    check(rec.modelName == "Liste E/S armoire A1", "le PC 2 reconnait la liste d'E/S par le modele importe");
    // un ancien .xia (avant les modeles) : se lit tel quel
    useRoot("pc3");
    const std::string old = (scratch() / "ancien.xia").string();
    writeFile(old, "{ \"format\": \"xia\", \"version\": 1, \"logiciel\": \"1.20.0\", \"connaissances\": [ { \"id\": \"k-20260101-000000-1\", "
                   "\"nom\": \"Regle des couleurs\", \"categorie\": \"regles\", \"contenu\": \"Rouge : defaut.\" } ] }");
    lf::ImportReport ro;
    check(lf::importFrom(old, ro, false, &why) && ro.added == 1 && ro.models == 0 && sh::loadModels().empty(), "un ancien .xia : lu, aucun modele");
}

// ========================================================================= 5. les actions ===
void testActions() {
    std::printf("== 5. les propositions ==\n");
    useRoot("pc4");
    // une liste d'E/S : des variables, rien d'invente
    const sh::Workbook io = wbOf(ioXlsx());
    const sh::Attached f = attached("liste_es.xlsx", ioXlsx(), sh::detect(io).chosen()->layout);
    const auto readings = sh::readAll(*f.workbook, f.layout);
    hmi::Project project;
    sh::Plan p = sh::planVariables(f, readings, &project);
    const auto vars = allVariables(p.ops);
    check(p.ok && vars.size() == 8, "8 variables (" + std::to_string(vars.size()) + ")");
    const auto* v1 = varNamed(vars, "P101_MARCHE");
    const auto* v2 = varNamed(vars, "P101_VITESSE");
    const auto* v3 = varNamed(vars, "K301_COMMANDE");
    check(v1 && v1->type == "BOOL" && has(v1->description, "Pompe 101 en marche") && has(v1->description, "%I0.1.0"), "DI -> BOOL, la designation et l'adresse");
    check(v2 && v2->type == "REAL" && has(v2->description, "0 \xC3\xA0 1500 tr/min"), "AI -> REAL, l'echelle et l'unite");
    check(v3 && v3->type == "BOOL", "un repere ajuste en identifiant (K301.COMMANDE -> K301_COMMANDE)");
    bool invented = false;
    for (const auto& v : vars) {
        bool found = false;
        for (const auto& row : kIoRows)
            if (!row.empty() && sh::identifierFrom(row[0]) == v.name) found = true;
        invented = invented || !found;
    }
    check(!invented, "aucun nom invente : chaque variable vient d'un repere du fichier");
    check(has(p.text, "1 ligne sans rep\xC3\xA8re") && has(p.text, "ligne 13") && has(p.text, "en double") && has(p.text, "K301.COMMANDE \xE2\x86\x92 K301_COMMANDE"),
          "laisse de cote et ajuste : dit (" + p.text + ")");
    check(!opOf(p.ops, ai::Operation::Action::Address), "une liste d'E/S : aucune adresse posee (une E/S ne se lit pas par Modbus)");
    ai::Proposal prop1;
    prop1.ops = p.ops;
    ai::Check ck = ai::validate(project, prop1);
    check(ck.ok(), "la proposition est valide (" + (ck.errors.empty() ? std::string() : ck.errors.front()) + ")");
    // les types : une famille par equipement repete
    sh::PlanOptions o;
    o.types = true;
    p = sh::planVariables(f, readings, &project, o);
    const auto* type = opOf(p.ops, ai::Operation::Action::Type);
    check(type && type->name == "T_P" && type->members.size() == 3 && type->members[0].name == "MARCHE" && type->members[2].type == "REAL",
          "un type IHM T_P (MARCHE, DEFAUT, VITESSE)");
    const auto tv = allVariables(p.ops);
    check(varNamed(tv, "P101") && varNamed(tv, "P101")->type == "T_P" && varNamed(tv, "P102") && varNamed(tv, "V201_OUVERT") && !varNamed(tv, "P101_MARCHE"),
          "une variable par equipement (P101, P102 : T_P), les autres seules");
    prop1.ops = p.ops;
    ck = ai::validate(project, prop1);
    check(ck.ok() && ck.newTypes.size() == 1, "valide : le type avant les variables (" + (ck.errors.empty() ? std::string() : ck.errors.front()) + ")");
    // le reseau : des equipements
    const sh::Workbook net = wbOf(networkXlsx());
    const sh::Attached fn = attached("reseau.xlsx", networkXlsx(), sh::detect(net).chosen()->layout);
    p = sh::planEquipments(fn, sh::readAll(*fn.workbook, fn.layout), &project);
    std::vector<std::string> names;
    for (const auto& op : p.ops) names.push_back(op.name);
    check(p.ok && names == std::vector<std::string>{"API_1", "Variateur_P101", "Switch_1"}, "3 equipements, ceux du fichier");
    const auto* api = opOf(p.ops, ai::Operation::Action::Equipment);
    check(api && prop(*api, "type") == "modbus_tcp" && prop(*api, "adresse") == "192.168.10.10" && prop(*api, "port") == "502"
              && has(prop(*api, "description"), "masque 255.255.255.0") && has(prop(*api, "description"), "VLAN 10"),
          "API_1 : Modbus TCP, son IP, son port ; le masque et le VLAN dans la description");
    check(prop(p.ops[2], "type") == "ethernet_tcp", "SNMP : un equipement Ethernet TCP/IP (surveille, sans adresses)");
    check(has(p.text, "Camera_1") && has(p.text, "illisible") && has(p.text, "Variateur_P101") && has(p.text, "en double") && has(p.text, "Modbus RTU"),
          "l'IP illisible, le doublon, le Modbus RTU : dits (" + p.text + ")");
    prop1.ops = p.ops;
    ck = ai::validate(project, prop1);
    check(ck.ok(), "les equipements sont valides (" + (ck.errors.empty() ? std::string() : ck.errors.front()) + ")");
    // les reports : les adresses posees sur le seul equipement Modbus du projet
    const sh::Workbook ex = wbOf(reportsXlsx());
    const sh::Attached fx = attached("reports.xlsx", reportsXlsx(), sh::detect(ex).chosen()->layout);
    const auto rx = sh::readAll(*fx.workbook, fx.layout);
    check(rx.size() == 2 && rx[1].sheet == "Reports API 2", "la feuille jumelle (le meme en-tete) lue aussi");
    p = sh::planVariables(fx, rx, &project);
    check(p.ok && allVariables(p.ops).size() == 5 && !opOf(p.ops, ai::Operation::Action::Address) && has(p.text, "aucun \xC3\xA9quipement Modbus"),
          "sans equipement : les variables, les adresses dans la description, et c'est dit");
    hmi::Equipment e;
    e.id = project.allocate();
    e.name = "API_1";
    e.host = "192.168.10.10";
    project.equipments.push_back(e);
    p = sh::planVariables(fx, rx, &project);
    std::vector<const ai::Operation*> addr;
    for (const auto& op : p.ops)
        if (op.action == ai::Operation::Action::Address) addr.push_back(&op);
    check(addr.size() == 5 && addr[0]->variable == "ETAT_POMPE_1" && prop(*addr[0], "equipement") == "API_1" && prop(*addr[0], "adresse") == "%MW100"
              && prop(*addr[0], "lecture_seule") == "oui" && prop(*addr[1], "lecture_seule") == "non",
          "5 adresses sur API_1, le sens lu (lecture : lecture seule)");
    const auto xv = allVariables(p.ops);
    check(varNamed(xv, "DEFAUT_GENERAL") && varNamed(xv, "DEFAUT_GENERAL")->type == "BOOL" && varNamed(xv, "NIVEAU_CUVE")->type == "REAL"
              && !has(varNamed(xv, "ETAT_POMPE_1")->description, "%MW100"),
          "les types du fichier ; l'adresse posee ne se repete pas dans la description");
    prop1.ops = p.ops;
    ck = ai::validate(project, prop1);
    check(ck.ok(), "creer puis adresser : valide (" + (ck.errors.empty() ? std::string() : ck.errors.front()) + ")");
    // adresser des variables qui existent deja : seulement elles
    hmi::Variable existing;
    existing.id = project.allocate();
    existing.name = "ETAT_POMPE_1";
    existing.type = "INT";
    project.programs.variables.push_back(existing);
    o = {};
    o.onlyExisting = true;
    p = sh::planVariables(fx, rx, &project, o);
    check(p.ok && p.ops.size() == 1 && p.ops[0].action == ai::Operation::Action::Address && p.ops[0].variable == "ETAT_POMPE_1"
              && has(p.text, "absentes du projet"),
          "adresser l'existante seule ; les absentes sont dites");
    // une fusion : la designation recopiee sur ses deux lignes
    sh::Layout bits = sh::detectSheet(ex.sheets[2], 2).layout;
    const sh::Reading rb = sh::read(ex, bits);
    check(rb.rows.size() == 2 && rb.rows[1].value("designation") == "Alarmes pompe 1", "la case fusionnee : sa valeur dans chacune de ses lignes");
}

// ============================================================================ 5b. l'IA integree ===
void testLocal() {
    std::printf("== 5b. l'IA integree ==\n");
    useRoot("pc5");
    auto doc = std::make_shared<hmi::Document>();
    const sh::Workbook io = wbOf(ioXlsx());
    const sh::Workbook net = wbOf(networkXlsx());
    ai::PromptInput in;
    in.project = &doc->project;
    in.workbooks = {attached("liste_es.xlsx", ioXlsx(), sh::detect(io).chosen()->layout),
                    attached("reseau.xlsx", networkXlsx(), sh::detect(net).chosen()->layout)};
    ai::LocalReply r = ai::understand(in, "Cr\xC3\xA9" "e les variables de la liste d'E/S jointe");
    check(r.proposal && allVariables(r.proposal->ops).size() == 8, "<< cree les variables de la liste d'E/S jointe >> : une proposition");
    check(has(r.text, "Appliquer"), "toujours une proposition : rien n'est applique avant Appliquer");
    r = ai::understand(in, "Cr\xC3\xA9" "e les types IHM de la liste d'E/S jointe");
    check(r.proposal && opOf(r.proposal->ops, ai::Operation::Action::Type), "<< cree les types IHM >> : un type par famille");
    r = ai::understand(in, "Cr\xC3\xA9" "e les \xC3\xA9quipements du fichier r\xC3\xA9seau");
    check(r.proposal && r.proposal->ops.size() == 3 && r.proposal->ops[0].action == ai::Operation::Action::Equipment,
          "<< cree les equipements du fichier reseau >> : 3 equipements");
    r = ai::understand(in, "Que contient le fichier r\xC3\xA9seau joint ?");
    check(!r.proposal && has(r.text, "R\xC3\xA9seau et adressage IP") && has(r.text, "ligne 2"), "une question : ce qu'il contient, sans proposition");
    r = ai::understand(in, "");
    check(!r.proposal && has(r.text, "liste_es.xlsx"), "joint sans demande : ce qu'il contient");
    r = ai::understand(in, "Cr\xC3\xA9" "e une variable Niveau en REAL");
    const auto vars = r.proposal ? allVariables(r.proposal->ops) : std::vector<ai::Operation::VariableSpec>{};
    check(vars.size() == 1 && vars[0].name == "Niveau", "une autre demande n'est pas prise par le classeur");
}

// ========================================================================== 6. l'outil des lignes ===
void testTool() {
    std::printf("== 6. l'outil des lignes, les consignes ==\n");
    const sh::Workbook io = wbOf(ioXlsx());
    sh::Attached f = attached("liste_es.xlsx", ioXlsx(), sh::detect(io).chosen()->layout, "Liste E/S armoire A1");
    f.rows = 10;
    std::vector<sh::Attached> files{f};
    bool err = true;
    std::string out = sh::runRowsTool(files, "{\"combien\": 2}", &err);
    check(!err && has(out, "Lignes 1 \xC3\xA0 2") && has(out, "[4] P101_MARCHE | Pompe 101 en marche | DI | %I0.1.0") && has(out, "a_partir_de=3")
              && has(out, "10 lignes lues"),
          "la premiere page (" + out + ")");
    out = sh::runRowsTool(files, "{\"a_partir_de\": 9, \"combien\": 500}", &err);
    check(!err && has(out, "Lignes 9 \xC3\xA0 10") && has(out, "[14] K301.COMMANDE") && has(out, "Fin du tableau"), "la derniere page (200 au plus)");
    out = sh::runRowsTool(files, "{\"fichier\": \"absent.xlsx\"}", &err);
    check(err && has(out, "Aucun classeur"), "un fichier inconnu : dit");
    sh::Attached g = f;
    g.workbook = nullptr;   // pas encore lu : relu depuis sa copie (le fil du fournisseur)
    g.name = "copie.xlsx";
    files.push_back(g);
    out = sh::runRowsTool(files, "{}", &err);
    check(err && has(out, "Plusieurs classeurs"), "deux classeurs sans nom : il faut le dire");
    out = sh::runRowsTool(files, "{\"fichier\": \"copie\", \"combien\": 1}", &err);
    check(!err && has(out, "[4] P101_MARCHE"), "un classeur pas encore lu : relu depuis sa copie");
    out = sh::runRowsTool(files, "{\"fichier\": \"liste_es.xlsx\", \"feuille\": \"Absente\"}", &err);
    check(err && has(out, "absente"), "une feuille absente : dit, avec les feuilles");
    const std::string prompt = sh::workbooksPrompt(files, true);
    check(has(prompt, "lignes_classeur") && has(prompt, "Liste E/S armoire A1") && has(prompt, "n'invente jamais"), "les consignes : l'outil, le modele, la regle");
    check(has(sh::workbooksPrompt(files, false), "texte extrait") && sh::workbooksPrompt({}, true).empty(), "sans outil : le texte extrait ; sans classeur : rien");
    const std::string block = sh::compactBlock(f, 3);
    check(has(block, "10 lignes lues") && has(block, "[6] P101_VITESSE") && !has(block, "[7]") && has(block, "lignes_classeur"), "le bloc compact (" + block + ")");
    check(has(sh::rowsToolJson(), "\"lignes_classeur\"") && has(sh::rowsToolJson(), "input_schema"), "la definition de l'outil");
}

// ================================================================ 7. la piece jointe, l'Assistant ===
void testAttachment() {
    std::printf("== 7. la piece jointe, le choix, l'Assistant ==\n");
    useRoot("pc6");
    const fs::path src = writeFile(scratch() / "fichiers" / "liste_es.xlsx", ioXlsx());
    ai::Attachment a;
    std::string why;
    check(ai::addAttachment(src.string(), a, &why), "joint (" + why + ")");
    at::adopt(a, "c-essai");
    at::Outcome o = at::analyzeNow(a);
    check(o.workbook && o.model.empty() && o.analysis.state == "analysee" && has(o.analysis.summary, "D\xC3\xA9tection : Liste d'entr\xC3\xA9" "es/sorties"),
          "analysee : le classeur lu, detecte (" + o.analysis.summary + ")");
    check(o.analysis.extra.find("classeur") != nullptr, "la reconnaissance gardee avec l'analyse (extra << classeur >>)");
    check(at::applyOutcome(a, o), "remise a la piece");
    auto c = sh::choiceOf(a);
    check(c && !c->byUser && c->layout.genre == sh::Genre::IoList && c->rows == 10, "le choix : la detection (10 lignes)");
    // le modele enregistre : la prochaine analyse le reconnait (sans etape d'analyse IA)
    sh::FileModel m = sh::makeModel(*o.workbook, c->layout, "Liste E/S A1");
    check(sh::saveModel(m, &why), "un modele");
    o = at::analyzeNow(a);
    check(o.model == m.id && o.analysis.summary == "Mod\xC3\xA8le reconnu : Liste E/S A1 \xE2\x80\x94 10 lignes lues", "reconnu : " + o.analysis.summary);
    // un .xlsm
    ai::Attachment xm;
    const fs::path xsrc = writeFile(scratch() / "fichiers" / "liste_macros.xlsm", xlsxOf({ioSheet("Liste E/S", 3, kIoRows)}, true));
    check(ai::addAttachment(xsrc.string(), xm, &why), "un .xlsm se joint (" + why + ")");
    at::adopt(xm, "c-essai");
    const at::Outcome ox = at::analyzeNow(xm);
    check(ox.model == m.id && std::any_of(ox.analysis.details.begin(), ox.analysis.details.end(), [](const std::string& d) { return has(d, "macros"); }),
          "un .xlsm : reconnu, ses macros dites");
    // le choix de l'utilisateur : garde avec la piece, et dans la conversation enregistree
    sh::Choice ch = *c;
    ch.layout.set("unite", -1);
    ch.byUser = true;
    sh::setChoice(a, ch);
    ai::Conversation conv;
    conv.id = "c-essai";
    ai::Message msg;
    msg.id = "m1";
    msg.role = "utilisateur";
    msg.attachments.push_back(a);
    conv.messages.push_back(msg);
    const ai::Conversation back = ai::conversationFromJson(ai::toJson(conv));
    const auto c2 = sh::choiceOf(back.messages[0].attachments[0]);
    check(c2 && c2->byUser && !c2->layout.has("unite") && c2->layout.has("repere"), "le choix relu avec la conversation");
    at::ContextFile cf;
    cf.attachment = back.messages[0].attachments[0];
    cf.attachment.ext = "xlsx";
    check(ai::AiContextPane::workbookChip(cf) == "E/S (choisi)", "le volet le dit : " + ai::AiContextPane::workbookChip(cf));
    sh::clearChoice(cf.attachment);
    check(has(ai::AiContextPane::workbookChip(cf), "E/S "), "sans choix : le genre detecte et sa confiance");
}

void testAssistant() {
    std::printf("== 7b. l'Assistant de bout en bout ==\n");
    useRoot("pc7");
    auto doc = std::make_shared<hmi::Document>();
    ai::Assistant::Host host;
    host.document = [doc] { return doc; };
    host.activeView = [] { return hmi::kNoId; };
    ai::Assistant a(host);
    a.load();
    a.newConversation();
    const fs::path src = writeFile(scratch() / "fichiers" / "reports.xlsx", reportsXlsx());
    std::string why;
    check(a.attach(src.string(), &why), "joindre un classeur (" + why + ")");
    check(a.processing(), "en cours de lecture : l'envoi attend");
    check(a.waitWorkbooks(20000), "lu et reconnu sur le fil d'analyse");
    double t = 1.0;
    a.tick(t);
    a.tick(t += 3.0);
    check(!a.processing(), "pret a envoyer");
    const auto books = a.workbooks();
    check(books.size() == 1 && books[0].workbook && books[0].hasLayout && books[0].layout.genre == sh::Genre::Exchange, "le classeur, pour l'IA");
    const std::string id = books.empty() ? std::string() : books[0].id;
    // la fenetre << Modele... >> (sans ecran)
    ai::AiFileModelDialog d(a, id);
    check(d.ready() && has(d.headline(), "Reports / table d'\xC3\xA9" "change") && d.rowsRead() == 4, "la fenetre : " + d.headline());
    check(d.setColumn("sens", -1) && !d.layout().has("sens") && d.setColumn("sens", 4) && d.layout().colOf("sens") == 4, "un role, sa colonne");
    check(d.setHeaderLine(2) && d.layout().has("repere"), "la ligne d'en-tete");
    check(d.saveAsModel("Reports essai") && !d.modelId().empty(), "enregistrer comme modele : " + d.message());
    const ai::Attachment* att = a.attachmentById(id);
    check(att && sh::choiceOf(*att) && sh::choiceOf(*att)->modelName == "Reports essai", "le choix garde avec la piece (le modele)");
    check(d.applyModel("Reports essai") && d.messageOk(), "choisir un modele existant : il est cherche dans ce classeur");
    // une demande a l'IA integree, avec le classeur
    check(a.send("Cr\xC3\xA9" "e les variables du fichier joint", &why), "envoyer (" + why + ")");
    for (int k = 0; k < 4000 && a.generating(); ++k) a.tick(t += 0.05);
    const auto* conv = a.current();
    const int m = conv ? static_cast<int>(conv->messages.size()) - 1 : -1;
    const ai::Proposal* p = a.proposal(m);
    check(p && allVariables(p->ops).size() == 5, "la proposition : les 5 variables des reports");
    check(conv && !has(conv->messages.back().text, "ne lit pas le contenu"), "l'IA integree lit les classeurs");
    // ce qui part chez un fournisseur : la lecture compacte, pas le texte brut
    if (conv) {
        const ai::ProviderRequest req = a.requestFor(*conv, conv->messages.size());
        bool compact = false;
        for (const auto& turn : req.turns)
            for (const auto& pa : turn.attachments)
                compact = compact || (pa.prepared && has(pa.name, "(lignes lues)") && !pa.blocks.empty() && has(pa.blocks.front(), "lignes_classeur"));
        check(compact, "un fournisseur recoit la lecture compacte et l'outil, pas le texte brut");
    }
}

} // namespace

// 1.21.1 : UN CLASSEUR A PLUSIEURS FEUILLES (les entrees TOR, les sorties analogiques a un autre en-tete, une feuille libre,
// une feuille masquee) - toutes les feuilles du meme genre sont lues, l'IA les connait toutes.
void testSheets() {
    std::printf("== 9. toutes les feuilles du classeur (1.21.1) ==\n");
    useRoot("pc8");
    const Cells torHeader = {"Rep\xC3\xA8re", "D\xC3\xA9signation", "Type", "Adresse"};
    const Cells anaHeader = {"Voie", "Adresse", "Signal", "D\xC3\xA9signation", "Rep\xC3\xA8re"};
    TSheet tor = ioSheet("Entr\xC3\xA9" "es TOR", 2,
                         {{"DI_ARRET_URG", "Arr\xC3\xAAt d'urgence", "DI", "%I0.1.0"},
                          {"DI_PORTE", "Porte armoire", "DI", "%I0.1.1"},
                          {"DI_DEFAUT_VAR", "D\xC3\xA9" "faut variateur", "DI", "%I0.1.2"}},
                         torHeader);
    TSheet ana = ioSheet("Sorties Analogiques", 4,
                         {{"0", "%QW200.5.0", "AO", "Consigne variateur V258", "AO_V258"},
                          {"1", "%QW200.5.1", "AO", "Consigne variateur V259", "AO_V259"}},
                         anaHeader);
    TSheet free;
    free.name = "vars";
    free.rows = {{1, {"Notes de l'affaire"}}, {2, {"Ne pas modifier"}}};
    TSheet old = ioSheet("Archive", 2, {{"DI_ANCIEN", "Ancien capteur", "DI", "%I0.9.0"}}, torHeader);
    old.hidden = true;
    const std::string bytes = xlsxOf({tor, ana, free, old});
    const sh::Workbook wb = wbOf(bytes);
    const sh::Detection det = sh::detect(wb);
    check(det.sheets.size() == 4 && det.sheets[0].genre == sh::Genre::IoList && det.sheets[1].genre == sh::Genre::IoList
              && !det.sheets[2].headerFound && det.sheets[3].hidden,
          "4 feuilles : deux listes d'E/S aux en-t\xC3\xAAtes differents, une libre, une masquee");
    // la lecture : les deux feuilles d'E/S, pas la libre ni la masquee
    const auto readings = sh::readAll(wb, det.chosen()->layout);
    std::size_t rows = 0;
    for (const auto& rd : readings) rows += rd.rows.size();
    check(readings.size() == 2 && rows == 5, "les deux feuilles d'E/S lues (" + std::to_string(readings.size()) + " feuilles, " + std::to_string(rows) + " lignes)");
    // la piece jointe : l'analyse le dit, et l'Assistant connait les quatre feuilles
    const fs::path src = writeFile(scratch() / "fichiers" / "ali_luna.xlsx", bytes);
    ai::Attachment a;
    std::string why;
    check(ai::addAttachment(src.string(), a, &why), "joint (" + why + ")");
    at::adopt(a, "c-feuilles");
    at::Outcome o = at::analyzeNow(a);
    bool alsoRead = false, freeSaid = false, hiddenSaid = false;
    for (const auto& d : o.analysis.details) {
        alsoRead = alsoRead || has(d, "lues aussi");
        freeSaid = freeSaid || (has(d, "\xC2\xAB vars \xC2\xBB non lue") && has(d, "aucun en-t\xC3\xAA" "te"));
        hiddenSaid = hiddenSaid || (has(d, "\xC2\xAB Archive \xC2\xBB non lue") && has(d, "masqu"));
    }
    check(has(o.analysis.summary, "5 lignes lues") && alsoRead && freeSaid && hiddenSaid,
          "l'analyse : 5 lignes lues, les autres feuilles dites (" + o.analysis.summary + ")");
    check(at::applyOutcome(a, o), "remise a la piece");
    const sh::Attached f = sh::attachedOf(a, src.string(), o.workbook);
    std::size_t nRead = 0;
    for (const auto& si : f.sheets) nRead += si.read ? 1 : 0;
    check(f.sheets.size() == 4 && nRead == 2 && f.rows == 5 && !f.sheets[2].read && f.sheets[3].hidden && !f.sheets[3].read,
          "les feuilles de la piece : 4, dont 2 lues (" + std::to_string(nRead) + "), 5 lignes");
    const std::string block = sh::compactBlock(f);
    check(has(block, "Entr\xC3\xA9" "es TOR") && has(block, "Sorties Analogiques") && has(block, "\xC2\xAB vars \xC2\xBB") && has(block, "Archive")
              && has(block, "non lue") && has(block, "Consigne variateur V258") && has(block, "Arr\xC3\xAAt d'urgence"),
          "l'IA connectee : les quatre feuilles, et les premieres lignes de chaque feuille lue");
    const std::string prompt = sh::workbooksPrompt({f}, true);
    check(has(prompt, "4 feuilles") && has(prompt, "feuille=\"vars\"") && has(prompt, "Ne d\xC3\xA9" "cris jamais le classeur entier"),
          "les consignes : toutes les feuilles, et la regle");
    const std::string desc = sh::describeFile(f, readings);
    check(has(desc, "Entr\xC3\xA9" "es TOR") && has(desc, "Sorties Analogiques") && has(desc, "Les autres feuilles") && has(desc, "\xC2\xAB vars \xC2\xBB"),
          "l'IA integree : les feuilles lues et les autres");
    hmi::Project project;
    const sh::Plan p = sh::planVariables(f, readings, &project);
    const auto vars = allVariables(p.ops);
    check(p.ok && vars.size() == 5 && varNamed(vars, "AO_V258") && varNamed(vars, "DI_PORTE"),
          "les variables des deux feuilles (" + std::to_string(vars.size()) + ")");
    bool err = true;
    const std::string free2 = sh::runRowsTool({f}, "{\"feuille\": \"vars\"}", &err);
    check(!err && has(free2, "Notes de l'affaire"), "l'outil lit aussi la feuille libre, case par case");
}

int main() {
    testRead();
    testDetect();
    testModels();
    testXia();
    testActions();
    testLocal();
    testTool();
    testAttachment();
    testAssistant();
    testSheets();
    std::error_code ec;
    fs::remove_all(scratch(), ec);
    std::printf("%d verifications, %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
