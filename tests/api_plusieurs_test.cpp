// =============================================================================
//  tests/api_plusieurs_test.cpp - 1.24.1 : supprimer plusieurs d'un coup, cote application
// -----------------------------------------------------------------------------
//  Le client (05/11) : << partout pouvoir supprimer plusieurs d'un coup >> ; puis
//  << corrige les limites principales >> de la 1.24.0. Les volets de l'automate
//  (API Studio) et les listes de l'Assistant IA vivent dans xpg_app : ils
//  s'essaient ici, sur le projet d'essai (MAST.XPG et CONFIG.XHW) et sur un
//  dossier IA/ jetable, sans ecran.
//
//    - les volets qui supprimaient deja a plusieurs, sans essai jusque-la : les
//      variables, les tables d'animation, les types derives (DDT) ;
//    - ceux qui supprimaient une ligne a la fois : les taches, les modules (Ctrl+clic
//      dans les racks), les unites et leurs sections, les blocs DFB ;
//    - l'Assistant IA, Memoire et apprentissage : les connaissances (Apprentissage),
//      la Memoire, les Souvenirs - 1.25.1 : dans la corbeille de l'IA (IA/corbeille),
//      UN Ctrl+Z les rend (avant : une confirmation qui le disait definitif) ; les
//      conversations du panneau aussi.
//    - 1.25.1 : les fichiers deposes (leur chemin, s'il est sur le disque) ; le
//      fournisseur des chemins hors de l'ecran du projet (l'accueil, le centre d'aide).
//
//  Chaque fois : << Supprimer les N >>, UNE question au plus, UN Ctrl+Z, ce qui reste
//  et pourquoi dans le compte rendu, les gestes d'une seule ligne grises.
//  API_PLUSIEURS_DETAIL=1 : chaque controle reussi est ecrit. API_PLUSIEURS_RES=1 : les
//  essais de la 1.25.1 seuls (l'Assistant IA, les fichiers deposes, les chemins).
// =============================================================================
#include "../src/app/AnimationTablesPane.hpp"
#include "../src/app/ApiPanes.hpp"
#include "../src/app/ConfigurationPane.hpp"
#include "../src/app/RackView.hpp"
#include "../src/app/TaskPanes.hpp"
#include "../src/app/TypePanes.hpp"
#include "../src/app/VariablesPane.hpp"
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiMemoryDialog.hpp"
#include "../src/app/ai/AiPanel.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/DropFilesDialog.hpp"     // 1.25.1 : le chemin d'un fichier depose
#include "../src/app/screens/Screens.hpp"     // 1.25.1 : installBasicPathLinks (l'accueil, le centre d'aide)
#include "../src/core/Trash.hpp"              // 1.25.1 : la corbeille de l'IA
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/widgets/PropertyGridHooks.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/app/hmi/HmiPanels.hpp"
#include "../src/core/Command.hpp"
#include "../src/core/EventBus.hpp"
#include "../src/import/ProjectImporter.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace mem = app::ai::memory;

namespace {

int checks = 0, failures = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    static const bool detail = [] {
        const char* d = std::getenv("API_PLUSIEURS_DETAIL");
        return d && *d == '1';
    }();
    if (!ok) {
        ++failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (detail) {
        std::printf("  ok     %s\n", what.c_str());
    }
}
bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Un renderer muet (8 px par octet) : dessiner place ce qui ne se place qu'au dessin (les cases des racks).
class QuietRenderer final : public gfx::IRenderer {
public:
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view, gfx::FontId, gfx::Color) override {}
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId) const override {
        return {8.f * static_cast<float>(s.size()), 16.f, 12.f, 4.f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId) const override { return 16.f; }
    [[nodiscard]] gfx::Size surfaceSize() const override { return {1600.f, 900.f}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId, float maxWidth) const override {
        return std::min(s.size(), static_cast<std::size_t>(std::max(0.f, maxWidth) / 8.f));
    }
};

// La question unique (app::several) : l'essai la lit et repond oui (ou non).
struct Question {
    int         asked{0};
    bool        yes{true};
    std::string title, text, button;
};
Question q;

// Le projet d'essai et ce que les volets demandent a l'ecran.
struct Api {
    std::shared_ptr<domain::Project> project;
    core::CommandStack               stack;
    std::string                      status;
    app::ApiPaneHosts hosts() {
        app::ApiPaneHosts h;
        h.view = [this] { return std::shared_ptr<const domain::Project>(project); };
        h.project = [this] { return project; };
        h.apply = [this](core::CommandPtr c) { (void)stack.push(std::move(c)); };
        h.status = [this](const std::string& s) { status = s; };
        return h;
    }
    [[nodiscard]] std::string text(domain::SymbolId id) const { return std::string(project->strings.text(id)); }
};

