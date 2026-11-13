// =============================================================================
//  project/SfcDiagram.hpp - 1.51.0 (S) : le dessin d'un GRAFCET natif (SFC)
// -----------------------------------------------------------------------------
//  Le meme dessin que les grafcets ecrits en ST (grafcet::Diagram), peint par la
//  meme vue (app/GrafcetView) : etapes, transitions et leur receptivite, actions
//  a droite de l'etape (le qualificatif dans sa case), traits simples (OU) et
//  doubles (ET), sauts, liens, textes.
//
//  LA PLACE EST CELLE DE CONTROL EXPERT. Pas de placement automatique : chaque
//  objet est dans sa case (objPosition) ; une colonne est aussi large que ce
//  qu'elle porte (l'etape et ses actions, la transition et sa receptivite), une
//  rangee aussi haute. Un graphe se lit ici comme dans Control Expert.
//
//  Les identifiants : une etape, une transition, un saut, une divergence... est
//  l'indice de son element dans Chart::items ; une action, actionId(etape, rang).
// =============================================================================
#pragma once

#include "GrafcetCheck.hpp"
#include "GrafcetDiagram.hpp"
#include "Sfc.hpp"

#include <map>
#include <utility>
#include <vector>

namespace sfc {

    // La grille dessinee : ou est chaque colonne et chaque rangee (coordonnees du dessin).
    struct Grid {
        int minCol{ 0 };                      // la colonne de colLeft[0] (un coude peut etre a gauche de 0)
        std::vector<float> colLeft, axis, colRight;
        std::vector<float> rowTop, rowH;
        // La case sous un point (une etape glissee : ou elle tombe).
        [[nodiscard]] std::pair<int, int> cellAt(gfx::Point p) const;
    };

    [[nodiscard]] int actionId(int stepItem, int rank) noexcept;
    // (etape, rang) ; (-1, -1) : pas une action.
    [[nodiscard]] std::pair<int, int> actionOf(int id) noexcept;

    // `moved` : des etapes glissees a la main (id -> coin haut gauche), le temps du glisser.
    [[nodiscard]] grafcet::Diagram buildDiagram(const Chart&, const Topology&, const grafcet::MeasureText& measure,
                                                Grid* grid = nullptr, const std::map<int, gfx::Point>* moved = nullptr);

    // Le meme graphe en grafcet::Chart (les infobulles, chercher, l'historique de la vue).
    [[nodiscard]] grafcet::Chart asGrafcet(const Chart&, const Topology&, const std::string& name);

    // Les problemes du graphe, a leur place sur le dessin.
    [[nodiscard]] std::vector<grafcet::Check> checks(const Chart&, const Topology&);

} // namespace sfc
