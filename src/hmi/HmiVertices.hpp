// =============================================================================
//  hmi/HmiVertices.hpp - 1.12.12 (lot H de l'audit des objets, § 3.1, 3.4, 3.9) :
//                        LES SOMMETS SUR LE DESSIN
// -----------------------------------------------------------------------------
//  Une ligne, un polygone : leurs points "x,y x,y ..." dans le repere de
//  l'objet (son coin haut-gauche, avant rotation et miroir). Avant, ils ne se
//  changeaient qu'en tapant ce texte ; "10, 20" (une espace apres la virgule)
//  etait ignore sans un mot, et Generer ne comptait que les points lus.
//
//  ICI :
//   - LE LECTEUR UNIQUE du texte des points : le dessin, le choix a la souris,
//     le redimensionnement, Generer et Contenu lisent pareil. Ce qu'il ne lit
//     pas (un nombre seul, un morceau illisible, trois nombres) est dit a sa
//     place (« point 3 ») ;
//   - LES GESTES de l'editeur sur un sommet : le deplacer, en inserer un sur un
//     segment, le retirer, reecrire la liste (Contenu). Chaque geste laisse le
//     dessin a sa place a l'ecran et refait le cadre de l'objet : le plus petit
//     qui tient ses points, les points ramenes a son coin (le cadre suit les
//     points) ;
//   - LES CIBLES du dessin : une ligne ou un polygone (repere de l'objet), une
//     zone d'un plan a zones (en % du plan), la courbe de reference d'une courbe
//     XY (dans les unites de ses axes) - les memes poignees pour toutes.
//
//  Une ligne garde au moins 2 sommets, un polygone et une zone 3. Un nombre
//  ecrit avec un repere $...$ garde son texte tant qu'un geste ne le change pas.
// =============================================================================
#pragma once

