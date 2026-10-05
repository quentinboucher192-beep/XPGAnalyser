// =============================================================================
//  tests/api34_test.cpp - lots API 3 et 4 : les tables d'animation (avec l'IHM),
//  l'ordre d'execution en entrees, les taches, les voies et adresses, le plan
//  memoire, le materiel
// -----------------------------------------------------------------------------
//      api34_test <MAST.XPG> <CONFIG.XHW>
//
//  1. Tables d'animation : l'export relu a l'identique ; les lignes de l'IHM
//     ecrites (ihm ; nom), relues, jamais exportees vers Control Expert ; un
//     ancien fichier (que des "entry") se lit tel quel ; chaque commande
//     s'annule et se refait a l'identique.
//  2. L'ordre d'execution : une unite de programme tourne EN BLOC a son rang
//     (<programUnitDesc SectionOrder>) - Init d'abord, les unites en 15..17 ;
//     la deplacer la deplace entiere ; Ctrl+Z rend tout ; l'export relu garde
//     l'ordre.
//  3. Les taches : cyclique / periodique, periode, chien de garde, + FAST, en
//     commandes ; la periode ecrite dans tasks.txt et dans l'export.
//  4. Voies et adresses (MAST.XPG + CONFIG.XHW) : les adresses du code face aux
//     racks ; le plan des %MW ; le reseau.
//  5. Le materiel : importer un .XHW dans le projet ouvert, retirer, remplacer
//     un module - et Ctrl+Z.
// =============================================================================
#ifdef NDEBUG
#  undef NDEBUG
#endif

#include "../src/domain/ExecutionOrder.hpp"
#include "../src/export/XpgWriter.hpp"
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/ApiCommands.hpp"
#include "../src/project/EditCommands.hpp"
#include "../src/project/IoCheck.hpp"
#include "../src/project/ProjectStore.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace domain;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

std::shared_ptr<Project> load(const std::vector<std::string>& paths) {
    static core::EventBus bus;
    importer::ProjectImporter importer(bus);
    auto imported = paths.size() == 1 ? importer.importFile(paths.front()) : importer.importFiles(paths);
    if (!imported) {
        std::printf("       import : %s\n", imported.error().message().c_str());
        return nullptr;
    }
    return std::const_pointer_cast<Project>(imported->project);
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string tablesSignature(const Project& p) {
    std::string s;
    for (const auto& t : p.animationTables) {
        s += std::string(p.strings.text(t.name)) + "@" + std::string(p.strings.text(t.owner)) + ":";
        for (const auto& e : t.entries) s += (e.hmi ? "ihm." : "") + std::string(p.strings.text(e.name)) + ",";
        s += "|";
    }
    return s;
}

std::string orderListing(const Project& p) {
    std::string out;
    for (const auto& e : executionEntries(p, "MAST")) {
        if (!out.empty()) out += " ";
        out += e.unit ? std::string(p.strings.text(p.pous[e.pou].name)) + "*" + std::to_string(e.sections.size())
                      : std::string(p.strings.text(p.sections[e.section].name));
    }
    return out;
}

fs::path tempDir(const char* what) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto d = fs::temp_directory_path() / (std::string("api34_") + what + "_" + std::to_string(stamp));
    fs::create_directories(d);
    return d;
}

