// =============================================================================
//  hmi/HmiExamples.hpp - l'exemple anime de chaque objet de la bibliotheque
// -----------------------------------------------------------------------------
//  L'AIDE D'UN OBJET LE MONTRE EN MARCHE. Chaque exemple est un petit projet
//  IHM (une vue, ses variables, un script cyclique qui les fait vivre, et au
//  besoin des alarmes, une recette, des utilisateurs) que le VRAI moteur fait
//  tourner (hmi::Runtime, sans automate) ; un scenario joue les gestes d'un
//  utilisateur : un clic, une touche du clavier virtuel, un mot de passe tape.
//  Le scenario recommence a la fin de sa periode.
//
//  Rien n'est dessine ici : le volet d'aide (app/hmi) montre la vue avec le
//  meme dessin que la simulation, et un pointeur qui fait les gestes. Sans
//  ecran, hmi_test fait tourner chaque exemple et verifie qu'il ne produit
//  aucune erreur.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <optional>
#include <string>
#include <vector>

namespace hmi {
class Runtime;
}

namespace hmi::examples {

inline constexpr int kWidth = 600, kHeight = 300;   // la vue de chaque exemple

// Un geste du scenario, a l'instant `at` (secondes depuis le debut du tour).
//   "clic"        objet             : appuyer puis relacher sur l'objet
//   "partie"      objet, partie     : une partie d'un objet a parties ("champ",
//                                     "bouton", "bouton:Appliquer", "ligne:1",
//                                     "champ:motdepasse", "suivant")
//   "texte"       texte             : des caracteres tapes (le champ qui a le focus)
//   "clavier"     libelle           : une touche du clavier virtuel ("7", "↵")
//   "deconnecter"                   : l'utilisateur se deconnecte
//   "appui", "relache"  objet       : appuyer (et tenir), relacher (lot 9 : bouton a
//                                     impulsion, appui maintenu)
//   "tirer", "lacher"   objet, 0..1 : la poignee d'un curseur ou d'un potentiometre,
//                                     a cette fraction de sa course ; lachee : ecrite
//   "menu"        partie            : une partie du menu natif Parametres systeme
//                                     (lot 10 : "plus:volume", "onglet:diagnostic", "fermer")
//   "connexion"   partie            : une partie du menu natif de connexion (lot 12 :
//                                     "suivant", "bouton:connexion", "onglet:comptes")
struct Step {
    double      at{0};
    std::string op;
    std::string object;
    std::string arg;
};

struct Example {
    Project           project;     // une vue (de demarrage) et ce qui la fait vivre
    std::vector<Step> steps;
    double            period{10};  // la duree d'un tour, en secondes
};

// L'exemple d'un genre d'objet (nullopt : pas d'exemple, un groupe).
[[nodiscard]] std::optional<Example> exampleFor(Kind kind);

// ---- lot 16 : le tutoriel de chaque objet -----------------------------------------------
//  JOUE PAR LE VRAI MOTEUR : c'est l'exemple de l'objet (sa vue, son scenario), en
//  chapitres. Chaque chapitre part a un instant du tour, montre un objet (une
//  bulle pointee sur lui) et dit ce qui se passe. Le guide peut les ecrire (@tuto
//  dans la source) ; sinon ils se deduisent de l'exemple : la presentation (le
//  resume du guide), un chapitre par geste du scenario (un clic et l'action qu'il
//  declenche, une touche, une poignee tiree...), ou, pour un objet qu'on ne
//  touche pas, ce qu'il montre en marche et ses parametres. Le dernier, "A toi",
//  rend la main : les clics vont au moteur.
struct Chapter {
    double      at{0};            // l'instant de l'exemple (secondes du tour)
    std::string target;           // l'objet que montre la bulle ("" : la vue)
    std::string title;            // le chapitre
    std::string text;             // la bulle
    bool        yourTurn{false};  // "A toi" : l'utilisateur prend la main
};
[[nodiscard]] std::vector<Chapter> tutorialFor(Kind kind, const Example& example);

// Lot 16 : un GIF anime ecrit par le programme - un ventilateur de `size` pixels,
// `frames` images par tour, `delayMs` chacune, en boucle (l'exemple du GIF anime,
// le didacticiel, les essais).
[[nodiscard]] Bytes fanGif(int size = 120, int frames = 24, int delayMs = 40);

// Les gestes dont l'instant tombe dans ]from, to] sont joues sur le moteur (a
// l'heure `now` du moteur). Rend le nombre de gestes joues.
std::size_t play(const Example&, Runtime&, double from, double to, double now);

} // namespace hmi::examples
