#include "FontAtlas.hpp"
#include "AtlasSymbols.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb_truetype.h"

namespace gfx {

namespace {

// The atlas grows in rows. 512 is enough for Latin-1 at 16 px with room to
// spare; a face that needs more simply stops adding glyphs rather than
// reallocating mid-frame, and says so through Glyph::loaded.
constexpr std::uint16_t kAtlasSide = 512;
constexpr std::uint16_t kPadding = 1;

// ---- 1.11.2 (T2, decision 155) : LES SYMBOLES QUE L'ATLAS DESSINE ------------
// Voir AtlasSymbols.hpp. Chaque symbole est une boite (en hauteur de capitale H
// de la police chargee) et des traits dedans, en unites de la boite (0..1, y vers
// le bas). Rasterise ici, dans l'atlas, comme un glyphe : le renderer, la mesure
// et la coupe des textes ne voient pas la difference.
constexpr float kPi = 3.14159265358979f;

struct Pt { float x, y; };

struct Prim {
    enum Kind : std::uint8_t { Fill, Stroke, Closed, Disc, Ring, Arc };
    Kind kind{Fill};
    std::vector<Pt> pts;      // Fill / Stroke / Closed
    float width{1.f};         // l'epaisseur, en traits de base
    Pt c{0.5f, 0.5f};         // Disc / Ring / Arc : le centre
    float r{0.5f};            // Disc / Ring / Arc : le rayon, en part du petit cote de la boite
    float a0{0.f}, a1{0.f};   // Arc : de a0 a a1 (degres, sens inverse des aiguilles a l'ecran)
};

struct SymbolShape {
    float w{0.f}, h{0.f};     // la boite, en H
    float space{-1.f};        // >= 0 : une espace (avance = space x celle de ' ' ; 0 : largeur nulle)
    std::vector<Prim> prims;
};

Prim fillP(std::initializer_list<Pt> p) { Prim q; q.kind = Prim::Fill; q.pts = p; return q; }
Prim lineP(std::initializer_list<Pt> p, float w = 1.f) { Prim q; q.kind = Prim::Stroke; q.pts = p; q.width = w; return q; }
Prim loopP(std::initializer_list<Pt> p, float w = 1.f) { Prim q; q.kind = Prim::Closed; q.pts = p; q.width = w; return q; }
Prim discP(Pt c, float r) { Prim q; q.kind = Prim::Disc; q.c = c; q.r = r; return q; }
Prim ringP(Pt c, float r, float w = 1.f) { Prim q; q.kind = Prim::Ring; q.c = c; q.r = r; q.width = w; return q; }
Prim arcP(Pt c, float r, float a0, float a1, float w = 1.f) {
    Prim q; q.kind = Prim::Arc; q.c = c; q.r = r; q.a0 = a0; q.a1 = a1; q.width = w; return q;
}
// La pointe d'une fleche circulaire, au bout d'un arc (boite carree), dans le sens du mouvement.
Prim arcHead(Pt c, float r, float aDeg, bool anticlockwise, float size) {
    const float a = aDeg * kPi / 180.f;
    const Pt e{c.x + r * std::cos(a), c.y - r * std::sin(a)};
    const Pt d = anticlockwise ? Pt{-std::sin(a), -std::cos(a)} : Pt{std::sin(a), std::cos(a)};
    const Pt n{-d.y, d.x};
    return fillP({ {e.x + d.x * size * 0.75f, e.y + d.y * size * 0.75f},
                   {e.x - d.x * size * 0.35f + n.x * size * 0.55f, e.y - d.y * size * 0.35f + n.y * size * 0.55f},
                   {e.x - d.x * size * 0.35f - n.x * size * 0.55f, e.y - d.y * size * 0.35f - n.y * size * 0.55f} });
}
std::vector<Pt> starPoints() {
    std::vector<Pt> out;
    for (int k = 0; k < 10; ++k) {
        const float a = (-90.f + 36.f * static_cast<float>(k)) * kPi / 180.f;
        const float r = (k % 2 == 0) ? 0.5f : 0.2f;
        out.push_back({0.5f + r * std::cos(a), 0.54f + r * std::sin(a)});
    }
    return out;
}

// Le trace de chaque symbole de kDrawnSymbols. false : pas un symbole dessine.
bool symbolShape(char32_t code, SymbolShape& s) {
    auto box = [&](float w, float h, std::initializer_list<Prim> prims) { s.w = w; s.h = h; s.prims = prims; return true; };
    switch (code) {
    // triangles : ▶ ► ▸ / ◀ ◄ ◂ / ▼ ▾ / ▲ ▴
    case 0x25B6: case 0x25BA: return box(0.78f, 0.9f,  { fillP({{0, 0}, {1, 0.5f}, {0, 1}}) });
    case 0x25B8:              return box(0.52f, 0.62f, { fillP({{0, 0}, {1, 0.5f}, {0, 1}}) });
    case 0x25C0: case 0x25C4: return box(0.78f, 0.9f,  { fillP({{1, 0}, {0, 0.5f}, {1, 1}}) });
    case 0x25C2:              return box(0.52f, 0.62f, { fillP({{1, 0}, {0, 0.5f}, {1, 1}}) });
    case 0x25BC:              return box(0.9f, 0.78f,  { fillP({{0, 0}, {1, 0}, {0.5f, 1}}) });
    case 0x25BE:              return box(0.66f, 0.5f,  { fillP({{0, 0}, {1, 0}, {0.5f, 1}}) });
    case 0x25B2:              return box(0.9f, 0.78f,  { fillP({{0, 1}, {1, 1}, {0.5f, 0}}) });
    case 0x25B4:              return box(0.66f, 0.5f,  { fillP({{0, 1}, {1, 1}, {0.5f, 0}}) });
    // formes : ● ○ ◆ ■ □
    case 0x25CF: return box(0.72f, 0.72f, { discP({0.5f, 0.5f}, 0.5f) });
    case 0x25CB: return box(0.72f, 0.72f, { ringP({0.5f, 0.5f}, 0.5f) });
    case 0x25C6: return box(0.8f, 0.9f,   { fillP({{0.5f, 0}, {1, 0.5f}, {0.5f, 1}, {0, 0.5f}}) });
    case 0x25A0: return box(0.72f, 0.72f, { fillP({{0, 0}, {1, 0}, {1, 1}, {0, 1}}) });
    case 0x25A1: return box(0.72f, 0.72f, { loopP({{0.08f, 0.08f}, {0.92f, 0.08f}, {0.92f, 0.92f}, {0.08f, 0.92f}}) });
    // etoiles : ★ ☆
    case 0x2605: case 0x2606: {
        Prim p; p.pts = starPoints();
        p.kind = code == 0x2605 ? Prim::Fill : Prim::Closed;
        p.width = 0.8f;
        s.w = 1.0f; s.h = 0.96f; s.prims = {p};
        return true;
    }
    // coches et croix : ✓ ✔ ✕ ✖ ✗ ✘ ⊘
    case 0x2713: return box(0.92f, 0.82f, { lineP({{0.04f, 0.55f}, {0.36f, 0.9f}, {0.96f, 0.1f}}, 1.15f) });
    case 0x2714: return box(0.92f, 0.82f, { lineP({{0.04f, 0.55f}, {0.36f, 0.9f}, {0.96f, 0.1f}}, 1.6f) });
    case 0x2715: case 0x2717:
        return box(0.72f, 0.72f, { lineP({{0.08f, 0.08f}, {0.92f, 0.92f}}), lineP({{0.92f, 0.08f}, {0.08f, 0.92f}}) });
    case 0x2716: case 0x2718:
        return box(0.72f, 0.72f, { lineP({{0.08f, 0.08f}, {0.92f, 0.92f}}, 1.5f), lineP({{0.92f, 0.08f}, {0.08f, 0.92f}}, 1.5f) });
    case 0x2298: return box(0.92f, 0.92f, { ringP({0.5f, 0.5f}, 0.5f), lineP({{0.2f, 0.8f}, {0.8f, 0.2f}}) });
    // fleches : → ← ↑ ↓ ↔ ↕ ↗ ↘
    case 0x2192: return box(1.1f, 0.72f, { lineP({{0.03f, 0.5f}, {0.95f, 0.5f}}), lineP({{0.6f, 0.12f}, {0.96f, 0.5f}, {0.6f, 0.88f}}) });
    case 0x2190: return box(1.1f, 0.72f, { lineP({{0.97f, 0.5f}, {0.05f, 0.5f}}), lineP({{0.4f, 0.12f}, {0.04f, 0.5f}, {0.4f, 0.88f}}) });
    case 0x2191: return box(0.72f, 1.05f, { lineP({{0.5f, 0.97f}, {0.5f, 0.05f}}), lineP({{0.12f, 0.4f}, {0.5f, 0.04f}, {0.88f, 0.4f}}) });
    case 0x2193: return box(0.72f, 1.05f, { lineP({{0.5f, 0.03f}, {0.5f, 0.95f}}), lineP({{0.12f, 0.6f}, {0.5f, 0.96f}, {0.88f, 0.6f}}) });
    case 0x2194: return box(1.25f, 0.72f, { lineP({{0.04f, 0.5f}, {0.96f, 0.5f}}),
                                            lineP({{0.3f, 0.12f}, {0.04f, 0.5f}, {0.3f, 0.88f}}),
                                            lineP({{0.7f, 0.12f}, {0.96f, 0.5f}, {0.7f, 0.88f}}) });
    case 0x2195: return box(0.72f, 1.2f, { lineP({{0.5f, 0.04f}, {0.5f, 0.96f}}),
                                           lineP({{0.12f, 0.3f}, {0.5f, 0.04f}, {0.88f, 0.3f}}),
                                           lineP({{0.12f, 0.7f}, {0.5f, 0.96f}, {0.88f, 0.7f}}) });
    case 0x2197: return box(0.9f, 0.9f, { lineP({{0.08f, 0.92f}, {0.92f, 0.08f}}), lineP({{0.4f, 0.08f}, {0.92f, 0.08f}, {0.92f, 0.6f}}) });
    case 0x2198: return box(0.9f, 0.9f, { lineP({{0.08f, 0.08f}, {0.92f, 0.92f}}), lineP({{0.92f, 0.4f}, {0.92f, 0.92f}, {0.4f, 0.92f}}) });
    // ↳ ↵ ⇄
    case 0x21B3: return box(1.0f, 0.9f, { lineP({{0.12f, 0.04f}, {0.12f, 0.68f}, {0.94f, 0.68f}}),
                                          lineP({{0.66f, 0.4f}, {0.95f, 0.68f}, {0.66f, 0.96f}}) });
    case 0x21B5: return box(1.0f, 0.9f, { lineP({{0.9f, 0.04f}, {0.9f, 0.68f}, {0.08f, 0.68f}}),
                                          lineP({{0.36f, 0.4f}, {0.06f, 0.68f}, {0.36f, 0.96f}}) });
    case 0x21C4: return box(1.1f, 0.95f, { lineP({{0.04f, 0.28f}, {0.94f, 0.28f}}), lineP({{0.68f, 0.06f}, {0.95f, 0.28f}, {0.68f, 0.5f}}),
                                           lineP({{0.96f, 0.72f}, {0.06f, 0.72f}}), lineP({{0.32f, 0.5f}, {0.05f, 0.72f}, {0.32f, 0.94f}}) });
    // fleches circulaires : ↺ ⟲ (sens inverse des aiguilles), ↻ ⟳ (sens des aiguilles)
    case 0x21BA: case 0x27F2:
        return box(0.95f, 0.95f, { arcP({0.5f, 0.53f}, 0.4f, 120.f, 400.f), arcHead({0.5f, 0.53f}, 0.4f, 400.f, true, 0.34f) });
    case 0x21BB: case 0x27F3:
        return box(0.95f, 0.95f, { arcP({0.5f, 0.53f}, 0.4f, -220.f, 60.f), arcHead({0.5f, 0.53f}, 0.4f, -220.f, false, 0.34f) });
    // ⇧ ⇩ (fleches creuses)
    case 0x21E9: return box(0.72f, 0.98f, { loopP({{0.3f, 0.03f}, {0.7f, 0.03f}, {0.7f, 0.5f}, {0.95f, 0.5f}, {0.5f, 0.97f}, {0.05f, 0.5f}, {0.3f, 0.5f}}, 0.8f) });
    case 0x21E7: return box(0.72f, 0.98f, { loopP({{0.3f, 0.97f}, {0.7f, 0.97f}, {0.7f, 0.5f}, {0.95f, 0.5f}, {0.5f, 0.03f}, {0.05f, 0.5f}, {0.3f, 0.5f}}, 0.8f) });
    // ⚠ ✎
    case 0x26A0: return box(1.1f, 0.98f, { loopP({{0.5f, 0.04f}, {0.97f, 0.94f}, {0.03f, 0.94f}}, 0.85f),
                                           lineP({{0.5f, 0.38f}, {0.5f, 0.62f}}), discP({0.5f, 0.79f}, 0.09f) });
    case 0x270E: return box(0.95f, 0.95f, { lineP({{0.34f, 0.66f}, {0.86f, 0.14f}}, 1.9f),
                                            fillP({{0.06f, 0.94f}, {0.14f, 0.62f}, {0.38f, 0.86f}}) });
    // − ≤ ≥ ≠ ≡ ⋯ ⌫
    case 0x2212: return box(0.7f, 0.25f, { lineP({{0.06f, 0.5f}, {0.94f, 0.5f}}, 0.9f) });
    case 0x2264: return box(0.7f, 0.9f, { lineP({{0.92f, 0.06f}, {0.1f, 0.4f}, {0.92f, 0.74f}}), lineP({{0.1f, 0.95f}, {0.92f, 0.95f}}) });
    case 0x2265: return box(0.7f, 0.9f, { lineP({{0.08f, 0.06f}, {0.9f, 0.4f}, {0.08f, 0.74f}}), lineP({{0.08f, 0.95f}, {0.9f, 0.95f}}) });
    case 0x2260: return box(0.72f, 0.8f, { lineP({{0.06f, 0.32f}, {0.94f, 0.32f}}), lineP({{0.06f, 0.68f}, {0.94f, 0.68f}}),
                                           lineP({{0.7f, 0.04f}, {0.3f, 0.96f}}) });
    case 0x2261: return box(0.72f, 0.8f, { lineP({{0.06f, 0.14f}, {0.94f, 0.14f}}), lineP({{0.06f, 0.5f}, {0.94f, 0.5f}}),
                                           lineP({{0.06f, 0.86f}, {0.94f, 0.86f}}) });
    case 0x22EF: return box(1.15f, 0.3f, { discP({0.14f, 0.5f}, 0.45f), discP({0.5f, 0.5f}, 0.45f), discP({0.86f, 0.5f}, 0.45f) });
    case 0x232B: return box(1.25f, 0.88f, { loopP({{0.3f, 0.06f}, {0.97f, 0.06f}, {0.97f, 0.94f}, {0.3f, 0.94f}, {0.03f, 0.5f}}, 0.8f),
                                            lineP({{0.5f, 0.3f}, {0.78f, 0.7f}}), lineP({{0.78f, 0.3f}, {0.5f, 0.7f}}) });
    // 1.11.13 : les etats du build de l'IHM - ◌ non genere (un cercle en pointilles),
    // ⚙ generation en cours (une roue dentee), ◔ compilation en cours (un quart plein),
    // ⛓ dependance invalide (deux maillons), ⏱ obsolete (un chronometre)
    case 0x25CC: {
        s.w = 0.8f; s.h = 0.8f;
        for (int k = 0; k < 8; ++k) {
            const float a = static_cast<float>(k) * kPi / 4.f;
            s.prims.push_back(discP({0.5f + 0.42f * std::cos(a), 0.5f + 0.42f * std::sin(a)}, 0.085f));
        }
        return true;
    }
    case 0x2699: {
        s.w = 0.95f; s.h = 0.95f;
        s.prims.push_back(ringP({0.5f, 0.5f}, 0.3f, 1.25f));
        for (int k = 0; k < 8; ++k) {
            const float a = static_cast<float>(k) * kPi / 4.f;
            s.prims.push_back(lineP({{0.5f + 0.3f * std::cos(a), 0.5f + 0.3f * std::sin(a)}, {0.5f + 0.47f * std::cos(a), 0.5f + 0.47f * std::sin(a)}}, 1.3f));
        }
        return true;
    }
    case 0x25D4: return box(0.85f, 0.85f, { ringP({0.5f, 0.5f}, 0.5f), fillP({{0.5f, 0.5f}, {0.5f, 0.02f}, {0.7f, 0.06f}, {0.86f, 0.16f}, {0.95f, 0.32f}, {0.98f, 0.5f}}) });
    case 0x26D3: return box(1.05f, 0.95f, { loopP({{0.06f, 0.62f}, {0.32f, 0.36f}, {0.5f, 0.54f}, {0.24f, 0.8f}}, 0.95f),
                                            loopP({{0.5f, 0.46f}, {0.76f, 0.2f}, {0.94f, 0.38f}, {0.68f, 0.64f}}, 0.95f),
                                            lineP({{0.37f, 0.63f}, {0.63f, 0.37f}}, 0.95f) });
    case 0x23F1: return box(0.9f, 1.0f, { ringP({0.5f, 0.56f}, 0.42f, 1.f), lineP({{0.5f, 0.56f}, {0.5f, 0.32f}}), lineP({{0.5f, 0.56f}, {0.68f, 0.66f}}),
                                          lineP({{0.4f, 0.04f}, {0.6f, 0.04f}}), lineP({{0.5f, 0.04f}, {0.5f, 0.14f}}) });
    // espaces fines, puis largeur nulle
    case 0x2009: case 0x202F: s.space = 0.5f; return true;
    case 0x200A:              s.space = 0.25f; return true;
    case 0x200B: case 0x200C: case 0x200D: case 0x2060: case 0xFEFF: case 0x0336:
        s.space = 0.f; return true;
    default:
        return false;
    }
}

float segmentDistance(Pt p, Pt a, Pt b) {
    const float vx = b.x - a.x, vy = b.y - a.y;
    const float len2 = vx * vx + vy * vy;
    float t = len2 > 0.f ? ((p.x - a.x) * vx + (p.y - a.y) * vy) / len2 : 0.f;
    t = std::clamp(t, 0.f, 1.f);
    const float dx = p.x - (a.x + t * vx), dy = p.y - (a.y + t * vy);
    return std::sqrt(dx * dx + dy * dy);
}

bool insidePolygon(Pt p, const std::vector<Pt>& poly) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Pt& a = poly[i];
        const Pt& b = poly[j];
        if ((a.y > p.y) != (b.y > p.y)
            && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            in = !in;
    }
    return in;
}

} // namespace

