// =============================================================================
//  app/screens/MainAnalysisScreen.cpp - the workspace
// -----------------------------------------------------------------------------
//  Three things here are worth calling out:
//
//  * SOURCE PREVIEW. Selecting a section in the explorer opens it as a closable
//    tab in the centre pane, next to the configuration tabs. Several sections
//    stay open at once and keep their scroll position, which is what makes
//    comparing two pieces of logic possible at all. Previously, clicking a
//    section did nothing visible - the body was parsed and then never shown.
//
//  * VIEW TAB. Every dockable panel has a checkbox. Toggling one collapses the
//    corresponding splitter pane rather than hiding a widget, so the space is
//    reclaimed by its neighbours instead of leaving a hole.
//
//  * PERSISTENCE. Panel visibility and splitter ratios are written to the
//    settings file on every change and on leaving the screen, then restored on
//    entry. A workspace the user arranged should survive closing the program.
// =============================================================================
#include "Screens.hpp"
#include "../../core/Edition.hpp"   // 1.12.0 : l'ecran de chaque application
#include "../../project/CodeIconKeys.hpp"   // 1.8.0 : l'icone des onglets de section
#include "LibraryHelpScreen.hpp"   // lot macros 1 : helpMenuFor
#include "../../help/F1Table.hpp"   // 1.11 (T2, tranche 16) : F1 ouvre le centre, au sujet de l'onglet
#include "../hmi/HmiTutorial.hpp"

#include "../App.hpp"
#include "../ApiTrails.hpp"                // Lot API 8 : didacticiels et aide (F1 sur un onglet du dossier Simulation)
#include "../ExecutionOrderWiring.hpp"
#include "../GrafcetPanels.hpp"
#include "../GrafcetCodePanel.hpp"              // 1.10 (R2) : le volet Code sous le dessin
#include "../../project/GrafcetCheck.hpp"
#include "../../project/GrafcetRewrite2.hpp"    // 1.10 (R2) : supprimer, modifier, l'apercu
#include "../../help/HelpCodes.hpp"
#include "../../help/HelpIndex.hpp"
#include "../../help/HelpSession.hpp"
#include "../../project/CrossReference.hpp"
#include "../../project/GrafcetEdit.hpp"
#include "../../project/GrafcetLayoutFile.hpp"
#include "../../project/GrafcetRewrite.hpp"
#include "../GrafcetView.hpp"
#include "../HistoryPanel.hpp"
#include "../../project/DeleteCommands.hpp"
#include "../../project/Grafcet.hpp"
#include "../../project/EditCommands.hpp"
#include "../../project/SharedLibrary.hpp"
#include "../../project/MacroFolders.hpp"
#include "../../project/MacroSpec.hpp"
#include "../MacroEditorView.hpp"
#include "../MacrosPane.hpp"
#include "../ApiPanes.hpp"      // lot API 2 : les volets de l'API
#include "../TopBar.hpp"        // lot API 2 : la barre du haut
#include "../SimulationPane.hpp"   // lot API 7 : frenchMessage (Simuler qui ne demarre pas)
#include "../hmi/HmiPanels.hpp"
#include "../hmi/HmiApiVarsPane.hpp"  // 1.11.1 (API-V) : une variable de l'automate glissee dans un script
#include "../hmi/HmiSimulation.hpp"   // 1.10 : la pastille IHM de la barre du haut
#include "../hmi/HmiBuildPanes.hpp"   // 1.11.14 : le panneau du bas (Sorties, Console, Diagnostics)

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>

namespace app {

using namespace ui;

namespace {

// Forces a height on whatever it wraps, because a BoxLayout gives each child the
// height it asks for and some widgets ask for the height they would have
// somewhere else.
class FixedHeight final : public Widget {
public:
    FixedHeight(std::string id, WidgetPtr child, float height)
        : Widget(std::move(id)), height_(height) { addChild(std::move(child)); }
    [[nodiscard]] SizeHint sizeHint() const override {
        SizeHint h;
        h.preferred = {0.f, height_};
        h.minimum   = {0.f, height_};
        h.stretchX  = 1.f;
        return h;
    }
protected:
    void onLayout() override {
        if (!children().empty()) children()[0]->setBounds(contentRect());
    }
private:
    float height_;
};

// Keys are grouped by prefix so the settings file reads as a structure.
constexpr const char* kViewExplorer    = "view.projectExplorer";
constexpr const char* kViewDocuments   = "view.openDocuments";
constexpr const char* kViewCentre      = "view.plcConfiguration";
constexpr const char* kViewBottomRow   = "view.bottomRow";
constexpr const char* kViewDiagnostics = "view.diagnostics";
constexpr const char* kViewSummary     = "view.analysisSummary";
constexpr const char* kViewStatus      = "view.projectStatus";
constexpr const char* kViewOutputPanel = "view.outputPanel";        // 1.11.14 : le panneau du bas

constexpr const char* kLayoutOuter  = "layout.outer";
constexpr const char* kLayoutUpper  = "layout.upper";
constexpr const char* kLayoutMiddle = "layout.middle";
constexpr const char* kLayoutModeKey = "view.layoutMode";   // lot 7 : onglets, groupes, mosaique
constexpr const char* kLayoutBottom = "layout.bottom";
constexpr const char* kLayoutLeft   = "layout.left";
constexpr const char* kLayoutCentre = "layout.centreColumn";        // 1.11.14 : le centre / le panneau du bas
constexpr std::size_t kLayoutCount  = 6;
const char* layoutKey(std::size_t i) {
    return i == 0 ? kLayoutOuter : i == 1 ? kLayoutUpper : i == 2 ? kLayoutMiddle : i == 3 ? kLayoutBottom : i == 4 ? kLayoutLeft : kLayoutCentre;
}

// One open source file: a strip of per-document controls above the viewer.
// Language, folding and zoom belong to the document, not to the application -
// two sections open side by side can legitimately want different settings.
class DocumentPane final : public BoxLayout {
public:
    DocumentPane(std::string id, Language language)
        : BoxLayout(Orientation::Vertical, std::move(id)) {
        setSpacing(2.f);

        auto bar = std::make_unique<BoxLayout>(Orientation::Horizontal, this->id() + ".bar");
        bar->setSpacing(6.f);

        // Edit comes first: it is the control that changes what the pane is for,
        // and the one that was being pushed off the end of the row.
        auto edit = std::make_unique<ToggleButton>("Edit");
        edit->setTooltip("Makes this section editable. Ctrl+Space suggests names; "
                         "typing two letters does the same.");
        editToggle_ = &static_cast<ToggleButton&>(bar->addChild(std::move(edit)));

        auto lang = std::make_unique<DropDown>(this->id() + ".lang");
        // Short labels: the long ones alone were 280 px of a 780 px pane.
        lang->setItems({{"ST", "st"}, {"IL", "il"}, {"C", "c"}, {"C++", "cpp"}, {"Text", "text"}});
        lang->setTooltip("Highlighting rules for this document only");
        language_ = &static_cast<DropDown&>(bar->addChild(std::move(lang)));

        auto foldAll = std::make_unique<Button>("Fold all");
        foldAll->setStyle(Button::Style::Flat);
        foldAll->setIcon(Icon::Collapse);
        foldAll->setCompact(true);
        foldAll->setTooltip("Collapse every indented block");
        foldAllButton_ = &static_cast<Button&>(bar->addChild(std::move(foldAll)));

        auto unfoldAll = std::make_unique<Button>("Unfold");
        unfoldAll->setStyle(Button::Style::Flat);
        unfoldAll->setIcon(Icon::Expand);
        unfoldAll->setCompact(true);
        unfoldAll->setTooltip("Expand every block");
        unfoldAllButton_ = &static_cast<Button&>(bar->addChild(std::move(unfoldAll)));

        auto depth = std::make_unique<DropDown>(this->id() + ".depth");
        depth->setItems({{"All", "-1"}, {"L1", "1"}, {"L2", "2"}, {"L3", "3"}, {"L4", "4"}});
        depth->setSelectedIndex(0);
        depth->setTooltip("Collapse everything nested deeper than this level");
        depth_ = &static_cast<DropDown&>(bar->addChild(std::move(depth)));

        auto zoomOut = std::make_unique<Button>("-");
        zoomOut->setStyle(Button::Style::Flat);
        zoomOutButton_ = &static_cast<Button&>(bar->addChild(std::move(zoomOut)));
        auto zoomIn = std::make_unique<Button>("+");
        zoomIn->setStyle(Button::Style::Flat);
        zoomIn->setTooltip("Ctrl + wheel does the same");
        zoomInButton_ = &static_cast<Button&>(bar->addChild(std::move(zoomIn)));

        auto copy = std::make_unique<Button>("Copy");
        copy->setStyle(Button::Style::Flat);
        copy->setIcon(Icon::Document);
        copy->setCompact(true);
        copy->setTooltip("Copies the selection, or the whole document when nothing is selected "
                         "(Ctrl+A, Ctrl+C)");
        copyButton_ = &static_cast<Button&>(bar->addChild(std::move(copy)));

        bar->setVisibility(Visibility::Visible);
        addChild(std::move(bar));

        auto view = std::make_unique<MultiLineText>(this->id() + ".code");
        view->setReadOnly(true);
        view->setShowLineNumbers(true);
        view->setLanguage(language);
        // 1.12.2 : les raccourcis de Visual Studio (lecture : chercher, aller a la ligne, F12 ;
        // Edit : commenter en (* *), dupliquer, deplacer des lignes...).
        view->setCommandKeys(true);
        view->setCommentStyle(MultiLineText::CommentStyle::ParenStar);
        view_ = &static_cast<MultiLineText&>(addChild(std::move(view)));

        // The bar under the code. Everything about the name being read, on one
        // line: what it is, where it lives, how it was reached. A reader who has
        // to leave the section to find out what stnew1.test2 is has lost the
        // thread of what they were reading.
        // StatusBar rather than a third copy of the private Label that
        // Screens.cpp and Dialogs.cpp each define: it is already a themed strip
        // with a message and an accent colour, which is exactly this.
        auto info = std::make_unique<StatusBar>(this->id() + ".symbol");
        info->setTooltip("The variable under the caret. Double-click a name, or "
                         "just put the caret in it.");
        symbolBar_ = &static_cast<StatusBar&>(addChild(std::move(info)));

        language_->setSelectedIndex(indexForLanguage(language));

        links_ += language_->selectionChanged->connect([this](int) {
            const auto* item = language_->selectedItem();
            if (!item) return;
            view_->setLanguage(item->value == "st"  ? Language::StructuredText
                             : item->value == "il"  ? Language::InstructionList
                             : item->value == "c"   ? Language::C
                             : item->value == "cpp" ? Language::Cpp
                                                    : Language::PlainText);
        });
        links_ += foldAllButton_->clicked->connect([this] { view_->foldAll(); });
        links_ += unfoldAllButton_->clicked->connect([this] {
            view_->unfoldAll();
            depth_->setSelectedIndex(0);
        });
        links_ += depth_->selectionChanged->connect([this](int i) {
            view_->foldToDepth(i == 0 ? -1 : i);
        });
        links_ += zoomInButton_->clicked->connect([this] { view_->setZoom(view_->zoom() + 0.25f); });
        links_ += zoomOutButton_->clicked->connect([this] { view_->setZoom(view_->zoom() - 0.25f); });
        links_ += copyButton_->clicked->connect([this] { view_->copySelection(); });
        links_ += editToggle_->toggled->connect([this](bool on) {
            view_->setReadOnly(!on);
            if (on) view_->takeFocus();
        });
    }

    [[nodiscard]] MultiLineText& view() const { return *view_; }
    [[nodiscard]] ToggleButton&  editToggle() const { return *editToggle_; }

    // Filled by the screen, which is the only thing that knows the project.
    void setSymbolLine(const std::string& text, gfx::Color accent = {}) {
        if (symbolBar_) symbolBar_->setMessage(text, accent);
    }

protected:
    void onLayout() override {
        // Fixed strip on top, fixed strip at the bottom, the viewer takes the
        // rest. The bottom strip keeps its height whether or not it has anything
        // to say: a bar that appears and disappears makes the code jump under
        // the reader's eyes every time the caret moves.
        const auto area = contentRect();
        const float barH  = 30.f;
        const float infoH = 22.f;
        if (children().size() < 3) return;
        children()[0]->setBounds({area.x, area.y, area.w, barH});
        children()[1]->setBounds({area.x, area.y + barH + 2.f, area.w,
                                  std::max(0.f, area.h - barH - infoH - 6.f)});
        children()[2]->setBounds({area.x, area.bottom() - infoH, area.w, infoH});
    }

private:
    static int indexForLanguage(Language l) {
        switch (l) {
            case Language::StructuredText:  return 0;
            case Language::InstructionList: return 1;
            case Language::C:               return 2;
            case Language::Cpp:             return 3;
            default:                        return 4;
        }
    }

    DropDown*             language_{nullptr};
    DropDown*             depth_{nullptr};
    Button*               foldAllButton_{nullptr};
    Button*               unfoldAllButton_{nullptr};
    Button*               zoomInButton_{nullptr};
    Button*               zoomOutButton_{nullptr};
    Button*               copyButton_{nullptr};
    ToggleButton*         editToggle_{nullptr};
    MultiLineText*        view_{nullptr};
    StatusBar*            symbolBar_{nullptr};
    core::ConnectionScope links_;
};


// ---------------------------------------------------------------------------
//  One chart, laid out: the drawing on the left, the numbers on the right, the
//  totals underneath.
//
//  The drawing and the tables are two readings of one thing, so they share one
//  runtime pointer by address. Giving each of them its own copy is how a table
//  ends up showing a live chart while the drawing shows a dead one.
// ---------------------------------------------------------------------------
// A two-column table of plain strings. There is no such model in ViewModels
// because nothing needed one until now: everything else there is a view onto the
// project, and this is a view onto whatever happens to be selected.
class StringTableModel final : public ITableModel {
public:
    void setRows(std::vector<std::vector<std::string>> rows) {
        rows_ = std::move(rows);
        modelReset->emit();
    }
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 2; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        return c == 0 ? "Field" : "Value";
    }
    [[nodiscard]] std::string cellText(RowIndex row, std::size_t c) const override {
        if (row >= rows_.size() || c >= rows_[row].size()) return {};
        return rows_[row][c];
    }
    // The order is the order it was given in, which is the order a reader needs:
    // what it is, then its name, then the rest. Sorting it alphabetically would
    // be sorting a sentence.
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override {
        return a < b;
    }
private:
    std::vector<std::vector<std::string>> rows_;
};

class GrafcetPane final : public Widget {
public:
    // 1.10, chantier R : l'editeur refait. A gauche les grafcets du programme
    // (les instances de DFB_GRAFCETENGINE), au centre le dessin, a droite les
    // Proprietes de la selection puis les onglets Etapes / Transitions /
    // Actions / Controles. Tout en francais.
    GrafcetPane(std::string id, grafcet::Chart chart,
                std::vector<GrafcetInstanceModel::Entry> instances)
        : Widget(std::move(id)), chart_(std::move(chart)) {

        auto palette = std::make_unique<ToolBar>(this->id() + ".palette");
        auto tool = [&](const char* text, const char* action, Icon icon, const char* why) {
            palette->addButton(text, action, icon).setTooltip(why);
        };
        tool("S\xC3\xA9lection", "gc.select", Icon::Search,
             "Choisir un \xC3\xA9l\xC3\xA9ment, glisser une \xC3\xA9tape pour la d\xC3\xA9placer, glisser le fond pour se d\xC3\xA9placer");
        palette->addSeparator();
        tool("\xC3\x89tape", "gc.addStep", Icon::Section,
             "Clique une \xC3\xA9tape pour ins\xC3\xA9rer avant elle, le vide pour l'ajouter \xC3\xA0 la fin");
        tool("Transition", "gc.addTransition", Icon::StepOnce,
             "Une transition arrive avec sa r\xC3\xA9" "ceptivit\xC3\xA9 : sans elle, elle ne franchirait jamais");
        tool("Action", "gc.addAction", Icon::Play,
             "Clique l'\xC3\xA9tape qui la porte ; elle va dans SFC_<grafcet>_Actions");
        palette->addSeparator();
        tool("Relier", "gc.wire", Icon::Force,
             "Clique une transition et une \xC3\xA9tape, dans un ordre ou dans l'autre. Deux \xC3\xA9tapes "
             "ne se relient jamais directement : il y a toujours une transition entre elles");
        tool("Supprimer", "gc.remove", Icon::Close, "Clique ce qui doit partir. On te le demande d'abord");
        palette->addSeparator();
        tool("\xE2\x88\x92", "gc.zoomOut", Icon::None, "Zoom arri\xC3\xA8re (Ctrl+molette)");
        tool("+", "gc.zoomIn", Icon::None, "Zoom avant (Ctrl+molette)");
        tool("Ajuster", "gc.fit", Icon::None, "Tout le grafcet dans la fen\xC3\xAAtre");
        palette_ = &static_cast<ToolBar&>(addChild(std::move(palette)));

        auto split = std::make_unique<Splitter>(Orientation::Horizontal, this->id() + ".split");

        // ---- le navigateur des instances ----------------------------------------
        instanceModel_ = std::make_shared<GrafcetInstanceModel>(std::move(instances), &runtime_);
        {
            auto box = std::make_unique<BoxLayout>(Orientation::Vertical, this->id() + ".nav");
            box->setSpacing(2.f);
            box->setAlignment(Align::Stretch);
            auto heading = std::make_unique<StatusBar>(this->id() + ".nav.title");
            heading->setMessage("Grafcets du programme (" + std::to_string(instanceModel_->rowCount()) + ")");
            heading->setTooltip(std::to_string(instanceModel_->rowCount())
                                + " instances de DFB_GRAFCETENGINE dans le programme ; un clic ouvre le grafcet");
            box->addChild(std::make_unique<FixedHeight>(this->id() + ".nav.cap", std::move(heading), 18.f));
            auto table = std::make_unique<TableView>(this->id() + ".instances");
            instances_ = &static_cast<TableView&>(box->addChild(std::move(table)));
            auto foot = std::make_unique<StatusBar>(this->id() + ".nav.total");
            foot->setMessage("en tout : " + std::to_string(instanceModel_->totalSteps()) + " \xC3\xA9t. \xC2\xB7 "
                             + std::to_string(instanceModel_->totalTransitions()) + " tr. \xC2\xB7 "
                             + std::to_string(instanceModel_->totalActions()) + " act.");
            box->addChild(std::make_unique<FixedHeight>(this->id() + ".nav.foot", std::move(foot), 18.f));
            split->addPane(std::move(box), 0.2f, 236.f);
        }
        instances_->setModel(instanceModel_);
        instances_->setColumns({{"Grafcet", 100.f}, {"\xC3\xA9t.\xC2\xB7tr.\xC2\xB7" "act.", 74.f},
                                {"\xC3\x89tat", 60.f}});
        if (const int row = instanceModel_->rowOf(chart_.instance); row >= 0)
            instances_->selectModelRows({static_cast<RowIndex>(row)}, /*notify=*/false);

        auto drawing = std::make_unique<GrafcetView>(this->id() + ".view");
        drawing->setChart(chart_);
        // 1.10 (R2, decision 11 bis) : le dessin en haut, le volet Code dessous
        // (receptivite / corps de l'element choisi, l'editeur des sections ST).
        auto centre = std::make_unique<Splitter>(Orientation::Vertical, this->id() + ".centre");
        view_ = &static_cast<GrafcetView&>(centre->addPane(std::move(drawing), 0.68f, 160.f));
        code_ = &static_cast<GrafcetCodePanel&>(
            centre->addPane(std::make_unique<GrafcetCodePanel>(this->id() + ".code"), 0.32f, 110.f));
        split->addPane(std::move(centre), 0.55f, 280.f);

        // ---- a droite : Proprietes, puis les onglets ------------------------------
        auto side = std::make_unique<Splitter>(Orientation::Vertical, this->id() + ".side");
        properties_ = addTable(*side, "Propri\xC3\xA9t\xC3\xA9s", "la s\xC3\xA9lection", 0.38f);
        auto tabs = std::make_unique<TabControl>(this->id() + ".tabs");
        tabs_ = tabs.get();
        steps_       = addTab(*tabs, "\xC3\x89tapes");
        transitions_ = addTab(*tabs, "Transitions");
        actions_     = addTab(*tabs, "Actions");
        checksTable_ = addTab(*tabs, "Contr\xC3\xB4les");
        historyTable_ = addTab(*tabs, "Historique");
        side->addPane(std::move(tabs), 0.62f, 120.f);
        split->addPane(std::move(side), 0.28f, 260.f);
        addChild(std::move(split));

        auto strip = std::make_unique<StatusBar>(this->id() + ".summary");
        summary_ = &static_cast<StatusBar&>(addChild(std::move(strip)));

        stepModel_       = std::make_shared<GrafcetStepModel>(chart_, &runtime_);
        transitionModel_ = std::make_shared<GrafcetTransitionModel>(chart_, &runtime_);
        actionModel_     = std::make_shared<GrafcetActionModel>(chart_, &runtime_);

        steps_->setModel(stepModel_);
        steps_->setColumns({{"N\xC2\xB0", 46.f}, {"Nom", 150.f}, {"\xC3\x89tat", 70.f}, {"Active depuis", 90.f}});
        transitions_->setModel(transitionModel_);
        transitions_->setColumns({{"N\xC2\xB0", 46.f}, {"R\xC3\xA9" "ceptivit\xC3\xA9", 170.f}, {"\xC3\x89tat", 70.f},
                                  {"De", 60.f}, {"Vers", 60.f}});
        actions_->setModel(actionModel_);
        actions_->setColumns({{"N\xC2\xB0", 46.f}, {"Nom", 90.f}, {"Genre", 230.f},
                              {"\xC3\x89tape", 56.f}, {"\xC3\x89tat", 70.f}, {"\xC3\x89" "coul\xC3\xA9", 80.f}});
        checkModel_ = std::make_shared<GrafcetRowsModel>(std::vector<std::string>{
            "Contr\xC3\xB4le", "O\xC3\xB9", "D\xC3\xA9tail"});
        checksTable_->setModel(checkModel_);
        checksTable_->setColumns({{"Contr\xC3\xB4le", 170.f}, {"O\xC3\xB9", 50.f}, {"D\xC3\xA9tail", 420.f}});

        // L'historique des franchissements vus en simulation, et des forcages (le
        // plus recent en haut) : la vue le tient, la table le montre.
        historyModel_ = std::make_shared<GrafcetRowsModel>(std::vector<std::string>{
            "Heure", "Tr.", "De \xE2\x86\x92 vers", "R\xC3\xA9" "ceptivit\xC3\xA9"});
        historyTable_->setModel(historyModel_);
        historyTable_->setColumns({{"Heure", 70.f}, {"Tr.", 46.f}, {"De \xE2\x86\x92 vers", 110.f},
                                   {"R\xC3\xA9" "ceptivit\xC3\xA9", 320.f}});
        historyModel_->setRows({{"", "", "", "Les franchissements s'inscrivent ici quand l'API tourne."}});

        propertyModel_ = std::make_shared<GrafcetRowsModel>(std::vector<std::string>{"Champ", "Valeur"});
        properties_->setModel(propertyModel_);
        properties_->setColumns({{"Champ", 130.f}, {"Valeur", 320.f}});

        links_ += view_->stepSelected->connect(
            [this](int picked) { showProperties(GrafcetPart::Step, picked); });
        links_ += view_->transitionSelected->connect(
            [this](int picked) { showProperties(GrafcetPart::Transition, picked); });
        links_ += view_->actionSelected->connect(
            [this](int picked) { showProperties(GrafcetPart::Action, picked); });
        links_ += view_->nothingSelected->connect(
            [this] { showProperties(GrafcetPart::None, -1); });
        // l'export PNG du dessin : ou il est parti
        links_ += view_->exported->connect([this](const std::string& path, bool ok) {
            summary_->setTransientMessage(ok ? "Dessin export\xC3\xA9 : " + path
                                             : "L'export PNG n'a pas pu s'\xC3\xA9" "crire : " + path, 8.0);
        });
        links_ += view_->historyChanged->connect([this] {
            std::vector<std::vector<std::string>> rows;
            std::vector<gfx::Color> colors;
            const auto& h = view_->history();
            for (auto it = h.rbegin(); it != h.rend(); ++it) {
                rows.push_back({it->time, it->transition >= 0 ? "T" + std::to_string(it->transition) : std::string("-"),
                                it->from + " \xE2\x86\x92 " + it->to, it->condition});
                colors.push_back(it->forced ? gfx::Color::rgb(0xE8C46F) : gfx::Color{});
            }
            historyModel_->setRows(std::move(rows), std::move(colors));
            if (tabs_) tabs_->setTabTitle(4, "Historique (" + std::to_string(h.size()) + ")");
        });

        // Un clic dans une liste choisit l'element dans le dessin, et l'y montre.
        links_ += steps_->selectionChanged->connect(
            [this](const std::vector<RowIndex>& rows) {
                if (!rows.empty() && rows.front() < chart_.steps.size())
                    pick(GrafcetPart::Step, chart_.steps[rows.front()].id);
            });
        links_ += transitions_->selectionChanged->connect(
            [this](const std::vector<RowIndex>& rows) {
                if (!rows.empty() && rows.front() < chart_.transitions.size())
                    pick(GrafcetPart::Transition, chart_.transitions[rows.front()].id);
            });
        links_ += actions_->selectionChanged->connect(
            [this](const std::vector<RowIndex>& rows) {
                if (!rows.empty() && rows.front() < chart_.actions.size())
                    pick(GrafcetPart::Action, chart_.actions[rows.front()].id);
            });
        links_ += checksTable_->selectionChanged->connect(
            [this](const std::vector<RowIndex>& rows) {
                if (rows.empty() || rows.front() >= checks_.size()) return;
                const auto& c = checks_[rows.front()];
                if (c.part == 'X') pick(GrafcetPart::Step, c.id);
                else if (c.part == 'T') pick(GrafcetPart::Transition, c.id);
                else if (c.part == 'A') pick(GrafcetPart::Action, c.id);
            });
        // Un autre grafcet du navigateur : l'ecran ouvre son onglet.
        links_ += instances_->selectionChanged->connect(
            [this](const std::vector<RowIndex>& rows) {
                if (rows.empty()) return;
                const auto* e = instanceModel_->entry(rows.front());
                if (e && e->chart.instance != chart_.instance) openRequested->emit(e->chart.instance);
            });

        links_ += steps_->activated->connect([this](RowIndex row) {
            if (row < chart_.steps.size()) editRequested->emit(GrafcetPart::Step,
                                                               chart_.steps[row].id);
        });
        links_ += transitions_->activated->connect([this](RowIndex row) {
            if (row < chart_.transitions.size())
                editRequested->emit(GrafcetPart::Transition, chart_.transitions[row].id);
        });
        links_ += actions_->activated->connect([this](RowIndex row) {
            if (row < chart_.actions.size())
                editRequested->emit(GrafcetPart::Action, chart_.actions[row].id);
        });
        links_ += view_->editRequested->connect(
            [this](GrafcetPart part, int picked) { editRequested->emit(part, picked); });
        links_ += properties_->activated->connect([this](RowIndex row) {
            if (row < propertyLinks_.size() && !propertyLinks_[row].empty() && propertyLinks_[row] != chart_.instance)
                openRequested->emit(propertyLinks_[row]);
        });

        showProperties(GrafcetPart::None, -1);
        refresh();
    }

