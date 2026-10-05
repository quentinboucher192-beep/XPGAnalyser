#pragma once

// ---------------------------------------------------------------------------
// Windows header hygiene. <windows.h> and the RPC headers it drags in define
// three macros that collide with ordinary C++ identifiers:
//   * `small`  (rpcndr.h) -> breaks Theme::Fonts and anything else with a
//                            member of that name;
//   * `min` / `max`       -> break std::min / std::max, used throughout the
//                            layout and rendering code.
// Defining NOMINMAX in the project settings handles the last two; this guard
// makes the headers self-sufficient regardless of build settings.
// ---------------------------------------------------------------------------
#ifdef small
#  undef small
#endif
#ifdef min
#  undef min
#endif
#ifdef max
#  undef max
#endif

#include <algorithm>
#include <cstdint>

namespace gfx {

struct Point { float x{}, y{}; };
struct Size  { float w{}, h{}; };

struct Rect {
    float x{}, y{}, w{}, h{};
    [[nodiscard]] float right()  const noexcept { return x + w; }
    [[nodiscard]] float bottom() const noexcept { return y + h; }
    [[nodiscard]] bool  contains(Point p) const noexcept {
        return p.x >= x && p.y >= y && p.x < right() && p.y < bottom();
    }
    [[nodiscard]] Rect inset(float dx, float dy) const noexcept {
        return {x + dx, y + dy, std::max(0.f, w - 2 * dx), std::max(0.f, h - 2 * dy)};
    }
    [[nodiscard]] Rect intersect(const Rect& o) const noexcept {
        const float l = std::max(x, o.x), t = std::max(y, o.y);
        const float r = std::min(right(), o.right()), b = std::min(bottom(), o.bottom());
        return {l, t, std::max(0.f, r - l), std::max(0.f, b - t)};
    }
    [[nodiscard]] bool empty() const noexcept { return w <= 0.f || h <= 0.f; }
};

struct Color {
    std::uint8_t r{}, g{}, b{}, a{255};
    static constexpr Color rgb(std::uint32_t hex) {
        return {static_cast<std::uint8_t>((hex >> 16) & 0xFF),
                static_cast<std::uint8_t>((hex >>  8) & 0xFF),
                static_cast<std::uint8_t>( hex        & 0xFF), 255};
    }
    [[nodiscard]] constexpr Color withAlpha(std::uint8_t na) const { return {r, g, b, na}; }
};

// Opaque resource handles. They live here rather than in Renderer.hpp so that
// Theme.hpp (and anything else that only needs to *name* a font) does not have
// to pull in the renderer interface.
struct FontId    { std::uint16_t v{0}; };
struct TextureId { std::uint32_t v{0}; };

// Padding / margins, CSS order.
struct Edges { float t{}, r{}, b{}, l{}; };

} // namespace gfx
