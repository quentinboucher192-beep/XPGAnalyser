// =============================================================================
//  hmi/HmiDocView.hpp - 1.12.30 : LA VISIONNEUSE DE DOCUMENTS, SANS ECRAN
// -----------------------------------------------------------------------------
//  Ce que le moteur, le dessin, le canevas et les essais partagent : la barre
//  d'outils et ses boutons (des cibles de 32 pixels au moins, pour un ecran
//  tactile), la colonne des vignettes, la recherche, le menu des zooms ; ou sont
//  les pages a un zoom et un defilement donnes ; ce que fait chaque geste.
//
//  LE REPERE : les coordonnees locales de l'objet (0,0 en haut a gauche, w x h),
//  en pixels de la vue. Les pages se suivent de haut en bas, centrees, separees
//  de kGap ; une page a 100 % a la taille que dit son document (HmiDocument.hpp).
//  Le defilement (scrollX, scrollY) est celui de ce contenu dans la zone des
//  pages. Navigation interdite : une seule page a la fois (celle que dit la
//  propriete Page ou sa variable), qu'on peut zoomer et deplacer.
//
//  LES GESTES (des "parties", comme les autres objets a parties) :
//    doc:first doc:prev doc:next doc:last      les pages
//    doc:page                                   le champ "3 / 12" (taper un numero)
//    doc:zoomout doc:zoomin doc:zoommenu doc:zoom:<i>   le zoom, ses choix tout faits
//    doc:rotate doc:thumbs doc:search           tourner, les vignettes, la recherche
//    doc:searchfield doc:hitprev doc:hitnext doc:searchclose
//    doc:thumb:<n>                              une vignette (la page n, a partir de 1)
//    doc:area                                   la zone des pages (elle prend le clavier)
//    doc:wheel:<dy> doc:wheelx:<dx> doc:thumbwheel:<dy>   la molette (pixels)
//    doc:zoomat:<facteur>:<x>:<y>               Ctrl+molette, autour du curseur (local)
//    doc:pan:<dx>:<dy>                          glisser (la main)
//    doc:dbl:<x>:<y>                            double-clic : ajuste <-> 100 %
//    doc:key:<touche>                           PageUp PageDown Home End Plus Minus Zero Up Down
// =============================================================================
#pragma once

#include "HmiDocument.hpp"
#include "HmiModel.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::docview {

inline constexpr double kBar = 40;          // la barre d'outils (et celle de la recherche)
inline constexpr double kButton = 32;       // une cible : 32 x 32 au moins
inline constexpr double kGap = 12;          // entre deux pages
inline constexpr double kMargin = 12;       // autour des pages
inline constexpr double kMinZoom = 0.1, kMaxZoom = 8.0;
inline constexpr double kWheelStep = 60;    // un cran de molette, en pixels

// Les zooms tout faits (le menu, et Zoom de depart).
inline constexpr std::string_view kZoomChoices[] = {
    "ajuster \xC3\xA0 la largeur", "page enti\xC3\xA8re", "50 %", "75 %", "100 %", "125 %", "150 %", "200 %", "300 %", "400 %"};

enum class Fit : std::uint8_t { None, Width, Page };

// Ce qui se lit sur l'objet (evalue).
struct Settings {
    bool toolbar{true}, thumbnails{false}, navigation{true};
    Fit  startFit{Fit::Width};
    double startZoom{1.0};
};
[[nodiscard]] Settings settingsOf(const Object&);
// "150 %", "ajuster a la largeur", "page entiere" (sans casse ni accent) ; faux : illisible.
bool parseZoom(std::string_view text, Fit& fit, double& zoom);

// L'etat d'une visionneuse (le moteur le garde ; l'editeur en fait un neuf).
struct State {
    double      zoom{1.0};            // 1 = 100 % ; le zoom vrai quand fit vaut None
    Fit         fit{Fit::Width};
    double      scrollX{0}, scrollY{0};
    int         quarter{0};           // tourne de quarter quarts de tour (sens horaire)
    bool        thumbs{false};
    bool        searchOpen{false};
    bool        zoomMenu{false};
    int         page{1};              // la page courante (1..)
    int         single{0};            // navigation interdite : la page montree seule (1..) ; 0 : toutes
    double      thumbScroll{0};
    // la saisie
    bool        editingPage{false};
    std::string pageText;
    bool        editingSearch{false};
    std::string query;
    int         hit{-1};              // l'occurrence montree (0..)
};
[[nodiscard]] State initialState(const Settings&);

