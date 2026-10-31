// =============================================================================
//  app/FileView.hpp - 1.21.0 : L'APERCU DE TOUS LES FICHIERS, SANS ECRAN
// -----------------------------------------------------------------------------
//  LA DEMANDE : "dans les resources, ajouter une visualisation de tous les
//  types de fichiers, pareil pour l'objet explorateur de dossier etc".
//
//  CE FICHIER NE DESSINE RIEN. Il reconnait un fichier (son extension, puis ses
//  premiers octets : une image PNG nommee .jpg reste une image, un .txt qui est
//  un zip est un zip), et il en tire ce que l'apercu montre (app/FileViewer) :
//
//    image (PNG, JPEG, BMP, GIF, ICO, SVG)   l'image decodee (bornee), ses dimensions
//    video (MP4, AVI, MOV, WMV, WEBM)        duree, dimensions, codec ; les octets
//    son (WAV, MP3)                          duree, l'onde (256 colonnes) ; les octets
//    PDF                                     sa version ; les octets (la visionneuse)
//    texte, code (TXT, LOG, ST, C, PY...)    les lignes, l'encodage detecte (UTF-8,
//                                            UTF-16, Windows-1252), les fins de ligne
//    CSV, TSV                                une grille (le separateur devine)
//    classeur (XLSX, XLSM)                   ses onglets, les premieres lignes de chacun
//    JSON, XML                               le texte indente, la profondeur de chaque
//                                            ligne (deplier, replier)
//    DOCX                                    les paragraphes et les titres
//    ZIP (.xihm, .xapi s'ils en sont)        la liste des entrees (nom, taille)
//    police (TTF, OTF)                       famille, style ; un echantillon ecrit
//    .XPG, .XHW                              ce qu'en dit app/FilePreview
//    dossier                                 son contenu resume (app/FilePreview)
//    le reste (exe, binaire inconnu)         le genre reconnu a sa signature et
//                                            un vidage hexadecimal du debut
//
//  BORNES : rien n'est lu en entier sans raison - quelques Mo de texte, N lignes,
//  N lignes par onglet, N entrees de zip, quelques Ko pour le vidage ; un fichier
//  trop gros pour la memoire le dit. JAMAIS D'EXCEPTION : un fichier abime donne
//  `error` ("illisible : pourquoi"), le reste de l'apercu (nom, taille, date) tient.
//
//  LA SOURCE : un chemin sur le disque, ou des octets deja en memoire (une
//  ressource du projet), ou une fonction qui les lit (une entree de zip) - sur le
//  fil de fond de l'apercu. Il se verifie sans ecran : fileview_test.
// =============================================================================
#pragma once

#include "../hmi/HmiMedia.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app::fileview {

// Ce que l'apercu montre.
enum class Kind : std::uint8_t {
    Missing,        // introuvable (ou rien a montrer)
    Folder,
    Image,
    Video,
    Sound,
    Pdf,
    Text,           // texte et code
    Csv,            // CSV, TSV
    Workbook,       // XLSX, XLSM
    Json,
    Xml,
    Docx,
    Zip,
    Font,
    ControlExpert,  // .XPG, .XHW
    Binary,         // le reste : la signature et le vidage hexadecimal
};
[[nodiscard]] std::string_view kindLabel(Kind) noexcept;   // "Image", "Texte", "Classeur"...

struct Limits {
    std::size_t   textBytes{4u << 20};        // le texte lu au plus (4 Mo)
    std::size_t   textLines{20000};           // ... et ses lignes
    std::size_t   lineChars{2000};            // une ligne plus longue est coupee
    std::size_t   gridRows{500};              // les lignes d'une grille (CSV, chaque onglet)
    std::size_t   gridCols{64};
    std::size_t   sheets{32};                 // les onglets lus
    std::size_t   zipEntries{5000};
    std::size_t   hexBytes{4096};             // le vidage hexadecimal
    std::size_t   paragraphs{5000};           // DOCX
    std::uint64_t memoryBytes{128ull << 20};  // ce qui se lit en memoire au plus (image, son, video, police, PDF)
    std::uint64_t imagePixels{64ull << 20};   // une image plus grande ne se decode pas (ses dimensions seules)
    int           imageSide{4096};            // ... et se ramene a ce cote au plus
    const std::atomic<bool>* cancel{nullptr}; // vrai : abandonner (un autre fichier est choisi)
};

