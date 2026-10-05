// =============================================================================
//  app/ScriptRunner.hpp - rejouer une session a la souris et au clavier
// -----------------------------------------------------------------------------
//      xpg_analyzer --script session.txt [--captures dossier] [--size 1600x1000]
//
//  Un fichier texte, une commande par ligne, joue dans la VRAIE application :
//  les clics et les touches passent par MenuManager::HandleEvent, comme ceux
//  que SDL traduit, et "capture" ecrit l'image que le rendu vient de dessiner
//  (IRenderer::readPixels). C'est ainsi que sont faites les captures des
//  livraisons ; la meme session se rejoue apres une modification.
//
//  Les commandes (les mots entre guillemets peuvent contenir des espaces) :
//
//    # commentaire
//    attendre N                   laisser passer N images
//    attendre-projet              jusqu'a ce qu'un projet soit ouvert
//    theme Dark | Light | "High contrast"
//    importer <fichier>           ouvrir un export (.XPG), comme Fichier > Ouvrir
//    ouvrir <dossier>             ouvrir un dossier de projet
//    action <id>                  une action de l'application (project.save...)
//    clic X Y [droit] [double] [ctrl] [maj] [alt]
//    glisser X1 Y1 X2 Y2 [ctrl] [maj] [alt]
//    survol X Y
//    molette X Y DY [ctrl]
//    touche Ctrl+Z | Return | Escape | Maj+Tab ...
//    texte "..."
//    bouton "Libelle"             un bouton de l'ecran du dessus (dialogue compris)
//    champ N "texte"              le N-ieme champ de saisie de l'ecran du dessus
//                                 (lot 12 : ou champ "hmi.find.find" "texte", par identifiant)
//    explorateur "chemin"         la reponse de la PROCHAINE ouverture de l'explorateur de
//                                 fichiers (il ne s'ouvre pas) ; puis :
//    parcourir "Libelle" | N      le bouton ... d'un champ de chemin de l'ecran du dessus,
//                                 par le libelle du champ (ou son debut), ou le N-ieme
//    onglet "Titre"               un onglet de l'espace de travail
//    fermer-onglet "Titre"        sa croix
//    arbre "A/B/C" [double|deplier|replier]  une ligne de l'explorateur du projet ; "A/B/C" :
//                                 C cherche apres B, cherche apres A
//    arbre-menu "A/B/C"           lot 7 : un vrai clic droit sur ce noeud - son menu s'ouvre ;
//                                 choisir "Entree" y clique (dans le sous-menu ouvert d'abord)
//    case "Libelle"               une case a cocher de l'ecran du dessus
//    liste N                      ouvrir la N-ieme liste deroulante de l'ecran du dessus
//                                 (lot 12 : ou liste "hmi.find.scope", par identifiant)
//    choisir "Libelle"            un element de la liste deroulee ouverte
//    deposer <fichier> [X Y]      un fichier glisse depuis l'explorateur et lache
//    code N <login>               le code dynamique du moment de <login>, tape dans
//                                 le N-ieme champ (connexion en simulation)
//    ligne "texte" [double]       la ligne d'un tableau de l'onglet courant
//    etat                         ecrire la pile d'ecrans (mise au point)
//    --- dans l'onglet IHM courant ---
//    outil "Infobulle"            un bouton de barre d'outils IHM (debut de l'infobulle)
//    palette "Rectangle" [etoile] une tuile de la bibliotheque de composants
//                                 (etoile : l'ajouter aux favoris / l'en retirer)
//    vue-clic X Y [double] [ctrl] [maj] [alt]     en coordonnees de la VUE
//    vue-glisser X1 Y1 X2 Y2 [ctrl] [maj] [alt] [tenir]
//                                 tenir (lot 12) : sans relacher - une capture
//                                 pendant le glisser (reperes, ecarts egaux)
//    vue-lacher                   relacher la souris la ou elle est
//    vue-zoom P [X Y]             le zoom de l'editeur, en % ; le point (X, Y) de la
//                                 vue en haut a gauche de la zone de dessin
//    objet "Nom" [double|oeil|verrou]             une ligne de l'explorateur d'objets
//    calque "Nom" [double|oeil|verrou]
//    propriete "Nom" "valeur"     taper dans une case du panneau Proprietes
//    propriete-clic "Nom"         cliquer une case (bascule, liste)
//    sous-onglet "Titre"          un onglet dans l'onglet courant (Actions, Journal...)
//    editeur "texte" [ajout]      l'editeur de code de l'onglet (\n, \t)
//    sim-clic "Objet" [double]    un objet de la vue simulee ; sim-appui / sim-relache
//    --- lot API 7 : API > Simulation et API > Statistiques (l'onglet s'ouvre s'il
//        ne l'est pas ; il peut etre derriere un autre, dans un autre groupe, detache) ---
//    simulation-deplier "Armoires[0].ana"    deplier une variable (ses parents et son
//                                 dossier avec), ou un dossier par son libelle
//    simulation-forcer "Armoires[0].ana.PT1.mes" "7.25"   la choisir et la forcer
//                                 ("" : la relacher)
//    simulation-suivre "Armoires[0].ana.PT1.mes"   la choisir et la tracer sur la courbe
//    simulation-vitesse 1|10|0    x1, x10, au plus vite
//    simulation-politique continuer|arreter   une fonction inconnue : rendre 0 et
//                                 continuer, ou arreter le cycle
//    statistiques-vue entree|section|langage  les barres du programme, reparties par ...
//    attendre-cycles N            jusqu'a N cycles de plus ; la simulation doit tourner
//                                 (Simuler) - sinon, ou apres 120 s, l'echec est compte
//    ---
//    --- 1.11 (T1) : les gestes des tutoriels, pour les sessions de captures ---
//    encadrer <cible>             la cible encadree en orange sur les images suivantes
//                                 (cible de tutoriel : bouton:Creer, outil:Compiler,
//                                 propriete:Nom, biblio:Vanne, vue:320,190, ecran:640,400) ;
//                                 introuvable : compte comme un echec. Sans cible : l'efface.
//    dire "texte"                 la bulle du tutoriel, en bas de l'image ("" : l'efface)
//    capture fichier.png          l'image courante (le dossier --captures, sinon
//                                 celui du script)
//    quitter
//
//  ${NOM} est remplace par la variable d'environnement NOM : le meme script se
//  rejoue ailleurs (set XPG=D:\exports\MAST.XPG).
//
//  L'HORLOGE EST FIXE pendant une session rejouee : 1/30 s par image (App::run),
//  quelle que soit la machine. "attendre 30" = une seconde de l'application ;
//  fondus, clignotements, messages temporaires et cycles du simulateur tombent
//  au meme endroit a chaque rejeu, et les captures sont reproductibles.
//
//  Une commande qui ne trouve pas sa cible le dit sur la sortie d'erreur et le
//  compte : le programme rend alors 3 au lieu de 0.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "../platform/Geometry.hpp"
#include "../platform/InputEvent.hpp"
#include "tutorial/UiDriver.hpp"            // 1.11 (T1) : l'entree simulee et les cibles, partagees avec les tutoriels

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gfx { class IRenderer; }
namespace ui { class Widget; }

