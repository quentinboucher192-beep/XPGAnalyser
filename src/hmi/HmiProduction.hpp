// =============================================================================
//  hmi/HmiProduction.hpp - les objets de production du lot 11 : les calculs
//                          et la geometrie
// -----------------------------------------------------------------------------
//  COMPTEURS DE PRODUCTION  les pieces bonnes et rebutees du poste (les
//                           compteurs de l'automate, depuis le debut du poste),
//                           la cadence (pieces par heure, sur une fenetre), et
//                           le TRS = disponibilite x performance x qualite :
//                             disponibilite = temps de marche / temps du poste
//                             performance   = pieces faites / pieces possibles
//                                             a la cadence nominale pendant la marche
//                             qualite       = bonnes / faites
//                           Remis a zero a chaque debut de poste ("06:00;14:00;22:00")
//                           ou par son bouton RAZ.
//  TABLEAU DE VARIABLES     une ligne par variable : nom, valeur en direct,
//                           unite ; un clic sur la valeur la modifie (Entree
//                           ecrit, permission Piloter).
//  EDITEUR DE RECETTE       un jeu d'une recette, element par element : la
//                           valeur du jeu (modifiable), celle de l'installation,
//                           l'ecart ; Enregistrer, Appliquer, Lire, Annuler,
//                           Nouveau.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// ---- compteurs de production -----------------------------------------------------------
struct ProductionFigures {
    double good{0}, bad{0}, total{0};
    double rate{0};                       // pieces par heure, sur la fenetre de cadence
    double availability{0}, performance{0}, quality{0}, oee{0};   // 0..1
    double runSeconds{0}, plannedSeconds{0};
    double target{0};                     // l'objectif du poste (0 : aucun)
    bool   running{false};
    std::string shift;                    // "Poste de 14:00" ("depuis 14:05:12" apres une RAZ)
};
// Les chiffres, depuis les compteurs du poste, les temps et la cadence nominale
// (pieces par heure). Sans temps de marche : performance 0 ; sans piece : qualite 1.
[[nodiscard]] ProductionFigures productionFigures(double good, double bad, double runSeconds, double plannedSeconds,
                                                  double idealRatePerHour, double rate, double target);
// "06:00;14:00;22:00" -> minutes du jour, rangees. Faux et `why` : une heure illisible.
[[nodiscard]] bool parseShifts(std::string_view text, std::vector<int>& minutes, std::string* why = nullptr);
// Le debut du poste en cours a cette minute du jour (0..1439) ; -1 : pas de postes.
// Avant le premier debut : le dernier de la veille.
[[nodiscard]] int currentShift(const std::vector<int>& shifts, int minuteOfDay);
// "14:00"
[[nodiscard]] std::string minutesText(int minuteOfDay);

struct ProductionLayout {
    Box title, reset;
    Box tiles[4];                          // bons, rebuts, cadence, TRS
    Box bars[3];                           // disponibilite, performance, qualite
    Box progress;                          // l'objectif (vide : aucun)
};
[[nodiscard]] ProductionLayout productionLayout(const Object&, double w, double h);
[[nodiscard]] std::string productionHit(const Object&, double w, double h, double x, double y);   // "raz"

// ---- tableau de variables ----------------------------------------------------------------
struct VariableRow {
    std::string expression, name, unit;
};
[[nodiscard]] std::vector<VariableRow> variableRows(const Object&);
struct GridLayout {
    double              headerH{24}, rowH{22};
    std::vector<double> xs;                // les bords des colonnes (n + 1)
    std::size_t         visible{0};        // les lignes qui tiennent
    Box                 table;             // en-tete + lignes
    [[nodiscard]] Box cell(std::size_t row, std::size_t col) const;
};
// Nom | Valeur | Unite.
[[nodiscard]] GridLayout variableTableLayout(const Object&, double w, double h);
[[nodiscard]] std::string variableTableHit(const Object&, double w, double h, double x, double y, std::size_t rows);   // "ligne:2"

// ---- editeur de recette ---------------------------------------------------------------------
inline constexpr std::string_view kRecipeEditorButtons[] = {"Enregistrer", "Appliquer", "Lire", "Annuler", "Nouveau"};
struct RecipeEditorLayout {
    Box prev, next, name;                  // le jeu montre, et ses fleches
    struct Button { std::string label; Box box; };
    std::vector<Button> buttons;
    GridLayout grid;                       // Element | Jeu | Installation | Ecart (| Unite)
    bool compare{true};
};
[[nodiscard]] RecipeEditorLayout recipeEditorLayout(const Object&, double w, double h);
// "precedent", "suivant", "bouton:Enregistrer", "ligne:2" (la valeur du jeu), "".
[[nodiscard]] std::string recipeEditorHit(const Object&, double w, double h, double x, double y, std::size_t fields);
// Une valeur de jeu montree a l'operateur : une chaine sans ses apostrophes
// ('Azote N2' -> Azote N2, $' -> ', $$ -> $) ; le reste tel quel.
[[nodiscard]] std::string recipeValueShown(std::string_view value);

} // namespace hmi
