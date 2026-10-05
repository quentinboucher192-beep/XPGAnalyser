// =============================================================================
//  help/CenterView.hpp - 1.11 (chantier T2, tranche 2) : ce que l'ecran du
//  centre d'aide dessine
// -----------------------------------------------------------------------------
//  Le debut de l'ecran (app/help/HelpCenterScreen, tranche 3) : tout ce qu'il
//  decide est ici, pur et essayable, et l'ecran ne fait que le dessiner -
//  l'arbre (chapitres, sous-chapitres, sujets et leur pastille), le fil
//  d'Ariane, la carte du tutoriel en tete de page, le compteur de la barre et
//  le bas de page (precedent / suivant). La navigation, les favoris et les
//  recents : help::Navigation (HelpSession), avec help::center::topicTarget.
// =============================================================================
#pragma once

#include "CenterIndex.hpp"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace help::center {

// ---- l'arbre ----------------------------------------------------------------
enum class RowKind : std::uint8_t { Chapter, Sub, Topic };

struct TreeRow {
    RowKind     kind{RowKind::Topic};
    int         depth{0};          // 0 : un chapitre ; 1 : un sous-chapitre, ou un sujet sans sous-chapitre ; 2 : un sujet d'un sous-chapitre
    std::string label;             // ce que la ligne ecrit
    std::string key;               // un sujet : sa cle ; un chapitre / sous-chapitre : sa cle de depliage (foldKey)
    Chapter     chapter{Chapter::Start};
    std::size_t count{0};          // un chapitre / sous-chapitre : ses sujets
    std::string pill;              // un sujet : "\xE2\x96\xB6 1 min 04" (la duree de son tutoriel) ; vide sinon
    bool        estimated{false};  // la duree est estimee (aucun tutoriel fourni)
    bool        open{false};       // un chapitre / sous-chapitre : deplie
    bool        current{false};    // un sujet : celui de la page ; un chapitre / sous-chapitre : il le contient
};

// Ce qui est deplie (les cles de depliage) et le sujet ouvert. Le chapitre et
// le sous-chapitre du sujet ouvert sont deplies d'office.
struct TreeState {
    std::set<std::string> open;
    std::string           current;
};

// "ch:<n>" pour un chapitre, "sub:<n>:<sous-chapitre>" pour un sous-chapitre.
[[nodiscard]] std::string foldKey(Chapter c, std::string_view sub = {});

// Les lignes de l'arbre, dans l'ordre : les 9 chapitres toujours, leurs
// sous-chapitres et sujets quand ils sont deplies.
[[nodiscard]] std::vector<TreeRow> treeRows(const Index& ix, const TreeState& state);

// ---- la page ----------------------------------------------------------------
// Le fil d'Ariane : le chapitre, le sous-chapitre s'il y en a un, le titre.
[[nodiscard]] std::vector<std::string> breadcrumb(const Index& ix, std::string_view key);

// La carte du tutoriel, en tete de page : "\xE2\x96\xB6 Regarder le tutoriel \xC2\xB7 1 min 04 \xC2\xB7 7 \xC3\xA9tapes"
// (une estimation le dit : "(dur\xC3\xA9" "e estim\xC3\xA9" "e)").
[[nodiscard]] std::string tutorialCardText(const TutorialInfo& info);

// 1.11, decision 12 du chef : les pages des notes de version n'ont pas de
// tutoriel. Leur page n'a pas la carte "Regarder le tutoriel", leur ligne de
// l'arbre pas de pastille, et le compteur ne les compte pas.
[[nodiscard]] bool hasTutorial(const Topic& t);

// Le compteur de la barre : les sujets dont le tutoriel est fourni (non
// estime), et tous les sujets qui en ont un (hasTutorial).
// "Tous les sujets ont leur tutoriel : n / N".
struct TutorialCount { std::size_t provided{0}, total{0}; };
[[nodiscard]] TutorialCount tutorialCount(const Index& ix);
[[nodiscard]] std::string tutorialCountText(const TutorialCount& c);

// Le bas de page : "\xE2\x80\xB9 <precedent>" et "<suivant> \xE2\x80\xBA", vides aux deux bouts de l'arbre.
struct PageFooter { std::string prevKey, prevTitle, nextKey, nextTitle; };
[[nodiscard]] PageFooter pageFooter(const Index& ix, std::string_view key);

} // namespace help::center
