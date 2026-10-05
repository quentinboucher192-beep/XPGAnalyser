// =============================================================================
//  export/DocKit.hpp - 1.8.0 : les outils des documents de l'export lisible
// -----------------------------------------------------------------------------
//  Un zip (compresse quand miniz est la : un classeur de 8 600 lignes de code
//  pese 400 Ko au lieu de 4 Mo), un flux PDF compresse, le texte Windows-1252
//  des polices standard du PDF et leur chasse (Helvetica, Courier). Les memes
//  que hmi/HmiExport.cpp, ici parce que xpg_import ne voit pas xpg_hmi.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace exporter::doc {

using Bytes = std::vector<std::uint8_t>;

[[nodiscard]] std::string xmlEscape(std::string_view s);
[[nodiscard]] std::string columnName(std::size_t k);     // 0 -> "A", 26 -> "AA"

// Un zip (chemin, contenu) ; `compress` : deflate si possible, sinon stocke.
// Date fixe : le meme contenu donne le meme fichier.
[[nodiscard]] Bytes zip(const std::vector<std::pair<std::string, std::string>>& items, bool compress = true);
// Le format zlib (FlateDecode du PDF) ; vide : pas de compresseur (le flux reste en clair).
[[nodiscard]] std::string zlibCompress(std::string_view data);

[[nodiscard]] std::string toWinAnsi(std::string_view utf8);
// La largeur (points) d'un texte Windows-1252 en Helvetica (bold : Helvetica-Bold) ou en Courier.
[[nodiscard]] double helveticaWidth(std::string_view winAnsi, double size, bool bold);
[[nodiscard]] inline double courierWidth(std::size_t chars, double size) { return static_cast<double>(chars) * 0.6 * size; }
[[nodiscard]] std::string pdfLiteral(std::string_view winAnsi);    // "(...)" echappee
[[nodiscard]] std::string pdfNumber(double v);                       // "12.5", "0"

// 1 234 (espace insecable fine en UTF-8 : non ; une espace simple, lisible partout).
[[nodiscard]] std::string thousands(std::size_t n);

} // namespace exporter::doc