std::shared_ptr<domain::Project> load(const std::string& xpg) {
    core::EventBus bus;
    importer::ProjectImporter imp(bus);
    std::vector<std::string> files{xpg};
    const fs::path xhw = fs::path(xpg).parent_path() / "CONFIG.XHW";
    if (fs::exists(xhw)) files.push_back(xhw.string());
    auto res = imp.importFiles(files);
    bus.drain();
    if (!res) {
        std::printf("projet d'essai illisible : %s\n", res.error().message().c_str());
        return nullptr;
    }
    return res->project;
}

// Choisir plusieurs lignes : `pick` choisit chacune (elle depliee au besoin) ; puis toutes ensemble.
std::size_t chooseRows(ui::TableView& t, const std::vector<std::string>& names, const std::function<bool(const std::string&)>& pick) {
    for (const auto& n : names) (void)pick(n);   // d'abord tout deplier (les lignes ne bougent plus ensuite)
    std::vector<ui::RowIndex> rows;
    for (const auto& n : names)
        if (pick(n) && !t.selectedModelRows().empty()) rows.push_back(t.selectedModelRows().front());
    t.selectModelRows(rows);
    return rows.size();
}

template <class Pane>
struct Framed {
    Pane*         pane{nullptr};
    std::unique_ptr<app::ApiFrame> frame;
    explicit Framed(std::string id) {
        auto p = std::make_unique<Pane>(id + ".volet");
        pane = p.get();
        frame = std::make_unique<app::ApiFrame>(std::move(id), std::move(p));
        pane->attach(*frame);
    }
    app::HmiToolStrip& tools() { return frame->tools(); }
    void layout() {
        frame->setBounds({0, 0, 1600, 900});
        frame->layout();
    }
};

// ---------------------------------------------------------------- les variables (1.24.0, sans essai jusque-la) ----
void variables(Api& api) {
    std::printf("== les variables de l'automate : plusieurs d'un coup ==\n");
    Framed<app::VariablesPane> f("api.variables");
    f.pane->setHosts(api.hosts());
    f.layout();
    auto& p = *api.project;
    std::vector<std::string> names;
    for (auto it = p.variables.rbegin(); it != p.variables.rend() && names.size() < 3; ++it)
        if (it->scope == domain::VariableScope::Global) names.push_back(api.text(it->name));
    const std::size_t before = p.variables.size();
    const std::size_t n = chooseRows(f.pane->table(), names, [&](const std::string& v) { return f.pane->selectVariable(v); });
    check(names.size() == 3 && n == 3, "Variables : 3 variables globales choisies (" + std::to_string(n) + ")");
    const int del = app::VariablesPane::ADelete, ren = app::VariablesPane::ARename;
    check(f.tools().labelOf(del) == "Supprimer les 3", "le bouton : " + f.tools().labelOf(del));
    check(f.tools().isEnabled(del) && !f.tools().isEnabled(ren), "Supprimer actif, Renommer gris\xC3\xA9");
    q = Question{};
    f.pane->runAction(del);
    // Une variable de ce nom (un nom peut se retrouver ailleurs : une locale, un doublon de l'export).
    const auto alive = [&](const std::string& name) {
        return std::any_of(p.variables.begin(), p.variables.end(), [&](const domain::Variable& v) { return api.text(v.name) == name; });
    };
    const std::size_t gone = before - p.variables.size();
    check(q.asked <= 1, "une question au plus (si l'une emporte quelque chose)");
    check(gone >= 1 && gone <= 3, "elles partent (" + std::to_string(gone) + " sur 3) ; " + api.status);
    check(has(api.status, std::to_string(gone) + " variable") && (gone == 3 || has(api.status, "refus")), "le compte rendu : " + api.status);
    check(api.stack.undo() && p.variables.size() == before && alive(names[0]) && alive(names[1]) && alive(names[2]), "UN Ctrl+Z les rend toutes");
    f.pane->refresh();
}

// ------------------------------------------------------- les tables d'animation (1.24.0, sans essai jusque-la) ----
void animationTables(Api& api) {
    std::printf("== les tables d'animation : plusieurs d'un coup ==\n");
    auto& p = *api.project;
    for (const char* n : {"Essai_A", "Essai_B", "Essai_C"}) (void)api.stack.push(std::make_unique<project::AddAnimationTableCommand>(api.project, n, ""));
    Framed<app::AnimationTablesPane> f("api.tables");
    app::AnimationTablesPane::Hosts h;
    h.project = [&api] { return api.project; };
    h.apply = [&api](core::CommandPtr c) { (void)api.stack.push(std::move(c)); };
    h.status = [&api](const std::string& s) { api.status = s; };
    f.pane->setHosts(std::move(h));
    f.layout();
    f.pane->refresh();
    const std::size_t before = p.animationTables.size();
    check(before >= 3, "trois tables ajout\xC3\xA9" "es (" + std::to_string(before) + ")");
    std::vector<ui::RowIndex> rows;
    for (std::size_t i = 0; i < p.animationTables.size(); ++i)
        if (api.text(p.animationTables[i].name).rfind("Essai_", 0) == 0) rows.push_back(static_cast<ui::RowIndex>(i));
    f.pane->tables().selectModelRows(rows);
    const int del = app::AnimationTablesPane::ADelete, ren = app::AnimationTablesPane::ARename;
    check(rows.size() == 3 && f.tools().labelOf(del) == "Supprimer les 3", "le bouton : " + f.tools().labelOf(del));
    check(!f.tools().isEnabled(ren), "Renommer gris\xC3\xA9 (une \xC3\xA0 la fois)");
    q = Question{};
    f.pane->runAction(del);
    check(q.asked == 0 && p.animationTables.size() == before - 3, "les 3 tables partent, sans question (comme une seule)");
    check(has(api.status, "3 tables supprim"), "le compte rendu : " + api.status);
    check(api.stack.undo() && p.animationTables.size() == before, "UN Ctrl+Z les rend");
}

