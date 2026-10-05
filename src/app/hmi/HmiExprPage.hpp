// =============================================================================
//  app/hmi/HmiExprPage.hpp - 1.11 (chantier T3, D5) : la page des expressions,
//                            sa partie sans ecran
// -----------------------------------------------------------------------------
//  Le centre de la page (maquette 1.11, scene 5) est un ARTICLE d'aide compose
//  depuis la table des types (hmi::exprguide) : la syntaxe, les operateurs (un
//  clic les ajoute au champ d'essai), les fonctions utiles, les exemples
//  (« Essayer › » les met dans le champ) et les erreurs courantes (le faux, le
//  juste, la raison, « l'essayer »). HelpArticleView le met en page et le
//  peint ; l'ecran (la page autonome, ou la zone de page du centre de T2) ne
//  fait que relier ses puces au champ d'essai (hmi::exprguide::Bench).
//
//  LES CIBLES DES PUCES (ce que HelpArticleView::linkActivated rend) :
//    "essayer:<source>"         un exemple, le faux ou le juste d'une erreur :
//                               dans le champ d'essai, sur le type de la page ;
//    "essayer@<cle>:<source>"   ... sur le type <cle> (Mistake::caseKey :
//                               « Mode = 1 » est montre sous Enumeration, mais
//                               c'est une condition) ;
//    "inserer:<texte>"          un operateur : ajoute au champ, au curseur ;
//    "expr-<cle>"               un autre type (le sujet du centre d'aide).
// =============================================================================
#pragma once

#include "../../hmi/HmiExprGuide.hpp"
#include "../../ui/widgets/HelpArticleView.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::exprpage {

struct Action {
    enum class Kind : std::uint8_t { Try, Insert, Open } kind{Kind::Try};
    std::string typeKey;   // Try : le type de la case ("" : celui de la page) ; Open : le type a ouvrir
    std::string text;      // Try : la source ; Insert : le texte a ajouter
};
[[nodiscard]] std::optional<Action> parseTarget(std::string_view target);
[[nodiscard]] std::string tryTarget(std::string_view source, std::string_view caseKey = {});

// L'article d'un type : ce que la page montre au centre.
// Tranche 11 (decision du chef) : dans le centre d'aide de T2, l'article est montre
// SANS le champ « Essaie ici » (trialField = false). Il ne parle donc plus d'un champ
// d'essai : il montre, sous son resume, le lien « Essayer dans la page des
// expressions » (cible openPageTarget : "expr:<cle>", que le centre traite) qui ouvre
// la page seule (HmiExprScreen) sur ce type ; ses puces y menent aussi. La page seule
// garde son champ et l'article d'avant (trialField = true).
struct ArticleOptions { bool trialField{true}; };
inline constexpr std::string_view kOpenPageLabel = "Essayer dans la page des expressions";
[[nodiscard]] std::string openPageTarget(std::string_view typeKey);   // "expr:<cle>"
[[nodiscard]] ui::HelpArticle article(const hmi::exprguide::TypeEntry&, ArticleOptions options = {});

// La colonne de gauche quand la page est seule (dans le centre de T2, c'est
// son arbre qui liste les 11 sujets "expr-<cle>").
struct ListRow { std::string key, glyph, title, summary; };
[[nodiscard]] std::vector<ListRow> typeList();

} // namespace app::exprpage
