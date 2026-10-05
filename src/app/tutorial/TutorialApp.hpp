#pragma once
// =============================================================================
//  app/tutorial/TutorialApp.hpp - les tutoriels branches dans l'appli (1.11, T1)
// -----------------------------------------------------------------------------
//  Tranche 3 : ce que App fait des tutoriels, sans toucher a App.hpp (lu par
//  des dizaines de fichiers) :
//   - install : inscrit les tutoriels (XPG_TUTORIELS, sinon tools/tutoriels a
//     cote de l'executable ou du dossier courant) et branche le lanceur de
//     help::startTutorial (le centre d'aide, T2, n'appelle que lui) ;
//   - le lanceur : une session = le tutoriel, la scene reelle (UiDriver), le
//     lecteur et le calque. La scene recoit les trois fonctions de l'appli :
//     le bac a sable (une copie NEUVE du projet de @bac, puis son ouverture),
//     les commandes de @avant (vue, poser, regler, sur le document de l'IHM) et
//     la lecture des chemins des conditions ;
//   - frame (avant Update) : la scene joue sa file, l'horloge avance ;
//   - paint (apres le dessin des ecrans, avant les captures) : le calque ;
//   - event : la souris et le clavier de l'utilisateur passent d'abord par le
//     calque (il mange pendant la lecture ; en "A toi", il ne garde que sa barre).
// =============================================================================
#include "../../platform/Geometry.hpp"
#include "../../platform/InputEvent.hpp"
#include <string>

namespace gfx { class IRenderer; }
namespace ui { struct Theme; }
namespace help { class TutorialPlayer; }

namespace app {
class App;
class TutorialOverlay;
class TutorialStageApp;

namespace tutorials {
void install(App& app);
void shutdown();
void frame(double dtMs);
void paint(gfx::IRenderer& r, const ui::Theme& theme, gfx::Size surface, double time);
// Vrai : le calque a garde l'evenement (l'appli ne le passe pas aux ecrans).
bool event(const ui::InputEvent& e);

// Pour les sessions rejouees (ScriptRunner) et le verificateur.
[[nodiscard]] help::TutorialPlayer* player();
[[nodiscard]] TutorialStageApp*     stage();
[[nodiscard]] TutorialOverlay*      overlay();
void stop();

// 1.11.1 (T1, R111-14 / R111-24) : le dossier est-il un bac a sable des tutoriels
// (<tmp>/xpg-bac-a-sable/...) ? L'ecran d'analyse et le bandeau y sautent leurs deux
// comparaisons a la derniere version (ver::compare relit tout le bac : 0,9 a 1,8 s
// chacune, apres chaque remise en place) ; les lignes des versions restent.
[[nodiscard]] bool isSandboxFolder(const std::string& folder);
} // namespace tutorials
} // namespace app