// ------------------------------------------------------------------- les types derives (1.24.0, sans essai) ----
void derivedTypes(Api& api) {
    std::printf("== les types d\xC3\xA9riv\xC3\xA9s (DDT) : plusieurs d'un coup ==\n");
    Framed<app::DerivedTypesPane> f("api.types");
    f.pane->setHosts(api.hosts());
    f.layout();
    auto& p = *api.project;
    std::vector<std::string> names;
    for (auto it = p.derivedTypes.rbegin(); it != p.derivedTypes.rend() && names.size() < 3; ++it) names.push_back(api.text(it->name));
    const std::size_t before = p.derivedTypes.size();
    const std::size_t n = chooseRows(f.pane->table(), names, [&](const std::string& t) { return f.pane->selectType(t); });
    const int del = app::DerivedTypesPane::ADelete, ren = app::DerivedTypesPane::ARename;
    check(n == 3 && f.tools().labelOf(del) == "Supprimer les 3", "Types : 3 choisis, le bouton : " + f.tools().labelOf(del));
    check(!f.tools().isEnabled(ren), "Renommer gris\xC3\xA9");
    q = Question{};
    f.pane->runAction(del);
    check(q.asked == 1 && has(q.title, "Supprimer les 3 types"), "une seule question : " + q.title);
    const std::size_t gone = before - p.derivedTypes.size();
    check(gone <= 3 && has(api.status, "supprim") && (gone == 3 || has(api.status, "refus")),
          "les types non employ\xC3\xA9s partent, les autres restent (dit) : " + api.status);
    if (gone > 0) check(api.stack.undo() && p.derivedTypes.size() == before, "UN Ctrl+Z les rend");
    f.pane->refresh();
}

// ---------------------------------------------------------------------------------- les taches (1.24.1) ----
void tasks(Api& api) {
    std::printf("== les t\xC3\xA2" "ches : plusieurs d'un coup ==\n");
    Framed<app::TasksPane> f("api.taches");
    f.pane->setHosts(api.hosts());
    f.layout();
    auto& p = *api.project;
    const std::size_t base = p.tasks.size();
    f.pane->runAction(app::TasksPane::AAddFast);
    f.pane->runAction(app::TasksPane::AAddAux);
    f.pane->refresh();
    check(p.tasks.size() == base + 2, "FAST et AUX ajout\xC3\xA9" "es (" + std::to_string(p.tasks.size()) + " t\xC3\xA2" "ches)");
    std::vector<ui::RowIndex> rows;
    const auto model = f.pane->table().model();
    for (std::size_t r = 0; model && r < model->rowCount(); ++r) {
        const std::string name = model->cellText(static_cast<ui::RowIndex>(r), 0);
        if (name == "MAST" || name == "FAST" || name.rfind("AUX", 0) == 0) rows.push_back(static_cast<ui::RowIndex>(r));
    }
    f.pane->table().selectModelRows(rows);
    const int del = app::TasksPane::ARemove;
    check(f.pane->chosenTasks().size() == 3 && f.tools().labelOf(del) == "Supprimer les 3" && f.tools().isEnabled(del),
          "MAST, FAST, AUX choisies : Supprimer les 3 (" + f.tools().labelOf(del) + ")");
    q = Question{};
    f.pane->table().takeFocus();
    (void)f.pane->table().dispatch(ui::KeyDown{ui::Key::Delete, {}, false});
    check(q.asked == 0 && p.tasks.size() == base, "Suppr : FAST et AUX partent, sans question");
    check(has(api.status, "2 t\xC3\xA2" "ches supprim") && has(api.status, "MAST") && has(api.status, "refus"), "MAST reste, le compte rendu le dit : " + api.status);
    check(api.stack.undo() && p.tasks.size() == base + 2, "UN Ctrl+Z les rend");
    check(api.stack.undo() && api.stack.undo() && p.tasks.size() == base, "(les deux ajouts annul\xC3\xA9s)");
}

