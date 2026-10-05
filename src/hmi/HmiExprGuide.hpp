// =============================================================================
//  hmi/HmiExprGuide.hpp - 1.11 (chantier T3, D5) : la table des types
//                         d'expression
// -----------------------------------------------------------------------------
//  UNE SEULE TABLE DANS LES SOURCES. Pour chacun des 11 types qu'une case de
//  l'inspecteur peut attendre (BOOL, entier, reel, texte, texte a trous,
//  couleur, duree / heure, vue, enumeration, membre et element, liste de
//  choix), elle dit comment l'expression s'ecrit : la syntaxe, les
//  operateurs, les fonctions utiles, des exemples et les erreurs courantes
//  avec leur correction.
//
//  QUI LA LIT : la page des expressions du centre d'aide, l'index du centre
//  (11 sujets "expr-<cle>", remplis par l'appli : l'index ne depend pas de
//  xpg_hmi), l'aide a la saisie, et les tutoriels deduits d'un type
//  d'expression.
//
//  LES EXEMPLES SONT VERIFIES : l'essai expressions111() les evalue tous avec
//  le vrai moteur (hmi::Expression, hmi::exprcheck) sur les variables
//  d'exemple du banc (HmiExprBench) ; chaque erreur courante doit y etre
//  reconnue, et sa correction, quand le banc en propose une, est exactement
//  le "juste" de la table.
//
//  Les textes sont en UTF-8 (echappements \x..), tutoient, et suivent la
//  maquette 1.11 (scene 5) et les decisions du 02/10 : une couleur s'ecrit
//  '#E53935' ; SEL(G, IN0, IN1) rend IN1 quand G est vraie.
// =============================================================================
#pragma once

#include "HmiExprCheck.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::exprguide {

enum class Kind : std::uint8_t { Bool, Integer, Real, Text, Template, Color, Time, View, Enum, Member, Choice };

struct Operator {
    std::string_view chip;      // la pastille : "SEL", "AND", "'#RRGGBB'"
    std::string_view text;      // ce qu'il fait, en quelques mots
};

struct Function {
    std::string_view call;      // "SEL(c, a, b)"
    std::string_view returns;   // "COLOR"
    std::string_view does;      // une phrase courte
};

struct Example {
    std::string_view source;    // ce que le clic met dans le champ d'essai ("=..." sauf texte a trous)
    std::string_view caption;   // ce qu'il montre
};

struct Mistake {
    std::string_view wrong;     // le faux, barre
    std::string_view right;     // le juste
    std::string_view reason;    // pourquoi, et quoi faire
    // Vrai : le faux se lit et s'evalue, mais ne fait pas ce qu'on croit (SEL
    // a l'envers). Le banc ne peut pas le reconnaitre : la page le montre.
    bool trap{false};
    // La cle du type de case ou essayer le faux et le juste ; vide : ce type.
    // ("Mode = 1" est montre sous Enumeration, mais c'est une condition.)
    std::string_view caseKey{};
};

struct TypeEntry {
    Kind             kind;
    std::string_view key;        // "bool", "entier", ... ; le sujet du centre est "expr-<key>"
    std::string_view title;      // "Couleur"
    std::string_view glyph;      // la pastille de la liste : "B", "12", "1,5", "#"
    std::string_view wanted;     // le badge "type attendu" : "BOOL", "COLOR", "T_MODE"
    std::string_view summary;    // une ligne d'exemple, pour la liste et la recherche
    std::string_view syntax;     // le cadre ; les lignes sont separees par '\n'
    exprcheck::Want  want;       // ce que la case attend, pour exprcheck::check
    bool             keepEquals; // le champ d'essai garde "=" (faux : texte a trous)
    std::vector<Operator> operators;
    std::vector<Function> functions;
    std::vector<Example>  examples;
    std::vector<Mistake>  mistakes;
};

// Les 11 types, dans l'ordre de la page.
[[nodiscard]] const std::vector<TypeEntry>& all();
// Par la cle ("couleur") ou par la cle du sujet ("expr-couleur") ; nullptr : aucun.
[[nodiscard]] const TypeEntry* find(std::string_view key);
[[nodiscard]] const TypeEntry* forKind(Kind);
// F1 dans une case d'expression : le type de la page qui correspond a ce
// qu'elle attend (Bool -> BOOL, Color -> Couleur, Number -> Reel, Text ->
// Texte, Any -> Membre et element).
[[nodiscard]] const TypeEntry* forWant(exprcheck::Want);
// La cle du sujet du centre d'aide : "expr-couleur".
[[nodiscard]] std::string topicKey(const TypeEntry&);

} // namespace hmi::exprguide