struct FontAtlas::Face {
    std::vector<std::uint8_t> data;
    stbtt_fontinfo            info{};
};

FontAtlas::FontAtlas() = default;
FontAtlas::~FontAtlas() = default;
FontAtlas::FontAtlas(FontAtlas&&) noexcept = default;
FontAtlas& FontAtlas::operator=(FontAtlas&&) noexcept = default;

bool FontAtlas::load(const std::string& path, float pixelHeight) {
    ready_ = false;
    glyphs_.clear();
    pixels_.clear();

    if (pixelHeight < 4.f || pixelHeight > 256.f) return false;

    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    auto face = std::make_unique<Face>();
    face->data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (face->data.size() < 64) return false;

    const int offset = stbtt_GetFontOffsetForIndex(face->data.data(), 0);
    if (offset < 0) return false;
    if (!stbtt_InitFont(&face->info, face->data.data(), offset)) return false;

    face_ = std::move(face);
    path_ = path;
    pixelHeight_ = pixelHeight;
    scale_ = stbtt_ScaleForPixelHeight(&face_->info, pixelHeight);

    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&face_->info, &ascent, &descent, &gap);
    ascent_ = static_cast<float>(ascent) * scale_;
    descent_ = static_cast<float>(descent) * scale_;

    // The line box, rounded UP. A fractional row height accumulates down a
    // table until the last row is half a line out of place, and nothing in the
    // layout code is prepared for that.
    lineHeight_ = std::ceil(ascent_ - descent_ + static_cast<float>(gap) * scale_);

    width_ = kAtlasSide;
    height_ = kAtlasSide;
    pixels_.assign(static_cast<std::size_t>(width_) * height_, 0);
    penX_ = kPadding;
    penY_ = kPadding;
    rowHeight_ = 0;
    dirty_ = true;
    ready_ = true;
    return true;
}

