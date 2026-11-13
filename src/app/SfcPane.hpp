// =============================================================================
//  app/SfcPane.hpp - 1.51.0 (S) : l'editeur d'un GRAFCET natif (section SFC)
// -----------------------------------------------------------------------------
//  Le volet "Sch\xC3\xA9ma" d'une section SFC (le volet "Texte" garde la lecture en
//  texte, project/GraphListing) :
//
//    en haut     la barre : + Etape (etape et transition sous la selection),
//                Divergence OU, Divergence ET, Saut, Receptivite, Action, Etape...
//                (nom, initiale, supervision, commentaire), Supprimer, le zoom ;
//    au centre   le dessin (app/GrafcetView, la grille de Control Expert :
//                project/SfcDiagram) puis le volet Code : la receptivite de la
//                transition choisie (variable ou section ST, modifiable ; LD lue),
//                la section d'action de l'etape ou de l'action choisie ;
//    a droite    les Proprietes de la selection ;
//    en bas      ce que contient le graphe, ses problemes, en simulation les
//                etapes actives.
//
//  Chaque geste ecrit le XML de la section (sfc::write) par UNE commande
//  (project::SfcEditCommand) : Ctrl+Z le defait. En simulation, le dessin lit le
//  runtime a chaque image (sim::Runtime::sfcChart) : etapes actives et leur temps,
//  transitions validees / vraies, actions en cours, supervision en defaut ; la
//  barre du dessin initialise le GRAFCET ou force l'etape choisie.
// =============================================================================
#pragma once

#include "../project/SfcDiagram.hpp"
#include "../project/SfcEdit.hpp"
#include "../ui/Widget.hpp"
#include "GrafcetView.hpp"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ui { class ToolBar; class StatusBar; class TableView; class Splitter; }
namespace sim { struct SfcChartRun; class Runtime; }

namespace app {

    class App;
    class GrafcetCodePanel;
    class GrafcetRowsModel;

    class SfcPane final : public ui::Widget {
    public:
        SfcPane(App& app, domain::Index section, std::string paneId);
        ~SfcPane() override;

        // Relit la section (apres Ctrl+Z, une modification ailleurs) ; garde la selection si elle existe encore.
        void reload();

        [[nodiscard]] GrafcetView& view() const { return *view_; }
        [[nodiscard]] const sfc::Chart& chart() const noexcept { return chart_; }
        [[nodiscard]] const sfc::Topology& topology() const noexcept { return topo_; }
        [[nodiscard]] domain::Index section() const noexcept { return section_; }

        // ---- les gestes (la barre d'outils, et les scripts) ------------------------
        void command(const std::string& id);
        bool selectStep(std::string_view name);
        bool selectAt(int x, int y);                         // l'objet de la case (x, y)
        bool selectItem(int item);
        bool setConditionOfSelection(const std::string& text);
        bool addStepBelowSelection(const std::string& condition = {});
        bool addActionToSelection(const std::string& qualifier, const std::string& name, bool isSection,
                                  const std::string& time);
        bool removeSelection();
        // L'etat pour un script (sfc-etat) : selection, etapes actives, problemes.
        [[nodiscard]] std::string describe() const;
        [[nodiscard]] const std::string& lastMessage() const noexcept { return lastMessage_; }

        // Le corps de la section a change (par cet editeur) : le volet Texte suit.
        const core::SignalPtr<> edited = core::Signal<>::create();

    protected:
        void onLayout() override;
        void onPaint(const ui::PaintContext&) override;

    private:
        bool apply(const sfc::EditResult& r, const std::string& label);
        void rebuild();
        void showSelection();
        void editCondition(int item);
        void editStep(int item);
        void editAction(int step, int rank);
        void askJump(int item);
        void removePart(GrafcetPart part, int id);
        void onDrop();
        void applyCode();
        void say(const std::string& text, bool error = false);
        [[nodiscard]] std::string newStepName() const;
        [[nodiscard]] std::set<std::string> takenNames() const;
        [[nodiscard]] std::string runtimeSection() const;
        [[nodiscard]] sim::Runtime* runtime() const;
        [[nodiscard]] const sim::SfcChartRun* liveChart() const;
        [[nodiscard]] std::string freeTransitionSection() const;

        App&                  app_;
        domain::Index         section_;
        std::shared_ptr<bool> alive_;
        sfc::Chart            chart_;
        sfc::Topology         topo_;
        sfc::Grid             grid_;
        std::string           body_;          // le corps lu (pour savoir s'il a change)
        std::string           lastMessage_;
        std::string           statusLine_;
        std::size_t           caretLine_{ 0 };   // le curseur du volet Texte (Aller a)
        bool                  caretSeen_{ false };

        ui::ToolBar*          bar_{ nullptr };
        GrafcetView*          view_{ nullptr };
        GrafcetCodePanel*     code_{ nullptr };
        ui::TableView*        props_{ nullptr };
        ui::StatusBar*        status_{ nullptr };
        std::shared_ptr<GrafcetRowsModel> propsModel_;
        core::ConnectionScope links_;
    };

    // 1.51.0 (S) : les commandes sfc-... du mode --script (ScriptRunner) ; faux : `error` dit pourquoi.
    bool runSfcScript(App& app, SfcPane& pane, const std::vector<std::string>& words, std::string& said, std::string& error);

} // namespace app
