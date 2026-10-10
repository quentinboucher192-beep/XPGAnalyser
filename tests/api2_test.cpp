// =============================================================================
//  tests/api2_test.cpp - lot API 2 : la barre du haut, le tableau de bord de
//  l'API, les pastilles des macros, les pages gardees des onglets
// -----------------------------------------------------------------------------
//      api2_test <MAST.XPG> [libs]
//
//  1. TopBar : chaque bouton de l'ANCIENNE barre (28) a sa place dans la
//     nouvelle - une partie ou une entree de menu - avec la meme action ; son
//     ancien libelle la retrouve (les sessions de capture d'avant se rejouent).
//  2. TabControl::takeTab : un onglet retire sans que sa page soit detruite
//     (Configuration et Panneaux se ferment et se rouvrent sans perdre leurs
//     widgets relies au projet).
//  3. ApiChecks sur le vrai projet : les entrees de MAST (21 : 18 sections et
//     3 unites), les lignes, les lectures avant l'ecriture, les variables que le
//     programme n'utilise pas, les versions face a la bibliotheque.
//  4. Avec [libs] : les pastilles des 31 macros (ce qu'elles touchent), lues
//     dans leur code.
// =============================================================================
#ifdef NDEBUG
#  undef NDEBUG
#endif

#include "../src/app/TopBar.hpp"
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/ApiChecks.hpp"
#include "../src/project/MacroSpec.hpp"
#include "../src/project/SharedLibrary.hpp"
#include "../src/ui/widgets/Containers.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

