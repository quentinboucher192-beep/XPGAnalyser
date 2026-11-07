// =============================================================================
//  xls/ZipLimits.hpp - 1.32.0 : les bornes d'un zip recu (une "bombe" ne s'ouvre pas)
// -----------------------------------------------------------------------------
//  Un zip annonce dans son repertoire central la taille de chaque entree une
//  fois decompressee ; miniz alloue cette taille avant de decompresser. Un
//  fichier de quelques Ko peut donc demander des Go. Chaque lecteur de zip
//  d'un fichier venu d'ailleurs (archive de projet, paquet de vues,
//  bibliotheque, classeur) passe par ces bornes AVANT d'extraire :
//
//    - 20 000 entrees au plus ;
//    - 256 Mio par entree, une fois decompressee ;
//    - 1 Gio pour toutes les entrees lues.
//
//  Sans dependance (pas meme miniz) : chacun donne les tailles lues dans son
//  propre repertoire (m_uncomp_size).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace xls::ziplimits {

inline constexpr std::size_t   kMaxEntries = 20000;
inline constexpr std::uint64_t kMaxEntryBytes = 256ull * 1024 * 1024;
inline constexpr std::uint64_t kMaxTotalBytes = 1024ull * 1024 * 1024;

// Le budget d'une lecture : le nombre d'entrees, puis chaque entree avant de l'extraire. Faux : `why` dit pourquoi
// (en francais), et rien ne doit etre extrait.
class Budget {
public:
    [[nodiscard]] bool entries(std::size_t count, std::string* why = nullptr) const {
        if (count <= kMaxEntries) return true;
        if (why) *why = "le zip compte " + std::to_string(count) + " entr\xC3\xA9" "es (" + std::to_string(kMaxEntries) + " au plus) : il ne s'ouvre pas";
        return false;
    }
    [[nodiscard]] bool take(std::uint64_t uncompressed, std::string_view name, std::string* why = nullptr) {
        if (uncompressed > kMaxEntryBytes) {
            if (why) *why = std::string(name) + " annonce " + std::to_string(uncompressed / (1024 * 1024)) + " Mo une fois d\xC3\xA9" "compress\xC3\xA9 (256 Mo au plus) : le zip ne s'ouvre pas";
            return false;
        }
        if (uncompressed > kMaxTotalBytes - total_) {
            if (why) *why = "le zip annonce plus de 1 Go une fois d\xC3\xA9" "compress\xC3\xA9 : il ne s'ouvre pas";
            return false;
        }
        total_ += uncompressed;
        return true;
    }
    [[nodiscard]] std::uint64_t total() const noexcept { return total_; }

private:
    std::uint64_t total_{0};
};

} // namespace xls::ziplimits
