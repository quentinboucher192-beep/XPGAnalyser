#include "Capture.hpp"
#include "Dossiers.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <vector>

// stb_image_write : domaine public, un seul fichier, dans third_party/. Son
// implementation n'est compilee qu'ici.
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wconversion"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_DEPRECATION
#include "../../third_party/stb_image_write.h"
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

namespace app {

bool encodeJpeg(const std::vector<std::uint8_t>& rgba, int w, int h, int quality, std::vector<std::uint8_t>& out) {
    out.clear();
    if (w <= 0 || h <= 0 || rgba.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4) return false;
    // Le JPEG n'a pas d'alpha : RGB seul.
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    for (std::size_t i = 0, j = 0; j < rgb.size(); i += 4, j += 3) {
        rgb[j] = rgba[i];
        rgb[j + 1] = rgba[i + 1];
        rgb[j + 2] = rgba[i + 2];
    }
    const auto sink = [](void* context, void* data, int size) {
        auto* v = static_cast<std::vector<std::uint8_t>*>(context);
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        v->insert(v->end(), bytes, bytes + size);
    };
    return stbi_write_jpg_to_func(sink, &out, w, h, 3, rgb.data(), std::clamp(quality, 1, 100)) != 0 && !out.empty();
}

core::Status captureToPng(gfx::IRenderer& renderer, const std::string& path) {
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    if (!renderer.readPixels(rgba, w, h) || w <= 0 || h <= 0)
        return core::fail(core::ErrorCode::NotImplemented, "ce rendu ne sait pas relire son image", path);
    std::error_code ec;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    // Opaque : une capture a moitie transparente se lit mal hors de l'appli.
    for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
    if (!stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4))
        return core::fail(core::ErrorCode::FileUnreadable, "ecriture PNG impossible", path);
    return core::ok();
}

core::Status captureRegionToPng(gfx::IRenderer& renderer, const std::string& path, int x, int y, int w, int h) {
    std::vector<std::uint8_t> rgba;
    int fw = 0, fh = 0;
    if (!renderer.readPixels(rgba, fw, fh) || fw <= 0 || fh <= 0)
        return core::fail(core::ErrorCode::NotImplemented, "ce rendu ne sait pas relire son image", path);
    x = std::max(0, x);
    y = std::max(0, y);
    w = std::min(w, fw - x);
    h = std::min(h, fh - y);
    if (w <= 0 || h <= 0) return core::fail(core::ErrorCode::InvalidArgument, "rectangle hors de l'image", path);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    for (int row = 0; row < h; ++row)
        std::copy_n(rgba.data() + (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(fw) + static_cast<std::size_t>(x)) * 4,
                    static_cast<std::size_t>(w) * 4, out.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4);
    std::error_code ec;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    for (std::size_t i = 3; i < out.size(); i += 4) out[i] = 255;
    if (!stbi_write_png(path.c_str(), w, h, 4, out.data(), w * 4))
        return core::fail(core::ErrorCode::FileUnreadable, "ecriture PNG impossible", path);
    return core::ok();
}

bool regionToPng(gfx::IRenderer& renderer, int x, int y, int w, int h, std::vector<std::uint8_t>& png) {
    png.clear();
    std::vector<std::uint8_t> rgba;
    int fw = 0, fh = 0;
    if (!renderer.readPixels(rgba, fw, fh) || fw <= 0 || fh <= 0) return false;
    x = std::max(0, x);
    y = std::max(0, y);
    w = std::min(w, fw - x);
    h = std::min(h, fh - y);
    if (w <= 0 || h <= 0) return false;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    for (int row = 0; row < h; ++row)
        std::copy_n(rgba.data() + (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(fw) + static_cast<std::size_t>(x)) * 4,
                    static_cast<std::size_t>(w) * 4, out.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4);
    for (std::size_t i = 3; i < out.size(); i += 4) out[i] = 255;
    auto sink = [](void* ctx, void* data, int size) {
        auto* v = static_cast<std::vector<std::uint8_t>*>(ctx);
        const auto* p = static_cast<const std::uint8_t*>(data);
        v->insert(v->end(), p, p + size);
    };
    return stbi_write_png_to_func(sink, &png, w, h, 4, out.data(), w * 4) != 0 && !png.empty();
}

std::string defaultCapturePath() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char name[64];
    std::snprintf(name, sizeof name, "XPG-%04d-%02d-%02d_%02d-%02d-%02d.png", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    // 1.8.0 : le dossier des captures de XPGAnalyser.ini (version installee), sinon captures\ du dossier de travail.
    const std::string regle = dossiers::actif(dossiers::Cle::Captures);
    return ((regle.empty() ? std::filesystem::current_path() / "captures" : dossiers::cheminDe(regle)) / name).string();
}

} // namespace app
