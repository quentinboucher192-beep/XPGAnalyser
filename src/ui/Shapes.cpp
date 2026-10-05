#include "Shapes.hpp"

#include <algorithm>
#include <cmath>

namespace ui::shapes {

namespace {

constexpr float kPi = 3.14159265358979f;

float cross(Point a, Point b, Point c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

bool insideTriangle(Point p, Point a, Point b, Point c) {
    const float d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
    const bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}

} // namespace

void fillPolygon(gfx::IRenderer& r, const std::vector<Point>& in, Color c) {
    if (in.size() < 3 || c.a == 0) return;
    // Oreilles : on retire un sommet convexe dont le triangle ne contient
    // aucun autre sommet, jusqu'a ce qu'il n'en reste que trois.
    std::vector<Point> pts = in;
    float area = 0;
    for (std::size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++)
        area += pts[j].x * pts[i].y - pts[i].x * pts[j].y;
    if (area < 0) std::reverse(pts.begin(), pts.end());   // sens direct (y vers le bas : horaire)
    std::vector<gfx::Vertex> tris;
    tris.reserve((pts.size() - 2) * 3);
    std::size_t guard = pts.size() * pts.size() + 8;
    while (pts.size() > 3 && guard-- > 0) {
        bool cut = false;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const Point a = pts[(i + pts.size() - 1) % pts.size()], b = pts[i], d = pts[(i + 1) % pts.size()];
            if (cross(a, b, d) <= 0) continue;              // concave : pas une oreille
            bool empty = true;
            for (std::size_t k = 0; k < pts.size() && empty; ++k) {
                if (k == i || k == (i + 1) % pts.size() || k == (i + pts.size() - 1) % pts.size()) continue;
                if (insideTriangle(pts[k], a, b, d)) empty = false;
            }
            if (!empty) continue;
            tris.push_back({a, c}); tris.push_back({b, c}); tris.push_back({d, c});
            pts.erase(pts.begin() + static_cast<std::ptrdiff_t>(i));
            cut = true;
            break;
        }
        if (!cut) break;   // polygone croise : on finit en eventail
    }
    for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
        tris.push_back({pts[0], c}); tris.push_back({pts[i], c}); tris.push_back({pts[i + 1], c});
    }
    r.fillTriangles(tris.data(), tris.size());
}

void strokePolyline(gfx::IRenderer& r, const std::vector<Point>& pts, bool closed, Color c, float width) {
    if (pts.size() < 2 || c.a == 0 || width <= 0) return;
    for (std::size_t i = 1; i < pts.size(); ++i) r.line(pts[i - 1], pts[i], c, width);
    if (closed && pts.size() > 2) r.line(pts.back(), pts.front(), c, width);
}

std::vector<Point> ellipse(Point centre, float rx, float ry, int segments) {
    std::vector<Point> out;
    out.reserve(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const float a = 2 * kPi * static_cast<float>(i) / static_cast<float>(segments);
        out.push_back({centre.x + rx * std::cos(a), centre.y + ry * std::sin(a)});
    }
    return out;
}

std::vector<Point> arc(Point centre, float r, float fromDeg, float toDeg, int segments) {
    std::vector<Point> out;
    segments = std::max(2, segments);
    for (int i = 0; i <= segments; ++i) {
        const float a = (fromDeg + (toDeg - fromDeg) * static_cast<float>(i) / static_cast<float>(segments)) * kPi / 180.f;
        out.push_back({centre.x + r * std::cos(a), centre.y + r * std::sin(a)});
    }
    return out;
}

void fillArcBand(gfx::IRenderer& r, Point centre, float rInner, float rOuter, float fromDeg, float toDeg, Color c) {
    const auto outer = arc(centre, rOuter, fromDeg, toDeg);
    const auto inner = arc(centre, rInner, fromDeg, toDeg);
    std::vector<gfx::Vertex> tris;
    for (std::size_t i = 1; i < outer.size(); ++i) {
        tris.push_back({outer[i - 1], c}); tris.push_back({outer[i], c}); tris.push_back({inner[i], c});
        tris.push_back({outer[i - 1], c}); tris.push_back({inner[i], c}); tris.push_back({inner[i - 1], c});
    }
    r.fillTriangles(tris.data(), tris.size());
}

std::vector<Point> roundedRect(float x, float y, float w, float h, float radius) {
    const float rad = std::clamp(radius, 0.f, std::min(w, h) / 2.f);
    if (rad < 0.5f) return {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    std::vector<Point> out;
    auto corner = [&](float cx, float cy, float from) {
        for (int i = 0; i <= 6; ++i) {
            const float a = (from + 90.f * static_cast<float>(i) / 6.f) * kPi / 180.f;
            out.push_back({cx + rad * std::cos(a), cy + rad * std::sin(a)});
        }
    };
    corner(x + w - rad, y + rad, -90);
    corner(x + w - rad, y + h - rad, 0);
    corner(x + rad, y + h - rad, 90);
    corner(x + rad, y + rad, 180);
    return out;
}

} // namespace ui::shapes
