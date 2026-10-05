// =============================================================================
//  project/GrafcetDiagram.hpp - le dessin d'un grafcet, en coordonnees (1.10, R)
// -----------------------------------------------------------------------------
//  computeLayout (GrafcetLayout.hpp) dit sur quelle rangee et dans quelle
//  colonne va chaque etape. Ceci en fait un DESSIN conforme a l'IEC 60848, en
//  coordonnees du dessin (zoom 1), sans fenetre : la vue n'a plus qu'a peindre.
//
//    etape        un carre numerote (initiale : double carre) ;
//    action       des rectangles accroches a droite de l'etape, empiles, le
//                 qualificatif dans une case a gauche ;
//    transition   un trait horizontal sur la liaison, la receptivite ecrite a
//                 droite (les raccourcis FIN(A2)... rendus lisibles) ;
//    OU / ET      divergence et convergence en OU : un trait simple ; en ET :
//                 deux traits paralleles ;
//    renvoi       quand une liaison remonterait, sauterait des rangees ou
//                 croiserait autre chose : une fleche et le numero de l'etape
//                 visee sous la transition, et au-dessus de l'etape d'arrivee
//                 les etapes d'ou l'on vient.
//
//  Entre deux rangees d'etapes, une BANDE porte les transitions de la rangee du
//  dessus, en zones : les traits de divergence, les transitions et leurs
//  receptivites, les renvois, les arrivees de renvois, les convergences. Dans
//  une bande, les transitions d'une colonne sont des VOIES, de gauche a droite ;
//  la largeur d'une colonne est celle de ses voies et de ses actions.
//
//  RIEN NE SE CHEVAUCHE, ET C'EST VERIFIE PAR LE CALCUL. Chaque liaison directe
//  est essayee, puis controlee contre tout le reste (cases, textes, autres
//  liaisons) ; celle qui croiserait devient un renvoi, et on recommence.
//  `problems()` refait le controle sur le resultat : les essais le demandent
//  vide sur les 17 grafcets du projet de reference.
//
//  Les positions choisies par l'utilisateur (GrafcetLayoutFile) deplacent une
//  etape et ses actions ; ses liaisons suivent en equerre et ne sont pas jugees.
// =============================================================================
#pragma once

