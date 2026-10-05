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

} // namespace ui::shapes
