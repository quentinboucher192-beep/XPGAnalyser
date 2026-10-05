// =============================================================================
//  hmi/HmiCharts.hpp - les graphiques du lot 11 : ce qui se calcule sans ecran
// -----------------------------------------------------------------------------
//  SIX GRAPHIQUES, UNE MEME FORME DE REGLAGE. Chacun lit une liste
//  d'expressions ("variables" : a;b;c), leurs noms ("names") et leurs couleurs
//  ("colors") - comme les plumes d'une courbe :
//
//    Graphique en barres   une barre par expression (verticales ou
//                          horizontales), seuils bas et haut en pointilles ;
//    Camembert             la part de chaque expression dans leur somme ; avec
//                          un trou ("hole", en %), c'est un anneau ;
//    Radar                 un axe par expression, un polygone ; une consigne
//                          ("references") en second polygone ;
//    Courbe XY             Y en fonction de X ("xVariable"), une trace des N
//                          derniers points, une courbe de reference ("x,y ...")
//                          et sa tolerance ;
//    Chronogramme d'etats  une ligne par expression, ses etats dans le temps
//                          (les couleurs de "stateList") ;
//    Histogramme           la repartition des mesures d'UNE expression
//                          ("variable") en classes, moyenne, ecart type, Cp et
//                          Cpk entre les tolerances ("low", "high").
//
//  EN MARCHE, les trois premiers sont evalues a chaque image (LiveView les range
//  dans "liveValues") ; les trois autres ont une memoire : le moteur les mesure
//  a chaque cycle (Runtime::chartSeries). Ici : le decoupage des listes, les
//  parts, les classes, les segments d'etat, l'interpolation - le dessin et les
//  tests s'en servent.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi {

// Un element d'un graphique : son expression, son nom (vide : l'expression),
// sa couleur (celle de "colors" a son rang, sinon la palette).
struct ChartItem {
    std::string expression;
    std::string name;
    std::string color;
};
[[nodiscard]] const std::vector<std::string>& chartPalette();
// `listKey` : "variables" (ou "references" pour la consigne d'un radar).
[[nodiscard]] std::vector<ChartItem> chartItems(const Object&, std::string_view listKey = "variables");
// Le nom a montrer : le nom donne, sinon l'expression.
[[nodiscard]] std::string chartItemLabel(const ChartItem&);

// ---- les valeurs evaluees (LiveView -> dessin) -------------------------------------
//  Une valeur par element, separees par le caractere 0x1F (une chaine peut
//  contenir un ';'). "###" : illisible.
inline constexpr char kLiveSeparator = '\x1F';
[[nodiscard]] std::string              joinLiveValues(const std::vector<std::string>&);
[[nodiscard]] std::vector<std::string> splitLiveValues(std::string_view);
// En nombres : TRUE 1, FALSE 0 ; illisible : nullopt.
[[nodiscard]] std::vector<std::optional<double>> liveNumbers(std::string_view);

// ---- camembert ------------------------------------------------------------------------
//  La part de chaque valeur dans leur somme (une valeur negative compte 0). Les
//  angles en degres, 0 en haut, dans le sens horaire. Somme nulle : aucune part.
struct PieSlice {
    double value{0}, fraction{0};
    double from{0}, to{0};
};
[[nodiscard]] std::vector<PieSlice> pieSlices(const std::vector<double>& values);

// ---- histogramme ------------------------------------------------------------------------
struct HistogramStats {
    std::vector<std::size_t> counts;       // une par classe, de lo a hi
    std::size_t below{0}, above{0};        // hors de l'echelle
    std::size_t n{0};
    double      mean{0}, sigma{0}, min{0}, max{0};
    // Entre les tolerances (low, high) : les capabilites ; -1 sans elles (ou
    // sans dispersion). Et les mesures hors tolerance.
    double      cp{-1}, cpk{-1};
    std::size_t outOfTolerance{0};
    [[nodiscard]] std::size_t tallest() const noexcept;
};
[[nodiscard]] HistogramStats histogram(const std::vector<double>& samples, double lo, double hi, int bins,
                                       std::optional<double> low = std::nullopt, std::optional<double> high = std::nullopt);

// ---- courbe XY ----------------------------------------------------------------------------
//  La courbe de reference : "x,y x,y ..." (ou separes par ;). Faux et `why` :
//  un point illisible. Rangee par X croissant.
[[nodiscard]] bool parseXYPoints(std::string_view text, std::vector<std::pair<double, double>>& out, std::string* why = nullptr);
// Y de la courbe a X (interpole entre ses points) ; nullopt hors de ses X.
[[nodiscard]] std::optional<double> interpolateAt(const std::vector<std::pair<double, double>>& curve, double x);

// ---- chronogramme d'etats -------------------------------------------------------------------
//  Les segments d'une ligne entre `start` et `end`, depuis ses changements
//  (instant, valeur), le plus ancien d'abord. Le dernier changement avant
//  `start` donne l'etat au debut ; avant le premier changement : rien.
struct StateSegment {
    double from{0}, to{0};
    double value{0};
};
[[nodiscard]] std::vector<StateSegment> stateSegments(const std::deque<std::pair<double, double>>& changes, double start, double end);

// ---- un axe ---------------------------------------------------------------------------------
//  Des graduations rondes (1, 2, 2,5 ou 5 x 10^n) de lo a hi, environ `approx`.
[[nodiscard]] std::vector<double> niceTicks(double lo, double hi, int approx);
// Une echelle automatique : [lo ; hi] qui contient les valeurs, avec une marge,
// arrondie aux graduations. Sans valeur : [fallbackLo ; fallbackHi].
[[nodiscard]] std::pair<double, double> autoRange(const std::vector<double>& values, double fallbackLo, double fallbackHi,
                                                  bool includeZero = false);

} // namespace hmi
