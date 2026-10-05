#include "Icons.hpp"

#include "../core/CodeIcons.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ui {
namespace {

// Every icon is designed on a 16x16 grid. This helper maps grid units to the
// destination rectangle, so one definition serves a 14 px tree row and a 32 px
// toolbar button.
struct Canvas {
    gfx::IRenderer& r;
    gfx::Rect       box;
    gfx::Color      c;
    float           u;      // one grid unit in pixels
    float           ox, oy; // origin of the 16x16 square inside box

    Canvas(gfx::IRenderer& renderer, const gfx::Rect& b, gfx::Color color)
        : r(renderer), box(b), c(color) {
        const float side = std::min(b.w, b.h);
        u  = side / 16.f;
        ox = b.x + (b.w - side) * 0.5f;
        oy = b.y + (b.h - side) * 0.5f;
    }

    [[nodiscard]] gfx::Point p(float x, float y) const { return {ox + x * u, oy + y * u}; }
    [[nodiscard]] gfx::Rect  rect(float x, float y, float w, float h) const {
        return {ox + x * u, oy + y * u, w * u, h * u};
    }
    [[nodiscard]] float stroke() const { return std::max(1.f, u * 0.9f); }

    void fill(float x, float y, float w, float h) const { r.fillRect(rect(x, y, w, h), c); }
    void frame(float x, float y, float w, float h) const { r.strokeRect(rect(x, y, w, h), c, stroke()); }
    void line(float x1, float y1, float x2, float y2) const {
        r.line(p(x1, y1), p(x2, y2), c, stroke());
    }
    void dot(float x, float y) const { fill(x - 0.75f, y - 0.75f, 1.5f, 1.5f); }
};

void folder(const Canvas& g, bool open) {
    g.line(1.5f, 4.f, 6.f, 4.f);          // tab
    g.line(6.f, 4.f, 7.f, 5.5f);
    if (open) {
        g.frame(1.5f, 5.5f, 13.f, 7.5f);
        g.line(3.f, 8.f, 14.5f, 8.f);     // open lid
    } else {
        g.frame(1.5f, 5.5f, 13.f, 7.5f);
    }
}

void chip(const Canvas& g) {
    g.frame(4.f, 4.f, 8.f, 8.f);
    g.fill(6.5f, 6.5f, 3.f, 3.f);
    for (float y : {5.5f, 8.f, 10.5f}) {   // pins
        g.line(2.f, y, 4.f, y);
        g.line(12.f, y, 14.f, y);
    }
}

void document(const Canvas& g) {
    g.frame(3.f, 2.f, 10.f, 12.f);
    g.line(5.f, 5.f, 11.f, 5.f);
    g.line(5.f, 7.5f, 11.f, 7.5f);
    g.line(5.f, 10.f, 9.f, 10.f);
}

void triangleUp(const Canvas& g, float cx, float cy, float half) {
    // Outline triangle: two slanted sides plus a base.
    g.line(cx, cy - half, cx - half, cy + half);
    g.line(cx, cy - half, cx + half, cy + half);
    g.line(cx - half, cy + half, cx + half, cy + half);
}

} // namespace

namespace {

// 1.8.0 : une icone du catalogue, trait par trait (les memes donnees que le PDF).
void codeGlyph(const Canvas& g, int index) {
    const float w = std::max(1.f, g.u * 1.25f);
    for (const auto& path : core::codeicons::glyph(static_cast<std::size_t>(index))) {
        const auto& pts = path.points;
        if (pts.size() < 2) continue;
        if (path.filled) {
            // En eventail depuis le centre : les surfaces du catalogue sont convexes.
            float cx = 0.f, cy = 0.f;
            for (const auto& q : pts) { cx += q.x; cy += q.y; }
            cx /= static_cast<float>(pts.size());
            cy /= static_cast<float>(pts.size());
            std::vector<gfx::Vertex> v;
            v.reserve(pts.size() * 3);
            for (std::size_t i = 0; i < pts.size(); ++i) {
                const auto& a = pts[i];
                const auto& b = pts[(i + 1) % pts.size()];
                v.push_back({g.p(cx, cy), g.c});
                v.push_back({g.p(a.x, a.y), g.c});
                v.push_back({g.p(b.x, b.y), g.c});
            }
            g.r.fillTriangles(v.data(), v.size());
            continue;
        }
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) g.r.line(g.p(pts[i].x, pts[i].y), g.p(pts[i + 1].x, pts[i + 1].y), g.c, w);
        if (path.closed) g.r.line(g.p(pts.back().x, pts.back().y), g.p(pts.front().x, pts.front().y), g.c, w);
    }
}

} // namespace

