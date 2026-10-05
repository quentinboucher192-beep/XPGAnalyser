// =============================================================================
//  app/hmi/HmiQuickTrend.hpp - la fenetre graphique temporaire (1.10, chantier O)
// -----------------------------------------------------------------------------
//  CLIC DROIT SUR DES VARIABLES IHM -> "Ouvrir une visualisation graphique".
//  Une fenetre flottante et redimensionnable (l'ecran la pose dans une fenetre
//  a elle), plusieurs a la fois, RIEN N'EST ENREGISTRE dans le projet. Une
//  courbe par variable : un nombre en ligne, un BOOL en escalier ; une
//  structure arrive deja depliee en ses membres numeriques (le volet le fait).
//
//    +---------------------------------------------------------------------+
//    | [10 s][1 min][5 min][30 min] [Pause] [Auto] [+] [PNG] [CSV]          |  barre
//    |  100 -+                                                             |
//    |       |        ____/\____            |                              |  trace
//    |    0 -+------------------------------|----------- (le curseur)      |
//    | [x] A = 12,5   [x] B = 1   [ ] C                                    |  legende
//    +---------------------------------------------------------------------+
//
//  LES VALEURS : sample(t) lit chaque variable par la source (l'ecran : la
//  simulation de l'IHM, sinon celle de l'API). Rien ne tourne : la fenetre
//  dit "Demarre la simulation pour voir les valeurs". Les points de plus de
//  30 minutes s'oublient.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../ui/Widget.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace app {

class HmiQuickTrend final : public ui::Widget {
public:
    // D'ou viennent les valeurs. read : vrai et la valeur (un BOOL : 0 ou 1 et
    // `isBool`) ; faux : pas de valeur (rien ne tourne, nom inconnu, un texte).
    struct Source {
        std::function<bool()> running;                                                  // une simulation tourne
        std::function<bool(const std::string& path, double& value, bool& isBool)> read;
        std::function<double()> clock;                                                  // secondes ; vide : l'horloge du poste
    };
    struct Point { double t{0}, v{0}; };
    struct Series {
        std::string       path;
        bool              isBool{false};
        bool              hidden{false};
        std::deque<Point> points;
    };

    HmiQuickTrend(std::string id, std::vector<std::string> paths, Source source);

    [[nodiscard]] std::string title() const;                 // "Graphique : A, B, C"
    [[nodiscard]] const std::vector<Series>& series() const noexcept { return series_; }
    bool addVariable(const std::string& path);                // faux : deja tracee (ou vide)
    bool removeVariable(std::size_t index);
    void setHidden(std::size_t index, bool hidden);           // la legende cliquee

    // Une lecture de chaque variable a l'instant `t` (secondes). Rien ne tourne :
    // rien n'est ajoute. En pause les lectures continuent, l'affichage reste
    // fige (window()). Rend le nombre de valeurs lues.
    std::size_t sample(double t);
    void tick();                                              // sample(horloge) : l'ecran, a chaque image
    [[nodiscard]] bool live() const;                          // la source tourne

    static constexpr double kDurations[4] = {10.0, 60.0, 300.0, 1800.0};
    void   setDuration(double seconds);
    [[nodiscard]] double duration() const noexcept { return duration_; }
    void   setPaused(bool on);
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    void   setAutoScale();
    void   setFixedScale(double lo, double hi);
    [[nodiscard]] bool autoScale() const noexcept { return auto_; }
    [[nodiscard]] std::pair<double, double> scale() const;   // l'echelle montree (bas, haut)
    [[nodiscard]] std::pair<double, double> window() const;  // les heures montrees (debut, fin)

    // Le curseur : une heure ; les valeurs lues la (le dernier point avant).
    void   setCursor(double t);
    void   clearCursor();
    [[nodiscard]] bool hasCursor() const noexcept { return cursorOn_; }
    [[nodiscard]] double cursor() const noexcept { return cursorT_; }
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> cursorValues() const;

    // Les exports. CSV : "Temps (s);A;B" puis une ligne par lecture (';' et la
    // virgule decimale, comme Excel en francais). L'image : l'ecran prend le
    // widget (exportRequested) et passe par le dialogue des exports.
    [[nodiscard]] std::string csv() const;
    // L'image (PNG) : a la prochaine image dessinee, le rectangle de la fenetre
    // est lu dans le rendu et rendu a `done` (le PNG ; vide : ce rendu ne sait
    // pas relire son image). Pendant le dessin : `done` ecrit, rien ne s'ouvre.
    void requestImage(std::function<void(std::vector<std::uint8_t>)> done);
    [[nodiscard]] bool imagePending() const noexcept { return static_cast<bool>(imageDone_); }
    enum class Export : int { Png = 0, Csv = 1 };
    const core::SignalPtr<int> exportRequested = core::Signal<int>::create();   // Export
    const core::SignalPtr<> addRequested = core::Signal<>::create();            // le bouton +
    const core::SignalPtr<> changed = core::Signal<>::create();                  // le titre a pu changer
    // Rien ne tourne : la fenetre le dit et PROPOSE de demarrer (un bouton au
    // milieu du trace) ; rien ne demarre tout seul, l'ecran fait le geste.
    const core::SignalPtr<> startRequested = core::Signal<>::create();

    // Pour les scripts et les essais : ou est un bouton de la barre, une entree
    // de la legende (PLegend0 + rang), la zone du trace, le bouton Demarrer
    // (PStart : seulement quand rien ne tourne).
    enum Part : int { PDuration0 = 0, PPause = 10, PScale = 11, PAdd = 12, PExportPng = 13, PExportCsv = 14, PStart = 15, PPlot = 50, PLegend0 = 100 };
    [[nodiscard]] bool partRect(int part, gfx::Rect& out) const;
    [[nodiscard]] std::string statusText() const;             // ce que dit la fenetre (arretee, en pause...)

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    [[nodiscard]] double now() const;
    [[nodiscard]] gfx::Rect plotRect() const;
    [[nodiscard]] float legendWidth(std::size_t i) const;
    void trim();

    Source              source_;
    std::vector<Series> series_;
    double              duration_{60.0};
    bool                paused_{false};
    bool                auto_{true};
    double              lo_{0.0}, hi_{100.0};
    bool                cursorOn_{false};
    double              cursorT_{0.0};
    double              last_{0.0};          // l'heure de la derniere lecture
    double              frozen_{0.0};        // en pause : la fin de la fenetre montree
    bool                any_{false};         // au moins une lecture
    std::function<void(std::vector<std::uint8_t>)> imageDone_;   // l'image demandee
};

} // namespace app
