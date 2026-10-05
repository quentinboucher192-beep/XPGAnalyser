// =============================================================================
//  hmi/HmiRecipes.hpp — les recettes hors du moteur : CSV, comparaison
// -----------------------------------------------------------------------------
//  EXPORTER : un CSV (separateur ';', comme l'Excel francais) - une colonne par
//  element, une ligne par jeu :
//
//      Jeu;Pression (bar);Debit (m3/h);Gaz
//      Azote;7.5;120;'Azote'
//      Argon;6.2;90;'Argon'
//
//  IMPORTER : le meme format (';', tabulation ou ','). Les colonnes sont
//  reconnues par le nom de l'element (l'unite entre parentheses est ignoree) ;
//  un jeu de meme nom est remplace, un nouveau est ajoute. Une colonne qui ne
//  correspond a aucun element est signalee, pas inventee.
//
//  COMPARER : deux jeux, ou un jeu et les valeurs lues dans l'installation.
//  7.5 et 7.50 sont egaux : les nombres sont compares comme des nombres.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi {

[[nodiscard]] std::string recipeCsv(const Recipe&);

struct RecipeImport {
    std::size_t              added{0}, replaced{0};
    std::vector<std::string> warnings;
};
// Dans la recette `recipe` du projet (les jeux ajoutes prennent un identifiant).
[[nodiscard]] bool importRecipeCsv(Project&, Id recipe, std::string_view text, RecipeImport* report = nullptr,
                                   std::string* error = nullptr);

struct RecipeDiff {
    std::string field, unit, left, right;
    bool        differs{false};
};
[[nodiscard]] std::vector<RecipeDiff> compareValues(const Recipe&, const std::vector<std::string>& left,
                                                    const std::vector<std::string>& right);
[[nodiscard]] bool sameRecipeValue(std::string_view a, std::string_view b);

} // namespace hmi