    void setRuntime(sim::Runtime* runtime) {
        runtime_ = runtime;
        view_->setRuntime(runtime);
    }

    // Les controles : sur le dessin, dans l'onglet Controles (son titre compte
    // ceux en defaut), dans la barre du bas.
    void setChecks(std::vector<grafcet::Check> checks) {
        checks_ = std::move(checks);
        view_->setChecks(checks_);
        std::vector<std::vector<std::string>> rows;
        std::vector<gfx::Color> colors;
        severe_ = 0;
        for (const auto& c : checks_) {
            const std::string where = c.part ? std::string(1, c.part) + std::to_string(c.id) : std::string("-");
            rows.push_back({std::string(grafcet::checkTitle(c.kind)), where, c.message});
            colors.push_back(c.severe ? gfx::Color::rgb(0xE06C6C) : gfx::Color::rgb(0xE8C46F));
            if (c.severe) ++severe_;
        }
        if (rows.empty()) {
            rows.push_back({"Aucun d\xC3\xA9" "faut", "-", "\xC3\xA9tape initiale, sorties, liaisons, raccourcis et variables : tout est bon"});
            colors.push_back(gfx::Color::rgb(0x7FD08A));
        }
        checkModel_->setRows(std::move(rows), std::move(colors));
        if (tabs_) tabs_->setTabTitle(3, severe_ ? "Contr\xC3\xB4les (" + std::to_string(severe_) + ")"
                                                 : std::string("Contr\xC3\xB4les"));
        refresh();
    }

    // Tout relit le runtime : rien n'a besoin d'y etre pousse.
    void refresh() {
        std::string line = executionSummary(runtime_, chart_);
        if (severe_) line += "  \xC2\xB7  " + std::to_string(severe_) + " contr\xC3\xB4le"
                           + (severe_ > 1 ? "s" : "") + " en d\xC3\xA9" "faut";
        summary_->setMessage(line, engineFaults(runtime_, chart_).empty() && !severe_
                                 ? gfx::Color{} : gfx::Color::rgb(0xE06C6C));
        steps_->invalidate();
        transitions_->invalidate();
        actions_->invalidate();
        instances_->invalidate();
        view_->invalidate();
        invalidate();
    }

    [[nodiscard]] GrafcetView& view() const { return *view_; }
    [[nodiscard]] ToolBar& palette() const { return *palette_; }
    [[nodiscard]] GrafcetCodePanel& codePanel() const { return *code_; }   // 1.10 (R2)

    // Relu dans le projet apres une modification, le lecteur garde sa place.
    void setChart(grafcet::Chart chart) {
        const auto part = view_->selectedPart();
        const auto id   = view_->selectedId();
        chart_ = std::move(chart);
        view_->setChart(chart_);
        stepModel_       = std::make_shared<GrafcetStepModel>(chart_, &runtime_);
        transitionModel_ = std::make_shared<GrafcetTransitionModel>(chart_, &runtime_);
        actionModel_     = std::make_shared<GrafcetActionModel>(chart_, &runtime_);
        steps_->setModel(stepModel_);
        transitions_->setModel(transitionModel_);
        actions_->setModel(actionModel_);
        if (part != GrafcetPart::None) { view_->select(part, id); showProperties(part, id); }
        refresh();
    }

    // Choisir un element depuis une liste : le dessin le montre, les Proprietes suivent.
    void pick(GrafcetPart part, int id) {
        view_->select(part, id);
        view_->reveal(part, id);
        showProperties(part, id);
        pickedInList->emit(part, id);   // 1.10 (R2) : le volet Code suit le choix fait dans une liste
    }

    // Les proprietes de la selection, relues dans le grafcet a chaque fois : apres
    // une modification, le panneau dit ce que le fichier dit maintenant.
    void showProperties(GrafcetPart part, int id) {
        std::vector<std::vector<std::string>> rows;
        auto add = [&](std::string field, std::string value) {
            rows.push_back({std::move(field), std::move(value)});
        };
        auto ids = [](const std::vector<int>& xs) {
            std::string out;
            for (int x : xs) out += (out.empty() ? "" : ", ") + ("X" + std::to_string(x));
            return out.empty() ? std::string("-") : out;
        };
        if (part == GrafcetPart::Step) {
            if (const auto* s = chart_.stepById(id)) {
                add("\xC3\x89tape", "X" + std::to_string(s->id)
                    + (s->initial ? "  \xC2\xB7  initiale" : "") + (s->isFinal ? "  \xC2\xB7  finale" : ""));
                add("Nom", s->name.empty() ? "-" : s->name);
                const auto in = grafcet::transitionsInto(chart_, id), out = grafcet::transitionsFrom(chart_, id);
                std::string tin, tout;
                for (int t : in) tin += (tin.empty() ? "" : ", ") + ("T" + std::to_string(t));
                for (int t : out) tout += (tout.empty() ? "" : ", ") + ("T" + std::to_string(t));
                add("Entr\xC3\xA9" "es", tin.empty() ? "-" : tin + (in.size() > 1 ? "  (convergence en OU)" : ""));
                add("Sorties", tout.empty() ? "-" : tout + (out.size() > 1 ? "  (divergence en OU)" : ""));
                for (const auto* a : chart_.actionsOfStep(id))
                    add("Action A" + std::to_string(a->id), a->name + "  \xC2\xB7  " + grafcet::kindLabel(a->kind, a->delay));
                if (const auto active = view_->stepActive(id))
                    add("En simulation", *active ? "active depuis " + view_->stepActiveTime(id) : "inactive");
                add("Programme", "Steps_" + chart_.arrayPrefix + "[...]  \xC2\xB7  ligne " + std::to_string(s->line));
            }
        } else if (part == GrafcetPart::Transition) {
            if (const auto* t = grafcet::transitionById(chart_, id)) {
                add("Transition", "T" + std::to_string(t->id) + "  \xC2\xB7  " + ids(t->sources) + " \xE2\x86\x92 " + ids(t->destinations));
                add("R\xC3\xA9" "ceptivit\xC3\xA9", grafcet::drawnCondition(chart_, *t));
                add("ST du programme", t->conditionExpr.empty() ? "(aucune : elle ne franchit jamais)"
                                                                : t->conditionExpr);
                if (!t->conditionText.empty()) add("Libell\xC3\xA9 court", t->conditionText);
                if (!t->delay.empty()) add("Retard", t->delay);
                if (t->isConvergence()) add("Convergence en ET", "toutes les \xC3\xA9tapes d'amont doivent \xC3\xAAtre actives");
                if (t->isDivergence()) add("Divergence en ET", "les \xC3\xA9tapes d'aval s'activent ensemble");
                if (view_->live()) {
                    const auto truth = view_->transitionTrue(id);
                    add("En simulation", std::string(view_->transitionValidated(id) ? "valid\xC3\xA9" "e" : "non valid\xC3\xA9" "e")
                        + (truth ? (*truth ? "  \xC2\xB7  vraie" : "  \xC2\xB7  fausse") : ""));
                }
            }
        } else if (part == GrafcetPart::Action) {
            if (const auto* a = grafcet::actionById(chart_, id)) {
                add("Action", "A" + std::to_string(a->id) + "  \xC2\xB7  " + (a->name.empty() ? "-" : a->name));
                add("\xC3\x89tape", "X" + std::to_string(a->boundStep));
                add("Genre", grafcet::kindLabel(a->kind, a->delay) + "  (" + grafcet::kindCode(a->kind) + ", "
                    + std::string(grafcet::kindQualifier(a->kind)) + ")");
                add("Sens", std::string(grafcet::kindMeaning(a->kind)));
                if (grafcet::usesDelay(a->kind)) add("Dur\xC3\xA9" "e", a->delay.empty() ? "-" : a->delay);
                add("Condition", a->enableExpr.empty() ? "-" : a->enableExpr);
                if (const auto on = view_->actionRunning(id)) add("En simulation", *on ? "en cours" : "arr\xC3\xAAt\xC3\xA9" "e");
                if (a->body.empty()) {
                    add("Corps", "(vide : elle ne pilote que .Out)");
                } else {
                    bool first = true;
                    std::string line;
                    for (char ch : a->body + "\n") {
                        if (ch != '\n') { line.push_back(ch); continue; }
                        if (!line.empty()) { add(first ? "Corps" : "", line); first = false; }
                        line.clear();
                    }
                }
            }
        } else {
            add("Grafcet", chart_.name + "  \xC2\xB7  " + chart_.instance);
            add("Bloc", "DFB_GRAFCETENGINE");
            add("Tableaux", "Steps_ / Trans_ / Acts_" + chart_.arrayPrefix);
            add("Contenu", std::to_string(chart_.steps.size()) + " \xC3\xA9tapes, " + std::to_string(chart_.transitions.size())
                + " transitions, " + std::to_string(chart_.actions.size()) + " actions");
            std::size_t orDiv = 0, orConv = 0, andDiv = 0, andConv = 0;
            for (const auto& b : grafcet::branches(chart_)) {
                if (b.kind == grafcet::BranchKind::OrDivergence) ++orDiv;
                else if (b.kind == grafcet::BranchKind::OrConvergence) ++orConv;
                else if (b.kind == grafcet::BranchKind::AndDivergence) ++andDiv;
                else ++andConv;
            }
            add("Structure", std::to_string(orDiv) + " divergence(s) et " + std::to_string(orConv)
                + " convergence(s) en OU, " + std::to_string(andDiv + andConv) + " en ET");
        }
        // INIT(Autre) / FINI(Autre) : le grafcet lance ou attendu, un double-clic l'ouvre
        propertyLinks_.assign(rows.size(), std::string{});
        if (part == GrafcetPart::Transition || part == GrafcetPart::Action) {
            std::vector<grafcet::Chart> all;
            for (RowIndex r = 0; r < instanceModel_->rowCount(); ++r)
                if (const auto* e = instanceModel_->entry(r)) all.push_back(e->chart);
            for (const auto& link : grafcet::chartLinks(chart_, all)) {
                if (link.id != id || link.part != (part == GrafcetPart::Transition ? 'T' : 'A')) continue;
                std::string instance;
                for (const auto& c : all)
                    if (c.name == link.other || c.instance == link.other) instance = c.instance;
                rows.push_back({link.launches ? "Lance le grafcet" : "Attend le grafcet",
                                link.other + "   (double-clic : l'ouvrir)"});
                propertyLinks_.push_back(instance);
            }
        }
        propertyModel_->setRows(std::move(rows));
        properties_->invalidate();
    }

    const core::SignalPtr<GrafcetPart, int> editRequested =
        core::Signal<GrafcetPart, int>::create();
    // 1.10 (R2) : un element choisi depuis une liste (Etapes, Transitions, Actions, Controles)
    const core::SignalPtr<GrafcetPart, int> pickedInList = core::Signal<GrafcetPart, int>::create();
    // un autre grafcet choisi dans le navigateur (son instance)
    const core::SignalPtr<std::string> openRequested = core::Signal<std::string>::create();

protected:
    void onLayout() override {
        const auto area = contentRect();
        const float paletteH = 30.f, stripH = 22.f;
        if (children().size() < 3) return;
        children()[0]->setBounds({area.x, area.y, area.w, paletteH});
        children()[1]->setBounds({area.x, area.y + paletteH + 2.f, area.w,
                                  std::max(0.f, area.h - paletteH - stripH - 6.f)});
        children()[2]->setBounds({area.x, area.bottom() - stripH, area.w, stripH});
    }

private:
    static TableView* addTable(Splitter& into, const char* title, const char* subtitle,
                               float share) {
        auto box = std::make_unique<BoxLayout>(Orientation::Vertical,
                                               std::string("grafcet.box.") + title);
        box->setSpacing(2.f);
        box->setAlignment(Align::Stretch);
        auto heading = std::make_unique<StatusBar>(std::string("grafcet.title.") + title);
        heading->setMessage(std::string(title) + "   \xC2\xB7   " + subtitle);
        box->addChild(std::make_unique<FixedHeight>(std::string("grafcet.cap.") + title,
                                                    std::move(heading), 18.f));
        auto table = std::make_unique<TableView>(std::string("grafcet.") + title);
        auto& ref = static_cast<TableView&>(box->addChild(std::move(table)));
        into.addPane(std::move(box), share, 90.f);
        return &ref;
    }

    TableView* addTab(TabControl& into, const char* title) {
        auto table = std::make_unique<TableView>(this->id() + ".tab." + std::to_string(into.tabCount()));
        auto* ref = table.get();
        into.addTab(TabControl::Tab{title, Icon::None, /*closable=*/false, false}, std::move(table));
        return ref;
    }

    grafcet::Chart chart_;
    sim::Runtime*  runtime_{nullptr};   // les modeles tiennent &runtime_, pas une copie
    std::vector<grafcet::Check> checks_;
    std::size_t    severe_{0};
    std::vector<std::string> propertyLinks_;   // ligne des Proprietes -> instance d'un autre grafcet

    ToolBar*     palette_{nullptr};
    GrafcetView* view_{nullptr};
    GrafcetCodePanel* code_{nullptr};   // 1.10 (R2) : le volet Code
    TableView*   instances_{nullptr};
    TableView*   properties_{nullptr};
    TabControl*  tabs_{nullptr};
    TableView*   steps_{nullptr};
    TableView*   transitions_{nullptr};
    TableView*   actions_{nullptr};
    TableView*   checksTable_{nullptr};
    TableView*   historyTable_{nullptr};
    StatusBar*   summary_{nullptr};

    std::shared_ptr<GrafcetInstanceModel>   instanceModel_;
    std::shared_ptr<GrafcetStepModel>       stepModel_;
    std::shared_ptr<GrafcetTransitionModel> transitionModel_;
    std::shared_ptr<GrafcetActionModel>     actionModel_;
    std::shared_ptr<GrafcetRowsModel>       checkModel_;
    std::shared_ptr<GrafcetRowsModel>       historyModel_;
    std::shared_ptr<GrafcetRowsModel>       propertyModel_;
    core::ConnectionScope                   links_;
};

// The open-documents list, so tabs stop being the only way to navigate once
// there are more of them than fit across the pane.
class DocumentListModel final : public IListModel {
public:
    void setEntries(std::vector<std::string> entries) { entries_ = std::move(entries); }
    [[nodiscard]] std::size_t rowCount() const override { return entries_.size(); }
    [[nodiscard]] std::string text(RowIndex r) const override { return entries_[r]; }
    [[nodiscard]] CellStyle style(RowIndex) const override {
        CellStyle s;
        s.icon = Icon::Document;
        return s;
    }
private:
    std::vector<std::string> entries_;
};

// =============================================================================
//  1.10 (R2, decision 11 bis) : le volet Code et le direct grafcet <-> sections
// =============================================================================
std::string sectionNameIn(const domain::Project& p, domain::Index s) {
    return s < p.sections.size() ? std::string(p.strings.text(p.sections[s].name)) : std::string{};
}

// Le code de l'element choisi, dans le volet. Une etape n'en a pas.
void showCodeFor(GrafcetPane& pane, const domain::Project& p, GrafcetCodePanel::Target target, int id) {
    auto& panel = pane.codePanel();
    const auto& chart = pane.view().chart();
    const std::string hint =
        "Ctrl+Espace : les noms du programme et les raccourcis du grafcet (X3, A2, FIN(A2), ACTIF(X3), "
        "DUREE(X1) >= T#5s, FINI(Autre)) ; Ctrl+Entr\xC3\xA9" "e applique ; Ctrl+Z d\xC3\xA9" "fait.";
    if (target == GrafcetCodePanel::Target::Transition) {
        if (const auto* t = grafcet::transitionById(chart, id)) {
            std::string where = sectionNameIn(p, chart.section);
            std::size_t line = 0;
            if (project::findConditionLine(p, chart.section, chart.arrayPrefix, id, line))
                where += ", ligne " + std::to_string(line + 1);
            panel.show(target, id, "R\xC3\xA9" "ceptivit\xC3\xA9 de T" + std::to_string(id) + "   \xC2\xB7   " + where,
                       t->conditionExpr, "Double-clic sur T" + std::to_string(id) + " : ses \xC3\xA9tapes De / Vers. " + hint);
            return;
        }
    }
    else if (target == GrafcetCodePanel::Target::Action) {
        if (const auto* a = grafcet::actionById(chart, id)) {
            if (chart.actionSection == domain::kNoIndex) {
                panel.showNothing("Ce grafcet n'a pas de section SFC_" + chart.name
                                  + "_Actions : le corps d'une action n'a nulle part o\xC3\xB9 aller.");
                return;
            }
            std::string body = a->body;
            while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) body.pop_back();
            std::string where = sectionNameIn(p, chart.actionSection);
            std::size_t open = 0, close = 0;
            if (project::findActionBlock(p, chart.actionSection, chart.arrayPrefix, id, open, close))
                where += ", lignes " + std::to_string(open + 1) + "-" + std::to_string(close + 1);
            panel.show(target, id, "Corps de l'action A" + std::to_string(id)
                                       + (a->name.empty() ? std::string{} : " (" + a->name + ")") + "   \xC2\xB7   "
                                       + grafcet::kindLabel(a->kind, a->delay) + "   \xC2\xB7   " + where,
                       body, "Double-clic sur A" + std::to_string(id) + " : genre, dur\xC3\xA9" "e, nom, \xC3\xA9tape. " + hint);
            return;
        }
    }
    panel.showNothing("Choisis une transition (sa r\xC3\xA9" "ceptivit\xC3\xA9) ou une action (son corps) : "
                      "son code s'\xC3\xA9" "crit ici, avec l'aide \xC3\xA0 la saisie des sections.");
}

// Les grafcets ouverts qui lisent `section` (kNoIndex : tous) relus dans le projet,
// et leur volet Code avec eux s'il n'est pas en cours de modification. Une empreinte
// du texte de leurs deux sections : une frappe ailleurs ne relit rien.
void refreshGrafcetPanes(ui::Widget& root, const std::map<std::string, int>& tabs,
                         const domain::Project* p, domain::Index section) {
    if (!p) return;
    static std::map<std::string, std::size_t> seen;
    std::vector<grafcet::Chart> all;
    for (const auto& entry : tabs) {
        const std::string paneId = "grafcet." + entry.first;
        auto* pane = dynamic_cast<GrafcetPane*>(root.findById(paneId));
        if (!pane) continue;
        const auto current = pane->view().chart();
        if (section != domain::kNoIndex && current.section != section && current.actionSection != section)
            continue;
        std::size_t print = 17;
        for (const auto s : {current.section, current.actionSection})
            if (s < p->sections.size()) print = print * 31 + std::hash<std::string>{}(p->sections[s].body);
        if (const auto it = seen.find(paneId); it != seen.end() && it->second == print) continue;
        seen[paneId] = print;
        if (all.empty()) all = grafcet::findCharts(*p);
        for (const auto& c : all) {
            if (c.section != current.section) continue;
            pane->setChart(c);
            pane->setChecks(grafcet::runChecks(pane->view().chart(), p, &all));
            auto& code = pane->codePanel();
            if (!code.dirty()) showCodeFor(*pane, *p, code.target(), code.targetId());
            else code.setStatus("Le programme a chang\xC3\xA9 pendant que tu modifiais ce code : "
                                "R\xC3\xA9tablir reprend son texte, Appliquer garde le tien.",
                                StatusBar::Severity::Warning);
            break;
        }
    }
}

// Les raccourcis du grafcet pour l'aide a la saisie : ACTIF(X3), DUREE(X3) >= T#5s,
// FIN(A2), FINI(Autre). Le libelle contient X3 / A2 : taper "X3" ou "A2" les trouve.
void grafcetCompletions(const grafcet::Chart& chart, const std::vector<std::string>& others,
                        std::string_view prefix, std::vector<MultiLineText::Completion>& out) {
    std::string wanted(prefix);
    for (auto& ch : wanted) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    auto add = [&](std::string text, std::string detail, std::string insert, std::size_t caret) {
        std::string upper = text;
        for (auto& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (!wanted.empty() && upper.find(wanted) == std::string::npos) return;
        MultiLineText::Completion c;
        c.text = std::move(text);
        c.detail = std::move(detail);
        c.icon = Icon::Section;
        c.rank = upper.rfind(wanted, 0) == 0 ? 1 : 3;
        c.insert = std::move(insert);
        c.caret = caret;
        out.push_back(std::move(c));
    };
    for (const auto& st : chart.steps) {
        const std::string x = "X" + std::to_string(st.id);
        const std::string name = st.name.empty() || st.name == x ? std::string{} : " '" + st.name + "'";
        add("ACTIF(" + x + ")", "l'\xC3\xA9tape " + x + name + " est active", {}, std::string::npos);
        const std::string duree = "DUREE(" + x + ") >= T#5s";
        add(duree, "depuis combien de temps " + x + " est active", duree, duree.size() - 2);
    }
    for (const auto& a : chart.actions) {
        const std::string id = "A" + std::to_string(a.id);
        add("FIN(" + id + ")", "l'action " + id + (a.name.empty() ? std::string{} : " (" + a.name + ")")
                                  + " a fini (" + grafcet::kindLabel(a.kind, a.delay) + ")", {}, std::string::npos);
    }
    for (const auto& other : others)
        if (other != chart.name)
            add("FINI(" + other + ")", "le grafcet " + other + " est fini", {}, std::string::npos);
}

// L'apercu "Ce qui va changer dans le programme", en lignes lisibles.
std::string changeListText(const std::vector<project::StLineChange>& lines, std::size_t max = 14) {
    auto cut = [](std::string text) {
        for (auto& ch : text) if (ch == '\t') ch = ' ';
        const auto first = text.find_first_not_of(' ');
        text = first == std::string::npos ? std::string{} : text.substr(first);
        if (text.size() > 86) {
            std::size_t at = 83;
            while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) --at;
            text = text.substr(0, at) + "...";
        }
        return text;
    };
    std::string out;
    std::size_t shown = 0;
    for (const auto& l : lines) {
        if (shown++ == max) {
            out += "\xE2\x80\xA6 et " + std::to_string(lines.size() - max) + " autre(s) ligne(s)\n";
            break;
        }
        if (l.removed())    out += l.sectionName + ", l. " + std::to_string(l.lineBefore) + " retir\xC3\xA9" "e :  " + cut(l.before) + "\n";
        else if (l.added()) out += l.sectionName + ", l. " + std::to_string(l.lineAfter) + " ajout\xC3\xA9" "e :  " + cut(l.after) + "\n";
        else                out += l.sectionName + ", l. " + std::to_string(l.lineBefore) + " :  " + cut(l.before)
                                 + "\n      devient :  " + cut(l.after) + "\n";
    }
    return out;
}

} // namespace

MainAnalysisScreen::MainAnalysisScreen(App& app)
    : menu::WidgetMenu("analysis"), app_(app) {}

menu::MenuTraits MainAnalysisScreen::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Screen;
    t.closableWithEscape = false;      // Escape must not drop the user's workspace
    return t;
}

