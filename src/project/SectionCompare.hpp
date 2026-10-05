// =============================================================================
//  project/SectionCompare.hpp - 1.8.0 : comparer deux sections (ou plus)
// -----------------------------------------------------------------------------
//  API > Unites de programme (ou l'arbre) : deux sections choisies (Ctrl+clic),
//  "Comparer". Les lignes sont alignees (plus longue sous-suite commune), une
//  ligne retiree suivie d'une ajoutee au meme endroit est "modifiee" et ses
//  mots differents sont reperes.
//
//  RAPPROCHER LES NOMS. Deux sections jumelles (SFC_ManuA / SFC_ManuB) ne
//  different, ligne a ligne, que par leurs suffixes : Trans_ManuA / Trans_ManuB.
//  Une EQUIVALENCE "A <-> B" fait compter ces noms pour les memes : il ne
//  reste que les vraies differences. Elle est deduite des noms des sections
//  (ce qui differe entre eux), ou de leurs unites (Matrice / Matrice, dans
//  Logigrammes_A / Logigrammes_B) ; d'autres s'ajoutent a la main ([0] <-> [1]).
//  Un identifiant compte pour le meme quand, de chaque cote, le texte de
//  l'equivalence le TERMINE (ou termine un de ses morceaux : SFC_ManuA_Actions)
//  et que le reste est identique. Un nom entier (la chaine 'HCL') ne change pas.
//
//  Ignorer les espaces, la casse, les commentaires : au choix.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace project::compare {

struct Equivalence {
    std::string left, right;
    bool        enabled{true};
    bool        fromNames{false};     // deduite des noms (le comparateur la montre ainsi)
};

struct Options {
    bool ignoreSpaces{true};
    bool ignoreCase{false};
    bool ignoreComments{false};
    std::vector<Equivalence> equivalences;
};

struct Row {
    enum Kind : std::uint8_t { Same, Changed, Removed, Added };
    Kind kind{Same};
    int  left{-1}, right{-1};          // le numero de ligne (0...) de chaque cote ; -1 : absente
};

struct Result {
    std::vector<Row> rows;
    std::size_t leftLines{0}, rightLines{0};
    std::size_t same{0}, changed{0}, added{0}, removed{0};
    int         similarity{0};          // 0..100 : 2 x identiques / (gauche + droite)
    // Les blocs de differences : [premiere, derniere] ligne de rows.
    std::vector<std::pair<std::size_t, std::size_t>> blocks;
};

// Ce qui differe entre deux noms (SFC_ManuA / SFC_ManuB -> A / B ; Config_HCL /
// Config_ND3 -> HCL / ND3) ; des noms egaux : ce qui differe entre les
// proprietaires. Rien si rien ne se deduit (ou si c'est trop long pour un suffixe).
[[nodiscard]] std::optional<Equivalence> equivalenceFromNames(std::string_view leftName, std::string_view rightName,
                                                              std::string_view leftOwner = {}, std::string_view rightOwner = {});

[[nodiscard]] std::vector<std::string> splitLines(std::string_view text);

[[nodiscard]] Result compare(const std::vector<std::string>& left, const std::vector<std::string>& right, const Options& = {});

// Les morceaux (octets [debut, fin)) qui different entre deux lignes modifiees.
struct Span { std::size_t begin{0}, end{0}; };
void inlineDiff(std::string_view left, std::string_view right, const Options&, std::vector<Span>& leftSpans, std::vector<Span>& rightSpans);

// Le pourcentage de lignes semblables, pour le tableau "qui ressemble a qui".
[[nodiscard]] int similarity(const std::vector<std::string>& a, const std::vector<std::string>& b, const Options& = {});

// Le rapport en texte (Copier le rapport) : l'en-tete, les chiffres, chaque
// difference avec ses lignes (- a gauche, + a droite).
[[nodiscard]] std::string report(std::string_view leftTitle, std::string_view rightTitle, const std::vector<std::string>& left,
                                 const std::vector<std::string>& right, const Options&, const Result&);

} // namespace project::compare