namespace app {

class App;
class HmiEditor;

class ScriptRunner {
public:
    static core::Result<std::unique_ptr<ScriptRunner>> fromFile(App& app, const std::string& path,
                                                                 std::string capturesDir = {});

    // Apres le dessin d'une image et avant sa presentation : une capture prend
    // exactement ce qui vient d'etre dessine. Rend false : quitter.
    bool afterRender(gfx::IRenderer& renderer);
    [[nodiscard]] int failures() const noexcept { return failures_; }

private:
    ScriptRunner(App& app, std::vector<std::string> lines, std::filesystem::path capturesDir);

    enum class Step { Next, Yield, Retry, Quit };
    Step run(const std::vector<std::string>& words, gfx::IRenderer& renderer);
    void fail(const std::string& why);

    void send(const ui::InputEvent& e);
    void moveTo(gfx::Point p, ui::KeyMods m);
    void click(gfx::Point p, ui::MouseButton b, int clicks, ui::KeyMods m);
    // 1.11.1 (T1, R111-18) : un clic par la voie du client (App::deliverClientEvent), pour clic-souris.
    void clientClick(gfx::Point p, ui::MouseButton b, int clicks, ui::KeyMods m);
    void drag(gfx::Point a, gfx::Point b, ui::KeyMods m, bool release = true);
    void typeInto(gfx::Point cell, const std::string& text);

    [[nodiscard]] ui::Widget* top() const;
    [[nodiscard]] ui::Widget* currentPage() const;
    [[nodiscard]] HmiEditor*  currentEditor() const;

    App&                     app_;
    std::vector<std::string> lines_;
    std::filesystem::path    capturesDir_;
    std::size_t              pc_{0};
    int                      wait_{0};
    int                      retries_{0};
    // Lot 14 : attendre en temps reel (la liaison Modbus vit a l'horloge du
    // poste, pas a celle, fixe, de la session) : l'echeance en cours.
    double                   realUntil_{-1};
    double                   waitStarted_{0};     // lot 15 : le debut d'une attente
    std::string              screenCapture_;      // lot 14 : capture-ecran en cours
    int                      sinceInput_{0};
    int                      failures_{0};
    UiDriver                 driver_;             // 1.11 (T1) : l'entree simulee (et la souris) passe par lui
    gfx::Point               clientMouse_{};      // 1.11.1 (T1, R111-18) : la souris de clic-souris
    std::optional<gfx::Rect> spot_;               // 1.11 (T1) : encadrer
    std::string              say_;                // 1.11 (T1) : dire
};

} // namespace app