// ------------------------------------------------------------------------- les modules (1.24.1, les racks) ----
void modules(Api& api) {
    std::printf("== les modules : Ctrl+clic dans les racks, plusieurs d'un coup ==\n");
    auto& hw = api.project->hardware;
    std::size_t total = 0;
    for (const auto& r : hw.racks) total += r.modules.size();
    if (hw.racks.empty() || total < 3) {
        check(false, "la configuration d'essai (CONFIG.XHW) : des racks et des modules");
        return;
    }
    Framed<app::ConfigurationPane> f("api.configuration");
    f.pane->setHosts(api.hosts());
    f.pane->showTab(app::ConfigurationPane::TRacks);
    f.layout();
    f.pane->refresh();
    // Deux modules d'entrees / sorties et le processeur.
    std::vector<app::RackView::Place> chosen;
    const domain::Module* cpu = nullptr;
    for (const auto& r : hw.racks)
        for (const auto& m : r.modules) {
            if (m.isCpu && !cpu) {
                cpu = &m;
                chosen.emplace_back(r.number, m.slot);
            } else if (!m.isCpu && m.slot > 0 && chosen.size() < 3 && !(r.number == 0 && m.slot == 0)) {
                chosen.emplace_back(r.number, m.slot);
            }
        }
    f.pane->racks().setChosen(chosen);
    const int del = app::ConfigurationPane::ARemove, rep = app::ConfigurationPane::AReplace;
    check(chosen.size() == 3 && f.pane->racks().chosenModules().size() == 3, "trois modules choisis (dont le processeur)");
    check(f.tools().labelOf(del) == "Supprimer les 3" && f.tools().isEnabled(del) && !f.tools().isEnabled(rep),
          "Supprimer les 3 ; Remplacer gris\xC3\xA9 (" + f.tools().labelOf(del) + ")");
    q = Question{};
    f.pane->runAction(del);
    std::size_t after = 0;
    for (const auto& r : hw.racks) after += r.modules.size();
    const bool hadCpu = cpu != nullptr;
    check(after == total - (hadCpu ? 2u : 3u), "les modules d'entr\xC3\xA9" "es / sorties partent (" + std::to_string(total - after) + ")");
    check(!hadCpu || (has(api.status, "processeur") && has(api.status, "refus")), "le processeur reste, c'est dit : " + api.status);
    check(static_cast<bool>(api.stack.undo()), "UN Ctrl+Z");
    std::size_t back = 0;
    for (const auto& r : hw.racks) back += r.modules.size();
    check(back == total, "... les remet tous");
    // Ctrl+clic a la souris : un module s'ajoute au choix, un clic simple revient a un seul.
    f.pane->refresh();
    f.layout();
    {   // les cases des modules se placent au dessin
        QuietRenderer r;
        f.frame->invalidate();
        f.frame->render(ui::PaintContext{r, ui::Theme::dark(), {0.f, 0.f, 1600.f, 900.f}, 0.0, nullptr});
    }
    gfx::Rect a{}, b{};
    if (chosen.size() >= 3 && f.pane->racks().slotRect(chosen[1].first, chosen[1].second, a)
        && f.pane->racks().slotRect(chosen[2].first, chosen[2].second, b)) {
        auto& rv = f.pane->racks();
        rv.dispatch(ui::MouseDown{{a.x + a.w * 0.5f, a.y + a.h * 0.5f}, ui::MouseButton::Left, 1, {}});
        ui::KeyMods ctrl;
        ctrl.ctrl = true;
        rv.dispatch(ui::MouseDown{{b.x + b.w * 0.5f, b.y + b.h * 0.5f}, ui::MouseButton::Left, 1, ctrl});
        check(rv.chosenModules().size() == 2 && f.tools().labelOf(del) == "Supprimer les 2", "Ctrl+clic : deux modules choisis");
        rv.dispatch(ui::MouseDown{{a.x + a.w * 0.5f, a.y + a.h * 0.5f}, ui::MouseButton::Left, 1, {}});
        check(rv.chosenModules().size() == 1, "un clic simple : un seul");
    } else {
        check(false, "les cases des modules choisis a l'ecran");
    }
}

// ----------------------------------------------------------- les unites et leurs sections (1.24.1) ----
void unitsAndSections(Api& api) {
    std::printf("== les unit\xC3\xA9s et leurs sections : plusieurs d'un coup ==\n");
    auto& p = *api.project;
    Framed<app::UnitsPane> f("api.unites");
    f.pane->setHosts(api.hosts());
    f.layout();
    // Une unite qui a au moins deux sections.
    std::vector<std::string> names;
    for (const auto& u : p.pous)
        if (u.kind == domain::PouKind::ProgramUnit && u.sections.size() >= 2 && names.empty())
            for (std::size_t k = 0; k < 2; ++k) names.push_back(api.text(p.sections[u.sections[k]].name));
    if (names.size() < 2) {
        check(false, "le projet d'essai : une unit\xC3\xA9 de deux sections");
        return;
    }
    const std::size_t before = p.sections.size();
    const std::size_t n = chooseRows(f.pane->table(), names, [&](const std::string& s) { return f.pane->selectSection(s); });
    const int del = app::UnitsPane::ADelete, ren = app::UnitsPane::ARename, code = app::UnitsPane::AOpenCode;
    check(n == 2 && f.pane->chosenDeletable().size() == 2, "deux sections choisies");
    check(f.tools().labelOf(del) == "Supprimer les 2" && f.tools().isEnabled(del) && !f.tools().isEnabled(ren) && !f.tools().isEnabled(code),
          "Supprimer les 2 ; Renommer, Ouvrir le code gris\xC3\xA9s (" + f.tools().labelOf(del) + ")");
    q = Question{};
    f.pane->table().takeFocus();
    (void)f.pane->table().dispatch(ui::KeyDown{ui::Key::Delete, {}, false});
    check(q.asked <= 1 && p.sections.size() == before - 2, "Suppr : les deux sections partent (" + std::to_string(before - p.sections.size()) + ")");
    check(has(api.status, "2 sections supprim"), "le compte rendu : " + api.status);
    check(api.stack.undo() && p.sections.size() == before, "UN Ctrl+Z les rend");
    f.pane->refresh();
}

