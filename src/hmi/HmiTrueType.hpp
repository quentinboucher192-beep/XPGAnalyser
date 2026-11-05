// =============================================================================
//  hmi/HmiTrueType.hpp - 1.26.0 : lire, modifier et ecrire une police TrueType
// -----------------------------------------------------------------------------
//  LE CLIENT (05/11) : << pouvoir editer des TTF en live sur le logiciel, en creer,
//  en dupliquer a partir de >>.
//
//  UNE POLICE EN MEMOIRE (Font) : ses noms (famille, version, droits, licence),
//  ses mesures (unites par cadratin, hauteurs au-dessus et au-dessous de la ligne,
//  des capitales, des minuscules), et ses glyphes. Un glyphe : les caracteres qu'il
//  dessine, sa chasse, ses contours (des points SUR la courbe et HORS de la courbe :
//  des courbes du second degre, celles de TrueType), ou des COMPOSANTS (un << e >>
//  et un accent : modifier le << e >> change << e accent >>, comme dans la police).
//
//  LIRE (read) : une TrueType (contours glyf ; une OpenType a contours CFF est
//  refusee : elle s'emploie, mais ne se modifie pas). Les tables de typographie
//  (crenage, substitutions : GDEF GPOS GSUB kern gasp) sont gardees telles quelles ;
//  les instructions de lissage (hinting) ne le sont pas : elles ne vaudraient plus
//  pour un contour change, et l'IHM dessine sans elles.
//
//  ECRIRE (write) : un .ttf complet, que Windows installe et que le PDF integre :
//  head hhea maxp OS/2 name cmap (formats 4 et 12) post (format 3) loca glyf hmtx,
//  plus les tables gardees. Le meme contenu donne les memes octets.
//
//  LES NOMS RESERVES : une police copiee d'une autre ne porte pas son nom (la SIL
//  Open Font License le demande pour ses << noms reserves >> : Liberation,
//  Carlito... ; la licence Bitstream Vera de DejaVu pour << Bitstream >> et
//  << Vera >>). nameAllowed le verifie.
// =============================================================================
#pragma once

#include "HmiMedia.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::ttf {

struct Point {
    int  x{0}, y{0};
    bool on{true};                     // sur la courbe ; faux : un point de controle
    bool operator==(const Point&) const = default;
};
struct Contour {
    std::vector<Point> points;
    bool operator==(const Contour&) const = default;
};
struct Component {
    int    glyph{0};
    int    dx{0}, dy{0};
    double xx{1}, xy{0}, yx{0}, yy{1};   // la transformation (F2Dot14)
    bool   useMyMetrics{false};          // le compose prend la chasse de ce composant
    bool   roundXY{false};
    bool operator==(const Component&) const = default;
};
struct Glyph {
    std::vector<std::uint32_t> codes;    // les caracteres dessines (croissants) ; vide : aucun (.notdef, une ligature)
    int                        advance{0};
    std::vector<Contour>       contours;
    std::vector<Component>     components;   // non vide : un compose (et pas de contours)
    [[nodiscard]] bool composite() const noexcept { return !components.empty(); }
    bool operator==(const Glyph&) const = default;
};
struct Box {
    int  xMin{0}, yMin{0}, xMax{0}, yMax{0};
    bool empty{true};
};

struct Font {
    // ---- les noms ----
    std::string family{"Ma police"};
    std::string version{"1.000"};
    std::string copyright, trademark, manufacturer, designer, description, license, licenseUrl;
    // ---- le style ----
    bool   bold{false}, italic{false};
    int    weight{400};
    double italicAngle{0};
    bool   fixedPitch{false};
    // ---- les mesures (unites de la police) ----
    int unitsPerEm{1000};
    int ascent{800}, descent{-200}, lineGap{0};
    int capHeight{700}, xHeight{500};
    int underlinePosition{-100}, underlineThickness{50};
    std::int64_t created{3846096000}, modified{3846096000};   // secondes depuis 1904 (01/11/2025 par defaut)
    // ---- les glyphes : [0] = .notdef ----
    std::vector<Glyph> glyphs;
    // ---- ce qui est garde de la police lue ----
    Bytes                                    os2;      // sa table OS/2 (ses mesures sont reecrites)
    std::vector<std::pair<std::string, Bytes>> keep;   // GDEF GPOS GSUB kern gasp, telles quelles
    std::vector<std::string>                 reserved; // les noms qu'une copie ne porte pas
};

