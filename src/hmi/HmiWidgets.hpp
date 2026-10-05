// =============================================================================
//  hmi/HmiWidgets.hpp - le contenu des objets du lot 6
// -----------------------------------------------------------------------------
//  CE QUE GARDENT, EN TEXTE, LES OBJETS A CONTENU. Une propriete de plus par
//  objet, lisible dans ihm/vues/*.vue et modifiable a la main :
//
//    Image animee    states  une ligne par etat :
//                              Alarme => alarme_1.png, alarme_2.png @ 250
//                              Marche => pompe_on.png
//                            (la condition, les images qui defilent, la periode
//                            en ms ; sans periode : celle de l'objet). Le premier
//                            etat vrai l'emporte ; aucun : l'image par defaut.
//    Tableau         cells   une ligne par ligne du tableau, les cases separees
//                            par des tabulations. Une case : du texte, un texte a
//                            trous ({Niveau:0.0} bar) ou =expression.
//                    widths  la largeur relative des colonnes (a;b;c), facultative.
//    Courbe          names   le nom de chaque plume (a;b), pour la legende.
//
//  LE GESTIONNAIRE DE RECETTES se dessine et se clique avec la meme geometrie
//  (recipeManagerLayout) : la barre de boutons en haut, puis le tableau des jeux.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// ---------------------------------------------------------------- image animee
struct ImageState {
    std::string              condition;
    std::vector<std::string> images;
    int                      periodMs{0};     // 0 : la periode de l'objet
    bool operator==(const ImageState&) const = default;
};
[[nodiscard]] std::vector<ImageState> parseImageStates(std::string_view text);
[[nodiscard]] std::string             formatImageStates(const std::vector<ImageState>&);
// L'image a montrer : l'etat retenu (-1 : aucun, l'image par defaut) et, a
// l'instant `seconds`, l'image de son defilement.
[[nodiscard]] std::string imageFrame(const ImageState& s, int objectPeriodMs, double seconds);

// ------------------------------------------------------------------- tableau
[[nodiscard]] std::vector<std::vector<std::string>> parseCells(std::string_view text);
[[nodiscard]] std::string                           formatCells(const std::vector<std::vector<std::string>>&);
// Une case pilotee : "=expression" ou un texte a trous.
[[nodiscard]] bool cellIsExpression(std::string_view cell) noexcept;
[[nodiscard]] bool cellIsTemplate(std::string_view cell) noexcept;
// Les largeurs relatives des `n` colonnes (widths, ou egales), en fraction.
[[nodiscard]] std::vector<double> columnFractions(const Object&, std::size_t n);

// ------------------------------------------------------------ liste a ; b ----
[[nodiscard]] std::vector<std::string> splitSemicolons(std::string_view s);   // "a; b" -> {a, b} (vides gardes)
[[nodiscard]] std::string              joinSemicolons(const std::vector<std::string>&);

// ------------------------------------------------------ gestionnaire de recettes
struct RecipeManagerLayout {
    struct Button { std::string label; Box box; };
    std::vector<Button> buttons;          // dans le repere de l'objet
    Box                 title;            // le nom de la recette, a droite de la barre
    Box                 table;            // en-tete + lignes
    double              headerH{24}, rowH{22};
    std::size_t         visibleRows{0};
};
// Les boutons de l'objet (Ajouter;Modifier;Supprimer;Appliquer;Lire), a leur place.
[[nodiscard]] RecipeManagerLayout recipeManagerLayout(const Object&, double w, double h);
inline constexpr std::string_view kRecipeButtons[] = {"Ajouter", "Modifier", "Supprimer", "Appliquer", "Lire"};
// Ce qu'un clic en (x, y) (repere de l'objet) touche : "bouton:Ajouter",
// "ligne:2" (0 = le premier jeu), ou "".
[[nodiscard]] std::string recipeManagerHit(const Object&, double w, double h, double x, double y, std::size_t records);

} // namespace hmi