// Les boutons de la barre (et de la recherche) ; les champs.
struct Button {
    std::string part;
    Box         box;
    bool        enabled{true}, active{false};
};
struct Layout {
    double              w{0}, h{0};
    Box                 toolbar, searchBar, thumbs, area;
    std::vector<Button> buttons;
    Box                 pageField, zoomField, searchField;   // vides : pas montres
    Box                 hitLabel;                            // "3 / 12" de la recherche
    std::vector<Box>    zoomMenu;                            // ouvert : une entree par choix de kZoomChoices
};
[[nodiscard]] Layout layoutOf(double w, double h, const Settings&, const State&);

// Le contenu : ou est chaque page au zoom de l'etat.
struct Placed {
    int index{0};       // 0..
    Box rect;           // en local
};
struct Content {
    double              zoom{1};        // le zoom vrai (l'ajustement compte)
    double              width{0}, height{0};
    std::vector<int>    pages;          // les pages posees (toutes, ou la seule montree)
    std::vector<double> tops;           // le haut de chacune, dans le contenu
    std::vector<double> widths, heights;
    double              originX{0}, originY{0};   // ou tombe le point (0,0) du contenu, en local
    double              maxX{0}, maxY{0};         // le defilement possible
};
// `sizes` : les pages a 100 % (non tournees).
[[nodiscard]] Content contentOf(const Layout&, const State&, const std::vector<doc::Size>& sizes);
// Les pages qui se voient dans la zone, et ou.
[[nodiscard]] std::vector<Placed> visiblePages(const Layout&, const Content&);
// La page sous un point local (0..), -1 : aucune.
[[nodiscard]] int pageAt(const Layout&, const Content&, double lx, double ly);
// Un rectangle d'une page (pixels a 100 %, page non tournee) a l'ecran (la page tournee de quarter).
[[nodiscard]] Box onScreen(const Box& inPage, const doc::Size& pageSize, const Placed& where, int quarter, double zoom);

// Les vignettes : leur cadre, leur place dans la colonne.
struct Thumb {
    int index{0};
    Box frame;          // l'image, en local
    Box cell;           // la case (l'image et son numero)
};
[[nodiscard]] std::vector<Thumb> thumbsOf(const Layout&, const State&, const std::vector<doc::Size>& sizes, double* total = nullptr);

// Ce qu'un clic touche ("doc:next", "doc:thumb:3"...) ; "doc:area" : la zone des pages ; vide : rien.
[[nodiscard]] std::string hitPart(const Layout&, const State&, const std::vector<doc::Size>& sizes, double lx, double ly);

// Remet l'etat en ordre : le zoom de l'ajustement, le defilement dans ses bornes, la page courante.
void settle(State&, const Layout&, const std::vector<doc::Size>& sizes);
// La disposition change (la colonne des vignettes s'ouvre, la barre de recherche, l'objet change de taille) : ce qu'on
// lisait reste en haut de la zone (la meme page, au meme endroit de la page), au zoom de la nouvelle zone.
void relayout(State&, const Layout& from, const Layout& to, const std::vector<doc::Size>& sizes);
// Aller a la page `page` (1..) : son haut en haut de la zone. Faux : hors des bornes (1..pageCount).
bool goToPage(State&, const Layout&, const std::vector<doc::Size>& sizes, int page);
// Zoomer de `factor` autour d'un point local (le point sous le curseur reste sous le curseur).
void zoomAround(State&, const Layout&, const std::vector<doc::Size>& sizes, double newZoom, double lx, double ly);
// Le zoom au-dessus / en dessous dans les paliers (25, 33, 50, 67, 75, 100, 125, 150, 200, 300, 400, 600, 800 %).
[[nodiscard]] double nextZoom(double zoom, bool up);
// Le zoom montre : "100 %", "33 %".
[[nodiscard]] std::string zoomLabel(double zoom);
// Le champ de la page : "3 / 12".
[[nodiscard]] std::string pageLabel(int page, int count);

// UN GESTE. `navigation` : l'operateur peut changer de page ; `hits` : les occurrences de la recherche.
struct Effect {
    bool changed{false};         // l'etat a change (a redessiner)
    bool pageChanged{false};     // la page courante a change (la variable de la page s'ecrit)
    bool focus{false};           // la visionneuse prend le clavier
    bool startSearch{false};     // chercher State::query maintenant
    bool refused{false};         // un geste de navigation, navigation interdite
};
Effect gesture(State&, const Layout&, const std::vector<doc::Size>& sizes, std::string_view part, bool navigation,
               const std::vector<doc::Hit>& hits = {});
