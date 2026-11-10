// 1.47.0 : LA PAGE D'ESSAI DU GLOBE (license_admin/gui/PageGlobeTest.cpp) - sans ecran. Elle s'ouvre par son
// identifiant (--page globe-essai) et n'est PAS dans le menu ; elle dit qu'elle montre des villes d'essai ; ses calques et
// ses villes ; << Figer >> (la vue de la planche : Lyon choisie, zoom 1,45, plus rien ne bouge), << Survoler Paris >>
// (l'infobulle), << En direct >> et << Une connexion >>, un clic dans la liste des villes (le globe y va), un calque decoche.
#include "admin_gui_harness.hpp"

#include "../src/license_admin/gui/GlobeBoard.hpp"
#include "../src/license_admin/gui/GlobeStats.hpp"
#include "../src/license_admin/gui/Shell.hpp"
#include "../src/ui/widgets/FilterBar.hpp"
#include "../src/ui/widgets/Globe.hpp"
#include "../src/ui/widgets/GlobeLists.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace admintest;
namespace gl = ui::globe;
namespace stats = license_admin::stats;
using Seconds = licensing::Seconds;

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
    auto* miniCard = find<gui::GlobeMiniCard>(*page, "globe-essai.petite");
    ui::GlobeView* mini = miniCard ? &miniCard->globe() : nullptr;
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
    CHECK(globe->bounds().h > 700.f && !miniCard->visible());   // la carte prend la page
    CHECK(click(*page, "globe-essai.plein"));
    t = paint(*page, r, 1141.f, 853.f);
    CHECK(miniCard->visible());

    // ---- 3. LA VRAIE VUE (Statistiques > Le globe) : des reponses d'essai a la place du service (un faux).
    namespace gs = gui::globestats;
    CHECK(gui::Shell::entry("globe") != nullptr && gui::Shell::parentOf("globe") == "statistiques" && !gui::Shell::plusOnly("globe"));
    stats::GlobeReply fake = gs::sampleReply(30, g_now);
    int globeCalls = 0, liveCalls = 0, lastDays = 0;
    Seconds lastSince = -1;
    bool sameLive = false;
    const auto fakeGlobe = [&](int days, stats::GlobeReply& out) {
        ++globeCalls;
        lastDays = days;
        out = fake;
        out.periodDays = days;
        return gui::OpResult::done({});
    };
    const auto fakeLive = [&](Seconds since, stats::LiveReply& out) {
        ++liveCalls;
        lastSince = since;
        out.at = 5000 + liveCalls;
        out.active = 40 + liveCalls;
        out.pings = {{sameLive ? 4999 : 4990 + liveCalls, sameLive ? "FR|Lyon" : "FR|Paris", "studio"}};
        return gui::OpResult::done({});
    };
    gs::setFetchersForTests({fakeGlobe, fakeLive});
    {
        Fixture fx3(source, "globe-vue");
        HostRig rig3(fx3);
        auto view = gui::statskit::makeStatsGlobeView(rig3.host());
        auto* v = dynamic_cast<gs::GlobeStatsView*>(view.get());
        CHECK(v != nullptr);
        if (v) {
            t = paint(*v, r, 1600.f, 760.f);   // la peinture (t = 0) : le premier releve part
            CHECK(globeCalls == 1 && lastDays == 30 && v->hasReply() && v->board().places().size() == 21 && v->problem().empty());
            t = paint(*v, r, 1600.f, 760.f);
            CHECK(t.find("54 clients, 342 PC, 21 villes, 9 pays") != std::string::npos && t.find("3 PC sans lieu") != std::string::npos);
            CHECK(t.find("P\xC3\xA9riode") != std::string::npos && t.find("Relev\xC3\xA9 le ") != std::string::npos);
            CHECK(v->board().globe().findPlace("FR|Lyon") >= 0 && v->board().emptyTitle().empty());
            CHECK(globeCalls == 1);
            v->tick(100.0);
            CHECK(globeCalls == 1);   // pas encore vieux
            v->tick(301.0);
            CHECK(globeCalls == 2);   // 5 minutes : relu
            // La periode : 90 j, relu tout de suite.
            CHECK(click(*v, "globe.periode.90j"));
            CHECK(globeCalls == 3 && lastDays == 90 && v->periodDays() == 90);
            // En direct : un releve toutes les 6 s, seulement quand la vue est dessinee (tick) ; la connexion pulse sa ville.
            CHECK(click(*v, "globe.direct") && v->board().live());
            v->tick(310.0);
            CHECK(v->livePolls() == 1 && liveCalls == 1 && lastSince == fake.at);
            CHECK(v->board().tickerCity() == "Paris, FR");
            v->tick(313.0);
            CHECK(liveCalls == 1);
            v->tick(316.5);
            CHECK(liveCalls == 2 && lastSince == 5001);   // depuis l'heure du service du releve d'avant
            // ... la vue cachee (plus de tick) : rien ne part, meme longtemps apres.
            CHECK(liveCalls == 2);
            // Une connexion deja vue n'est pas redite.
            sameLive = true;
            v->tick(323.0);
            CHECK(v->board().tickerCity() == "Lyon, FR");
            const int polled = liveCalls;
            v->tick(329.5);
            CHECK(liveCalls == polled + 1 && v->board().tickerCity() == "Lyon, FR");
            sameLive = false;
            // Compte : le direct s'arrete.
            CHECK(click(*v, "globe.compte") && !v->board().live());
            const int polls = liveCalls;
            v->tick(400.0);
            v->tick(410.0);
            CHECK(liveCalls == polls);
            // Aucune ville : la carte le dit au milieu du globe.
            const auto keep = fake;
            fake.cities.clear();
            fake.unplacedPcs = 4;
            v->reload();
            CHECK(v->board().places().empty() && v->board().emptyTitle() == "Aucune ville pour l'instant");
            t = paint(*v, r, 1600.f, 760.f);   // pas de << 0 client, 0 PC, 0 ville >> en haut ; les PC sans lieu, oui
            CHECK(t.find("connexion au service \xC2\xB7 0 client") == std::string::npos && t.find("connexion au service \xC2\xB7 4 PC sans lieu") != std::string::npos);
            fake = keep;
            // Un service d'avant la 1.47.0 : le bandeau et << Deployer le service >>.
            gs::setFetchersForTests({[](int, stats::GlobeReply&) { return gui::OpResult::fail(stats::serviceTooOldText("1.46.0")); }, {}});
            v->reload();
            auto* deploy = v->findById("globe.deployer");
            CHECK(v->tooOld() && deploy && deploy->visible());
            t = paint(*v, r, 1093.f, 760.f);
            CHECK(t.find("d\xC3\xA9ploie la 1.47.0") != std::string::npos);
            // Un refus : sa phrase et << Reessayer >>.
            gs::setFetchersForTests({[](int, stats::GlobeReply&) { return gui::OpResult::fail("le service ne r\xC3\xA9pond pas"); }, {}});
            v->reload();
            auto* retry = v->findById("globe.reessayer");
            CHECK(!v->tooOld() && retry && retry->visible() && deploy && !deploy->visible());
            // Le service pas regle (le vrai chemin, sans reponses d'essai) : << Regler le service >>.
            gs::setFetchersForTests({});
            v->reload();
            auto* settings = v->findById("globe.regler");
            CHECK(v->problem().find("pas r\xC3\xA9gl\xC3\xA9") != std::string::npos && settings && settings->visible());
            CHECK(click(*v, "globe.regler") && !rig3.navigations.empty() && rig3.navigations.back() == "service");
            gs::setFetchersForTests({fakeGlobe, fakeLive});
        }
        // La petite carte du tableau de bord : les PC actifs, les 3 premieres villes ; un clic : le globe.
        auto dashCard = gui::statskit::makeDashboardGlobe(rig3.host());
        t = paint(*dashCard, r, 476.f, 150.f);
        t = paint(*dashCard, r, 476.f, 150.f);
        CHECK(t.find("42 PC actifs maintenant") != std::string::npos && t.find("Lyon, FR") != std::string::npos);
        dashCard->dispatch(ui::MouseDown{{200.f, 75.f}, ui::MouseButton::Left, 1, {}});
        CHECK(!rig3.navigations.empty() && rig3.navigations.back() == "globe");
    }

    // ---- 4. La carte d'une ville dans la vraie coquille : Clients et Activations filtrees sur la ville.
    {
        Fixture fx4(source, "globe-listes");
        menu::MenuManager menus{menu::MenuFactory{}};
        auto owned = std::make_unique<gui::Shell>(U(fx4.admin), [] { return g_now; });
        gui::Shell* shell = owned.get();
        menus.PushMenu(std::move(owned));
        menus.applyPending();
        shell->navigate("globe");
        CHECK(shell->currentPageId() == "globe" && shell->pageTitle() == "Le globe");
        auto* pg = shell->page("globe");
        auto* v = pg ? find<gs::GlobeStatsView>(*pg, "globe.page-vue") : nullptr;
        CHECK(v != nullptr);
        if (v) {
            v->tick(0.0);
            (void)shell->finishSlow();   // le releve (un travail sans bruit de la coquille)
            CHECK(v->hasReply());
            const int lyonIdx = v->board().globe().findPlace("FR|Lyon");
            const auto& customers = fake.cities[static_cast<std::size_t>(lyonIdx)].customers;
            const std::string said = v->openClients(lyonIdx);
            CHECK(shell->currentPageId() == "clients" && said.find("les 9 clients de Lyon, FR") != std::string::npos && said.find("pas pris") == std::string::npos);
            ui::FilterBar* bar = shell->page("clients") ? find<ui::FilterBar>(*shell->page("clients"), "clients") : nullptr;
            CHECK(bar != nullptr);
            if (bar) {
                const int f = bar->fieldByTitle("Client");
                bool kept = false;
                for (const auto& p : bar->state().picks)
                    if (static_cast<int>(p.field) == f) kept = p.values == customers;
                CHECK(f >= 0 && kept);
            }
            // Les PC : l'archive de ce poste ne les connait pas -> les PC des clients de la ville (la colonne Client).
            const std::string said2 = v->openPcs(lyonIdx);
            CHECK(shell->currentPageId() == "activations" && said2.find("les PC des clients de Lyon, FR") != std::string::npos);
            ui::FilterBar* bar2 = shell->page("activations") ? find<ui::FilterBar>(*shell->page("activations"), "activations") : nullptr;
            CHECK(bar2 != nullptr);
            if (bar2) {
                const int f = bar2->fieldByTitle("Client");
                bool kept = false;
                for (const auto& p : bar2->state().picks)
                    if (static_cast<int>(p.field) == f) kept = p.values == customers;
                CHECK(f >= 0 && kept);
            }
        }
    }
    gs::setFetchersForTests({});

    std::printf("%d verifications, %d echec(s)\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
