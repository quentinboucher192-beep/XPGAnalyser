// =============================================================================
//  hmi/HmiEdit.hpp — les operations de l'editeur de vues, sans ecran
// -----------------------------------------------------------------------------
//  Ajouter, dupliquer, supprimer, deplacer, redimensionner, tourner, retourner,
//  aligner, distribuer, grouper, ordonner, calques, style, magnetisme, choix a
//  la souris : tout ce que fait l'editeur est ici, en fonctions pures sur une
//  vue. L'ecran ne fait que les appeler dans une commande (HmiCommands) ; les
//  tests les appellent directement. Aucune ne depend d'un widget.
//
//  UNE SELECTION = DES "UNITES". Un objet choisi avec un groupe parent qui
//  l'est aussi est ignore : on deplace le groupe, pas deux fois l'enfant.
//  Un objet verrouille (lui, son calque ou un groupe parent) ne bouge pas.
//
//  ROTATION : en degres, sens horaire a l'ecran, autour du pivot de l'objet
//  (le centre par defaut, proprietes pivotX / pivotY en fraction de la
//  taille). Le cadre (x, y, w, h) est celui de l'objet NON tourne : tourner
//  ne le deplace pas, et le pivot reste ou il etait - ce que demande la
//  specification ("conservation du pivot").
//
//  MIROIR : une vraie symetrie a l'ecran autour de l'axe de la selection. Pour
//  un objet seul, l'axe passe par son centre : il bascule sur place.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <vector>

namespace hmi::edit {

struct Pt { double x{0}, y{0}; };

// ------------------------------------------------------------ geometrie ----
[[nodiscard]] Pt   toView(const Object&, Pt local);    // repere de l'objet -> vue
[[nodiscard]] Pt   toLocal(const Object&, Pt inView);  // vue -> repere de l'objet
[[nodiscard]] Box  rotatedBounds(const Object&);      // cadre englobant a l'ecran
[[nodiscard]] std::vector<Pt> corners(const Object&);  // les 4 coins, a l'ecran
[[nodiscard]] std::vector<Pt> points(const Object&);   // ligne / polygone, repere local
void                          setPoints(Object&, const std::vector<Pt>&);
[[nodiscard]] bool hit(const Object&, double x, double y, double tolerance = 4.0);

// ------------------------------------------------------------ selection ----
[[nodiscard]] std::vector<Id> units(const View&, const std::vector<Id>& ids);
[[nodiscard]] Id              topLevelOf(const View&, Id);
[[nodiscard]] Box             selectionBounds(const View&, const std::vector<Id>& ids);
// L'objet sous le point, du dessus vers le dessous. `insideGroup` : le groupe
// en cours d'edition interne (kNoId sinon) ; on rend alors ses enfants
// directs, sinon les groupes de premier niveau. Les objets caches et
// verrouilles ne se prennent pas a la souris (l'explorateur, lui, les prend).
[[nodiscard]] Id hitTest(const View&, double x, double y, Id insideGroup = kNoId, double tolerance = 4.0);
[[nodiscard]] std::vector<Id> inRect(const View&, const Box&, Id insideGroup = kNoId);

// ------------------------------------------------------------ creation -----
Id add(Project&, View&, Kind, double x, double y);
Id addObject(View&, Object);   // en haut de son calque
[[nodiscard]] std::vector<Id> duplicate(Project&, View&, const std::vector<Id>& ids, double dx, double dy);
void remove(View&, const std::vector<Id>& ids);   // avec les enfants ; un groupe vide disparait
[[nodiscard]] bool rename(View&, Id, const std::string& name, std::string* error = nullptr);
[[nodiscard]] bool validName(const std::string&) noexcept;

// ------------------------------------------------------------ geometrie ----
void move(View&, const std::vector<Id>& ids, double dx, double dy);
void setBox(View&, Id, const Box&);                // redimensionner (groupe : les enfants suivent)
// Redimensionner une selection de plusieurs unites : chaque centre et chaque
// taille suivent le passage du cadre `from` au cadre `to`.
void scaleSelection(View&, const std::vector<Id>& ids, const Box& from, const Box& to);
void rotate(View&, const std::vector<Id>& ids, double delta, bool aroundSelectionCenter = false);
void setRotation(View&, const std::vector<Id>& ids, double angle);
void mirror(View&, const std::vector<Id>& ids, bool horizontal);
[[nodiscard]] double normalizeAngle(double) noexcept;   // [0, 360)

enum class Align : std::uint8_t { Left, Right, Top, Bottom, CenterH, CenterV };
void align(View&, const std::vector<Id>& ids, Align);
void distribute(View&, const std::vector<Id>& ids, bool horizontal);   // ecarts egaux (3 unites ou plus)
void space(View&, const std::vector<Id>& ids, bool horizontal, double gap);

enum class ZMove : std::uint8_t { Front, Back, Forward, Backward };
void zorder(View&, const std::vector<Id>& ids, ZMove);

// ------------------------------------------------------------ groupes ------
[[nodiscard]] Id              group(Project&, View&, const std::vector<Id>& ids);
[[nodiscard]] std::vector<Id> ungroup(View&, Id group);
void refreshGroupBounds(View&);

void setLocked(View&, const std::vector<Id>& ids, bool);
void setHidden(View&, const std::vector<Id>& ids, bool);

// ------------------------------------------------------------ calques ------
Id   addLayer(Project&, View&, std::string name = {});
[[nodiscard]] bool removeLayer(View&, Id layer, std::string* error = nullptr);
[[nodiscard]] bool renameLayer(View&, Id layer, const std::string& name, std::string* error = nullptr);
void moveLayer(View&, Id layer, int delta);        // +1 : monte d'un rang
void setLayerVisible(View&, Id layer, bool);
void setLayerLocked(View&, Id layer, bool);
void moveToLayer(View&, const std::vector<Id>& ids, Id layer);
[[nodiscard]] std::vector<Id> objectsInLayer(const View&, Id layer);

// ------------------------------------------------------------ style --------
[[nodiscard]] std::vector<Prop> copyStyle(const Object&);
void pasteStyle(View&, const std::vector<Id>& ids, const std::vector<Prop>& style);

// ------------------------------------------------------------ magnetisme ---
// Lot 12 : un ecart egal trouve - a dessiner comme une cote (une fleche et sa
// valeur) : horizontal, entre `from` et `to` (x) a la hauteur `at` (y) ;
// vertical, entre `from` et `to` (y) a l'abscisse `at` (x).
struct SpacingMark {
    bool   horizontal{true};
    double from{0}, to{0}, at{0};
    double gap{0};
};
struct SnapResult {
    double dx{0}, dy{0};
    std::vector<double> xLines, yLines;   // les reperes a dessiner (coordonnees vue)
    std::vector<SpacingMark> spacing;     // lot 12 : les ecarts egaux (le nouveau et son modele)
};
// Deplacer `moving` de (dx, dy) en accrochant ses bords et son centre aux
// guides, aux autres objets et a la vue (dans cet ordre), sinon a la grille.
// Lot 12 : ou en le posant a un ECART EGAL - celui qui separe deja deux
// voisins de sa rangee (ou de sa colonne), ou au milieu de ses deux voisins ;
// le plus proche des deux accroches gagne.
[[nodiscard]] SnapResult snapMove(const View&, const Box& moving, double dx, double dy,
                                  const std::vector<Id>& exclude, double threshold);
[[nodiscard]] double snapToGrid(double v, int step) noexcept;

} // namespace hmi::edit