// ---- reconnaitre ------------------------------------------------------------------------
// Ce que disent les premiers octets ; format vide : rien de connu.
struct Signature {
    std::string format;     // "PNG", "PDF", "ZIP", "EXE"...
    std::string label;      // "Image PNG", "Programme Windows (PE)"...
    Kind        kind{Kind::Binary};
};
[[nodiscard]] Signature signatureOf(const std::uint8_t* data, std::size_t size);
// Le genre d'apres le nom seul (son extension).
[[nodiscard]] Kind kindOfName(std::string_view name);
// L'extension, en minuscules, sans le point ("MAST.XPG" -> "xpg").
[[nodiscard]] std::string extensionOf(std::string_view name);
// Le genre retenu : l'extension, corrigee par la signature quand elle ment.
struct Recognition {
    Kind        kind{Kind::Binary};
    std::string label;      // "Image PNG", "Texte", "Archive zip"...
    std::string format;     // "PNG", "TXT"...
    bool        mismatch{false};   // l'extension ne dit pas ce que sont les octets
    std::string note;              // ... et ce qu'ils sont ("en fait : une archive zip")
};
[[nodiscard]] Recognition recognize(std::string_view name, const std::uint8_t* head, std::size_t size);

// ---- le texte -----------------------------------------------------------------------------
enum class Encoding : std::uint8_t { Utf8, Utf8Bom, Utf16Le, Utf16Be, Windows1252 };
[[nodiscard]] std::string_view encodingLabel(Encoding) noexcept;   // "UTF-8", "UTF-16 LE", "Windows-1252"...
[[nodiscard]] Encoding detectEncoding(std::string_view bytes);
// Des octets qui ne sont pas du texte (des octets nuls hors UTF-16, trop de caracteres de controle).
[[nodiscard]] bool looksBinary(std::string_view bytes);
// En UTF-8 (sans la marque d'ordre ; un octet impossible devient U+FFFD).
[[nodiscard]] std::string toUtf8(std::string_view bytes, Encoding);
struct TextView {
    Encoding                 encoding{Encoding::Utf8};
    std::vector<std::string> lines;       // tabulations deployees (4), controles en espaces
    std::string              endings;     // "CRLF (Windows)", "LF (Unix)", "m\xC3\xA9lang\xC3\xA9" "es", "" (une ligne)
    bool                     truncated{false};
};
// `complete` : les octets sont tout le fichier (sinon le texte est dit coupe).
[[nodiscard]] TextView readText(std::string_view bytes, const Limits& = {}, bool complete = true);

// ---- les grilles (CSV, TSV, classeur) -----------------------------------------------------
struct Sheet {
    std::string                           name;
    std::vector<std::vector<std::string>> rows;
    std::size_t                           totalRows{0};   // dans le fichier (0 : inconnu)
    std::size_t                           columns{0};     // la ligne la plus large montree
    bool                                  truncated{false};
    bool                                  hidden{false};
};
[[nodiscard]] char guessSeparator(std::string_view text);   // ';' ',' '\t' '|'
[[nodiscard]] Sheet parseCsv(std::string_view utf8, char separator, const Limits& = {});
// Un classeur XLSX / XLSM en memoire : ses onglets (les premieres lignes de chacun). Faux : `why`.
bool readWorkbook(std::string_view bytes, std::vector<Sheet>& sheets, bool& macros, std::string& why, const Limits& = {});

// ---- JSON, XML indentes ---------------------------------------------------------------------
struct Tree {
    std::vector<std::string> lines;
    std::vector<int>         depth;       // une par ligne
    bool                     ok{true};    // faux : mal forme (le texte est montre tel quel, `error` dit ou)
    std::string              error;
    bool                     truncated{false};
};
[[nodiscard]] Tree prettyJson(std::string_view text, std::size_t maxLines = 20000);
[[nodiscard]] Tree prettyXml(std::string_view text, std::size_t maxLines = 20000);
// Les lignes visibles quand certaines sont repliees : une ligne repliee cache celles qui la suivent plus profondes.
[[nodiscard]] std::vector<std::size_t> visibleLines(const std::vector<int>& depth, const std::vector<bool>& folded);
// Une ligne qui ouvre un bloc (la suivante est plus profonde) : elle se replie.
[[nodiscard]] bool opensBlock(const std::vector<int>& depth, std::size_t line);

// ---- DOCX ---------------------------------------------------------------------------------
struct Paragraph {
    int         level{0};       // 0 : un texte ; 1..9 : un titre de ce niveau
    bool        table{false};   // une ligne de tableau (ses cellules separees par " | ")
    bool        item{false};    // un element de liste
    std::string text;
};
bool readDocx(std::string_view bytes, std::vector<Paragraph>& out, std::string& why, const Limits& = {});