// La saisie au clavier dans un champ (le numero de page, la recherche) : un texte tape, une touche.
Effect typeText(State&, std::string_view text);
Effect typeKey(State&, const Layout&, const std::vector<doc::Size>& sizes, std::string_view key);
// Montrer l'occurrence `index` de la recherche (sa page, et elle au milieu de la zone).
bool showHit(State&, const Layout&, const std::vector<doc::Size>& sizes, const std::vector<doc::Hit>& hits, int index);

} // namespace hmi::docview

// =============================================================================
//  1.14.1 (lot N5) : L'EXPLORATEUR DE FICHIERS, SANS ECRAN (l'objet FileExplorer)
// -----------------------------------------------------------------------------
//  Le contenu d'un dossier ou d'un .zip - un zip se parcourt comme un dossier, un
//  zip dans un zip aussi (s'il est petit) -, une ligne par element : son genre,
//  son nom, sa taille, sa date ; le tri par colonne ; le filtre d'extensions et
//  la recherche ; l'infobulle d'une ligne ; l'extraction d'une entree de zip.
//
//  LECTURE SEULE : rien n'est efface, rien n'est ecrase. On ne remonte jamais
//  au-dessus de la source (un nom qui sort, un lien qui mene ailleurs : refuses).
//  UN ZIP HOSTILE NE CASSE RIEN : ses noms ../ ou absolus sont ramenes dans le
//  zip (et le disent), ses tailles annoncees ne sont crues qu'apres lecture et
//  bornees, ses entrees comptees au plus kMaxZipEntries.
//
//  LE REPERE de la geometrie : les coordonnees locales de l'objet (comme la
//  visionneuse). Les gestes, des "parties" : monter, ariane:N, recherche,
//  rafraichir, extraire, colonne:nom|taille|date, parent, ligne:N, ouvrir:N,
//  liste:haut, liste:bas.
// =============================================================================
namespace hmi::explorer {

inline constexpr std::size_t   kMaxZipEntries = 100000;          // au-dela, le zip n'est pas lu en entier (dit)
inline constexpr std::uint64_t kMaxNestedZip = 32ull << 20;      // un zip dans un zip : 32 Mio au plus, en memoire
inline constexpr std::uint64_t kMaxExtract = 256ull << 20;       // une entree extraite : 256 Mio au plus
inline constexpr std::size_t   kFolderCount = 5000;              // l'infobulle d'un dossier : le compte s'arrete la

enum class Genre : std::uint8_t { Folder, Zip, Image, Text, Pdf, Sound, Video, Csv, Project, Other };
[[nodiscard]] Genre genreOf(std::string_view name, bool folder);
[[nodiscard]] std::string genreLabel(Genre, std::string_view name = {});   // "Dossier", "Archive zip", "Image PNG"...
[[nodiscard]] std::string_view genreTag(Genre);                            // l'etiquette de l'icone : "ZIP", "PDF", "CSV"...

struct Entry {
    std::string   name;              // le nom montre
    std::string   path;              // le chemin complet (dans un zip : <le zip>\dossier\nom, comme l'Explorateur de Windows)
    Genre         genre{Genre::Other};
    bool          folder{false};     // un dossier (ou un dossier d'un zip)
    bool          inZip{false};      // une entree d'un zip
    std::uint64_t size{0};           // octets (un dossier : 0, non compte)
    std::uint64_t packed{0};         // dans un zip : la taille compressee
    std::int64_t  modified{0};       // millisecondes depuis 1970, a l'heure du poste ; 0 : inconnue
    std::int64_t  created{0};        // ... la creation, quand le systeme la donne
    bool          suspicious{false}; // un nom de zip ramene dans le zip (../, un chemin absolu)
};

// La source et le dossier courant : `root` un dossier ou un .zip (un chemin reel), `at` les noms sous lui.
struct Location {
    std::string              root;
    std::vector<std::string> at;
};

struct Options {
    std::vector<std::string> extensions;   // le filtre ("csv", "pdf") ; vide : tout. Les dossiers et les zips passent.
    std::string              search;       // Nom contient (sans casse ni accents)
    std::size_t              maxItems{2000};
};

struct Listing {
    std::vector<Entry> entries;        // celles qui passent le filtre et la recherche (non triees)
    std::size_t        total{0};       // ce que contient le dossier (avant le filtre)
    bool               truncated{false};   // maxItems atteint : la liste s'arrete la, et le dit
    bool               inZip{false};   // le dossier courant est dans un zip
    std::size_t        suspicious{0};  // des noms ramenes dans le zip
    std::string        error;          // la source ou le dossier ne se lit pas (vide : bon)
};
[[nodiscard]] Listing list(const Location&, const Options&);

// Nom contient : sans casse ni accents (vide : tout).
[[nodiscard]] bool nameContains(std::string_view name, std::string_view search);
// Un nom d'etape valable (pas vide, ni . ni .., sans / \ : ni caractere de controle).
[[nodiscard]] bool validStep(std::string_view name);
// "a/b\\c" -> {"a","b","c"} ; faux : une etape invalide (.., un chemin absolu).
bool splitSteps(std::string_view path, std::vector<std::string>& out);

enum class SortKey : std::uint8_t { Name, Size, Date, Genre };
[[nodiscard]] SortKey sortKeyFrom(std::string_view);   // "nom", "taille", "date", "genre"
[[nodiscard]] std::string_view sortKeyWord(SortKey);
void sort(std::vector<Entry>&, SortKey, bool descending);   // les dossiers toujours d'abord

// Le filtre d'extensions ("*.csv;*.pdf", "csv, pdf") : "" lisible ; sinon la raison.
[[nodiscard]] std::string filterProblem(std::string_view text);

// L'infobulle d'une ligne : le titre (le nom) et ses lignes - le chemin complet, le genre, la taille (dans un zip : la
// taille compressee et le taux), les dates, pour un dossier ses elements et sa taille totale (bornes), pour une image
// ses dimensions, pour un texte ses premieres lignes (ce qui se lit vite).
struct Tip {
    std::string title, body;
};
[[nodiscard]] Tip tipOf(const Location&, const Entry&);

// Extraire un fichier d'un zip vers `folder` : sous son nom seul (jamais ses dossiers : rien ne sort de `folder`),
// sans ecraser un fichier, borne a kMaxExtract. Vrai : `written` dit ou ; faux : `why`.
bool extract(const Location&, const Entry&, const std::string& folder, std::string& written, std::string& why);

// "12,4 Ko", "980 octets", "3,1 Mo" ; "30/10/2026 14:02" (vide : inconnue).
[[nodiscard]] std::string sizeText(std::uint64_t bytes);
[[nodiscard]] std::string dateText(std::int64_t ms);

// ---- la geometrie : le dessin et le clic la partagent ----
struct Layout {
    Box bar, up, search, refresh, extract;     // la barre : remonter, la recherche, Rafraichir, Extraire
    std::vector<Box> crumbs;                    // le fil d'Ariane, une boite par etape (0 : la source)
    std::size_t firstCrumb{0};                  // les premieres qui ne tiennent pas : "..." devant
    Box header, colName, colSize, colDate;      // les titres des colonnes (un clic trie)
    Box list;                                   // la zone des lignes
    Box parent;                                 // la ligne ".." (vide : a la source)
    std::vector<Box> rows;                      // les lignes montrees
    std::size_t first{0};                       // le rang de la premiere
    Box upArrow, downArrow;                     // defiler (vides : tout tient)
    Box status;                                 // la ligne du bas : le compte, le filtre, le message
    double rowHeight{24};
};
// La largeur d'un texte a la taille `sizePx`, en pixels de l'objet : la vraie police (le peintre et la souris passent la
// meme mesure, ce qui se dessine est ce qui se clique).
using TextWidth = std::function<double(std::string_view text, double sizePx)>;
// `crumbs` : les libelles du fil ; `parent` : une ligne ".." (pas a la source) ; `rows` : les entrees. `measure` : la largeur
// d'une etape du fil (1.14.1, lot N5, retouche) ; absente (Generer, sans police) : une estimation, 0,5 em par caractere.
[[nodiscard]] Layout layoutOf(double w, double h, double fontSize, const std::vector<std::string>& crumbs, bool searchBox, bool refresh,
                              bool extract, bool parent, bool showSize, bool showDate, std::size_t rows, std::size_t first,
                              const TextWidth& measure = {});
// "monter", "ariane:N", "recherche", "rafraichir", "extraire", "colonne:nom", "colonne:taille", "colonne:date", "parent",
// "ligne:N" (N : le rang dans la liste), "liste:haut", "liste:bas" ; "" : rien.
[[nodiscard]] std::string hit(const Layout&, double x, double y);

} // namespace hmi::explorer