#include "HmiEdit.hpp"
#include "HmiSources.hpp"   // « Points depuis » : les sources du lot E

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::vertices {

// ---- le texte des points ----------------------------------------------------------------
//  Un MORCEAU du texte (entre deux blancs) : un point lu (`ok`), ou un morceau illisible, garde tel
//  qu'ecrit (`raw`) - Contenu le montre et le rend, le dessin l'ignore (comme avant).
struct Vertex {
    double      x{0}, y{0};
    std::string xText, yText;   // tels qu'ecrits (un repere garde) ; vides : le nombre
    bool        ok{true};
    std::string raw{};          // illisible : le morceau entier
};
// Un morceau du texte qui ne donne pas un point net : son rang (1 = le premier), ce qui est
// ecrit, et pourquoi. `kept` : il donne quand meme un point (10,5,20 : lu 10 ; 5,2, comme avant).
struct Problem {
    std::size_t position{0};
    std::string written;
    std::string why;
    bool        kept{false};
};
struct Parsed {
    std::vector<Vertex>  points;     // les points lus (le dessin)
    std::vector<Problem> problems;
    std::vector<Vertex>  entries;    // tous les morceaux, dans l'ordre (Contenu : une ligne chacun)
};
// "10,20 30,40" ; 1.12.12 : "10, 20" et "10 ,20" se lisent aussi. Un nombre seul, un morceau
// illisible : ignore (comme avant), et dit. "10,5,20" (trois nombres : une virgule decimale ?)
// se lit comme avant (x 10, y 5,2), et se dit. Les reperes $...$ sont transparents.
[[nodiscard]] Parsed parse(std::string_view text);
// Le texte d'une liste de sommets : "x,y x,y" (chaque nombre tel qu'ecrit, sinon formatNumber ; un
// morceau illisible tel qu'il etait).
[[nodiscard]] std::string format(const std::vector<Vertex>&);
// Le rang d'un sommet du dessin (parmi les points lus) dans les morceaux, et l'inverse ; -1 : aucun.
[[nodiscard]] int entryOfPoint(const Parsed&, int point);
[[nodiscard]] int pointOfEntry(const Parsed&, int entry);
// Une case X ou Y d'un morceau reecrite (le texte deja accepte par cellText) : relu, il peut devenir un
// point (ou cesser de l'etre).
void setCell(Vertex&, bool xField, const std::string& text);
// Ce que Generer dit d'un morceau : « point 3 (10,5,20) : ... ».
[[nodiscard]] std::string describe(const Problem&);
// Ce qu'une case de Contenu ecrit pour un nombre tape : un nombre (12,5 a la francaise : 12.5), ou
// un texte qui porte un repere ($Cote_X$ : Dupliquer le remplacera) ; nullopt : refuse (un blanc, une
// virgule hors d'un nombre couperaient la liste).
[[nodiscard]] std::optional<std::string> cellText(std::string_view typed);

// ---- une ligne, un polygone ------------------------------------------------------------------
[[nodiscard]] bool isShape(const Object&) noexcept;                // une ligne ou un polygone
[[nodiscard]] std::size_t minimum(const Object&) noexcept;         // 2 (ligne), 3 (polygone)
// Ses points se changent-ils sur le dessin ? Ecrits (pas une formule, pas « Points depuis »).
[[nodiscard]] bool editable(const Object&);
// Les morceaux reecrits (Parsed::entries) ; le cadre suit les points lus (le dessin ne bouge pas a l'ecran).
void setVertices(Object&, std::vector<Vertex>);

// ---- la courbe de reference d'une courbe XY (§ 3.4) -----------------------------------------
//  Ses points (x,y dans les unites des axes) se reglent sur le dessin, dans la zone de trace, quand
//  l'echelle est fixe (une echelle auto bougerait avec eux) ; Contenu les montre en tableau. Reecrite,
//  elle est rangee par x croissant, comme le dessin la lit ; un point glisse ne passe pas ses voisins.
// Ecrite (ni formule, ni source Ref[*].X, Ref[*].Y) et lue en entier par le dessin (rien d'illisible).
[[nodiscard]] bool referenceEditable(const Object&);
void setReference(Object&, std::vector<Vertex>);
// La zone de trace d'une courbe XY (repere de l'objet) et ses axes, comme la dessine l'editeur ; faux :
// echelle auto, ou pas une courbe XY.
struct XYPlot {
    Box    area;
    double x0{0}, x1{1}, y0{0}, y1{1};
};
[[nodiscard]] bool xyPlot(const Object&, XYPlot&);

// ---- Contenu : la liste d'un objet -----------------------------------------------------------------
//  Les sommets d'une ligne, d'un polygone ("points", dans l'ordre ecrit) ; la courbe de reference d'une
//  courbe XY ("reference" : par x croissant, comme le dessin la lit et la reecrit ; un morceau illisible
//  a la fin). Les rangs des points lus sont ceux du dessin (entryOfPoint, pointOfEntry).
[[nodiscard]] bool        hasList(const Object&) noexcept;
[[nodiscard]] Parsed      listOf(const Object&);
[[nodiscard]] bool        listEditable(const Object&);
[[nodiscard]] std::size_t listMinimum(const Object&) noexcept;   // ligne 2, polygone 3, reference 0 (vide : aucune)
void                      setList(Object&, std::vector<Vertex>);

// ---- les cibles du dessin --------------------------------------------------------------------
//  SHAPE : les points d'une ligne, d'un polygone ; ZONE : ceux d'une zone d'un plan (`zone` :
//  son rang), en % du plan - le cadre du plan ne change pas ; REFERENCE : la courbe de reference
//  d'une courbe XY, dans la zone de trace.
struct Target {
    enum class What : std::uint8_t { None, Shape, Zone, Reference };
    What what{What::None};
    Id   object{kNoId};
    int  zone{-1};
    [[nodiscard]] bool valid() const noexcept { return what != What::None && object != kNoId; }
    bool operator==(const Target&) const = default;
};
// Les sommets de la cible, dans la vue ; vide : elle n'existe plus (ou n'a rien a montrer).
[[nodiscard]] std::vector<edit::Pt> points(const View&, const Target&);
[[nodiscard]] bool        closed(const View&, const Target&);      // polygone, zone : le dernier rejoint le premier
[[nodiscard]] std::size_t minimum(const View&, const Target&);
[[nodiscard]] bool        editable(const View&, const Target&);
// Le sommet sous le point de la vue, a `tolerance` pres (le plus proche) ; -1 : aucun.
[[nodiscard]] int vertexAt(const View&, const Target&, edit::Pt at, double tolerance);
// Le segment sous le point : le rang de son premier sommet (le dernier segment d'un polygone va du
// dernier au premier) ; -1 : aucun. `foot` : le point du segment le plus proche (dans la vue).
[[nodiscard]] int segmentAt(const View&, const Target&, edit::Pt at, double tolerance, edit::Pt* foot = nullptr);
// LES GESTES. Faux (ou -1) : rien n'a change (hors de la liste, trop peu de sommets).
bool moveVertex(View&, const Target&, std::size_t index, edit::Pt at);
int  insertVertex(View&, const Target&, std::size_t after, edit::Pt at);   // le rang du sommet insere
bool removeVertex(View&, const Target&, std::size_t index);
// « Tracer une zone » : une zone de plus au plan (« Zone N »), ses points pris dans la vue (en % du plan) ; son
// rang ; -1 : refusee (moins de 3 points, pas un plan, des zones en formule).
int addZone(View&, Id map, const std::vector<edit::Pt>& at);

// LE MAGNETISME D'UN SOMMET, comme celui d'un deplacement : les guides, les bords et centres des
// autres objets et de la vue (a `threshold` pres), et les autres sommets de la cible (deux sommets
// alignes) ; sinon la grille. `xLines` / `yLines` : les reperes a dessiner.
struct Snap {
    edit::Pt            at;
    std::vector<double> xLines, yLines;
};
[[nodiscard]] Snap snapPoint(const View&, edit::Pt p, const std::vector<edit::Pt>& siblings, Id exclude, double threshold);

// ---- « POINTS DEPUIS » (§ 3.1 : un profil trace par une ligne ou un polygone) ----------------------
//  Les points viennent d'une source, lus en marche (dans l'editeur : ses valeurs initiales) :
//   - un tableau ou une LIST de nombres, x et y alternes (x0, y0, x1, y1...) : Courbe, ou Courbe[*] ;
//   - un tableau ou une LIST de structures, leurs membres X et Y : Profil[*].X, Profil[*].Y - les
//     conventions du joker de la courbe XY en profil (lot E) ; un repere $Profil$[*].X garde son $.
//  Ils se placent dans le cadre de l'objet selon une echelle X et une echelle Y (min, max ; vide :
//  celle des donnees) : X min a gauche, Y min en bas, comme un graphique ; hors de l'echelle, au bord.
//  Absentes d'un objet qui ne s'en sert pas : son fichier ne change pas.
inline constexpr std::string_view kPointsFromKey = "pointsFrom";
inline constexpr std::string_view kPointsXMinKey = "pointsXMin";
inline constexpr std::string_view kPointsXMaxKey = "pointsXMax";
inline constexpr std::string_view kPointsYMinKey = "pointsYMin";
inline constexpr std::string_view kPointsYMaxKey = "pointsYMax";
[[nodiscard]] bool hasPointsFrom(const Object&);
// La source telle qu'ecrite : deux formules a joker (x, y), ou une seule (des nombres alternes).
struct PointsSource {
    std::string x, y;           // Profil[*].X, Profil[*].Y ; alternes : x = Courbe[*], y vide
    bool        alternate{false};
};
[[nodiscard]] std::optional<PointsSource> pointsSourceOf(std::string_view text);
// Les points (repere de l'objet) : `source` donne les elements, `text` la valeur d'une formule (le
// moteur ; dans l'editeur, previewText). nullopt : la source ne se lit pas (aucun point).
[[nodiscard]] std::optional<std::vector<edit::Pt>> pointsFromSource(const Object&, const sources::SourceFn& source,
                                                                   const sources::TextFn& text);
// L'APERCU DE L'EDITEUR : la valeur initiale d'un chemin (Courbe[2], Profil[1].X : la valeur initiale de la
// case, celle de son type pour une structure ; une LIST : l'element de son litteral). nullopt : inconnue.
[[nodiscard]] sources::TextFn previewText(const Project&);
// Les points que l'editeur dessine pour « Points depuis » (previewSources, previewText) ; vide : rien a dessiner.
[[nodiscard]] std::vector<edit::Pt> previewPoints(const Project&, const Object&);

} // namespace hmi::vertices