Glyph FontAtlas::rasterise(char32_t code) {
    Glyph g;
    if (!ready_) return g;

    const int index = stbtt_FindGlyphIndex(&face_->info, static_cast<int>(code));
    if (index == 0 && code != U'\uFFFD') return g;   // the face has no such glyph

    int advance = 0, bearing = 0;
    stbtt_GetGlyphHMetrics(&face_->info, index, &advance, &bearing);
    g.advance = static_cast<float>(advance) * scale_;
    g.bearingX = static_cast<float>(bearing) * scale_;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&face_->info, index, scale_, scale_, &x0, &y0, &x1, &y1);
    const int w = x1 - x0;
    const int h = y1 - y0;

    // A space has metrics and no pixels. Returning early keeps it out of the
    // atlas, where a zero-sized rectangle would confuse the upload.
    if (w <= 0 || h <= 0) {
        g.loaded = true;
        return g;
    }

    if (penX_ + w + kPadding >= width_) {
        penX_ = kPadding;
        penY_ = static_cast<std::uint16_t>(penY_ + rowHeight_ + kPadding);
        rowHeight_ = 0;
    }
    if (penY_ + h + kPadding >= height_) {
        // The atlas is full. Said through loaded=false rather than by growing
        // mid-frame: a reallocation would invalidate the texture the renderer
        // is drawing from.
        return g;
    }

    stbtt_MakeGlyphBitmap(&face_->info,
                          pixels_.data() + static_cast<std::size_t>(penY_) * width_ + penX_,
                          w, h, width_, scale_, scale_, index);

    g.x = penX_;
    g.y = penY_;
    g.w = static_cast<std::uint16_t>(w);
    g.h = static_cast<std::uint16_t>(h);
    g.bearingX = static_cast<float>(x0);
    g.bearingY = static_cast<float>(y0);   // negative: above the baseline
    g.loaded = true;

    penX_ = static_cast<std::uint16_t>(penX_ + w + kPadding);
    rowHeight_ = std::max(rowHeight_, static_cast<std::uint16_t>(h));
    dirty_ = true;
    return g;
}