// ------------------------------------------------------------------ build ---
core::Status MainAnalysisScreen::buildUi() {
    auto shell = std::make_unique<DockLayout>("analysis.shell");

    // LOT API 2 : LA BARRE DU HAUT, REPENSEE (TopBar). Sept groupes nommes au lieu
    // de 28 icones ; chaque bouton d'avant garde son action - le meme identifiant,
    // donc le meme raccourci - et change seulement de place (menus Projet,
    // + Nouveau, Affichage, Aide). Aller a... et Release / Debug y sont poses.
    auto bar = std::make_unique<TopBar>("analysis.topbar");
    topBar_ = bar.get();
    {
        auto box = std::make_unique<GoToBox>("analysis.goto");
        goToBox_ = box.get();
        bar->setGoTo(std::move(box));
    }
    {
        auto config = std::make_unique<DropDown>("analysis.buildConfig");
        config->setItems({{"Release", "release", {}, true}, {"Debug", "debug", {}, true}});
        config->setSelectedIndex(0);
        bar->setConfiguration(std::move(config));
    }
    // ---- Lot API 8 : bandeau haut - 44 px (48 en fort contraste) ----
    shell->dock(std::move(bar), DockLayout::Side::Top, std::max(TopBar::kBarHeight, app_.theme().metric.toolbarHeight));
    // ---- fin Lot API 8 : bandeau haut ----

    auto status = std::make_unique<StatusBar>("analysis.status");
    status_ = &static_cast<StatusBar&>(
        shell->dock(std::move(status), DockLayout::Side::Bottom, app_.theme().metric.statusBarHeight));

    auto outer = std::make_unique<Splitter>(Orientation::Vertical, "analysis.outer");
    outer_ = outer.get();

    //   row 1: explorer | centre | variables
    {
        auto upper = std::make_unique<Splitter>(Orientation::Horizontal, "analysis.upper");
        upper_ = upper.get();

        auto left = std::make_unique<Splitter>(Orientation::Vertical, "analysis.left");
        left_ = left.get();

        auto explorer = std::make_unique<TreeView>("analysis.explorer");
        // ---- Lot API 8 : l'arbre du projet (le filtre au-dessus, TreeWorkspace.cpp : wrapExplorer pose explorer_) ----
        left_->addPane(wrapExplorer(std::move(explorer)), 0.68f, 140.f);
        // ---- fin Lot API 8 : l'arbre du projet ----
        // Lot 21 : Ctrl+clic en choisit plusieurs, pour les glisser ensemble dans un dossier.
        explorer_->setSelectionMode(SelectionMode::Extended);
        explorer_->setModernLook(true);    // 1.11.22 : l'explorateur modernise (la maquette validee le 09/10)
        explorer_->setTooltip("Un clic ouvre l'onglet de l'entr\xC3\xA9" "e ; une section s'ouvre dans son \xC3\xA9" "diteur");
        // Lot 7 : sur une variable en simulation, sa valeur du moment ; sur API >
        // Simulation, l'etat et le cycle - relus tant que l'infobulle est ouverte.
        // Lot API 8 : l'arbre du projet - sinon, la carte du noeud (treeCardText).
        explorer_->setNodeTooltip([this](NodeId n) { auto tip = explorerTip(n); return tip.empty() ? treeCardText(n) : tip; });

        auto docs = std::make_unique<ListView>("analysis.openDocuments");
        docs->setTooltip("Les documents ouverts : un clic en am\xC3\xA8ne un devant, un double-clic le ferme.");
        documentList_ = &static_cast<ListView&>(left_->addPane(std::move(docs), 0.32f, 90.f));

        upper_->addPane(std::move(left), 0.22f, 180.f);

        // Lot 7 : le centre en plusieurs groupes d'onglets (TabArea) - la meme
        // interface que le TabControl d'avant, en indices globaux.
        auto centre = std::make_unique<TabArea>("analysis.centre");
        centre_ = centre.get();
        centre_->setDetachEnabled(true);

        // LOT API 2 : PLUS D'ONGLETS FIXES. IO Mapping, Task Configuration,
        // Communication et Layout restaient ouverts, vides pour la plupart ; ils
        // deviennent des entrees de l'arbre. Le rack et ses proprietes forment la
        // page Configuration de l'API, le choix des panneaux la page Panneaux
        // (Affichage > Panneaux a afficher) : des pages GARDEES, qui s'ouvrent
        // et se ferment comme les autres onglets sans perdre leurs widgets.
        {
            auto io = std::make_unique<Splitter>(Orientation::Vertical, "analysis.io");
            auto rack = std::make_unique<RackView>("analysis.rack");
            rack->setTooltip("Ctrl + molette : zoom ; molette : d\xC3\xA9" "filer ; un clic sur un module : ses propri\xC3\xA9t\xC3\xA9s");
            rackView_ = &static_cast<RackView&>(io->addPane(std::move(rack), 0.60f, 160.f));

            auto grid = std::make_unique<PropertyGrid>("analysis.configuration");
            configuration_ = grid.get();
            io->addPane(std::move(grid), 0.40f, 120.f);

            auto frame = std::make_unique<ApiFrame>("analysis.api.configuration", std::move(io));
            auto& tools = frame->tools();
            tools.add(1, HmiGlyph::Gear, "Les propri\xC3\xA9t\xC3\xA9s du processeur et du projet", "Processeur");
            tools.separator();
            tools.add(2, HmiGlyph::Plus, "Ajouter un rack", "Rack");
            tools.add(3, HmiGlyph::Plus, "Poser un module dans un emplacement libre", "Module");
            tools.separator();
            tools.add(4, HmiGlyph::Import, "Importer le .XHW (ou le .XEF) de Control Expert : les racks, les modules, leurs voies", "Importer le .XHW\xE2\x80\xA6");
            tools.add(5, HmiGlyph::Refresh, "R\xC3\xA9importer et r\xC3\xA9" "analyser les fichiers (F5)", "R\xC3\xA9" "analyser");
            paneLinks_ += tools.triggered->connect([this](int a) {
                if (a == 1) {
                    if (auto p = app_.project()) configuration_->setCategories(buildConfigurationProperties(*p));
                    return;
                }
                const char* id = a == 2 ? "create.rack" : a == 3 ? "create.module" : a == 4 ? "file.importHardware" : "analyze.run";
                (void)app_.actions().trigger(id, app_.commands());
            });
            frame->setHint("Le processeur vient de l'export .XPG ; les racks et les modules, du .XHW. Un clic sur un module : ses propri\xC3\xA9t\xC3\xA9s.");
            auto* raw = frame.get();
            keptPages_["configuration"] = KeptPage{TabControl::Tab{"API \xC2\xB7 Configuration", Icon::Rack, true, false}, std::move(frame), raw};
        }
        {
            auto panel = buildViewPanel();
            auto* raw = panel.get();
            keptPages_["panneaux"] = KeptPage{TabControl::Tab{"Panneaux", Icon::Settings, true, false}, std::move(panel), raw};
        }
        fixedTabCount_ = 0;
        // 1.11.14 : LE PANNEAU DU BAS - Sorties (le build, la simulation), Console (IHM_LOG,
        // les erreurs d'execution), Diagnostics - sous les onglets du centre.
        auto column = std::make_unique<Splitter>(Orientation::Vertical, "analysis.centreColumn");
        centreColumn_ = column.get();
        column->addPane(std::move(centre), 0.68f, 200.f);
        auto outputs = std::make_unique<HmiBuildOutputPane>("hmi.sorties");
        bottomPanel_ = outputs.get();
        column->addPane(std::move(outputs), 0.32f, 120.f);
        upper_->addPane(std::move(column), 0.48f, 320.f);

        auto right = std::make_unique<BoxLayout>(Orientation::Vertical, "analysis.right");
        {
            auto filters = std::make_unique<BoxLayout>(Orientation::Horizontal, "analysis.varFilters");
            auto search = std::make_unique<InputText>("analysis.varSearch");
            search->setPlaceholder("Search variables...");
            variableSearch_ = &static_cast<InputText&>(filters->addChild(std::move(search)));

            auto scope = std::make_unique<DropDown>("analysis.varScope");
            scope->setItems({{"All variables", "all"}, {"Global", "Global"}, {"Local", "Local"},
                             {"Located I/O", "located"}, {"Unused", "unused"}});
            scope->setSelectedIndex(0);
            variableScopeFilter_ = &static_cast<DropDown&>(filters->addChild(std::move(scope)));
            right->addChild(std::move(filters));
        }
        auto table = std::make_unique<TableView>("analysis.variables");
        table->setColumns({{"Name", 300.f}, {"Type", 210.f}, {"Address", 150.f},
                           {"Scope", 130.f}, {"Comment", 340.f},
                           {"Usage", 110.f, 60.f, true, true, true, Align::End}});
        table->setSelectionMode(SelectionMode::Extended);
        variables_ = table.get();
        right->addChild(std::move(table));
        upper_->addPane(std::move(right), 0.30f, 260.f);

        outer_->addPane(std::move(upper), 0.56f, 200.f);
    }

    //   (lot 7 : la rangee DFB library | DB library | programming sections est
    //   partie - l'arbre et les onglets Blocs DFB, Unites, Ordre d'execution
    //   disent la meme chose en mieux ; sa place revient au centre.)

    //   row 3: diagnostics | analysis summary | project status
    {
        auto bottom = std::make_unique<Splitter>(Orientation::Horizontal, "analysis.bottom");
        bottom_ = bottom.get();

        auto diags = std::make_unique<TableView>("analysis.diagnostics");
        diags->setColumns({{"Severity", 140.f, 90.f, false, true}, {"Type", 300.f},
                           {"Subject", 320.f}, {"Message", 640.f}});
        diagnostics_ = &static_cast<TableView&>(bottom_->addPane(std::move(diags), 0.50f, 300.f));
        summary_ = &static_cast<PropertyGrid&>(
            bottom_->addPane(std::make_unique<PropertyGrid>("analysis.summary"), 0.25f, 180.f));
        projectStatus_ = &static_cast<PropertyGrid&>(
            bottom_->addPane(std::make_unique<PropertyGrid>("analysis.projectStatus"), 0.25f, 180.f));

        outer_->addPane(std::move(bottom), 0.18f, 100.f);
    }

    shell->dock(std::move(outer), DockLayout::Side::Center, 0.f);

    // The context menu lives outside the dock layout, in an OverlayHost, so it
    // can draw and be clicked anywhere on the screen without the layout
    // reserving a slot for something that has no size.
    auto host = std::make_unique<OverlayHost>("analysis.host");
    host->setContent(std::move(shell));
    auto popup = std::make_unique<PopupMenu>("analysis.contextMenu");
    contextMenu_ = popup.get();
    host->addOverlay(std::move(popup));
    // Lot API 2 : les menus de la barre du haut (Projet, + Nouveau, Affichage, Aide).
    auto barPopup = std::make_unique<PopupMenu>("analysis.barMenu");
    barMenu_ = barPopup.get();
    host->addOverlay(std::move(barPopup));
    // Lot 7 : le menu d'un onglet du centre (clic droit sur son en-tete). Le
    // sien, pas celui de l'arbre : ses numeros d'entree ne sont pas les memes.
    auto tabPopup = std::make_unique<PopupMenu>("analysis.tabMenu");
    tabMenu_ = tabPopup.get();
    host->addOverlay(std::move(tabPopup));
    // Le didacticiel de l'IHM (lot 7) : par-dessus tout, cache tant qu'il ne
    // tourne pas (HmiWorkspace.cpp le compose et le lance).
    auto tutorial = std::make_unique<HmiTutorial>("analysis.hmiTutorial");
    hmiTutorial_ = tutorial.get();
    host->addOverlay(std::move(tutorial));
    // Lot 19 : l'historique, un tiroir a droite (Ctrl+H).
    auto history = std::make_unique<HistoryPanel>("analysis.history");
    historyPanel_ = history.get();
    host->addOverlay(std::move(history));
    // Lot 20 : le panneau d'Aller a..., sous son champ.
    auto go = std::make_unique<GoToPanel>("analysis.gotoPanel");
    goToPanel_ = go.get();
    host->addOverlay(std::move(go));
    // ---- Lot API 8 : le bandeau bas (StatusStripWorkspace.cpp) ----
    buildStatusStrip(*host);
    // ---- fin Lot API 8 : le bandeau bas ----

    setRoot(std::move(host));
    return core::ok();
}

// -------------------------------------------------------------- view tab ----
WidgetPtr MainAnalysisScreen::buildViewPanel() {
    auto panel = std::make_unique<BoxLayout>(Orientation::Vertical, "analysis.viewPanel");
    panel->setSpacing(2.f);
    panel->setPadding({8.f, 10.f, 8.f, 10.f});
    panel->setAlignment(Align::Start);

    // Each entry binds a checkbox to one splitter pane. Collapsing a pane
    // rather than hiding a widget is what lets the neighbours take the space
    // back instead of leaving a gap where the panel used to be.
    // Lot API 8 : en francais (les sessions d'avant cochent encore "Bottom row",
    // "Open documents" : la commande "case" connait les anciens noms).
    const struct { const char* label; const char* key; std::size_t owner; std::size_t pane; }
    entries[] = {
        {"Arbre du projet",                 kViewExplorer,    4, 0},
        {"Documents ouverts",               kViewDocuments,   4, 1},
        {"Configuration de l'automate",     kViewCentre,      1, 1},
        {"Rang\xC3\xA9" "e du bas",       kViewBottomRow,   0, 1},
        {"Diagnostics",                     kViewDiagnostics, 3, 0},
        {"R\xC3\xA9sum\xC3\xA9 de l'analyse", kViewSummary, 3, 1},
        {"\xC3\x89tat du projet",         kViewStatus,      3, 2},
        {"Panneau du bas : Sorties, Console, Diagnostics (Ctrl+J)", kViewOutputPanel, 5, 1},
    };

    for (const auto& e : entries) {
        auto box = std::make_unique<Checkbox>(e.label);
        auto& ref = static_cast<Checkbox&>(panel->addChild(std::move(box)));
        ref.setTooltip(std::string("Montrer ou cacher le volet \xC2\xAB ") + e.label
                       + " \xC2\xBB. Cach\xC3\xA9, il laisse sa place \xC3\xA0 ses voisins ; "
                         "le choix est retenu dans les r\xC3\xA9glages.");
        panels_.push_back(PanelToggle{&ref, e.key, e.owner, e.pane});
    }

    auto alt = std::make_unique<Checkbox>("Lignes altern\xC3\xA9" "es");
    altRowsBox_ = &static_cast<Checkbox&>(panel->addChild(std::move(alt)));
    return panel;
}

Splitter* MainAnalysisScreen::splitterFor(std::size_t which) const {
    switch (which) {
        case 0:  return outer_;
        case 1:  return upper_;
        case 2:  return middle_;
        case 3:  return bottom_;
        case 5:  return centreColumn_;      // 1.11.14
        default: return left_;
    }
}

void MainAnalysisScreen::applyPanelVisibility() {
    for (const auto& p : panels_)
        if (auto* sp = splitterFor(p.owner)) sp->collapsePane(p.pane, !p.box->isChecked());
    // Lot API 6 : l'ancien explorateur de variables (a droite) n'est plus montre -
    // l'onglet API > Variables fait mieux, et la place revient au centre. La table
    // reste en memoire : des recherches et des filtres s'en servent encore.
    if (upper_) upper_->collapsePane(2, true);
    if (variables_) variables_->setAlternatingRowColors(altRowsBox_->isChecked());
}

// 1.11.14 : le panneau du bas - sa case dans Affichage > Panneaux fait foi (retenue).
bool MainAnalysisScreen::bottomPanelShown() const {
    for (const auto& p : panels_)
        if (std::string_view(p.key) == kViewOutputPanel) return p.box->isChecked();
    return false;
}

void MainAnalysisScreen::showBottomPanel(bool show, std::size_t tab) {
    for (const auto& p : panels_)
        if (std::string_view(p.key) == kViewOutputPanel && p.box->isChecked() != show)
            p.box->setState(show ? Checkbox::State::Checked : Checkbox::State::Unchecked);   // stateChanged : le replie et retient
    if (show && bottomPanel_ && tab != static_cast<std::size_t>(-1)) bottomPanel_->showTab(tab);
}

void MainAnalysisScreen::toggleBottomPanel() {
    const bool show = !bottomPanelShown();
    showBottomPanel(show);
    if (status_)
        status_->setTransientMessage(show ? "Panneau du bas : Sorties, Console, Diagnostics (Ctrl+J le replie)"
                                          : "Panneau du bas repli\xC3\xA9 (Ctrl+J le rouvre)", 3.0);
}

void MainAnalysisScreen::loadWorkspace() {
    auto& s = app_.settings();

    // READ EVERYTHING FIRST.
    //
    // The bug this replaces: setState() fires stateChanged, whose handler calls
    // saveWorkspace(), which writes *every* key from the checkboxes' current
    // state. Loading panel 0 therefore overwrote the stored value of panels 1..n
    // with "false" (they had not been loaded yet), and the next iteration read
    // that back. Result: the first panel restored and every other one came up
    // collapsed - which is exactly the all-explorer window in the report.
    //
    // Snapshotting the values before touching a single widget removes the
    // ordering dependency entirely; the guard below then stops the load from
    // writing anything at all.
    std::vector<bool> wanted;
    wanted.reserve(panels_.size());
    // Lot API 8 : une installation neuve (l'installateur) ne montre ni la rangee du bas (les
    // anciens volets Diagnostics, Resume, Etat) ni la liste des documents ouverts (les onglets
    // la donnent) ; Affichage > Panneaux les rend. Un reglage deja enregistre est garde.
    for (const auto& p : panels_)
        wanted.push_back(s.getBool(p.key, std::string_view(p.key) != kViewBottomRow && std::string_view(p.key) != kViewDocuments));
    const bool wantAltRows = s.getBool("view.alternatingRows", true);

    std::vector<std::vector<float>> ratios;
    for (std::size_t i = 0; i < kLayoutCount; ++i) ratios.push_back(s.getFloats(layoutKey(i)));

    const RestoreGuard guard{restoring_};       // suppresses saveWorkspace()

    for (std::size_t i = 0; i < panels_.size(); ++i)
        panels_[i].box->setState(wanted[i] ? Checkbox::State::Checked
                                           : Checkbox::State::Unchecked);
    altRowsBox_->setState(wantAltRows ? Checkbox::State::Checked : Checkbox::State::Unchecked);

    for (std::size_t i = 0; i < kLayoutCount; ++i)
        // Lot 7 : seulement s'ils ont le bon nombre de panneaux - la rangee des
        // bibliotheques est partie, trois valeurs d'avant ne vont plus a deux.
        if (auto* sp = splitterFor(i); sp && !ratios[i].empty() && ratios[i].size() == sp->ratios().size())
            sp->setRatios(ratios[i]);

    applyPanelVisibility();
    // Lot 7 : la disposition du centre (onglets, groupes, mosaique).
    if (centre_) {
        TabArea::Mode mode{};
        if (TabArea::modeFromName(s.getString(kLayoutModeKey, "onglets"), mode) && mode == TabArea::Mode::Mosaic)
            centre_->setMode(mode);        // les groupes se refont en divisant : on repart d'un groupe
    }
}

void MainAnalysisScreen::saveWorkspace() {
    if (restoring_) return;      // a load must never write back mid-flight
    auto& s = app_.settings();
    for (const auto& p : panels_) s.set(p.key, p.box->isChecked());
    s.set("view.alternatingRows", altRowsBox_->isChecked());
    for (std::size_t i = 0; i < kLayoutCount; ++i)
        if (auto* sp = splitterFor(i)) s.setFloats(layoutKey(i), sp->ratios());
    s.save();
}

// ------------------------------------------------------------- documents ----
void MainAnalysisScreen::openDocument(domain::Index sectionIndex) {
    auto project = app_.project();
    if (!project || sectionIndex >= project->sections.size()) return;

    // Already open? Focus it rather than opening a second copy of the same body.
    for (const auto& d : documents_)
        if (d.section == sectionIndex) { centre_->setCurrentIndex(d.tab); return; }

    const auto& section = project->sections[sectionIndex];
    // The section's declared language picks the initial lexer; the per-document
    // dropdown can override it, which is what makes opening a C or C++ file in
    // the same viewer useful.
    const auto initial = section.language == domain::PouLanguage::ST ? Language::StructuredText
                       : section.language == domain::PouLanguage::IL ? Language::InstructionList
                                                                     : Language::PlainText;

    auto pane = std::make_unique<DocumentPane>("analysis.doc." + std::to_string(sectionIndex),
                                               initial);
    pane->view().setText(section.body);
    pane->view().setReadOnly(true);        // the Edit toggle opens it

    // The editor knows nothing about PLCs; it asks, and this is the answer.
    // The document owns a shared_ptr to the model, so a completion request can
    // never outlive what it reads.
    auto document = app_.document();
    pane->view().setCompletionProvider(
        [document, sectionIndex](std::string_view prefix,
                                 std::vector<MultiLineText::Completion>& out) {
            if (!document) return;
            for (const auto& s : project::suggestionsFor(*document, sectionIndex, prefix)) {
                MultiLineText::Completion c;
                c.text   = s.text;
                c.detail = s.detail;
                c.rank   = s.rank;
                c.insert = s.insert;      // empty for a plain name
                c.caret  = s.caret;
                switch (s.kind) {
                    case project::Suggestion::Kind::LocatedVariable: c.icon = Icon::LocatedVariable; break;
                    case project::Suggestion::Kind::Variable:        c.icon = Icon::Variable; break;
                    case project::Suggestion::Kind::FunctionBlock:
                    case project::Suggestion::Kind::Function:        c.icon = Icon::FunctionBlock; break;
                    case project::Suggestion::Kind::DerivedType:     c.icon = Icon::DerivedType; break;
                    case project::Suggestion::Kind::Type:            c.icon = Icon::Constant; break;
                    case project::Suggestion::Kind::Keyword:         c.icon = Icon::Section; break;
                }
                out.push_back(std::move(c));
            }
        });

    // What a call takes, shown as soon as the parenthesis is opened. It answers
    // the question people actually stop to look up: what goes in, in what order.
    pane->view().setSignatureProvider(
        [document](std::string_view name, MultiLineText::Signature& out) {
            if (!document) return false;
            project::CallSignature sig;
            if (!project::signatureFor(*document, name, sig)) return false;
            out.name       = std::move(sig.name);
            out.parameters = std::move(sig.parameters);
            out.returns    = std::move(sig.returns);
            return true;
        });

    // While a simulation is running, hovering a name in the code says what it
    // holds this scan. The runtime lives on the simulation screen, so it is
    // reached through the app rather than captured here.
    pane->view().setValueProvider(
        [this](std::string_view symbol, std::string& text) {
            auto* runtime = app_.simulationRuntime();
            if (!runtime) return false;
            sim::Value v;
            if (!runtime->get(symbol, v)) return false;
            text = std::string(symbol) + " = " + v.display();
            if (runtime->isForced(symbol)) text += "   (forced)";
            return true;
        });
    // ---- Lot API 8 : les points d'arret dans la marge, la ligne d'arret, la
    // valeur au survol (les locales d'une unite, l'instance d'un bloc) -
    // SimDebugWorkspace.cpp ; remplace le fournisseur de valeurs du dessus.
    wireSectionBreakpoints(pane->view(), sectionIndex);
    // ---- fin Lot API 8 ----

    // The bar under the code. Wired to the caret rather than polled, so a
    // project-wide lookup happens when the name changes and not once a frame.
    {
        auto* raw = pane.get();
        paneLinks_ += pane->view().caretSymbolChanged->connect(
            [this, raw, sectionIndex](const std::string& symbol) {
                // Le nom sous le curseur est aussi ce que F1 doit ouvrir. On le
                // retient ici plutot que d'aller le rechercher au moment de la
                // touche : l'editeur le calcule deja, et deux calculs du meme
                // nom finissent par ne plus designer la meme chose.
                lastSymbol_ = symbol;
                if (symbol.empty()) { raw->setSymbolLine({}); return; }
                auto current = app_.project();
                if (!current) return;

                const auto info = project::describeSymbol(*current, sectionIndex, symbol);
                gfx::Color accent{};
                if (!info.found) accent = gfx::Color::rgb(0xE8C46F);   // amber: it did not resolve

                std::string line = project::symbolStatusLine(info);

                // If the program is running, what it holds right now belongs on
                // the same line. Two places reporting the same variable is how
                // they end up disagreeing.
                if (auto* runtime = app_.simulationRuntime(); runtime && info.found) {
                    sim::Value v;
                    if (runtime->get(symbol, v)) {
                        line += "   = " + v.display();
                        if (runtime->isForced(symbol)) line += " (forced)";
                        accent = gfx::Color::rgb(0x7FD08A);
                    }
                }
                raw->setSymbolLine(line, accent);
            });
    }

    // Double-clicking a name in the code. The editor reports the name; whether
    // it means anything is this screen's business, and for an engine instance it
    // means "draw this chart".
    paneLinks_ += pane->view().symbolActivated->connect([this](const std::string& symbol) {
        openGrafcetFor(symbol);
    });

    // Each keystroke is a command. They merge, so Ctrl+Z steps back over a
    // sentence rather than a letter, and the views are not rebound while typing.
    auto* paneRaw = pane.get();
    paneLinks_ += paneRaw->view().textChanged->connect(
        [this, sectionIndex](const std::string& body) {
            if (!app_.document()) return;
            app_.apply(std::make_unique<project::SetSectionBodyCommand>(
                           app_.document(), sectionIndex, body),
                       /*refreshViews*/ false);
            status_->setMessage("edited - Ctrl+S saves the project");
            // 1.10 (R2, decision 11 bis) : le grafcet ouvert sur cette section se
            // redessine aussitot (son volet Code suit s'il n'est pas en cours).
            refreshGrafcetPanes(root(), grafcetTabs_, app_.document().get(), sectionIndex);
        });

    const int codeIcon = project::codeicons::sectionIcon(*project, sectionIndex);   // 1.8.0 : l'icone choisie
    const auto tab = centre_->addTab(
        TabControl::Tab{std::string(project->strings.text(section.name)), codeIcon >= 0 ? ui::codeIcon(codeIcon) : Icon::Section, true, false},
        std::move(pane));
    documents_.push_back(OpenDocument{sectionIndex, tab});
    centre_->setCurrentIndex(tab);
    refreshDocumentList();

    status_->setMessage(std::string(project->strings.text(section.name)) + "  -  "
                        + std::string(domain::toString(section.language)) + ", "
                        + std::to_string(section.lineCount) + " lines, "
                        + std::to_string(section.statementCount) + " statements");
}

void MainAnalysisScreen::closeDocument(std::size_t tabIndex) {
    if (tabIndex < fixedTabCount_) return;      // configuration tabs are permanent
    forgetHmiTab(tabIndex);                     // avant removeTab : la page existe encore
    // Lot API 2 : un onglet de l'API oublie son volet ; une page gardee
    // (Configuration, Panneaux) est mise de cote, pas detruite.
    for (auto it = apiTabs_.begin(); it != apiTabs_.end();)
        it = it->second == centre_->page(tabIndex) ? apiTabs_.erase(it) : std::next(it);
    if (!parkTab(tabIndex)) centre_->removeTab(tabIndex);
    shiftTabIndices(tabIndex);
    refreshDocumentList();
}

