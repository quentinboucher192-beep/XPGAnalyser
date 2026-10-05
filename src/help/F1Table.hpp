#pragma once
// =============================================================================
//  help/F1Table.hpp - 1.11 (chantier T2, tranche 16) : LA TABLE DE F1
// -----------------------------------------------------------------------------
//  Decision du chef (03/10, 4 h 40) : F1 ouvre le centre d'aide PARTOUT, au
//  sujet de l'endroit. Plus d'onglet "IHM . Aide" (1.10), ni du didacticiel de
//  l'API, en reponse a F1 ; le didacticiel reste une entree du centre.
//
//  Chaque ecran, onglet ou panneau, et le sujet du centre que F1 y ouvre :
//   - un lieu de l'IHM (le volet ou le noeud de l'arbre, dit comme
//     hmi::guide::topicForPlace l'attend) : keyForPlace le traduit. La table
//     passe AVANT le guide, qui prend le premier sujet citant le lieu : "styles"
//     menait a "Choisir une couleur", "compiler" et "fonctions" a "Les erreurs
//     des scripts" (comme "alarmes" menait aux champs a expression, T2-2) ;
//   - un onglet de l'API ou de la simulation : forApiTab, forSimTab ;
//   - un ecran ou une fenetre (le poste, les themes, Renommer, le glisser).
//  L'essai (centreAide111) verifie que chaque ligne mene a un sujet qui existe.
// =============================================================================
#include <string>
#include <string_view>
#include <vector>

namespace help::f1 {

enum class Kind {
    HmiPlace,     // un volet ou un noeud de l'IHM : `id` est le lieu (keyForPlace)
    HmiKey,       // un endroit de l'ecran du projet qui nomme son sujet du guide (l'historique, le grafcet)
    ApiTab,       // un onglet de l'API : `id` est sa cle (apiTabs_)
    SimTab,       // un onglet de la simulation : `id` est sa cle (lot8::helpAnchorFor)
    Screen,       // un ecran ou une fenetre : `id` est son ancre (lot8::helpAnchorFor) ou son lieu
};

struct Place {
    Kind             kind{Kind::HmiPlace};
    std::string_view id;      // ce que l'appli sait de l'endroit
    std::string_view where;   // l'endroit, tel que l'utilisateur le voit (UTF-8)
    std::string_view key;     // la cle du sujet du centre ; vide : le premier sujet du chapitre de `menu`
    std::string_view menu;    // la fabrique qui ouvre le centre : "help", "help.hmi", "help.macros", "help.blocs"
};

// Toute la table, dans l'ordre de l'appli (l'IHM, l'API, la simulation, les ecrans).
[[nodiscard]] const std::vector<Place>& places();

// Un lieu de l'IHM -> la cle du sujet : la table d'abord, sinon le guide
// (hmi::guide::topicForPlace). Vide : aucun sujet ne cite ce lieu.
[[nodiscard]] std::string keyForPlace(std::string_view place);

// La ligne d'un onglet de l'API, d'un onglet de la simulation (nul : pas dans la table).
[[nodiscard]] const Place* forApiTab(std::string_view tab);
[[nodiscard]] const Place* forSimTab(std::string_view tab);

// 1.11.2 (T2, decision 208) : le lieu de chaque onglet d'IHM > Programmation generale, dans
// l'ordre de HmiScriptsPane::Tab : 0 Scripts generaux ("scripts"), 1 Variables IHM ("variables"),
// 2 Types IHM ("types-ihm"), 3 Methodes des symboles ("methodes-symboles"). Un autre rang : "scripts"
// (le volet lui-meme). F1 sur Variables IHM et Types IHM menait au sujet des scripts.
[[nodiscard]] std::string_view placeOfProgrammingTab(std::size_t tab) noexcept;

} // namespace help::f1
