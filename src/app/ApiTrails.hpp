// =============================================================================
//  app/ApiTrails.hpp - lot API 7 : le didacticiel de l'API
// -----------------------------------------------------------------------------
//  LE MEME MOTEUR QUE CELUI DE L'IHM (lot 21 : TutorialWorkspace.cpp et
//  HmiTutorial) : une bulle par etape, une visite qui montre, des parcours
//  interactifs dont chaque etape attend un geste et le verifie, Montre-moi,
//  Reprendre, Garder / Tout defaire par l'historique. Ce fichier ne fait que
//  COMPOSER les etapes de l'API :
//   - api-decouvrir   la visite << Decouvrir l'API >> (14 etapes) ;
//   - api-variable    << Ma premiere variable suivie >> : Compteur_Essais creee,
//                     ecrite dans une section, dans une table d'animation, en
//                     simulation, qui compte, forcee ;
//   - api-ihm-table   << Une variable IHM dans une table >> (Vanne_Purge, PURGE) ;
//   - api-bloc        << Mettre a jour un bloc >> (CAPTEUR 0.02 -> 0.03) ;
//   - api-ordre       << Ranger l'ordre d'execution >> ;
//   - api-macro       << Lancer une macro >> (ImporterClasseur).
//  Les noms viennent du projet ouvert : un projet sans PURGE, sans CAPTEUR en
//  retard ou sans ImporterClasseur prend ce qu'il a de plus proche.
//
//  LA PROGRESSION, PROJET PAR PROJET : "didacticiel.<cle>.<projet>.etape",
//  ".fait", ".date" (<projet> : une empreinte du dossier du projet) - un
//  parcours interactif laisse ses traces dans CE projet, et "Reprendre" n'a de
//  sens que la.
//
//  Les cartes sont dans l'onglet "API . Didacticiel" (la cle "didacticiel" des
//  onglets de l'API) : MainAnalysisScreen::openApiTutorial l'ouvre - le seul
//  point d'entree (Aide > Didacticiel de l'API, la carte du tableau de bord,
//  F1 dans un onglet de l'API ; l'accueil passe par requestApiTutorial).
//
//  LES ONGLETS SE RETROUVENT PAR LEUR PAGE, PAS PAR L'ONGLET COURANT : le
//  centre peut montrer plusieurs groupes d'onglets cote a cote (la mosaique).
//  Une etape regarde si la page est a l'ecran (elle et ses parents montres,
//  dans la fenetre de la bulle) ; un onglet detache dans une autre fenetre
//  compte pour les gestes, pas pour les rectangles a eclairer.
// =============================================================================
#pragma once

#include "hmi/HmiTutorial.hpp"
#include "screens/Screens.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace app {

// Un parcours de l'API, tel que sa carte le montre.
struct ApiTrailDef {
    const char* key;       // "api-variable" : la cle des reglages et des scripts (parcours "api-variable")
    const char* title;
    const char* kind;      // "visite" ou "interactif"
    int         steps;
    int         minutes;
    ui::Icon    icon;
};

struct MainAnalysisScreen::ApiTrails {
    // Ce que le moteur (startTrail) recoit pour lancer un parcours de l'API.
    struct Built {
        std::vector<HmiTutorial::Step> steps;
        // Tout defaire : avant le retour de l'historique (deforcer), et apres
        // (ce que l'historique ne porte pas : un bloc mis a jour par la macro,
        // une simulation qui tourne sur le programme defait).
        std::function<void()> beforeUndo{};
        std::function<void()> afterUndo{};
        // Reste-t-il a defaire HORS de l'historique (un bloc que la macro a mis
        // a jour, une variable forcee) ? Vrai : Tout defaire est propose meme
        // si la pile n'a rien de neuf.
        std::function<bool()> pending{};
        std::function<void()> closed{};     // le parcours se ferme (fini ou passe)
        std::string           refusal{};    // non vide : il ne peut pas partir, et pourquoi
    };
    [[nodiscard]] static const std::vector<ApiTrailDef>& all();
    [[nodiscard]] static const ApiTrailDef* find(std::string_view key);
    // "didacticiel.<cle>.<projet>" : la progression d'un parcours, pour CE projet.
    [[nodiscard]] static std::string prefix(const MainAnalysisScreen& s, std::string_view key);
    // Les etapes d'un parcours ; il est note comme le dernier lance (Reprendre, dans l'onglet).
    [[nodiscard]] static Built build(MainAnalysisScreen& s, const std::string& key);
    // Les cartes de l'onglet ; le rafraichir (une etape passee, un parcours ferme).
    [[nodiscard]] static std::vector<TrailCard> cards(MainAnalysisScreen& s);
    static void refreshPage(MainAnalysisScreen& s);
    // "28/09" : le jour ou un parcours a ete fait.
    [[nodiscard]] static std::string today();
    struct Kit;      // ce que les etapes regardent et font (ApiTrails.cpp)
    // ---- Lot API 8 : didacticiels et aide ----
    //  Les huit parcours du lot 8 (le catalogue : TutorialsLot8.hpp ; les etapes :
    //  ApiTrailsLot8.cpp), marques NOUVEAU dans l'onglet ; Aide > Nouveautes du
    //  lot 8 ouvre l'onglet "API . Nouveautes du lot 8" (openApiTutorial("nouveautes-lot8")).
    struct Lot8 {
        // Les etapes d'un parcours du lot 8 ; `refusal` non vide : il ne peut pas partir.
        [[nodiscard]] static std::vector<HmiTutorial::Step> steps(MainAnalysisScreen& s, const std::string& key, std::string& refusal);
        // F1 sur un onglet du dossier Simulation : sa page de l'aide generale. Faux : pas le cas.
        static bool helpNow(MainAnalysisScreen& s);
        // L'aide generale, ouverte a cette page (lot8::setPendingHelpAnchor).
        static void openHelpAt(MainAnalysisScreen& s, const std::string& anchor);
        // L'onglet des nouveautes : leurs cartes seules (ApiTrails.cpp).
        static void openNews(MainAnalysisScreen& s);
        struct Kit;  // ce que leurs etapes regardent (ApiTrailsLot8.cpp)
    };
    // ---- fin Lot API 8 : didacticiels et aide ----
};

} // namespace app
