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
//
//  MES POLICES : les TTF (et OTF) de l'utilisateur, hors des projets, dans ses
//  reglages (a cote de Mes images et de Mes sons) : elles le suivent d'un projet
//  a l'autre. Les polices creees ou modifiees dans l'editeur de polices y vont.
//
//  IMPORTER : une police devient une ressource du projet (son fichier), citee par
//  la propriete Police des objets, des styles, et la police par defaut du projet
//  (hmi::Assets::defaultFont). Le meme contenu deja la est reutilise.
// =============================================================================
#pragma once

#include "HmiMedia.hpp"
#include "HmiModel.hpp"

#include <cstdint>
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
    [[nodiscard]] bool mine() const noexcept { return id.rfind("mes/", 0) == 0; }
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
[[nodiscard]] bool readable(const Bytes& data, std::string* family = nullptr, bool* editable = nullptr, std::string* why = nullptr);

// ---- chercher, importer, choisir ----
[[nodiscard]] bool matches(const Font&, std::string_view query);
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

// LES CHOIX D'UNE CASE POLICE : Sans, Mono (celles de l'IHM), les polices du projet, puis celles de la bibliotheque
// pas encore importees (<< Bibliotheque > Sans empattement > Carlito >>, << Mes polices > Ma police >>). En choisir une de
// la bibliotheque l'importe (fromChoice, puis importFont).
[[nodiscard]] std::vector<std::string> choices(const Project&);
[[nodiscard]] std::string choiceLabel(const Font&);
[[nodiscard]] const Font* fromChoice(std::string_view choice);
// Le phrase d'apercu ("Portez ce vieux whisky au juge blond qui fume - 0123456789").
[[nodiscard]] std::string_view sampleText();

} // namespace hmi::fontlib
