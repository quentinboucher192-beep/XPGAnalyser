// =============================================================================
//  ui/widgets/ExprField.hpp - les champs a expression, partout pareils (1.10, K)
// -----------------------------------------------------------------------------
//  Une case de PropertyGrid qui accepte "=expression" se reconnait a son
//  INVITE (Property::placeholder) : elle finit par "= expression" ("nombre ou
//  = expression", "12 . valeur par defaut . ou = expression"). Le mot de tete
//  dit le type attendu (l'aide a la saisie s'en sert). La grille en fait :
//    vide                la pastille fx en creux et l'invite en gris ;
//    avec une expression le "=" devant elle (au repos et a l'edition), la
//                        pastille pleine, un bouton X au bout de la ligne et le
//                        clic droit (Modifier l'expression..., Retirer
//                        l'expression, Copier, Revenir a la valeur par
//                        defaut) ;
//    retirer             X, le menu, effacer tout le texte ou le "=" : la
//                        grille appelle commit("=") - l'hote revient a la
//                        valeur fixe (celle d'avant, sinon celle par defaut).
//  Deux sortes de champs :
//    mark()       une valeur fixe ET une expression (l'inspecteur) : commit
//                 recoit "=..." pour l'expression, "=" seul pour la retirer ;
//    markWhole()  la valeur EST une expression (Condition, Visibilite
//                 (expression)...) : le "=" tape est retire avant commit, et
//                 commit("") la vide.
// =============================================================================
#pragma once

#include "DataViews.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace ui::exprfield {

// Ce qu'attend la case (l'invite et la liste d'aide en dependent).
enum class Expect : std::uint8_t { Value, Bool, Number, Text, Color, View, List, Template, Time };

// Le mot de l'invite ("nombre") et l'invite entiere ("nombre ou = expression").
[[nodiscard]] std::string_view word(Expect) noexcept;
[[nodiscard]] std::string invite(Expect);
inline constexpr std::string_view kTail = "= expression";

// La case accepte une expression (son invite finit par "= expression").
[[nodiscard]] bool accepts(const PropertyGrid::Property&) noexcept;
// Le type attendu, lu dans l'invite (Value : rien de particulier).
[[nodiscard]] Expect expectOf(const PropertyGrid::Property&) noexcept;

// "Revenir a la valeur par defaut" (le clic droit) : la grille appelle
// p.revert s'il existe, sinon commit(kToDefault). mark() et markWhole() le
// changent en "" (une case videe revient a sa valeur par defaut), sauf si
// l'hote le traite lui-meme (mark(p, e, true) : l'inspecteur, qui remet la
// valeur d'un objet neuf de ce genre).
inline constexpr std::string_view kToDefault = "\x1B" "defaut";

// Marquer une case : son invite (une invite deja la est gardee devant :
// "12 . valeur par defaut . ou = expression").
void mark(PropertyGrid::Property&, Expect, bool ownDefault = false);
// Une case "tout expression" : marquee, sa valeur montree comme expression
// (pastille pleine, "=" devant) des qu'elle n'est pas vide (`asExpression`
// faux : l'hote decide, un litteral n'en est pas une), et le "=" tape retire
// avant commit.
void markWhole(PropertyGrid::Property&, Expect, bool asExpression = true);

// 1.10.1 (U2) : CE QUE LA GRILLE MONTRE d'une case a expression. La pastille fx et
// le type ecrit au bout du nom disent deja qu'elle accepte une expression : le nom
// perd son "(expression)" (Visibilite (expression) -> Visibilite) et l'invite son
// "ou = expression" ; le type ne reste dans l'invite que s'il n'est pas ecrit au
// bout du nom (`typeShown` faux : l'infobulle, une colonne trop etroite). Ce qui
// precede reste ("12 . valeur par defaut"). Property::name et Property::placeholder
// ne changent pas : la case se reconnait toujours a son invite, et se cherche
// toujours par son nom.
[[nodiscard]] std::string_view shownLabel(const PropertyGrid::Property&) noexcept;
[[nodiscard]] std::string shownInvite(const PropertyGrid::Property&, bool typeShown = true);

// Le texte du champ ouvert : "=" + l'expression, sinon la valeur.
[[nodiscard]] std::string editText(const PropertyGrid::Property&);
// Ce qui part a commit apres la saisie `typed` : une case qui avait une
// expression et qu'on a videe (ou laissee a "=") -> "=" (la retirer).
[[nodiscard]] std::string normalized(const PropertyGrid::Property&, std::string_view typed);

// Le bouton X "Retirer l'expression" d'une case (tests, scripts) ; faux s'il
// n'est pas montre (pas d'expression, pas modifiable, hors de la vue).
[[nodiscard]] bool clearRect(const PropertyGrid&, std::string_view name, gfx::Rect& out);
// Le menu du clic droit d'une case a expression : un enfant de la grille, de cet id.
[[nodiscard]] inline std::string menuId(const PropertyGrid& g) { return g.id() + ".exprmenu"; }

inline constexpr std::string_view kRemoveTip = "Retirer l'expression : revenir \xC3\xA0 la valeur fixe (Ctrl+Suppr)";
inline constexpr std::string_view kRemove    = "Retirer l'expression";
inline constexpr std::string_view kModify    = "Modifier l'expression\xE2\x80\xA6";
inline constexpr std::string_view kCopy      = "Copier";
inline constexpr std::string_view kDefault   = "Revenir \xC3\xA0 la valeur par d\xC3\xA9" "faut";

} // namespace ui::exprfield
