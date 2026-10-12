// =============================================================================
//  app/hmi/HmiResourceFilter.hpp - 1.12.12 (lot H) : LE FILTRE DES DIALOGUES
//  « IMPORTER UNE RESSOURCE » ET « REMPLACER »
// -----------------------------------------------------------------------------
//  Fait de la liste des extensions acceptees (hmi::acceptedExtensions), rangees
//  par genre (Images, Sons, Videos, Polices) : un format de plus y entre de
//  lui-meme. Avant, la liste etait recopiee a la main - les videos AVI, MOV et
//  WMV, acceptees en 1.12.12, n'y etaient pas.
//  "Ressources IHM|*.png;*.jpg;...|Images|*.png;...|Sons|*.wav;*.mp3|Videos|...|Polices|*.ttf;*.otf"
// =============================================================================
#pragma once

#include "../../hmi/HmiMedia.hpp"

#include <string>
#include <string_view>

namespace app {

[[nodiscard]] inline std::string hmiResourceFilter() {
    std::string all, images, sounds, videos, fonts;
    const auto append = [](std::string& list, const std::string& pattern) {
        if (!list.empty()) list += ';';
        list += pattern;
    };
    std::string_view rest = hmi::acceptedExtensions();
    while (!rest.empty()) {
        const auto comma = rest.find(',');
        std::string_view ext = rest.substr(0, comma);
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        while (!ext.empty() && ext.front() == ' ') ext.remove_prefix(1);
        while (!ext.empty() && ext.back() == ' ') ext.remove_suffix(1);
        if (ext.empty()) continue;
        std::string pattern = "*.";
        for (const char c : ext) pattern += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        append(all, pattern);
        switch (hmi::kindOfFormat(hmi::formatFromExtension(pattern))) {
            case hmi::MediaKind::Image: append(images, pattern); break;
            case hmi::MediaKind::Sound: append(sounds, pattern); break;
            case hmi::MediaKind::Video: append(videos, pattern); break;
            case hmi::MediaKind::Font:  append(fonts, pattern); break;
            default: break;
        }
    }
    return "Ressources IHM|" + all + "|Images|" + images + "|Sons|" + sounds + "|Vid\xC3\xA9os|" + videos + "|Polices|" + fonts;
}

} // namespace app