// LES ONGLETS RETROUVES PAR UN INDICE ONT GLISSE EUX AUSSI. Grafcets, macros,
// variables, appels : seul documents_ etait recale, et le double-clic suivant
// amenait devant l'onglet VOISIN de celui qu'on demandait. Lot 7 : aussi quand
// un onglet part dans une fenetre a lui (detachTab).
void MainAnalysisScreen::shiftTabIndices(std::size_t removed) {
    std::erase_if(documents_, [removed](const OpenDocument& d) { return d.tab == removed; });
    for (auto& d : documents_)
        if (d.tab > removed) --d.tab;           // tabs after the removed one shifted down
    const int closed = static_cast<int>(removed);
    for (auto* tabs : {&grafcetTabs_, &macroTabs_, &variableTabs_, &callTabs_}) {
        for (auto it = tabs->begin(); it != tabs->end();) {
            if (it->second == closed) { it = tabs->erase(it); continue; }
            if (it->second > closed) --it->second;
            ++it;
        }
    }
    if (viewTabIndex_ > removed) --viewTabIndex_;
}

// Lot 7 : LE CENTRE INSERE AU MILIEU. Un onglet ajoute a un groupe qui n'est
// pas le dernier (ou une page ramenee d'une fenetre) prend une place au milieu
// de l'ordre global : ceux d'apres avancent d'un cran.
void MainAnalysisScreen::onTabInserted(std::size_t at) {
    for (auto& d : documents_)
        if (d.tab >= at) ++d.tab;
    const int from = static_cast<int>(at);
    for (auto* tabs : {&grafcetTabs_, &macroTabs_, &variableTabs_, &callTabs_})
        for (auto& [name, index] : *tabs)
            if (index >= from) ++index;
    if (viewTabIndex_ >= at && viewTabIndex_ + 1 < centre_->tabCount()) ++viewTabIndex_;
}

// Un onglet passe de `from` a `to` (d'un groupe a l'autre, ou dans sa bande) :
// il emporte son indice, ceux d'entre les deux glissent d'un cran vers `from`.
void MainAnalysisScreen::onTabMoved(std::size_t from, std::size_t to) {
    const auto remap = [from, to](std::size_t i) -> std::size_t {
        if (i == from) return to;
        if (from < to && i > from && i <= to) return i - 1;
        if (to < from && i >= to && i < from) return i + 1;
        return i;
    };
    for (auto& d : documents_) d.tab = remap(d.tab);
    for (auto* tabs : {&grafcetTabs_, &macroTabs_, &variableTabs_, &callTabs_})
        for (auto& [name, index] : *tabs)
            if (index >= 0) index = static_cast<int>(remap(static_cast<std::size_t>(index)));
    viewTabIndex_ = remap(viewTabIndex_);
    refreshDocumentList();
}

// =============================================================================
//  LOT 7 : FERMER DES ONGLETS
// -----------------------------------------------------------------------------
//  Le clic droit sur un onglet ouvre son menu : Fermer, Fermer tout, Fermer
//  tout sauf celui-ci, pour l'onglet VISE (pas forcement l'onglet ouvert). Sa
//  croix et le clic du milieu le ferment ; Ctrl+W ferme l'onglet ouvert.
//
//  TOUT PASSE PAR closeTabs(). Un onglet sans croix (ou fixe) reste ; une page
//  gardee (Configuration, Panneaux) est mise de cote par closeDocument, comme
//  a la croix. Et FERMER NE PERD RIEN EN SILENCE : les onglets vises qui ont
//  quelque chose de pas enregistre - une macro modifiee dans l'editeur de
//  l'onglet Macros, qui garde ses editeurs jusqu'a sa fermeture - sont listes
//  dans UNE question, pas une par onglet. Les autres sont des vues du projet :
//  chaque geste y est deja une commande, les fermer ne perd rien.
// =============================================================================
namespace {
// Les entrees du menu d'un onglet (Item::id, rendu par itemChosen).
enum TabMenuEntry : int { kTabMenuClose = 1, kTabMenuCloseAll, kTabMenuCloseOthers,
                          // Lot 7 : l'affichage multi-fenetre.
                          kTabMenuSplitRight, kTabMenuSplitDown, kTabMenuNeighbour, kTabMenuMosaic, kTabMenuDetach };
// Ce que Fermer annonce : le raccourci de l'onglet ouvert (handleShortcut).
constexpr const char* kCloseTabShortcut = "Ctrl+W";
} // namespace

bool MainAnalysisScreen::tabClosable(std::size_t index) const {
    const auto* t = centre_ ? centre_->tab(index) : nullptr;
    return t != nullptr && t->closable && index >= fixedTabCount_;
}

std::vector<std::string> MainAnalysisScreen::unsavedInTab(std::size_t index) const {
    std::vector<std::string> out;
    const auto* t = centre_ ? centre_->tab(index) : nullptr;
    const Widget* page = centre_ ? centre_->page(index) : nullptr;
    if (!t || !page) return out;
    // Les editeurs de macros, ou qu'ils soient dans la page : l'onglet Macros
    // en garde un par macro ouverte en mode Modifier, pas encore enregistre.
    std::function<void(const Widget&)> visit = [&](const Widget& w) {
        if (const auto* editor = dynamic_cast<const MacroEditorView*>(&w); editor && editor->dirty())
            out.push_back("la macro " + editor->name() + ", modifi\xC3\xA9" "e dans l'\xC3\xA9" "diteur");
        for (const auto& c : w.children()) visit(*c);
    };
    visit(*page);
    // Un onglet marque "modifie" (son point) sans rien de plus precis a dire.
    if (out.empty() && t->modified) out.emplace_back("des modifications pas encore enregistr\xC3\xA9" "es");
    return out;
}

std::size_t MainAnalysisScreen::closeTab(std::size_t index, bool askUnsaved) {
    return closeTabs({index}, askUnsaved);
}

std::size_t MainAnalysisScreen::closeAllTabs(bool askUnsaved) {
    std::vector<std::size_t> all;
    for (std::size_t i = 0; centre_ && i < centre_->tabCount(); ++i) all.push_back(i);
    return closeTabs(all, askUnsaved);
}

std::size_t MainAnalysisScreen::closeOtherTabs(std::size_t keep, bool askUnsaved) {
    if (!centre_ || keep >= centre_->tabCount()) return 0;
    std::vector<std::size_t> others;
    for (std::size_t i = 0; i < centre_->tabCount(); ++i)
        if (i != keep) others.push_back(i);
    // Celui qu'on garde devient l'onglet ouvert : c'est depuis lui qu'on a demande.
    return closeTabs(others, askUnsaved, centre_->page(keep));
}

std::size_t MainAnalysisScreen::closeCurrentTab() {
    if (!centre_ || centre_->tabCount() == 0) return 0;
    return closeTab(centre_->currentIndex());
}

std::size_t MainAnalysisScreen::closeTabs(const std::vector<std::size_t>& indices, bool askUnsaved, Widget* focus) {
    if (!centre_) return 0;
    // Par PAGE : chaque fermeture decale les indices des onglets de droite.
    std::vector<Widget*>     pages;
    std::vector<std::string> lost;         // "Macros - la macro X, modifiee dans l'editeur"
    std::size_t              unsavedTabs = 0;
    std::string              firstTitle;
    for (const auto i : indices) {
        if (!tabClosable(i)) continue;     // sans croix, ou fixe : il reste
        auto* page = centre_->page(i);
        if (!page || std::find(pages.begin(), pages.end(), page) != pages.end()) continue;
        pages.push_back(page);
        const std::string& tabTitle = centre_->tab(i)->title;
        if (firstTitle.empty()) firstTitle = tabTitle;
        if (!askUnsaved) continue;
        const auto what = unsavedInTab(i);
        if (!what.empty()) ++unsavedTabs;
        for (const auto& w : what) lost.push_back(tabTitle + " \xE2\x80\x94 " + w);
    }
    if (pages.empty()) return 0;
    if (lost.empty()) return closePages(pages, focus);

    // UNE question pour tous : fermer neuf onglets ne doit pas en poser deux,
    // et Annuler laisse TOUT ouvert (rien n'a encore change).
    const std::size_t n = pages.size();
    std::string text = n == 1             ? std::string("Cet onglet a")
                     : unsavedTabs == 1   ? std::string("Un des onglets a")
                                          : std::to_string(unsavedTabs) + " des onglets ont";
    text += " des modifications qui ne sont pas enregistr\xC3\xA9" "es :\n";
    for (std::size_t k = 0; k < lost.size() && k < 12; ++k) text += "  - " + lost[k] + "\n";
    if (lost.size() > 12) text += "  et " + std::to_string(lost.size() - 12) + " de plus\n";
    if (const std::size_t rest = n - unsavedTabs; rest > 0)
        text += rest == 1 ? "\nL'autre se ferme sans rien perdre : c'est une vue du projet, il reste tel quel."
                          : "\nLes " + std::to_string(rest) + " autres se ferment sans rien perdre : ce sont des vues du projet, il reste tel quel.";
    text += "\nPour les garder : Annuler, puis enregistre-les dans l'\xC3\xA9" "diteur (Ctrl+S).";
    const std::string title = n == 1 ? "Fermer l'onglet " + firstTitle : "Fermer " + std::to_string(n) + " onglets";
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>(title, text, MessageDialog::Icon::Question, "Fermer sans les garder"),
        [this, pages, focus](const menu::DialogResult& r) {
            if (r.accepted()) (void)closePages(pages, focus);
        });
    return 0;
}

std::size_t MainAnalysisScreen::closePages(const std::vector<Widget*>& pages, Widget* focus) {
    if (!centre_) return 0;
    std::size_t closed = 0;
    for (auto* page : pages) {
        // Retrouve a chaque tour (un pointeur compare, jamais suivi) : deja
        // ferme entre-temps, il est simplement passe.
        const int at = centre_->indexOf(page);
        if (at < 0) continue;
        const auto before = centre_->tabCount();
        closeDocument(static_cast<std::size_t>(at));
        if (centre_->tabCount() < before) ++closed;
    }
    if (const int at = focus ? centre_->indexOf(focus) : -1; at >= 0)
        centre_->setCurrentIndex(static_cast<std::size_t>(at));
    // Plusieurs d'un coup : la barre d'etat dit combien (un seul se voit partir).
    if (closed > 1 && status_)
        status_->setTransientMessage(std::to_string(closed) + " onglets ferm\xC3\xA9s", 5.0, StatusBar::Severity::Success);
    return closed;
}

void MainAnalysisScreen::showTabMenu(std::size_t index, gfx::Point at) {
    if (!tabMenu_ || !centre_ || index >= centre_->tabCount()) return;
    tabMenuPage_ = centre_->page(index);
    std::size_t closable = 0;
    for (std::size_t i = 0; i < centre_->tabCount(); ++i)
        if (tabClosable(i)) ++closable;
    const bool        self   = tabClosable(index);
    const std::size_t others = closable - (self ? 1u : 0u);
    const auto count = [](std::size_t n, const char* one, const char* many) {
        return std::to_string(n) + " " + (n > 1 ? many : one);
    };

    std::vector<PopupMenu::Item> items;
    PopupMenu::Item head;
    head.label   = centre_->tab(index)->title;      // l'onglet vise, pas forcement l'ouvert
    head.heading = true;
    items.push_back(std::move(head));
    const auto entry = [&items](std::string label, std::string hint, std::string why, bool enabled, int id) {
        PopupMenu::Item it;
        it.label          = std::move(label);
        it.shortcut       = std::move(hint);         // a droite, en gris : combien
        it.disabledReason = std::move(why);          // grise : pourquoi
        it.icon           = Icon::Close;
        it.enabled        = enabled;
        it.id             = id;
        items.push_back(std::move(it));
    };
    entry("Fermer", kCloseTabShortcut, self ? "" : "cet onglet ne se ferme pas", self, kTabMenuClose);
    entry("Fermer tout", count(closable, "onglet", "onglets"), "", closable > 0, kTabMenuCloseAll);
    entry("Fermer tout sauf celui-ci", others > 0 ? count(others, "autre", "autres") : std::string(),
          others > 0 ? "" : centre_->tabCount() > 1 ? "les autres ne se ferment pas" : "c'est le seul onglet",
          others > 0, kTabMenuCloseOthers);
    // Lot 7 : l'affichage multi-fenetre - cote a cote, la mosaique, une fenetre a lui.
    {
        PopupMenu::Item rule;
        rule.separator = true;
        items.push_back(rule);
        const bool mosaic = centre_->mode() == TabArea::Mode::Mosaic;
        const bool alone  = centre_->tabCount() < 2;
        const auto layoutEntry = [&items](std::string label, std::string hint, std::string why, bool enabled, Icon icon, int id) {
            PopupMenu::Item it;
            it.label = std::move(label);
            it.shortcut = std::move(hint);
            it.disabledReason = std::move(why);
            it.icon = icon;
            it.enabled = enabled;
            it.id = id;
            items.push_back(std::move(it));
        };
        layoutEntry("Diviser \xC3\xA0 droite", "", alone ? "il faut au moins deux onglets" : "", !alone, Icon::Layers, kTabMenuSplitRight);
        layoutEntry("Diviser en bas", "", alone ? "il faut au moins deux onglets" : "", !alone, Icon::Layers, kTabMenuSplitDown);
        const bool neighbour = centre_->neighbourGroup(index) != TabArea::npos;
        layoutEntry("D\xC3\xA9placer dans le groupe voisin", "", neighbour ? "" : "un seul groupe d'onglets", neighbour, Icon::Expand, kTabMenuNeighbour);
        layoutEntry(mosaic ? "Revenir aux onglets" : "Mosa\xC3\xAFque automatique", mosaic ? "" : std::to_string(centre_->tabCount()) + " tuiles",
                    "", true, Icon::Chart, kTabMenuMosaic);
        const bool canDetach = tabClosable(index);
        layoutEntry("D\xC3\xA9tacher dans une fen\xC3\xAAtre", "", canDetach ? "" : "cet onglet reste dans la fen\xC3\xAAtre principale",
                    canDetach, Icon::Screen, kTabMenuDetach);
    }
    tabMenu_->setItems(std::move(items));
    const auto surface = root().bounds();
    tabMenu_->openAt(at, {surface.w, surface.h});
    centre_->markTab(index, tabMenu_);      // entoure de tirets tant que le menu est ouvert
}

void MainAnalysisScreen::runTabMenuAction(int action) {
    if (!centre_) return;
    const int at = centre_->indexOf(tabMenuPage_);
    tabMenuPage_ = nullptr;
    if (at < 0) return;                     // ferme entre-temps : rien a faire
    const auto index = static_cast<std::size_t>(at);
    switch (action) {
        case kTabMenuClose:       (void)closeTab(index); break;
        case kTabMenuCloseAll:    (void)closeAllTabs(); break;
        case kTabMenuCloseOthers: (void)closeOtherTabs(index); break;
        case kTabMenuSplitRight:  (void)splitTab(index, false); break;
        case kTabMenuSplitDown:   (void)splitTab(index, true); break;
        case kTabMenuNeighbour:
            if (centre_->moveTabToNeighbour(index)) saveLayoutMode();
            break;
        case kTabMenuMosaic:
            (void)setLayoutMode(centre_->mode() == TabArea::Mode::Mosaic ? "onglets" : "mosaique");
            break;
        case kTabMenuDetach:      (void)detachTab(index); break;
        default: break;
    }
}

// =============================================================================
//  LOT 7 : L'AFFICHAGE MULTI-FENETRE
// -----------------------------------------------------------------------------
//  Le centre (TabArea) montre plusieurs onglets a la fois : en groupes cote a
//  cote ou empiles (on tire un en-tete sur le bord d'un groupe, ou Diviser a
//  droite / en bas), ou en mosaique automatique (une tuile par onglet, qui se
//  replacent a chaque ouverture). Un onglet part aussi dans une fenetre a lui
//  - un deuxieme ecran - et revient quand on la ferme. Le mode est retenu dans
//  les reglages (view.layoutMode).
// =============================================================================
bool MainAnalysisScreen::setLayoutMode(const std::string& name) {
    if (!centre_) return false;
    TabArea::Mode mode{};
    if (!TabArea::modeFromName(name, mode)) return false;
    // "Cote a cote" depuis un seul groupe : l'onglet ouvert part a droite -
    // sans quoi le mode ne changerait rien a l'ecran.
    if (mode == TabArea::Mode::Groups && centre_->groupCount() < 2 && centre_->tabCount() > 1) {
        if (centre_->mode() == TabArea::Mode::Mosaic) centre_->setMode(TabArea::Mode::Groups);
        (void)centre_->splitTab(centre_->currentIndex(), TabArea::Side::Right);
    } else {
        centre_->setMode(mode);
    }
    saveLayoutMode();
    if (status_) {
        const char* what = centre_->mode() == TabArea::Mode::Mosaic ? "mosa\xC3\xAFque : chaque onglet a sa tuile, qui se replacent seules"
                         : centre_->mode() == TabArea::Mode::Groups ? "groupes d'onglets c\xC3\xB4te \xC3\xA0 c\xC3\xB4te : tire un onglet sur un bord pour diviser"
                                                                    : "un seul groupe d'onglets";
        status_->setTransientMessage(std::string("Disposition : ") + what, 5.0, StatusBar::Severity::Info);
    }
    return true;
}

bool MainAnalysisScreen::splitTab(std::size_t index, bool below) {
    if (!centre_ || index >= centre_->tabCount()) return false;
    const bool ok = centre_->splitTab(index, below ? TabArea::Side::Bottom : TabArea::Side::Right);
    if (ok) saveLayoutMode();
    else if (status_) status_->setTransientMessage("Rien \xC3\xA0 diviser : cet onglet est seul dans son groupe.", 4.0, StatusBar::Severity::Warning);
    return ok;
}

void MainAnalysisScreen::saveLayoutMode() {
    if (!centre_ || restoring_) return;
    app_.settings().set(kLayoutModeKey, std::string(TabArea::modeName(centre_->mode())));
    (void)app_.settings().save();
}

// Un onglet dans une fenetre a lui. La page sort du centre (takeTab) mais
// GARDE SES ATTACHES : les onglets de l'API et de l'IHM la retrouvent par son
// pointeur (apiTabs_, hmiTabs_), et le volet Simulation continue d'etre anime.
// Ce qui la retenait par son INDICE (une section, un grafcet...) est note et
// remis a son nouvel indice quand elle revient.
bool MainAnalysisScreen::detachTab(std::size_t index) {
    if (!centre_ || index >= centre_->tabCount() || !tabClosable(index)) return false;
    const ui::TabControl::Tab meta = *centre_->tab(index);
    // Les attaches par indice, a remettre au retour.
    std::vector<domain::Index> sections;
    for (const auto& d : documents_)
        if (d.tab == index) sections.push_back(d.section);
    std::vector<std::pair<std::map<std::string, int>*, std::string>> named;
    for (auto* tabs : {&grafcetTabs_, &macroTabs_, &variableTabs_, &callTabs_})
        for (const auto& [name, at] : *tabs)
            if (at == static_cast<int>(index)) named.emplace_back(tabs, name);

    ui::WidgetPtr page = centre_->takeTab(index);
    if (!page) return false;
    ui::Widget* raw = page.get();
    auto& windows = app_.detachedWindows();
    const bool ok = windows.detach(meta.title, page,
        [this, meta, sections, named](ui::WidgetPtr back) {
            if (!back || !centre_) return;
            const auto at = centre_->addTab(meta, std::move(back));
            for (const auto s : sections) documents_.push_back(OpenDocument{s, at});
            for (const auto& [tabs, name] : named) (*tabs)[name] = static_cast<int>(at);
            centre_->setCurrentIndex(at);
            refreshDocumentList();
        });
    if (!ok) {
        // Pas de fenetre possible : la page n'a pas bouge, elle revient a sa place.
        const auto at = centre_->addTab(meta, std::move(page));
        if (at != index) (void)centre_->moveTab(at, index);
        centre_->setCurrentIndex(index);
        if (status_) status_->setTransientMessage("Impossible d'ouvrir une fen\xC3\xAAtre pour cet onglet.", 5.0, StatusBar::Severity::Warning);
        return false;
    }
    shiftTabIndices(index);
    refreshDocumentList();
    (void)raw;
    if (status_)
        status_->setTransientMessage("\xC2\xAB " + meta.title + " \xC2\xBB est dans sa fen\xC3\xAAtre ; la fermer le ram\xC3\xA8ne ici.", 6.0, StatusBar::Severity::Info);
    return true;
}

bool MainAnalysisScreen::showPage(ui::Widget* page) {
    if (!page || !centre_) return false;
    if (const int at = centre_->indexOf(page); at >= 0) {
        centre_->setCurrentIndex(static_cast<std::size_t>(at));
        return true;
    }
    auto& windows = app_.detachedWindows();
    if (windows.isDetached(page)) {
        windows.raise(page);
        return true;
    }
    return false;
}

void MainAnalysisScreen::refreshDocumentList() {
    auto project = app_.project();
    auto model = std::make_shared<DocumentListModel>();
    std::vector<std::string> rows;
    rows.reserve(documents_.size());
    for (const auto& d : documents_) {
        if (!project || d.section >= project->sections.size()) continue;
        const auto& s = project->sections[d.section];
        rows.push_back(std::string(project->strings.text(s.name)) + "   ["
                       + std::string(domain::toString(s.language)) + ", "
                       + std::to_string(s.lineCount) + (s.lineCount > 1 ? " lignes]" : " ligne]"));
    }
    if (rows.empty()) rows.emplace_back("Aucun document ouvert : choisir une section dans l'arbre");
    model->setEntries(std::move(rows));
    documentModel_ = model;
    documentList_->setModel(documentModel_);
}



// The state, spelled by the host so the workspace and the Simulation tab can
// never disagree, and the transport greyed to match it: Run is dead while it is
// already running, Step is dead while it is, Stop is dead when there is nothing
// to stop. A button that does nothing when you press it teaches you to distrust
// the row.
// The transport acts on the host directly, exactly as the Simulation tab's
// does. Routing one of them through the action registry and not the other is
// how two views of one simulation start disagreeing about what it is doing.
// (Une methode, et plus un morceau du lambda de la barre : le volet Simulation
// de l'IHM a les memes quatre boutons, et ils doivent faire la meme chose.)
void MainAnalysisScreen::runSimulationTransport(std::string_view id) {
    using State = SimulationHost::State;
    auto& sim = app_.simulation();
    // 1.10 : l'IHM seule - la memoire de l'automate simule, preparee sans cycle
    // (l'API reste arretee) : l'IHM y lit et y ecrit.
    if (id == "sim.prepare") {
        if (sim.attached()) return;
        if (std::string why; !attachSimulation(&why)) {
            status_->setTransientMessage("La m\xC3\xA9moire de l'automate simul\xC3\xA9 n'est pas pr\xC3\xAAte : " + SimulationPane::frenchMessage(why)
                                         + ". L'IHM tourne sans elle.", 10.0, StatusBar::Severity::Warning);
            return;
        }
        refreshSimulationIndicator();
        return;
    }
    // Lot API 7 : le programme a change depuis la preparation (une section,
    // un bloc mis a jour) : Simuler / Un cycle repartent du nouveau code.
    // ---- Lot API 8 : le moteur ---- plus de "detach puis attach" (une pause
    // repartait du cycle 0) : attach() fait la modification en ligne en pause
    // (cycle, valeurs, forcages gardes) et prepare le nouveau code arretee.
    const bool reprepare = sim.attached() && sim.stale() && sim.state() != SimulationHost::State::Running && (id == "sim.run" || id == "sim.step");
    if (!sim.attached() || reprepare) {
        // Nothing prepared yet: prepare it here rather than making the user
        // open the Simulation tab first to arm a button that is sitting right
        // in front of them. Lot API 7 : la meme preparation que l'onglet
        // (attachSimulation), et la raison d'un refus dite comme lui, en francais.
        if (std::string why; !attachSimulation(&why)) {
            status_->setTransientMessage("La simulation ne d\xC3\xA9marre pas : " + SimulationPane::frenchMessage(why) + ".", 10.0,
                                         StatusBar::Severity::Warning);
            return;
        }
    }
    if      (id == "sim.run")   sim.setState(State::Running);
    else if (id == "sim.pause") sim.setState(State::Paused);
    else if (id == "sim.stop")  sim.setState(State::Stopped);
    else                        sim.step();
    refreshSimulationIndicator();
    // 1.10 : les commandes de l'API ne touchent pas l'IHM - on dit ce qu'elle devient.
    if (app_.hmi() && status_) {
        const auto* hsim = dynamic_cast<const HmiSimulationPane*>(hmiTab("simulation"));
        const bool hmiOn = hsim && hsim->hmiRunning();
        const std::string api = id == "sim.run" ? "API d\xC3\xA9marr\xC3\xA9" "e" : id == "sim.pause" ? "API en pause"
                              : id == "sim.stop" ? "API arr\xC3\xAAt\xC3\xA9" "e (variables \xC3\xA0 leur valeur initiale)" : "Un cycle de l'API";
        status_->setTransientMessage(api + (hmiOn ? (id == "sim.step" ? " \xE2\x80\x94 l'IHM tourne : elle voit ce cycle et continue"
                                                                      : " \xE2\x80\x94 l'IHM continue")
                                                  : " \xE2\x80\x94 l'IHM reste arr\xC3\xAAt\xC3\xA9" "e"),
                                     4.0);
    }
}

