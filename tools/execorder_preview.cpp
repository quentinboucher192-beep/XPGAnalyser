// =============================================================================
//  tools/execorder_preview.cpp — a quoi ressemble le dossier "Ordre d'execution"
// -----------------------------------------------------------------------------
//  Le meme principe que help_preview : on ne dessine pas une maquette, on fait
//  peindre le VRAI arbre par un renderer SVG. Ce qui sort est ce que SDL
//  afficherait, au rendu des glyphes pres - y compris le trait d'insertion du
//  glisser-deposer, qui est dessine par le widget et par personne d'autre.
//
//      execorder_preview <dossier de sortie>
// =============================================================================
#include "../src/app/ExecutionOrderWiring.hpp"
#include "../src/ui/Theme.hpp"

#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

#include "help_preview_svg.inc"

using namespace domain;
using NK = app::ProjectTreeModel::NodeKind;

std::shared_ptr<Project> buildProject() {
    auto p = std::make_shared<Project>();
    p->header.projectName = "2024_06_264_ALI";

    Task mast;
    mast.name = p->strings.intern("MAST");
    mast.type = "cyclic";
    mast.watchdog = 250;
    p->tasks.push_back(mast);

    struct Row { const char* name; std::uint32_t lines; };
    const Row rows[] = {
        {"Mapping_Entrees", 505}, {"Cablage_Entrees", 1}, {"Reports_TC", 100},
        {"Equipements", 7},       {"Alarmes_Cycle", 13},  {"Reports_TM_TA", 127},
        {"Cablage_Sorties", 1},   {"Mapping_Sorties", 241},
    };
    std::uint32_t order = 0;
    for (const auto& row : rows) {
        Section s;
        s.name = p->strings.intern(row.name);
        s.language = PouLanguage::ST;
        s.task = p->strings.intern("MAST");
        s.order = order;
        s.lineCount = row.lines;
        p->sections.push_back(s);
        const auto si = static_cast<Index>(p->sections.size() - 1);
        Pou pou;
        pou.name = p->sections[si].name;
        pou.kind = PouKind::Section;
        pou.sections.push_back(si);
        p->pous.push_back(pou);
        p->sections[si].owner = static_cast<Index>(p->pous.size() - 1);
        p->tasks[0].sections.push_back(si);
        order += 10;
    }

    // Une unite de programme, pour montrer d'ou vient une etape ; et une
    // section en LD, pour montrer ce que le simulateur ne fera pas tourner.
    {
        Section s;
        s.name = p->strings.intern("Securites");
        s.language = PouLanguage::LD;
        s.order = 25;
        s.lineCount = 42;
        p->sections.push_back(s);
        const auto si = static_cast<Index>(p->sections.size() - 1);
        Pou pou;
        pou.name = p->strings.intern("UniteSecurites");
        pou.kind = PouKind::ProgramUnit;
        pou.task = p->strings.intern("MAST");
        pou.sections.push_back(si);
        p->pous.push_back(pou);
        p->sections[si].owner = static_cast<Index>(p->pous.size() - 1);
    }

    p->buildIndices();
    return p;
}

std::vector<ui::NodeId> visibleNodes(const ui::TreeView& tree,
                                     const app::ProjectTreeModel& model) {
    std::vector<ui::NodeId> out;
    const auto walk = [&](auto&& self, ui::NodeId n) -> void {
        out.push_back(n);
        if (!tree.isExpanded(n)) return;
        for (std::size_t k = 0; k < model.childCount(n); ++k) self(self, model.childAt(n, k));
    };
    walk(walk, model.root());
    return out;
}

void shoot(const std::string& out, const ui::Theme& theme, bool enTrainDeTrainer) {
    constexpr float W = 520.f, H = 480.f;
    SvgRenderer r(W, H);
    ui::installPlatformServices(ui::PlatformServices{
        [&r](std::string_view t, gfx::FontId f) { return r.measure(t, f).width; },
        [&r](gfx::FontId f) { return r.lineHeight(f); },
        [] { return std::string{}; },
        [](std::string_view) {},
    });

    auto project = buildProject();
    auto model = std::make_shared<app::ProjectTreeModel>(project);

    ui::TreeView tree{"arbre"};
    tree.setModel(model);
    tree.setBounds({0.f, 0.f, W, H});
    tree.expand(model->root());
    tree.expand(app::ProjectTreeModel::pack(NK::ExecOrderFolder, 0));

    core::CommandStack pile;
    core::ConnectionScope links;
    app::ExecutionOrderWiring::install(
        tree, model, project,
        [&pile](core::CommandPtr c) { (void)pile.push(std::move(c)); }, links);

    // Une premiere peinture pour connaitre la hauteur de ligne, puis le geste.
    tree.layout();
    r.beginFrame();
    std::vector<ui::Widget*> overlays;
    tree.render(ui::PaintContext{r, theme, {0.f, 0.f, W, H}, 0.0, &overlays});
    r.endFrame();

    if (enTrainDeTrainer) {
        const float rowH = theme.metric.rowHeight;
        const auto visibles = visibleNodes(tree, *model);
        const auto rowOf = [&](ui::NodeId n) {
            for (std::size_t i = 0; i < visibles.size(); ++i) if (visibles[i] == n) return i;
            return std::size_t{0};
        };
        const auto depart = app::ProjectTreeModel::pack(NK::ExecStep, 0, 8);   // Mapping_Sorties
        const auto cible  = app::ProjectTreeModel::pack(NK::ExecStep, 0, 2);   // Securites
        const float y0 = static_cast<float>(rowOf(depart)) * rowH + rowH * 0.5f;
        const float y1 = static_cast<float>(rowOf(cible)) * rowH + rowH * 0.1f;
        tree.dispatch(ui::MouseDown{{240.f, y0}, ui::MouseButton::Left, 1, {}});
        tree.dispatch(ui::MouseMove{{240.f, y0 - 20.f}, {}, {}});
        tree.dispatch(ui::MouseMove{{240.f, y1}, {}, {}});
        // On ne relache PAS : on veut l'image du geste en cours.
    }

    SvgRenderer out2(W, H);
    ui::installPlatformServices(ui::PlatformServices{
        [&out2](std::string_view t, gfx::FontId f) { return out2.measure(t, f).width; },
        [&out2](gfx::FontId f) { return out2.lineHeight(f); },
        [] { return std::string{}; },
        [](std::string_view) {},
    });
    tree.layout();
    out2.beginFrame();
    overlays.clear();
    tree.render(ui::PaintContext{out2, theme, {0.f, 0.f, W, H}, 0.0, &overlays});
    out2.endFrame();

    std::ofstream f(out);
    f << out2.document(theme.color.windowBg);
    std::printf("%s\n", out.c_str());
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    shoot(dir + "/1-ordre-clair.svg",  ui::Theme::light(), false);
    shoot(dir + "/2-ordre-sombre.svg", ui::Theme::dark(),  false);
    shoot(dir + "/3-ordre-glisser.svg", ui::Theme::light(), true);
    return 0;
}
