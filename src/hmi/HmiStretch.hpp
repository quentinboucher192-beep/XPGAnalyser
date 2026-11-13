// =============================================================================
//  hmi/HmiStretch.hpp - 1.50.0 : ETIRER un objet de la vue (planche 02)
// -----------------------------------------------------------------------------
//  « dans les deplacements des objets du clic droit ajouter Etirer pour etirer
//  vers les bordures de la vue » : un ou des bords de chaque objet choisi vont
//  jusqu'a une cible ; l'autre bord ne bouge pas. Un etirement se fait UNE FOIS
//  (la taille change ; rien ne suit la vue ensuite).
//
//  LES CIBLES (Request::to) :
//    - View : le bord de la vue, moins la marge (Garder les marges : 1.50.1, les
//      marges de la vue - View::margins, une par bord - ; une vue qui n'en a pas,
//      celle de la grille de colonnes quand elle est montree, sinon le pas
//      d'aimantation ; decochee, l'objet touche le bord). Toute la largeur, toute
//      la hauteur, toute la vue : deux ou quatre bords ;
//    - Neighbour : l'objet voisin dans le sens du bord (le plus proche, au meme
//      niveau, qui chevauche l'objet sur l'autre axe) ; il s'arrete avant lui,
//      l'espacement garde (le pas d'aimantation ; sans marge : contre lui) ;
//    - Selection : le bord le plus loin des objets choisis (deux ou plus) -
//      « Aligner a droite » qui agrandit ;
//    - Guide : un repere (View::guides) ; vertical, le bord gauche ou droit qui
//      est de son cote ; horizontal, le haut ou le bas. index < 0 (la fenetre
//      Etirer...) : le repere le plus proche dans le sens de chaque bord ;
//    - Column : une colonne de la grille de colonnes - l'objet va jusqu'a elle,
//      elle comprise.
//
//  LES REGLES : un groupe s'etire en entier (ses objets a l'echelle, setBox) ;
//  un objet tourne, par sa boite a l'ecran (rotatedBounds) ; une ligne, dans
//  son sens seulement (elle s'allonge, son angle garde) ; un objet verrouille ne
//  bouge pas (Plan::skipped le dit ; tous verrouilles : Plan::why). Rien a faire :
//  Plan::why dit pourquoi (l'entree du menu grisee). apply() fait le plan : a
//  envelopper dans UNE commande (un seul Ctrl+Z pour tous les objets).
//  Sans ecran : hmi_editor_test le verifie.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hmi::stretch {

// Les bords qui bougent (un masque).
enum Edge : unsigned { kLeft = 1u, kRight = 2u, kTop = 4u, kBottom = 8u, kWidth = 3u, kHeight = 12u, kAll = 15u };

enum class To : std::uint8_t { View, Neighbour, Selection, Guide, Column };

struct Request {
    unsigned edges{kRight};
    To       to{To::View};
    int      index{-1};           // Guide : le rang dans View::guides (-1 : le plus proche) ; Column : la colonne (0 = la 1re)
    bool     keepMargins{true};   // les marges de la vue (View), l'espacement (Neighbour)
    double   margin{-1.0};        // >= 0 : cette marge, a chaque bord (la fenetre Etirer...) ; < 0 : celles de la vue
    bool operator==(const Request&) const = default;
};

// 1.50.1 : LES MARGES GARDEES, une par bord, et d'ou elles viennent : les marges de la vue (View::margins, dans
// ses proprietes), sinon celle de la grille de colonnes montree (les quatre bords), sinon le pas d'aimantation.
struct Margins {
    enum class From : std::uint8_t { View, Columns, Snap, Given };
    double left{0}, right{0}, top{0}, bottom{0};
    From   from{From::Snap};
    [[nodiscard]] double of(unsigned edge) const noexcept;   // kLeft, kRight, kTop ou kBottom (sinon 0)
    [[nodiscard]] bool   uniform() const noexcept;           // les quatre egales
    bool operator==(const Margins&) const = default;
};

// Un objet (une unite de la selection) et sa boite a l'ecran, avant et apres.
struct Change {
    Id          id{kNoId};
    std::string name;
    Box         before, after;
};

struct Plan {
    std::vector<Change>      changes;
    std::vector<std::string> skipped;   // "Bouton_2 (verrouill\xC3\xA9)"
    std::string              why;       // rien a faire : pourquoi (vide : changes n'est pas vide)
    double                   margin{0}; // la marge (View : la plus grande des bords qui bougent) ou l'espacement (Neighbour) ; 0 : aucun
    Margins                  margins;   // 1.50.1 (View) : la marge de chaque bord (0 : aucune ; Garder les marges decochee : 0)
    std::string              neighbour; // Neighbour, un seul objet : le nom du voisin
    To                       to{To::View};   // la cible (l'apercu dessine la marge du bord de la vue, l'espacement d'un voisin)
    [[nodiscard]] bool possible() const noexcept { return !changes.empty(); }
};

// 1.50.1 : les marges que garde Etirer : celles de la vue (View::margins.any()), sinon celle de la grille de
// colonnes montree, sinon le pas d'aimantation.
[[nodiscard]] Margins viewMargins(const View&);
// Une seule valeur (la fenetre, le menu) : la marge des quatre bords quand elles sont egales, sinon la plus grande.
[[nodiscard]] double viewMargin(const View&);
// "16 px" ; differentes : "haut 16 \xC2\xB7 bas 16 \xC2\xB7 gauche 24 \xC2\xB7 droite 24 px".
[[nodiscard]] std::string marginsText(const Margins&);
// D'ou elles viennent : "les marges de la vue", "la marge de la grille de colonnes", "le pas d'aimantation".
[[nodiscard]] std::string marginsSource(const Margins&);
// L'espacement garde avec un voisin : le pas d'aimantation.
[[nodiscard]] double neighbourGap(const View&);

// Le voisin d'une unite du cote d'un bord (kLeft, kRight, kTop ou kBottom) : l'objet du meme niveau, ni choisi
// (`exclude`) ni cache, le plus proche de ce cote, qui la chevauche sur l'autre axe. kNoId : aucun.
[[nodiscard]] Id neighbour(const View&, Id unit, unsigned edge, const std::vector<Id>& exclude);

[[nodiscard]] Plan plan(const View&, const std::vector<Id>& selection, const Request&);
void apply(View&, const Plan&);

// ---- les mots (le menu, l'apercu, la barre d'etat, la fenetre) ----
// "jusqu'au bord droit de la vue", "sur toute la largeur de la vue", "jusqu'au rep\xC3\xA8re x = 512"...
[[nodiscard]] std::string targetText(const View&, const Request&, const Plan&);
// "+ 396 px \xC2\xB7 largeur 412 \xE2\x86\x92 808" : ce qui change (l'apercu).
[[nodiscard]] std::string deltaText(const Change&);
// "x 200 \xC2\xB7 largeur 412 \xE2\x86\x92 808 \xC2\xB7 hauteur 260 (ne change pas)" : la taille obtenue (la fenetre).
[[nodiscard]] std::string sizeText(const Change&);
// La phrase de la barre d'etat apres l'etirement.
[[nodiscard]] std::string sentence(const View&, const Request&, const Plan&);
// Le nom d'un bord seul ("gauche", "droite", "en haut", "en bas") ; vide : plusieurs bords.
[[nodiscard]] std::string edgeWord(unsigned edge);

} // namespace hmi::stretch