// ---- lire, ecrire ----
[[nodiscard]] bool  read(const Bytes& data, Font& out, std::string* why = nullptr);
[[nodiscard]] Bytes write(const Font&);

// ---- creer, dupliquer ----
// NOUVELLE POLICE : .notdef (une boite), l'espace, et les glyphes vides des lettres, chiffres, signes, accentuees (Latin-1)
// et de l'euro, a dessiner.
[[nodiscard]] Font blank(const std::string& family);
// DUPLIQUER A PARTIR DE : la meme police sous un autre nom (les droits et la licence suivent la copie).
[[nodiscard]] Font duplicate(const Font& source, const std::string& family);
// Le nom d'une copie : pas vide, pas celui de la source, sans ses noms reserves ; faux et `why` sinon.
[[nodiscard]] bool nameAllowed(const Font& source, std::string_view family, std::string* why = nullptr);
// "Normal", "Gras", "Italique", "Gras italique".
[[nodiscard]] std::string styleLabel(const Font&);
// Le nom PostScript : "MaPolice-Bold".
[[nodiscard]] std::string postScriptName(const Font&);

// ---- les glyphes ----
// Le glyphe d'un caractere ; -1 : absent.
[[nodiscard]] int glyphOf(const Font&, std::uint32_t code);
// Un glyphe pour `code` : le sien s'il existe, sinon un glyphe vide ajoute en fin (chasse : `advance`, ou la moitie du cadratin).
int ensureGlyph(Font&, std::uint32_t code, int advance = -1);
// "a", "e accent aigu" n'est pas tente : le caractere lui-meme s'il s'affiche, sinon "U+00A0".
[[nodiscard]] std::string codeText(std::uint32_t code);
// La chasse reelle (un compose << useMyMetrics >> prend celle de son composant).
[[nodiscard]] int advanceOf(const Font&, int glyph);
// Les contours, composants developpes (dans les unites de la police).
[[nodiscard]] std::vector<Contour> outline(const Font&, int glyph);
[[nodiscard]] Box bounds(const Font&, int glyph);
// Les composes qui emploient ce glyphe (directement).
[[nodiscard]] std::vector<int> usersOf(const Font&, int glyph);
// Un compose devient un glyphe simple (ses contours developpes) : il se modifie point par point.
void decompose(Font&, int glyph);
// Copier le dessin d'un glyphe d'une autre police (mis a l'echelle de celle-ci, chasse comprise).
void copyGlyph(Font& into, int glyph, const Font& from, int fromGlyph);

// ---- modifier un contour ----
// Les segments d'un contour : une droite (p0 p2) ou une courbe (p0 c p2), les points implicites calcules. `start` : le
// rang du point qui commence le segment (un point sur la courbe, ou le controle d'une courbe dont le debut est implicite).
struct Segment {
    double x0, y0, cx, cy, x1, y1;
    bool   curve;
    int    start;
};
[[nodiscard]] std::vector<Segment> segments(const Contour&);
// Ajouter un point sur le contour a la position t (0..1) du segment `seg` : la forme ne change pas. Rend le rang du point.
int insertPoint(Contour&, int seg, double t);
// Retirer un point (un contour de moins de 2 points disparait de lui-meme a l'ecriture).
void removePoint(Contour&, int index);
void addRectangle(Glyph&, int x0, int y0, int x1, int y1);
void addEllipse(Glyph&, int cx, int cy, int rx, int ry);
void reverse(Contour&);
// Le point du contour le plus proche de (x, y) a moins de `radius` unites ; -1 : aucun. Le segment le plus proche et
// sa position (pour << ajouter un point sur le trait >>).
struct Hit {
    int    contour{-1}, point{-1}, segment{-1};
    double t{0}, distance{0};
};
[[nodiscard]] Hit hitPoint(const std::vector<Contour>&, double x, double y, double radius);
[[nodiscard]] Hit hitSegment(const std::vector<Contour>&, double x, double y, double radius);

// ---- dessiner ----
// Les contours remplis (regle non nulle) dans une image w x h de couverture (0..255) : un point (x, y) de la police va
// en (originX + x * scale, baselineY - y * scale).
[[nodiscard]] std::vector<std::uint8_t> raster(const std::vector<Contour>&, double scale, double originX, double baselineY,
                                               int w, int h);

} // namespace hmi::ttf
