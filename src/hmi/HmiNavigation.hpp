// =============================================================================
//  hmi/HmiNavigation.hpp - les objets de la navigation et de la structure
//                          (lot 12) : ce qu'ils montrent, ou ils se cliquent,
//                          et comment ils rangent leurs enfants
// -----------------------------------------------------------------------------
//  BARRE DE NAVIGATION   un bouton par vue (la liste "views", sinon les vues
//                        ordinaires du projet), la vue courante en surbrillance ;
//                        Precedent / Suivant en tete si on les veut.
//  FIL D'ARIANE          le chemin jusqu'a la vue courante : sa hierarchie (la
//                        vue parente de chaque vue) ou l'historique ; un clic
//                        sur une etape y retourne.
//  CONTENEUR A ONGLETS   des pages dans la meme zone ; chaque enfant porte sa
//                        page ("tabPage"), le conteneur montre "page".
//  CADRE AVEC TITRE      regroupe : son titre dans la bordure ou en bandeau.
//  PANNEAU DEFILANT      un contenu plus grand que lui : ses enfants defilent
//                        ("scrollX", "scrollY"), rognes a son cadre.
//  PANNEAU REPLIABLE     un bandeau de titre ; replie, son contenu disparait et
//                        les objets du dessous remontent ("pushBelow").
//  PLAN A ZONES          une image et des zones (polygones en % de l'objet) :
//                        la couleur d'une zone suit ses alarmes, un clic la
//                        choisit et ouvre sa vue.
//
//  Le dessin et le clic lisent la meme geometrie. En marche, layoutStructures()
//  range la vue evaluee (enfants caches, decales, remontes) avant le dessin et
//  les clics ; dans l'editeur, View::effectivelyHidden cache les autres pages
//  et le contenu d'un panneau replie.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi {

// ---- la barre de navigation -----------------------------------------------------------------
struct NavItem {
    std::string view;      // le nom de la vue
    std::string label;     // ce qui est ecrit (vide dans "labels" : le nom, _ en espaces)
};
// Les boutons : "views" (a;b;c), sinon les vues ordinaires du projet (ni modeles,
// ni popups, ni symboles), dans l'ordre du projet.
[[nodiscard]] std::vector<NavItem> navItems(const Project* project, const Object& bar);
[[nodiscard]] std::string viewCaption(std::string_view viewName);   // "Vue_Ligne_1" -> "Ligne 1"
struct NavLayout {
    std::vector<Box> items;
    Box              back, forward;     // vides sans "backForward"
    double           fontSize{15};
    bool             vertical{false};
};
[[nodiscard]] NavLayout navBarLayout(const Object&, double w, double h, std::size_t count);
// "vue:2" (0 = la premiere), "precedent", "suivant", ou "".
[[nodiscard]] std::string navBarHit(const Object&, double w, double h, double x, double y, std::size_t count);

// ---- le fil d'Ariane -------------------------------------------------------------------------
struct Crumb {
    std::string view;      // le nom de la vue
    std::string label;
    int         back{-1};  // historique : les pas en arriere pour y revenir ; -1 : naviguer
};
// "trail" : "hierarchie" (les vues parentes, jusqu'a une racine ; "home" : la vue
// de demarrage en tete si elle n'y est pas) ou "historique" (les vues d'avant,
// puis la courante). Au plus "maxItems" etapes : les premieres tombent.
[[nodiscard]] std::vector<Crumb> breadcrumbTrail(const Project&, const Object&, std::string_view current,
                                                 const std::vector<std::string>& history, std::string_view home);
// La largeur approchee d'un texte (le dessin et le clic mesurent pareil).
[[nodiscard]] double approxTextWidth(std::string_view text, double fontSize);
struct CrumbLayout {
    std::vector<Box> crumbs;
    std::vector<Box> separators;    // entre deux etapes
    double           fontSize{15};
};
[[nodiscard]] CrumbLayout breadcrumbLayout(const Object&, double w, double h, const std::vector<Crumb>&);
// "etape:1" (0 = la premiere), ou "".
[[nodiscard]] std::string breadcrumbHit(const Object&, double w, double h, double x, double y, const std::vector<Crumb>&);

// ---- le conteneur a onglets ------------------------------------------------------------------
[[nodiscard]] std::vector<std::string> tabLabels(const Object&);    // au moins un
[[nodiscard]] int tabPageOf(const Object& child);                   // "tabPage", 1 par defaut
[[nodiscard]] int shownTabPage(const Object& tabs);                 // "page", borne a 1..N
struct TabLayout {
    std::vector<Box> tabs;
    Box              content;
    double           fontSize{14};
};
[[nodiscard]] TabLayout tabLayout(const Object&, double w, double h, std::size_t count);
[[nodiscard]] std::string tabHit(const Object&, double w, double h, double x, double y, std::size_t count);   // "onglet:2" (1 = le premier)

