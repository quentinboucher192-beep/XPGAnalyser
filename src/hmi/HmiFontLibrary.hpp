// =============================================================================
//  hmi/HmiFontLibrary.hpp - 1.26.0 : la bibliotheque de polices
// -----------------------------------------------------------------------------
//  LE CLIENT (05/11) : << ajouter des TTF dans les ressources de base, un onglet
//  Polices dans la bibliotheque de l'editeur de vues, pouvoir changer de police
//  partout, avoir l'apercu des TTF, pouvoir editer des TTF en live, en creer, en
//  dupliquer a partir de >>.
//
//  LES POLICES FOURNIES : seize TrueType libres (SIL Open Font License, licence
//  DejaVu), livrees avec le logiciel dans resources/polices (et leurs licences) -
//  Liberation Sans, Serif et Mono (les chasses d'Arial, de Times New Roman, de
//  Courier New), DejaVu Sans, Serif et Sans Mono, Carlito (les chasses de
//  Calibri), Caladea (celles de Cambria), JetBrains Mono. Rangees en trois
//  dossiers : Sans empattement, Avec empattement, Chasse fixe. Pas celles de
//  l'interface du logiciel (resources/fonts) : elles ne changent pas son aspect.
//  Une entree par fichier (un style) ; la bibliotheque les montre par FAMILLE
//  (Liberation Sans : Normal, Gras, Italique). Lues a la demande : un fichier
//  absent, la police n'est pas listee.
//
//  MES POLICES : les TTF (et OTF) de l'utilisateur, hors des projets, dans ses
//  reglages (a cote de Mes images et de Mes sons) : elles le suivent d'un projet
//  a l'autre. Les polices creees ou modifiees dans l'editeur de polices y vont.
//  Une collection TTC est refusee (une police par fichier).
//
//  IMPORTER : une police devient une ressource du projet (son fichier, sous son
//  nom), citee par la propriete Police des objets, des styles, et par les polices
//  du theme (Config::defaultFont, Config::valueFont). Le meme contenu deja la est
//  reutilise.
//
//  LA POLICE PARTOUT : la valeur << Sans >> (ou rien) d'un objet est la police par
//  defaut du theme, << Mono >> la police des valeurs (resolve) ; tant qu'elles ne
//  sont pas choisies, les polices de l'IHM, comme avant.
// =============================================================================
#pragma once

#include "HmiMedia.hpp"
#include "HmiModel.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::fontlib {

inline constexpr std::string_view kMine = "Mes polices";

struct Font {
    std::string id;             // "liberation-sans" ; Mes polices : "mes/" + le nom du fichier
    std::string label;          // "Liberation Sans" ; Mes polices : la famille lue dans le fichier, sinon son nom
    std::string category;       // "Sans empattement", "Avec empattement", "Chasse fixe" ; Mes polices : kMine
    std::string style;          // "Normal", "Gras", "Italique"
    std::string words;          // les mots-cles de la recherche
    std::string description;    // a quoi elle ressemble (une phrase)
    std::string format;         // "TTF", "OTF"
    std::string file;           // son fichier (une fournie : dans resources/polices)
    BlobPtr     data;           // son contenu
    bool        editable{false};// une TrueType (des contours glyf) : l'editeur la lit, le PDF l'integre
    std::string license;        // "SIL Open Font License 1.1" ; Mes polices : vide
    std::string licenseFile;    // le texte de la licence (resources/polices/licences/...) ; vide : aucun
    int         glyphs{0};      // le nombre de ses dessins
    [[nodiscard]] bool mine() const noexcept { return id.rfind("mes/", 0) == 0; }
    // "Carlito", "Carlito Gras".
    [[nodiscard]] std::string fullName() const;
};

// ---- les fournies ----
void setNativeFolder(std::string folder);                            // resources/polices (absolu)
[[nodiscard]] const std::string& nativeFolder();
[[nodiscard]] const std::vector<Font>& builtIn();                     // celles dont le fichier est la, dans l'ordre
[[nodiscard]] const std::vector<std::string>& builtInCategories();   // Sans empattement, Avec empattement, Chasse fixe
[[nodiscard]] const Font* findBuiltIn(std::string_view id);

// ---- Mes polices ----
void setUserFolder(std::string folder);
[[nodiscard]] const std::string& userFolder();
[[nodiscard]] std::vector<Font> userFonts();                          // par nom
// Ajouter (le meme contenu deja la : `existed`, rien d'ecrit ; un nom pris par un autre contenu : "Nom (2).ttf").
bool addUserFont(const std::string& name, const Bytes& data, std::string* stored = nullptr, bool* existed = nullptr,
                 std::string* why = nullptr);
bool addUserFile(const std::string& path, std::string* stored = nullptr, bool* existed = nullptr, std::string* why = nullptr);
// Remplacer le contenu d'une de Mes polices (l'editeur de polices l'enregistre).
bool replaceUserFont(const Font&, const Bytes& data, std::string* why = nullptr);
bool removeUserFont(const Font&, std::string* why = nullptr);
[[nodiscard]] std::uint64_t userRevision() noexcept;

// ---- lire une police ----
// Une police lisible (TrueType ou OpenType) ; `family` : sa famille, `editable` : une TrueType (contours glyf).
// Une collection (TTC) : non (`why`).
[[nodiscard]] bool readable(const Bytes& data, std::string* family = nullptr, bool* editable = nullptr, std::string* why = nullptr);
// Le style lu dans une police ("Bold", "Italic", "Regular", "Book") en francais : "Gras", "Italique", "Normal".
[[nodiscard]] std::string frenchStyle(std::string_view subfamily);
// La famille et le style d'un contenu ("Carlito", "Carlito Gras") ; vide : pas une police.
[[nodiscard]] std::string displayName(const Bytes& data);

