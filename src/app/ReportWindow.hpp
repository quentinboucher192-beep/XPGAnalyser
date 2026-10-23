// =============================================================================
//  app/ReportWindow.hpp - 1.12.28 : la fenetre de rapport a part
// -----------------------------------------------------------------------------
//  L'exe relance par le chien de garde (un blocage) ou par le rapporteur (un
//  plantage), voir core::crash::launchReportWindow :
//    --fenetre-rapport blocage|plantage --rapport <chemin> --pid <n> --secondes <s>
//  Une petite fenetre, dessinee avec les widgets et le theme de l'appli
//  (ui::CrashReportScreen) : le processus bloque ne dessine plus, celui-ci si.
//  Pendant un blocage elle regarde l'appli : la boucle repart (l'evenement
//  core::crash::recoveryEventName leve) ou le processus se ferme - elle le dit
//  et se ferme. « Fermer IHM Studio » arrete le processus bloque ; apres un
//  plantage, « Relancer IHM Studio ».
//  Ni instance unique, ni chien de garde, ni rapport de plantage ici.
//  --capture <png> [--capture-apres <s>] : l'image de la fenetre, puis la fin
//  (les captures des livraisons).
// =============================================================================
#pragma once

#include <string>

namespace app::reportwindow {

struct Args {
    std::string   kind;              // « blocage », « plantage »
    std::string   report;            // le chemin du rapport (UTF-8)
    unsigned long pid{0};            // le processus bloque (blocage)
    double        seconds{0};        // l'arret deja compte (blocage)
    std::string   capture;           // une image de la fenetre, puis la fin
    double        captureAfter{1.5}; // apres tant de secondes
};

// Vrai si la ligne de commande demande la fenetre de rapport (--fenetre-rapport) ; `out` la recoit.
[[nodiscard]] bool parse(int argc, char** argv, Args& out);
// La fenetre, jusqu'a ce qu'on la ferme. Rend le code de sortie.
int run(const Args& args);

} // namespace app::reportwindow