// 1.11.2 (T2, decision 155) : un symbole de AtlasSymbols.hpp, trace au trait.
Glyph FontAtlas::synthesise(char32_t code) {
    Glyph g;
    if (!ready_) return g;
    SymbolShape s;
    if (!symbolShape(code, s)) return g;

    if (s.space >= 0.f) {
        const float space = s.space > 0.f ? glyph(U' ').advance : 0.f;   // copie avant tout ajout
        g.advance = space * s.space;
        g.loaded = true;
        return g;
    }

    // La hauteur de capitale de la police : un symbole s'accorde a la lettre qu'il
    // accompagne (« ▶ 28 s », « ▸ Démarrer »), pas a l'em, qui varie d'une police a l'autre.
    float cap = 0.7f * ascent_;
    int hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;
    if (stbtt_GetCodepointBox(&face_->info, 'H', &hx0, &hy0, &hx1, &hy1) && hy1 > 0)
        cap = static_cast<float>(hy1) * scale_;

    const float bw = s.w * cap;
    const float bh = s.h * cap;
    const float unit = std::max(1.1f, 0.13f * cap);   // l'epaisseur d'un trait de base, en pixels
    float widest = 1.f;
    for (const Prim& p : s.prims) widest = std::max(widest, p.width);
    const float margin = std::ceil(unit * widest * 0.5f) + 1.f;
    const float side = std::max(1.f, std::round(0.14f * cap));       // le blanc de chaque cote
    const float boxLeft = side;
    const float boxTop = -(0.5f * cap + 0.5f * bh);                  // centre sur le milieu des capitales

    const int ix0 = static_cast<int>(std::floor(boxLeft - margin));
    const int iy0 = static_cast<int>(std::floor(boxTop - margin));
    const int ix1 = static_cast<int>(std::ceil(boxLeft + bw + margin));
    const int iy1 = static_cast<int>(std::ceil(boxTop + bh + margin));
    const int w = ix1 - ix0;
    const int h = iy1 - iy0;
    g.advance = std::round(bw + 2.f * side);
    if (w <= 0 || h <= 0) { g.loaded = true; return g; }

    if (penX_ + w + kPadding >= width_) {
        penX_ = kPadding;
        penY_ = static_cast<std::uint16_t>(penY_ + rowHeight_ + kPadding);
        rowHeight_ = 0;
    }
    if (penY_ + h + kPadding >= height_) return Glyph{};   // l'atlas est plein : comme rasterise

    // Les traits, en pixels du petit bitmap.
    const float ox = boxLeft - static_cast<float>(ix0);
    const float oy = boxTop - static_cast<float>(iy0);
    const float shortSide = std::min(bw, bh);
    auto toPx = [&](Pt p) { return Pt{ox + p.x * bw, oy + p.y * bh}; };
    struct Ready { Prim::Kind kind; std::vector<Pt> pts; float half; Pt c; float r; float a0, a1; };
    std::vector<Ready> ready;
    for (const Prim& p : s.prims) {
        Ready q{p.kind, {}, 0.5f * unit * p.width, toPx(p.c), 0.f, p.a0, p.a1};
        for (const Pt& v : p.pts) q.pts.push_back(toPx(v));
        if (p.kind == Prim::Disc) q.r = std::max(0.75f, p.r * shortSide);
        else                      q.r = p.r * shortSide - q.half;   // Ring, Arc : le bord exterieur dans la boite
        ready.push_back(std::move(q));
    }
    auto inside = [&](Pt p) {
        for (const Ready& q : ready) {
            switch (q.kind) {
            case Prim::Fill:
                if (q.pts.size() >= 3 && insidePolygon(p, q.pts)) return true;
                break;
            case Prim::Stroke:
            case Prim::Closed: {
                const std::size_t n = q.pts.size();
                for (std::size_t i = 0; i + 1 < n; ++i)
                    if (segmentDistance(p, q.pts[i], q.pts[i + 1]) <= q.half) return true;
                if (q.kind == Prim::Closed && n >= 3 && segmentDistance(p, q.pts[n - 1], q.pts[0]) <= q.half) return true;
                break;
            }
            case Prim::Disc: {
                const float dx = p.x - q.c.x, dy = p.y - q.c.y;
                if (dx * dx + dy * dy <= q.r * q.r) return true;
                break;
            }
            case Prim::Ring:
            case Prim::Arc: {
                const float dx = p.x - q.c.x, dy = p.y - q.c.y;
                if (std::fabs(std::sqrt(dx * dx + dy * dy) - q.r) > q.half) break;
                if (q.kind == Prim::Ring) return true;
                float a = std::atan2(-dy, dx) * 180.f / kPi;          // y vers le haut, en degres
                float t = std::fmod(a - q.a0, 360.f);
                if (t < 0.f) t += 360.f;
                if (t <= q.a1 - q.a0) return true;
                break;
            }
            }
        }
        return false;
    };

    // Quatre sur quatre echantillons par pixel : des bords lisses, comme la police.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx)
                    if (inside(Pt{static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / 4.f,
                                  static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / 4.f}))
                        ++hits;
            pixels_[static_cast<std::size_t>(penY_ + y) * width_ + penX_ + static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>(hits * 255 / 16);
        }
    }

    g.x = penX_;
    g.y = penY_;
    g.w = static_cast<std::uint16_t>(w);
    g.h = static_cast<std::uint16_t>(h);
    g.bearingX = static_cast<float>(ix0);
    g.bearingY = static_cast<float>(iy0);   // negatif : au-dessus de la ligne de base
    g.loaded = true;

    penX_ = static_cast<std::uint16_t>(penX_ + w + kPadding);
    rowHeight_ = std::max(rowHeight_, static_cast<std::uint16_t>(h));
    dirty_ = true;
    return g;
}

