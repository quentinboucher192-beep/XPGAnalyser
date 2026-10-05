// =============================================================================
//  hmi/HmiDisplay.hpp - ce que l'operateur voit (lot 13) : la vue preparee
// -----------------------------------------------------------------------------
//  LA VUE PREPAREE. Avant d'etre liee et dessinee, la vue qui tourne passe par
//  displayView() : une copie ou
//    - les textes sont dans la langue de l'operateur (HmiLanguages.hpp) ;
//    - l'unite et le format de chaque variable (Configuration > Unites et
//      formats) remplacent ceux des objets qui la montrent (sauf un objet qui
//      decoche "Format de la variable"), et remplissent les trous sans format
//      des textes ({Pression} : 3.2 ; {Pression:u} : 3.2 bar) ;
//    - les tailles des textes sont multipliees (Taille du texte) ;
//    - les couleurs passent a la palette "daltonien" (le vert au bleu, le rouge
//      au vermillon, l'orange a l'ambre) et/ou au theme "jour" (les gris
//      sombres deviennent clairs, les textes clairs sombres ; un texte sur une
//      couleur franche la garde) ;
//    - les voyants portent un symbole (coche, croix, point d'exclamation, tiret).
//  Le projet n'est jamais touche : le moteur garde ses objets (les valeurs
//  ecrites ne dependent pas de ce qu'on voit).
//
//  LES REGLAGES : au lancement, ceux du projet (Configuration) ; en marche, le
//  menu Parametres systeme, SYS.TextScale, SYS.ColorMode, SYS.StatusSymbols,
//  SYS.Theme, l'action "Changer de theme", IHM_THEME('jour'), l'objet
//  Selecteur de theme.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi {

struct DisplayOptions {
    std::string language;                 // "" : celle du projet
    int         textScale{100};           // en %
    std::string colorMode{"normal"};      // "normal", "daltonien"
    bool        symbols{false};
    std::string theme{"nuit"};            // "nuit", "jour"
    bool operator==(const DisplayOptions&) const = default;
};
// Les reglages du projet (Configuration), sans langue choisie : la premiere.
[[nodiscard]] DisplayOptions projectDisplay(const Project&);
// Vrai : la vue preparee ne differe pas de la vue (une langue, 100 %, couleurs
// normales, sans symboles, nuit, aucune unite de variable).
[[nodiscard]] bool plainDisplay(const Project&, const DisplayOptions&);

// ---- les unites et les formats --------------------------------------------------------
// L'unite et le format de cette variable ("Armoires[2].ana.PT1.mes" trouve
// "Armoires[].ana.PT1.mes") ; nul : aucun. Sans casse. Montree dans la vue `in`,
// un parametre de la vue se lit comme ce qu'il designe par defaut (dans une
// popup qui declare Armoire := Armoires[0], "Armoire.ana.PT1.mes" trouve aussi
// "Armoires[].ana.PT1.mes").
[[nodiscard]] const VariableDisplay* displayOf(const Project&, std::string_view path, const View* in = nullptr);
// Le chemin d'une ligne pour cette variable : le parametre de la vue remplace
// par ce qu'il designe, les index vides ("Armoires[0].x" -> "Armoires[].x").
[[nodiscard]] std::string displayRowPath(std::string_view path, const View* in = nullptr);
// La variable que montre un objet : sa "value" pilotee par un chemin tout seul,
// sinon la variable qu'il lit (autoValueSource) ; "" : aucune (ou une expression).
[[nodiscard]] std::string displayVariableOf(const Object&);
// Vrai : l'objet suit le format de sa variable (sa case "Format de la variable",
// cochee par defaut).
[[nodiscard]] bool followsVariableFormat(const Object&);
// Le format (l'unite) qu'un objet montre : celui de sa variable s'il le suit et
// qu'il y en a un ; sinon le sien (`fallback` s'il n'en a pas).
[[nodiscard]] std::string effectiveFormat(const Project&, const Object&, std::string_view fallback = {});
[[nodiscard]] std::string effectiveUnit(const Project&, const Object&);
// Tableau de variables : le format de la ligne `row` ("formats" au meme rang,
// sinon celui de sa variable, sinon celui de l'objet).
[[nodiscard]] std::string rowFormat(const Project*, const Object&, std::size_t row);
// Un texte a trous : un trou sans format prend celui de sa variable ; {X:u},
// son format et son unite. Le reste tel quel.
[[nodiscard]] std::string formatTemplate(std::string_view text, const Project&, const View* in = nullptr);
// Un exemple : 1234.5678 au format "0.0" et l'unite (pour le volet).
[[nodiscard]] std::string displaySample(const VariableDisplay&);
// Ou se montre cette variable : "Vue.Objet" (un objet qui la montre, une ligne
// d'un tableau de variables, un trou d'un texte), une fois chacun.
[[nodiscard]] std::vector<std::string> displayUses(const Project&, const VariableDisplay&);
// Un chemin de variable ecrit comme il faut : lettres, chiffres, _, points,
// crochets (Armoires[].ana.PT1.mes).
[[nodiscard]] bool validDisplayPath(std::string_view) noexcept;

// ---- les couleurs -----------------------------------------------------------------------
// Une couleur (#RRGGBB ou #RRGGBBAA) telle qu'on la voit ; `onColored` : un texte
// pose sur une couleur franche (au theme jour, il la garde) ; `isText` : la
// couleur d'un texte (au theme jour, plus foncee qu'un fond). Autre chose : tel quel.
[[nodiscard]] std::string displayColor(std::string_view color, const DisplayOptions&, bool onColored = false, bool isText = false);
// Vrai : ces reglages changent les couleurs (daltonien, jour).
[[nodiscard]] bool changesColors(const DisplayOptions&) noexcept;
// La meme chose pour une couleur 0xRRGGBB : les couleurs fixes du dessin des objets.
[[nodiscard]] std::uint32_t displayRgb(std::uint32_t rgb, const DisplayOptions&, bool onColored = false, bool isText = false);
// Chaque couleur d'un texte (une liste "a;b", des zones, des etats, les
// chaines d'une expression) passee par displayColor.
[[nodiscard]] std::string displayColors(std::string_view text, const DisplayOptions&);
// Le symbole d'un etat, d'apres sa couleur : 1 coche (vert), 2 croix (rouge),
// 3 point d'exclamation (orange, jaune), 4 tiret (gris, eteint).
[[nodiscard]] int statusSymbolOf(std::string_view color) noexcept;

// ---- le selecteur de theme : deux boutons, Jour et Nuit (la geometrie du selecteur de langue)
// "theme:jour", "theme:nuit" ; "" : rien.
[[nodiscard]] std::string themeSelectorHit(const Object&, double w, double h, double x, double y);
// Les libelles des deux boutons ("themeLabels" : "Jour;Nuit").
[[nodiscard]] std::pair<std::string, std::string> themeLabels(const Object&);

// ---- la vue preparee --------------------------------------------------------------------
[[nodiscard]] View displayView(const View&, const Project&, const DisplayOptions&);

} // namespace hmi
