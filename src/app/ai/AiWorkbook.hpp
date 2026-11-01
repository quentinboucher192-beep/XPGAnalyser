// =============================================================================
//  app/ai/AiWorkbook.hpp - 1.21.0 : RECONNAITRE UN CLASSEUR JOINT (E/S, reseau, reports)
// -----------------------------------------------------------------------------
//  Un classeur (.xlsx, .xlsm) joint a l'Assistant est le plus souvent l'un de
//  trois documents du metier :
//
//    - une LISTE D'ENTREES/SORTIES : repere (tag), designation, type (DI, DO,
//      AI, AO, TOR, ANA), adresse (%I, %Q, %IW, %QW, I0.0, IW64), rack, slot,
//      voie, carte, echelle min/max, unite ;
//    - un fichier RESEAU ET ADRESSAGE IP : equipement, adresse IP, masque,
//      passerelle, VLAN, protocole, port, adresse MAC, esclave ;
//    - des REPORTS / une TABLE D'ECHANGE : %MW, %M, %MD, %MF (ou MW10, DB1.DBW2),
//      nom, type (BOOL, INT, REAL...), designation, sens (lecture, ecriture) ;
//
//  sinon << autre >>. Ce module, SANS ECRAN NI MEMOIRE (des fonctions pures,
//  essayables, rapides), fait trois choses :
//
//    1. LIRE le classeur (readWorkbook) : chaque feuille, ses cellules A LEUR
//       PLACE (le numero de ligne de l'attribut r, pas l'ordre d'arrivee : une
//       ligne vide n'est pas ecrite dans le fichier), ses cellules fusionnees ;
//    2. DETECTER (detect) : pour chaque feuille, la ligne d'en-tete (celle dont
//       les intitules repondent le mieux aux mots du metier - minuscules, sans
//       accents, le pluriel ramene au singulier, en francais et en anglais),
//       la correspondance role -> colonne, puis le GENRE et sa CONFIANCE, tires
//       des intitules ET des valeurs (la part des adresses %I/%Q, %M/%MW, IP,
//       des types DI/DO/AI/AO lus sous l'en-tete) ;
//    3. LIRE LES LIGNES avec une mise en page (read) : la feuille, la ligne
//       d'en-tete, les colonnes, les regles (lignes a sauter sous l'en-tete,
//       valeurs tenues pour vides, cases fusionnees recopiees, colonnes
//       recopiees vers le bas) ; une ligne vide ou sans cle est comptee, pas lue.
//
//  Il tourne sur le fil d'analyse des pieces jointes (AiAttachments.hpp), jamais
//  sur celui de l'interface. Les modeles de fichiers gardes en memoire et ce que
//  l'IA en fait (variables, types, equipements) : AiFileModels.hpp.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app::ai::sheets {

// ------------------------------------------------------------------ le classeur ---
struct Merge {
    std::size_t r0{0}, c0{0}, r1{0}, c1{0};   // une plage fusionnee (0 : la premiere ligne, la colonne A), bornes comprises
};
struct Sheet {
    std::string                           name;
    bool                                  hidden{false};
    std::vector<std::vector<std::string>> rows;     // a leur place (une ligne absente du fichier : vide)
    std::vector<Merge>                    merges;
    bool                                  truncated{false};   // plus de lignes que la limite : la suite n'est pas lue
    // La case (r, c) telle que lue ; vide hors de la feuille.
    [[nodiscard]] const std::string& cell(std::size_t r, std::size_t c) const noexcept;
    [[nodiscard]] std::size_t        width() const noexcept;   // la ligne la plus large
};
struct Workbook {
    std::vector<Sheet>       sheets;
    bool                     formulas{false};   // des cellules calculees : la valeur lue est le dernier resultat
    bool                     macros{false};     // un vbaProject.bin (un .xlsm) : jamais execute
    std::vector<std::string> notes;             // les feuilles illisibles, les limites
    std::size_t              skippedSheets{0};  // au-dela de la limite
    [[nodiscard]] const Sheet* sheet(std::string_view name) const noexcept;   // par son nom (sans casse a defaut)
};
struct ReadLimits {
    std::size_t   maxSheets{30};
    std::size_t   maxRows{20000};
    std::uint64_t maxXmlBytes{128ull * 1024 * 1024};   // une feuille plus grosse une fois decompressee : illisible (une bombe)
};
[[nodiscard]] bool workbookExt(std::string_view ext) noexcept;   // "xlsx", "xlsm" (sans casse)
// Les octets d'un .xlsx / .xlsm -> le classeur. Faux et `why` (en francais) : pas une archive, pas un classeur.
bool readWorkbook(std::string_view bytes, Workbook& out, std::string* why = nullptr, const ReadLimits& limits = {});
bool readWorkbookFile(const std::string& path, Workbook& out, std::string* why = nullptr, const ReadLimits& limits = {});
// "A", "B"... "AA" : la lettre d'une colonne (0 : A).
[[nodiscard]] std::string columnLetter(std::size_t col);