bool FontAtlas::drawsSymbol(char32_t code) noexcept {
    SymbolShape s;
    return atlas::drawnByAtlas(code) && symbolShape(code, s);
}

const Glyph& FontAtlas::glyph(char32_t code) {
    if (const auto at = glyphs_.find(code); at != glyphs_.end()) return at->second;

    // Lot API 8 : une tabulation (le code des exports Control Expert en est
    // plein) avance de quatre espaces, au lieu de dessiner le carre des
    // caracteres absents. La mesure et le dessin passent tous deux par ici.
    if (code == U'\t') {
        Glyph tab = glyph(U' ');
        tab.advance *= 4.f;
        return glyphs_.emplace(code, tab).first->second;
    }

    // 1.11.2 (decision 155) : un symbole de AtlasSymbols.hpp est dessine par
    // l'atlas, jamais demande a la police (Segoe UI n'a ni \u25B6 ni \u2605 : on voyait
    // le losange). Si l'atlas est plein, on retombe sur la police.
    if (atlas::drawnByAtlas(code)) {
        const Glyph drawn = synthesise(code);
        if (drawn.loaded) return glyphs_.emplace(code, drawn).first->second;
    }

    Glyph g = rasterise(code);
    if (!g.loaded && code != U'\uFFFD') {
        // A code point the face does not carry draws the replacement box. A
        // missing glyph that draws nothing is a missing glyph nobody reports.
        g = rasterise(U'\uFFFD');
        if (!g.loaded) g = rasterise(U'?');
    }
    return glyphs_.emplace(code, g).first->second;
}