// ---------------------------------------------------------------------------------- les blocs DFB (1.24.1) ----
void blocks(Api& api) {
    std::printf("== les blocs DFB : plusieurs d'un coup ==\n");
    auto& p = *api.project;
    Framed<app::DfbPane> f("api.dfb");
    f.pane->setHosts(api.hosts());
    f.layout();
    std::vector<std::string> names;
    for (const auto& u : p.pous)
        if (u.kind == domain::PouKind::FunctionBlockType && u.userDefined && names.size() < 3) names.push_back(api.text(u.name));
    const std::size_t before = p.pous.size();
    const std::size_t n = chooseRows(f.pane->table(), names, [&](const std::string& b) { return f.pane->selectBlock(b); });
    const int del = app::DfbPane::ADelete, ren = app::DfbPane::ARename;
    check(names.size() == 3 && n == 3 && f.pane->chosenBlocks().size() == 3, "trois blocs choisis");
    check(f.tools().labelOf(del) == "Supprimer les 3" && !f.tools().isEnabled(ren), "Supprimer les 3 ; Renommer gris\xC3\xA9 (" + f.tools().labelOf(del) + ")");
    q = Question{};
    f.pane->runAction(del);
    const std::size_t gone = before > p.pous.size() ? 1u : 0u;   // au moins un bloc parti (avec ce qui lui appartient)
    check(q.asked <= 1, "une question au plus");
    check(has(api.status, "bloc") && (has(api.status, "supprim") || has(api.status, "refus")), "le compte rendu : " + api.status);
    // Un bloc qui a des instances reste (comme seul) ; s'il en est parti, un Ctrl+Z les rend.
    if (gone) check(api.stack.undo() && p.pous.size() == before, "UN Ctrl+Z les rend");
    else check(has(api.status, "refus"), "aucun ne part : tous ont des instances, c'est dit");
}

