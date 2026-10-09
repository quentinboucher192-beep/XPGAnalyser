// =============================================================================
//  tests/edition_test.cpp - 1.12.0 : DEUX APPLICATIONS, DEUX RANGEMENTS
// -----------------------------------------------------------------------------
//  core/Edition (les noms), app/EditionMigration (les projets de la 1.11 recopies,
//  chacun sa moitie, une seule fois, les recents qui suivent), ProjectStore dans
//  XPGAnalyser IHM (le programme ni lu ni ecrit : un dossier de la 1.11 garde le
//  sien), les versions (chaque application n'y voit que sa moitie), dupliquer.
// =============================================================================
#include "../src/app/EditionMigration.hpp"
#include "../src/core/Edition.hpp"
#include "../src/hmi/HmiVersions.hpp"
#include "../src/project/ProjectStore.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Un projet de la 1.11 : le programme, et une IHM (vide : la vue de demarrage seule).
void legacyProject(const fs::path& dir, const std::string& name, bool fullHmi) {
    write(dir / "project.xpgproj", "# XpgAnalyzer project. Safe to read; edit with care.\nformatVersion = 1\nname = " + name
                                       + "\nversion = 0.0.1\nstate = DEV\ncpu = BMXP342020\n");
    write(dir / "config" / "hardware.txt", "# PLC configuration.\ncpu = BMXP342020\n");
    write(dir / "vars" / "globals.txt", "Niveau ; REAL ; %MW10 ;  ; \n");
    write(dir / "sections" / "index.txt", "Principal ; MAST ; 1\n");
    write(dir / "sections" / "Principal.st", "Niveau := Niveau + 1.0;\n");
    write(dir / "ihm" / "ihm.txt", fullHmi ? "ihm format=19\nvue id=1 fichier=\"vues/0001-Vue_Accueil.vue\"\nvue id=3 fichier=\"vues/0003-Vue_Cuve.vue\"\nvariable nom=\"Consigne\"\n"
                                           : "ihm format=19\nvue id=1 fichier=\"vues/0001-Vue_Accueil.vue\"\n");
    write(dir / "ihm" / "vues" / "0001-Vue_Accueil.vue", "vue id=1 nom=\"Vue_Accueil\"\ncalque id=2 nom=\"Calque 1\"\n");
    write(dir / "versions" / "index.txt", "# versions\n");
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "xpg-edition-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    // ---- les noms -------------------------------------------------------------
    core::setEdition(core::Edition::Both);
    check(core::hasApi() && core::hasIhm() && core::editionKey().empty() && core::productName() == "XPGAnalyser",
          "Both (les essais) : tout, le nom de toujours");
    core::setEdition(core::Edition::Api);
    check(core::hasApi() && !core::hasIhm() && core::editionKey() == "api" && core::productName() == "XPGAnalyser API",
          "API : l'automate seul, XPGAnalyser API");
    core::setEdition(core::Edition::Ihm);
    check(!core::hasApi() && core::hasIhm() && core::editionKey() == "ihm" && core::productName() == "XPGAnalyser IHM",
          "IHM : l'IHM seule, XPGAnalyser IHM");

    // ---- la recopie des projets de la 1.11 -----------------------------------------
    const fs::path projets = root / "projets";
    legacyProject(projets / "Armoire", "Armoire", true);
    legacyProject(projets / "Pompe", "Pompe", false);                 // son IHM est la vue vide de la 1.11
    write(projets / "notes.txt", "pas un projet\n");
    fs::create_directories(projets / "Brouillon");                    // pas un projet (pas de manifeste)

    const auto api = app::edition::migrateLegacyProjects(projets, projets / "api", core::Edition::Api);
    check(api.ran && api.copied.size() == 2, "API : les deux projets recopies (" + std::to_string(api.copied.size()) + ")");
    check(fs::exists(projets / "api" / "Armoire" / "sections" / "Principal.st") && fs::exists(projets / "api" / "Armoire" / "vars" / "globals.txt"),
          "API : le programme suit");
    check(!fs::exists(projets / "api" / "Armoire" / "ihm"), "API : pas ihm/");
    check(fs::exists(projets / "api" / "Armoire" / "versions" / "index.txt"), "API : versions/ suit");
    check(read(projets / "api" / "Armoire" / "project.xpgproj").find("edition = api\n") != std::string::npos, "API : le manifeste dit edition = api");
    check(fs::exists(projets / "Armoire" / "ihm" / "ihm.txt") && fs::exists(projets / "Armoire" / "sections" / "Principal.st"),
          "les originaux ne bougent pas");
    check(fs::exists(projets / "api" / app::edition::kMarker), "API : le marqueur");

    const auto again = app::edition::migrateLegacyProjects(projets, projets / "api", core::Edition::Api);
    check(!again.ran && again.copied.empty(), "API : une seule fois");

    check(app::edition::hmiWorthKeeping(projets / "Armoire") && !app::edition::hmiWorthKeeping(projets / "Pompe"),
          "une IHM : deux vues et une variable valent la copie ; la vue vide de la 1.11 non");
    const auto ihm = app::edition::migrateLegacyProjects(projets, projets / "ihm", core::Edition::Ihm);
    check(ihm.ran && ihm.copied.size() == 1 && ihm.skipped.size() == 1, "IHM : Armoire recopiee, Pompe laissee (IHM vide)");
    check(fs::exists(projets / "ihm" / "Armoire" / "ihm" / "vues" / "0001-Vue_Accueil.vue"), "IHM : ihm/ suit");
    check(!fs::exists(projets / "ihm" / "Armoire" / "sections") && !fs::exists(projets / "ihm" / "Armoire" / "vars")
              && !fs::exists(projets / "ihm" / "Armoire" / "config"),
          "IHM : pas le programme");
    check(read(projets / "ihm" / "Armoire" / "project.xpgproj").find("edition = ihm\n") != std::string::npos, "IHM : edition = ihm");
    check(!fs::exists(projets / "ihm" / "api") && !fs::exists(projets / "api" / "ihm"), "les rangements ne se recopient pas l'un dans l'autre");

    std::vector<std::string> recent{(projets / "Armoire").string(), (projets / "Pompe").string(), "C:/ailleurs/X.XPG"};
    app::edition::remapRecent(recent, ihm);
    check(recent[0] == (projets / "ihm" / "Armoire").string() && recent[1] == (projets / "Pompe").string() && recent[2] == "C:/ailleurs/X.XPG",
          "les recents suivent les copies (et seulement elles)");

    // ---- ProjectStore dans XPGAnalyser IHM -------------------------------------------
    core::setEdition(core::Edition::Ihm);
    {
        auto opened = project::ProjectStore::open((projets / "Armoire").string());
        check(opened && opened->project && opened->project->variables.empty() && opened->project->sections.empty(),
              "IHM : un dossier de la 1.11 s'ouvre sans son programme");
        check(opened && opened->project->header.projectName == "Armoire" && opened->manifest.edition.empty(),
              "IHM : son nom, son manifeste (sans edition : la 1.11)");
        const std::string before = read(projets / "Armoire" / "vars" / "globals.txt");
        if (opened) {
            auto ok = project::ProjectStore::save(*opened->project, opened->manifest, (projets / "Armoire").string());
            check(static_cast<bool>(ok), "IHM : enregistrer un dossier de la 1.11");
        }
        check(read(projets / "Armoire" / "vars" / "globals.txt") == before && fs::exists(projets / "Armoire" / "sections" / "Principal.st"),
              "IHM : enregistrer n'ecrase pas le programme");
        check(read(projets / "Armoire" / "project.xpgproj").find("cpu = BMXP342020") != std::string::npos,
              "IHM : le manifeste garde l'automate du projet");

        domain::Project empty;
        empty.header.projectName = "Ecran";
        project::Manifest m;
        m.name = "Ecran";
        m.edition = "ihm";
        auto ok = project::ProjectStore::save(empty, m, (root / "neuf" / "Ecran").string());
        check(static_cast<bool>(ok) && fs::exists(root / "neuf" / "Ecran" / "project.xpgproj") && !fs::exists(root / "neuf" / "Ecran" / "config")
                  && !fs::exists(root / "neuf" / "Ecran" / "vars"),
              "IHM : un nouveau projet n'est que son manifeste");
        auto back = project::ProjectStore::readManifest((root / "neuf" / "Ecran").string());
        check(back && back->edition == "ihm" && back->name == "Ecran", "IHM : edition relue");
    }

    // ---- les versions : chaque application, sa moitie --------------------------------
    core::setEdition(core::Edition::Ihm);
    check(hmi::ver::included("ihm/vues/0001-Vue_Accueil.vue") && hmi::ver::included("project.xpgproj") && hmi::ver::included("donnees/x.csv")
              && !hmi::ver::included("sections/Principal.st") && !hmi::ver::included("vars/globals.txt"),
          "versions IHM : ihm/, le manifeste, donnees/ - pas le programme");
    core::setEdition(core::Edition::Api);
    check(!hmi::ver::included("ihm/vues/0001-Vue_Accueil.vue") && hmi::ver::included("sections/Principal.st") && hmi::ver::included("project.xpgproj"),
          "versions API : le programme - pas ihm/");
    core::setEdition(core::Edition::Both);
    check(hmi::ver::included("ihm/vues/0001-Vue_Accueil.vue") && hmi::ver::included("sections/Principal.st"), "versions (Both) : tout, comme avant");

    // ---- dupliquer : sa moitie ------------------------------------------------------
    core::setEdition(core::Edition::Ihm);
    {
        auto ok = project::ProjectStore::duplicate((projets / "Armoire").string(), (root / "copies" / "IHM").string(), "Copie");
        check(static_cast<bool>(ok) && fs::exists(root / "copies" / "IHM" / "ihm" / "ihm.txt") && !fs::exists(root / "copies" / "IHM" / "sections"),
              "IHM : dupliquer copie ihm/, pas le programme");
        auto m = project::ProjectStore::readManifest((root / "copies" / "IHM").string());
        check(m && m->edition == "ihm" && m->name == "Copie", "IHM : la copie est un projet IHM");
    }
    core::setEdition(core::Edition::Api);
    {
        auto ok = project::ProjectStore::duplicate((projets / "Armoire").string(), (root / "copies" / "API").string(), "Copie");
        check(static_cast<bool>(ok) && fs::exists(root / "copies" / "API" / "sections" / "Principal.st") && !fs::exists(root / "copies" / "API" / "ihm"),
              "API : dupliquer copie le programme, pas ihm/");
    }

    core::setEdition(core::Edition::Both);
    fs::remove_all(root, ec);
    std::printf("\n%s (%d echec%s)\n", failures == 0 ? "OK" : "ECHEC", failures, failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