// ------------------------------------------------------ 1. les tables ----
void testTables(const std::string& xpg) {
    std::printf("1. Les tables d'animation\n");
    auto p = load({xpg});
    check(p != nullptr, "le projet se charge");
    if (!p) return;
    check(p->animationTables.size() == 5, "5 tables dans l'export (" + std::to_string(p->animationTables.size()) + ")");
    const auto start = tablesSignature(*p);

    // Le dossier : ecrit puis relu a l'identique.
    const auto dir = tempDir("tables");
    project::Manifest manifest;
    check(static_cast<bool>(project::ProjectStore::save(*p, manifest, dir.string())), "le projet s'ecrit en dossier");
    auto reopened = project::ProjectStore::open(dir.string());
    check(reopened && tablesSignature(*reopened->project) == start, "et se relit : les tables a l'identique");

    // Les commandes, chacune annulee.
    std::vector<core::CommandPtr> done;
    const auto run = [&](core::CommandPtr c, const std::string& what) {
        const auto r = c->execute();
        check(static_cast<bool>(r), what + (r ? "" : " : " + r.error().message()));
        if (r) done.push_back(std::move(c));
    };
    const auto owner = project::defaultAnimationTableOwner(*p);
    check(owner == "Gestion_armoires", "une nouvelle table va dans l'unite des autres (" + owner + ")");
    run(std::make_unique<project::AddAnimationTableCommand>(p, "ESSAI", owner), "creer ESSAI");
    check(!project::AddAnimationTableCommand(p, "essai", owner).execute(), "un nom deja pris (sans la casse) est refuse");
    check(!project::AddAnimationTableCommand(p, "2x", owner).execute(), "un nom qui commence par un chiffre est refuse");
    const std::size_t essai = p->animationTables.size() - 1;
    run(std::make_unique<project::AddAnimationLinesCommand>(p, essai,
            std::vector<project::AnimationLine>{{"armoires[0].ana.PT1.mes", false}, {"Vanne_Purge", true}, {"Purge_Auto", true}}),
        "ajouter une ligne API et deux lignes IHM");
    check(p->animationTables[essai].entries.size() == 3 && p->animationTables[essai].entries[1].hmi, "3 lignes, la 2e vient de l'IHM");
    check(!project::AddAnimationLinesCommand(p, essai, {{"Vanne_Purge", true}}).execute(), "une ligne deja la n'est pas ajoutee deux fois");
    run(std::make_unique<project::MoveAnimationLineCommand>(p, essai, 2, 0), "deplacer la 3e ligne en tete");
    check(p->strings.text(p->animationTables[essai].entries[0].name) == "Purge_Auto", "Purge_Auto est en tete");
    run(std::make_unique<project::RenameAnimationTableCommand>(p, essai, "PURGE_IHM"), "renommer ESSAI en PURGE_IHM");
    run(std::make_unique<project::AddAnimationTableCommand>(p, project::freeAnimationTableName(*p, "PURGE_IHM"), owner,
            std::vector<project::AnimationLine>{{"x", false}}, essai + 1, true), "dupliquer (un nom libre : PURGE_IHM_2)");
    check(p->strings.text(p->animationTables[essai + 1].name) == "PURGE_IHM_2", "la copie s'appelle PURGE_IHM_2");
    run(std::make_unique<project::RemoveAnimationLinesCommand>(p, essai, std::vector<std::size_t>{1}), "retirer une ligne");
    run(std::make_unique<project::RemoveAnimationTableCommand>(p, 0), "supprimer la table ARM");

    // Les lignes IHM : ecrites "ihm ; nom", relues, pas exportees.
    const auto withHmi = tablesSignature(*p);
    const auto dir2 = tempDir("tables2");
    check(static_cast<bool>(project::ProjectStore::save(*p, manifest, dir2.string())), "le projet modifie s'ecrit");
    const auto file = readFile(dir2 / "tables" / "animation.txt");
    check(file.find("ihm ; Vanne_Purge") != std::string::npos, "tables/animation.txt : ihm ; Vanne_Purge");
    auto again = project::ProjectStore::open(dir2.string());
    check(again && tablesSignature(*again->project) == withHmi, "relu : les lignes IHM gardent leur source");
    const auto xml = exporter::writeXpg(*p);
    check(xml && xml->find("PURGE_IHM") != std::string::npos, "l'export ecrit la table PURGE_IHM");
    check(xml && xml->find("\"Vanne_Purge\"") == std::string::npos && xml->find("\"Purge_Auto\"") == std::string::npos,
          "mais jamais ses lignes IHM (Control Expert ne les connait pas)");
    check(xml && xml->find("armoires[0].ana.PT1.mes") != std::string::npos, "sa ligne API, si");

    // Un ancien fichier : que des "entry".
    {
        const auto dir3 = tempDir("old");
        check(static_cast<bool>(project::ProjectStore::save(*reopened->project, manifest, dir3.string())), "un ancien projet");
        std::ofstream(dir3 / "tables" / "animation.txt") << "# Watch tables.\ntable ; OLD ; Gestion_armoires\nentry ; armoires\nentry ; %M12\n";
        auto old = project::ProjectStore::open(dir3.string());
        check(old && old->project->animationTables.size() == 1 && old->project->animationTables[0].entries.size() == 2
                  && !old->project->animationTables[0].entries[1].hmi,
              "un ancien tables/animation.txt se lit tel quel");
    }

    // Tout defaire, dans l'ordre inverse : le projet revient.
    for (auto it = done.rbegin(); it != done.rend(); ++it) (void)(*it)->undo();
    check(tablesSignature(*p) == start, "Ctrl+Z sur chaque commande : les tables reviennent a l'identique");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::remove_all(dir2, ec);
}

