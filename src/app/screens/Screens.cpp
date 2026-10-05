// =============================================================================
//  app/screens/Screens.cpp - startup, explorers, help, settings
// -----------------------------------------------------------------------------
//  Same shape throughout: build the tree once in buildUi(), bind models and
//  subscribe in onEnter(), drop every subscription in onExit().
// =============================================================================
#include "../Brand.hpp"
#include "Screens.hpp"
#include "../../project/ProjectIcon.hpp"

#include "../../help/HelpSession.hpp"   // la boite aux lettres de F1
#include "../hmi/HmiHelpPane.hpp"          // lot 8 : l'aide de l'IHM, depuis l'aide generale
#include "../hmi/HmiStationLaunch.hpp"      // lot 15 : le poste au demarrage du PC
#include "LibraryHelpScreen.hpp"            // lot macros 1 : les onglets de l'aide
#include "../TutorialsLot8.hpp"             // Lot API 8 : didacticiels et aide (la page demandee)
#include "../../project/SharedLibrary.hpp"

#include "../App.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace app {

using namespace ui;

namespace {

// Small helper: a label rendered as a disabled, borderless text field. Avoids a
// dedicated Label widget for the handful of places that need static text.
class Label final : public Widget {
public:
    // `stretch` : 0 pour une etiquette qui garde sa largeur (lot macros 1 :
    // " Rechercher " prenait la moitie de la barre de l'aide).
    explicit Label(std::string text, bool bold = false, gfx::Color* colour = nullptr, float stretch = 1.f)
        : Widget(), text_(std::move(text)), bold_(bold), stretch_(stretch) {
        if (colour) { colour_ = *colour; hasColour_ = true; }
    }
    void setText(std::string t) { text_ = std::move(t); invalidate(); }
    [[nodiscard]] SizeHint sizeHint() const override {
        const float line = lineHeight(gfx::FontId{16});
        return SizeHint{{measureWidth(text_, gfx::FontId{16}) + 8.f, line + 6.f},
                        {40.f, line + 4.f}, stretch_, 0.f};
    }
protected:
    void onPaint(const PaintContext& ctx) override {
        const auto r = contentRect();
        const auto f = bold_ ? ctx.theme.font.uiBold : ctx.theme.font.ui;
        ctx.r.drawText({r.x, r.y + (r.h - ctx.r.lineHeight(f)) * 0.5f}, text_, f,
                       hasColour_ ? colour_ : ctx.theme.color.text);
    }
private:
    std::string text_;
    bool        bold_{false};
    float       stretch_{1.f};
    bool        hasColour_{false};
    gfx::Color  colour_{};
};

// (Lot API 7 : StringListModel, la liste des projets recents de l'ancien
// accueil, est partie avec lui - les cartes de app/StartPage.cpp la remplacent.)

std::string formatCount(std::size_t n) { return std::to_string(n); }

// Every pushed screen gets one of these. Without it a screen entered from the
// toolbar could only be left with the keyboard, and Escape was not wired up
// either - so there was simply no way back.
std::unique_ptr<ToolBar> makeBackToolBar(App& app, std::string title,
                                         core::ConnectionScope& links, ToolBar** out) {
    auto bar = std::make_unique<ToolBar>("screen.toolbar");
    auto& back = bar->addButton("Back", "nav.back", Icon::Collapse);
    back.setTooltip("Return to the analysis workspace (Escape)");
    bar->addSeparator();
    bar->addCustom(std::make_unique<Label>(std::move(title), true));

    links += back.clicked->connect([&app] { app.menus().PopMenu(); });
    *out = bar.get();
    return bar;
}

} // namespace

// ========================================================= StartupScreen ====
// Lot API 7 : l'accueil est dans screens/StartScreen.cpp (et ses widgets dans
// app/StartPage.cpp).

// Lot API 7 : l'ancien explorateur de variables en plein ecran (Ctrl+1) est
// parti - l'onglet API > Variables (VariablesPane) le remplace.

// ================================================= LibraryExplorerScreen ====
LibraryExplorerScreen::LibraryExplorerScreen(App& app)
    : menu::WidgetMenu("analysis.libraries"), app_(app) {}

