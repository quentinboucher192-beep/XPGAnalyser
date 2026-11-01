// =============================================================================
//  app/PerfPane.hpp - 1.22.2 : l'onglet Performances (le panneau du bas)
// -----------------------------------------------------------------------------
//  Le client (01/11) : << ajoute un onglet quelque part avec la performance du
//  logiciel, qu'est-ce qui consomme, car la l'IA on dirait que ca fait parfois
//  laguer >>. A cote de Sorties, Console et Diagnostics (Projet > Disposition :
//  bas.performances). Il lit la mesure de core/Perf.hpp :
//
//    * en haut : la fenetre (10 s, 60 s, depuis le lancement), le seuil des
//      saccades (33, 50, 100, 250 ms), Remettre a zero, Copier le rapport ;
//    * a gauche : la cadence et le temps d'une image, la memoire et le
//      processeur du processus, la courbe des 60 dernieres secondes (la pire
//      image de chaque seconde en barres, la moyenne en trait, le seuil), les
//      travaux de fond en cours ;
//    * au milieu : CE QUI CONSOMME - une ligne par partie, la plus gourmande
//      (temps propre cumule sur la fenetre) d'abord : sa part, sa moyenne par
//      image, sa pire image, ses appels ;
//    * a droite : LE JOURNAL DES SACCADES - chaque image d'au moins le seuil,
//      la plus recente en haut, avec les trois parties qui l'ont prise.
//  Les deux tableaux sont ceux de l'appli : le clic droit (Copier), Ctrl+C, le
//  tri par colonne.
//
//  SANS COUTER LUI-MEME. Rien ne se relit a chaque image : tick() relit la
//  mesure au plus quatre fois par seconde, et seulement quand l'onglet est a
//  l'ecran (la memoire et le processeur : une fois par seconde, core/Perf).
//  Les textes des cases sont faits a la relecture, pas au dessin.
// =============================================================================
#pragma once

#include "../core/Perf.hpp"
#include "../core/Signal.hpp"
#include "../ui/Widget.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace app {

class PerfPane final : public ui::Widget {
public:
    explicit PerfPane(std::string id);
    ~PerfPane() override;

    // A chaque image (le panneau du bas) : `shown`, l'onglet est a l'ecran. Relu au plus
    // quatre fois par seconde (tout de suite quand il vient d'apparaitre). Vrai : relu.
    bool tick(bool shown);
    void refresh();                                  // relire maintenant

    void setWindowSeconds(int seconds);              // 10, 60 ; 0 : depuis le lancement
    [[nodiscard]] int windowSeconds() const noexcept { return window_; }
    void setThresholdMs(int ms);                     // le seuil des saccades (core::perf)
    void resetAll();                                 // Remettre a zero
    [[nodiscard]] std::string reportText() const;    // Copier le rapport
    void copyReport();
    void setSearch(std::string text);                // ne garder que les parties dont le nom contient `text`
    [[nodiscard]] std::string statusText() const;    // la barre d'etat du panneau

    [[nodiscard]] ui::TableView& partsTable() noexcept { return *parts_; }
    [[nodiscard]] ui::TableView& stuttersTable() noexcept { return *stutters_; }
    [[nodiscard]] const core::perf::Window& shownWindow() const noexcept { return win_; }

    // Les zones cliquables du haut (les essais, les sessions rejouees) : faux si absente.
    enum Action : int {
        kWindow10 = 1, kWindow60 = 2, kWindowAll = 3,
        kThreshold33 = 11, kThreshold50 = 12, kThreshold100 = 13, kThreshold250 = 14,
        kReset = 21, kCopy = 22,
    };
    [[nodiscard]] bool actionRect(int action, gfx::Rect& out) const;
    [[nodiscard]] gfx::Rect chartRect() const noexcept { return chart_; }
    void trigger(int action);

    // Ce que l'onglet dit (le rapport copie, la remise a zero) : la barre d'etat du panneau.
    const core::SignalPtr<std::string> said = core::Signal<std::string>::create();

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool hasTooltip() const override { return true; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    struct Hit {
        int         action{0};
        std::string label;
        std::string tip;
        gfx::Rect   r{};
        bool        button{false};      // un bouton (sinon : une puce a choisir)
    };
    void layoutHeader(float x, float y, float w);
    void layoutLeft(const gfx::Rect& r);
    [[nodiscard]] bool active(int action) const noexcept;
    void refreshParts();
    void refreshStutters();
    void paintChart(const ui::PaintContext& ctx) const;
    void paintJobs(const ui::PaintContext& ctx) const;

    class PartsModel;
    class StuttersModel;

    ui::TableView*                   parts_{nullptr};
    ui::TableView*                   stutters_{nullptr};
    std::shared_ptr<PartsModel>      partsModel_;
    std::shared_ptr<StuttersModel>   stuttersModel_;
    std::vector<Hit>                 hits_;
    int                              hot_{0};            // l'action sous la souris
    int                              window_{60};
    std::string                      search_;
    // ---- ce qui a ete relu (refresh)
    core::perf::Window               win_;
    std::vector<core::perf::Second>  timeline_;
    core::perf::ProcessStats         proc_;
    std::vector<core::perf::JobInfo> running_, finished_;
    std::uint64_t                    stutterTotal_{0};
    std::uint64_t                    stutterSeen_{~std::uint64_t{0}};   // le journal montre : son compte, son seuil
    int                              thresholdSeen_{-1};
    std::string                      line1_, line2_, partsCaption_, stuttersCaption_;
    // ---- la mise en page
    float                            headerH_{34.f};
    gfx::Rect                        summary_{}, chart_{}, jobs_{}, partsHead_{}, stuttersHead_{};
    double                           lastRefresh_{-1.0};
    bool                             wasShown_{false};
};

} // namespace app