Icon codeIcon(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(core::codeicons::kCount)) return Icon::None;
    return static_cast<Icon>(static_cast<int>(Icon::CodeInit) + index);
}

int codeIconIndex(Icon icon) noexcept {
    const int i = static_cast<int>(icon) - static_cast<int>(Icon::CodeInit);
    return i >= 0 && i < static_cast<int>(core::codeicons::kCount) ? i : -1;
}

gfx::Color codeIconColor(int index) noexcept {
    const auto rgb = core::codeicons::info(static_cast<std::size_t>(index < 0 ? 0 : index)).rgb;
    return gfx::Color{static_cast<std::uint8_t>((rgb >> 16) & 0xFF), static_cast<std::uint8_t>((rgb >> 8) & 0xFF),
                      static_cast<std::uint8_t>(rgb & 0xFF), 255};
}

void drawIcon(gfx::IRenderer& r, Icon icon, const gfx::Rect& box, gfx::Color color) {
    if (icon == Icon::None || box.empty()) return;
    const Canvas g{r, box, color};
    if (const int ci = codeIconIndex(icon); ci >= 0) {
        codeGlyph(g, ci);
        return;
    }

    switch (icon) {
        case Icon::Folder:      folder(g, false); break;
        case Icon::FolderOpen:  folder(g, true);  break;

        case Icon::Project:
            g.frame(2.f, 3.f, 12.f, 11.f);
            g.line(2.f, 6.f, 14.f, 6.f);
            g.dot(4.f, 4.5f);
            break;

        case Icon::Cpu:   chip(g); break;

        case Icon::Rack:
            g.frame(1.5f, 3.f, 13.f, 10.f);
            for (float x : {5.f, 8.f, 11.f}) g.line(x, 3.f, x, 13.f);
            break;

        case Icon::Module:
            g.frame(5.f, 2.f, 6.f, 12.f);
            g.line(6.5f, 4.5f, 9.5f, 4.5f);
            g.line(6.5f, 6.5f, 9.5f, 6.5f);
            break;

        case Icon::Task:                       // a clock: cyclic execution
            g.frame(3.f, 3.f, 10.f, 10.f);
            g.line(8.f, 5.5f, 8.f, 8.f);
            g.line(8.f, 8.f, 10.f, 9.5f);
            break;

        case Icon::Section:
        case Icon::Program:
            g.frame(2.5f, 3.f, 11.f, 10.f);
            g.line(4.5f, 6.f, 6.5f, 8.f);      // a '<' caret, as in code
            g.line(6.5f, 8.f, 4.5f, 10.f);
            g.line(8.f, 10.5f, 11.5f, 10.5f);
            break;

        case Icon::DerivedType:                // braces: a structure
            g.line(6.f, 3.f, 4.5f, 4.5f);
            g.line(4.5f, 4.5f, 4.5f, 11.5f);
            g.line(4.5f, 11.5f, 6.f, 13.f);
            g.line(10.f, 3.f, 11.5f, 4.5f);
            g.line(11.5f, 4.5f, 11.5f, 11.5f);
            g.line(11.5f, 11.5f, 10.f, 13.f);
            break;

        case Icon::FunctionBlock:              // a block with input/output legs
            g.frame(4.5f, 4.f, 7.f, 8.f);
            g.line(2.f, 6.f, 4.5f, 6.f);
            g.line(2.f, 10.f, 4.5f, 10.f);
            g.line(11.5f, 8.f, 14.f, 8.f);
            break;

        case Icon::Library:                    // three books on a shelf
            g.frame(2.5f, 3.f, 3.f, 10.f);
            g.frame(6.5f, 4.f, 3.f, 9.f);
            g.frame(10.5f, 3.f, 3.f, 10.f);
            break;

        case Icon::Variable:
            g.line(5.f, 4.f, 5.f, 12.f);
            g.line(5.f, 4.f, 9.f, 4.f);
            g.line(5.f, 8.f, 8.f, 8.f);
            break;

        case Icon::LocatedVariable:            // variable bound to a terminal
            g.line(4.f, 4.f, 4.f, 12.f);
            g.line(4.f, 4.f, 8.f, 4.f);
            g.line(4.f, 8.f, 7.f, 8.f);
            g.frame(10.f, 6.f, 4.f, 4.f);
            break;

        case Icon::Constant:
            g.line(3.f, 8.f, 13.f, 8.f);
            g.line(3.f, 10.5f, 13.f, 10.5f);
            g.line(6.f, 4.5f, 10.f, 4.5f);
            break;

        case Icon::AnimationTable:
            g.frame(2.f, 3.5f, 12.f, 9.f);
            g.line(2.f, 6.5f, 14.f, 6.5f);
            g.line(8.f, 3.5f, 8.f, 12.5f);
            break;

        case Icon::Info:
            g.frame(3.f, 3.f, 10.f, 10.f);
            g.dot(8.f, 5.5f);
            g.line(8.f, 7.5f, 8.f, 11.f);
            break;

        case Icon::Warning:
            triangleUp(g, 8.f, 8.f, 5.5f);
            g.line(8.f, 6.f, 8.f, 10.f);
            g.dot(8.f, 11.5f);
            break;

        case Icon::Error:
            g.frame(3.f, 3.f, 10.f, 10.f);
            g.line(5.5f, 5.5f, 10.5f, 10.5f);
            g.line(10.5f, 5.5f, 5.5f, 10.5f);
            break;

        case Icon::Ok:
            g.line(4.f, 8.5f, 7.f, 11.5f);
            g.line(7.f, 11.5f, 12.5f, 4.5f);
            break;

        // Une etoile a cinq branches, tracee comme un pentagramme ferme : dix
        // segments suffisent, et sur une grille de 16 unites c'est ce qui reste
        // lisible a 14 pixels de haut.
        case Icon::Star:
        case Icon::StarFilled: {
            constexpr float kX[10] = {8.f, 9.6f, 14.4f, 10.7f, 12.1f,
                                      8.f, 3.9f, 5.3f, 1.6f, 6.4f};
            constexpr float kY[10] = {1.6f, 6.2f, 6.2f, 9.2f, 14.f,
                                      11.2f, 14.f, 9.2f, 6.2f, 6.2f};
            for (int i = 0; i < 10; ++i) {
                const int j = (i + 1) % 10;
                g.line(kX[i], kY[i], kX[j], kY[j]);
            }
            // La pleine est la meme, remplie au centre : sans renderer de
            // polygones, un coeur plein suffit a la distinguer d'un coup d'oeil.
            if (icon == Icon::StarFilled) {
                g.fill(5.6f, 5.6f, 4.8f, 4.4f);
                g.line(8.f, 2.6f, 6.6f, 6.4f);
                g.line(8.f, 2.6f, 9.4f, 6.4f);
                g.line(2.9f, 6.6f, 6.2f, 8.2f);
                g.line(13.1f, 6.6f, 9.8f, 8.2f);
                g.line(5.1f, 12.9f, 6.8f, 9.6f);
                g.line(10.9f, 12.9f, 9.2f, 9.6f);
            }
            break;
        }

        case Icon::Open:                       // folder with an arrow going in
            folder(g, true);
            g.line(8.f, 6.f, 8.f, 10.5f);
            g.line(6.f, 8.5f, 8.f, 10.5f);
            g.line(10.f, 8.5f, 8.f, 10.5f);
            break;

        case Icon::Save:                       // floppy: still the clearest glyph
            g.frame(2.5f, 2.5f, 11.f, 11.f);
            g.fill(5.f, 2.5f, 6.f, 4.f);
            g.frame(4.5f, 8.5f, 7.f, 5.f);
            break;

        case Icon::Refresh:                    // open circular arrow
            g.line(4.f, 5.f, 12.f, 5.f);
            g.line(12.f, 5.f, 12.f, 11.f);
            g.line(12.f, 11.f, 4.f, 11.f);
            g.line(4.f, 11.f, 4.f, 8.f);
            g.line(4.f, 5.f, 6.f, 3.f);
            g.line(4.f, 5.f, 6.f, 7.f);
            break;

        case Icon::Analyze:                    // magnifier over a bar chart
            g.frame(2.f, 3.f, 8.f, 8.f);
            g.line(9.f, 10.f, 13.5f, 14.f);
            g.line(4.f, 9.f, 4.f, 7.f);
            g.line(6.f, 9.f, 6.f, 5.f);
            g.line(8.f, 9.f, 8.f, 6.5f);
            break;

        case Icon::Export:
            document(g);
            g.line(11.f, 11.f, 14.5f, 11.f);
            g.line(13.f, 9.5f, 14.5f, 11.f);
            g.line(13.f, 12.5f, 14.5f, 11.f);
            break;

        case Icon::Print:
            g.frame(4.f, 2.5f, 8.f, 3.f);
            g.frame(2.f, 5.5f, 12.f, 5.f);
            g.frame(4.f, 10.5f, 8.f, 3.5f);
            break;

        case Icon::Search:
            g.frame(3.f, 3.f, 8.f, 8.f);
            g.line(10.5f, 10.5f, 14.f, 14.f);
            break;

        case Icon::Settings:                   // gear approximated by a cross of teeth
            g.frame(6.f, 6.f, 4.f, 4.f);
            g.line(8.f, 1.5f, 8.f, 5.f);
            g.line(8.f, 11.f, 8.f, 14.5f);
            g.line(1.5f, 8.f, 5.f, 8.f);
            g.line(11.f, 8.f, 14.5f, 8.f);
            break;

        case Icon::Play:                       // right-pointing triangle
            g.line(5.f, 3.f, 5.f, 13.f);
            g.line(5.f, 3.f, 12.5f, 8.f);
            g.line(5.f, 13.f, 12.5f, 8.f);
            break;

        case Icon::Pause:                      // two upright bars
            g.line(6.f, 3.f, 6.f, 13.f);
            g.line(7.f, 3.f, 7.f, 13.f);
            g.line(10.f, 3.f, 10.f, 13.f);
            g.line(11.f, 3.f, 11.f, 13.f);
            break;

        case Icon::Stop:                       // a filled square
            for (float y = 4.f; y <= 12.f; y += 1.f) g.line(4.f, y, 12.f, y);
            break;

        case Icon::StepOnce:                   // a triangle against a bar
            g.line(4.f, 3.f, 4.f, 13.f);
            g.line(4.f, 3.f, 10.f, 8.f);
            g.line(4.f, 13.f, 10.f, 8.f);
            g.line(12.f, 3.f, 12.f, 13.f);
            break;

        case Icon::Halt:                       // a warning triangle with a bar
            g.line(8.f, 2.f, 14.f, 13.f);
            g.line(14.f, 13.f, 2.f, 13.f);
            g.line(2.f, 13.f, 8.f, 2.f);
            g.line(8.f, 6.f, 8.f, 9.f);
            g.line(8.f, 11.f, 8.f, 11.5f);
            break;

        case Icon::Force:                      // a hand-like arrow pushing a bar
            g.line(2.f, 8.f, 10.f, 8.f);
            g.line(7.f, 5.f, 10.f, 8.f);
            g.line(7.f, 11.f, 10.f, 8.f);
            g.line(13.f, 3.f, 13.f, 13.f);
            break;

        case Icon::Close:
            g.line(4.5f, 4.5f, 11.5f, 11.5f);
            g.line(11.5f, 4.5f, 4.5f, 11.5f);
            break;

        case Icon::Document: document(g); break;

        case Icon::Chart:
            g.line(2.5f, 13.f, 13.5f, 13.f);
            g.line(2.5f, 13.f, 2.5f, 3.f);
            g.fill(4.5f, 8.f, 2.f, 5.f);
            g.fill(7.5f, 5.f, 2.f, 8.f);
            g.fill(10.5f, 9.5f, 2.f, 3.5f);
            break;

        case Icon::Filter:                     // funnel
            g.line(2.5f, 3.5f, 13.5f, 3.5f);
            g.line(2.5f, 3.5f, 7.f, 8.5f);
            g.line(13.5f, 3.5f, 9.f, 8.5f);
            g.line(7.f, 8.5f, 7.f, 13.f);
            g.line(9.f, 8.5f, 9.f, 11.f);
            g.line(7.f, 13.f, 9.f, 11.f);
            break;

        case Icon::Collapse:
            g.frame(3.f, 3.f, 10.f, 10.f);
            g.line(5.5f, 8.f, 10.5f, 8.f);
            break;

        case Icon::Expand:
            g.frame(3.f, 3.f, 10.f, 10.f);
            g.line(5.5f, 8.f, 10.5f, 8.f);
            g.line(8.f, 5.5f, 8.f, 10.5f);
            break;

        case Icon::Screen:                     // un moniteur sur son pied
            g.frame(1.5f, 2.5f, 13.f, 8.5f);
            g.fill(3.5f, 4.5f, 3.5f, 2.5f);    // un synoptique dessus
            g.line(8.5f, 5.f, 12.f, 5.f);
            g.line(8.5f, 7.5f, 11.f, 7.5f);
            g.line(8.f, 11.f, 8.f, 13.f);
            g.line(5.f, 13.5f, 11.f, 13.5f);
            break;
        case Icon::Image:                      // un cadre, une montagne, un soleil
            g.frame(1.5f, 3.f, 13.f, 10.f);
            g.line(3.f, 11.5f, 6.5f, 7.f);
            g.line(6.5f, 7.f, 9.f, 10.f);
            g.line(9.f, 10.f, 10.5f, 8.5f);
            g.line(10.5f, 8.5f, 13.f, 11.5f);
            g.dot(11.f, 5.5f);
            break;
        case Icon::Layers:                     // trois feuilles empilees
            g.frame(5.5f, 2.f, 9.f, 6.f);
            g.line(3.5f, 4.f, 3.5f, 10.f);
            g.line(3.5f, 10.f, 12.5f, 10.f);
            g.line(1.5f, 6.f, 1.5f, 12.5f);
            g.line(1.5f, 12.5f, 10.5f, 12.5f);
            break;
        case Icon::Code:                       // </>
            g.line(5.5f, 4.5f, 2.f, 8.f);
            g.line(2.f, 8.f, 5.5f, 11.5f);
            g.line(10.5f, 4.5f, 14.f, 8.f);
            g.line(14.f, 8.f, 10.5f, 11.5f);
            g.line(9.f, 3.5f, 7.f, 12.5f);
            break;
        case Icon::Lock:                       // un cadenas : l'anse, le corps, la serrure
            g.line(5.f, 7.f, 5.f, 4.5f);
            g.line(5.f, 4.5f, 6.5f, 2.5f);
            g.line(6.5f, 2.5f, 9.5f, 2.5f);
            g.line(9.5f, 2.5f, 11.f, 4.5f);
            g.line(11.f, 4.5f, 11.f, 7.f);
            g.frame(3.f, 7.f, 10.f, 7.f);
            g.fill(7.25f, 9.f, 1.5f, 3.f);
            break;
        case Icon::User:                       // une tete et des epaules
            g.frame(5.5f, 2.f, 5.f, 5.f);
            g.line(2.5f, 14.f, 3.5f, 10.5f);
            g.line(3.5f, 10.5f, 6.f, 9.f);
            g.line(6.f, 9.f, 10.f, 9.f);
            g.line(10.f, 9.f, 12.5f, 10.5f);
            g.line(12.5f, 10.5f, 13.5f, 14.f);
            g.line(2.5f, 14.f, 13.5f, 14.f);
            break;

        case Icon::Globe: {                    // le contour, un meridien, l'equateur
            const auto ring = [&](float rx) {
                constexpr int n = 20;
                for (int k = 0; k < n; ++k) {
                    const float a0 = 6.2831853f * static_cast<float>(k) / n, a1 = 6.2831853f * static_cast<float>(k + 1) / n;
                    g.line(8.f + rx * std::cos(a0), 8.f + 6.f * std::sin(a0), 8.f + rx * std::cos(a1), 8.f + 6.f * std::sin(a1));
                }
            };
            ring(6.f);
            ring(2.6f);
            g.line(2.f, 8.f, 14.f, 8.f);
            break;
        }

        case Icon::Network:                    // l'automate en haut a gauche, le poste en bas a droite, relies
            g.frame(1.5f, 1.5f, 6.f, 4.5f);
            g.frame(8.5f, 10.f, 6.f, 4.5f);
            g.line(4.5f, 6.f, 4.5f, 12.25f);
            g.line(4.5f, 12.25f, 8.5f, 12.25f);
            g.fill(3.75f, 11.5f, 1.5f, 1.5f);
            break;
        case Icon::Station:                    // un ecran, son pied, sa base
            g.frame(1.5f, 2.f, 13.f, 9.f);
            g.line(8.f, 11.f, 8.f, 13.5f);
            g.line(5.f, 14.f, 11.f, 14.f);
            break;
        case Icon::Mail:                       // une enveloppe
            g.frame(1.5f, 3.5f, 13.f, 9.f);
            g.line(1.5f, 3.5f, 8.f, 9.f);
            g.line(8.f, 9.f, 14.5f, 3.5f);
            break;

        case Icon::Undo:                       // une fleche vers la gauche, la queue recourbee
            g.line(3.f, 6.f, 10.f, 6.f);
            g.line(10.f, 6.f, 12.5f, 8.5f);
            g.line(12.5f, 8.5f, 12.5f, 10.5f);
            g.line(12.5f, 10.5f, 10.5f, 12.5f);
            g.line(10.5f, 12.5f, 7.f, 12.5f);
            g.line(3.f, 6.f, 5.5f, 3.5f);
            g.line(3.f, 6.f, 5.5f, 8.5f);
            break;

        case Icon::Redo:                       // la meme, vers la droite
            g.line(13.f, 6.f, 6.f, 6.f);
            g.line(6.f, 6.f, 3.5f, 8.5f);
            g.line(3.5f, 8.5f, 3.5f, 10.5f);
            g.line(3.5f, 10.5f, 5.5f, 12.5f);
            g.line(5.5f, 12.5f, 9.f, 12.5f);
            g.line(13.f, 6.f, 10.5f, 3.5f);
            g.line(13.f, 6.f, 10.5f, 8.5f);
            break;

        case Icon::History: {                  // une horloge
            const float cx = 8.f, cy = 8.5f, rr = 5.5f;
            constexpr int n = 12;
            for (int i = 0; i < n; ++i) {
                const float a0 = 6.2831853f * static_cast<float>(i) / n;
                const float a1 = 6.2831853f * static_cast<float>(i + 1) / n;
                g.line(cx + rr * std::cos(a0), cy + rr * std::sin(a0), cx + rr * std::cos(a1), cy + rr * std::sin(a1));
            }
            g.line(cx, cy, cx, cy - 3.5f);
            g.line(cx, cy, cx + 2.5f, cy + 1.5f);
            break;
        }

        case Icon::None: break;
        // 1.8.0 : les icones au choix sont tracees plus haut (codeGlyph).
        case Icon::CodeInit: case Icon::CodeTor: case Icon::CodeAna: case Icon::CodeOutputs: case Icon::CodeGrafcet:
        case Icon::CodeActions: case Icon::CodeConfig: case Icon::CodeReset: case Icon::CodeAlarm: case Icon::CodeSafety:
        case Icon::CodeHmi: case Icon::CodeComm: case Icon::CodeCalc: case Icon::CodeTimer: case Icon::CodeMatrix:
        case Icon::CodeReports: case Icon::CodeData: case Icon::CodeDebug: break;
    }
}

} // namespace ui
