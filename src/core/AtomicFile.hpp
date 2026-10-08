// =============================================================================
//  core/AtomicFile.hpp - 1.11.13 : ecrire un fichier d'un bloc
// -----------------------------------------------------------------------------
//  Le cache de build, les artefacts, les rémanences : jamais un fichier a
//  moitie ecrit a la place d'un bon.
//
//    1. tout est ecrit dans <cible>.tmp ;
//    2. le .tmp est relu et compare a ce qui devait etre ecrit ;
//    3. l'ancienne cible est copiee en <cible>.bak (facultatif) ;
//    4. le .tmp prend la place de la cible (un renommage) ;
//    5. seulement alors l'ancienne version cesse d'etre la cible.
//
//  Une coupure a l'etape 1 ou 2 laisse la cible intacte (et un .tmp que les
//  lecteurs ignorent) ; a l'etape 4, la cible ou le .bak est entier.
//  (HmiStore a la meme regle pour les fichiers du projet IHM.)
// =============================================================================
#pragma once

#include "Result.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace core {

struct AtomicWrite {
    bool keepBackup{true};    // l'ancienne cible en <cible>.bak
    bool verify{true};        // relire le .tmp avant de remplacer
};

[[nodiscard]] Status writeFileAtomic(const std::filesystem::path& target, std::string_view content,
                                     const AtomicWrite& how = {});
// Lire un fichier entier ; faux : absent ou illisible.
[[nodiscard]] bool readFileAll(const std::filesystem::path& file, std::string& out);

} // namespace core
