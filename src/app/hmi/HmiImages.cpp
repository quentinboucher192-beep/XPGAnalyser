#include "HmiImages.hpp"

#include <cmath>

#include "../../hmi/HmiAssets.hpp"
#include "../../hmi/HmiMedia.hpp"
#include "../../ui/Widget.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

namespace app {

std::function<double(std::string_view, std::string_view, double)> hmiTextMeasure(const hmi::Project& project) {
    return [&project](std::string_view text, std::string_view font, double size) -> double {
        if (!font.empty() && font != "Sans")
            if (const auto* res = project.resourceByName(font); res && res->data && res->kind() == hmi::MediaKind::Font) {
                hmi::Rgba img;
                if (hmi::renderFontText(*res->data, text, static_cast<float>(size), img)) return img.width;
            }
        const auto px = static_cast<std::uint16_t>(std::clamp(size, 6.0, 200.0));
        return ui::measureWidth(text, gfx::FontId{px});
    };
}

namespace {
HmiImageCache* g_current = nullptr;   // lot 14 : le cache d'un ecran secondaire, le temps de son dessin
} // namespace

HmiImageCache& HmiImageCache::instance() {
    static HmiImageCache cache;
    return g_current ? *g_current : cache;
}

HmiImageCache::Scope::Scope(HmiImageCache& cache) noexcept : previous_(g_current) { g_current = &cache; }
HmiImageCache::Scope::~Scope() { g_current = previous_; }

void HmiImageCache::adopt(gfx::IRenderer& r) {
    // Un autre renderer (les tests en ont plusieurs) : ses textures ne sont pas
    // les notres, on repart de zero sans rien lui rendre.
    if (renderer_ == &r) return;
    renderer_ = &r;
    images_.clear();
    texts_.clear();
    svgs_.clear();
    gifs_.clear();
}

void HmiImageCache::purge(gfx::IRenderer& r) {
    if (++tick_ % 128 != 0) return;
    for (auto it = images_.begin(); it != images_.end();) {
        if (it->second.blob.expired()) {
            if (it->second.image.tex.v) r.releaseImage(it->second.image.tex);
            it = images_.erase(it);
        } else {
            ++it;
        }
    }
    // Lot 10 : les SVG a leur taille - leur contenu parti, ou trop de tailles gardees.
    for (auto it = svgs_.begin(); it != svgs_.end();) {
        if (it->second.blob.expired()) {
            if (it->second.image.tex.v) r.releaseImage(it->second.image.tex);
            it = svgs_.erase(it);
        } else {
            ++it;
        }
    }
    if (svgs_.size() > 192) {
        std::vector<std::pair<std::uint64_t, std::string>> byAge;
        for (const auto& [k, e] : svgs_) byAge.emplace_back(e.used, k);
        std::sort(byAge.begin(), byAge.end());
        for (std::size_t i = 0; i + 128 < byAge.size(); ++i) {
            auto it = svgs_.find(byAge[i].second);
            if (it->second.image.tex.v) r.releaseImage(it->second.image.tex);
            svgs_.erase(it);
        }
    }
    for (auto it = envelopes_.begin(); it != envelopes_.end();)
        it = it->second.blob.expired() ? envelopes_.erase(it) : std::next(it);
    // Lot 16 : les GIF dont le contenu est parti.
    for (auto it = gifs_.begin(); it != gifs_.end();) {
        if (it->second.blob.expired()) {
            for (const auto& f : it->second.gif.frames)
                if (f.tex.v) r.releaseImage(f.tex);
            it = gifs_.erase(it);
        } else {
            ++it;
        }
    }
    if (texts_.size() > 512) {
        std::vector<std::pair<std::uint64_t, std::string>> byAge;
        for (const auto& [k, e] : texts_) byAge.emplace_back(e.used, k);
        std::sort(byAge.begin(), byAge.end());
        for (std::size_t i = 0; i + 384 < byAge.size(); ++i) {
            auto it = texts_.find(byAge[i].second);
            if (it->second.image.tex.v) r.releaseImage(it->second.image.tex);
            texts_.erase(it);
        }
    }
}

HmiImageCache::Image HmiImageCache::resource(gfx::IRenderer& r, const hmi::Resource& res) {
    if (!res.data || res.kind() != hmi::MediaKind::Image) return {};
    adopt(r);
    purge(r);
    auto& e = images_[res.data.get()];
    e.used = tick_;
    if (e.image.valid() || e.failed) return e.image;
    e.blob = res.data;
    hmi::Rgba px;
    if (!hmi::decodeImage(*res.data, res.format, px, 1024)) { e.failed = true; return {}; }
    e.image.tex = r.createImage(px.pixels.data(), px.width, px.height);
    e.image.width = px.width;
    e.image.height = px.height;
    if (!e.image.tex.v) e.failed = true;    // un renderer sans images : pas la peine de redecoder
    return e.image;
}

// ---- lot 16 : les GIF animes --------------------------------------------------------
const HmiImageCache::Image& HmiImageCache::Gif::looping(double seconds) const {
    static const Image none{};
    if (frames.empty()) return none;
    if (timing.totalMs <= 0 || frames.size() == 1) return frames.front();
    const double ms = std::fmod(std::max(0.0, seconds) * 1000.0, static_cast<double>(timing.totalMs));
    const std::size_t i = std::min(timing.frameAt(ms), frames.size() - 1);
    return frames[i];
}

const HmiImageCache::Gif* HmiImageCache::gif(gfx::IRenderer& r, const hmi::Resource& res) {
    if (!res.data || !hmi::isGif(*res.data)) return nullptr;
    adopt(r);
    purge(r);
    auto& e = gifs_[res.data.get()];
    if (e.gif.valid()) return &e.gif;
    if (e.failed) return nullptr;
    e.blob = res.data;
    hmi::GifFrames frames;
    if (!hmi::decodeGif(*res.data, frames, nullptr, 600)) {
        e.failed = true;
        return nullptr;
    }
    e.gif.timing = frames.timing;
    for (const auto& f : frames.frames) {
        Image img;
        img.tex = r.createImage(f.pixels.data(), f.width, f.height);
        img.width = f.width;
        img.height = f.height;
        if (!img.tex.v) {
            e.failed = true;
            for (const auto& done : e.gif.frames)
                if (done.tex.v) r.releaseImage(done.tex);
            e.gif.frames.clear();
            return nullptr;
        }
        e.gif.frames.push_back(img);
    }
    return &e.gif;
}

HmiImageCache::Image HmiImageCache::svg(gfx::IRenderer& r, const hmi::Resource& res, int width, int height,
                                        const hmi::SvgRecolor& rc) {
    if (!res.data || width <= 0 || height <= 0 || !hmi::isSvgImage(*res.data, res.format)) return {};
    adopt(r);
    purge(r);
    width = std::min(width, 4096);
    height = std::min(height, 4096);
    std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(res.data.get())) + "|" + std::to_string(width) + "|"
                    + std::to_string(height);
    if (rc.active) key += "|" + std::to_string(rc.rgb) + "|" + rc.mode + "|" + std::to_string(rc.from);
    auto& e = svgs_[key];
    e.used = tick_;
    if (e.image.valid() || e.failed) return e.image;
    e.blob = res.data;
    hmi::Rgba px;
    if (!hmi::rasterizeSvg(*res.data, width, height, px, &rc)) { e.failed = true; return {}; }
    ++svgRasterized_;
    e.image.tex = r.createImage(px.pixels.data(), px.width, px.height);
    e.image.width = px.width;
    e.image.height = px.height;
    if (!e.image.tex.v) e.failed = true;
    return e.image;
}

