// =============================================================================
//  app/hmi/HmiChoiceItems.hpp - 1.26.1 : les listes longues de l'inspecteur
// -----------------------------------------------------------------------------
//  Le client (06/11), devant les listes Police et Style nomme : << faire quelque
//  chose de mieux si y'en a bcp on pourrait vite s'y perdre >>.
//
//  Les choix de ces cases deviennent des ui::DropDown::Item riches :
//   * POLICE : des sections (Theme, Du projet, Mes polices, Bibliotheque > Sans
//     empattement...), UNE LIGNE PAR FAMILLE ecrite dans sa police, ses styles en
//     pastilles (Normal, Gras, Italique), ce qui l'emploie en note (3 objets) ;
//   * STYLE NOMME : (aucun), puis Du projet (par dossier) et Fournis, chacun avec
//     son apercu (son remplissage, son contour, << Aa >> dans sa couleur de texte)
//     et ses objets ;
//  et la recherche en tete de la liste (ui::DropDown). La valeur ecrite ne change
//  pas : l'etiquette de la case Police (hmi::fontlib::fieldChoices), le nom du style.
// =============================================================================
#pragma once

#include "../../hmi/HmiModel.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <vector>

namespace app {

// Les choix de la case Police (objets, styles ; theme : les deux cases de Configuration > Affichage).
[[nodiscard]] std::vector<ui::DropDown::Item> hmiFontChoiceItems(const hmi::Project&, bool theme = false);
// Les choix de la case Style nomme ; `forObject` : l'apercu des valeurs que le style donne a cet objet (nul : celles
// pour tous les objets, sinon celles de son premier type).
[[nodiscard]] std::vector<ui::DropDown::Item> hmiStyleChoiceItems(const hmi::Project&, const hmi::Object* forObject);

} // namespace app