core::Status LibraryExplorerScreen::buildUi() {
    auto shell = std::make_unique<DockLayout>("libraries.shell");
    shell->dock(makeBackToolBar(app_, "Library explorer", links_, &toolbar_),
                DockLayout::Side::Top, app_.theme().metric.toolbarHeight);

    auto split = std::make_unique<Splitter>(Orientation::Horizontal, "libraries.split");

    auto tree = std::make_unique<TreeView>("libraries.tree");
    tree_ = &static_cast<TreeView&>(split->addPane(std::move(tree), 0.30f, 200.f));

    auto right = std::make_unique<Splitter>(Orientation::Vertical, "libraries.right");
    auto details = std::make_unique<PropertyGrid>("libraries.details");
    details_ = &static_cast<PropertyGrid&>(right->addPane(std::move(details), 0.35f, 100.f));
    auto source = std::make_unique<MultiLineText>("libraries.source");
    source->setReadOnly(true);
    source->setLanguage(Language::StructuredText);
    source_ = &static_cast<MultiLineText&>(right->addPane(std::move(source), 0.65f, 120.f));
    split->addPane(std::move(right), 0.70f, 260.f);

    split_ = split.get();
    shell->dock(std::move(split), DockLayout::Side::Center, 0.f);
    setRoot(std::move(shell));
    return core::ok();
}

void LibraryExplorerScreen::onEnter() {
    auto project = app_.project();
    if (!project) return;

    model_ = std::make_shared<LibraryTreeModel>(project);
    tree_->setModel(model_);
    tree_->expandToDepth(1);

    links_ += tree_->selectionChanged->connect([this, project](NodeId n) {
        // Library entries are encoded as 1000 + index by LibraryTreeModel.
        if (n < 1000) { details_->clearProperties(); source_->setText({}); return; }
        const auto libIndex = static_cast<domain::Index>(n - 1000);
        if (libIndex >= project->libraries.size()) return;

        const auto& lib = project->libraries[libIndex];
        std::vector<PropertyGrid::Category> cats;
        PropertyGrid::Category info{"Block", {}, {}, true};
        info.properties.push_back({"Name", std::string(project->strings.text(lib.name)),
                                   PropertyGrid::ValueType::ReadOnly, {}, {}, nullptr});
        info.properties.push_back({"Version", lib.version,
                                   PropertyGrid::ValueType::ReadOnly, {}, {}, nullptr});
        info.properties.push_back({"Instances", std::to_string(lib.usageCount),
                                   PropertyGrid::ValueType::ReadOnly, {}, {}, nullptr});

        std::string body;
        if (lib.pouIndex != domain::kNoIndex) {
            const auto& pou = project->pous[lib.pouIndex];
            PropertyGrid::Category params{"Parameters", {}, {}, true};
            for (auto vi : pou.parameters) {
                const auto& v = project->variables[vi];
                params.properties.push_back({std::string(project->strings.text(v.name)),
                                             std::string(project->strings.text(v.type.name)),
                                             PropertyGrid::ValueType::ReadOnly,
                                             std::string(domain::toString(v.scope)), {}, nullptr});
            }
            cats.push_back(std::move(info));
            cats.push_back(std::move(params));
            for (auto si : pou.sections) {
                body += "(* --- " + std::string(project->strings.text(project->sections[si].name))
                      + " --- *)\n" + project->sections[si].body + "\n";
            }
        } else {
            cats.push_back(std::move(info));
        }
        details_->setCategories(std::move(cats));
        source_->setText(std::move(body));
    });
}

void LibraryExplorerScreen::onExit() { links_.clear(); }

// ==================================================== ProgramUnitsScreen ====
ProgramUnitsScreen::ProgramUnitsScreen(App& app)
    : menu::WidgetMenu("analysis.programs"), app_(app) {}