void MainAnalysisScreen::refreshSimulationIndicator() {
    using State = SimulationHost::State;
    if (!topBar_) return;
    const auto& sim   = app_.simulation();
    const auto  state = sim.state();
    const bool  live  = sim.attached();

    // 1.10 : la pastille de l'IHM, a cote de l'API (pas d'IHM dans le projet : aucune).
    if (!app_.hmi()) topBar_->setHmi(TopBar::Hmi::None);
    else {
        const auto tab = hmiTabs_.find("simulation");   // aussi detache dans sa fenetre (hmiTab ne le voit pas)
        const auto* hsim = dynamic_cast<const HmiSimulationPane*>(tab != hmiTabs_.end() ? tab->second : nullptr);
        topBar_->setHmi(hsim && hsim->hmiRunning() ? TopBar::Hmi::Running : TopBar::Hmi::Stopped);
    }
    // Lot API 2 : le bloc Simulation de la barre - Simuler / Pause, Arreter, Un
    // cycle, et l'etat en clair avec le numero de cycle.
    topBar_->setSimulation(!live ? TopBar::Sim::Off
                           : state == State::Running ? TopBar::Sim::Running
                           : state == State::Paused  ? TopBar::Sim::Paused
                           : state == State::Halted  ? TopBar::Sim::Halted
                                                     : TopBar::Sim::Stopped,
                           live ? sim.scanCount() : 0);

    if (!live) { app_.setSimulationStatus({}); barHaltShown_.clear(); topBar_->setSimulationNote({}); return; }
    app_.setSimulationStatus(sim.statusLine());
    // Lot API 2 : cette mise a jour tourne a chaque image (le numero de cycle de
    // la barre). Le message d'arret n'est pose qu'une fois par arret : repose a
    // chaque image, son fondu repartait de zero et il ne se voyait jamais.
    if (state == State::Halted && !sim.haltMessage().empty()) {
        if (barHaltShown_ != sim.haltMessage()) {
            barHaltShown_ = sim.haltMessage();
            // Lot API 7 : en francais (le moteur parle anglais).
            status_->setTransientMessage("Simulation arr\xC3\xAAt\xC3\xA9" "e : " + SimulationPane::frenchMessage(sim.haltMessage()), 12.0);
        }
        topBar_->setSimulationNote("Arr\xC3\xAAt\xC3\xA9" "e sur un d\xC3\xA9" "faut : " + SimulationPane::frenchMessage(sim.haltMessage())
                                   + "\nArr\xC3\xAAter remet la simulation \xC3\xA0 z\xC3\xA9ro.");
    } else {
        barHaltShown_.clear();
        topBar_->setSimulationNote({});
    }
}


// ============================================================== GRAFCET ====
//
//  A chart is opened from the engine instance a reader double-clicked, not from
//  an index into a list: the instance name is what they can see, and it survives
//  the project being re-analysed while an index does not.

// A name from anywhere - the code, the variables table, the tree. If it is an
// instance of a block that a chart drives, the chart opens; if it is a plain
// BOOL, nothing happens and nothing should.
void MainAnalysisScreen::openGrafcetFor(const std::string& symbol) {
    if (symbol.empty()) return;
    auto project = app_.project();
    if (!project) return;

    // The root of a path: double-clicking inside Gc_ManuA.Runtime.Cycle is still
    // asking about Gc_ManuA.
    std::string root = symbol.substr(0, symbol.find('.'));
    if (const auto bracket = root.find('['); bracket != std::string::npos)
        root = root.substr(0, bracket);
    if (root.empty()) return;

    for (const auto& v : project->variables) {
        if (project->strings.text(v.name) != root) continue;
        if (v.type.fbTypeIndex == domain::kNoIndex) return;   // not a block instance
        openGrafcet(root);
        return;
    }
}

std::string MainAnalysisScreen::grafcetInstanceAt(NodeId node) const {
    auto project = app_.project();
    if (!project) return {};

    domain::EntityKind kind{};
    domain::Index index{};
    if (!entityForNode(node, kind, index)) return {};
    if (kind != domain::EntityKind::Variable || index >= project->variables.size()) return {};

    // An instance of the engine, whatever it is called. Matching on the Gc_
    // prefix would miss the ones that do not follow it - and in this project
    // two of seventeen do not.
    const auto& v = project->variables[index];
    if (v.type.fbTypeIndex == domain::kNoIndex
        || v.type.fbTypeIndex >= project->pous.size()) return {};
    return std::string(project->strings.text(v.name));
}

void MainAnalysisScreen::openGrafcet(const std::string& instanceName) {
    auto project = app_.project();
    if (!project || instanceName.empty()) return;

    // Already open: bring it forward. Opening a second tab on the same chart is
    // the sort of thing that looks like the click did nothing.
    if (const auto at = grafcetTabs_.find(instanceName); at != grafcetTabs_.end()) {
        if (at->second < static_cast<int>(centre_->tabCount())) {
            centre_->setCurrentIndex(static_cast<std::size_t>(at->second));
            return;
        }
        grafcetTabs_.erase(at);
    }

    const auto charts = grafcet::findCharts(*project);
    const auto found = std::find_if(charts.begin(), charts.end(),
                                    [&](const grafcet::Chart& c) {
                                        return c.instance == instanceName;
                                    });
    if (found == charts.end()) {
        // Say which instance and why, rather than doing nothing: a double-click
        // that produces silence is indistinguishable from one that missed.
        status_->setTransientMessage(
            instanceName + " ne fait tourner aucun grafcet de ce projet "
            "(aucune section SFC_ ne remplit ses tableaux Steps_/Trans_/Acts_)", 6.0);
        return;
    }

    // One pane: the drawing, the three tables that say the same thing in
    // numbers, and a line of totals. The pane owns the runtime pointer and hands
    // its ADDRESS to the models, so a simulation starting or stopping reaches
    // all four at once - four copies of the pointer would go out of step the
    // first time one of them was updated and another was not.
    // Le navigateur : tous les grafcets du programme, leurs comptes et leurs controles.
    std::vector<GrafcetInstanceModel::Entry> entries;
    for (const auto& c : charts) {
        GrafcetInstanceModel::Entry e;
        e.chart = c;
        if (c.section < project->sections.size())
            e.section = std::string(project->strings.text(project->sections[c.section].name));
        for (const auto& k : grafcet::runChecks(c, project.get(), &charts)) (k.severe ? e.severe : e.remarks)++;
        entries.push_back(std::move(e));
    }
    auto pane = std::make_unique<GrafcetPane>("grafcet." + instanceName, *found, std::move(entries));
    pane->setRuntime(app_.simulationRuntime());
    auto* raw = pane.get();

    const auto tab = centre_->addTab(
        TabControl::Tab{found->name, Icon::Section, /*closable=*/true, false},
        std::move(pane));
    grafcetTabs_[instanceName] = static_cast<int>(tab);
    centre_->setCurrentIndex(tab);

    // The runtime appears and disappears with the simulation, so the view is
    // told rather than left holding whatever was there when the tab opened.
    // Le signal est celui de l'application, il survit a l'onglet : l'onglet
    // ferme, `raw` ne designe plus rien - on ne le touche que s'il est encore la.
    paneLinks_ += app_.simulation().changed->connect([this, raw] {
        // (Detache dans une fenetre, il est encore la : il suit aussi.)
        if (!centre_ || (centre_->indexOf(raw) < 0 && !app_.detachedWindows().isDetached(raw))) return;
        raw->setRuntime(app_.simulationRuntime());
        raw->refresh();
    });

    // ---- the palette, and what it asks for ---------------------------------
    raw->palette().setActionSink([raw](core::ActionId id) {
        if      (id == "gc.select")        raw->view().setTool(GrafcetTool::Select);
        else if (id == "gc.addStep")       raw->view().setTool(GrafcetTool::AddStep);
        else if (id == "gc.addTransition") raw->view().setTool(GrafcetTool::AddTransition);
        else if (id == "gc.addAction")     raw->view().setTool(GrafcetTool::AddAction);
        else if (id == "gc.wire")          raw->view().setTool(GrafcetTool::Wire);
        else if (id == "gc.remove")        raw->view().setTool(GrafcetTool::Remove);
        else if (id == "gc.zoomIn")        raw->view().setZoom(raw->view().zoom() * 1.2f);
        else if (id == "gc.zoomOut")       raw->view().setZoom(raw->view().zoom() / 1.2f);
        else if (id == "gc.fit")           raw->view().fit();
    });
    // Les controles du grafcet, sur le dessin (1.10, R).
    raw->setChecks(grafcet::runChecks(*found, project.get(), &charts));
    paneLinks_ += raw->openRequested->connect([this](const std::string& other) { openGrafcet(other); });

    const auto chartSection = found->section;

    // Positions live beside the project, never in it. Losing the file costs
    // nothing: computeLayout takes over.
    raw->view().setPlacement(layoutFile_.find(found->name));
    paneLinks_ += raw->view().placementChanged->connect([this, raw, name = found->name] {
        layoutFile_.at(name) = raw->view().pendingPlacement();
        saveLayoutFile();
    });

    paneLinks_ += raw->editRequested->connect(
        [this, raw, chartSection](GrafcetPart part, int id) {
            editGrafcetPart(raw, chartSection, static_cast<int>(part), id);
        });
    paneLinks_ += raw->view().removeRequested->connect(
        [this, raw, chartSection](GrafcetPart part, int id) {
            removeGrafcetPart(raw, chartSection, static_cast<int>(part), id);
        });
    paneLinks_ += raw->view().insertRequested->connect(
        [this, raw, chartSection](GrafcetTool tool, int at) {
            insertGrafcetPart(raw, chartSection, static_cast<int>(tool), at);
        });
    paneLinks_ += raw->view().linkRequested->connect(
        [this, raw, chartSection](int transitionId, int stepId) {
            linkGrafcet(raw, chartSection, transitionId, stepId);
        });

    // ---- 1.10 (R2, decision 11 bis) : le volet Code sous le dessin ------------------
    //  Le meme editeur que les sections ST (DocumentPane) : memes fournisseurs (noms du
    //  programme, signatures, valeur au survol en simulation), plus les raccourcis du
    //  grafcet. Appliquer = la commande annulable de GrafcetEdit, puis la relecture du
    //  grafcet et des onglets ST ouverts ; en simulation, la modification en ligne.
    {
        auto document = app_.document();
        std::vector<std::string> chartNames;
        for (const auto& c : charts) chartNames.push_back(c.name);
        auto& editor = raw->codePanel().editor();
        editor.setCompletionProvider(
            [document, raw, chartNames](std::string_view prefix, std::vector<MultiLineText::Completion>& out) {
                if (!document) return;
                const auto& chart = raw->view().chart();
                const bool body = raw->codePanel().target() == GrafcetCodePanel::Target::Action
                               && chart.actionSection != domain::kNoIndex;
                for (const auto& sg : project::suggestionsFor(*document, body ? chart.actionSection : chart.section, prefix)) {
                    MultiLineText::Completion c;
                    c.text   = sg.text;
                    c.detail = sg.detail;
                    c.rank   = sg.rank;
                    c.insert = sg.insert;
                    c.caret  = sg.caret;
                    switch (sg.kind) {
                        case project::Suggestion::Kind::LocatedVariable: c.icon = Icon::LocatedVariable; break;
                        case project::Suggestion::Kind::Variable:        c.icon = Icon::Variable; break;
                        case project::Suggestion::Kind::FunctionBlock:
                        case project::Suggestion::Kind::Function:        c.icon = Icon::FunctionBlock; break;
                        case project::Suggestion::Kind::DerivedType:     c.icon = Icon::DerivedType; break;
                        case project::Suggestion::Kind::Type:            c.icon = Icon::Constant; break;
                        case project::Suggestion::Kind::Keyword:         c.icon = Icon::Section; break;
                    }
                    out.push_back(std::move(c));
                }
                grafcetCompletions(chart, chartNames, prefix, out);
            });
        editor.setSignatureProvider(
            [document](std::string_view name, MultiLineText::Signature& out) {
                // Les raccourcis du grafcet d'abord : ce ne sont pas des fonctions du programme.
                std::string upper(name);
                for (auto& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (upper == "ACTIF" || upper == "DUREE" || upper == "FIN" || upper == "FINI" || upper == "INIT") {
                    out.name = upper;
                    out.parameters = { upper == "FIN" ? "A<n> : l'action"
                                     : (upper == "FINI" || upper == "INIT") ? "le grafcet (son nom)"
                                     : "X<n> : l'\xC3\xA9tape" };
                    out.returns = upper == "DUREE" ? "TIME" : upper == "INIT" ? "(commande)" : "BOOL";
                    return true;
                }
                if (!document) return false;
                project::CallSignature sig;
                if (!project::signatureFor(*document, name, sig)) return false;
                out.name       = std::move(sig.name);
                out.parameters = std::move(sig.parameters);
                out.returns    = std::move(sig.returns);
                return true;
            });
        editor.setValueProvider(
            [this](std::string_view symbol, std::string& text) {
                auto* runtime = app_.simulationRuntime();
                if (!runtime) return false;
                sim::Value v;
                if (!runtime->get(symbol, v)) return false;
                text = std::string(symbol) + " = " + v.display();
                if (runtime->isForced(symbol)) text += "   (forc\xC3\xA9)";
                return true;
            });

        const std::string paneId = raw->id();
        auto awaitingOnline = std::make_shared<bool>(false);
        auto applyCode = std::make_shared<std::function<void()>>(
            [this, raw, chartSection, paneId, awaitingOnline] {
                auto& code = raw->codePanel();
                if (!code.dirty()) return;
                auto document2 = app_.document();
                if (!document2) return;
                const auto chart = grafcet::findChart(*document2, chartSection);
                const auto all = grafcet::findCharts(*document2);
                const int id = code.targetId();
                const bool isTransition = code.target() == GrafcetCodePanel::Target::Transition;
                const domain::Index written = isTransition ? chartSection : chart.actionSection;
                if (written == domain::kNoIndex || written >= document2->sections.size()) return;
                const std::string before = document2->sections[written].body;
                const std::string typed = code.text();
                // Les raccourcis tapes (FIN(A2), ACTIF(X3)...) partent dans la forme du programme.
                std::string text = grafcet::expandShortcuts(chart, typed, &all);
                if (isTransition) {
                    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
                    if (!text.empty() && text.back() == ';') text.pop_back();
                    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
                    if (text.empty()) {
                        code.setStatus("Une r\xC3\xA9" "ceptivit\xC3\xA9 vide ne franchirait jamais : \xC3\xA9" "cris FALSE si c'est voulu.",
                                       StatusBar::Severity::Error);
                        return;
                    }
                    app_.apply(std::make_unique<project::SetTransitionConditionCommand>(document2, chartSection, id, text),
                               /*refreshViews=*/false);
                } else {
                    if (!text.empty() && text.back() != '\n') text.push_back('\n');
                    app_.apply(std::make_unique<project::SetActionBodyCommand>(document2, chart.actionSection,
                                                                               chart.arrayPrefix, id, text),
                               /*refreshViews=*/false);
                }
                const std::string name = sectionNameIn(*document2, written);
                const auto changes = project::diffSectionText(written, name, before, document2->sections[written].body);
                // Ecrit : le volet n'est plus "modifie" ; la relecture qui suit y remet le
                // texte du programme (raccourcis developpes), comme l'onglet ST le montre.
                if (!changes.empty()) code.markApplied(code.text());
                refreshGrafcetPane(paneId, chartSection);
                auto* live = dynamic_cast<GrafcetPane*>(root().findById(paneId));
                if (!live) return;
                if (changes.empty()) {
                    live->codePanel().setStatus("Rien n'a \xC3\xA9t\xC3\xA9 \xC3\xA9" "crit : le programme a refus\xC3\xA9 ce texte "
                                                "(la barre d'\xC3\xA9tat dit pourquoi).", StatusBar::Severity::Warning);
                    return;
                }
                std::string said = "Appliqu\xC3\xA9 : " + std::to_string(changes.size()) + " ligne(s) de " + name
                                 + " r\xC3\xA9\xC3\xA9" "crite(s) ; les onglets ST ouverts suivent ; Ctrl+Z d\xC3\xA9" "fait.";
                if (typed != text && isTransition) said += " Raccourcis d\xC3\xA9velopp\xC3\xA9s dans le programme.";
                const auto state = app_.simulation().state();
                if (state == SimulationHost::State::Running || state == SimulationHost::State::Paused) {
                    *awaitingOnline = true;
                    said += " En simulation : modification en ligne\xE2\x80\xA6";
                }
                live->codePanel().setStatus(said, StatusBar::Severity::Success);
            });
        paneLinks_ += raw->codePanel().applyRequested->connect([applyCode] { (*applyCode)(); });
        // La modification en ligne de l'API (lot API 8) : le volet dit qu'elle est passee.
        paneLinks_ += app_.simulation().onlineChanged->connect([this, paneId, awaitingOnline] {
            if (!*awaitingOnline) return;
            *awaitingOnline = false;
            auto* live = dynamic_cast<GrafcetPane*>(root().findById(paneId));
            const auto& oc = app_.simulation().lastOnlineChange();
            if (!live || !oc) return;
            live->codePanel().setStatus(oc->failed ? "En simulation : " + oc->summary
                                                   : "Appliqu\xC3\xA9 en ligne : " + oc->summary,
                                        oc->failed ? StatusBar::Severity::Error : StatusBar::Severity::Success);
        });
        auto showFor = [this, raw, applyCode](GrafcetCodePanel::Target target, int id) {
            auto& code = raw->codePanel();
            // Un code modifie qu'on quitte pour un autre element est applique d'abord.
            if (code.dirty() && (code.target() != target || code.targetId() != id)) (*applyCode)();
            if (auto document3 = app_.document()) showCodeFor(*raw, *document3, target, id);
        };
        paneLinks_ += raw->view().transitionSelected->connect(
            [showFor](int id) { showFor(GrafcetCodePanel::Target::Transition, id); });
        paneLinks_ += raw->view().actionSelected->connect(
            [showFor](int id) { showFor(GrafcetCodePanel::Target::Action, id); });
        paneLinks_ += raw->view().stepSelected->connect(
            [showFor](int) { showFor(GrafcetCodePanel::Target::None, -1); });
        paneLinks_ += raw->view().nothingSelected->connect(
            [showFor] { showFor(GrafcetCodePanel::Target::None, -1); });
        paneLinks_ += raw->pickedInList->connect([showFor](GrafcetPart part, int id) {
            showFor(part == GrafcetPart::Transition ? GrafcetCodePanel::Target::Transition
                    : part == GrafcetPart::Action   ? GrafcetCodePanel::Target::Action
                                                    : GrafcetCodePanel::Target::None, id);
        });
    }
    paneLinks_ += raw->view().stepSelected->connect([this, raw](int stepId) {
        const auto& chartNow = raw->view().chart();
        const auto* step = chartNow.stepById(stepId);
        if (!step) return;
        std::string line = "X" + std::to_string(step->id);
        if (!step->name.empty()) line += " '" + step->name + "'";
        if (step->initial) line += "  initiale";
        if (step->isFinal) line += "  finale";
        for (const auto* a : chartNow.actionsOfStep(stepId))
            line += "   |  " + (a->name.empty() ? std::string("?") : a->name) + " : "
                  + grafcet::kindLabel(a->kind, a->delay);
        status_->setMessage(line);
    });

    const auto structural = found->countOf(grafcet::Diagnostic::Level::Structure);
    if (structural > 0)
        status_->setTransientMessage(
            found->name + " : " + std::to_string(structural)
                + " d\xC3\xA9" "faut(s) de structure : ce grafcet ne marche pas comme il est dessin\xC3\xA9 (onglet Contr\xC3\xB4les)", 8.0);
    else
        status_->setMessage(found->name + "  -  " + std::to_string(found->steps.size())
                            + " \xC3\xA9tapes, " + std::to_string(found->transitions.size())
                            + " transitions, " + std::to_string(found->actions.size())
                            + " actions");
}


// ---------------------------------------------------------------------------
//  The chart editors. Every one of them ends in a command on the stack, so
//  Ctrl+Z undoes a chart edit exactly as it undoes a typed character - and every
//  one of them re-reads the chart from the file afterwards, because the file is
//  the authority and the pane is a view of it.
// ---------------------------------------------------------------------------
namespace {
// The same completion the document editor has, for a code field in a dialog.
// One function, so an expression typed in a dialog gets exactly the names an
// expression typed in the section would.
ui::MultiLineText::CompletionProvider completionFor(
        const std::shared_ptr<const domain::Project>& document, domain::Index section) {
    return [document, section](std::string_view prefix,
                               std::vector<ui::MultiLineText::Completion>& out) {
        if (!document) return;
        for (const auto& s : project::suggestionsFor(*document, section, prefix)) {
            ui::MultiLineText::Completion c;
            c.text   = s.text;
            c.detail = s.detail;
            c.rank   = s.rank;
            out.push_back(std::move(c));
        }
    };
}

GrafcetPane* asPane(ui::Widget* widget) { return dynamic_cast<GrafcetPane*>(widget); }

// The pane, looked up by id rather than remembered.
//
// A dialog callback runs one or more frames after it was created, and in between
// the tab it belongs to can be gone - closed by the reader, or torn down by a
// rebind. Holding the pointer across that gap is how this crashed:
//
//     0xC0000005 reading 0xFFFFFFFFFFFFFFFF in GrafcetView::selectedPart
//
//  which is the same shape as the simulation runtime that outlived its screen.
//  A lookup costs a walk of the widget tree and cannot dangle.
GrafcetPane* livePane(ui::Widget& from, const std::string& paneId) {
    return dynamic_cast<GrafcetPane*>(from.findById(paneId));
}
} // namespace

// Re-reads the chart from the file into the pane, if the pane is still there.
// One place, so every editor refreshes the same way and none of them has to
// remember that the tab may have gone.
void MainAnalysisScreen::refreshGrafcetPane(const std::string& paneId, domain::Index section) {
    auto document = app_.document();
    if (!document) return;
    if (auto* pane = livePane(root(), paneId)) {
        pane->setChart(grafcet::findChart(*document, section));
        const auto all = grafcet::findCharts(*document);
        pane->setChecks(grafcet::runChecks(pane->view().chart(), document.get(), &all));
    }
    // The section's text changed, so the line counts in the sections table did
    // too. Cheap, and the alternative is a number that quietly goes stale.
    if (sections_) sections_->invalidate();
    if (treeModel_) treeModel_->refresh();

    // AND THE OPEN ST TAB, which is the part that was missing.
    //
    // A DocumentPane loads the section's text once, when the tab opens, and holds
    // its own copy after that. So an edit made from the chart landed in the file
    // and the tab kept showing what the file used to say - "I pressed Apply and
    // nothing changed" was exactly right, from where the reader was standing.
    refreshOpenDocuments();
}

// Re-reads every open document from the project. Called after any edit that
// rewrote a section from somewhere other than the editor itself.
void MainAnalysisScreen::refreshOpenDocuments() {
    auto project = app_.project();
    if (!project) return;
    for (const auto& open : documents_) {
        if (open.section >= project->sections.size()) continue;
        auto* widget = root().findById("analysis.doc." + std::to_string(open.section));
        auto* pane = dynamic_cast<DocumentPane*>(widget);
        if (!pane) continue;
        const auto& body = project->sections[open.section].body;
        // Only when it differs: setText moves the caret and drops the selection,
        // and doing that to a tab nobody edited would be a change of its own.
        if (pane->view().text() != body) {
            pane->view().setText(body);
            // 1.10 (R2) : un onglet ouvert EN MODIFICATION qui change sous les yeux
            // de son lecteur le dit (chaque frappe y est deja une commande : rien
            // de tape n'est perdu, Ctrl+Z defait l'un ou l'autre).
            if (pane->editToggle().checked())
                status_->setTransientMessage(
                    std::string(project->strings.text(project->sections[open.section].name))
                        + " (ouverte en modification) vient d'\xC3\xAAtre r\xC3\xA9\xC3\xA9" "crite par le grafcet : "
                          "son onglet montre le nouveau texte. Ctrl+Z d\xC3\xA9" "fait.",
                    8.0, StatusBar::Severity::Warning);
        }
    }
    // 1.10 (R2) : et les grafcets ouverts sur une section relue (Ctrl+Z d'une frappe ST).
    refreshGrafcetPanes(root(), grafcetTabs_, project.get(), domain::kNoIndex);
}

void MainAnalysisScreen::saveLayoutFile() {
    // The most recent path is the project that is open. There is no
    // currentPath() on App, and adding one for this would be a change to
    // everyone's header for one line here.
    const auto& recent = app_.recentPaths();
    if (recent.empty()) return;
    const auto& path = recent.front();
    // Best effort, and silent on failure: a layout is a convenience, and a modal
    // about a sidecar in the middle of dragging a box would be worse than losing
    // the position.
    std::ofstream out(project::LayoutFile::pathFor(path), std::ios::binary);
    if (out) out << layoutFile_.serialise();
}

// 1.10 (R2, decision 11 bis) : les dialogues du grafcet en francais. La receptivite
// et le corps d'une action s'ecrivent dans le volet Code (sous le dessin) ; ici :
// les extremites d'une transition, le genre / la duree / le nom / l'etape d'une
// action (avec l'apercu des lignes du ST qui changent), le nom d'une etape.
void MainAnalysisScreen::editGrafcetPart(ui::Widget* widget, domain::Index section,
                                         int partValue, int id) {
    auto* pane = asPane(widget);
    auto document = app_.document();
    if (!pane || !document) return;
    const std::string paneId = pane->id();
    const auto part = static_cast<GrafcetPart>(partValue);
    const auto chart = grafcet::findChart(*document, section);

    std::vector<std::string> stepNames;
    for (const auto& st : chart.steps)
        stepNames.push_back("X" + std::to_string(st.id)
                            + (st.name.empty() || st.name == "X" + std::to_string(st.id) ? "" : " " + st.name));
    auto nameOfStep = [&](int stepId) {
        for (const auto& n : stepNames)
            if (std::atoi(n.c_str() + 1) == stepId) return n;
        return stepNames.empty() ? std::string{} : stepNames.front();
    };

    if (part == GrafcetPart::Transition) {
        const grafcet::Transition* target = grafcet::transitionById(chart, id);
        if (!target) return;
        std::vector<FormDialog::Field> fields;
        fields.push_back({"De (\xC3\xA9tape amont)", nameOfStep(target->sources.empty() ? -1 : target->sources[0]),
                          "", false, stepNames});
        fields.push_back({"Vers (\xC3\xA9tape aval)", nameOfStep(target->destinations.empty() ? -1 : target->destinations[0]),
                          "", false, stepNames});
        app_.menus().ShowDialog(
            std::make_unique<FormDialog>("dialog.gcCondition", "Transition T" + std::to_string(id),
                "Changer De ou Vers ne d\xC3\xA9place que cette extr\xC3\xA9mit\xC3\xA9 : la r\xC3\xA9" "ceptivit\xC3\xA9 "
                "et le retard restent o\xC3\xB9 ils sont.\n"
                "La r\xC3\xA9" "ceptivit\xC3\xA9 s'\xC3\xA9" "crit dans le volet Code, sous le dessin (aide \xC3\xA0 la saisie, "
                "raccourcis FIN(A2), ACTIF(X3)\xE2\x80\xA6).",
                fields, "Appliquer"),
            [this, paneId, section, id, was = *target](const menu::DialogResult& r) {
                if (!r.accepted()) return;
                auto values = FormDialog::split(r.payload);
                if (values.size() < 2) return;
                auto document2 = app_.document();
                if (!document2) return;
                const int from = std::atoi(values[0].c_str() + 1);
                const int to   = std::atoi(values[1].c_str() + 1);
                if (was.sources != std::vector<int>{from} || was.destinations != std::vector<int>{to})
                    app_.apply(std::make_unique<project::SetTransitionEndpointsCommand>(
                                   document2, section, id, std::vector<int>{from}, std::vector<int>{to}),
                               /*refreshViews=*/false);
                refreshGrafcetPane(paneId, section);
            });
        return;
    }

    if (part == GrafcetPart::Action) {
        const grafcet::Action* target = grafcet::actionById(chart, id);
        if (!target) return;
        std::vector<std::string> kinds;
        int current = 0;
        const auto all = grafcet::allKinds();
        for (std::size_t i = 0; i < all.size(); ++i) {
            kinds.push_back(std::string(grafcet::kindQualifier(all[i])) + " - " + grafcet::kindLabel(all[i])
                            + " (" + grafcet::kindCode(all[i]) + ")");
            if (all[i] == target->kind) current = static_cast<int>(i);
        }
        std::vector<std::string> stepIds;
        for (const auto& st : chart.steps) stepIds.push_back("X" + std::to_string(st.id));
        std::vector<FormDialog::Field> fields;
        fields.push_back({"Nom", target->name, "8 caract\xC3\xA8res au plus", false, {}});
        fields.push_back({"Genre", kinds.empty() ? std::string{} : kinds[static_cast<std::size_t>(current)], "", false, kinds});
        fields.push_back({"Dur\xC3\xA9" "e", target->delay, "t#5s - pour les genres retard\xC3\xA9s, limit\xC3\xA9s, impulsions", false, {}});
        fields.push_back({"\xC3\x89tape", "X" + std::to_string(target->boundStep), "", false, stepIds});
        auto dialog = std::make_unique<FormDialog>("dialog.gcAction",
            "Action A" + std::to_string(id) + (target->name.empty() ? "" : " (" + target->name + ")"),
            std::string("Aujourd'hui : ") + grafcet::kindLabel(target->kind, target->delay) + " - "
                + std::string(grafcet::kindMeaning(target->kind)) + "\n"
                "Seuls les champs chang\xC3\xA9s sont r\xC3\xA9\xC3\xA9" "crits dans le programme. Son corps s'\xC3\xA9" "crit "
                "dans le volet Code, sous le dessin.",
            fields, "Appliquer");
        dialog->setCodeFields({2});
        dialog->setCompletionProvider(completionFor(document, section));
        app_.menus().ShowDialog(
            std::move(dialog),
            [this, paneId, section, id, was = *target, kinds, all](const menu::DialogResult& r) {
                if (!r.accepted()) return;
                auto values = FormDialog::split(r.payload);
                if (values.size() < 4) return;
                auto document2 = app_.document();
                if (!document2) return;
                auto change = project::ActionChange::from(was);
                change.name = values[0];
                for (std::size_t i = 0; i < kinds.size() && i < all.size(); ++i)
                    if (kinds[i] == values[1]) change.kind = all[i];
                change.delay = values[2];
                change.boundStep = std::atoi(values[3].c_str() + 1);
                project::ModifyActionCommand probe(document2, section, id, change);
                std::string error;
                const auto lines = probe.preview(&error);
                if (!error.empty()) {
                    app_.menus().ShowDialog(std::make_unique<MessageDialog>("Action A" + std::to_string(id), error,
                                                                            MessageDialog::Icon::Warning),
                                            [](const menu::DialogResult&) {});
                    return;
                }
                if (lines.empty()) { status_->setTransientMessage("Rien ne change dans le programme.", 4.0); return; }
                auto apply = [this, paneId, section, id, change] {
                    auto document3 = app_.document();
                    if (!document3) return;
                    app_.apply(std::make_unique<project::ModifyActionCommand>(document3, section, id, change),
                               /*refreshViews=*/false);
                    refreshGrafcetPane(paneId, section);
                };
                if (lines.size() <= 1) { apply(); return; }
                app_.menus().ShowDialog(
                    std::make_unique<MessageDialog>("Ce qui va changer dans le programme",
                        changeListText(lines) + "\nCtrl+Z d\xC3\xA9" "fait tout d'un coup.",
                        MessageDialog::Icon::Question, "Appliquer"),
                    [apply](const menu::DialogResult& r2) { if (r2.accepted()) apply(); });
            });
        return;
    }

    // Une etape : son nom et ses deux reperes, dans le descripteur. Le numero ne se
    // change pas ici : ce serait renumeroter le grafcet (inserer et supprimer).
    const auto* step = chart.stepById(id);
    if (!step) return;
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", step->name, "4 caract\xC3\xA8res au plus", false, {}});
    fields.push_back({"Initiale", step->initial ? "oui" : "non", "", false, {"non", "oui"}});
    fields.push_back({"Finale", step->isFinal ? "oui" : "non", "", false, {"non", "oui"}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.gcStepEdit", "\xC3\x89tape X" + std::to_string(id),
            "Le moteur garde le nom d'une \xC3\xA9tape dans un string[4] et JETTE un nom plus long "
            "(l'automate n'aurait plus de nom du tout).\n"
            "Le num\xC3\xA9ro ne se change pas ici : ce serait renum\xC3\xA9roter le grafcet.",
            fields, "Appliquer"),
        [this, paneId, section, id](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto values = FormDialog::split(r.payload);
            if (values.size() < 3) return;
            auto document2 = app_.document();
            if (!document2) return;
            app_.apply(std::make_unique<project::RenameStepCommand>(
                           document2, section, id, values[0], values[1] == "oui", values[2] == "oui"),
                       /*refreshViews=*/false);
            refreshGrafcetPane(paneId, section);
        });
}

// Supprimer (outil Supprimer, touche Suppr) : une etape, une transition, une action.
// Toujours demande, avec ce qui va changer dans le programme ; refuse avec la raison
// quand la suppression casserait autre chose (une action employee).
void MainAnalysisScreen::removeGrafcetPart(ui::Widget* widget, domain::Index section,
                                           int partValue, int id) {
    auto* pane = asPane(widget);
    auto document = app_.document();
    if (!pane || !document) return;
    const std::string paneId = pane->id();
    const auto part = static_cast<GrafcetPart>(partValue);

    auto refused = [this](const std::string& title, const std::string& why) {
        app_.menus().ShowDialog(std::make_unique<MessageDialog>(title, why, MessageDialog::Icon::Warning),
                                [](const menu::DialogResult&) {});
    };

    if (part == GrafcetPart::Transition) {
        project::RemoveTransitionCommand probe(document, section, id);
        std::string error;
        const auto lines = probe.preview(&error);
        if (!error.empty()) { refused("Supprimer T" + std::to_string(id), error); return; }
        std::string message = "Ses deux appels Builder et sa r\xC3\xA9" "ceptivit\xC3\xA9 partent ; les transitions "
                              "suivantes descendent d'un num\xC3\xA9ro.\n";
        for (const auto& c : probe.consequences()) message += "Attention : " + c + "\n";
        message += "\nCe qui va changer dans le programme (" + std::to_string(lines.size()) + " ligne(s)) :\n"
                 + changeListText(lines) + "\nCtrl+Z d\xC3\xA9" "fait.";
        app_.menus().ShowDialog(
            std::make_unique<MessageDialog>("Supprimer la transition T" + std::to_string(id) + " ?", message,
                                            MessageDialog::Icon::Question, "Supprimer"),
            [this, paneId, section, id](const menu::DialogResult& r) {
                if (!r.accepted()) return;
                auto document2 = app_.document();
                if (!document2) return;
                app_.apply(std::make_unique<project::RemoveTransitionCommand>(document2, section, id),
                           /*refreshViews=*/false);
                refreshGrafcetPane(paneId, section);
            });
        return;
    }

    if (part == GrafcetPart::Action) {
        project::RemoveActionCommand probe(document, section, id);
        std::string error;
        const auto lines = probe.preview(&error);
        if (!error.empty()) { refused("Supprimer A" + std::to_string(id), error); return; }
        std::string message = "Ses appels Builder, sa condition et son bloc IF \xE2\x80\xA6 END_IF partent ; les actions "
                              "suivantes descendent d'un num\xC3\xA9ro, partout dans l'unit\xC3\xA9 (r\xC3\xA9" "ceptivit\xC3\xA9s, SFC_DEBUG).\n"
                              "\nCe qui va changer dans le programme (" + std::to_string(lines.size()) + " ligne(s)) :\n"
                            + changeListText(lines) + "\nCtrl+Z d\xC3\xA9" "fait.";
        app_.menus().ShowDialog(
            std::make_unique<MessageDialog>("Supprimer l'action A" + std::to_string(id) + " ?", message,
                                            MessageDialog::Icon::Question, "Supprimer"),
            [this, paneId, section, id](const menu::DialogResult& r) {
                if (!r.accepted()) return;
                auto document2 = app_.document();
                if (!document2) return;
                app_.apply(std::make_unique<project::RemoveActionCommand>(document2, section, id),
                           /*refreshViews=*/false);
                refreshGrafcetPane(paneId, section);
            });
        return;
    }

    if (part != GrafcetPart::Step) return;
    project::RemoveStepCommand probe(document, section, id, true);
    const auto damage = probe.orphaned();
    std::string message = "Les \xC3\xA9tapes au-dessus de X" + std::to_string(id)
                        + " peuvent garder leur num\xC3\xA9ro, ou descendre d'un cran.\n\n";
    if (!damage.empty()) {
        message += "Cette \xC3\xA9tape est cit\xC3\xA9" "e par :\n";
        for (const auto& line : damage) message += "  - " + line + "\n";
        message += "\nCe n'est pas r\xC3\xA9par\xC3\xA9 en silence : une transition re-point\xC3\xA9" "e sur l'\xC3\xA9tape voisine "
                   "se d\xC3\xA9" "couvre sur la machine.\n\n";
    }
    message += "Supprimer et renum\xC3\xA9roter les \xC3\xA9tapes au-dessus (le grafcet reste continu, "
               "comme les tableaux du moteur le veulent ; SFC_DEBUG suit) ?";
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Supprimer l'\xC3\xA9tape X" + std::to_string(id) + " ?", message,
                                        MessageDialog::Icon::Question, "Supprimer"),
        [this, paneId, section, id](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            auto document2 = app_.document();
            if (!document2) return;
            app_.apply(std::make_unique<project::RemoveStepCommand>(document2, section, id, true),
                       /*refreshViews=*/false);
            refreshGrafcetPane(paneId, section);
        });
}

