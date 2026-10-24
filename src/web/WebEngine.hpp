// =============================================================================
//  web/WebEngine.hpp - 1.12.31 : l'interface C du moteur wasm (voir WebEngine.cpp)
// =============================================================================
#pragma once

// Chaque fonction de l'interface est exportee du module wasm sous son nom.
#ifdef __wasm__
#define XPG_WEB_EXPORT(name) __attribute__((export_name(#name)))
#else
#define XPG_WEB_EXPORT(name)
#endif

#ifdef XPG_WEB

#include "../hmi/HmiHistory.hpp"
#include "../hmi/HmiModel.hpp"
#include "../hmi/HmiRuntime.hpp"
#include "WebCanvas.hpp"
#include "WebRenderer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace app::web {

// L'etat du moteur, un seul par page.
struct Engine {
    hmi::Project                  project;
    hmi::History                  history;
    std::unique_ptr<hmi::Runtime> runtime;
    WebCanvas                     canvas;
    WebRenderer                   renderer;
    std::vector<std::uint8_t>     font;        // le TTF embarque (mesures du texte)
    std::string                   lastError;
    std::vector<std::string>      journal;     // les lignes du journal, depuis le debut
    std::string                   scratch;     // le tampon de la derniere chaine rendue
};

Engine& engine();

} // namespace app::web

#endif // XPG_WEB