// ---- le cadre avec titre -----------------------------------------------------------------------
struct FrameLayout {
    Box    title;       // le texte du titre (bordure : sur la ligne du haut ; bandeau : la bande)
    Box    band;        // le bandeau (vide en bordure)
    Box    content;     // sous le titre
    double top{0};      // la ligne du haut du cadre (bordure : au milieu du titre)
};
[[nodiscard]] FrameLayout frameLayout(const Object&, double w, double h);

// ---- le panneau defilant ---------------------------------------------------------------------
struct ScrollLayout {
    Box    viewport;                     // ce qui se voit, dans le repere de l'objet
    Box    vbar, vthumb, hbar, hthumb;   // vides quand il n'y a pas de quoi defiler
    double contentW{0}, contentH{0};     // la taille du contenu
    double maxX{0}, maxY{0};             // le defilement maximal
    double scrollX{0}, scrollY{0};       // le defilement montre (borne)
};
inline constexpr double kScrollBarW = 12;
// Le contenu : "contentWidth" / "contentHeight" (0 : jusqu'au bord des enfants).
[[nodiscard]] ScrollLayout scrollLayout(const View&, const Object& panel);
// "vbarre:0.42" (la barre verticale a 42 % de sa course), "hbarre:0.3", ou "".
[[nodiscard]] std::string scrollHit(const ScrollLayout&, double x, double y);

// ---- le panneau repliable ----------------------------------------------------------------------
[[nodiscard]] double collapsibleHeaderHeight(const Object&);
[[nodiscard]] std::string collapsibleHit(const Object&, double w, double h, double x, double y);   // "entete" ou ""

// ---- le plan a zones -----------------------------------------------------------------------------
//  Une zone par ligne : Nom | x,y x,y x,y ... (en % de l'objet) | groupe d'alarmes |
//  vue | couleur. Le groupe vide : le nom de la zone ; la vue vide : pas de
//  navigation ; la couleur vide : celle d'une zone calme de l'objet.
struct MapZone {
    std::string                            name;
    std::vector<std::pair<double, double>> points;    // en % (0..100)
    std::string                            group, view, color;
    [[nodiscard]] std::string alarmGroup() const { return group.empty() ? name : group; }
};
[[nodiscard]] std::vector<MapZone> parseMapZones(std::string_view text);
[[nodiscard]] std::string          formatMapZones(const std::vector<MapZone>&);
[[nodiscard]] std::vector<std::pair<double, double>> parseZonePoints(std::string_view text);   // "10,20 40,20 40,60"
[[nodiscard]] std::string formatZonePoints(const std::vector<std::pair<double, double>>&);
[[nodiscard]] bool pointInPolygon(const std::vector<std::pair<double, double>>& poly, double x, double y);
// Le centre (la ou s'ecrit le nom) et le cadre d'une zone, dans le repere de l'objet.
[[nodiscard]] std::pair<double, double> zoneCenter(const MapZone&, double w, double h);
[[nodiscard]] Box zoneBounds(const MapZone&, double w, double h);
[[nodiscard]] std::string zoneMapHit(const Object&, double w, double h, double x, double y);   // "zone:2" (0 = la premiere)

// ---- la mise en page des structures ----------------------------------------------------------------
//  EN MARCHE, sur la vue evaluee : les enfants d'une autre page, ceux d'un panneau
//  replie, passent a "visible" FAUX ; le panneau replie prend la hauteur de son
//  bandeau et, s'il pousse, les objets du dessous (meme parent, qui le
//  chevauchent en largeur) remontent d'autant ; les enfants d'un panneau
//  defilant se decalent de son defilement.
void layoutStructures(View& shown);
// La zone ou un objet qui tient des enfants les montre (dans la vue) : conteneur
// qui rogne, page du conteneur a onglets, contenu d'un panneau, cadre qui rogne.
// Faux : il ne rogne pas.
[[nodiscard]] bool childClip(const View&, const Object& holder, Box& out);
// Le point (dans la vue) est-il hors de la zone d'un des parents de l'objet ?
// (un enfant decale hors du panneau ne se clique pas)
// `except` : un parent qui ne rogne pas (l'editeur, entre dans un panneau defilant).
[[nodiscard]] bool clippedAt(const View&, const Object&, double x, double y, Id except = kNoId);
// L'objet qui tient des enfants sous ce point, au plus profond (l'editeur : ou
// va un objet pose) - un conteneur a onglets, un cadre, un panneau (pas le
// conteneur simple, ou l'on entre d'un double-clic) ; kNoId : aucun.
[[nodiscard]] Id holderAt(const View&, double x, double y, Id exclude = kNoId);
// Ranger un objet (et ses descendants) dans `holder` (kNoId : la vue) : son
// parent, son calque et sa page (conteneur a onglets : celle montree), dessine
// au-dessus de son nouveau parent. Faux : rien a faire.
bool adopt(View&, Id object, Id holder);

} // namespace hmi