void MainAnalysisScreen::insertGrafcetPart(ui::Widget* widget, domain::Index section,
                                           int toolValue, int at) {
    auto* pane = asPane(widget);
    auto document = app_.document();
    if (!pane || !document) return;
    const std::string paneId = pane->id();
    const auto tool = static_cast<GrafcetTool>(toolValue);
    const auto chart = grafcet::findChart(*document, section);

    if (tool == GrafcetTool::AddStep) {
        const int where = at >= 0 ? at : static_cast<int>(chart.steps.size());
        std::vector<FormDialog::Field> fields;
        fields.push_back({"Nom", "", "4 caract\xC3\xA8res au plus", false, {}});
        fields.push_back({"Initiale", "non", "", false, {"non", "oui"}});
        fields.push_back({"Finale", "non", "", false, {"non", "oui"}});
        app_.menus().ShowDialog(
            std::make_unique<FormDialog>("dialog.gcStep", "Nouvelle \xC3\xA9tape X" + std::to_string(where),
                "Le moteur garde le nom d'une \xC3\xA9tape dans un string[4] et JETTE un nom plus long.\n"
                "Les \xC3\xA9tapes \xC3\xA0 partir de X" + std::to_string(where) + " montent d'un num\xC3\xA9ro, partout dans "
                "l'unit\xC3\xA9 (transitions, actions, SFC_DEBUG).",
                fields, "Ins\xC3\xA9rer"),
            [this, paneId, section, where](const menu::DialogResult& r) {
                if (!r.accepted()) return;
                auto values = FormDialog::split(r.payload);
                if (values.size() < 3) return;
                auto document2 = app_.document();
                if (!document2) return;
                app_.apply(std::make_unique<project::InsertStepCommand>(
                               document2, section, where, values[0], values[1] == "oui", values[2] == "oui"),
                           /*refreshViews=*/false);
                refreshGrafcetPane(paneId, section);
            });
        return;
    }

    if (tool == GrafcetTool::AddTransition) {
        if (chart.steps.size() < 2) {
            status_->setTransientMessage("Une transition relie deux \xC3\xA9tapes : il en faut deux.", 5.0);
            return;
        }
        std::vector<std::string> stepNames;
        for (const auto& st : chart.steps) stepNames.push_back("X" + std::to_string(st.id));
        std::vector<FormDialog::Field> fields;
        fields.push_back({"De", stepNames.front(), "", false, stepNames});
        fields.push_back({"Vers", stepNames[1], "", false, stepNames});
        fields.push_back({"Libell\xC3\xA9", "", "8 caract\xC3\xA8res au plus, sans '|'", false, {}});
        fields.push_back({"R\xC3\xA9" "ceptivit\xC3\xA9", "FALSE", "une expression ST", false, {}});
        auto dialog = std::make_unique<FormDialog>("dialog.gcTrans", "Nouvelle transition",
            "Elle est \xC3\xA9" "crite avec sa r\xC3\xA9" "ceptivit\xC3\xA9 : sans elle, elle ne franchirait jamais.",
            fields, "Ins\xC3\xA9rer");
        dialog->setCodeFields({3});
        dialog->setCompletionProvider(completionFor(document, section));
        app_.menus().ShowDialog(
            std::move(dialog),
            [this, paneId, section, chart](const menu::DialogResult& r) {
                if (!r.accepted()) return;
                auto values = FormDialog::split(r.payload);
                if (values.size() < 4) return;
                auto document2 = app_.document();
                if (!document2) return;
                const auto all = grafcet::findCharts(*document2);
                app_.apply(std::make_unique<project::InsertTransitionCommand>(
                               document2, section, static_cast<int>(chart.transitions.size()),
                               std::vector<int>{std::atoi(values[0].c_str() + 1)},
                               std::vector<int>{std::atoi(values[1].c_str() + 1)},
                               values[2], grafcet::expandShortcuts(chart, values[3], &all)),
                           /*refreshViews=*/false);
                refreshGrafcetPane(paneId, section);
            });
        return;
    }

    if (tool == GrafcetTool::AddAction) {
        if (chart.steps.empty()) return;
        std::vector<std::string> stepNames, kinds;
        for (const auto& st : chart.steps) stepNames.push_back("X" + std::to_string(st.id));
        const auto all = grafcet::allKinds();
        for (const auto k : all)
            kinds.push_back(std::string(grafcet::kindQualifier(k)) + " - " + grafcet::kindLabel(k)
                            + " (" + grafcet::kindCode(k) + ")");
        const int boundDefault = at >= 0 ? at : chart.steps.front().id;
        std::vector<FormDialog::Field> fields;
        fields.push_back({"Nom", "", "8 caract\xC3\xA8res au plus", false, {}});
        fields.push_back({"\xC3\x89tape", "X" + std::to_string(boundDefault), "", false, stepNames});
        fields.push_back({"Genre", kinds.front(), "", false, kinds});
        fields.push_back({"Dur\xC3\xA9" "e", "t#0s", "pour les genres retard\xC3\xA9s, limit\xC3\xA9s, impulsions", false, {}});
        fields.push_back({"Corps", "", "les instructions dans IF \xE2\x80\xA6 .Out THEN", false, {}});
        auto dialog = std::make_unique<FormDialog>("dialog.gcAction2", "Nouvelle action",
            "Le corps va dans SFC_<grafcet>_Actions, avec deux lignes de commentaire au-dessus : "
            "l'\xC3\xA9tape qui la porte et quand elle agit.",
            fields, "Ins\xC3\xA9rer");
        dialog->setCodeFields({3, 4});
        dialog->setCompletionProvider(completionFor(document, section));
        app_.menus().ShowDialog(
            std::move(dialog),
            [this, paneId, section, chart, kinds, all](const menu::DialogResult& r) {
                if (!r.accepted()) return;
                auto values = FormDialog::split(r.payload);
                if (values.size() < 5) return;
                auto kind = grafcet::ActionKind::Continuous;
                for (std::size_t i = 0; i < kinds.size() && i < all.size(); ++i)
                    if (kinds[i] == values[2]) kind = all[i];
                auto document2 = app_.document();
                if (!document2) return;
                app_.apply(std::make_unique<project::InsertActionCommand>(
                               document2, section, static_cast<int>(chart.actions.size()), values[0], kind,
                               std::atoi(values[1].c_str() + 1),
                               values[4].empty() ? std::string{} : values[4] + "\n", values[3]),
                           /*refreshViews=*/false);
                refreshGrafcetPane(paneId, section);
            });
    }
}

void MainAnalysisScreen::linkGrafcet(ui::Widget* widget, domain::Index section,
                                     int transitionId, int stepId) {
    auto* pane = asPane(widget);
    auto document = app_.document();
    if (!pane || !document) return;
    const std::string paneId = pane->id();

    const auto chart = grafcet::findChart(*document, section);
    const grafcet::Transition* target = grafcet::transitionById(chart, transitionId);
    if (!target) return;

    auto listOf = [](const std::vector<int>& ids) {
        std::string out;
        for (std::size_t i = 0; i < ids.size(); ++i)
            out += (i ? ", " : "") + ("X" + std::to_string(ids[i]));
        return out.empty() ? std::string("-") : out;
    };

    std::vector<FormDialog::Field> fields;
    fields.push_back({"X" + std::to_string(stepId) + " devient", "l'\xC3\xA9tape aval", "",
                      false, {"l'\xC3\xA9tape aval", "l'\xC3\xA9tape amont"}});
    fields.push_back({"Celle qui y est", "la remplacer", "", false, {"la remplacer", "s'y ajouter"}});

    auto dialog = std::make_unique<FormDialog>(
        "dialog.gcWire",
        "T" + std::to_string(transitionId) + "  et  X" + std::to_string(stepId),
        "T" + std::to_string(transitionId) + " va de " + listOf(target->sources)
            + " \xC3\xA0 " + listOf(target->destinations) + ".\n"
            "Ajouter une \xC3\xA9tape amont fait une convergence en ET : toutes doivent \xC3\xAAtre actives "
            "pour franchir.\n"
            "Ajouter une \xC3\xA9tape aval fait une divergence en ET, que ce moteur ne sait pas jouer "
            "(Builder \xC3\xA9" "crit toujours SplitKind=0 : seule la premi\xC3\xA8re est activ\xC3\xA9" "e). "
            "Pour une divergence, une transition par branche.",
        fields, "Relier");

    app_.menus().ShowDialog(
        std::move(dialog),
        [this, paneId, section, transitionId, stepId,
         sources = target->sources, destinations = target->destinations]
        (const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto values = FormDialog::split(r.payload);
            if (values.size() < 2) return;
            auto document2 = app_.document();
            if (!document2) return;

            const bool asDestination = values[0] == "l'\xC3\xA9tape aval";
            const bool replace       = values[1] == "la remplacer";

            auto next = asDestination ? destinations : sources;
            if (replace) next.assign(1, stepId);
            else if (std::find(next.begin(), next.end(), stepId) == next.end())
                next.push_back(stepId);

            app_.apply(std::make_unique<project::SetTransitionEndpointsCommand>(
                           document2, section, transitionId,
                           asDestination ? sources : next,
                           asDestination ? next : destinations),
                       /*refreshViews=*/false);
            refreshGrafcetPane(paneId, section);
        });
}


// ================================================================== MACROS ====
//
//  LOT MACROS 1. Lancer une macro, c'est l'onglet Macros (MacrosPane, et ses
//  dialogues dans MacrosWorkspace.cpp) : la fiche, le formulaire type,
//  l'apercu en direct, Appliquer = une commande. Ici : le code d'une macro
//  dans un onglet, et la liste de l'arbre.

std::string MainAnalysisScreen::macroSourceOf(const std::string& name) const {
    project::SharedLibrary library(project::SharedLibrary::defaultRoot());
    if (!library.scan()) return {};
    return library.macroSource(name);
}

void MainAnalysisScreen::refreshMacroList() {
    project::SharedLibrary library(project::SharedLibrary::defaultRoot());
    if (!library.scan()) return;
    // Written only when missing, so a file somebody corrected survives.
    (void)library.ensureDefaultMacros();

    // Lot macros 1 : rangees comme dans l'onglet Macros (dossiers.txt, sinon
    // leur ligne "#! categorie").
    std::vector<std::pair<std::string, std::string>> known;
    for (const auto& item : library.items()) {
        if (item.kind != project::LibraryItemKind::Macro) continue;
        std::ifstream in(item.path, std::ios::binary);
        std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        known.emplace_back(item.name, project::macro::parseMacroSpec(source, item.name).category);
    }
    project::macro::MacroFolders folders(library.macroFoldersFile());
    (void)folders.load();
    folders.setMacros(std::move(known));
    const auto layout = folders.arrange();
    std::vector<std::pair<std::string, std::string>> macros;
    for (const auto& m : layout.macros) macros.emplace_back(m.name, m.folder);
    if (treeModel_) treeModel_->setMacroTree(layout.folders, std::move(macros));
    if (explorer_) explorer_->invalidate();
}

// Le code d'une macro. Le lire avant de le lancer : un script qui modifie le
// projet est un script qu'on doit pouvoir lire, et corriger.
// Lot API 6 : "Modifier le code" - le mode Modifier, dans l'onglet Macros
// (Utiliser / Modifier dans le meme onglet). L'onglet "Macro X" d'avant n'existe
// plus : il y menait.
void MainAnalysisScreen::openMacro(const std::string& name) {
    openMacros(name);
    if (auto* pane = macrosPane()) pane->edit(name);
}

std::unique_ptr<MacroEditorView> MainAnalysisScreen::makeMacroEditor(const std::string& name) {
    const auto source = macroSourceOf(name);
    if (source.empty()) {
        status_->setTransientMessage("la macro '" + name + "' est introuvable ou vide", 6.0);
        return nullptr;
    }

    // Lot API 6 : le mode Modifier - le code, les questions en cartes, l'apercu
    // du formulaire, Essayer (F5), Enregistrer (la version suivante).
    auto editor = std::make_unique<MacroEditorView>("analysis.macro." + name, name, source);
    auto* raw = editor.get();
    MacroEditorView::Hosts h;
    h.project = [this] { return app_.document(); };
    h.libsRoot = macrosPane() ? macrosPane()->libsRoot() : project::SharedLibrary::defaultRoot();
    h.save = [this](const std::string& macro, const std::string& text) {
        project::SharedLibrary library(project::SharedLibrary::defaultRoot());
        (void)library.scan();
        auto st = library.publishMacro(macro, text, "Macros");
        if (!st) {
            status_->setTransientMessage("\xC3\xA9" "chec de l'\xC3\xA9" "criture : " + st.error().message(), 8.0);
            return false;
        }
        macrosChanged(macro);
        return true;
    };
    h.use = [this](const std::string& macro, const std::string& text) { runMacroSource(macro, text); };
    h.showInMacros = [this](const std::string& macro) {
        if (auto* pane = macrosPane()) pane->use();
        openMacros(macro);
    };
    h.help = [this, name] { openMacroHelp(name); };
    h.status = [this](const std::string& text, bool error) {
        if (status_) status_->setTransientMessage(text, error ? 8.0 : 5.0, error ? StatusBar::Severity::Warning : StatusBar::Severity::Success);
    };
    h.askSave = [this, name](const std::string& current, const std::string& next,
                             std::function<void(const std::string&, const std::string&)> done) {
        std::vector<FormDialog::Field> fields;
        fields.push_back({"Version", next, current.empty() ? std::string("la premi\xC3\xA8re") : current + " \xE2\x86\x92 " + next, false, {}});
        fields.push_back({"Ce qui change", "", "une phrase : elle va dans #! changes et dans l'aide", false, {}});
        app_.menus().ShowDialog(std::make_unique<FormDialog>("dialog.macroSave", "Enregistrer " + name,
                                                             "Les lignes #! version et #! changes sont \xC3\xA9" "crites dans l'en-t\xC3\xAA" "te, puis le fichier dans libs/Macros. "
                                                             "Ctrl+Z dans l'\xC3\xA9" "diteur les reprend jusqu'\xC3\xA0 la fermeture de l'onglet.",
                                                             std::move(fields), "Enregistrer"),
                                [done](const menu::DialogResult& r) {
                                    if (!r.accepted()) return;
                                    const auto v = FormDialog::split(r.payload);
                                    done(v.empty() ? std::string{} : v[0], v.size() > 1 ? v[1] : std::string{});
                                });
    };
    h.askQuestion = [this](std::function<void(const std::string&, const std::string&, const std::string&)> done) {
        std::vector<FormDialog::Field> fields;
        fields.push_back({"Cl\xC3\xA9", "nouvelle", "le nom de la r\xC3\xA9ponse dans le code : Ask('cle', ...)", false, {}});
        std::vector<std::string> kinds;
        for (int i = 0; i <= static_cast<int>(project::macro::FieldKind::Checks); ++i)
            kinds.emplace_back(project::macro::kindKey(static_cast<project::macro::FieldKind>(i)));
        fields.push_back({"Genre", "texte", "", false, kinds});
        fields.push_back({"Libell\xC3\xA9", "", "ce que lira celui qui lance la macro", false, {}});
        app_.menus().ShowDialog(std::make_unique<FormDialog>("dialog.macroQuestion", "Ajouter une question",
                                                             "\xC3\x89" "crit #! champ et #! libelle dans l'en-t\xC3\xAA" "te, et l'appel Ask apr\xC3\xA8s le dernier du code.",
                                                             std::move(fields), "Ajouter"),
                                [done](const menu::DialogResult& r) {
                                    if (!r.accepted()) return;
                                    const auto v = FormDialog::split(r.payload);
                                    if (v.size() >= 3) done(v[0], v[1], v[2]);
                                });
    };
    raw->setHosts(std::move(h));
    return editor;
}