// ---------------------------------------------------------------------------
char32_t FontAtlas::decode(std::string_view utf8, std::size_t& at) {
    if (at >= utf8.size()) return 0;
    const auto b0 = static_cast<unsigned char>(utf8[at]);

    auto continuation = [&](std::size_t k) {
        return at + k < utf8.size()
               && (static_cast<unsigned char>(utf8[at + k]) & 0xC0) == 0x80;
    };

    if (b0 < 0x80) { ++at; return b0; }

    if ((b0 & 0xE0) == 0xC0 && continuation(1)) {
        const char32_t c = ((b0 & 0x1Fu) << 6)
                         | (static_cast<unsigned char>(utf8[at + 1]) & 0x3Fu);
        at += 2;
        return c;
    }
    if ((b0 & 0xF0) == 0xE0 && continuation(1) && continuation(2)) {
        const char32_t c = ((b0 & 0x0Fu) << 12)
                         | ((static_cast<unsigned char>(utf8[at + 1]) & 0x3Fu) << 6)
                         | (static_cast<unsigned char>(utf8[at + 2]) & 0x3Fu);
        at += 3;
        return c;
    }
    if ((b0 & 0xF8) == 0xF0 && continuation(1) && continuation(2) && continuation(3)) {
        const char32_t c = ((b0 & 0x07u) << 18)
                         | ((static_cast<unsigned char>(utf8[at + 1]) & 0x3Fu) << 12)
                         | ((static_cast<unsigned char>(utf8[at + 2]) & 0x3Fu) << 6)
                         | (static_cast<unsigned char>(utf8[at + 3]) & 0x3Fu);
        at += 4;
        return c;
    }

    // A byte that starts nothing valid. One step forward and a replacement
    // character: a corrupt string must cost a box, never a hang.
    ++at;
    return U'\uFFFD';
}

