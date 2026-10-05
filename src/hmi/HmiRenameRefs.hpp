// =============================================================================
//  hmi/HmiRenameRefs.hpp - renommer un objet d'une vue : ce qui le cite suit
//                          (lot API 8 : renommer partout (IHM))
// -----------------------------------------------------------------------------
//  Un objet se lit partout par son chemin d'instance Vue.Objet.Propriete (voir
//  HmiPublicVars.hpp) : les expressions, les textes a trous, les conditions et
//  les actions de TOUS les objets de TOUTES les vues, les actions et les
//  scripts ST des vues, les scripts generaux et les fonctions, les alarmes
//  (condition, message, consigne), les recettes, les historiques, les
//  utilisateurs, les rapports, les styles, les unites et formats, les essais
//  (expressions ; "cliquer" sur Vue.Objet) et les traductions. Un objet d'un
//  ecran modele (d'un en-tete, d'un pied de page) se lit aussi par les vues qui
//  l'empruntent (Emprunteuse.Objet) - sauf celles qui ont un objet a elles du
//  meme nom. Les actions qui le visent par son nom (GIF : jouer, pause, arret,
//  rejouer ; lier un tableau), dans sa vue et dans celles qui l'empruntent.
//
//  A appeler DANS la commande qui renomme l'objet (un seul Ctrl+Z). Ne renomme
//  pas l'objet lui-meme. Rend le nombre d'endroits qui ont suivi (pour le
//  message) ; `where` (non nul) : ou, un par endroit ("Vue_B / Voyant_1").
// =============================================================================
#pragma once
#include "HmiModel.hpp"
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

std::size_t renameObjectReferences(Project&, const std::string& view, const std::string& from, const std::string& to,
                                   std::vector<std::string>* where = nullptr);

// ---- Lot API 8 : finitions (les chaines comparees, les scripts C / C++) ----
//  Une chaine qui DESIGNE une alarme ou un objet ne se lit pas comme un nom :
//  elle suit quand elle est comparee (=, <>, d'un cote ou de l'autre) a une
//  variable systeme qui en contient un - 'Alarme_1' = SYS.AlarmSelected
//  (AlarmLastName), 'Vue_A.Bouton' = SYS.FocusedObject (PressedObject). Ailleurs
//  (IHM_JOURNAL('Alarme_1 vue')), une chaine reste un texte.
//  renameObjectReferences le fait pour l'objet (et 'Vue.Objet' dans les scripts
//  C / C++ : ni compiles ni executes, une chaine qui nomme tout entier l'objet
//  - ou un chemin qui en part - est la forme d'un acces par nom sur la cible).
//  Suite : le premier argument d'une fonction qui designe l'alarme
//  (IHM_METTRE_DE_COTE('Alarme_1', 60), IHM_REMETTRE) suit aussi ; celui de
//  IHM_GIF_JOUER / PAUSE / ARRETER / REJOUER('Ventilateur') quand l'objet est
//  un GIF anime, dans les scripts ST de sa vue (et de celles qui l'empruntent).
//
// Le texte (ST ou expression) avec chaque chaine egale a `from`, comparee a
// SYS.<une de sysVars> (sans la casse), devenue `to`.
[[nodiscard]] std::string renameComparedStrings(std::string_view code, const std::vector<std::string>& sysVars,
                                                std::string_view from, std::string_view to);
// Un script C / C++ : chaque chaine "..." (son contenu, sans les guillemets)
// passee par `onString` ; les commentaires et les caracteres ('x') tels quels.
[[nodiscard]] std::string rewriteCStrings(std::string_view code, const std::function<std::string(std::string_view)>& onString);
// Renommer une alarme : les chaines comparees a SYS.AlarmSelected /
// SYS.AlarmLastName qui la nomment suivent, partout ou une expression ou un
// script ST se lit (objets, actions, scripts, fonctions, alarmes, recettes,
// utilisateurs, styles, essais...). A appeler DANS la commande qui renomme
// l'alarme. Rend le nombre d'endroits ; `where` : lesquels.
std::size_t renameAlarmReferences(Project&, const std::string& from, const std::string& to, std::vector<std::string>* where = nullptr);
// ---- fin Lot API 8 : finitions ----

} // namespace hmi