MacroEditorView* MainAnalysisScreen::macroEditor(const std::string& name) {
    auto* pane = macrosPane();
    if (!pane || !pane->editing()) return nullptr;
    std::string a = pane->editedMacro(), b = name;
    for (auto& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return a == b ? pane->editor() : nullptr;
}

void MainAnalysisScreen::runMacro(const std::string& name) {
    openMacros(name, /*launch=*/true);
}

void MainAnalysisScreen::runMacroSource(const std::string& name, std::string source) {
    openMacros(name);
    if (auto* pane = macrosPane()) pane->launch(name, std::move(source));
}

// Writing a macro back into libs/. Save overwrites the same name; Save as asks
// for another, so trying something out never costs you the original.
void MainAnalysisScreen::saveMacro(const std::string& name, std::string source, bool askName) {
    if (!askName) {
        project::SharedLibrary library(project::SharedLibrary::defaultRoot());
        (void)library.scan();
        auto status = library.publishMacro(name, source, "Macros");
        if (!status)
            status_->setTransientMessage("\xC3\xA9" "chec de l'\xC3\xA9" "criture : " + status.error().message(), 8.0);
        else {
            status_->setMessage(name + " enregistr\xC3\xA9" "e dans libs/Macros.");
            macrosChanged(name);
        }
        return;
    }

    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", name + "_copie", "le nom du fichier dans libs/Macros", false, {}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.macroSaveAs", "Enregistrer la macro sous",
            "Elle sera \xC3\xA9" "crite dans libs/Macros, dans le m\xC3\xAA" "me dossier que l'originale. Un nom d\xC3\xA9j\xC3\xA0 pris est \xC3\xA9" "cras\xC3\xA9 : "
            "c'est la seule mani\xC3\xA8re de corriger une macro livr\xC3\xA9" "e, et c'est voulu.",
            fields, "Enregistrer"),
        [this, name, source](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto values = FormDialog::split(r.payload);
            if (values.empty() || values[0].empty()) return;
            const auto check = project::macro::checkMacroName(values[0]);
            if (check.verdict == project::macro::Verdict::Error) {
                status_->setTransientMessage(values[0] + " : " + check.message, 8.0);
                return;
            }
            project::SharedLibrary library(project::SharedLibrary::defaultRoot());
            (void)library.scan();
            const auto folder = library.macroFolderOf(name);
            const bool exists = library.find(values[0]) != nullptr;
            auto status = exists ? library.publishMacro(values[0], project::SharedLibrary::renameInHeader(source, name, values[0]), "Macros")
                                 : library.createMacro(values[0], project::SharedLibrary::renameInHeader(source, name, values[0]), folder);
            if (!status)
                status_->setTransientMessage("\xC3\xA9" "chec : " + status.error().message(), 8.0);
            else {
                status_->setMessage(values[0] + " enregistr\xC3\xA9" "e dans libs/Macros.");
                macrosChanged(values[0]);
            }
        });
}

// =============================================================== VARIABLE ====
//
//  One tab per variable: what it is, what it contains, and - the part a count
//  could never give - WHO WRITES IT and WHO READS IT, with the line and the
//  condition each sits under.
//
//  Four sub-tabs rather than one long page, because the four questions are
//  asked separately: what is this, what is inside it, who writes it, who reads
//  it. A page that answers all four at once answers none of them quickly.

namespace {

// The structure, flattened for a table. A tree widget would be prettier; a table
// can be sorted, filtered and read at a glance, and the depth is carried by the
// indentation of the name.
void flattenStructure(const project::StructureNode& node,
                      std::vector<std::vector<std::string>>& rows) {
    std::string indent(node.depth * 2, ' ');
    rows.push_back({indent + node.name,
                    node.type,
                    node.isStruct ? "struct" : "",
                    node.truncated ? "(recursion arretee ici)" : node.comment});
    for (const auto& child : node.children) flattenStructure(child, rows);
}

std::vector<std::vector<std::string>> referenceRows(
        const std::vector<project::Reference>& refs) {
    std::vector<std::vector<std::string>> rows;
    rows.reserve(refs.size());
    for (const auto& r : refs) {
        std::string kind = r.kind == project::Reference::Kind::Write    ? "ecriture"
                         : r.kind == project::Reference::Kind::CallArgument
                               ? "passee a un bloc"
                               : "lecture";
        std::string where = r.sectionName + ":" + std::to_string(r.line);
        rows.push_back({r.ownerName.empty() ? "(tache)" : r.ownerName,
                        where, kind,
                        r.condition.empty() ? "toujours" : "si " + r.condition,
                        r.path, r.text});
    }
    return rows;
}

} // namespace

void MainAnalysisScreen::openVariable(domain::Index variable) {
    auto project = app_.project();
    if (!project || variable >= project->variables.size()) return;

    const auto& v = project->variables[variable];
    const std::string name(project->strings.text(v.name));
    if (name.empty()) return;

    if (const auto at = variableTabs_.find(name); at != variableTabs_.end()) {
        if (at->second < static_cast<int>(centre_->tabCount())) {
            centre_->setCurrentIndex(static_cast<std::size_t>(at->second));
            return;
        }
        variableTabs_.erase(at);
    }

    const auto xref = project::crossReference(*project, name);
    const auto structure = project::expandStructure(*project, variable);

    auto tabs = std::make_unique<TabControl>("analysis.var.tabs." + name);

    // ---- overview --------------------------------------------------------
    {
        std::vector<std::vector<std::string>> rows;
        auto add = [&](std::string field, std::string value) {
            rows.push_back({std::move(field), std::move(value)});
        };
        add("Nom", name);
        add("Type", std::string(project->strings.text(v.type.name)));
        add("Portee", std::string(domain::toString(v.scope)));
        if (v.owner != domain::kNoIndex && v.owner < project->pous.size())
            add("Definie dans", std::string(project->strings.text(project->pous[v.owner].name)));
        else
            add("Definie dans", "global");
        if (v.type.derivedIndex != domain::kNoIndex) add("Nature", "instance de DDT");
        else if (v.type.fbTypeIndex != domain::kNoIndex) add("Nature", "instance de bloc");
        else add("Nature", "variable elementaire");
        if (v.address.valid()) add("Adresse", v.address.raw);
        if (v.initValue != domain::kNoIndex)
            add("Valeur initiale", std::string(project->strings.text(v.initValue)));
        if (v.comment != domain::kNoIndex)
            add("Commentaire", std::string(project->strings.text(v.comment)));
        add("Ecritures", std::to_string(xref.writes.size()));
        add("Lectures", std::to_string(xref.reads.size()));
        add("Sections", std::to_string(xref.sectionCount()));
        // The one line a reviewer looks for first, said in words rather than
        // left to be worked out from two numbers.
        if (xref.writes.empty() && xref.reads.empty())
            add("Constat", "jamais utilisee");
        else if (xref.writes.empty())
            add("Constat", "lue mais jamais ecrite - valeur initiale, entree, ou ecrite "
                           "par un bloc via un InOut");
        else if (xref.reads.empty())
            add("Constat", "ecrite mais jamais lue");

        auto model = std::make_shared<StringTableModel>();
        model->setRows(std::move(rows));
        auto table = std::make_unique<TableView>("analysis.var.overview." + name);
        table->setModel(model);
        table->setColumns({{"Champ", 150.f}, {"Valeur", 520.f}});
        variableModels_.push_back(model);
        tabs->addTab(TabControl::Tab{"Vue d'ensemble"}, std::move(table));
    }

    // ---- structure -------------------------------------------------------
    if (!structure.children.empty()) {
        std::vector<std::vector<std::string>> rows;
        flattenStructure(structure, rows);
        auto model = std::make_shared<StringTableModel>();
        model->setRows(std::move(rows));
        auto table = std::make_unique<TableView>("analysis.var.struct." + name);
        table->setModel(model);
        table->setColumns({{"Nom", 260.f}, {"Type", 170.f}, {"", 70.f}, {"Commentaire", 320.f}});
        variableModels_.push_back(model);
        tabs->addTab(TabControl::Tab{"Structure (" + std::to_string(rows.size() - 1) + ")"},
                     std::move(table));
    }

    // ---- writes and reads -------------------------------------------------
    auto addRefs = [&](const char* title, const std::vector<project::Reference>& refs) {
        auto model = std::make_shared<StringTableModel>();
        model->setRows(referenceRows(refs));
        auto table = std::make_unique<TableView>(std::string("analysis.var.") + title + name);
        table->setModel(model);
        table->setColumns({{"Contexte", 150.f}, {"Emplacement", 190.f}, {"Type", 120.f},
                           {"Condition", 200.f}, {"Chemin", 180.f}, {"Ligne", 420.f}});
        auto* raw = table.get();
        variableModels_.push_back(model);
        tabs->addTab(TabControl::Tab{std::string(title) + " (" + std::to_string(refs.size()) + ")"},
                     std::move(table));

        // Double-clicking a reference opens the section AT that line. A list of
        // places you then have to go and find by hand is a list half done.
        paneLinks_ += raw->activated->connect([this, refs](RowIndex row) {
            if (row >= refs.size()) return;
            openDocument(refs[row].section);
            status_->setMessage(refs[row].sectionName + " ligne "
                                + std::to_string(refs[row].line) + " : " + refs[row].text);
        });
    };
    addRefs("Ecriture", xref.writes);
    addRefs("Lecture", xref.reads);

    const auto tab = centre_->addTab(
        TabControl::Tab{"Variable " + name, Icon::Variable, /*closable=*/true, false},
        std::move(tabs));
    variableTabs_[name] = static_cast<int>(tab);
    centre_->setCurrentIndex(tab);

    status_->setMessage(name + " : " + std::to_string(xref.writes.size()) + " ecriture(s), "
                        + std::to_string(xref.reads.size()) + " lecture(s) dans "
                        + std::to_string(xref.sectionCount()) + " section(s).");
}


// ============================================================= SUBROUTINE ====
//
//  Une sous-routine se lit comme une section, mais la question qu'on se pose
//  devant elle est differente : une section tourne a chaque cycle, une SR tourne
//  quand quelque chose l'appelle. "Qui l'appelle" est donc LA question, et c'est
//  la seule que la table des sections ne savait pas poser.
void MainAnalysisScreen::openSubroutine(domain::Index section) {
    auto project = app_.project();
    if (!project || section >= project->sections.size()) return;
    const std::string name(project->strings.text(project->sections[section].name));

    // La source, dans l'editeur de sections habituel : meme coloration, meme
    // edition, meme pile d'annulation. Une SR n'a pas besoin d'un editeur a elle.
    openDocument(section);

    const auto xref = project::crossReference(*project, name);

    std::vector<std::vector<std::string>> rows;
    rows.reserve(xref.calls.size());
    for (const auto& c : xref.calls)
        rows.push_back({c.ownerName.empty() ? "(tache)" : c.ownerName,
                        c.sectionName + ":" + std::to_string(c.line),
                        c.condition.empty() ? "toujours" : "si " + c.condition,
                        c.text});

    if (rows.empty()) {
        // Le dire, plutot que de montrer une table vide. Une SR que personne
        // n'appelle est du code mort, et c'est une information, pas un blanc.
        status_->setMessage(name + " : AUCUN APPEL trouve. Cette sous-routine "
                            "n'est executee par personne.");
        return;
    }

    const auto tabName = "Appels " + name;
    if (const auto at = callTabs_.find(name); at != callTabs_.end()) {
        if (at->second < static_cast<int>(centre_->tabCount())) {
            centre_->setCurrentIndex(static_cast<std::size_t>(at->second));
            return;
        }
        callTabs_.erase(at);
    }

    auto model = std::make_shared<StringTableModel>();
    model->setRows(std::move(rows));
    auto table = std::make_unique<TableView>("analysis.sr.calls." + name);
    table->setModel(model);
    table->setColumns({{"Contexte", 160.f}, {"Emplacement", 200.f},
                       {"Condition", 220.f}, {"Ligne", 420.f}});
    auto* raw = table.get();
    variableModels_.push_back(model);

    const auto calls = xref.calls;
    paneLinks_ += raw->activated->connect([this, calls](RowIndex row) {
        if (row >= calls.size()) return;
        openDocument(calls[row].section);
        status_->setMessage(calls[row].sectionName + " ligne "
                            + std::to_string(calls[row].line));
    });

    const auto tab = centre_->addTab(
        TabControl::Tab{tabName, Icon::Section, /*closable=*/true, false}, std::move(table));
    callTabs_[name] = static_cast<int>(tab);
    status_->setMessage(name + " : " + std::to_string(calls.size()) + " appel(s) dans "
                        + std::to_string(xref.sectionCount()) + " section(s).");
}


// ============================================================ right-click ====
//
//  The menu is built from the node, every time. Caching it would be one line
//  shorter and would show "Delete" as available on a type that acquired an
//  instance since the menu was last built - the entry has to be judged at the
//  moment it is shown, against the model as it is now.

bool MainAnalysisScreen::entityForNode(NodeId node, domain::EntityKind& kind,
                                       domain::Index& index) const {
    using NodeKind = ProjectTreeModel::NodeKind;
    const auto k = ProjectTreeModel::kindOf(node);
    index = ProjectTreeModel::indexOf(node);

    switch (k) {
        case NodeKind::DerivedType:  kind = domain::EntityKind::DerivedType; return true;
        case NodeKind::DfbType:
        case NodeKind::ProgramUnit:  kind = domain::EntityKind::Pou;         return true;
        case NodeKind::Section:
        case NodeKind::DfbSection:   kind = domain::EntityKind::Section;     return true;
        case NodeKind::DerivedField:
        case NodeKind::DfbVariable:  kind = domain::EntityKind::Variable;    return true;
        default: return false;    // folders, racks, tasks: nothing to act on
    }
}

// Lot 7 : showExplorerMenu (le menu par genre de noeud) est dans
// ExplorerMenus.cpp, avec buildExplorerMenu et runExplorerAction.

void MainAnalysisScreen::confirmAndDelete(domain::EntityKind kind, domain::Index index) {
    auto document = app_.document();
    if (!document) return;

    // Built once here only to ask what it would take with it, and again inside
    // the callback if the answer is yes. Handing the half-built command to the
    // dialog by raw pointer was the first version: it leaks the moment a dialog
    // is dismissed without invoking its callback, and a command is cheap enough
    // that constructing it twice costs nothing worth that risk. Nothing can
    // mutate the model in between - the dialog is modal.
    const project::RemoveEntityCommand probe(document, kind, index);
    const auto        damage = probe.collateralDamage();
    const std::string what   = probe.label();
    // Lot API 5 : un refus se dit tout de suite (demander « Supprimer ? » puis
    // repondre « impossible » faisait cliquer pour rien).
    if (const auto no = probe.refusalReason(); !no) {
        const auto& e = no.error();
        app_.menus().ShowDialog(std::make_unique<MessageDialog>("Impossible : " + what, e.context.empty() ? e.message() : e.context, MessageDialog::Icon::Warning),
                                [](const menu::DialogResult&) {});
        return;
    }

    auto perform = [this, kind, index, what] {
        auto target = app_.document();
        if (!target) return;
        app_.apply(std::make_unique<project::RemoveEntityCommand>(target, kind, index));
        status_->setMessage(what + " - Ctrl+Z le d\xC3\xA9" "fait.");
    };

    if (damage.empty()) { perform(); return; }

    // Say what else goes. A deletion that quietly takes eight sections with it
    // is one people undo, and being told afterwards is being told too late.
    std::string message = "Part aussi :\n";
    for (std::size_t i = 0; i < damage.size() && i < 12; ++i) message += "  - " + damage[i] + "\n";
    if (damage.size() > 12)
        message += "  et " + std::to_string(damage.size() - 12) + " de plus\n";
    message += "\nCtrl+Z remet tout.";

    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>(what + " ?", message, MessageDialog::Icon::Question,
                                        "Supprimer"),
        [perform](const menu::DialogResult& r) { if (r.accepted()) perform(); });
}

// ----------------------------------------------------------------- frame ----
void MainAnalysisScreen::onEnter() {
    // RENTRER SANS ETRE SORTI. MenuManager rappelle OnEnter quand un dialogue se
    // ferme, mais n'appelle pas OnExit quand il s'ouvre (seul un ECRAN pousse
    // le fait). Sans ce qui suit, chaque dialogue ajoutait un second abonnement
    // a tout - un double-clic dans l'arbre ouvrait deux fois - et
    // loadWorkspace() remettait les separateurs la ou ils etaient AVANT d'etre
    // tires, puisque les tirer n'enregistre rien. On fait donc d'abord ce que
    // OnExit aurait fait.
    if (entered_) {
        saveWorkspace();
        links_.clear();
    }
    entered_ = true;
    installCodeEditorHooks();   // 1.12.2 : F12, Ctrl+T, la barre d'etat des editeurs de code
    // OnEnter runs every time this screen comes back to the top of the stack -
    // including when a modal dialog closes. bindProject tears down every open
    // tab, so an unconditional call here meant that cancelling a dialog closed
    // the document or chart the dialog was about. It has always done that; the
    // chart editor is simply the first thing to make it obvious.
    //
    // Rebinding is for a DIFFERENT project, not for a return to the screen.
    if (auto p = app_.project(); p && p != boundProject_) bindProject(p, app_.report());
    takePendingApiTutorial();      // lot API 7 : l'accueil a demande le didacticiel de l'API

    links_ += app_.events().subscribe<ProjectOpened>(
        [this](const ProjectOpened& e) { bindProject(e.project, e.report); });
    // Lot API 7 : F9, Ctrl+1, Ctrl+5 (sim.open, view.variables, view.statistics)
    // ouvraient des ecrans pleins ; ils ouvrent des onglets de l'API.
    links_ += app_.events().subscribe<OpenApiTab>([this](const OpenApiTab& e) { openApiTabFromAction(e.key); });

    links_ += explorer_->selectionChanged->connect([this](NodeId n) { onTreeSelection(n); });
    // ---- Lot API 8 : l'arbre du projet (le filtre suit le champ) ----
    if (treeFilter_) links_ += treeFilter_->textChanged->connect([this](const std::string& t) { applyTreeFilter(t); });
    // Les outils sortis de l'arbre : un bouton de la rangee sous API / IHM.
    links_ += explorer_->chipClicked->connect(
        [this](NodeId n, std::size_t k) { openTreeTool(ProjectTreeModel::indexOf(n) == 1, k); });
    // 2e partie : les actions au survol d'une ligne (Epingler, Detacher, ...).
    links_ += explorer_->hoverActionClicked->connect([this](NodeId n, std::size_t k) { treeHoverAction(n, k); });
    links_ += explorer_->headActionClicked->connect([this](NodeId n, std::size_t k) { treeHeadAction(n, k); });   // 1.11.23
    // ---- fin Lot API 8 : l'arbre du projet ----
    // Double-clicking an engine instance opens its chart, drawn.
    // Double-clicking a macro runs it - in preview first, always.
    links_ += explorer_->activated->connect([this](NodeId n) {
        if (isHmiNode(n)) { openHmiNode(n); return; }
        if (treeModel_ && treeModel_->kindOf(n) == ProjectTreeModel::NodeKind::Macro) {
            // Lot macros 1 : un double-clic LANCE (le formulaire de l'onglet
            // Macros) ; le code s'ouvre par le clic droit, Modifier le code.
            const auto name = treeModel_->text(n);
            if (!name.empty()) openMacros(name, /*launch=*/true);
            return;
        }
        if (std::string folder; treeModel_ && treeModel_->macroFolderOf(n, folder)) {
            openMacros(folder);
            return;
        }
        // A variable from one of the flat lists: go to where it is USED. A list
        // that only tells you a variable exists is a list you consult once.
        // Une sous-routine : sa source a editer, et la liste de ses appels.
        if (treeModel_) {
            if (const auto sr = treeModel_->subroutineOf(n); sr != domain::kNoIndex) {
                openSubroutine(sr);
                return;
            }
        }
        if (treeModel_) {
            const auto at = treeModel_->variableOf(n);
            if (at != domain::kNoIndex) {
                auto project = app_.project();
                if (project && at < project->variables.size()) {
                    // 1.10 (R) : une instance du moteur de grafcet qui fait tourner un
                    // grafcet du programme ouvre ce grafcet (l'editeur refait) ; les
                    // autres variables, leur fiche.
                    const auto& v = project->variables[at];
                    if (v.type.fbTypeIndex != domain::kNoIndex && v.type.fbTypeIndex < project->pous.size()
                        && project->strings.text(project->pous[v.type.fbTypeIndex].name) == "DFB_GRAFCETENGINE") {
                        const std::string name(project->strings.text(v.name));
                        for (const auto& c : grafcet::findCharts(*project))
                            if (c.instance == name) { openGrafcet(name); return; }
                    }
                    openVariable(at);
                    return;
                }
            }
        }
        if (auto instance = grafcetInstanceAt(n); !instance.empty()) openGrafcet(instance);
    });
    links_ += explorer_->contextMenuRequested->connect(
        [this](NodeId n, gfx::Point at) { showExplorerMenu(n, at); });
    links_ += contextMenu_->itemChosen->connect([this](int a) { runExplorerAction(a); });
    // Lot 7 : F2 et Suppr sur le noeud courant de l'arbre - l'entree Renommer
    // (ou Supprimer) de son menu du clic droit (ExplorerMenus.cpp).
    links_ += explorer_->renameRequested->connect([this](NodeId n) { treeKey(n, true); });
    links_ += explorer_->deleteRequested->connect([this](NodeId n) { treeKey(n, false); });

    // UN DIAGNOSTIC TIENT SUR UNE LIGNE ET NE S'EXPLIQUE PAS EN UNE LIGNE.
    //
    //  " la voie 15 n'est jamais traitee " se comprend avec trois paragraphes
    //  de contexte, et ces paragraphes n'ont pas leur place dans un tableau de
    //  vingt lignes. Le double-clic ouvre la page du code correspondant ; les
    //  messages qui n'en ont pas encore le disent au lieu d'ouvrir une page
    //  vide.
    links_ += diagnostics_->activated->connect([this](RowIndex) {
        const auto cible = helpTargetForDiagnostic();
        if (cible.kind == help::TargetKind::None) {
            status_->setTransientMessage(
                "ce diagnostic n'a pas encore de page d'aide", 6.0);
            return;
        }
        openHelpFor(cible);
    });
    // Lot 7 : la croix et le clic du milieu passent par closeTab (la question
    // pour ce qui n'est pas enregistre) ; le clic droit ouvre le menu de l'onglet.
    links_ += centre_->tabCloseRequested->connect([this](std::size_t i) { (void)closeTab(i); });
    // Lot 7 : le centre en groupes - recaler ce qui retient un onglet par son indice.
    links_ += centre_->tabInserted->connect([this](std::size_t at) { onTabInserted(at); });
    links_ += centre_->tabMoved->connect([this](std::size_t from, std::size_t to) { onTabMoved(from, to); });
    links_ += centre_->detachRequested->connect([this](std::size_t i) { (void)detachTab(i); });
    links_ += centre_->modeChanged->connect([this](TabArea::Mode) { saveLayoutMode(); });
    links_ += centre_->contextMenuRequested->connect([this](std::size_t i, gfx::Point at) { showTabMenu(i, at); });
    links_ += tabMenu_->itemChosen->connect([this](int a) { runTabMenuAction(a); });

    // Lot API 7 : plus de ligne "Simulation: RUNNING  scan N" dans la barre
    // d'etat. La barre du haut dit l'etat et le cycle (lot API 2), l'arbre le dit
    // sur API > Simulation ; cette ligne, reecrite a chaque cycle, effacait
    // aussitot ce que l'onglet Simulation venait d'y ecrire (un forcage refuse,
    // pourquoi la simulation ne demarre pas).

    links_ += documentList_->activated->connect([this](RowIndex r) {
        // Double-click closes; a single click just brings the tab forward.
        if (r < documents_.size()) closeDocument(documents_[r].tab);
    });
    links_ += documentList_->selectionChanged->connect([this](RowIndex r) {
        if (r < documents_.size()) centre_->setCurrentIndex(documents_[r].tab);
    });
    if (sections_)      // lot 7 : la table des sections du bas est partie
        links_ += sections_->activated->connect(
            [this](RowIndex r) { openDocument(static_cast<domain::Index>(r)); });

    // Clicking a module in the drawn rack scopes the property grid to it.
    links_ += rackView_->moduleSelected->connect([this](std::uint16_t rack, std::int16_t slot) {
        auto project = app_.project();
        if (!project) return;
        for (const auto& r : project->hardware.racks) {
            if (r.number != rack) continue;
            for (const auto& m : r.modules) {
                if (m.slot != slot) continue;
                configuration_->setCategories(buildModuleProperties(*project, m));
                status_->setMessage("Rack " + std::to_string(rack) + ", "
                                    + (slot < 0 ? std::string("power supply") : "slot " + std::to_string(slot))
                                    + "  -  " + m.reference
                                    + (m.description.empty() ? "" : "  -  " + m.description));
                return;
            }
        }
    });

    links_ += variableSearch_->textChanged->connect([this](const std::string& term) {
        FilterChain chain;
        chain.setGlobalTerm(term);
        variables_->setFilter(std::move(chain));
    });
    // The same double-click, from the variables browser. Two ways in, one way
    // through: openGrafcetFor decides, so the two can never disagree about what
    // counts as an instance.
    links_ += variables_->activated->connect([this](RowIndex row) {
        if (variableModel_)
            openGrafcetFor(variableModel_->cellText(row, VariableTableModel::Name));
    });

    links_ += variables_->selectionChanged->connect(
        [this](const std::vector<RowIndex>& rows) {
            status_->setMessage(std::to_string(rows.size()) + " selected");
        });

    for (const auto& p : panels_)
        links_ += p.box->stateChanged->connect([this](Checkbox::State) {
            applyPanelVisibility();
            saveWorkspace();
        });
    wireBottomPanel();     // 1.11.14 : le panneau du bas (HmiBuildWorkspace.cpp)
    links_ += altRowsBox_->stateChanged->connect([this](Checkbox::State) {
        applyPanelVisibility();
        saveWorkspace();
    });

    // Lot API 2 : la barre du haut - ses actions, ses menus (un PopupMenu pose
    // dans l'OverlayHost, a elle seule), ce qu'elle grise.
    if (topBar_) {
        topBar_->setActionSink([this](core::ActionId id) { onBarAction(id); });
        topBar_->setPopup(barMenu_);
        topBar_->setEnabledProvider([this](core::ActionId id) {
            if (const auto* a = app_.actions().find(id)) return !a->enabled || a->enabled();
            return true;
        });
    }

    // One line for the state, refreshed from the host rather than polled, so the
    // workspace says what the program is doing without the Simulation tab having
    // to be open. Lot API 7 : et la pastille de API > Simulation dans l'arbre -
    // aussi quand un dialogue est ouvert (l'ecran n'a alors pas d'Update, la
    // simulation, si : App la fait avancer).
    links_ += app_.simulation().changed->connect([this] {
        refreshSimulationIndicator();
        refreshSimulationBadge();
    });
    refreshSimulationIndicator();
    refreshSimulationBadge();

    wireHistory();      // lot 19 : l'historique, l'endroit de chaque commande
    loadWorkspace();
}

