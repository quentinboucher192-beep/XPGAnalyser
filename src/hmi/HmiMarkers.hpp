// =============================================================================
//  hmi/HmiMarkers.hpp - les reperes `$...$` (1.11, chantier REP) : ce qui varie
//                       quand on duplique, transparent pour le calcul
// -----------------------------------------------------------------------------
//  LA DEMANDE DU CLIENT DU 03/10 : un repere entoure un MORCEAU d'expression
//  ou de texte, n'importe lequel : `$V[1].Ouv$`, `$V[0]$.Nom`, `$V[0].Pos > 10$`,
//  `$Vanne$`. Il dit seulement "ce morceau varie quand on duplique"
//  (Dupliquer..., HmiDuplicate) ; ses `$` sont TRANSPARENTS pour le calcul :
//  l'analyse (Expression::compile, TextTemplate::compile, la verification de
//  HmiExprCheck) lit le texte sans eux, dans l'editeur, la verification, la
//  simulation et l'execution. Le texte enregistre les garde.
//   - le contenu : 2 caracteres au moins (1.11.24 : ou une lettre seule, $V$),
//     sur une ligne, sans `$`, qui ne commence ni par un chiffre ni par une
//     espace et ne finit pas par une espace (`$N`, `$0D$0A`, "12 $ ou 15 $" ne
//     sont pas des reperes) ;
//   - `$$` (la ou un repere pourrait s'ouvrir) ecrit un vrai `$` ;
//   - dans une EXPRESSION, un `$` entre apostrophes (ou guillemets) n'est
//     jamais un repere : les echappements du ST ('$N', '$0D', '5$$') gardent
//     leur sens ; dans un TEXTE (libelle, message), seulement dans ses trous
//     {...} : les apostrophes du francais ("l'armoire $Arm$") ne ferment rien.
//
//  UN EN-TETE A PART, ET C'EST VOULU (decision 73 du 03/10) : HmiExpr.hpp est
//  lu par plus de cent objets de l'appli (et autant sous Windows). Le lecteur
//  vit ici pour que HmiExpr.hpp garde son contenu de la 1.10 : seuls les
//  fichiers qui lisent les reperes se recompilent.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::markers {

enum class Mode : std::uint8_t { Expression, Text };

struct Span {
    std::size_t at{0};        // le premier $
    std::size_t length{0};    // `$...$` entier
    [[nodiscard]] std::size_t end() const noexcept { return at + length; }
    [[nodiscard]] std::string_view content(std::string_view text) const { return text.substr(at + 1, length - 2); }
};

[[nodiscard]] bool validContent(std::string_view content) noexcept;
[[nodiscard]] std::vector<Span> find(std::string_view text, Mode mode = Mode::Expression);
// Le texte sans les `$` de ses reperes, le reste tel quel ; Mode::Text ecrit
// aussi `$$` en `$` hors des trous (ce que montre un texte).
[[nodiscard]] std::string strip(std::string_view text, Mode mode = Mode::Expression);

// 1.11.1 (REP) : LES SCRIPTS ST. Leur code se lit comme une expression (mode
// Expression : les chaines ST '$N' '$0D' '5$$' "...", les commentaires (* *) et //,
// exactement les regles du lexeur de sim). Un `$` n'est jamais une fin de ligne :
// les LIGNES ne bougent pas (les erreurs de sim::parse restent justes) ; seules les
// COLONNES d'une ligne a repere glissent, et `column` les rend au texte d'origine.
struct Stripped {
    std::string              text;      // sans les `$` des reperes
    std::vector<std::size_t> removed;   // les `$` retires : leurs positions dans le texte d'origine, croissantes
    // La colonne (1 = le premier octet de la ligne) du texte d'origine qui correspond
    // a la colonne `column` de la ligne `line` (1 = la premiere) du texte retire.
    [[nodiscard]] int column(std::string_view original, int line, int column) const;
};
[[nodiscard]] Stripped stripKeep(std::string_view text, Mode mode = Mode::Expression);

// 1.11.1 (REP-5) : UN `$` MAL APPARIE. `lone` : les `$` restes seuls (ni repere, ni
// `$$`, hors chaines et commentaires). `repairPairing` : s'il n'y en a qu'un, le texte
// probable, avec un `$` de plus - `$V[1].Ouv+$V[2].Ouv$` (le repere `V[1].Ouv+` a pris
// le `$` du suivant) -> `$V[1].Ouv$+$V[2].Ouv$` ; `$V[1].Ouv + 2` -> `$V[1].Ouv$ + 2` ;
// `V[1].Ouv$ + 2` -> `$V[1].Ouv$ + 2`. Vide : rien de sur a proposer.
[[nodiscard]] std::vector<std::size_t> lone(std::string_view text, Mode mode = Mode::Expression);
[[nodiscard]] std::string repairPairing(std::string_view text, Mode mode = Mode::Expression);
// 1.11.1 (REP-5, le `$` de fin oublie) : la faute dite a sa place, et le texte juste -
// "il manque le $ de fin de $V[1].Ouv : ecris $V[1].Ouv$+$V[2].Ouv$", "il manque le $
// d'ouverture de V[1].Ouv$ : ecris $V[1].Ouv$ + 2". Vide : rien de sur (repairPairing vide).
[[nodiscard]] std::string pairingAdvice(std::string_view text, Mode mode = Mode::Expression);
// La regle que suit la faute d'un `$` seul, dans une expression comme dans un script (l'aide la cite) :
// " - un repere s'ecrit entre deux $ ($V[1].Ouv$) ; un vrai $ s'ecrit $$ dans une chaine".
[[nodiscard]] std::string_view pairingRule() noexcept;

// 1.11.2 (REP) : LE `$` RESTE SEUL D'UN SCRIPT ST. Le conseil de pairingAdvice pour la ligne `line`
// (1 = la premiere) d'un texte de plusieurs lignes, et la LIGNE juste : "il manque le $ de fin de $V[1] :
// ecris y := $V[1]$;". Les chaines, les commentaires (meme ouverts sur une ligne d'avant) et `$$` se
// lisent dans tout le texte ; les `$` seuls des autres lignes restent ce qu'ils sont. Vide : pas
// exactement un `$` seul sur la ligne, ou rien de sur.
[[nodiscard]] std::string lineAdvice(std::string_view text, int line, Mode mode = Mode::Expression);
// La faute de l'analyse d'un script ST a la ligne `line` (sans "ligne N : "), dite comme dans une
// expression quand elle parle d'un '$' ("caractere inattendu : '$'") : le conseil de lineAdvice s'il
// est sur, puis la regle. Une autre faute revient telle quelle.
[[nodiscard]] std::string explainScriptDollar(std::string message, std::string_view text, int line);

} // namespace hmi::markers