// ------------------------------------------------------------------- les genres ---
enum class Genre : std::uint8_t { Other, IoList, Network, Exchange };
[[nodiscard]] std::string_view genreKey(Genre g) noexcept;          // "autre", "es", "reseau", "reports"
[[nodiscard]] std::optional<Genre> genreFromKey(std::string_view key) noexcept;
[[nodiscard]] std::string genreLabel(Genre g);                      // "Liste d'entr\xC3\xA9" "es/sorties"...
[[nodiscard]] std::string genreShort(Genre g);                      // "E/S", "R\xC3\xA9seau", "Reports", "Autre"
inline constexpr Genre kGenres[] = {Genre::IoList, Genre::Network, Genre::Exchange, Genre::Other};

// -------------------------------------------------------------------- les roles ---
//  Une colonne a un ROLE (sa cle, ASCII) : repere, designation, type, adresse, mot, bit, rack, slot, voie, carte, min,
//  max, unite, equipement, ip, masque, passerelle, vlan, protocole, port, mac, esclave, sens, commentaire.
struct RoleInfo {
    std::string_view key;
    std::string_view label;
};
[[nodiscard]] const std::vector<RoleInfo>& roles();
[[nodiscard]] std::string roleLabel(std::string_view key);    // une cle inconnue : elle-meme
[[nodiscard]] bool        knownRole(std::string_view key) noexcept;
// Les roles qui comptent pour un genre, dans l'ordre de la fenetre (autre : tous).
[[nodiscard]] std::vector<std::string> rolesFor(Genre g);
// Un intitule ramene a sa forme comparable : minuscules, sans accents, ce qui n'est ni lettre ni chiffre ni % devient
// un blanc, chaque mot au singulier (<< Mnemoniques API >> -> << mnemonique api >>, << @IP >> -> << ip >>).
[[nodiscard]] std::string normalizeHeader(std::string_view text);
// Le role que suggere un intitule seul, et sa force (0 : aucun ; 1000 et plus : l'intitule exact d'un mot-cle).
[[nodiscard]] std::pair<std::string, int> roleOfHeader(std::string_view text);

// ------------------------------------------------------------- les valeurs lues ---
[[nodiscard]] bool isIoAddress(std::string_view v);       // %I0.1, %QW2, %IW0.3.2, I0.0, Q4.1, IW64, PEW256, %IX1.0
[[nodiscard]] bool isMemoryAddress(std::string_view v);   // %M10, %MW100, %MD20, %MF30, %MW100.3, MW10, M10.0, DB1.DBW2
[[nodiscard]] bool isIpv4(std::string_view v);            // 192.168.1.10 (et /24)
[[nodiscard]] bool isMac(std::string_view v);             // 00:1A:2B:3C:4D:5E, 00-1A-2B-3C-4D-5E
// Le type de signal d'une case : "DI", "DO", "AI", "AO", "TOR", "ANA" ; vide : aucun.
[[nodiscard]] std::string signalOf(std::string_view v);
// Le type de donnee IHM d'une case (BOOL, INT, DINT, UINT, UDINT, WORD, DWORD, REAL, LREAL, STRING, TIME) ; vide : aucun.
[[nodiscard]] std::string dataTypeOf(std::string_view v);
// Le type IHM qu'implique une adresse (%M, %I, %Q, M10.0 -> BOOL ; %MW, %IW -> INT ; %MD -> DINT ; %MF -> REAL) ; vide : aucun.
[[nodiscard]] std::string typeOfAddress(std::string_view v);

// --------------------------------------------------------------- la mise en page ---
struct Column {
    std::string role;
    std::size_t col{0};
    std::string header;   // l'intitule tel qu'ecrit
    bool operator==(const Column&) const = default;
};
struct Rules {
    std::size_t              skipAfterHeader{0};   // les lignes sautees sous l'en-tete (unites, sous-titres)
    std::vector<std::string> emptyValues{"-", "--", "/", "n/a", "na", "neant", "nc", "?"};   // normalisees : tenues pour vides
    std::vector<std::string> carryDown;            // les roles dont une case vide reprend la valeur du dessus
    bool                     mergedCells{true};    // une case fusionnee : sa valeur dans chacune de ses lignes
    std::vector<std::string> keyRoles;             // une ligne sans aucun de ces roles est ignoree (vide : la regle du genre)
    bool operator==(const Rules&) const = default;
};
struct Layout {
    Genre               genre{Genre::Other};
    std::string         sheet;
    std::size_t         headerRow{0};   // 0 : la premiere ligne
    std::vector<Column> columns;        // une colonne par role
    Rules               rules;
    [[nodiscard]] int  colOf(std::string_view role) const noexcept;   // -1 : aucune
    [[nodiscard]] bool has(std::string_view role) const noexcept { return colOf(role) >= 0; }
    void               set(std::string_view role, int col, std::string header = {});   // -1 : retire le role
    bool operator==(const Layout&) const = default;
};
// Les roles cles d'un genre (une ligne sans aucun d'eux est ignoree).
[[nodiscard]] std::vector<std::string> defaultKeyRoles(Genre g);
// Les intitules normalises d'une ligne (la signature d'un modele), jusqu'a la derniere case non vide.
[[nodiscard]] std::vector<std::string> headerCells(const Sheet& s, std::size_t row);
// L'intitule d'une case tel qu'ecrit : le sien, ou celui de la plage fusionnee qui la couvre ; et la largeur de la ligne.
[[nodiscard]] std::string headerText(const Sheet& s, std::size_t row, std::size_t col);
[[nodiscard]] std::size_t headerWidthOf(const Sheet& s, std::size_t row);