// --------------------------------------------------------------- l'Assistant IA : Memoire et apprentissage ----
void aiMemory() {
    std::printf("== l'Assistant IA : supprimer plusieurs connaissances, souvenirs, m\xC3\xA9moires d'un coup ==\n");
    const fs::path root = fs::temp_directory_path() / "xpg_api_plusieurs_ia";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "IA", ec);
    app::ai::setRoot((root / "IA").string());
    using L = app::AiMemoryDialog::List;
    app::AiMemoryDialog d(app::AiMemoryDialog::Spec{});
    check(static_cast<bool>(d.Initialize()), "la fen\xC3\xAAtre se construit");
    std::vector<std::string> ids;
    for (const char* n : {"Regle_A", "Regle_B", "Regle_C", "Regle_D"}) {
        mem::Knowledge k;
        k.name = n;
        k.category = "preferences";
        k.content = std::string("Contenu de ") + n;
        check(d.addKnowledge(k) && d.selectedKnowledge(), std::string("ajouter ") + n);
        if (d.selectedKnowledge()) ids.push_back(d.selectedKnowledge()->id);
    }
    const std::size_t before = d.registry().all().size();
    core::CommandStack stack;   // 1.25.1 : Ctrl+Z
    core::CommandGroupScope::registerStack(&stack);
    // Apprentissage : trois connaissances choisies, une confirmation (1.25.1 : la corbeille, Ctrl+Z), puis elles partent.
    check(ids.size() == 4 && d.selectKeys(L::Learning, {ids[0], ids[1], ids[2]}) && d.chosenKeys(L::Learning).size() == 3,
          "Apprentissage : trois connaissances choisies");
    check(d.deleteSelected() && d.confirming(), "Supprimer : une confirmation");
    check(has(d.confirmText(), "les 3 connaissances") && has(d.confirmText(), "Regle_A") && has(d.confirmText(), "corbeille de l'IA")
              && has(d.confirmText(), "un seul Ctrl+Z") && !has(d.confirmText(), "d\xC3\xA9" "finitif"),
          "elle nomme ce qui part, dit la corbeille et le Ctrl+Z (plus << d\xC3\xA9" "finitif >>) : " + d.confirmText());
    d.confirmNo();
    check(d.registry().all().size() == before, "Non : rien ne part");
    check(d.selectKeys(L::Learning, {ids[0], ids[1], ids[2]}) && d.deleteSelected() && d.confirmYes(), "Oui");
    check(d.registry().all().size() == before - 3 && has(d.message(), "3 connaissances supprim") && has(d.message(), "un seul Ctrl+Z"),
          "les trois partent ; le message : " + d.message());
    // 1.25.1 : la corbeille de l'IA (cachee de l'Assistant), puis UN Ctrl+Z.
    check(core::trash::list(app::ai::trashDir()).size() == 3 && !fs::exists(fs::path(app::ai::knowledgeDir()) / "corbeille"),
          "leurs fichiers dans IA/corbeille (" + std::to_string(core::trash::list(app::ai::trashDir()).size()) + ")");
    {
        mem::Registry fresh;
        fresh.load();
        check(fresh.all().size() == before - 3, "l'IA ne les lit plus (un registre relu)");
    }
    check(stack.done().size() == 1 && stack.undo() && d.registry().all().size() == before && d.registry().find(ids[0]) && d.registry().find(ids[2]),
          "UN Ctrl+Z : les trois reviennent (la fen\xC3\xAAtre les relit)");
    check(stack.redo() && d.registry().all().size() == before - 3, "Ctrl+Y : de nouveau dans la corbeille");
    check(stack.undo() && d.registry().all().size() == before, "Ctrl+Z encore");
    check(d.selectKeys(L::Learning, {ids[0], ids[1], ids[2]}) && d.deleteSelected() && d.confirmYes() && d.registry().all().size() == before - 3,
          "pour la suite : elles repartent");
    // La purge au demarrage : plus de 7 jours.
    {
        const fs::path old = fs::path(app::ai::trashDir()) / "20200101-000000~vieux.json";
        { std::ofstream f(old); f << "{}"; }
        check(mem::purgeTrash() == 1 && !fs::exists(old) && core::trash::list(app::ai::trashDir()).size() == 3,
              "la purge au d\xC3\xA9marrage : ce qui a plus de 7 jours part, le reste attend");
    }
    // La Memoire (la categorie des memoires) : une ajoutee et une du logiciel - celle-ci reste, c'est dit.
    d.showMemoryCategory(app::AiMemoryDialog::MemCat::Preferences);
    std::string builtin;
    for (const auto& k : d.registry().all())
        if (k.builtIn && builtin.empty()) builtin = k.id;
    const bool memRows = d.selectKeys(L::Memory, {"k:" + ids[3], "k:" + builtin});
    if (memRows) {
        check(d.deleteMemorySelection() && d.confirming() && has(d.confirmText(), "int\xC3\xA9gr\xC3\xA9" "e au logiciel"),
              "M\xC3\xA9moire : une confirmation qui dit ce qui reste : " + d.confirmText());
        check(d.confirmYes() && d.registry().find(ids[3]) == nullptr && d.registry().find(builtin) != nullptr, "l'ajout\xC3\xA9" "e part, celle du logiciel reste");
    } else {
        // Les preferences du logiciel ne sont pas dans cette categorie : deux ajoutees suffisent.
        mem::Knowledge k;
        k.name = "Regle_E";
        k.category = "preferences";
        k.content = "E";
        check(d.addKnowledge(k) && d.selectedKnowledge(), "ajouter Regle_E");
        const std::string e = d.selectedKnowledge() ? d.selectedKnowledge()->id : std::string{};
        d.showMemoryCategory(app::AiMemoryDialog::MemCat::Preferences);
        check(d.selectKeys(L::Memory, {"k:" + ids[3], "k:" + e}) && d.deleteMemorySelection() && d.confirming(), "M\xC3\xA9moire : deux choisies, une confirmation");
        check(d.confirmYes() && d.registry().find(ids[3]) == nullptr && d.registry().find(e) == nullptr, "M\xC3\xA9moire : les deux partent");
    }
    // Souvenirs : deux souvenirs d'un coup.
    std::vector<std::string> souv;
    for (const char* n : {"Souvenir_1", "Souvenir_2"}) {
        mem::Knowledge k;
        k.name = n;
        k.category = "preferences";
        k.content = n;
        if (d.addKnowledge(k) && d.selectedKnowledge()) souv.push_back(d.selectedKnowledge()->id);
    }
    d.setSouvenirFilter("", "", "");
    if (souv.size() == 2 && d.selectKeys(L::Souvenirs, souv)) {
        check(d.deleteSelectedSouvenir() && d.confirming() && has(d.confirmText(), "les 2 souvenirs"), "Souvenirs : une confirmation pour les deux : " + d.confirmText());
        check(d.confirmYes() && !d.registry().find(souv[0]) && !d.registry().find(souv[1]), "Souvenirs : les deux partent");
    } else {
        check(false, "Souvenirs : deux souvenirs montr\xC3\xA9s et choisis");
    }
    check(stack.undo() && souv.size() == 2 && d.registry().find(souv[0]) && d.registry().find(souv[1]), "Souvenirs : UN Ctrl+Z les rend");
    core::CommandGroupScope::registerStack(nullptr);
    app::ai::setRoot({});
    fs::remove_all(root, ec);
}

