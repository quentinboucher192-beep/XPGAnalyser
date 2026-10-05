// =============================================================================
//  hmi/HmiQr.hpp - le code QR d'un texte (lot 9, objet "Code QR")
// -----------------------------------------------------------------------------
//  Un encodeur complet et sans dependance : mode octets (le texte en UTF-8),
//  les quatre niveaux de correction (L 7 %, M 15 %, Q 25 %, H 30 %), les
//  versions 1 a 10 (de 21 x 21 a 57 x 57 modules : jusqu'a 271 octets en L,
//  213 en M), les codes de Reed-Solomon, l'entrelacement des blocs et le choix
//  du masque par les penalites de la norme (ISO/IEC 18004).
//
//  Le resultat est une grille de modules (vrai : noir) ; l'objet la dessine
//  avec sa marge. Sans ecran : hmi_test compare des grilles a une reference.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

struct QrCode {
    int               version{0};    // 1..10 ; 0 : rien (texte trop long, niveau inconnu)
    int               size{0};       // 17 + 4 x version modules de cote
    char              level{'M'};
    int               mask{-1};
    std::vector<bool> modules;       // ligne par ligne
    std::string       error;
    [[nodiscard]] bool ok() const noexcept { return version > 0; }
    [[nodiscard]] bool at(int x, int y) const noexcept {
        return x >= 0 && y >= 0 && x < size && y < size && modules[static_cast<std::size_t>(y * size + x)];
    }
};

// `level` : 'L', 'M', 'Q' ou 'H'. `forceMask` (0..7) : pour les tests ; -1 :
// le meilleur masque. `minVersion` : la plus petite version essayee.
[[nodiscard]] QrCode encodeQr(std::string_view text, char level = 'M', int forceMask = -1, int minVersion = 1);
// Le nombre d'octets qu'un niveau loge au plus (en version 10).
[[nodiscard]] int qrCapacity(char level);

} // namespace hmi
