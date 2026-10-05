// =============================================================================
//  app/GrafcetView.hpp - le grafcet dessine (1.10, chantier R : refait)
// -----------------------------------------------------------------------------
//  Le dessin vient de grafcet::buildDiagram (project/GrafcetDiagram.hpp), conforme
//  a l'IEC 60848 et sans chevauchement (verifie par le calcul) : la vue ne fait
//  plus que le peindre, au zoom choisi, et dire ce qu'on a clique.
//
//    etape        un carre numerote (initiale : double carre), son nom dessous
//                 s'il differe de X<numero> ;
//    transition   un trait epais sur la liaison, "T2" a gauche, la receptivite a
//                 droite (les raccourcis FIN(A2), ACTIF(X3)... lisibles) ;
//    OU / ET      un trait simple / deux traits paralleles ;
//    action       un rectangle accroche a droite de l'etape : la case du
//                 qualificatif (N, P1, D, L...), le nom, le genre en francais ;
//    renvoi       une fleche et le numero de l'etape visee.
//
//  DEUX ETATS, ET LA DIFFERENCE DOIT SE VOIR. Hors simulation, le dessin dit ce
//  que le programme ECRIT. En simulation, ce qu'il FAIT : les etapes actives
//  remplies en vert avec depuis combien de temps, une transition validee en
//  jaune, validee et vraie en vert vif, les actions en cours marquees. Les
//  valeurs sont lues dans le runtime a chaque image, par leur nom
//  (Steps_<prefixe>[i].Active / .ActiveTime, Trans_..[i].Condition,
//  Acts_..[i].Out) : rien n'est garde d'une image a l'autre.
//
//  LES CONTROLES (grafcet::runChecks) se voient a leur place : un cadre rouge
//  et la raison sous l'element.
// =============================================================================
#pragma once

