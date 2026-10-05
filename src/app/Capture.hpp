// =============================================================================
//  app/Capture.hpp — l'image de la fenetre, en PNG
// -----------------------------------------------------------------------------
//  Ce qui vient d'etre dessine, lu dans le rendu (IRenderer::readPixels) et
//  ecrit tel quel : pas de fenetre a reproduire, pas d'image refaite a cote.
//  F12 dans l'application, et la commande "capture" des scripts, passent ici.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "../platform/Renderer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace app {

// Lit l'image courante du rendu et l'ecrit en PNG (dossiers crees au besoin).
[[nodiscard]] core::Status captureToPng(gfx::IRenderer& renderer, const std::string& path);

// Lot 8 : seulement ce rectangle de l'image (l'exemple anime d'un objet, pour le
// guide Word), en pixels de la fenetre.
[[nodiscard]] core::Status captureRegionToPng(gfx::IRenderer& renderer, const std::string& path, int x, int y, int w, int h);

// 1.10 (chantier O) : ce rectangle de l'image, en PNG en memoire (l'image de la
// fenetre graphique, que l'ecran exporte ensuite). Faux : rendu illisible.
[[nodiscard]] bool regionToPng(gfx::IRenderer& renderer, int x, int y, int w, int h, std::vector<std::uint8_t>& png);

// Le nom d'une capture prise a la main : captures/XPG-2026-09-21_14-05-33.png.
[[nodiscard]] std::string defaultCapturePath();

// Lot 13 : une image RGBA en JPEG (qualite 1 a 100), en memoire - les vignettes
// du dossier de l'IHM. Faux : l'encodeur a echoue.
[[nodiscard]] bool encodeJpeg(const std::vector<std::uint8_t>& rgba, int w, int h, int quality, std::vector<std::uint8_t>& out);

} // namespace app