float FontAtlas::measure(std::string_view utf8) {
    if (!ready_) return 0.f;
    float total = 0.f;
    std::size_t at = 0;
    while (at < utf8.size()) {
        const char32_t code = decode(utf8, at);
        if (code == 0) break;
        total += glyph(code).advance;
    }
    return total;
}

std::size_t FontAtlas::fitBytes(std::string_view utf8, float maxWidth) {
    if (!ready_ || maxWidth <= 0.f) return 0;
    float total = 0.f;
    std::size_t at = 0;
    while (at < utf8.size()) {
        const std::size_t start = at;
        const char32_t code = decode(utf8, at);
        if (code == 0) break;
        total += glyph(code).advance;
        // The cut is BEFORE the glyph that overflows, and it lands on a
        // sequence boundary: cutting mid-sequence would draw a broken glyph.
        if (total > maxWidth) return start;
    }
    return utf8.size();
}

// ---------------------------------------------------------------------------
std::vector<std::string> defaultUiFontCandidates() {
    return {
        // Bundled first: a project that ships its own face looks the same on
        // every machine, which matters when two people compare screenshots.
        "resources/fonts/Inter-Regular.ttf",
        "resources/fonts/DejaVuSans.ttf",
        // Windows
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        // Linux
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        // macOS
        "/System/Library/Fonts/SFNS.ttf",
        "/Library/Fonts/Arial.ttf",
    };
}

std::vector<std::string> defaultMonoFontCandidates() {
    return {
        "resources/fonts/JetBrainsMono-Regular.ttf",
        "resources/fonts/DejaVuSansMono.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/cour.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/System/Library/Fonts/Menlo.ttc",
    };
}

} // namespace gfx
