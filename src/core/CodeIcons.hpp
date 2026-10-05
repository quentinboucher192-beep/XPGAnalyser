// =============================================================================
//  core/CodeIcons.hpp - 1.8.0 : les icones au choix de ce qui porte du code
// -----------------------------------------------------------------------------
//  L'utilisateur donne une icone a une section, une unite de programme, un bloc
//  DFB (et ses sections), un type DDT, un script ou une fonction de l'IHM :
//  clic droit > Definir l'icone... ; un mini-menu montre les dix-huit, chacune
//  avec son nom et sa description. L'application SUGGERE (d'apres le nom), elle
//  ne choisit jamais : sans choix, l'element garde son icone de toujours. Les
//  macros ne sont pas concernees.
//
//  ICI : LE CATALOGUE, SANS RIEN D'AUTRE. Les dessins sont des traits sur une
//  grille de 16 x 16 (l'epaisseur d'un trait : 1,5), en donnees : l'ecran les
//  trace (ui/Icons.cpp), le PDF de l'export aussi (export/ProgramBookPdf.cpp),
//  et les deux montrent la meme chose. Le choix de chaque element est range
//  dans le projet (domain::Project::codeIcons, config/icones-code.txt).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace core::codeicons {

struct Point {
    float x{0.f}, y{0.f};
};

// Un trait : une suite de points (ferme : le dernier rejoint le premier) ; plein :
// une surface (un point, une pastille), dessinee en eventail depuis son centre -
// les surfaces du catalogue sont toutes convexes.
struct Path {
    std::vector<Point> points;
    bool               closed{false};
    bool               filled{false};
};

struct Info {
    std::string_view key;           // "grafcet" : ce qui est range dans le projet
    std::string_view name;          // "Grafcet / sequence" (UTF-8)
    std::string_view description;   // une ligne, pour le mini-menu
    std::uint32_t    rgb{0};        // 0xRRGGBB : sa couleur, sur fond sombre comme sur papier
};

inline constexpr std::size_t kCount = 18;

// i < kCount ; au-dela : le premier (jamais d'acces hors du tableau).
[[nodiscard]] const Info& info(std::size_t i) noexcept;
[[nodiscard]] const std::vector<Path>& glyph(std::size_t i);
// -1 : cle inconnue (ou vide).
[[nodiscard]] int indexOf(std::string_view key) noexcept;

// Ce que porte l'element : la suggestion en depend (un DDT est une structure de
// donnees ; une section, d'apres son nom).
enum class Kind : std::uint8_t { Section, Unit, Dfb, DfbSection, Ddt, HmiScript, HmiFunction };

// La cle suggeree d'apres le nom ("SFC_ManuB" -> "grafcet") ; vide : aucune.
[[nodiscard]] std::string_view suggest(std::string_view name, Kind kind);

// ---- les cles rangees dans le projet ---------------------------------------------
//  "section:MAST/Init"          une section de tache (le proprietaire : la tache)
//  "section:Logigrammes_A/Init" une section d'unite de programme
//  "unit:Logigrammes_A"         "dfb:CAPTEUR"      "dfbsection:CAPTEUR/Code"
//  "ddt:armoire"                "hmiscript:Au demarrage"   "hmifunction:Convertir"
// Les noms gardent leur casse ; la recherche l'ignore (le ST aussi).
[[nodiscard]] std::string keyOf(Kind kind, std::string_view owner, std::string_view name);
[[nodiscard]] std::string_view kindWord(Kind kind) noexcept;

} // namespace core::codeicons
