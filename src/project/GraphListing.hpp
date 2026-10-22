// =============================================================================
//  project/GraphListing.hpp - a ladder, an FBD or a GRAFCET, readable as text
// -----------------------------------------------------------------------------
//  1.12.24 : une section LD, FBD ou SFC est gardee telle que Control Expert l'a
//  ecrite (domain::keepsSourceXml) : l'outil la conserve et la reexporte a
//  l'identique, il ne la modifie pas. Ce qui suit la rend LISIBLE :
//
//    - LD  : une ligne par rangee du reseau, les colonnes alignees
//            [X] contact, [/X] inverse, [P X] [N X] fronts,
//            (X) bobine, (/X) inverse, (S X) (R X) mise a 1 / a 0,
//            | liaison verticale (le OU), {BLOC} renvoie au detail sous le reseau ;
//    - FBD : les blocs et leurs broches ;
//    - SFC : les etapes, transitions et sauts du graphe, puis chaque receptivite.
//
//  Chaque ligne dit aussi les noms qu'elle LIT et ceux qu'elle ECRIT (bobine,
//  sortie de bloc, affectation d'un bloc operation) : les references croisees
//  trouvent ainsi une variable dans un ladder, et savent si elle y est ecrite.
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"

#include <string>
#include <vector>

namespace project {

    struct GraphLine {
        std::string              text;
        std::vector<std::string> reads;
        std::vector<std::string> writes;
    };

    // Rien pour une section dont le corps n'est pas un schema garde tel quel.
    [[nodiscard]] std::vector<GraphLine> graphListing(const domain::Section& s);

    // Le texte a afficher : un en-tete qui dit ce que l'on regarde, puis le listing.
    [[nodiscard]] std::string graphListingText(const domain::Section& s);

    // Les lignes de cet en-tete : la ligne i du listing est la ligne
    // graphListingHeaderLines(s) + i + 1 du texte affiche.
    [[nodiscard]] std::size_t graphListingHeaderLines(const domain::Section& s);

} // namespace project
