// =============================================================================
//  project/ProjectIcon.hpp - l'icone du projet (lot API 6)
// -----------------------------------------------------------------------------
//  32 x 32 pixels, chacun un indice dans une palette de seize couleurs propre a
//  l'icone (0 : transparent). La palette de depart est choisie pour se lire sur
//  les neuf themes ; chaque couleur se change pour un projet.
//
//  SUR LE DISQUE, config/icone.txt, EN TEXTE : la palette ("couleur 7 =
//  #E4574F"), puis 32 lignes de 32 lettres (".123456789ABCDEFG"). Elle se lit,
//  se compare d'une version a l'autre et se corrige dans un editeur de texte,
//  comme le reste du dossier. Pas d'icone : pas de fichier.
//
//  LES DESSINS DE LA GALERIE sont traces ici, en code, avec les memes outils
//  que l'editeur (disque, polygone, contour) : quinze, de l'armoire electrique
//  aux initiales du projet.
//
//  Ce fichier ne depend que du modele : projecticon_test le verifie sans ecran.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "../domain/ProjectModel.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace project::icon {

inline constexpr int kSize = 32;

// Les seize couleurs de depart, et leur nom ("rouge") ; 0 : "transparent".
[[nodiscard]] const std::array<std::uint32_t, 16>& defaultPalette();
[[nodiscard]] std::string_view colorName(int index);
// La lettre d'une couleur dans config/icone.txt : '.' pour 0, '1'..'9', 'A'..'G'.
[[nodiscard]] char letterOf(int index);

// Une icone transparente, avec la palette de depart.
[[nodiscard]] domain::ProjectIcon blank();

// ---- la galerie --------------------------------------------------------------
struct Preset {
    const char* key;      // "bouteille"
    const char* label;    // "Bouteille de gaz"
};
[[nodiscard]] const std::vector<Preset>& presets();
// `projectName` donne les initiales. Une cle inconnue : une icone vide.
[[nodiscard]] domain::ProjectIcon preset(std::string_view key, std::string_view projectName = {});
// "Armoire_Gaz" -> "AG", "station pompage" -> "SP", "PR3" -> "PR".
[[nodiscard]] std::string initialsOf(std::string_view projectName);

// ---- dessiner : les outils de l'editeur ----------------------------------------
// `mirror` : la symetrie gauche-droite - le meme trait de l'autre cote.
[[nodiscard]] std::uint8_t at(const domain::ProjectIcon&, int x, int y);   // 0 hors de l'icone
void put(domain::ProjectIcon&, int x, int y, std::uint8_t color, bool mirror = false);
void line(domain::ProjectIcon&, int x0, int y0, int x1, int y1, std::uint8_t color, bool mirror = false);
void frame(domain::ProjectIcon&, int x0, int y0, int x1, int y1, std::uint8_t color, bool mirror = false);
// Le pot de peinture : la zone de meme couleur, par les cotes (4-connexe).
void fill(domain::ProjectIcon&, int x, int y, std::uint8_t color, bool mirror = false);
// Pas un pixel pose.
[[nodiscard]] bool isBlank(const domain::ProjectIcon&);

// ---- les pixels en couleurs ---------------------------------------------------
[[nodiscard]] std::uint32_t colorOf(const domain::ProjectIcon&, int index);   // 0xRRGGBB
// RGBA, 32 x 32 (4 octets par pixel), pour une texture.
[[nodiscard]] std::vector<std::uint8_t> rgba(const domain::ProjectIcon&);
// La reduction en 16 x 16 : la moyenne de chaque carre de 2 x 2, le transparent
// compte pour l'opacite.
[[nodiscard]] std::vector<std::uint8_t> rgba16(const domain::ProjectIcon&);

// ---- config/icone.txt ----------------------------------------------------------
[[nodiscard]] std::string toText(const domain::ProjectIcon&);
[[nodiscard]] core::Result<domain::ProjectIcon> fromText(std::string_view text);
[[nodiscard]] std::string hex(std::uint32_t rgb);                  // "#E4574F"
[[nodiscard]] bool parseHex(std::string_view text, std::uint32_t& rgb);   // "#e4574f", "E4574F"

} // namespace project::icon