std::shared_ptr<domain::Project> load(const std::string& xpg) {
    static core::EventBus bus;
    importer::ProjectImporter importer(bus);
    auto imported = importer.importFile(xpg);
    if (!imported) return nullptr;
    return imported->project;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ---------------------------------------------------------------- 1. la barre ----
void testTopBar() {
    std::printf("1. La barre du haut\n");
    app::TopBar bar("barre");
    const auto& legacy = app::TopBar::legacyButtons();
    check(legacy.size() == 29, "l'ancienne barre : 29 boutons (Menu ... Layout)");
    bool all = true;
    for (const auto& [label, action] : legacy) {
        // Lot API 7 : l'ecran de simulation, l'explorateur de variables et les
        // statistiques sont des onglets de l'API (l'arbre les ouvre) - plus dans
        // Affichage ; leur ancien libelle retrouve toujours l'action (plus bas).
        const bool apiTab = action == "sim.open" || action == "view.variables" || action == "view.statistics";
        if (!apiTab && !bar.reaches(action)) {
            all = false;
            std::printf("       %s (%s) : pas de place dans la nouvelle barre\n", label.c_str(), action.c_str());
        }
        if (bar.actionForLabel(label) != action) {
            all = false;
            std::printf("       %s : l'ancien libelle ne retrouve pas %s\n", label.c_str(), action.c_str());
        }
    }
    check(all, "chaque bouton d'avant est joignable, et son libelle retrouve son action");
    check(bar.actionForLabel("Enregistrer sous\xE2\x80\xA6") == "project.saveAs" && bar.actionForLabel("Enregistrer sous...") == "project.saveAs",
          "une entree de menu par son libelle, avec ou sans les points");
    check(bar.actionForLabel("Historique") == "edit.history" && bar.actionForLabel("Vers Control Expert") == "project.exportSources",
          "une partie de la barre par son libelle");
    check(bar.actionForLabel("Tableau de bord de l'API") == "api.dashboard", "Affichage > Tableau de bord de l'API");
    check(bar.actionForLabel("rien de tel").empty(), "un libelle inconnu : rien");
    std::size_t entries = 0;
    for (const auto m : {app::TopBar::Menu::Project, app::TopBar::Menu::New, app::TopBar::Menu::View, app::TopBar::Menu::Help})
        for (const auto& e : bar.menuEntries(m)) if (!e.separator) ++entries;
    // Projet 9, Nouveau 8, Affichage 6, Aide 4 : le reste des 29 boutons
    // d'avant est une partie de la barre (Annuler, Simuler, Historique...).
    // Lot API 6 : + Icone du projet... (Projet) et + Theme... (Affichage).
    // Lot API 7 : - Ecran de simulation, Explorateur de variables, Statistiques
    // (Affichage 3) : des onglets de l'API. + Disposition : onglets, cote a cote,
    // mosaique, Detacher l'onglet (Affichage 4), Didacticiel de l'API (Aide),
    // Importer un .XPG (nouveau MAST) (Projet).
    // ---- Lot API 8 : didacticiels et aide ---- + Nouveautes du lot 8, Raccourcis clavier (Aide).
    // ---- Lot API 8 : bandeau haut ---- + Fermer le projet (Projet).
    // ---- Lot API 8 : l'explorateur de fichiers ---- + Explorateur de fichiers : l'appli ou le systeme (Affichage).
    // 1.8.0 : + Dossiers de l'application... (Projet), et Exporter le programme
    // lisible... (Projet), que ce compte oubliait : 38 entrees en 1.8.0, pas 37.
    // 1.10 (R2) : le menu Aide en quatre blocs titres (Chercher et lire, Les
    // guides, Apprendre, Nouveautes et support ; un titre compte comme une
    // entree) ; Nouveautes du lot 8 devient Nouveautes... ; + L'API et son
    // programme, Reperes des nouveautes, A propos d'XPGAnalyser (Aide).
    // 1.10.2 (CR) : + Journal interne (Aide). 1.11 (T3) : + Les expressions (Aide).
    // 1.11 (T2, decision 22) : Projet 14, Nouveau 8, Affichage 9, Aide 16 (dont 4 titres).
    // 1.12.3 : + Disposition... (Projet).
    check(entries == 48, "les quatre menus : " + std::to_string(entries) + " entrees");
    check(bar.reaches("project.disposition"), "1.12.3 : Projet > Disposition...");
    check(bar.reaches("layout.tabs") && bar.reaches("layout.groups") && bar.reaches("layout.mosaic") && bar.reaches("layout.detach")
              && bar.reaches("help.apiTutorial") && bar.reaches("file.importMast"),
          "lot API 7 : Disposition (3), Detacher, Didacticiel de l'API, Importer un .XPG dans les menus");
    check(!bar.reaches("sim.open") && !bar.reaches("view.variables") && !bar.reaches("view.statistics")
              && bar.actionForLabel("Simulate") == "sim.open" && bar.actionForLabel("Statistics") == "view.statistics",
          "lot API 7 : simulation, variables, statistiques hors d'Affichage, leurs anciens libelles gardes");
    check(bar.actionForLabel("Ic\xC3\xB4ne du projet\xE2\x80\xA6") == "project.icon" && bar.actionForLabel("Th\xC3\xA8me\xE2\x80\xA6") == "view.theme",
          "lot API 6 : Icone du projet... et Theme... dans les menus");

    std::vector<std::string> fired;
    bar.setActionSink([&](core::ActionId id) { fired.emplace_back(id); });
    bar.trigger("edit.undo");
    bar.setSimulation(app::TopBar::Sim::Running, 1204);
    check(fired.size() == 1 && fired[0] == "edit.undo", "trigger : l'action part, telle quelle");
    check(bar.actionForLabel("Pause") == "sim.pause", "la simulation tourne : Simuler devient Pause");

    // Le menu deroulant est repose a chaque retour sur l'ecran (chaque dialogue
    // qui se ferme) : un choix ne doit partir qu'une fois.
    ui::PopupMenu popup("barre.menu");
    bar.setPopup(&popup);
    bar.setPopup(&popup);
    bar.setPopup(&popup);
    fired.clear();
    bar.openMenu(app::TopBar::Menu::View);
    check(bar.openedMenu() == app::TopBar::Menu::View && popup.isOpen(), "Affichage s'ouvre");
    popup.itemChosen->emit(0);
    check(fired.size() == 1 && fired[0] == "api.dashboard",
          "setPopup trois fois, un choix : une seule action (" + std::to_string(fired.size()) + ")");
    check(bar.openedMenu() == app::TopBar::Menu::None, "le menu se referme");
}

// --------------------------------------------------------- 2. les pages gardees ----
void testTakeTab() {
    std::printf("2. Les pages gardees\n");
    ui::TabControl tabs("onglets");
    auto a = std::make_unique<ui::Widget>("page.a");
    auto b = std::make_unique<ui::Widget>("page.b");
    auto* rawB = b.get();
    tabs.addTab({"A", ui::Icon::None, true, false}, std::move(a));
    tabs.addTab({"B", ui::Icon::None, true, false}, std::move(b));
    tabs.setCurrentIndex(1);
    auto kept = tabs.takeTab(1);
    check(kept.get() == rawB && tabs.tabCount() == 1, "takeTab rend la page, vivante, et retire l'onglet");
    check(kept->parent() == nullptr, "la page n'a plus de parent");
    const auto at = tabs.addTab({"B", ui::Icon::None, true, false}, std::move(kept));
    check(at == 1 && tabs.page(1) == rawB, "et se remet dans un onglet, la meme");
    tabs.removeTab(0);
    check(tabs.tabCount() == 1 && tabs.page(0) == rawB, "removeTab : le voisin reste");
}

// ---------------------------------------------------------- 3. le tableau de bord ----
void testChecks(const std::string& xpg, const std::string& libs) {
    std::printf("3. Le tableau de bord de l'API (%s)\n", xpg.c_str());
    auto p = load(xpg);
    check(p != nullptr, "le projet se charge");
    if (!p) return;
    const auto entries = project::api::entriesOf(*p, "MAST");
    std::size_t units = 0, sections = 0, lines = 0;
    for (const auto& e : entries) {
        if (e.unit) ++units; else ++sections;
        lines += e.lines;
    }
    std::printf("       MAST : %zu entrees (%zu sections, %zu unites), %zu lignes\n", entries.size(), sections, units, lines);
    check(!entries.empty(), "MAST a des entrees");
    check(units == 0 || std::any_of(entries.begin(), entries.end(), [](const auto& e) { return e.unit && e.sections > 1; }),
          "une unite de programme est UNE entree, avec toutes ses sections");

    std::unique_ptr<project::SharedLibrary> lib;
    if (!libs.empty()) {
        lib = std::make_unique<project::SharedLibrary>(libs);
        (void)lib->scan();
    }
    const auto s = project::api::summarize(*p, lib.get());
    check(s.order.size() == entries.size() && s.lines == lines, "summarize reprend l'ordre et les lignes");
    check(s.variables > 0 && s.variables >= s.unused.size(), "les variables globales : " + std::to_string(s.variables)
                                                                + ", dont " + std::to_string(s.unused.size()) + " pas utilisees par le programme");
    check(s.unused.size() < s.variables, "l'analyse a compte les usages (toutes ne sont pas \"pas utilisees\")");
    std::printf("       lectures avant l'ecriture : %zu\n", s.late.size());
    for (std::size_t i = 0; i < s.late.size() && i < 5; ++i)
        std::printf("         %s (rang %zu) lit %s, ecrite par %s (rang %zu)\n", s.late[i].reader.c_str(), s.late[i].readerRank,
                    s.late[i].variable.c_str(), s.late[i].writer.c_str(), s.late[i].writerRank);
    bool ranks = true;
    for (const auto& l : s.late) ranks = ranks && l.readerRank < l.writerRank && l.readerRank >= 1 && l.writerRank <= entries.size();
    check(ranks, "une lecture avant l'ecriture : le lecteur passe AVANT le premier qui ecrit");
    if (lib) {
        std::printf("       plus recents en bibliotheque : %zu\n", s.outdated.size());
        for (const auto& n : s.outdated) std::printf("         %s %s -> %s\n", n.name.c_str(), n.projectVersion.c_str(), n.libraryVersion.c_str());
    }

    // Une lecture avant l'ecriture, construite : B lit X, que C ecrit plus loin.
    // (Le code des sections est celui de l'export ; on n'en fabrique pas ici :
    // la regle est verifiee sur ce que le projet donne, et sur accessOf.)
    if (!entries.empty()) {
        const auto a = project::api::accessOf(*p, entries.front());
        bool disjoint = true;
        for (const auto& w : a.writes) disjoint = disjoint && std::find(a.reads.begin(), a.reads.end(), w) == a.reads.end();
        check(disjoint, "accessOf : ce qu'une entree ecrit n'est pas compte dans ce qu'elle lit");
    }
}

// ------------------------------------------------------------ 4. les pastilles ----
void testTouches(const std::string& libs) {
    if (libs.empty()) return;
    std::printf("4. Les pastilles des macros (%s)\n", libs.c_str());
    const auto dir = fs::path(libs) / "Macros";
    std::size_t n = 0;
    std::map<std::string, project::macro::MacroSpec::Touches> t;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() != ".mac") continue;
        const auto name = e.path().stem().string();
        t[name] = project::macro::parseMacroSpec(readFile(e.path()), name).touches;
        ++n;
    }
    check(n >= 31, std::to_string(n) + " macros lues");
    const auto has = [&](const char* m) { return t.count(m) > 0; };
    if (has("ImporterClasseur")) {
        const auto& x = t["ImporterClasseur"];
        check(x.workbook && x.chains && x.fileOut && !x.sections, "ImporterClasseur : lit le classeur, enchaine, ecrit un fichier, n'ecrit pas de section elle-meme");
    }
    if (has("ImportES")) {
        const auto& x = t["ImportES"];
        check(x.csv && x.sections && x.variables && !x.workbook, "ImportES : lit un CSV, ecrit des sections, cree des variables");
    }
    if (has("RangerSections")) check(t["RangerSections"].order && !t["RangerSections"].sections, "RangerSections : range l'ordre");
    if (has("VerifierBibliotheque")) check(!t["VerifierBibliotheque"].modifiesProject(), "VerifierBibliotheque : ne modifie rien");
    if (has("MettreAJourBibliotheque")) check(t["MettreAJourBibliotheque"].library, "MettreAJourBibliotheque : la bibliotheque");
    std::size_t workbook = 0;
    for (const auto& [name, x] : t) if (x.workbook) ++workbook;
    check(workbook >= 12, std::to_string(workbook) + " macros lisent le classeur");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "api2_test <MAST.XPG> [libs]\n");
        return 2;
    }
    const std::string xpg = argv[1];
    const std::string libs = argc > 2 ? argv[2] : std::string{};
    testTopBar();
    testTakeTab();
    testChecks(xpg, libs);
    testTouches(libs);
    std::printf("%s : %d echec(s)\n", failures ? "ECHEC" : "OK", failures);
    return failures ? 1 : 0;
}