// ------------------------------------------------------------------ la detection ---
struct SheetDetection {
    std::string              sheet;
    std::size_t              index{0};          // dans Workbook::sheets
    Genre                    genre{Genre::Other};
    double                   confidence{0};     // 0 a 1
    double                   scores[4]{0, 0, 0, 0};   // par genre (l'ordre de l'enum)
    bool                     headerFound{false};
    Layout                   layout;            // la ligne d'en-tete, les colonnes reconnues, les regles par defaut
    std::size_t              dataRows{0};       // les lignes non vides sous l'en-tete
    std::vector<std::string> reasons;           // pourquoi ("12 adresses %MW sur 12 lignes", "en-t\xC3\xAA" "te : Adresse IP, Masque")
    bool                     hidden{false};     // 1.21.1 : une feuille masquee (detect)
};
struct Detection {
    std::vector<SheetDetection> sheets;     // une par feuille
    std::size_t                 best{0};    // la feuille retenue (sheets vide : aucune)
    [[nodiscard]] const SheetDetection* chosen() const noexcept { return best < sheets.size() ? &sheets[best] : nullptr; }
    [[nodiscard]] Genre  genre() const noexcept { return chosen() ? chosen()->genre : Genre::Other; }
    [[nodiscard]] double confidence() const noexcept { return chosen() ? chosen()->confidence : 0.0; }
};
// En dessous, une feuille est << autre >>.
inline constexpr double kMinScore = 0.35;
[[nodiscard]] SheetDetection detectSheet(const Sheet& s, std::size_t index = 0);
[[nodiscard]] Detection      detect(const Workbook& wb);
// La correspondance des colonnes d'une feuille pour un en-tete DONNE (la fenetre : une autre ligne d'en-tete choisie).
[[nodiscard]] Layout layoutAt(const Sheet& s, std::size_t headerRow, Genre genre);

// ------------------------------------------------------------------ lire les lignes ---
struct Row {
    std::size_t                                      line{0};   // la ligne du classeur (1 : la premiere)
    std::vector<std::pair<std::string, std::string>> values;    // role -> valeur (les roles de la mise en page)
    [[nodiscard]] const std::string& value(std::string_view role) const noexcept;
};
struct Reading {
    std::string              sheet;
    std::size_t              headerLine{0};   // 1 : la premiere ligne
    std::vector<Column>      columns;
    std::vector<Row>         rows;
    std::size_t              empty{0};        // lignes vides
    std::size_t              ignored{0};      // lignes sans cle (ni repere, ni adresse...)
    std::vector<std::size_t> ignoredLines;    // les premieres (20 au plus)
    std::vector<std::string> problems;        // la feuille absente, coupee a la limite
    bool                     ok{false};
};
// `maxRows` : s'arreter apres tant de lignes gardees (un apercu).
[[nodiscard]] Reading read(const Workbook& wb, const Layout& layout, std::size_t maxRows = static_cast<std::size_t>(-1));

// ------------------------------------------------- un classeur joint, pour l'IA ---
//  1.21.1 : chaque feuille du classeur, telle que l'analyse l'a vue - l'IA les connait TOUTES (lues ou non), et lit
//  n'importe laquelle (l'outil des lignes, son parametre << feuille >>).
struct SheetInfo {
    std::string name;
    Genre       genre{Genre::Other};
    bool        headerFound{false};
    std::size_t headerLine{0};   // 1 : la premiere ligne ; 0 : aucun en-tete reconnu
    std::size_t rows{0};         // les lignes lues (une feuille lue), sinon les lignes non vides sous l'en-tete
    bool        hidden{false};
    bool        read{false};     // lue avec la mise en page retenue (la feuille du modele, ses jumelles, celles du meme genre)
};
//  Ce que l'Assistant sait d'un classeur joint a la conversation : la piece, sa copie, le classeur lu (nul : pas encore),
//  la mise en page retenue (le choix de l'utilisateur, sinon le modele reconnu, sinon la detection).
struct Attached {
    std::string                     id, name, path;
    std::shared_ptr<const Workbook> workbook;
    bool                            hasLayout{false};
    Layout                          layout;
    std::string                     modelId, modelName;   // le modele reconnu ou choisi ; vide : une detection
    bool                            byUser{false};        // choisi dans la fenetre << Modele... >>
    double                          confidence{0};
    std::size_t                     rows{0};              // les lignes lues (a l'analyse, ou au choix) - toutes feuilles comprises
    std::vector<SheetInfo>          sheets;               // 1.21.1 : toutes les feuilles (vide : une analyse d'avant)
};

} // namespace app::ai::sheets