#include "../project/Grafcet.hpp"
#include "../project/GrafcetCheck.hpp"
#include "../project/GrafcetDiagram.hpp"
#include "../project/GrafcetLayoutFile.hpp"
#include "../sim/Runtime.hpp"
#include "../ui/Widget.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace app {

    // Ce que fait le pointeur. La barre d'outils le choisit ; la vue ne revient
    // jamais d'elle-meme a Selection (un outil qui se defait seul apres un usage
    // est un outil qu'on combat).
    enum class GrafcetTool : std::uint8_t {
        Select,      // choisir, glisser une etape, glisser le fond pour se deplacer
        AddStep,
        AddTransition,
        AddAction,
        Wire,        // une transition puis une etape (ou l'inverse) : Relier
        Remove,
    };

    // Ce qui est choisi. Les Proprietes suivent.
    enum class GrafcetPart : std::uint8_t { None, Step, Transition, Action };

    class GrafcetView final : public ui::Widget {
    public:
        explicit GrafcetView(std::string id = {});

        void setChart(grafcet::Chart chart);
        [[nodiscard]] const grafcet::Chart& chart() const noexcept { return chart_; }

        // Emprunte, jamais possede, relu a chaque image. Nul : pas de simulation,
        // ce qui est un autre dessin et non un dessin vide.
        void setRuntime(sim::Runtime* runtime) noexcept { runtime_ = runtime; }

        // Les controles du grafcet, montres sur le dessin (cadre rouge + raison).
        void setChecks(std::vector<grafcet::Check> checks);

        void setZoom(float zoom);
        [[nodiscard]] float zoom() const noexcept { return zoom_; }
        // Tout le grafcet dans la fenetre (Ajuster).
        void fit();
        // Amene un element au milieu de la vue (recherche, liste, controle).
        void reveal(GrafcetPart part, int id);

        void setTool(GrafcetTool tool);
        [[nodiscard]] GrafcetTool tool() const noexcept { return tool_; }

        // Les positions choisies a la main (le fichier .xpglayout). Empruntees.
        void setPlacement(const project::ChartLayout* placement) noexcept;
        [[nodiscard]] const project::ChartLayout& pendingPlacement() const noexcept {
            return pending_;
        }

        const core::SignalPtr<int> stepSelected = core::Signal<int>::create();
        const core::SignalPtr<int> transitionSelected = core::Signal<int>::create();
        const core::SignalPtr<int> actionSelected = core::Signal<int>::create();
        const core::SignalPtr<>    nothingSelected = core::Signal<>::create();
        const core::SignalPtr<float> zoomChanged = core::Signal<float>::create();

        // Des intentions, pas des actions : la vue sait ce qu'on a clique, l'ecran
        // en fait des dialogues et des commandes annulables.
        const core::SignalPtr<GrafcetPart, int>       editRequested = core::Signal<GrafcetPart, int>::create();
        const core::SignalPtr<GrafcetPart, int>       removeRequested = core::Signal<GrafcetPart, int>::create();
        // l'outil, et l'element sous le clic (-1 : le vide)
        const core::SignalPtr<GrafcetTool, int>       insertRequested = core::Signal<GrafcetTool, int>::create();
        // transition, etape : Relier les a joints
        const core::SignalPtr<int, int>               linkRequested = core::Signal<int, int>::create();
        // une etape a ete glissee : l'ecran enregistre le fichier des positions
        const core::SignalPtr<>                       placementChanged = core::Signal<>::create();

        [[nodiscard]] int selectedStep() const noexcept { return selectedStep_; }
        [[nodiscard]] GrafcetPart selectedPart() const noexcept { return selectedPart_; }
        [[nodiscard]] int selectedId() const noexcept { return selectedId_; }
        void select(GrafcetPart part, int id);

        [[nodiscard]] int wireFromTransition() const noexcept { return wireTransition_; }
        [[nodiscard]] int wireFromStep() const noexcept { return wireStep_; }

        [[nodiscard]] ui::SizeHint sizeHint() const override;
        // L'infobulle : l'element sous la souris ; en simulation, la valeur des
        // variables d'une receptivite.
        [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
        [[nodiscard]] bool hasTooltip() const override { return !chart_.steps.empty(); }

        // ---- pour les essais : la geometrie de la derniere peinture ------------
        struct StepBox { int id{ -1 }; gfx::Rect rect; bool active{ false }; };
        [[nodiscard]] const std::vector<StepBox>& stepBoxes() const noexcept { return stepBoxes_; }
        struct BarBox { int id{ -1 }; gfx::Rect rect; };
        [[nodiscard]] const std::vector<BarBox>& transitionBars() const noexcept { return bars_; }
        [[nodiscard]] const std::vector<BarBox>& actionChips() const noexcept { return chips_; }
        [[nodiscard]] int stepAt(gfx::Point) const;
        [[nodiscard]] int transitionAt(gfx::Point) const;
        [[nodiscard]] int actionAt(gfx::Point) const;
        [[nodiscard]] bool live() const noexcept { return runtime_ != nullptr; }
        // Le dessin en coordonnees (zoom 1), tel que la derniere peinture l'a fait.
        [[nodiscard]] const grafcet::Diagram& diagram() const noexcept { return diagram_; }

        // ---- 1.10 (R) : les commandes du moteur, avec confirmation ------------
        // En simulation, une barre en haut a droite du dessin : Initialiser, Tout
        // reinitialiser, Etape precedente, Etape suivante, Forcer (l'etape choisie).
        // Un clic DEMANDE : la vue dit ce que le moteur va faire, et n'ecrit
        // Ctrl_<nom>.Cmd.* (ST_GC_CTRL) qu'apres Confirmer (Echap : rien).
        enum class EngineCommand : std::uint8_t { None, Init, ResetAll, ForceBack, ForceNext, ForceSituation };
        void askEngine(EngineCommand cmd);
        [[nodiscard]] EngineCommand pendingEngine() const noexcept { return pendingCmd_; }
        [[nodiscard]] std::string engineText(EngineCommand cmd) const;
        bool confirmEngine();
        void cancelEngine();

        // L'historique des franchissements vus en simulation (et des forcages),
        // le plus recent en dernier.
        struct Firing {
            std::string time;          // "21:42:07"
            int         transition{ -1 };
            std::string from, to;      // "X1", "X2, X3"
            std::string condition;     // la receptivite lisible, ou la commande forcee
            bool        forced{ false };
        };
        [[nodiscard]] const std::vector<Firing>& history() const noexcept { return history_; }
        const core::SignalPtr<> historyChanged = core::Signal<>::create();

        // ---- 1.10 (R) : chercher (Ctrl+F) ---------------------------------------
        // Une etape (X3, son nom), une transition (T2, un mot de sa receptivite :
        // une variable), une action (son nom). Entree / F3 : la suivante ; Echap.
        void startSearch();
        void setSearch(std::string text);
        [[nodiscard]] bool searching() const noexcept { return searching_; }
        [[nodiscard]] const std::string& searchText() const noexcept { return search_; }
        [[nodiscard]] std::size_t searchHitCount() const noexcept { return searchHits_.size(); }
        void nextSearchHit();

        // ---- 1.10 (R) : exporter le dessin en PNG ------------------------------
        // Tout le grafcet (Ajuster le temps d'une image), sans les barres par-dessus,
        // dans le dossier des captures : Grafcet_<nom>_<date>.png. Le bouton
        // "Exporter PNG" en bas a gauche du dessin.
        void exportPng();
        const core::SignalPtr<std::string, bool> exported = core::Signal<std::string, bool>::create();

        // L'etat en simulation, ou rien quand rien ne tourne.
        [[nodiscard]] std::optional<bool> stepActive(int id) const;
        [[nodiscard]] std::string         stepActiveTime(int id) const;   // "2,6 s"
        [[nodiscard]] std::optional<bool> transitionTrue(int id) const;
        [[nodiscard]] bool                transitionValidated(int id) const;
        [[nodiscard]] std::optional<bool> actionRunning(int id) const;

    protected:
        void            onPaint(const ui::PaintContext&) override;
        ui::EventResult onEvent(const ui::InputEvent&) override;

    private:
        void rebuild(const ui::PaintContext&);
        [[nodiscard]] gfx::Point toScreen(gfx::Point p) const;
        [[nodiscard]] gfx::Rect  toScreen(const gfx::Rect& r) const;
        [[nodiscard]] gfx::Point toChart(gfx::Point p) const;
        void clampScroll();
        [[nodiscard]] std::string arrayField(std::string_view array, int index,
            std::string_view field) const;
        [[nodiscard]] const grafcet::Check* checkOn(char part, int id) const;

        grafcet::Chart   chart_;
        grafcet::Diagram diagram_;
        bool             dirty_{ true };
        std::vector<grafcet::Check> checks_;
        sim::Runtime* runtime_{ nullptr };
        const project::ChartLayout* placement_{ nullptr };
        project::ChartLayout        pending_;

        GrafcetTool tool_{ GrafcetTool::Select };
        GrafcetPart selectedPart_{ GrafcetPart::None };
        int         selectedId_{ -1 };
        int         wireTransition_{ -1 }, wireStep_{ -1 };
        int         draggingStep_{ -1 };
        gfx::Point  dragOffset_{};
        bool        dragged_{ false };
        bool        panning_{ false };
        gfx::Point  panFrom_{};
        bool        fitPending_{ true };
        int         revealStep_{ -1 };

        float  zoom_{ 1.f };
        float  scrollX_{ 0.f }, scrollY_{ 0.f };
        int    selectedStep_{ -1 };
        int    hoveredStep_{ -1 }, hoveredTransition_{ -1 };

        void trackHistory();
        bool                 exportPending_{ false };
        gfx::Rect            exportButton_{};
        float                topInset_{ 0.f };   // en simulation : la place de la barre du moteur
        bool                 searching_{ false };
        std::string          search_;
        std::vector<std::pair<GrafcetPart, int>> searchHits_;
        std::size_t          searchIndex_{ 0 };
        gfx::Rect            miniMap_{};      // la mini-carte (vide : pas montree)
        float                miniScale_{ 1.f };
        EngineCommand pendingCmd_{ EngineCommand::None };
        int           forceTarget_{ -1 };
        std::vector<std::pair<EngineCommand, gfx::Rect>> engineButtons_;
        gfx::Rect     confirmRect_{}, cancelRect_{};
        std::vector<int>    lastActive_;
        bool                haveLast_{ false };
        std::vector<Firing> history_;

        std::vector<StepBox> stepBoxes_;
        std::vector<BarBox>  bars_;
        std::vector<BarBox>  chips_;
    };

} // namespace app