#include "../platform/Geometry.hpp"
#include "Grafcet.hpp"
#include "GrafcetLayoutFile.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace grafcet {

    // La largeur d'un texte, en coordonnees du dessin. La vue donne celle de sa
    // police ; les essais une largeur fixe par caractere.
    using MeasureText = std::function<float(std::string_view)>;

    struct DiagramMetrics {
        float stepSize{ 44.f };     // le carre
        float actionH{ 36.f };      // un rectangle d'action : son nom, puis son genre
        float actionGap{ 18.f };    // de l'etape au premier rectangle
        float qualifierW{ 30.f };   // la case du qualificatif
        float textPad{ 6.f };       // marge d'un texte dans sa case
        float barHalf{ 14.f };      // demi-largeur du trait de transition
        float labelPad{ 8.f };      // du trait a sa receptivite
        float idGap{ 6.f };         // du numero "T2" (a gauche) au trait
        float lineH{ 16.f };        // une ligne de texte
        float laneGap{ 22.f };      // entre deux voies d'une bande
        float gapX{ 48.f };         // entre deux colonnes
        float margin{ 24.f };
        float maxLabel{ 300.f };    // une receptivite plus longue est coupee (...)
        float maxAction{ 220.f };   // un nom d'action plus long est coupe
        bool  labelTag{ true };     // le libelle court (c=) au-dessus de l'expression
    };

    struct DiagramStep {
        int         id{ -1 };
        gfx::Rect   box;
        bool        initial{ false }, isFinal{ false }, reachable{ true };
        bool        moved{ false };   // place a la main (le fichier des positions)
        int         level{ 0 }, column{ 0 };
        std::string number;           // "3"
        std::string name;             // le nom du programme ("X10") s'il differe de X<numero>
    };

    struct DiagramAction {
        int         id{ -1 };
        int         step{ -1 };
        gfx::Rect   box;              // le rectangle entier
        gfx::Rect   qualifierBox;     // sa case de gauche
        std::string qualifier;        // "D", "L", "N"...
        std::string text;             // le nom de l'action, coupe si trop long
        std::string kindText;         // son genre en francais : "limit\xC3\xA9" "e \xC3\xA0 10 s"
    };

    struct DiagramTransition {
        int         id{ -1 };
        gfx::Point  at;               // le milieu du trait
        gfx::Rect   bar;              // le trait lui-meme (epaissi)
        gfx::Rect   hit;              // la zone cliquable (trait et texte)
        gfx::Rect   label;            // la receptivite (une ou deux lignes)
        gfx::Rect   idBox;            // "T2", a gauche du trait
        std::string idText;
        std::string tag;              // le libelle court du programme (c=), vide s'il n'apporte rien
        std::string labelText;        // l'expression lisible (raccourcis), coupee si trop longue
        bool        renvoi{ false };
        bool        andJoin{ false }, andSplit{ false };
        int         level{ 0 }, column{ 0 }, lane{ 0 };
        std::vector<int> sources, destinations;
    };

    // Un bout de liaison. `transition` : a qui il appartient (-1 : un trait
    // partage) ; `group` : une divergence ou une convergence partagee
    // ("div:3", "conv:5", "in:0"), vide sinon.
    struct DiagramSegment {
        gfx::Point  a, b;
        int         transition{ -1 };
        int         step{ -1 };          // le trait d'une etape a ses actions
        std::string group;
        bool        arrowDown{ false };   // une fleche au bout b (renvoi, arrivee)
        bool        arrowUp{ false };     // une liaison qui remonte (a la main)
        bool        manual{ false };      // suit une etape placee a la main
    };

    // Un trait de divergence / convergence : simple (OU) ou double (ET).
    struct DiagramJunction {
        float x0{ 0.f }, x1{ 0.f }, y{ 0.f };
        bool  isAnd{ false };
        bool  divergence{ true };
        int   at{ -1 };                   // l'etape (OU) ou la transition (ET)
    };

    // Un renvoi : sous la transition (depart, vers l'etape) ou a l'arrivee.
    struct DiagramRef {
        int         transition{ -1 };     // le depart ; -1 a l'arrivee
        int         step{ -1 };           // l'etape visee (depart) ou d'arrivee
        bool        outgoing{ true };
        gfx::Rect   box;
        std::string text;                 // "X0" ; a l'arrivee : "X5, X7"
        std::vector<int> from;            // a l'arrivee : les transitions qui y menent
    };

    struct Diagram {
        std::vector<DiagramStep>       steps;
        std::vector<DiagramAction>     actions;
        std::vector<DiagramTransition> transitions;
        std::vector<DiagramSegment>    segments;
        std::vector<DiagramJunction>   junctions;
        std::vector<DiagramRef>        refs;
        gfx::Size                      size;
        int                            renvoiCount{ 0 };
        int                            passes{ 0 };   // essais avant un dessin propre

        [[nodiscard]] const DiagramStep* step(int id) const;
        [[nodiscard]] const DiagramTransition* transition(int id) const;
        [[nodiscard]] const DiagramAction* action(int id) const;
    };

    [[nodiscard]] Diagram buildDiagram(const Chart&, const project::ChartLayout* placement,
        const MeasureText& measure, const DiagramMetrics& metrics = {});

    // Ce qui se chevauche ou se croise dans un dessin, en francais : vide quand le
    // dessin est propre. Les positions choisies a la main ne sont pas jugees.
    [[nodiscard]] std::vector<std::string> problems(const Diagram&);

    // La receptivite telle que le dessin l'ecrit : lisible, sur une ligne.
    [[nodiscard]] std::string drawnCondition(const Chart&, const Transition&);

    // Une largeur fixe par caractere (UTF-8 compte une fois), pour les essais et
    // pour un dessin sans police.
    [[nodiscard]] MeasureText fixedWidthMeasure(float perChar = 7.f);

} // namespace grafcet