// ---- les familles : la bibliotheque montre une ligne par famille, ses styles dedans ----
struct Family {
    std::string       label;      // "Liberation Sans"
    std::string       category;   // son dossier
    std::vector<Font> fonts;      // ses styles : Normal, Gras, Italique, puis les autres
    [[nodiscard]] const Font& regular() const;                // Normal, sinon le premier
    [[nodiscard]] const Font* style(std::string_view style) const;
    [[nodiscard]] bool        mine() const noexcept { return category == kMine; }
    [[nodiscard]] std::string stylesText() const;             // "Normal, Gras, Italique"
};
// Les dossiers : Mes polices, puis ceux des fournies.
[[nodiscard]] std::vector<std::string> categories();
// Mes polices (par nom), puis les fournies (dans l'ordre de leurs dossiers).
[[nodiscard]] std::vector<Family> families();
[[nodiscard]] std::vector<Family> familiesOf(std::string_view category);
// Une fournie ou une de Mes polices, par son id. Le pointeur vaut jusqu'a la prochaine relecture de Mes polices : le
// copier tout de suite.
[[nodiscard]] const Font* findFont(std::string_view id);

// ---- chercher, importer, choisir ----
[[nodiscard]] bool matches(const Font&, std::string_view query);
[[nodiscard]] bool matches(const Family&, std::string_view query);   // un de ses styles
[[nodiscard]] std::string resourceName(const Font&);                  // "LiberationSans-Regular.ttf"
struct Imported {
    Id          id{kNoId};
    std::string name;
    bool        reused{false};
};
Imported importFont(Project&, const Font&);
[[nodiscard]] const Resource* importedAs(const Project&, const Font&);
// Une ressource police du projet (la police << Sans >> et << Mono >> n'en sont pas) ; nul : pas une police lisible.
[[nodiscard]] const Resource* projectFont(const Project&, std::string_view name);
// Les ressources polices du projet, dans leur ordre.
[[nodiscard]] std::vector<const Resource*> projectFonts(const Project&);

// LES CHOIX D'UNE CASE POLICE : Sans, Mono (celles de l'IHM), les polices du projet, puis celles de la bibliotheque
// pas encore importees (<< Bibliotheque > Sans empattement > Carlito >>, << Mes polices > Ma police >>). En choisir une de
// la bibliotheque l'importe (fromChoice, puis importFont).
[[nodiscard]] std::vector<std::string> choices(const Project&);
[[nodiscard]] std::string choiceLabel(const Font&);
// La police de la bibliotheque d'un choix ; nul : une valeur du projet. Le pointeur : comme findFont.
[[nodiscard]] const Font* fromChoice(std::string_view choice);

// LA CASE POLICE TELLE QU'ON LA VOIT (l'inspecteur, les styles, Configuration > Affichage) : chaque choix a son
// etiquette - << Police par defaut (Carlito) >>, << Police des valeurs (JetBrains Mono) >>, une police du projet par sa
// famille (<< Carlito >>, << Carlito Gras >> ; son nom de fichier si deux se confondent), puis la bibliotheque
// (choiceLabel). `theme` (les deux cases du theme) : << Celle de l'IHM >> (vide) au lieu de Sans et Mono.
struct Choice {
    std::string label;          // ce que la liste montre
    std::string value;          // ce qui s'ecrit : "Sans", "Mono", une ressource, "" (theme : celle de l'IHM)
    std::string libraryId;      // non vide : une police de la bibliotheque, importee en la choisissant (value vide)
    std::string group;          // "", "Du projet", "Biblioth\xC3\xA8que"
};
[[nodiscard]] std::vector<Choice> fieldChoices(const Project&, bool theme = false);
// L'etiquette d'une valeur ; une valeur inconnue (une ressource absente) : telle quelle.
[[nodiscard]] std::string fieldLabel(const Project&, std::string_view value, bool theme = false);
// Le choix d'une etiquette (ou d'une valeur tapee telle quelle, d'un choiceLabel) ; rien : inconnue.
[[nodiscard]] std::optional<Choice> fromLabel(const Project&, std::string_view label, bool theme = false);
// Le nom lisible d'une police du theme : la famille de sa ressource ; vide : celle de l'IHM.
[[nodiscard]] std::string themeFontName(const Project&, bool values);

// LA POLICE PARTOUT : la ressource qui dessine la valeur "font" d'un objet - "Sans" ou "" : la police par defaut du
// theme, "Mono" : la police des valeurs, un nom : cette ressource ; nul : les polices de l'IHM (comme avant).
[[nodiscard]] const Resource* resolve(const Assets&, const Config*, std::string_view value);
[[nodiscard]] const Resource* resolve(const Project&, std::string_view value);

// OU UNE POLICE DU PROJET SERT : les objets qui l'affichent (cites, ou sur Sans / Mono quand elle est celle du theme),
// les styles qui l'imposent, le theme.
struct Usage {
    std::size_t objects{0};
    std::size_t styles{0};
    bool        defaultFont{false};
    bool        valueFont{false};
    [[nodiscard]] bool any() const noexcept { return objects || styles || defaultFont || valueFont; }
};
[[nodiscard]] Usage usageOf(const Project&, std::string_view resourceName);
// "3 objets, 1 style, police par defaut" ; rien : "aucun objet".
[[nodiscard]] std::string usageText(const Usage&);

// La phrase d'apercu ("Pompe P1 en marche - 12,4 bar - aeecu" et l'euro, les accents ecrits).
[[nodiscard]] std::string_view sampleText();

} // namespace hmi::fontlib