core::Status ProgramUnitsScreen::buildUi() {
    auto shell = std::make_unique<DockLayout>("programs.shell");
    shell->dock(makeBackToolBar(app_, "Program units", links_, &toolbar_),
                DockLayout::Side::Top, app_.theme().metric.toolbarHeight);

    auto split = std::make_unique<Splitter>(Orientation::Horizontal, "programs.split");
    auto tree = std::make_unique<TreeView>("programs.tree");
    tree_ = &static_cast<TreeView&>(split->addPane(std::move(tree), 0.32f, 220.f));

    auto editors = std::make_unique<TabControl>("programs.editors");
    auto source = std::make_unique<MultiLineText>("programs.source");
    source->setReadOnly(true);
    source->setLanguage(Language::StructuredText);
    source_ = source.get();
    editors->addTab({"Source"}, std::move(source));

    auto list = std::make_unique<TableView>("programs.sections");
    list->setColumns({{"Name", 380.f}, {"Task", 130.f}, {"Language", 150.f},
                      {"Lines", 110.f, 60.f, true, true, true, Align::End},
                      {"Activation", 380.f}});
    sections_ = list.get();
    editors->addTab({"All sections"}, std::move(list));

    editors_ = &static_cast<TabControl&>(split->addPane(std::move(editors), 0.68f, 300.f));
    shell->dock(std::move(split), DockLayout::Side::Center, 0.f);

    auto status = std::make_unique<StatusBar>("programs.status");
    status_ = &static_cast<StatusBar&>(shell->dock(std::move(status), DockLayout::Side::Bottom, 24.f));

    setRoot(std::move(shell));
    return core::ok();
}

void ProgramUnitsScreen::onEnter() {
    auto project = app_.project();
    if (!project) return;

    model_ = std::make_shared<ProjectTreeModel>(project);
    tree_->setModel(model_);
    tree_->expandToDepth(2);

    sectionModel_ = std::make_shared<SectionTableModel>(project);
    sections_->setModel(sectionModel_);
    sections_->sortBy(SectionTableModel::Lines, SortOrder::Descending);

    links_ += tree_->selectionChanged->connect([this](NodeId n) {
        using Kind = ProjectTreeModel::NodeKind;
        const auto kind = ProjectTreeModel::kindOf(n);
        if (kind == Kind::Section || kind == Kind::DfbSection)
            showSection(ProjectTreeModel::indexOf(n));
    });
    links_ += sections_->activated->connect([this](RowIndex r) {
        showSection(static_cast<domain::Index>(r));
        editors_->setCurrentIndex(0);
    });

    status_->setMessage(formatCount(project->sections.size()) + " sections in "
                        + formatCount(project->pous.size()) + " program organisation units");
}

void ProgramUnitsScreen::onExit() { links_.clear(); }

void ProgramUnitsScreen::showSection(domain::Index index) {
    auto project = app_.project();
    if (!project || index >= project->sections.size()) return;
    const auto& s = project->sections[index];
    source_->setText(s.body);
    editors_->setCurrentIndex(0);
    status_->setMessage(std::string(project->strings.text(s.name)) + " - "
                        + std::string(domain::toString(s.language)) + ", "
                        + std::to_string(s.lineCount) + " lines, "
                        + std::to_string(s.statementCount) + " statements");
}

// Lot API 7 : l'ancien tableau des statistiques en plein ecran (Ctrl+5) est
// parti - l'onglet API > Statistiques (StatisticsPane) le remplace.

// ======================================================== SettingsScreen ====
SettingsScreen::SettingsScreen(App& app) : menu::WidgetMenu("settings"), app_(app) {}

// =============================================================================
//  L'aide
// =============================================================================
HelpScreen::HelpScreen(App& app) : menu::WidgetMenu("help"), app_(app) {}

