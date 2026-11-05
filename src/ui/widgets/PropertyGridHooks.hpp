#pragma once
// =============================================================================
//  ui/widgets/PropertyGridHooks.hpp - 1.11 (integration I111, lien 5)
// -----------------------------------------------------------------------------
//  "La page des expressions..." a la fin du menu du clic droit d'une case a
//  expression (ui::PropertyGrid, DataViews.cpp) : l'appli (App) branche le
//  crochet et ouvre la page des expressions de T3 sur le type de la case. Sans
//  crochet (les essais) : pas d'entree. A part de DataViews.hpp, que presque
//  tout inclut : le changer ferait tout reconstruire.
// =============================================================================

#include "DataViews.hpp"

#include <functional>
#include <string_view>

namespace ui::propertygrid {

using ExpressionPageHook = std::function<void(const PropertyGrid::Property&)>;
void setExpressionPageHook(ExpressionPageHook hook);
[[nodiscard]] const ExpressionPageHook& expressionPageHook();

// 1.12.18 : « Aller a la definition » (F12) et « Voir les usages » (Maj+F12) du nom sous le clic
// dans une expression - l'ecran d'analyse le branche (comme dans le code). Sans crochet : pas
// d'entree. Vrai : le nom a ete trouve.
using NameHook = std::function<bool(std::string_view name, bool usages)>;
void setNameHook(NameHook hook);
[[nodiscard]] const NameHook& nameHook();

// Le libelle de l'entree, que le tutoriel de l'expression (T1, etape 7) vise : "menu:<libelle>".
inline constexpr std::string_view kExpressionPageItem = "La page des expressions\xE2\x80\xA6";

} // namespace ui::propertygrid

// =============================================================================
//  1.24.0 : LES CHEMINS (le client, 05/11 : << a chaque fois qu'il y a un chemin vers un
//  fichier / dossier je veux pouvoir faire clic droit -> 'ouvrir dans l'explorateur' ou
//  'aller a' >>). Une case de la grille (PropertyGrid::Property::path), une case ou une
//  colonne d'un tableau (CellStyle::path, TableView::setPathColumn), un noeud de l'arbre :
//  leur clic droit propose Ouvrir dans l'explorateur, Aller a, Copier le chemin.
//
//  Le widget ne connait pas l'application : elle installe UNE FOIS le fournisseur (l'ecran du
//  projet) - resoudre un chemin, ouvrir l'Explorateur, aller a l'element. Sans fournisseur (les
//  essais qui ne l'installent pas) : aucune entree.
//
//  UN CHEMIN, CE PEUT ETRE : un chemin absolu ; un relatif (depuis le dossier du projet, ou depuis
//  la base de sa colonne) ; "ressource:Nom" (le fichier d'une ressource du projet) ;
//  "externe:Nom" (un fichier externe du projet).
// =============================================================================
#include <string>
#include <vector>

namespace ui::paths {

struct Provider {
    // Le chemin sur le disque ; vide : inconnu (un projet jamais enregistre). Sans lui : tel quel.
    std::function<std::string(const std::string& path)>                         resolve;
    // Ouvrir dans l'Explorateur (un fichier : choisi ; un dossier : ouvert) ; "" : fait, sinon la raison.
    std::function<std::string(const std::string& resolved)>                     reveal;
    // Aller a, dans l'application ; dry : seulement dire si c'est possible. "" : oui (fait) ; sinon la raison.
    std::function<std::string(const std::string& path, const std::string& resolved, bool dry)> goTo;
};
void setProvider(Provider provider);
[[nodiscard]] const Provider& provider();
// Un fournisseur est-il installe (les entrees existent) ?
[[nodiscard]] bool active();

// Les rangs des trois entrees, a partir d'un premier id (firstId + kReveal...).
inline constexpr int kReveal = 0, kGoTo = 1, kCopy = 2;
// Le chemin sur le disque : `base` (un dossier) devant un relatif, puis le fournisseur.
[[nodiscard]] std::string resolve(const std::string& path, const std::string& base = {});
// Les trois entrees (ids firstId + kReveal, kGoTo, kCopy), chacune grisee avec sa raison (<< logo.png
// n'existe pas >>). Vide : pas de fournisseur, ou pas de chemin.
[[nodiscard]] std::vector<PopupMenu::Item> menuItems(const std::string& path, int firstId, const std::string& base = {});
// L'entree choisie : vrai si c'est une des trois (firstId..firstId+2).
[[nodiscard]] inline bool owns(int id, int firstId) noexcept { return id >= firstId && id <= firstId + kCopy; }
// Son geste ; "" : fait ; sinon la raison (a dire dans la barre d'etat).
std::string run(int id, int firstId, const std::string& path, const std::string& base = {});

} // namespace ui::paths
