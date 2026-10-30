// =============================================================================
//  hmi/HmiAssets.hpp — ressources et fichiers externes : importer, citer, suivre
// -----------------------------------------------------------------------------
//  RESSOURCES. Importer lit le fichier une fois, en tire ce qu'il contient
//  (hmi/HmiMedia) et le copie dans le projet. Qui la cite : une Image par sa
//  propriete "image", une Video par "video" (et son image d'attente "poster"),
//  un texte par sa police "font", et toute expression qui la nomme entre
//  guillemets - SEL(Ouvert, 'vanne_fermee.png', 'vanne_ouverte.png'). Ce qui
//  n'est cite nulle part est "inutilise" : Generer le signale, et le
//  gestionnaire sait le montrer et le retirer.
//
//  FICHIERS EXTERNES. On garde le chemin, et ce qu'on a vu en liant : taille et
//  date. L'etat se recalcule a chaque affichage : present, modifie depuis le
//  lien, absent, ou non verifiable (une base externe, qui n'est qu'une chaine
//  de connexion tant qu'aucun pilote n'est installe).
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../core/Result.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// ---- ressources ---------------------------------------------------------------
// Lire un fichier et en faire une ressource (identifiant attribue par le
// projet, nom libre). N'ajoute rien : l'appelant l'ajoute dans une commande.
[[nodiscard]] core::Result<Resource> readResource(Project&, const std::string& path, std::string wantedName = {});
// La meme, depuis des octets deja en memoire (tests, import d'archive).
[[nodiscard]] Resource makeResource(Project&, std::string name, BlobPtr data, std::string origin);
// Remettre a jour ce qu'on sait d'une ressource d'apres son contenu.
void describeResource(Resource&);

struct Citation {
    Id          view{kNoId};
    Id          object{kNoId};
    std::string where;        // "Vue_Armoire_A/Logo_Site.image"
    bool        expression{false};
};
// Qui cite `name` : proprietes et expressions de tous les objets.
[[nodiscard]] std::vector<Citation> citations(const Project&, std::string_view name);
// Les proprietes qui designent une ressource, par genre d'objet.
[[nodiscard]] bool citesResource(std::string_view propertyKey) noexcept;
[[nodiscard]] std::vector<const Resource*> unusedResources(const Project&);
// Renommer et mettre a jour ce qui la cite (proprietes et expressions).
// Rend le nombre de proprietes modifiees ; faux si le nom est pris ou vide.
bool renameResource(Project&, Id resource, const std::string& newName, std::size_t* updated = nullptr,
                    std::string* why = nullptr);

// ---- fichiers externes ------------------------------------------------------------
enum class ExternalStatus : std::uint8_t { Present, Modified, Missing, Unverifiable };
[[nodiscard]] std::string_view externalStatusLabel(ExternalStatus) noexcept;   // "present", "modifie"...

struct ExternalState {
    ExternalStatus status{ExternalStatus::Missing};
    std::uint64_t  bytes{0};
    std::string    modified;       // la date du fichier sur le disque, maintenant
    std::string    resolvedPath;
};
// Le chemin reel : absolu tel quel, relatif au dossier du projet sinon.
[[nodiscard]] std::string   resolveExternalPath(const ExternalFile&, const std::string& projectFolder);
[[nodiscard]] ExternalState externalState(const ExternalFile&, const std::string& projectFolder);
// Un nouveau lien : taille et date relevees maintenant.
[[nodiscard]] ExternalFile linkExternal(Project&, std::string name, ExternalKind, std::string path, std::string part,
                                        const std::string& projectFolder);
// "Relier" : prendre acte d'un fichier modifie (nouvelle taille, nouvelle date).
void relinkExternal(ExternalFile&, const std::string& projectFolder);
// Qui montre ce fichier : les Tableaux dont la source est `name`.
[[nodiscard]] std::vector<Citation> externalCitations(const Project&, std::string_view name);

// La date d'un fichier, "2026-09-22 01:10:05" ; vide si absent.
[[nodiscard]] std::string fileStamp(const std::string& path);