void MainAnalysisScreen::onExit() {
    saveWorkspace();    // ratios may have been dragged since the last toggle
    links_.clear();     // every callback above dies here: no dangling `this`
    removeCodeEditorHooks();   // 1.12.2 : idem pour les editeurs de code
    entered_ = false;
}

namespace {
// Read whatever positions were saved beside this project. Absent is the normal
// state and costs nothing: computeLayout places everything.
std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}

void MainAnalysisScreen::bindProject(ProjectRef project,
                                     std::shared_ptr<const importer::AnalysisReport> report) {
    if (!project) return;
    boundProject_ = project;

    // Open documents belong to the previous project; they must not outlive it.
    // Sauf ceux de l'IHM quand c'est LA MEME IHM : une modification du
    // programme republie le projet, et fermer l'editeur de vues a chaque
    // section creee ferait perdre le fil. Ils sont en queue ou au milieu :
    // on retire tout le reste, de droite a gauche.
    const bool sameHmi = app_.hmi() && app_.hmi() == boundHmi_;
    for (std::size_t i = centre_->tabCount(); i > fixedTabCount_; --i) {
        const std::size_t at = i - 1;
        if (sameHmi) {
            const auto* page = centre_->page(at);
            const bool isHmi = std::any_of(hmiTabs_.begin(), hmiTabs_.end(),
                                           [page](const auto& e) { return e.second == page; });
            if (isHmi) continue;
        }
        // Lot API 2 : les onglets de l'API restent (ils se rafraichissent plus
        // bas), comme ceux de l'IHM ; une page gardee ne se detruit jamais.
        if (isApiTab(centre_->page(at))) continue;
        forgetHmiTab(at);
        if (!parkTab(at)) centre_->removeTab(at);
    }
    documents_.clear();
    grafcetTabs_.clear();
    macroTabs_.clear();         // indices du projet precedent : ils designeraient
    variableTabs_.clear();      // n'importe quel onglet ouvert ensuite
    callTabs_.clear();

    // Positions for THIS project. They belong to it, so the previous project's
    // are dropped rather than carried over onto charts that merely share a name.
    layoutFile_ = project::LayoutFile{};
    if (const auto& recent = app_.recentPaths(); !recent.empty())
        layoutFile_.parse(readFile(project::LayoutFile::pathFor(recent.front())));
    refreshDocumentList();

    treeModel_ = std::make_shared<ProjectTreeModel>(project);
    explorer_->setModel(treeModel_);
    refreshMacroList();
    bindHmi();                  // le dossier IHM, avant de deplier
    explorer_->expandToDepth(1);
    // ---- Lot API 8 : l'arbre du projet (les epingles de ce projet ; un filtre en cours se refait) ----
    // La pastille rouge du dernier Compiler reste sur le meme projet (l'arbre se refait a chaque modification).
    if (treeExprFolder_ == app_.projectFolder()) treeModel_->setHmiExprErrors(treeExprErrors_, treeExprErrorsInViews_);
    else treeExprErrors_ = treeExprErrorsInViews_ = 0;
    loadTreePins();
    if (!treeFilterText_.empty()) {
        const std::string again = treeFilterText_;
        treeFilterText_.clear();
        applyTreeFilter(again);
    }
    // ---- fin Lot API 8 : l'arbre du projet ----

    // LE GLISSER-DEPOSER DE L'ORDRE D'EXECUTION, refait a chaque projet.
    //
    //  Les connexions vont dans leur PROPRE portee, videe juste avant : les
    //  predicats et le gestionnaire de depot capturent CE projet et CE modele,
    //  et ceux du projet precedent deplaceraient des sections dans un modele
    //  qui n'est plus affiche. `links_` ne convient pas, elle vit aussi
    //  longtemps que l'ecran.
    //
    //  La portee est obligatoire pour une autre raison, moins visible :
    //  Signal::connect rend une Connection qui se DEBRANCHE en mourant. La
    //  laisser tomber par terre branche puis debranche aussitot, et le depot ne
    //  fait plus rien - sans message et sans plantage.
    //  Le MODELE MUTABLE, pas `project` : les commandes ecrivent, et
    //  `ProjectRef` est un pointeur vers du const. Un import qui n'a pas ete
    //  adopte comme projet n'en a pas - rien n'est alors deplacable, et les
    //  predicats sont remis a zero pour que ceux du projet precedent ne
    //  laissent pas l'arbre trainable dans le vide.
    execOrderLinks_.clear();
    if (auto document = app_.document()) {
        ExecutionOrderWiring::install(
            *explorer_, treeModel_, document,
            [this](core::CommandPtr c) { app_.apply(std::move(c)); },
            execOrderLinks_);
    } else {
        explorer_->setDragPredicate({});
        explorer_->setDropPredicate({});
    }
    // Lot 21 : les listes de l'IHM (vues, popups, modeles, symboles, scripts,
    // types) se rangent aussi dans l'arbre, par glisser-deposer.
    wireHmiTreeDrag();
    wireMacroTreeDrag();   // lot macros 1 : les dossiers des macros
    wireAnimationTreeDrag();   // lot API 3 : une variable glissee sur une table d'animation

    variableModel_ = std::make_shared<VariableTableModel>(
        project, importer::ProjectAnalyzer::buildReferenceIndex(*project));
    variables_->setModel(variableModel_);
    variables_->sortBy(VariableTableModel::Name, SortOrder::Ascending);

    // Lot 7 : la bibliotheque DFB et la table des sections du bas sont parties.
    if (dfbLibrary_) {
        libraryModel_ = std::make_shared<LibraryTreeModel>(project);
        dfbLibrary_->setModel(libraryModel_);
        dfbLibrary_->expandToDepth(1);
    }

    sectionModel_ = std::make_shared<SectionTableModel>(project);
    if (sections_) sections_->setModel(sectionModel_);

    configuration_->setCategories(buildConfigurationProperties(*project));
    rackView_->setHardware(project);
    refreshApiPanes();      // lot API 2 : le tableau de bord, les taches, le reseau...

    if (report) {
        diagnosticsModel_ = std::make_shared<DiagnosticsTableModel>(project, *report);
        diagnostics_->setModel(diagnosticsModel_);
        summary_->setCategories(buildAnalysisSummary(*report));
        projectStatus_->setCategories(buildProjectStatus(*project, *report));
        // 1.11 (R111, recette T3-11) : accordes (« 1 section »).
        const auto accord = [](std::size_t n, const char* un, const char* plusieurs) { return std::to_string(n) + " " + (n == 1 ? un : plusieurs); };
        status_->setMessage(accord(static_cast<std::size_t>(report->totalVariables), "variable", "variables") + " \xC2\xB7 "
                            + accord(static_cast<std::size_t>(report->sections), "section", "sections") + " \xC2\xB7 "
                            + accord(report->findings.size(), "constat", "constats"));   // Lot API 8 : en francais
    }

    // Lot API 8 : l'avis de l'import en francais a l'ecran ; son texte d'origine reste la
    // cle que l'import, la fusion d'un MAST et l'aide reconnaissent.
    for (const auto& notice : project->partialDataNotices)
        status_->setTransientMessage(notice.find("Rack and module layout is not part") != std::string::npos
                                         ? std::string("Les racks et les modules ne sont pas dans un export .XPG : importe le .XHW (ou le "
                                                       ".XEF) qui va avec pour remplir la configuration de l'automate.")
                                         : notice,
                                     12.0);

    // Lot API 2 : plus d'onglets fixes - un projet qui s'ouvre sur un centre
    // vide ouvre le tableau de bord de l'API (ce qu'est l'automate, ce qui est
    // a regarder), comme un clic sur API dans l'arbre.
    if (centre_ && centre_->tabCount() == 0) {
        // 1.12.0 : XPGAnalyser IHM - la vue de demarrage de l'IHM (pas de tableau de bord de l'API).
        if (!core::hasApi()) {
            if (const auto doc = app_.hmi(); doc && doc->project.view(doc->project.config.startView))
                openHmiView(doc->project.config.startView);
            else
                openHmiPane("config");
        } else {
            openApiPane("api");
        }
    }
}

// =============================================================================
//  F1, LA TOUCHE QUE TOUT LE MONDE ESSAIE
// -----------------------------------------------------------------------------
//  Elle etait annoncee dans l'info-bulle du bouton Aide et declaree dans la
//  table des actions, et rien ne la branchait : aucun code de ce projet ne
//  transforme une touche en ActionId. Elle ne faisait donc rien, ce qui est
//  pire que de ne pas l'annoncer.
//
//  ET ELLE DOIT OUVRIR LA BONNE PAGE, pas le sommaire. Le curseur pose sur un
//  nom dans une section, une ligne de diagnostic selectionnee, un noeud de
//  l'explorateur : les trois designent quelque chose de precis, et c'est cet
//  ecran - le seul a connaitre les trois - qui peut le dire.
// =============================================================================
const std::vector<project::CatalogEntry>& MainAnalysisScreen::helpLibrary() const {
    // Lue une fois, a la premiere demande. Relire soixante fichiers a chaque
    // F1 serait invisible a l'usage mais gratuit ; ne jamais les relire serait
    // faux apres un enregistrement depuis l'aide, d'ou refreshHelpLibrary().
    if (helpLibrary_.empty())
        helpLibrary_ = project::scanLibrary(project::SharedLibrary::defaultRoot());
    return helpLibrary_;
}

help::Target MainAnalysisScreen::helpTargetForNode(NodeId node) const {
    using NodeKind = ProjectTreeModel::NodeKind;
    auto project = app_.project();
    if (!project || !treeModel_) return {};

    const auto kind = ProjectTreeModel::kindOf(node);
    const auto i    = ProjectTreeModel::indexOf(node);

    std::string name;
    if (kind == NodeKind::DerivedType && i < project->derivedTypes.size())
        name = project->strings.text(project->derivedTypes[i].name);
    else if (kind == NodeKind::DfbType && i < project->pous.size())
        name = project->strings.text(project->pous[i].name);
    else if (kind == NodeKind::Macro)
        name = treeModel_->text(node);

    if (name.empty()) return {};
    return help::targetForWord(helpLibrary(), name);
}

help::Target MainAnalysisScreen::helpTargetForDiagnostic() const {
    if (!diagnosticsModel_ || !diagnostics_) return {};
    const auto rows = diagnostics_->selectedModelRows();
    if (rows.empty()) return {};
    // La colonne Detail porte le message ; c'est lui qu'on reconnait, pas le
    // sujet ni la gravite.
    return help::targetForDiagnostic(
        diagnosticsModel_->cellText(rows.front(), DiagnosticsTableModel::Detail));
}

help::Target MainAnalysisScreen::helpTargetNow() const {
    // L'ORDRE EST LE SUJET : du plus precis au plus vague. Le curseur dans du
    // code designe une chose et une seule ; l'explorateur designe ce qui est
    // selectionne depuis peut-etre dix minutes.
    // Lot macros 1 : l'onglet Macros - la macro de la fiche ou du formulaire.
    if (auto* macros = macrosPane(); macros && centre_ && centre_->indexOf(macros) == static_cast<int>(centre_->currentIndex())) {
        const std::string current = macros->selectedMacro().empty() ? (macros->session() ? macros->session()->name() : std::string{})
                                                                    : macros->selectedMacro();
        if (!current.empty())
            if (const auto t = help::targetForWord(helpLibrary(), current); t.kind != help::TargetKind::None) return t;
    }
    if (centre_ && centre_->currentIndex() >= fixedTabCount_ && !lastSymbol_.empty()) {
        const auto t = help::targetForWord(helpLibrary(), lastSymbol_);
        if (t.kind != help::TargetKind::None) return t;
    }
    if (const auto t = helpTargetForDiagnostic(); t.kind != help::TargetKind::None) return t;
    if (explorer_) {
        if (const auto t = helpTargetForNode(explorer_->currentNode());
            t.kind != help::TargetKind::None)
            return t;
    }
    return {};
}

void MainAnalysisScreen::openHelpFor(const help::Target& target) {
    // La cible est deposee avant de pousser l'ecran : celui-ci est fabrique par
    // une fabrique, et PushMenu ne transporte pas d'argument. C'est une boite
    // aux lettres a une place, videe par l'ecran d'aide en entrant.
    help::setPendingTarget(target);
    // Lot macros 1 : directement le bon onglet de l'aide (Macros, Blocs DFB /
    // DDT), sans passer par l'aide generale.
    app_.menus().PushMenu(helpMenuFor(target, project::SharedLibrary::defaultRoot()));
    if (target.kind != help::TargetKind::None)
        status_->setTransientMessage("Aide : " + help::labelOf(target), 6.0);
}

void MainAnalysisScreen::refreshHelpLibrary() { helpLibrary_.clear(); }

ui::EventResult MainAnalysisScreen::HandleEvent(const InputEvent& ev) {
    // F1 passe AVANT l'arbre de widgets : elle doit marcher quel que soit ce
    // qui a le focus, et aucun widget de cet ecran ne s'en sert.
    if (const auto* k = std::get_if<KeyDown>(&ev);
        k != nullptr && k->key == Key::F1 && k->mods.none() && !k->repeat
        && !(hmiTutorial_ && hmiTutorial_->blocking())) {
        // Lot 19 : la souris sur l'historique - son sujet.
        if (historyPanel_ && historyPanel_->isOpen() && historyPanel_->hovered()) {
            openHmiHelp("historique");
            return EventResult::Consumed;
        }
        // 1.10 (integration I2, note de P) : l'editeur de grafcet - le sujet grafcet de l'aide.
        if (app_.hmi() && centre_ && centre_->tabCount() > 0
            && dynamic_cast<GrafcetPane*>(centre_->page(centre_->currentIndex())) != nullptr) {
            openHmiHelp("grafcet");
            return EventResult::Consumed;
        }
        // ---- Lot API 8 : didacticiels et aide ----
        //  Un onglet du dossier Simulation (Vue d'ensemble, Automate, Debogage,
        //  Forcages, Courbes, Journal) : sa page de l'aide generale.
        if (ApiTrails::Lot8::helpNow(*this)) return EventResult::Consumed;
        // ---- fin Lot API 8 : didacticiels et aide ----
        // L'IHM a son aide (HmiWorkspace.cpp) : le volet ou le nom sous le
        // curseur. Ailleurs, celle de la bibliotheque.
        if (!openHmiHelpNow()) {
            // 1.11 (T2, tranche 16, decision du chef) : dans un onglet de l'API sans
            // sujet plus precis, F1 ouvre le centre au sujet de l'onglet (la table de
            // F1), et non plus le didacticiel de l'API, qui reste une entree du centre.
            const auto target = helpTargetNow();
            Widget* page = centre_ && centre_->tabCount() > 0 ? centre_->page(centre_->currentIndex()) : nullptr;
            if (target.kind == help::TargetKind::None && isApiTab(page)) {
                std::string tab;
                for (const auto& [cle, w] : apiTabs_)
                    if (w == page) tab = cle;
                const auto* row = help::f1::forApiTab(tab);
                if (row && !row->key.empty()) app_.setHelpTopic(std::string(row->key));   // HelpCenterScreen::onEnter
                app_.menus().PushMenu(row ? std::string(row->menu) : std::string("help"));
                if (row) status_->setTransientMessage("Aide : " + std::string(row->where), 6.0);
            } else {
                openHelpFor(target);
            }
        }
        return EventResult::Consumed;
    }
    // Lot 19 : les raccourcis de l'ecran. Ctrl+Tab et Ctrl+PgSuiv/PgPrec
    // AVANT les widgets (un editeur de code prendrait la tabulation) ; Ctrl+Z,
    // Ctrl+Y, Ctrl+S, Ctrl+H APRES (un champ en saisie garde son Ctrl+Z).
    const auto* key = std::get_if<KeyDown>(&ev);
    // Lot 21 : une etape interactive du didacticiel laisse passer les raccourcis.
    const bool tutorial = hmiTutorial_ && hmiTutorial_->blocking();
    // Lot 20 : un clic hors du panneau d'Aller a... le ferme (et fait son effet).
    if (const auto* d = std::get_if<MouseDown>(&ev); d && goToPanel_ && goToPanel_->isOpen() && !goToPanel_->boxRect().contains(d->pos)
                                                      && !(goToBox_ && goToBox_->bounds().contains(d->pos)))
        goToPanel_->close();
    if (key && !tutorial && handleShortcut(*key, true)) return EventResult::Consumed;
    // Lot API 3 : l'appui dans l'arbre, le glisser qui en part, le lacher.
    if (const auto* d = std::get_if<MouseDown>(&ev); d && explorer_ && explorer_->bounds().contains(d->pos)) {
        treePress_ = true;
        treeDragged_ = false;
        deferredTreeNode_ = kInvalidNode;
    } else if (std::holds_alternative<MouseMove>(ev) && treePress_ && explorer_ && explorer_->dragInProgress()) {
        treeDragged_ = true;
    } else if (std::holds_alternative<MouseUp>(ev) && treePress_) {
        treePress_ = false;
        const bool dragged = treeDragged_ || (explorer_ && explorer_->dragInProgress());
        const auto node = deferredTreeNode_;
        deferredTreeNode_ = kInvalidNode;
        if (node != kInvalidNode && !dragged) onTreeSelection(node);
    }
    // Lot API 3 : une variable tiree de l'arbre et lachee sur les lignes d'une table.
    if (dropTreeDragOnTables(ev)) return EventResult::Consumed;
    // 1.11.1 (API-V) : une variable de l'automate tiree de l'arbre de IHM > Configuration
    // et lachee dans un editeur de script de l'IHM (son nom API.… au point du lacher).
    if (HmiApiVarsView::routeDrag(ev)) return EventResult::Consumed;
    const auto r = menu::WidgetMenu::HandleEvent(ev);
    if (r == EventResult::Consumed) return r;
    if (key && !tutorial && handleShortcut(*key, false)) return EventResult::Consumed;
    return r;
}

void MainAnalysisScreen::onTreeSelection(NodeId node) {
    using Kind = ProjectTreeModel::NodeKind;
    const auto kind = ProjectTreeModel::kindOf(node);
    // Lot API 3 : une variable sous l'appui peut partir vers une table
    // d'animation ; son onglet s'ouvrira au lacher (voir HandleEvent).
    if (treePress_ && (kind == Kind::ListVariable || kind == Kind::HmiVariable)) {
        deferredTreeNode_ = node;
        return;
    }
    // ---- Lot API 8 : l'arbre du projet (un resultat du filtre : comme Aller a...) ----
    if (openTreeFilterHit(node)) return;
    // ---- fin Lot API 8 : l'arbre du projet ----
    const auto index = ProjectTreeModel::indexOf(node);
    auto project = app_.project();
    if (!project) return;

    // L'IHM : un clic ouvre, comme pour une section. Le dossier IHM lui-meme
    // n'ouvre rien - le deplier est ce qu'on attend de lui.
    if (isHmiNode(node)) {
        if (kind != Kind::HmiFolder) openHmiNode(node);
        return;
    }

    // ---- Lot API 8 : Centre de simulation ----
    // Le dossier Simulation et ses onglets (SimulationWorkspace.cpp) ; Automate
    // et IHM y gardent leurs genres (routeApiNode, openHmiNode).
    if (routeSimNode(node)) return;
    // ---- fin Lot API 8 ----

    // Lots API 3 et 4 : les tables d'animation, la configuration, les taches,
    // l'ordre d'execution ont leurs onglets (ApiWorkspace.cpp).
    if (routeApiNode(node)) return;

    switch (kind) {
        // Lot API 2 : chaque entree du dossier API ouvre son onglet.
        case Kind::ApiFolder:         openApiPane("api"); return;
        case Kind::TaskFolder:        openApiPane("taches"); return;
        case Kind::SubroutinesFolder: openApiPane("sous-routines"); return;
        // Selecting a section opens its source. This is what the explorer is
        // for, and it was the one thing clicking a node did not do.
        case Kind::Section:
        case Kind::DfbSection:
            openDocument(index);
            break;

        // Clicking a folder shows its contents in the table, rather than leaving
        // the reader to expand a hundred nodes one at a time. The tree says what
        // exists; the table is where you read it.
        case Kind::ElementaryFolder:
        case Kind::DdtInstanceFolder:
        case Kind::DfbInstanceFolder: {
            FilterChain chain;
            // The filter is on the TYPE column, which is what separates the
            // three: an elementary variable has a base type, an instance has the
            // name of a DDT or of a block.
            if (kind == Kind::DfbInstanceFolder) chain.setGlobalTerm("DFB_");
            else if (kind == Kind::DdtInstanceFolder) chain.setGlobalTerm("ST_");
            if (variables_) variables_->setFilter(std::move(chain));
            if (variableSearch_)
                variableSearch_->setText(kind == Kind::DfbInstanceFolder ? "DFB_"
                                       : kind == Kind::DdtInstanceFolder ? "ST_" : "");
            status_->setMessage(std::string(treeModel_ ? treeModel_->text(node) : "")
                                + " : la table des variables est filtree.");
            break;
        }

        case Kind::Cpu:
        case Kind::ConfigurationFolder:
        case Kind::RackFolder:
        case Kind::Rack:
            configuration_->setCategories(buildConfigurationProperties(*project));
            openApiPane("configuration");
            break;

        case Kind::HwModule: {
            const auto sub = static_cast<domain::Index>(node & ((1ull << 28) - 1));
            if (index < project->hardware.racks.size()
                && sub < project->hardware.racks[index].modules.size()) {
                configuration_->setCategories(
                    buildModuleProperties(*project, project->hardware.racks[index].modules[sub]));
                openApiPane("configuration");
            }
            break;
        }

        case Kind::Task:
            // routeApiNode la prend avant (l'onglet Taches, la tache choisie) ;
            // lot API 7 : plus de table des sections en bas a filtrer.
            openApiPane("taches");      // lot API 2
            break;

        case Kind::DerivedType: {
            FilterChain chain;
            chain.setColumnTerm(VariableTableModel::Type,
                                std::string(project->strings.text(project->derivedTypes[index].name)));
            variables_->setFilter(std::move(chain));
            break;
        }

        // Selecting a declaration jumps the variable browser to it.
        case Kind::DfbVariable:
        case Kind::DerivedField: {
            FilterChain chain;
            chain.setColumnTerm(VariableTableModel::Name,
                                std::string(project->strings.text(project->variables[index].name)));
            variables_->setFilter(std::move(chain));
            break;
        }

        case Kind::DfbType: {
            FilterChain chain;
            chain.setColumnTerm(VariableTableModel::Type,
                                std::string(project->strings.text(project->pous[index].name)));
            variables_->setFilter(std::move(chain));
            break;
        }

        default:
            break;
    }
}

} // namespace app
