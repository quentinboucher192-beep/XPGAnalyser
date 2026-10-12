// =============================================================================
//  hmi/HmiQr.hpp - le code QR d'un texte (lot 9, objet "Code QR")
// -----------------------------------------------------------------------------
//  Un encodeur complet et sans dependance : les quatre niveaux de correction
//  (L 7 %, M 15 %, Q 25 %, H 30 %), les versions 1 a 40 (de 21 x 21 a 177 x 177
//  modules), les codes de Reed-Solomon, l'entrelacement des blocs et le choix
//  du masque par les penalites de la norme (ISO/IEC 18004).
//
//  1.12.12 (lot H) : LES 40 VERSIONS ET LE MODE LE PLUS COMPACT. Le texte prend
//  le mode qui le loge le mieux : des chiffres seuls (7 089 en L, 5 596 en M),
//  l'alphanumerique - 0 a 9, A a Z, l'espace et $ % * + - . / : - (4 296 en L,
//  3 391 en M), sinon les octets du texte en UTF-8 (2 953 en L, 2 331 en M,
//  1 663 en Q, 1 273 en H) ; la version est la plus petite qui le loge. Avant :
//  les octets seuls, et la version 10 au plus (213 octets en M). Un texte vide
//  reste un segment d'octets vide (sa grille ne change pas).
//
//  Le resultat est une grille de modules (vrai : noir) ; l'objet la dessine
//  avec sa marge. Sans ecran : hmi_qr_test compare des grilles a une reference
//  et verifie chaque version a chaque niveau (les codes de format et de version,
//  les syndromes de Reed-Solomon de chaque bloc, la capacite de la norme).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

struct QrCode {
    int               version{0};    // 1..40 ; 0 : rien (texte trop long, niveau inconnu)
    int               size{0};       // 17 + 4 x version modules de cote
    char              level{'M'};
    int               mask{-1};
    char              mode{'B'};     // 1.12.12 : 'N' chiffres, 'A' alphanumerique, 'B' octets
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
// Le nombre d'octets qu'un niveau loge au plus (en version 40 : 2 953 L, 2 331 M,
// 1 663 Q, 1 273 H ; avant la 1.12.12, la version 10).
[[nodiscard]] int qrCapacity(char level);

// ---- 1.12.12 (lot H) : la place qui reste --------------------------------------
//  Ce que le texte prend, dans le mode le plus compact, et ce que le niveau loge
//  au plus dans ce mode (en version 40) : l'inspecteur dit « reste 1 234 octets
//  en M, version 27 », Generer dit un texte fixe trop long.
struct QrRoom {
    bool fits{false};
    int  version{0};       // la plus petite version qui loge le texte ; 0 : trop long
    char mode{'B'};        // 'N' chiffres, 'A' alphanumerique, 'B' octets
    int  used{0};          // le texte, compte dans son mode (chiffres, caracteres, octets)
    int  max{0};           // le plus que le niveau loge dans ce mode (version 40) ; 0 : niveau inconnu
    [[nodiscard]] int left() const noexcept { return max - used; }
};
[[nodiscard]] QrRoom qrRoom(std::string_view text, char level);
// En francais : « reste 1 234 octets en M, version 27 », « reste 12 chiffres en L,
// version 1 », « trop long : 3 000 octets, 2 331 au plus en M ».
[[nodiscard]] std::string qrRoomText(const QrRoom& room, char level);
// Le mode le plus compact d'un texte : 'N', 'A' ou 'B' (un texte vide : 'B').
[[nodiscard]] char qrModeOf(std::string_view text) noexcept;

// ---- les tables de la norme (pour les essais et l'inspecteur) ---------------------
//  0 : version (1..40) ou niveau inconnus.
[[nodiscard]] int qrDataCodewords(int version, char level);     // les mots de donnees
[[nodiscard]] int qrEccPerBlock(int version, char level);       // les mots correcteurs d'un bloc
[[nodiscard]] int qrBlocks(int version, char level);            // le nombre de blocs
[[nodiscard]] std::vector<int> qrAlignmentPositions(int version);

} // namespace hmi
