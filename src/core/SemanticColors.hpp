// =============================================================================
//  core/SemanticColors.hpp - 1.23.1 : le registre des COULEURS SEMANTIQUES
// -----------------------------------------------------------------------------
//  Le client (05/11) : << des couleurs semantiques (globales) utilisables partout
//  comme des constantes globales, pouvoir en definir soi-meme >>. Une couleur
//  semantique a un nom (Alarme, Marche, Fond) et une couleur ; une case couleur
//  la cite par << @Nom >> (une formule aussi : SEL(Defaut, '@Alarme', '@Normal')).
//
//  Ce registre est ce que LISENT ceux qui dessinent (le painter des vues, les
//  pastilles de l'inspecteur, l'export) : le nom et la couleur de chacune, sans
//  le projet. hmi::semantic::publish (HmiSemanticColors.hpp) le remplit depuis le
//  projet ouvert ; il est partage entre les fils (une lecture : une copie
//  partagee, sans verrou long).
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core::semantic {

// Le nom et la couleur (#RRGGBB ou #RRGGBBAA) de chaque couleur semantique ; remplace tout le registre.
void publish(std::vector<std::pair<std::string, std::string>> colors);
// La couleur d'un nom (sans casse, sans le @) ; nullopt : inconnu.
[[nodiscard]] std::optional<std::string> lookup(std::string_view name);
// << @Nom >> (blancs du bord permis) : vrai, et `name` (sans le @).
[[nodiscard]] bool isReference(std::string_view text, std::string* name = nullptr);
// Le texte d'une case couleur, la reference remplacee par sa couleur (une reference inconnue : telle quelle ; un autre
// texte : tel quel). Une chaine de references (@A vaut @B) est suivie, 8 fois au plus.
[[nodiscard]] std::string resolve(std::string_view text);
// Le nombre de couleurs publiees (essais).
[[nodiscard]] std::size_t count();
// Le nom et la couleur de chacune, dans l'ordre publie (celles du projet, puis les fournies) : la palette les propose.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> all();

} // namespace core::semantic
