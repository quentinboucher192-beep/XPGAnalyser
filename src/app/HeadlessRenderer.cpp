// =============================================================================
//  app/HeadlessRenderer.cpp - 1.50.1 : voir HeadlessRenderer.hpp
// =============================================================================
#include "HeadlessRenderer.hpp"

#include <algorithm>
#include <cstdlib>

namespace app {

bool headlessWanted(const std::string& script) {
    if (script.empty()) return false;
    const char* v = std::getenv("XPG_SANS_ECRAN");
    return v && std::string(v) == "1";
}

gfx::FontAtlas* HeadlessRenderer::faceFor(gfx::FontId f) const {
    const int px = std::max(1, static_cast<int>(f.v ? f.v : 16));
    const int key = px * 4 + f.w;
    if (auto it = faces_.find(key); it != faces_.end()) return it->second.ready() ? &it->second : nullptr;
    gfx::FontAtlas atlas;
    for (const auto& path : gfx::interfaceFontCandidates(f.w))
        if (atlas.load(path, static_cast<float>(px))) break;
    auto& kept = faces_.emplace(key, std::move(atlas)).first->second;
    return kept.ready() ? &kept : nullptr;
}

gfx::TextMetrics HeadlessRenderer::measure(std::string_view s, gfx::FontId f) const {
    gfx::FontAtlas* face = faceFor(f);
    const float h = f.v ? static_cast<float>(f.v) : 16.f;
    if (!face) return {static_cast<float>(s.size()) * h * 0.5f, h, h * 0.8f, h * 0.2f};
    gfx::TextMetrics m;
    m.width = face->measure(s);
    m.height = face->lineHeight();
    m.ascent = face->ascent();
    m.descent = face->descent();
    return m;
}

float HeadlessRenderer::lineHeight(gfx::FontId f) const {
    gfx::FontAtlas* face = faceFor(f);
    return face ? face->lineHeight() : (f.v ? static_cast<float>(f.v) : 16.f);
}

std::size_t HeadlessRenderer::fitCharacters(std::string_view s, gfx::FontId f, float maxWidth) const {
    if (maxWidth <= 0.f) return 0;
    gfx::FontAtlas* face = faceFor(f);
    return face ? face->fitBytes(s, maxWidth) : std::min(s.size(), static_cast<std::size_t>(maxWidth / 8.f));
}

} // namespace app
