// =============================================================================
//  ui/Shapes.hpp — polygones, ellipses, arcs, traits : le dessin des vues IHM
// -----------------------------------------------------------------------------
//  Au-dessus de gfx::IRenderer::fillTriangles et de line : ce fichier ne
//  connait ni SDL ni le modele IHM. Un polygone concave est decoupe en
//  triangles (oreilles), une ellipse devient un polygone de 48 cotes.
// =============================================================================
#pragma once

#include "../platform/Renderer.hpp"

#include <vector>

namespace ui::shapes {

using gfx::Color;
using gfx::Point;

void fillPolygon(gfx::IRenderer&, const std::vector<Point>&, Color);
void strokePolyline(gfx::IRenderer&, const std::vector<Point>&, bool closed, Color, float width);

[[nodiscard]] std::vector<Point> ellipse(Point centre, float rx, float ry, int segments = 48);
// Un arc de `fromDeg` a `toDeg` (0 = a droite, sens horaire a l'ecran).
[[nodiscard]] std::vector<Point> arc(Point centre, float r, float fromDeg, float toDeg, int segments = 32);
// La bande entre deux arcs, pour une jauge.
void fillArcBand(gfx::IRenderer&, Point centre, float rInner, float rOuter, float fromDeg, float toDeg, Color);
// Un rectangle arrondi en polygone (pour le tourner ensuite).
[[nodiscard]] std::vector<Point> roundedRect(float x, float y, float w, float h, float radius);

// 1.41.0 : LE CADRE ARRONDI des boutons, des champs et des cartes : le fond et son trait d'un pixel, les coins lisses
// (fillRoundedRect deux fois : le trait, puis le fond un pixel en dedans). Un fond translucide : le fond, puis le trait
// seul (strokeRoundedRect). Un rayon de moins de 1,5 px : un rectangle et son trait, comme avant.
void fillFramedRect(gfx::IRenderer&, const gfx::Rect&, Color fill, Color border, float radius);
// Un anneau arrondi de `width` pixels, a l'interieur de `r` (un trait, un halo translucide autour d'un champ).
void strokeRoundedRect(gfx::IRenderer&, const gfx::Rect&, Color, float radius, float width = 1.f);

} // namespace ui::shapes