namespace {

// Le modele de la liste de gauche. Le niveau de titre donne l'indentation :
// un sommaire plat ne dit pas ce qui depend de quoi.
class TocModel final : public IListModel {
public:
    explicit TocModel(std::vector<std::pair<std::string, int>> rows)
        : rows_(std::move(rows)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::string text(RowIndex r) const override {
        if (r >= rows_.size()) return {};
        const auto& [libelle, niveau] = rows_[r];
        return std::string(static_cast<std::size_t>(std::max(0, niveau - 1)) * 3, ' ')
             + libelle;
    }
private:
    std::vector<std::pair<std::string, int>> rows_;
};

} // namespace

void HelpScreen::rebuildToc(const std::string& filter) {
    entries_.clear();
    std::vector<std::pair<std::string, int>> rows;

    if (filter.empty()) {
        for (const auto& t : doc_->toc()) {
            entries_.push_back({t.title, t.anchor, t.level});
            rows.emplace_back(t.title, t.level);
        }
        status_->setMessage(std::to_string(entries_.size()) + " section(s). "
                            "Tapez pour chercher, ou cliquez Index.");
    } else {
        // LES RESULTATS PORTENT LEUR SECTION. Une liste de passages sans dire
        // d'ou ils viennent oblige a cliquer chacun pour savoir lequel est le
        // bon.
        for (const auto& h : doc_->search(filter, 200)) {
            const auto libelle = h.section.empty() ? h.excerpt
                                                   : h.section + "  -  " + h.excerpt;
            entries_.push_back({libelle, h.anchor, 2});
            rows.emplace_back(libelle, 2);
        }
        status_->setMessage(entries_.empty()
            ? "Rien pour \"" + filter + "\". Les accents et la casse ne comptent pas : "
              "ce mot n'est nulle part dans l'aide."
            : std::to_string(entries_.size()) + " passage(s) pour \"" + filter + "\"");
    }
    toc_->setModel(std::make_shared<TocModel>(std::move(rows)));
}

void HelpScreen::showIndex() {
    entries_.clear();
    std::vector<std::pair<std::string, int>> rows;
    for (const auto& e : doc_->index()) {
        for (std::size_t i = 0; i < e.anchors.size(); ++i) {
            // Un terme cite sous trois titres donne trois lignes, numerotees.
            // Une seule ligne qui menerait au premier ferait croire que les deux
            // autres n'existent pas.
            const auto libelle = e.anchors.size() == 1
                ? e.term
                : e.term + "  (" + std::to_string(i + 1) + "/"
                  + std::to_string(e.anchors.size()) + ")";
            entries_.push_back({libelle, e.anchors[i], 1});
            rows.emplace_back(libelle, 1);
        }
    }
    toc_->setModel(std::make_shared<TocModel>(std::move(rows)));
    status_->setMessage(std::to_string(entries_.size()) + " entree(s) d'index");
}

void HelpScreen::onEnter() {
    // F1 A DESIGNE QUELQUE CHOSE DE PRECIS : on ne s'arrete pas au sommaire.
    //
    //  L'ecran d'analyse depose la destination avant de pousser celui-ci -
    //  PushMenu ne transporte pas d'argument. Si la boite aux lettres est
    //  pleine, la vraie page est un etage plus loin, et s'y arreter ferait de
    //  F1 une touche qui ouvre toujours la meme chose.
    //
    //  On NE vide PAS la boite ici : c'est l'ecran des blocs qui la consomme,
    //  et la vider en passant lui ferait ouvrir son premier item.
    //
    //  Lot macros 1 : l'onglet de la destination (une macro -> Macros, un
    //  bloc -> Blocs DFB / DDT), qui REMPLACE cet ecran : Fermer ramene alors
    //  directement d'ou F1 a ete appuye.
    if (help::hasPendingTarget()) {
        const auto t = help::takePendingTarget();
        help::setPendingTarget(t);
        const auto id = helpMenuFor(t, project::SharedLibrary::defaultRoot());
        if (id != "help") app_.menus().ReplaceMenu(id);
    }
    // ---- Lot API 8 : didacticiels et aide ----
    //  Une page demandee (Aide > Raccourcis clavier, F1 sur un onglet du
    //  dossier Simulation, les nouveautes du lot 8) : on y va, et la barre
    //  d'etat dit son titre.
    if (auto anchor = lot8::takePendingHelpAnchor(); !anchor.empty() && view_ && toc_) {
        if (search_) search_->setText("");
        rebuildToc({});
        if (view_->goToAnchor(anchor)) {
            for (const auto& entry : entries_)
                if (entry.anchor == anchor) {
                    if (status_) status_->setMessage(entry.label);
                    break;
                }
        } else if (status_) {
            status_->setMessage("Ce renvoi ne m\xC3\xA8ne nulle part : " + anchor);
        }
    }
    // ---- fin Lot API 8 : didacticiels et aide ----
}

core::Status HelpScreen::buildUi() {
    doc_ = std::make_shared<const ui::HelpDocument>(ui::buildHelp());

    auto root = std::make_unique<DockLayout>("help.root");

    // Lot macros 1 : les quatre onglets de l'aide - Aide, Macros, Blocs DFB /
    // DDT, IHM - au-dessus de tout, et Fermer a droite.
    {
        auto strip = std::make_unique<HelpTabStrip>(HelpTabStrip::General);
        connectHelpTabs(*strip, app_.menus(), links_);
        root->dock(std::move(strip), DockLayout::Side::Top, 42.f);
    }

    auto bar = std::make_unique<BoxLayout>(Orientation::Horizontal, "help.bar");
    bar->setSpacing(6.f);
    bar->addChild(std::make_unique<Label>("  Rechercher", false, nullptr, 0.f));
    auto champ = std::make_unique<InputText>("help.search");
    champ->setPlaceholder("un mot, sans se soucier des accents");
    search_ = &static_cast<InputText&>(bar->addChild(std::move(champ)));
    auto sommaire = std::make_unique<Button>("Sommaire", "help.toc");
    auto index = std::make_unique<Button>("Index", "help.index");
    auto* bSommaire = &static_cast<Button&>(bar->addChild(std::move(sommaire)));
    auto* bIndex = &static_cast<Button&>(bar->addChild(std::move(index)));
    root->dock(std::move(bar), DockLayout::Side::Top, 34.f);

    status_ = &static_cast<StatusBar&>(
        root->dock(std::make_unique<StatusBar>("help.status"),
                   DockLayout::Side::Bottom, 24.f));

    auto liste = std::make_unique<ListView>("help.toc.list");
    // Le sommaire prend 280 px : assez pour un titre de section sans le couper,
    // et pas plus, parce que c'est le document qu'on vient lire.
    toc_ = &static_cast<ListView&>(root->dock(std::move(liste),
                                              DockLayout::Side::Left, 280.f));

    auto vue = std::make_unique<ui::HelpView>("help.view");
    vue->setDocument(doc_);
    view_ = &static_cast<ui::HelpView&>(root->dock(std::move(vue),
                                                   DockLayout::Side::Center, 0.f));

    setRoot(std::move(root));

    rebuildToc({});

    links_ += toc_->selectionChanged->connect([this](RowIndex r) {
        if (r >= entries_.size()) return;
        // UN LIEN CASSE SE DIT. goToAnchor rend faux quand l'ancre n'existe
        // pas ; ne rien faire donnerait un clic sans effet, le genre de defaut
        // qu'on met des mois a signaler parce qu'on croit avoir mal clique.
        if (!view_->goToAnchor(entries_[r].anchor))
            status_->setMessage("Ce renvoi ne mene nulle part : " + entries_[r].anchor);
        else
            status_->setMessage(entries_[r].label);
    });
    links_ += search_->textChanged->connect([this](const std::string& t) {
        view_->setHighlight(t);
        rebuildToc(t);
    });
    links_ += bSommaire->clicked->connect([this] {
        search_->setText("");
        view_->setHighlight("");
        rebuildToc({});
    });
    links_ += bIndex->clicked->connect([this] { showIndex(); });

    // L'AIDE DE LA BIBLIOTHEQUE ET CELLE DE L'IHM sont les onglets de la barre
    // du haut (lot macros 1) : les boutons " Blocs et macros ", " IHM (vues,
    // objets...) " et " Fermer " de cette barre-ci n'ont plus lieu d'etre.

    return core::ok();
}

// =============================================================================
//  Lot 8 : l'aide de l'IHM en plein ecran
// =============================================================================
HmiHelpScreen::HmiHelpScreen(App& app) : menu::WidgetMenu("help.hmi"), app_(app) {}

core::Status HmiHelpScreen::buildUi() {
    auto root = std::make_unique<DockLayout>("help.hmi.root");
    // Lot macros 1 : les quatre onglets de l'aide, IHM choisi ; Fermer est a droite.
    {
        auto strip = std::make_unique<HelpTabStrip>(HelpTabStrip::Hmi);
        connectHelpTabs(*strip, app_.menus(), links_);
        root->dock(std::move(strip), DockLayout::Side::Top, 42.f);
    }
    auto pane = std::make_unique<HmiHelpPane>("help.hmi.pane");
    pane->show(app_.takeHelpTopic("objets"));
    root->dock(std::move(pane), DockLayout::Side::Center, 0.f);
    setRoot(std::move(root));
    return core::ok();
}

core::Status SettingsScreen::buildUi() {
    auto box = std::make_unique<BoxLayout>(Orientation::Vertical, "settings.root");
    box->setSpacing(6.f);
    box->addChild(std::make_unique<Label>("Settings", true));
    box->addChild(std::make_unique<Label>("Choose a category from the navigation rail."));
    setRoot(std::move(box));
    return core::ok();
}

GraphicsSettingsScreen::GraphicsSettingsScreen(App& app) : SettingsScreen(app) {}

core::Status GraphicsSettingsScreen::buildUi() {
    auto box = std::make_unique<BoxLayout>(Orientation::Vertical, "settings.graphics.root");
    box->setSpacing(6.f);
    box->addChild(std::make_unique<Label>("Graphics", true));

    auto vsync = std::make_unique<Checkbox>("Wait for vertical sync");
    vsync->setState(Checkbox::State::Checked);
    box->addChild(std::move(vsync));

    auto rows = std::make_unique<Checkbox>("Alternating row colours in tables");
    rows->setState(Checkbox::State::Checked);
    box->addChild(std::move(rows));

    setRoot(std::move(box));
    return core::ok();
}

ThemeSettingsScreen::ThemeSettingsScreen(App& app) : SettingsScreen(app) {}

core::Status ThemeSettingsScreen::buildUi() {
    auto box = std::make_unique<BoxLayout>(Orientation::Vertical, "settings.theme.root");
    box->setSpacing(6.f);
    box->addChild(std::make_unique<Label>("Th\xC3\xA8me", true));

    // L'ordre suit le defaut : le clair en premier, puisque c'est celui qu'on
    // a sous les yeux en ouvrant cette page.
    group_ = std::make_shared<RadioGroup>();
    // Lot API 6 : les neuf themes (Affichage > Theme... les montre en miniature).
    // ---- Lot API 8 : themes ----
    // Quarante-trois integres et les tiens ne tiennent pas en boutons radio :
    // ici les neuf du lot API 6, le theme applique s'il est ailleurs, et les
    // tiens ; tous sont dans Affichage > Theme... (la galerie).
    auto keys = std::make_shared<std::vector<std::string>>();
    int actif = 0;
    std::size_t builtIn = 0;
    for (const auto& e : ui::Theme::all()) {
        const bool shown = app_.theme().name == e.key;
        if (!e.user && ++builtIn > 9 && !shown) continue;
        // Coche CE QUI EST REELLEMENT ACTIF, au lieu de supposer.
        if (shown) actif = static_cast<int>(keys->size());
        box->addChild(std::make_unique<RadioButton>(e.label + "  -  " + (e.user ? std::string("\xC3\xA0 toi") : e.description), group_,
                                                    static_cast<int>(keys->size())));
        keys->push_back(e.key);
    }
    group_->setValue(actif);
    box->addChild(std::make_unique<Label>("Les " + std::to_string(ui::Theme::all().size())
                                          + " th\xC3\xA8mes, et cr\xC3\xA9" "er ou importer les tiens : Affichage > Th\xC3\xA8me..."));

    links_ += group_->valueChanged->connect([this, keys](int v) {
        if (v >= 0 && static_cast<std::size_t>(v) < keys->size()) app_.setTheme((*keys)[static_cast<std::size_t>(v)]);
    });
    // ---- fin Lot API 8 ----

    setRoot(std::move(box));
    return core::ok();
}

} // namespace app