// ------------------------------------------------- l'Assistant IA : la liste des conversations du panneau ----
void aiPanelConversations() {
    std::printf("== l'Assistant IA : plusieurs conversations du panneau d'un coup ==\n");
    const fs::path root = fs::temp_directory_path() / "xpg_api_plusieurs_panneau";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "IA", ec);
    app::ai::setRoot((root / "IA").string());
    auto doc = std::make_shared<hmi::Document>();
    core::CommandStack stack;
    app::ai::Assistant::Host core;
    core.document = [doc] { return doc; };
    core.activeView = [] { return hmi::kNoId; };
    core.selection = [] { return std::vector<hmi::Id>{}; };
    core.apply = [&stack](core::CommandPtr c) { (void)stack.push(std::move(c)); };
    core.projectName = [] { return std::string("Essai"); };
    core.projectPath = [root] { return (root / "Essai").string(); };
    app::ai::AiPanel::UiHost ui;   // sans fenetre de confirmation (un essai) : le geste se fait
    core::CommandStack undo;       // 1.25.1 : Ctrl+Z
    core::CommandGroupScope::registerStack(&undo);
    {
        app::ai::AiPanel panel("ia.panneau.plusieurs", std::move(core), std::move(ui));
        panel.setBounds({0, 0, 440, 900});
        panel.ensureLoaded();
        panel.newConversation();
        panel.layout();
        const auto& list = panel.assistant().conversations();
        const std::size_t before = list.size();
        check(before >= 3, "trois conversations (les deux de d\xC3\xA9monstration et une neuve) : " + std::to_string(before));
        check(panel.chosenConversations().size() == 1, "une seule choisie au d\xC3\xA9part (l'ouverte)");
        panel.toggleChosen(1);
        panel.toggleChosen(2);
        check(panel.chosenConversations().size() == 3, "Ctrl+clic : trois choisies");
        panel.toggleChosen(0);
        check(panel.chosenConversations().size() == 2, "Ctrl+clic sur l'ouverte : elle sort du choix");
        const std::string kept = list[0].id;
        const std::size_t onDisk = app::ai::loadConversations().size();   // 1.25.1 : la neuve n'est pas encore ecrite
        panel.askDeleteChosen();
        check(panel.assistant().conversations().size() == before - 2 && panel.assistant().conversations()[0].id == kept,
              "les deux partent, l'ouverte reste");
        check(has(panel.lastStatus(), "2 conversations supprim") && has(panel.lastStatus(), "un seul Ctrl+Z"), "le compte rendu : " + panel.lastStatus());
        check(panel.chosenConversations().size() == 1, "apr\xC3\xA8s : une seule choisie");
        // 1.25.1 : leurs fichiers dans la corbeille de l'IA, que l'Assistant ne relit pas ; UN Ctrl+Z les rend.
        check(!core::trash::list(app::ai::trashDir()).empty() && app::ai::loadConversations().size() == onDisk - 2,
              "leurs fichiers dans IA/corbeille ; relues du disque : " + std::to_string(app::ai::loadConversations().size()));
        check(undo.done().size() == 1 && undo.undo() && panel.assistant().conversations().size() == before && app::ai::loadConversations().size() == onDisk,
              "UN Ctrl+Z : les deux reviennent (le panneau et le disque)");
        check(undo.redo() && panel.assistant().conversations().size() == before - 2, "Ctrl+Y : reparties");
        check(undo.undo() && panel.assistant().conversations().size() == before, "Ctrl+Z encore");
    }
    core::CommandGroupScope::registerStack(nullptr);
    app::ai::setRoot({});
    fs::remove_all(root, ec);
}

