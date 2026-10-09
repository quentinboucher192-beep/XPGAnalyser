// =============================================================================
//  app/EditionMigration.hpp - 1.12.0 : LES PROJETS DE LA 1.11, SEPARES
// -----------------------------------------------------------------------------
//  Jusqu'a la 1.11, un projet avait les deux : le programme de l'automate et
//  son IHM (ihm/), dans projets\<Nom>. Depuis la 1.12, deux applications, deux
//  rangements : XPGAnalyser API range ses projets dans projets\api, XPGAnalyser
//  IHM dans projets\ihm.
//
//  Au premier lancement de chaque application, ses projets de la 1.11 sont
//  RECOPIES dans son rangement - sa moitie seulement :
//    - API : tout, sauf ihm/ ;
//    - IHM : le manifeste, ihm/, donnees/, versions/ (si l'IHM n'est pas la vue
//      vide que la 1.11 creait pour chaque projet).
//  XPGAnalyser IHM rend chaque IHM recopiee autonome (hmi::standalone) : les noms de
//  l'automate qu'elle lisait deviennent ses variables, depuis le programme d'origine.
//  Le manifeste de la copie dit son application (edition = api | ihm). Les
//  originaux ne bougent pas ; versions/ suit (chaque application n'y voit et
//  n'y restaure que sa moitie : hmi::ver::included).
//
//  Une seule fois : le fichier .depuis-1.11.txt du rangement le dit (et ce qui
//  a ete copie). Sans ecran, sans App : migration_test l'essaie.
// =============================================================================
#pragma once

#include "../core/Edition.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace app::edition {

struct MigrationReport {
    bool ran{false};                                              // faux : deja fait, ou rien a faire
    std::vector<std::pair<std::string, std::string>> copied;      // l'original, la copie (UTF-8)
    std::vector<std::string> skipped;                             // "Nom : raison"
    std::vector<std::string> problems;                            // ce qui n'a pas pu se faire
    std::vector<std::pair<std::string, std::string>> standalone;  // IHM : le projet, ce qui a ete fait (hmi::standalone)
};

// Le nom du marqueur, dans le rangement de l'application.
inline constexpr const char* kMarker = ".depuis-1.11.txt";

// Une IHM de la 1.11 qui vaut d'etre recopiee : plus que la vue de demarrage vide
// (au moins deux vues, une variable, un script, une fonction, une alarme, un
// equipement, ou un objet dans sa vue).
[[nodiscard]] bool hmiWorthKeeping(const std::filesystem::path& projectFolder);

// La recopie, une fois. legacyRoot : projets\ (ses sous-dossiers api et ihm sont
// ignores) ; editionRoot : projets\api ou projets\ihm.
[[nodiscard]] MigrationReport migrateLegacyProjects(const std::filesystem::path& legacyRoot,
                                                    const std::filesystem::path& editionRoot, core::Edition edition);

// Les chemins des projets recents qui designaient un original recopie : la copie.
void remapRecent(std::vector<std::string>& recent, const MigrationReport& report);

} // namespace app::edition