// ---- ZIP ----------------------------------------------------------------------------------
struct ZipItem {
    std::string   name;         // le chemin dans le zip, tel quel
    std::uint64_t size{0}, packed{0};
    bool          folder{false};
    bool          encrypted{false};
};
// `total` : les entrees du zip (au-dela de la borne, elles sont comptees sans etre gardees).
bool listZip(std::string_view bytes, std::vector<ZipItem>& out, std::size_t& total, std::string& why, const Limits& = {});
bool listZipFile(const std::string& path, std::vector<ZipItem>& out, std::size_t& total, std::string& why, const Limits& = {});

// ---- le vidage hexadecimal ---------------------------------------------------------------
//  "00000000  4D 5A 90 00 03 00 00 00  04 00 00 00 FF FF 00 00  |MZ..............|"
[[nodiscard]] std::vector<std::string> hexDump(std::string_view bytes, std::size_t maxBytes = 4096, std::uint64_t offset = 0);

// ---- la source -------------------------------------------------------------------------------
struct Source {
    std::string  name;          // le nom montre (son extension decide d'abord)
    std::string  path;          // sur le disque ; vide : en memoire seulement
    hmi::BlobPtr data;          // les octets deja en memoire (une ressource du projet)
    // Les octets lus a la demande, sur le fil de fond (une entree de zip) ; faux : `why`.
    std::function<bool(hmi::Bytes& out, std::string& why)> fetch;
    std::int64_t modified{0};   // secondes depuis 1970 ; 0 : lue sur le disque (ou inconnue)
    std::string  origin;        // "Ressource du projet", "Dans archive.zip"...
    std::string  shownPath;     // le chemin montre (une entree de zip : archive.zip\dossier\nom) ; vide : `path`
    std::string  openFolder;    // le dossier qu'Ouvrir le dossier ouvre ; vide : celui de `path`
    // La cle : deux sources de meme cle montrent la meme chose (rien a relire).
    [[nodiscard]] std::string key() const;
    [[nodiscard]] bool empty() const noexcept { return name.empty() && path.empty() && !data && !fetch; }
};
[[nodiscard]] Source fromPath(const std::string& path);
[[nodiscard]] Source fromBytes(std::string name, hmi::BlobPtr data, std::string origin = {});

// ---- l'apercu calcule ----------------------------------------------------------------------
struct Preview {
    Kind          kind{Kind::Missing};
    std::string   name, path, shownPath, typeLabel, format, origin;
    std::uint64_t size{0};
    std::int64_t  modified{0};
    bool          onDisk{false};
    std::vector<std::pair<std::string, std::string>> facts;   // ("Dimensions", "1600 x 900")
    std::vector<std::string> notes;                           // ce qui merite d'etre dit
    std::string   error;                                      // illisible : pourquoi (vide : bon)
    // Le contenu, selon le genre.
    hmi::BlobPtr  bytes;                                      // image, son, video, police, PDF : les octets
    std::shared_ptr<const hmi::Rgba> image;                   // une image decodee (bornee)
    std::vector<std::shared_ptr<const hmi::Rgba>> frames;     // un GIF anime : ses images (bornees), ...
    std::vector<int> frameMs;                                 // ... et la duree de chacune
    hmi::MediaInfo media;                                     // image, son, video, police : ce que disent les octets
    std::vector<float> envelope;                              // un son : 256 paires (min, max)
    std::vector<std::string> lines;                           // texte, JSON, XML, vidage hexadecimal
    std::vector<int>         depth;                           // JSON, XML : la profondeur de chaque ligne
    Encoding      encoding{Encoding::Utf8};
    bool          truncated{false};                           // ce qui est montre n'est qu'un debut
    std::vector<Sheet> sheets;                                // CSV (une), classeur (ses onglets)
    std::vector<Paragraph> paragraphs;                        // DOCX
    std::vector<ZipItem> entries;                             // ZIP
    std::size_t   entryCount{0};
    std::vector<std::shared_ptr<const hmi::Rgba>> fontSamples;   // une police : l'echantillon (blanc, l'alpha porte le texte)
    std::vector<std::string> fontSampleTexts;
    [[nodiscard]] std::string fact(std::string_view label) const;   // la valeur d'un fait ("" : absent)
};

// Calculer l'apercu (jamais d'exception).
[[nodiscard]] Preview load(const Source&, const Limits& = {});
// Ce qui merite le fil de fond : un fichier sur le disque, une entree de zip, des octets nombreux, un son (son onde
// se decode), un classeur.
[[nodiscard]] bool slow(const Source&);
// Ce que montre l'apercu, une ligne par information (les essais, les scripts).
[[nodiscard]] std::vector<std::string> describeLines(const Preview&);

} // namespace app::fileview
