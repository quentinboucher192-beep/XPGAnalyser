// =============================================================================
//  ui/FileKinds.hpp - lot API 8 : le genre d'un fichier, d'apres son extension
// -----------------------------------------------------------------------------
//  CE QUE L'EXPLORATEUR DE L'APPLI (ui/widgets/FileExplorer) DIT D'UN FICHIER
//  sans l'ouvrir : sa famille, le libelle de son type ("Export Control Expert",
//  "Classeur Excel avec macros", "Image PNG"), la pastille (l'extension en
//  capitales), l'icone et la TEINTE - un role, que la peinture traduit dans le
//  theme du moment (tintColor) : aucune couleur n'est ecrite ici.
//
//    .XPG .XHW .STU .STA .XEF   bleu     (Control Expert)
//    .xls .xlsx .xlsm .csv      vert     (classeurs)
//    images                     violet
//    .pdf                       rouge
//    texte, log, json, xml...   gris
//    .zip .7z .rar              brun
//    fichiers de l'appli        orange   (.xpgproj .xpgtheme .xpgvues .xpglayout)
//    dossiers                   jaune
//
//  Tout se decide sur le NOM : un dossier de 5 000 fichiers se range sans en
//  lire un seul. Seule imageSize() ouvre le fichier (quelques octets d'en-tete).
// =============================================================================
#pragma once

#include "Icons.hpp"
#include "Theme.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ui::files {

    enum class Family : std::uint8_t {
        Folder,          // un dossier
        ControlExpert,   // .XPG, .XHW, .STU, .STA, .XEF, .ZEF
        Workbook,        // .xls, .xlsx, .xlsm, .csv
        Image,           // .png, .jpg, .bmp, .gif, .svg, .ico, .webp
        Pdf,             // .pdf
        Text,            // .txt, .log, .json, .xml, .md, .ini, .st...
        Archive,         // .zip, .7z, .rar
        AppFile,         // .xpgproj, .xpgtheme, .xpgvues, .xpglayout
        Drive,           // un lecteur (C:, une cle USB, un partage)
        Other,
    };

    // Le role de couleur d'une famille ; tintColor() le traduit dans le theme.
    enum class Tint : std::uint8_t { Yellow, Blue, Green, Violet, Red, Gray, Brown, Orange, Neutral };

    // L'extension, en minuscules, sans le point : "MAST.XPG" -> "xpg" ;
    // "a.tar.gz" -> "gz" ; ".gitignore", "LISEZMOI" -> "".
    [[nodiscard]] std::string extensionOf(std::string_view fileName);
    [[nodiscard]] Family      familyOfExtension(std::string_view ext);
    [[nodiscard]] Family      familyOf(std::string_view fileName, bool folder);
    [[nodiscard]] Tint        tintOf(Family f) noexcept;
    [[nodiscard]] Icon        iconOf(Family f) noexcept;
    // Le libelle du type, en francais : "Export Control Expert",
    // "Configuration mat\xC3\xA9rielle", "Classeur Excel avec macros", "Image PNG",
    // "Document PDF", "Dossier" ; inconnu : "Fichier XYZ" (ou "Fichier").
    [[nodiscard]] std::string typeLabel(std::string_view fileName, bool folder);
    // La pastille de l'icone : l'extension en capitales, 4 lettres au plus ;
    // "" pour un dossier ou un fichier sans extension.
    [[nodiscard]] std::string badgeText(std::string_view fileName, bool folder);
    // Le genre au pluriel, pour compter ("3 classeurs", "1 dossier", "8 CSV").
    [[nodiscard]] std::string countLabel(Family f, std::string_view ext, std::size_t n);
    // La couleur d'une teinte dans le theme `t` : les couleurs du theme
    // seulement (les portees, les etats, la coloration du code), lisibles sur
    // son fond (Theme::onSurface).
    [[nodiscard]] gfx::Color tintColor(const Theme& t, Tint tint) noexcept;

    // Les dimensions d'une image (PNG, GIF, BMP, JPEG) lues dans son en-tete ;
    // nullopt : pas une image, illisible, ou un format sans en-tete simple.
    struct ImageSize { std::uint32_t w{0}, h{0}; };
    [[nodiscard]] std::optional<ImageSize> imageSize(const std::string& utf8Path);
    // "Image PNG 1600 \xC3\x97 900" (le libelle du type, et les dimensions si on les a).
    [[nodiscard]] std::string imageTypeLabel(std::string_view fileName, std::optional<ImageSize> size);

} // namespace ui::files