// ------------------------------------------- 1.25.1 : les chemins hors de l'ecran du projet, les fichiers deposes ----
void cheminsHorsProjet() {
    std::printf("== 1.25.1 : les chemins hors de l'\xC3\xA9" "cran du projet ; les fichiers d\xC3\xA9pos\xC3\xA9s ==\n");
    const fs::path dir = fs::temp_directory_path() / "xpg_api_chemins_1251";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "Projet", ec);
    { std::ofstream f(dir / "logo.png", std::ios::binary); f << "PNG"; }
    const auto saved = ui::paths::provider();
    // ---- le fournisseur de l'accueil, du centre d'aide, de la fenetre du rapport (sans App : pas d'apercu) ----
    std::vector<std::string> said;
    std::vector<std::string> opened;
    app::installBasicPathLinks(nullptr, [&](const std::string& why) { said.push_back(why); },
                               [&](const std::string& path, const std::string&, bool dry) -> std::optional<std::string> {
                                   if (path != (dir / "Projet").string()) return std::nullopt;   // un projet de la liste : l'ouvrir
                                   if (!dry) opened.push_back(path);
                                   return std::string{};
                               });
    check(ui::paths::active(), "le fournisseur est l\xC3\xA0 (l'accueil)");
    const auto project = ui::paths::menuItems((dir / "Projet").string(), 40);
    const auto find = [](const std::vector<ui::PopupMenu::Item>& items, const std::string& label) -> const ui::PopupMenu::Item* {
        for (const auto& it : items)
            if (it.label == label) return &it;
        return nullptr;
    };
    const auto* go = find(project, "Aller \xC3\xA0");
    check(project.size() == 3 && go && go->enabled && find(project, "Ouvrir dans l'explorateur")->enabled && find(project, "Copier le chemin")->enabled,
          "la carte d'un projet : Ouvrir dans l'explorateur, Aller \xC3\xA0, Copier le chemin");
    check(ui::paths::run(40 + ui::paths::kGoTo, 40, (dir / "Projet").string()).empty() && opened.size() == 1, "Aller \xC3\xA0 : le projet s'ouvre");
    const auto file = ui::paths::menuItems((dir / "logo.png").string(), 40);
    const auto* goFile = find(file, "Aller \xC3\xA0");
    check(goFile && !goFile->enabled && has(goFile->disabledReason, "aper\xC3\xA7u"), "un fichier sans App (la fen\xC3\xAAtre du rapport) : Aller \xC3\xA0 gris\xC3\xA9 - " + (goFile ? goFile->disabledReason : std::string("?")));
    const auto absent = ui::paths::menuItems((dir / "absent.txt").string(), 40);
    const auto* rev = find(absent, "Ouvrir dans l'explorateur");
    check(rev && !rev->enabled && has(rev->disabledReason, "n'existe pas"), "un fichier absent : gris\xC3\xA9, avec la raison");
    // ---- les fichiers deposes : le menu si le fichier est sur le disque ----
    {
        std::vector<std::string> revealed;
        ui::paths::Provider pr;
        pr.reveal = [&](const std::string& r) { revealed.push_back(r); return std::string{}; };
        pr.goTo = [](const std::string&, const std::string&, bool) { return std::string{}; };
        ui::paths::setProvider(pr);
        auto plan = std::make_shared<app::dropfiles::Plan>();
        app::dropfiles::FileRow real;
        real.path = (dir / "logo.png").string();
        real.name = "logo.png";
        plan->files.push_back(real);
        app::dropfiles::FileRow temp;
        temp.path = (dir / "piece-jointe-temporaire.pdf").string();   // un nom temporaire deja parti
        temp.name = "piece-jointe-temporaire.pdf";
        plan->files.push_back(temp);
        menu::MenuManager menus{menu::MenuFactory{}};
        auto dialog = std::make_unique<app::DropFilesDialog>(plan);
        auto* d = dialog.get();
        menus.ShowDialog(std::move(dialog), {});
        menus.applyPending();
        check(d->pathOf("logo.png") == real.path && d->pathOf("piece-jointe-temporaire.pdf").empty() && d->pathOf("inconnu").empty(),
              "le chemin d'une ligne : le fichier sur le disque ; un nom temporaire parti, rien");
        check(d->openPathMenu("logo.png", {200.f, 200.f}) && !d->openPathMenu("piece-jointe-temporaire.pdf", {200.f, 200.f}),
              "le clic droit : le menu pour logo.png, rien pour le fichier parti");
    }
    ui::paths::setProvider(saved);
    fs::remove_all(dir, ec);
}

} // namespace

int main(int argc, char** argv) {
    const std::string xpg = argc > 1 ? argv[1] : "tests/fixtures/MAST.XPG";
    app::several::setConfirmHost([](const std::string& title, const std::string& text, const std::string& button, std::function<void()> yes) {
        ++q.asked;
        q.title = title;
        q.text = text;
        q.button = button;
        if (q.yes && yes) yes();
    });
    {
        ui::PlatformServices s;
        s.measureWidth = [](std::string_view t, gfx::FontId) { return 8.f * static_cast<float>(t.size()); };
        s.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v) + 4.f; };
        ui::installPlatformServices(std::move(s));
    }
    // 1.25.1 : API_PLUSIEURS_RES=1 - l'Assistant IA (la corbeille), les chemins hors du projet, les fichiers deposes, seuls.
    if (const char* only = std::getenv("API_PLUSIEURS_RES"); only && *only == '1') {
        aiMemory();
        aiPanelConversations();
        cheminsHorsProjet();
        app::several::setConfirmHost({});
        std::printf("%d controles, %d echec(s)\n", checks, failures);
        return failures == 0 ? 0 : 1;
    }
    Api api;
    api.project = load(xpg);
    check(api.project != nullptr, "le projet d'essai se lit : " + xpg);
    if (api.project) {
        core::CommandGroupScope::registerStack(&api.stack);
        variables(api);
        animationTables(api);
        derivedTypes(api);
        tasks(api);
        modules(api);
        unitsAndSections(api);
        blocks(api);
        core::CommandGroupScope::registerStack(nullptr);
    }
    aiMemory();
    aiPanelConversations();
    cheminsHorsProjet();   // 1.25.1
    app::several::setConfirmHost({});
    std::printf("%d controles, %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