HmiImageCache::Image HmiImageCache::fontText(gfx::IRenderer& r, const hmi::Resource& font, std::string_view text,
                                             float pixelHeight) {
    if (!font.data || font.kind() != hmi::MediaKind::Font || text.empty()) return {};
    adopt(r);
    purge(r);
    const int px = std::max(4, static_cast<int>(pixelHeight + 0.5f));
    std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(font.data.get())) + "|" + std::to_string(px) + "|";
    key += text;
    auto& e = texts_[key];
    e.used = tick_;
    if (e.image.valid() || e.failed) return e.image;
    e.blob = font.data;
    hmi::Rgba img;
    if (!hmi::renderFontText(*font.data, text, static_cast<float>(px), img)) { e.failed = true; return {}; }
    e.image.tex = r.createImage(img.pixels.data(), img.width, img.height);
    e.image.width = img.width;
    e.image.height = img.height;
    if (!e.image.tex.v) e.failed = true;
    return e.image;
}

const std::vector<float>& HmiImageCache::envelope(const hmi::Resource& res, int buckets) {
    auto& env = envelopes_[res.data.get()];
    if (env.minMax.size() != static_cast<std::size_t>(buckets) * 2 && res.data) {
        env.blob = res.data;
        if (!hmi::soundEnvelope(*res.data, res.format, buckets, env.minMax))
            env.minMax.assign(static_cast<std::size_t>(buckets) * 2, 0.f);
    }
    return env.minMax;
}

// ------------------------------------------------------------ fichiers externes ---
namespace {
std::string& folderRef() {
    static std::string folder;
    return folder;
}
struct ExternalEntry {
    std::string stamp;
    std::uint64_t bytes{0};
    std::shared_ptr<const hmi::ExternalData> data;
};
std::map<std::string, ExternalEntry>& externalCache() {
    static std::map<std::string, ExternalEntry> cache;
    return cache;
}
} // namespace

void setHmiProjectFolder(std::string folder) {
    if (folder != folderRef()) externalCache().clear();
    folderRef() = std::move(folder);
}
const std::string& hmiProjectFolder() { return folderRef(); }

std::shared_ptr<const hmi::ExternalData> hmiExternalData(const hmi::ExternalFile& f, std::size_t maxRows) {
    const auto st = hmi::externalState(f, folderRef());
    const std::string key = std::string(hmi::externalKindKey(f.kind)) + "|" + st.resolvedPath + "|" + f.part + "|"
                          + std::to_string(maxRows);
    auto& e = externalCache()[key];
    if (e.data && e.stamp == st.modified && e.bytes == st.bytes) return e.data;
    e.stamp = st.modified;
    e.bytes = st.bytes;
    e.data = std::make_shared<hmi::ExternalData>(hmi::readExternal(f.kind, st.resolvedPath, f.part, maxRows));
    return e.data;
}

} // namespace app
