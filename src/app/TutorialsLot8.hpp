// =============================================================================
//  app/TutorialsLot8.hpp - Lot API 8 : didacticiels et aide
// -----------------------------------------------------------------------------
//  LE CATALOGUE DES NOUVEAUTES DU LOT 8, sans l'ecran : un didacticiel par
//  nouveaute (sa cle "api-...", son titre, sa sorte, son nombre d'etapes, son
//  resume, la page de l'aide qui en parle). Les etapes elles-memes sont dans
//  ApiTrailsLot8.cpp (elles regardent l'ecran) ; l'onglet "API . Didacticiel"
//  montre ces cartes marquees NOUVEAU, et Aide > Nouveautes du lot 8 ne montre
//  qu'elles. Les pages de l'aide (les ancres) sont dans ui/HelpDocument.cpp.
//
//  Sans dependance a l'ecran : le test (tests/tutorials_lot8_test.cpp) verifie
//  les cles, les pages de l'aide et les entrees du menu Aide.
// =============================================================================
#pragma once

#include "../ui/Icons.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace app::lot8 {

struct NewTrail {
    const char* key;        // "api-simuler" : la cle des reglages et du script (parcours api-simuler)
    const char* title;
    const char* kind;       // "visite" ou "interactif"
    int         steps;      // le nombre d'etapes (ApiTrailsLot8.cpp doit en construire autant)
    int         minutes;
    ui::Icon    icon;
    const char* summary;    // la carte et la page des nouveautes
    const char* anchor;     // la page de l'aide generale qui en parle
};

// Les huit didacticiels du lot 8, dans l'ordre des cartes.
[[nodiscard]] const std::vector<NewTrail>& trails();
[[nodiscard]] const NewTrail* find(std::string_view key);

// Les pages de l'aide generale propres au lot 8 (ancres de ui::buildHelp) :
// la page des nouveautes, les raccourcis, puis une par nouvel ecran (F1).
inline constexpr const char* kNewsAnchor = "nouveautes-lot8";
inline constexpr const char* kShortcutsAnchor = "raccourcis";
// L'ancre de l'aide F1 d'un nouvel ecran, par sa cle : "ensemble", "automate",
// "ihm", "equipements", "debogage", "forcages", "courbes", "journal" (les
// onglets du dossier Simulation), "glisser", "themes", "renommer",
// "compiler". Vide : pas de page propre.
[[nodiscard]] std::string helpAnchorFor(std::string_view screen);
// Toutes les ancres que le lot 8 cite (pour le test : chacune existe).
[[nodiscard]] std::vector<std::string> allAnchors();

// Le menu Aide : les actions ajoutees par le lot 8.
inline constexpr const char* kNewsAction = "help.lot8";            // Nouveautes du lot 8
inline constexpr const char* kShortcutsAction = "help.shortcuts";  // Raccourcis clavier

// La page de l'aide generale a ouvrir (une boite aux lettres a une place :
// l'ecran d'aide la vide en entrant et y va). Vide : le sommaire.
void setPendingHelpAnchor(std::string anchor);
[[nodiscard]] std::string takePendingHelpAnchor();

} // namespace app::lot8