// ---- 1.14.1 (lot N6) : LA BIBLIOTHEQUE D'IMAGES ---------------------------------------------
//  Des images pretes a poser, rangees par categorie (fleches, interface, securite, industrie,
//  energie, batiment, reseau, vehicules, meteo, personnes, drapeaux, formes) : des SVG dessines
//  pour l'application (tools/images/images.txt -> HmiImageLibraryData.cpp, embarques : rien a
//  livrer de plus), nets a toute taille. Et MES IMAGES : celles que l'utilisateur y ajoute (une
//  ressource du projet, un fichier du disque), gardees HORS du projet, dans le dossier de ses
//  reglages (a cote de ses themes) : elles le suivent d'un projet a l'autre.
//  Poser ou importer une image en fait une RESSOURCE du projet, comme un fichier importe : le
//  meme contenu deja la est reutilise ; un nom pris par un autre contenu devient nom_2 (la regle
//  des ressources). Un dessin MONOCHROME (trace a l'encre kInk) prend la couleur choisie a
//  l'import ; une image en couleurs garde les siennes.
namespace imagelib {

inline constexpr std::uint32_t    kInk = 0xE6EAF0;           // l'encre des dessins, telle qu'embarquee
inline constexpr std::string_view kInkHex = "#E6EAF0";
inline constexpr std::uint32_t    kDarkInk = 0x2B3240;       // l'encre "Auto" sur un fond clair
inline constexpr std::string_view kMine = "Mes images";      // la categorie des images de l'utilisateur

// Une image embarquee, telle que le generateur l'ecrit (HmiImageLibraryData.cpp).
struct BuiltInImage {
    const char* id;
    const char* label;
    const char* category;
    const char* words;
    bool        ink;            // des traits a l'encre
    bool        mono;           // rien que l'encre
    int         width, height;  // la taille ou elle se pose (pixels)
    const char* svg;
};
[[nodiscard]] const std::vector<BuiltInImage>& builtInData();

struct Image {
    std::string id;             // "fleche-haut" ; Mes images : "mes/" + le nom du fichier
    std::string label;          // "Fleche haut" ; Mes images : le nom du fichier sans extension
    std::string category;
    std::string words;
    bool        ink{false};     // des traits a l'encre : ils prennent la couleur choisie
    bool        mono{false};    // seulement de l'encre (monochrome)
    int         width{0}, height{0};   // sa taille d'origine (un SVG : celle ou il se pose)
    std::string format;         // "SVG", "PNG"...
    std::string file;           // Mes images : son fichier ; vide : embarquee
    BlobPtr     data;           // son contenu (l'encre : kInk)
    [[nodiscard]] bool mine() const noexcept { return !file.empty(); }
};
[[nodiscard]] const std::vector<Image>& builtIn();                    // dans l'ordre de leurs categories
[[nodiscard]] const std::vector<std::string>& builtInCategories();
[[nodiscard]] const Image* findBuiltIn(std::string_view id);

// ---- Mes images : un dossier (les reglages de l'utilisateur), relu a la demande ----
void setUserFolder(std::string folder);
[[nodiscard]] const std::string& userFolder();
// Les images du dossier (PNG, JPEG, BMP, GIF, SVG, ICO), par nom ; vide : pas de dossier.
[[nodiscard]] std::vector<Image> userImages();
// Ajouter un contenu (une ressource du projet, un fichier lu) sous le nom `name` (avec son extension) : un
// autre contenu deja sous ce nom : "nom (2).png" ; le meme contenu deja la : rien n'est ecrit (`existed`).
// `stored` : le nom du fichier garde. Faux et `why` : pas une image, pas de dossier, ecriture impossible.
bool addUserImage(const std::string& name, const Bytes& data, std::string* stored = nullptr, bool* existed = nullptr,
                  std::string* why = nullptr);
bool addUserFile(const std::string& path, std::string* stored = nullptr, bool* existed = nullptr, std::string* why = nullptr);
bool removeUserImage(const Image&, std::string* why = nullptr);

// ---- chercher, colorer, importer ----
// Le nom, la categorie et les mots-cles : chaque mot cherche commence un de leurs mots, sans casse ni accents.
[[nodiscard]] bool matches(const Image&, std::string_view query);
struct Swatch {
    std::string_view key;       // "rouge" (le nom de la ressource : fleche-haut-rouge.svg)
    std::string_view label;     // "Rouge"
    std::uint32_t    rgb;
};
// Les couleurs proposees pour l'encre (apres Auto) : clair (l'encre telle quelle), sombre, noir, gris, bleu...
[[nodiscard]] const std::vector<Swatch>& swatches();
// Auto : claire sur un fond sombre (kInk), sombre sur un fond clair (kDarkInk). `background` : "#RRGGBB".
[[nodiscard]] std::uint32_t autoInk(std::string_view background);
// Le contenu, l'encre a cette couleur ; une image sans encre : tel quel (le meme pointeur, d'un appel a l'autre).
[[nodiscard]] BlobPtr contentFor(const Image&, std::uint32_t rgb);
// Le nom propose dans les ressources : "fleche-haut.svg", "fleche-haut-rouge.svg" ; Mes images : son fichier.
[[nodiscard]] std::string resourceName(const Image&, std::uint32_t rgb);
struct Imported {
    Id          id{kNoId};
    std::string name;
    bool        reused{false};  // le meme contenu etait deja une ressource du projet
};
// Dans le projet (l'appelant fait la commande) : le meme contenu deja la est reutilise, sinon une ressource neuve.
Imported importImage(Project&, const Image&, std::uint32_t rgb);
// "64 x 64, vectorielle (nette a toute taille)" ; "120 x 80 px".
[[nodiscard]] std::string sizeText(const Image&);

} // namespace imagelib

} // namespace hmi