// ---------------------------------------------------- 2. l'ordre d'execution ----
void testOrder(const std::string& xpg) {
    std::printf("2. L'ordre d'execution : les unites en bloc\n");
    auto p = load({xpg});
    if (!p) return;
    const auto listing = orderListing(*p);
    std::printf("       %s\n", listing.c_str());
    const auto entries = executionEntries(*p, "MAST");
    check(entries.size() == 21, "21 entrees dans MAST");
    check(entries.size() == 21 && p->strings.text(p->sections[entries[0].section].name) == "Init", "Init tourne en premier");
    check(entries.size() == 21 && entries[14].unit && p->strings.text(p->pous[entries[14].pou].name) == "Logigrammes_A"
              && entries[14].sections.size() == 17,
          "Logigrammes_A au rang 15, avec ses 17 sections (comme dans <taskDesc>)");
    check(entries.size() == 21 && p->strings.text(p->sections[entries[17].section].name) == "Gestion_sorties",
          "Gestion_sorties au rang 18, apres les trois unites");
    const auto steps = executionOrder(*p, "MAST");
    check(steps.size() == 65, "65 sections a executer (" + std::to_string(steps.size()) + ")");

    // Deplacer une unite (par l'une de ses sections) : l'unite entiere bouge.
    const auto unitSection = entries[15].sections[3];            // une section de Logigrammes_B
    project::ReorderSectionCommand up(p, unitSection, -2);
    check(static_cast<bool>(up.execute()), "monter Logigrammes_B de deux rangs");
    const auto moved = executionEntries(*p, "MAST");
    check(moved.size() == 21 && moved[13].unit && p->strings.text(p->pous[moved[13].pou].name) == "Logigrammes_B"
              && moved[13].sections.size() == 17,
          "Logigrammes_B au rang 14, toutes ses sections avec elle");
    // L'export relu garde l'ordre.
    const auto xml = exporter::writeXpg(*p);
    const auto dir = tempDir("order");
    const auto path = (dir / "MAST.XPG").string();
    std::ofstream(path, std::ios::binary) << (xml ? *xml : std::string());
    auto re = load({path});
    check(re && orderListing(*re) == orderListing(*p), "l'export relu garde le nouvel ordre");
    check(static_cast<bool>(up.undo()) && orderListing(*p) == listing, "Ctrl+Z : l'ordre de depart, unites comprises");

    // Deposer une section avant une unite : elle se place devant le bloc.
    const auto affichage = entries[13].section;
    const auto beforeUnit = entries[16].sections[5];             // une section de Gestion_armoires
    project::ReorderSectionCommand drop(p, affichage, project::ReorderSectionCommand::Before{beforeUnit});
    check(static_cast<bool>(drop.execute()), "deposer Affichage sur une section de Gestion_armoires");
    const auto dropped = executionEntries(*p, "MAST");
    check(dropped.size() == 21 && p->strings.text(p->sections[dropped[15].section].name) == "Affichage"
              && dropped[16].unit,
          "Affichage passe juste devant le bloc Gestion_armoires");
    check(static_cast<bool>(drop.undo()) && orderListing(*p) == listing, "et Ctrl+Z la remet");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// --------------------------------------------------------------- 3. les taches ----
void testTasks(const std::string& xpg) {
    std::printf("3. Les taches\n");
    auto p = load({xpg});
    if (!p) return;
    check(p->tasks.size() == 1 && p->tasks[0].watchdog == 250, "MAST, chien de garde 250 ms");
    project::SetTaskCommand periodic(p, 0, "periodic", 20, 300);
    check(static_cast<bool>(periodic.execute()) && p->tasks[0].type == "periodic" && p->tasks[0].period == 20,
          "MAST periodique 20 ms, chien de garde 300 ms");
    check(!project::SetTaskCommand(p, 0, "periodic", 0, 300).execute(), "une periode de 0 ms est refusee");
    check(!project::SetTaskCommand(p, 0, "cyclic", 0, 5).execute(), "un chien de garde de 5 ms est refuse");
    project::AddTaskCommand fast(p, "FAST");
    check(static_cast<bool>(fast.execute()) && p->tasks.size() == 2 && p->tasks[1].period == 5, "+ FAST : periodique, 5 ms");
    check(!project::AddTaskCommand(p, "FAST").execute(), "FAST une deuxieme fois : refuse");
    check(!project::AddTaskCommand(p, "LENTE").execute(), "une tache au nom inconnu : refusee");
    check(!project::RemoveTaskCommand(p, 0).execute(), "MAST ne se retire pas");

    const auto dir = tempDir("tasks");
    project::Manifest manifest;
    check(static_cast<bool>(project::ProjectStore::save(*p, manifest, dir.string())), "le projet s'ecrit");
    auto re = project::ProjectStore::open(dir.string());
    check(re && re->project->tasks.size() == 2 && re->project->tasks[0].period == 20 && re->project->tasks[1].period == 5,
          "tasks.txt garde les periodes");
    const auto xml = exporter::writeXpg(*p);
    check(xml && xml->find("taskType=\"periodic\" value=\"20\"") != std::string::npos, "l'export ecrit la periode de MAST");
    project::RemoveTaskCommand removeFast(p, 1);
    check(static_cast<bool>(removeFast.execute()) && p->tasks.size() == 1, "retirer FAST (vide)");
    (void)removeFast.undo();
    (void)fast.undo();
    (void)periodic.undo();
    check(p->tasks.size() == 1 && p->tasks[0].type == "cyclic" && p->tasks[0].watchdog == 250, "Ctrl+Z : MAST comme avant");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ------------------------------------------------ 4. voies, memoire, reseau ----
void testIo(const std::string& xpg, const std::string& xhw) {
    std::printf("4. Voies et adresses, plan memoire, reseau (%s)\n", xhw.c_str());
    auto bare = load({xpg});
    if (bare) {
        const auto r = project::io::check(*bare);
        check(!r.hardware && r.faulty == 0 && !r.addresses.empty(), "sans .XHW : les adresses du code, rien a comparer ("
                                                                      + std::to_string(r.addresses.size()) + ")");
    }
    auto p = load({xpg, xhw});
    check(p != nullptr && !p->hardware.racks.empty(), "MAST.XPG + CONFIG.XHW : des racks");
    if (!p) return;
    const auto r = project::io::check(*p);
    std::size_t noModule = 0, wrong = 0, ok = 0;
    for (const auto& a : r.addresses) {
        if (a.status == project::io::Address::Status::NoModule) ++noModule;
        else if (a.status == project::io::Address::Status::WrongDirection) ++wrong;
        else if (a.status == project::io::Address::Status::Ok) ++ok;
    }
    std::printf("       %zu adresses : %zu justes, %zu sans module, %zu dans le mauvais sens\n", r.addresses.size(), ok, noModule, wrong);
    for (const auto& a : r.addresses)
        if (a.faulty() && (a.status == project::io::Address::Status::WrongDirection))
            std::printf("         %s : %s (%s)\n", a.text.c_str(), project::io::verdict(a).c_str(), a.users.empty() ? "-" : a.users.front().c_str());
    check(r.addresses.size() >= 30, "des dizaines d'adresses topologiques");
    check(noModule > 0, "des adresses sans module (le rack 0 n'a que 8 emplacements)");
    check(wrong > 0, "des adresses dans le mauvais sens (lire une entree sur un module de sorties)");
    check(ok > 0, "et des adresses justes");
    bool usage = false;
    for (const auto& m : r.modules) usage = usage || (m.used > 0 && m.used <= m.total);
    check(usage, "les modules comptent les voies que le programme emploie");

    const auto mem = project::io::memoryOf(*p);
    std::printf("       plan memoire : %zu variables situees, %%MW%u a %%MW%u, place libre %%MW%u -> %%MW%u (%u mots), %zu %%MW en direct\n",
                mem.variables.size(), mem.low, mem.high, mem.gapFrom, mem.gapTo, mem.gapSize(), mem.directReferences);
    check(mem.variables.size() >= 50, "des dizaines de variables situees en %MW");
    check(mem.gapSize() > 0 && mem.gapFrom > mem.low && mem.gapTo < mem.high, "la plus grande place libre est entre deux variables");
    check(mem.overlaps.empty(), "pas de chevauchement dans ce projet");
    check(mem.directReferences > 0, "le code nomme aussi des %MW en direct");
    const auto* first = project::io::variableAt(mem, mem.low);
    check(first != nullptr, "le premier mot a sa variable");

    const auto net = project::io::networkOf(*p);
    std::printf("       reseau : %zu ports\n", net.size());
    check(net.size() >= 2, "les ports Ethernet des BMX NOC0401 et le port serie du processeur");
}

// ------------------------------------------------------------ 5. le materiel ----
void testHardware(const std::string& xpg, const std::string& xhw) {
    std::printf("5. Le materiel\n");
    auto p = load({xpg});
    auto h = load({xhw});
    if (!p || !h) return;
    check(p->hardware.racks.empty(), "le projet n'a pas de racks");
    project::ReplaceHardwareCommand import(p, h->hardware, "CONFIG.XHW");
    const bool imported = static_cast<bool>(import.execute());
    check(imported && p->hardware.racks.size() == h->hardware.racks.size() && !p->hardware.inferred,
          "importer le .XHW dans le projet ouvert : " + std::to_string(p->hardware.racks.size()) + " racks");
    check(p->hardware.cpuReference == "BMX P34 2020", "le processeur du projet est garde (" + p->hardware.cpuReference + ")");
    const auto modules = p->hardware.totalModules();
    std::int16_t slot = -1;
    std::uint16_t rackNo = 0;
    for (const auto& r : p->hardware.racks)
        for (const auto& m : r.modules)
            if (slot < 0 && !m.isCpu && m.slot > 0 && (m.kind == ModuleKind::DiscreteOutput)) { slot = m.slot; rackNo = r.number; }
    check(slot > 0, "un module de sorties a retirer");
    project::RemoveModuleCommand remove(p, rackNo, slot);
    check(static_cast<bool>(remove.execute()) && p->hardware.totalModules() == modules - 1, "retirer le module");
    (void)remove.undo();
    check(p->hardware.totalModules() == modules, "Ctrl+Z : il revient a sa place");
    check(!project::RemoveModuleCommand(p, 0, 0).execute(), "le processeur ne se retire pas");
    project::ReplaceModuleCommand replace(p, rackNo, slot, "BMXDDO1602");
    const auto r = replace.execute();
    check(static_cast<bool>(r), "remplacer par un BMX DDO 1602" + (r ? "" : " : " + r.error().message()));
    bool replaced = false;
    for (const auto& rk : p->hardware.racks)
        for (const auto& m : rk.modules) replaced = replaced || (rk.number == rackNo && m.slot == slot && m.reference == "BMXDDO1602");
    check(replaced, "le nouveau module est a l'emplacement");
    (void)replace.undo();
    (void)import.undo();
    check(p->hardware.racks.empty(), "Ctrl+Z : plus de racks, comme avant l'import");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "api34_test <MAST.XPG> <CONFIG.XHW>\n");
        return 2;
    }
    const std::string xpg = argv[1], xhw = argv[2];
    testTables(xpg);
    testOrder(xpg);
    testTasks(xpg);
    testIo(xpg, xhw);
    testHardware(xpg, xhw);
    std::printf("%s : %d echec(s)\n", failures ? "ECHEC" : "OK", failures);
    return failures ? 1 : 0;
}
