// 1.47.0 : LA PAGE D'ESSAI DU GLOBE (license_admin/gui/PageGlobeTest.cpp) - sans ecran. Elle s'ouvre par son
// identifiant (--page globe-essai) et n'est PAS dans le menu ; elle dit qu'elle montre des villes d'essai ; ses calques et
// ses villes ; << Figer >> (la vue de la planche : Lyon choisie, zoom 1,45, plus rien ne bouge), << Survoler Paris >>
// (l'infobulle), << En direct >> et << Une connexion >>, un clic dans la liste des villes (le globe y va), un calque decoche.
#include "admin_gui_harness.hpp"

#include "../src/license_admin/gui/Shell.hpp"
#include "../src/ui/widgets/Globe.hpp"
#include "../src/ui/widgets/GlobeLists.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace admintest;
namespace gl = ui::globe;

static int fails = 0, checks = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        ++checks;                                                         \
        if (!(c)) {                                                       \
            std::printf("ECHEC ligne %d : %s\n", __LINE__, #c);           \
            ++fails;                                                      \
        }                                                                 \
    } while (0)

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : ".";
    installMetrics();

    // ---- 1. Cachee : on y va par son identifiant, elle n'est pas dans le menu.
    CHECK(gui::Shell::entry("globe-essai") != nullptr);
    bool inMenu = false;
    for (const auto& e : gui::Shell::entries()) inMenu = inMenu || std::string(e.id) == "globe-essai";
    CHECK(!inMenu);
    CHECK(gui::Shell::parentOf("globe-essai").empty() && !gui::Shell::plusOnly("globe-essai"));
    {
        Fixture fx(source, "globe-essai");
        menu::MenuManager menus{menu::MenuFactory{}};
        auto owned = std::make_unique<gui::Shell>(U(fx.admin), [] { return g_now; });
        gui::Shell* shell = owned.get();
        menus.PushMenu(std::move(owned));
        menus.applyPending();
        shell->navigate("globe-essai");
        CHECK(shell->currentPageId() == "globe-essai");
        CHECK(shell->page("globe-essai") != nullptr);
        CHECK(shell->pageTitle() == "Le globe (essai)");
        CHECK(shell->widgetRoot()->findById("nav.globe-essai") == nullptr);   // aucune entree du menu
    }

    // ---- 2. La page seule (sur un ShellHost d'essai)
    Fixture fx(source, "globe-page");
    HostRig rig(fx);
    auto page = gui::makeGlobeTestPage(rig.host());
    TextRenderer r;
    std::string t = paint(*page, r, 1141.f, 853.f);
    CHECK(t.find("Le globe") != std::string::npos);
    CHECK(t.find("page d'essai") != std::string::npos);
    CHECK(t.find("rien du service") != std::string::npos);
    CHECK(t.find("D'o\xC3\xB9 se connectent vos clients") != std::string::npos);
    CHECK(t.find("CALQUES") != std::string::npos && t.find("3 sur 7") != std::string::npos);
    CHECK(t.find("Les villes") != std::string::npos && t.find("21 villes \xC2\xB7 9 pays") != std::string::npos);
    CHECK(t.find("Lyon, FR") != std::string::npos);
    CHECK(t.find("request.cf") != std::string::npos);
    auto* globe = find<ui::GlobeView>(*page, "globe-essai.globe");
    auto* layers = find<ui::GlobeLayerList>(*page, "globe-essai.calques");
    auto* cities = find<ui::GlobeCityList>(*page, "globe-essai.villes");
    auto* mini = find<ui::GlobeView>(*page, "globe-essai.petite");
    CHECK(globe && layers && cities && mini);
    if (!globe || !layers || !cities || !mini) {
        std::printf("%d verifications, %d echec(s)\n", checks, fails);
        return 1;
    }
    CHECK(globe->places().size() == 21 && !globe->frozen() && globe->autoRotate());
    CHECK(mini->compact() && mini->layers().has(gl::Layer::ActivePcs) && mini->layers().count() == 1);
    const int lyon = globe->findPlace("FR|Lyon"), paris = globe->findPlace("FR|Paris"), tunis = globe->findPlace("TN|Tunis");
    CHECK(lyon >= 0 && paris >= 0 && tunis >= 0);

    // << Figer >> : la vue de la planche (l'Europe, Lyon choisie, zoom 1,45), plus rien ne bouge ; la carte de Lyon.
    auto* freeze = find<ui::Checkbox>(*page, "globe-essai.fige");
    CHECK(freeze != nullptr);
    if (freeze) freeze->setState(ui::Checkbox::State::Checked);
    CHECK(globe->frozen() && globe->selected() == lyon && cities->selected() == lyon);
    CHECK(std::fabs(globe->view().lat - 45.76) < 1e-6 && std::fabs(globe->view().lon - 4.85) < 1e-6 && std::fabs(globe->view().zoom - 1.45f) < 1e-5);
    t = paint(*page, r, 1141.f, 853.f);
    CHECK(t.find("Auvergne-Rh\xC3\xB4ne-Alpes") != std::string::npos);
    CHECK(t.find("Camille Essai") != std::string::npos);
    CHECK(t.find("sur 64 PC") != std::string::npos);
    // Une fenetre etroite : la carte couvrirait Lyon - le disque glisse a gauche, Lyon reste a cote de la carte.
    const auto* card = page->findById("globe-essai.carte");
    const auto lyonAt = globe->screenOf(lyon);
    CHECK(card && lyonAt && lyonAt->x < card->bounds().x - 30.f);
    const gl::View before = globe->view();
    globe->advance(0.1);
    CHECK(globe->view().lon == before.lon);   // fige
    // << Survoler Paris >> : la souris sur Paris, son infobulle.
    CHECK(click(*page, "globe-essai.survol"));
    CHECK(globe->hoveredPlace() == paris);
    CHECK(globe->hoverText().rfind("Paris, FR", 0) == 0);
    // Fermer la carte.
    CHECK(click(*page, "globe-essai.fermer"));
    CHECK(globe->selected() == -1 && cities->selected() == -1);
    // Un clic dans la liste : le globe y va (fige : tout de suite), la ville est choisie.
    cities->placeChosen->emit(tunis);
    CHECK(globe->selected() == tunis && std::fabs(globe->view().lat - 36.81) < 1e-6 && std::fabs(globe->view().lon - 10.18) < 1e-6);
    // Un calque decoche : le globe le perd ; le dernier reste.
    CHECK(layers->toggle(gl::Layer::Outdated));
    CHECK(!globe->layers().has(gl::Layer::Outdated) && globe->layers().count() == 2);
    CHECK(layers->toggle(gl::Layer::Admins) && globe->layers().has(gl::Layer::Admins));
    // En direct : les PC actifs classent les villes ; une connexion fait pulser une ville d'essai.
    CHECK(click(*page, "globe-essai.direct"));
    CHECK(cities->layer() == gl::Layer::ActivePcs && globe->layers().has(gl::Layer::ActivePcs));
    CHECK(click(*page, "globe-essai.connexion"));
    t = paint(*page, r, 1141.f, 853.f);
    CHECK(t.find("PC connect\xC3\xA9s maintenant") != std::string::npos);
    CHECK(click(*page, "globe-essai.compte"));
    CHECK(cities->layer() != gl::Layer::ActivePcs || globe->layers().has(gl::Layer::ActivePcs));
    // Pause, zoom, Recentrer, plein ecran : ils agissent.
    if (freeze) freeze->setState(ui::Checkbox::State::Unchecked);
    CHECK(!globe->frozen() && globe->selected() == -1);
    CHECK(click(*page, "globe-essai.pause"));
    CHECK(!globe->autoRotate());
    const float z0 = globe->view().zoom;
    CHECK(click(*page, "globe-essai.zoom-plus"));
    CHECK(globe->view().zoom > z0);
    CHECK(click(*page, "globe-essai.recentrer"));
    CHECK(click(*page, "globe-essai.plein"));
    t = paint(*page, r, 1141.f, 853.f);
    CHECK(globe->bounds().h > 700.f && !mini->visible());   // la carte prend la page
    CHECK(click(*page, "globe-essai.plein"));
    t = paint(*page, r, 1141.f, 853.f);
    CHECK(mini->visible());

    std::printf("%d verifications, %d echec(s)\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
