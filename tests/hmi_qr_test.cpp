// =============================================================================
//  tests/hmi_qr_test.cpp - 1.12.12 (lot H) : LE CODE QR JUSQU'A LA VERSION 40
// -----------------------------------------------------------------------------
//  L'encodeur de hmi/HmiQr.cpp, sans ecran :
//   - des symboles de reference, module pour module (le generateur de Project
//     Nayuki, qrcodegen 1.8.0, sans relever le niveau : le meme que l'objet) -
//     l'exemple de la norme (01234567 en 1-M), un texte alphanumerique, un texte
//     en UTF-8, des chiffres, les plus longs textes des quatre niveaux en
//     version 40 ;
//   - l'exemple de la norme mot pour mot (ISO/IEC 18004, annexe I : les 16 mots
//     de donnees et les 10 mots correcteurs) ;
//   - CHAQUE VERSION A CHAQUE NIVEAU, dans chaque mode, relue ici par un lecteur
//     ecrit d'apres la norme et non d'apres l'encodeur : la place des motifs
//     (les modules de donnees comptent les mots de la table 1 et leurs bits de
//     reste), les deux copies du format et de la version (codes BCH justes),
//     l'entrelacement des blocs de la table 9, les syndromes de Reed-Solomon de
//     chaque bloc (tous nuls), le texte relu a l'identique, la capacite de la
//     table 7 ;
//   - la place qui reste (« reste 1 234 octets en M, version 27 ») et le texte
//     trop long.
//
//  QR_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiQr.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("QR_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

// ---- la norme, ecrite ici (et non lue dans l'encodeur) --------------------------------
// Table 1 : les mots de chaque version, et les bits de reste.
constexpr int kTotal[41] = {0,    26,   44,   70,   100,  134,  172,  196,  242,  292,  346,  404,  466,  532,
                            581,  655,  733,  815,  901,  991,  1085, 1156, 1258, 1364, 1474, 1588, 1706, 1828,
                            1921, 2051, 2185, 2323, 2465, 2611, 2761, 2876, 3034, 3196, 3362, 3532, 3706};
constexpr int kRemainder[41] = {0, 0, 7, 7, 7, 7, 7, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3,
                                4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0};
// Table 7 : les mots de donnees [version][L, M, Q, H].
constexpr int kData[41][4] = {
    {0, 0, 0, 0}, {19, 16, 13, 9}, {34, 28, 22, 16}, {55, 44, 34, 26}, {80, 64, 48, 36},
    {108, 86, 62, 46}, {136, 108, 76, 60}, {156, 124, 88, 66}, {194, 154, 110, 86}, {232, 182, 132, 100},
    {274, 216, 154, 122}, {324, 254, 180, 140}, {370, 290, 206, 158}, {428, 334, 244, 180}, {461, 365, 261, 197},
    {523, 415, 295, 223}, {589, 453, 325, 253}, {647, 507, 367, 283}, {721, 563, 397, 313}, {795, 627, 445, 341},
    {861, 669, 485, 385}, {932, 714, 512, 406}, {1006, 782, 568, 442}, {1094, 860, 614, 464}, {1174, 914, 664, 514},
    {1276, 1000, 718, 538}, {1370, 1062, 754, 596}, {1468, 1128, 808, 628}, {1531, 1193, 871, 661}, {1631, 1267, 911, 701},
    {1735, 1373, 985, 745}, {1843, 1455, 1033, 793}, {1955, 1541, 1115, 845}, {2071, 1631, 1171, 901}, {2191, 1725, 1231, 961},
    {2306, 1812, 1286, 986}, {2434, 1914, 1354, 1054}, {2566, 1992, 1426, 1096}, {2702, 2102, 1502, 1142}, {2812, 2216, 1582, 1222},
    {2956, 2334, 1666, 1276},
};
// Table 9 : [version][L, M, Q, H] = {blocs, mots correcteurs par bloc}.
constexpr int kBlocksEcc[41][4][2] = {
    {{0, 0}, {0, 0}, {0, 0}, {0, 0}}, {{1, 7}, {1, 10}, {1, 13}, {1, 17}}, {{1, 10}, {1, 16}, {1, 22}, {1, 28}},
    {{1, 15}, {1, 26}, {2, 18}, {2, 22}}, {{1, 20}, {2, 18}, {2, 26}, {4, 16}}, {{1, 26}, {2, 24}, {4, 18}, {4, 22}},
    {{2, 18}, {4, 16}, {4, 24}, {4, 28}}, {{2, 20}, {4, 18}, {6, 18}, {5, 26}}, {{2, 24}, {4, 22}, {6, 22}, {6, 26}},
    {{2, 30}, {5, 22}, {8, 20}, {8, 24}}, {{4, 18}, {5, 26}, {8, 24}, {8, 28}}, {{4, 20}, {5, 30}, {8, 28}, {11, 24}},
    {{4, 24}, {8, 22}, {10, 26}, {11, 28}}, {{4, 26}, {9, 22}, {12, 24}, {16, 22}}, {{4, 30}, {9, 24}, {16, 20}, {16, 24}},
    {{6, 22}, {10, 24}, {12, 30}, {18, 24}}, {{6, 24}, {10, 28}, {17, 24}, {16, 30}}, {{6, 28}, {11, 28}, {16, 28}, {19, 28}},
    {{6, 30}, {13, 26}, {18, 28}, {21, 28}}, {{7, 28}, {14, 26}, {21, 26}, {25, 26}}, {{8, 28}, {16, 26}, {20, 30}, {25, 28}},
    {{8, 28}, {17, 26}, {23, 28}, {25, 30}}, {{9, 28}, {17, 28}, {23, 30}, {34, 24}}, {{9, 30}, {18, 28}, {25, 30}, {30, 30}},
    {{10, 30}, {20, 28}, {27, 30}, {32, 30}}, {{12, 26}, {21, 28}, {29, 30}, {35, 30}}, {{12, 28}, {23, 28}, {34, 28}, {37, 30}},
    {{12, 30}, {25, 28}, {34, 30}, {40, 30}}, {{13, 30}, {26, 28}, {35, 30}, {42, 30}}, {{14, 30}, {28, 28}, {38, 30}, {45, 30}},
    {{15, 30}, {29, 28}, {40, 30}, {48, 30}}, {{16, 30}, {31, 28}, {43, 30}, {51, 30}}, {{17, 30}, {33, 28}, {45, 30}, {54, 30}},
    {{18, 30}, {35, 28}, {48, 30}, {57, 30}}, {{19, 30}, {37, 28}, {51, 30}, {60, 30}}, {{19, 30}, {38, 28}, {53, 30}, {63, 30}},
    {{20, 30}, {40, 28}, {56, 30}, {66, 30}}, {{21, 30}, {43, 28}, {59, 30}, {70, 30}}, {{22, 30}, {45, 28}, {62, 30}, {74, 30}},
    {{24, 30}, {47, 28}, {65, 30}, {77, 30}}, {{25, 30}, {49, 28}, {68, 30}, {81, 30}},
};
// Annexe E : les centres des motifs d'alignement.
const std::vector<std::vector<int>> kAlign = {
    {}, {}, {6, 18}, {6, 22}, {6, 26}, {6, 30}, {6, 34}, {6, 22, 38}, {6, 24, 42}, {6, 26, 46}, {6, 28, 50},
    {6, 30, 54}, {6, 32, 58}, {6, 34, 62}, {6, 26, 46, 66}, {6, 26, 48, 70}, {6, 26, 50, 74}, {6, 30, 54, 78},
    {6, 30, 56, 82}, {6, 30, 58, 86}, {6, 34, 62, 90}, {6, 28, 50, 72, 94}, {6, 26, 50, 74, 98}, {6, 30, 54, 78, 102},
    {6, 28, 54, 80, 106}, {6, 32, 58, 84, 110}, {6, 30, 58, 86, 114}, {6, 34, 62, 90, 118}, {6, 26, 50, 74, 98, 122},
    {6, 30, 54, 78, 102, 126}, {6, 26, 52, 78, 104, 130}, {6, 30, 56, 82, 108, 134}, {6, 34, 60, 86, 112, 138},
    {6, 30, 58, 86, 114, 142}, {6, 34, 62, 90, 118, 146}, {6, 30, 54, 78, 102, 126, 150}, {6, 24, 50, 76, 102, 128, 154},
    {6, 28, 54, 80, 106, 132, 158}, {6, 32, 58, 84, 110, 136, 162}, {6, 26, 54, 82, 110, 138, 166},
    {6, 30, 58, 86, 114, 142, 170},
};
constexpr char kLevels[4] = {'L', 'M', 'Q', 'H'};
// Les deux bits du niveau dans le format (table 12) : L 01, M 00, Q 11, H 10.
constexpr int kLevelBits[4] = {1, 0, 3, 2};
constexpr const char* kAlnum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

int countBits(char mode, int v) {
    const int r = v <= 9 ? 0 : v <= 26 ? 1 : 2;
    const int n[3] = {10, 12, 14}, a[3] = {9, 11, 13}, b[3] = {8, 16, 16};
    return mode == 'N' ? n[r] : mode == 'A' ? a[r] : b[r];
}
// Le plus de caracteres d'un mode que logent les mots de donnees de la table 7.
int capacityOf(char mode, int v, int li) {
    const int bits = kData[v][li] * 8 - 4 - countBits(mode, v);
    if (mode == 'N') return 3 * (bits / 10) + (bits % 10 >= 7 ? 2 : bits % 10 >= 4 ? 1 : 0);
    if (mode == 'A') return 2 * (bits / 11) + (bits % 11 >= 6 ? 1 : 0);
    return bits / 8;
}

// ---- GF(256) par ses tables (pas la multiplication de l'encodeur) ------------------------
struct Gf {
    std::array<int, 512> exp{};
    std::array<int, 256> log{};
    Gf() {
        int x = 1;
        for (int i = 0; i < 255; ++i) {
            exp[static_cast<std::size_t>(i)] = x;
            log[static_cast<std::size_t>(x)] = i;
            x <<= 1;
            if (x & 0x100) x ^= 0x11D;
        }
        for (int i = 255; i < 512; ++i) exp[static_cast<std::size_t>(i)] = exp[static_cast<std::size_t>(i - 255)];
    }
    [[nodiscard]] int mul(int a, int b) const {
        if (a == 0 || b == 0) return 0;
        return exp[static_cast<std::size_t>(log[static_cast<std::size_t>(a)] + log[static_cast<std::size_t>(b)])];
    }
};
const Gf g_gf;

// ---- LE LECTEUR, d'apres la norme -----------------------------------------------------------
struct Reading {
    bool        ok{false};
    std::string why;
    int         level{-1}, mask{-1}, version{0};
    std::vector<int> codewords;           // dans l'ordre du symbole (entrelaces)
    std::vector<std::vector<int>> data, ecc;   // par bloc
    std::string text;
    char        mode{'?'};
};

// Les modules de fonction : chercheurs et separateurs, format, synchronisation, alignement, version.
std::vector<bool> functionModules(int v) {
    const int n = 17 + 4 * v;
    std::vector<bool> f(static_cast<std::size_t>(n * n), false);
    const auto mark = [&](int x, int y) {
        if (x >= 0 && y >= 0 && x < n && y < n) f[static_cast<std::size_t>(y * n + x)] = true;
    };
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) mark(x, y);                      // en haut a gauche, le format compris
    for (int y = 0; y < 9; ++y)
        for (int x = n - 8; x < n; ++x) mark(x, y);                  // en haut a droite
    for (int y = n - 8; y < n; ++y)
        for (int x = 0; x < 9; ++x) mark(x, y);                      // en bas a gauche (le module sombre)
    for (int i = 0; i < n; ++i) {
        mark(6, i);
        mark(i, 6);
    }
    const auto& pos = kAlign[static_cast<std::size_t>(v)];
    for (std::size_t i = 0; i < pos.size(); ++i)
        for (std::size_t j = 0; j < pos.size(); ++j) {
            const bool finder = (i == 0 && j == 0) || (i == 0 && j + 1 == pos.size()) || (i + 1 == pos.size() && j == 0);
            if (finder) continue;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) mark(pos[i] + dx, pos[j] + dy);
        }
    if (v >= 7)
        for (int a = 0; a < 6; ++a)
            for (int b = n - 11; b < n - 8; ++b) {
                mark(b, a);
                mark(a, b);
            }
    return f;
}

bool maskAt(int mask, int x, int y) {   // x : la colonne (j), y : la ligne (i)
    switch (mask) {
        case 0: return (y + x) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (y + x) % 3 == 0;
        case 4: return (y / 2 + x / 3) % 2 == 0;
        case 5: return (y * x) % 2 + (y * x) % 3 == 0;
        case 6: return ((y * x) % 2 + (y * x) % 3) % 2 == 0;
        case 7: return ((y + x) % 2 + (y * x) % 3) % 2 == 0;
        default: return false;
    }
}

// Le reste d'une division de polynomes sur GF(2) (le code BCH est juste si le reste est nul).
int bchRemainder(int value, int generator, int degree, int bits) {
    for (int b = bits - 1; b >= degree; --b)
        if (value & (1 << b)) value ^= generator << (b - degree);
    return value;
}

Reading readSymbol(const QrCode& q) {
    Reading r;
    const int n = q.size;
    if (n < 21 || (n - 17) % 4 != 0) {
        r.why = "taille " + std::to_string(n);
        return r;
    }
    const int v = (n - 17) / 4;
    r.version = v;
    const auto at = [&](int x, int y) { return q.at(x, y); };
    // Le format, ses deux copies.
    int fa = 0, fb = 0;
    for (int i = 0; i <= 5; ++i) fa |= at(8, i) << i;
    fa |= at(8, 7) << 6;
    fa |= at(8, 8) << 7;
    fa |= at(7, 8) << 8;
    for (int i = 9; i < 15; ++i) fa |= at(14 - i, 8) << i;
    for (int i = 0; i < 8; ++i) fb |= at(n - 1 - i, 8) << i;
    for (int i = 8; i < 15; ++i) fb |= at(8, n - 15 + i) << i;
    if (fa != fb) {
        r.why = "les deux copies du format different";
        return r;
    }
    const int format = fa ^ 0x5412;
    if (bchRemainder(format, 0x537, 10, 15) != 0) {
        r.why = "code BCH du format faux";
        return r;
    }
    const int levelBits = format >> 13, mask = (format >> 10) & 7;
    for (int li = 0; li < 4; ++li)
        if (kLevelBits[li] == levelBits) r.level = li;
    r.mask = mask;
    if (r.level < 0) {
        r.why = "niveau illisible";
        return r;
    }
    if (!at(8, n - 8)) {
        r.why = "le module sombre manque";
        return r;
    }
    // La version (7 et plus), ses deux copies.
    if (v >= 7) {
        int va = 0, vb = 0;
        for (int i = 0; i < 18; ++i) {
            va |= at(n - 11 + i % 3, i / 3) << i;
            vb |= at(i / 3, n - 11 + i % 3) << i;
        }
        if (va != vb || bchRemainder(va, 0x1F25, 12, 18) != 0 || (va >> 12) != v) {
            r.why = "code de version faux";
            return r;
        }
    }
    // Les mots, en zigzag, demasques.
    const auto fn = functionModules(v);
    std::size_t dataModules = 0;
    for (const bool b : fn) dataModules += b ? 0 : 1;
    if (dataModules != static_cast<std::size_t>(kTotal[v] * 8 + kRemainder[v])) {
        r.why = "modules de donnees : " + std::to_string(dataModules);
        return r;
    }
    std::vector<int> bits;
    for (int right = n - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < n; ++vert)
            for (int j = 0; j < 2; ++j) {
                const int x = right - j;
                const bool upward = ((right + 1) & 2) == 0;
                const int y = upward ? n - 1 - vert : vert;
                if (fn[static_cast<std::size_t>(y * n + x)]) continue;
                bits.push_back((at(x, y) != maskAt(mask, x, y)) ? 1 : 0);
            }
    }
    for (int i = 0; i + 8 <= static_cast<int>(bits.size()) && static_cast<int>(r.codewords.size()) < kTotal[v]; i += 8) {
        int b = 0;
        for (int k = 0; k < 8; ++k) b = (b << 1) | bits[static_cast<std::size_t>(i + k)];
        r.codewords.push_back(b);
    }
    // Les blocs de la table 9.
    const int blocks = kBlocksEcc[v][r.level][0], ecc = kBlocksEcc[v][r.level][1];
    const int shortTotal = kTotal[v] / blocks, numShort = blocks - kTotal[v] % blocks;
    r.data.assign(static_cast<std::size_t>(blocks), {});
    r.ecc.assign(static_cast<std::size_t>(blocks), {});
    std::size_t k = 0;
    for (int i = 0; i <= shortTotal - ecc; ++i)
        for (int b = 0; b < blocks; ++b) {
            const int len = shortTotal - ecc + (b >= numShort ? 1 : 0);
            if (i < len) r.data[static_cast<std::size_t>(b)].push_back(r.codewords[k++]);
        }
    for (int i = 0; i < ecc; ++i)
        for (int b = 0; b < blocks; ++b) r.ecc[static_cast<std::size_t>(b)].push_back(r.codewords[k++]);
    if (k != r.codewords.size()) {
        r.why = "les blocs ne prennent pas tous les mots";
        return r;
    }
    int dataWords = 0;
    for (const auto& d : r.data) dataWords += static_cast<int>(d.size());
    if (dataWords != kData[v][r.level]) {
        r.why = "mots de donnees : " + std::to_string(dataWords);
        return r;
    }
    // Les syndromes de chaque bloc : c(alpha^i) = 0 pour i = 0..ecc-1.
    for (int b = 0; b < blocks; ++b) {
        std::vector<int> poly = r.data[static_cast<std::size_t>(b)];
        poly.insert(poly.end(), r.ecc[static_cast<std::size_t>(b)].begin(), r.ecc[static_cast<std::size_t>(b)].end());
        for (int i = 0; i < ecc; ++i) {
            const int root = g_gf.exp[static_cast<std::size_t>(i)];
            int s = 0;
            for (const int c : poly) s = g_gf.mul(s, root) ^ c;
            if (s != 0) {
                r.why = "syndrome non nul (bloc " + std::to_string(b + 1) + ")";
                return r;
            }
        }
    }
    // Le texte : le mode, la longueur, les donnees.
    std::vector<int> stream;
    for (const auto& d : r.data)
        for (const int c : d)
            for (int i = 7; i >= 0; --i) stream.push_back((c >> i) & 1);
    std::size_t p = 0;
    const auto take = [&](int count) {
        int x = 0;
        for (int i = 0; i < count; ++i) x = (x << 1) | (p < stream.size() ? stream[p++] : 0);
        return x;
    };
    const int indicator = take(4);
    r.mode = indicator == 1 ? 'N' : indicator == 2 ? 'A' : indicator == 4 ? 'B' : '?';
    if (r.mode == '?') {
        r.why = "mode inconnu " + std::to_string(indicator);
        return r;
    }
    const int count = take(countBits(r.mode, v));
    if (r.mode == 'N') {
        for (int i = 0; i < count; i += 3) {
            const int digits = std::min(3, count - i);
            const int group = take(digits == 3 ? 10 : digits == 2 ? 7 : 4);
            std::string s = std::to_string(group);
            while (static_cast<int>(s.size()) < digits) s = "0" + s;
            r.text += s;
        }
    } else if (r.mode == 'A') {
        for (int i = 0; i < count; i += 2) {
            const int value = take(i + 1 < count ? 11 : 6);
            if (value >= (i + 1 < count ? 45 * 45 : 45)) {
                r.why = "caractere alphanumerique hors table";
                return r;
            }
            if (i + 1 < count) r.text += kAlnum[value / 45];
            r.text += kAlnum[value % 45];
        }
    } else {
        for (int i = 0; i < count; ++i) r.text += static_cast<char>(take(8));
    }
    r.ok = true;
    return r;
}

// FNV-1a 64 bits des modules ("0" / "1", ligne par ligne) : une grille en un nombre.
std::uint64_t hashOf(const QrCode& q) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (int y = 0; y < q.size; ++y)
        for (int x = 0; x < q.size; ++x) {
            h ^= static_cast<unsigned char>(q.at(x, y) ? '1' : '0');
            h *= 0x100000001b3ULL;
        }
    return h;
}
std::string rowsOf(const QrCode& q) {
    std::string out;
    for (int y = 0; y < q.size; ++y) {
        for (int x = 0; x < q.size; ++x) out += q.at(x, y) ? '1' : '0';
        out += '\n';
    }
    return out;
}
std::string repeat(const std::string& s, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) out += s;
    return out;
}

// ---- 1. les symboles de reference ----------------------------------------------------------
void references() {
    std::printf("-- les symboles de reference (qrcodegen, module pour module)\n");
    const auto iso = encodeQr("01234567", 'M');
    const std::string isoRows =
        "111111100011101111111\n" "100000101110001000001\n" "101110100110001011101\n" "101110100101101011101\n"
        "101110101101101011101\n" "100000100001001000001\n" "111111101010101111111\n" "000000000000000000000\n"
        "101010100010100010010\n" "110100001011010100010\n" "000110111011011101110\n" "110011010101110110010\n"
        "001001110111011100001\n" "000000001010001000010\n" "111111100000100010001\n" "100000100010001001011\n"
        "101110101110101011101\n" "101110100101010101110\n" "101110101101011100101\n" "100000100001110111000\n"
        "111111101001011100101\n";
    check(iso.ok() && iso.version == 1 && iso.mask == 0 && iso.mode == 'N', "01234567 en M (l'exemple de la norme) : version 1, chiffres, masque 0");
    check(rowsOf(iso) == isoRows, "... module pour module");
    // L'annexe I de la norme : les mots de donnees et les mots correcteurs de 01234567 en 1-M.
    const auto read = readSymbol(iso);
    const std::vector<int> isoData = {0x10, 0x20, 0x0C, 0x56, 0x61, 0x80, 0xEC, 0x11, 0xEC, 0x11, 0xEC, 0x11, 0xEC, 0x11, 0xEC, 0x11};
    const std::vector<int> isoEcc = {0xA5, 0x24, 0xD4, 0xC1, 0xED, 0x36, 0xC7, 0x87, 0x2C, 0x55};
    check(read.ok && read.data.size() == 1 && read.data[0] == isoData && read.ecc[0] == isoEcc,
          "01234567 en 1-M : les 16 mots de donnees et les 10 mots correcteurs de l'annexe I");

    const auto hello = encodeQr("HELLO WORLD", 'M');
    const std::string helloRows =
        "111111100010101111111\n" "100000101110001000001\n" "101110100010101011101\n" "101110100010101011101\n"
        "101110101011101011101\n" "100000100111001000001\n" "111111101010101111111\n" "000000000000000000000\n"
        "101010100100100010010\n" "011110001001000010001\n" "000111111101001011000\n" "111101011001110101110\n"
        "010011110101001110101\n" "000000001010001000101\n" "111111100000100101100\n" "100000100110001101000\n"
        "101110101100101111111\n" "101110100011010100010\n" "101110101111011101001\n" "100000100001110001011\n"
        "111111101101011100001\n";
    check(hello.ok() && hello.version == 1 && hello.mask == 0 && hello.mode == 'A',
          "HELLO WORLD en M : alphanumerique, version 1, masque 0 (avant la 1.12.12 : des octets, masque 4)");
    check(rowsOf(hello) == helloRows, "... module pour module");
    const auto helloQ = encodeQr("HELLO WORLD", 'Q');
    const std::string helloQRows =
        "111111101100001111111\n" "100000101001001000001\n" "101110101001101011101\n" "101110101000001011101\n"
        "101110101010001011101\n" "100000100010001000001\n" "111111101010101111111\n" "000000001000000000000\n"
        "011010110000101011111\n" "010000001111000010001\n" "001101110110001011000\n" "011011010011010101110\n"
        "100010101011101110101\n" "000000001101001000101\n" "111111101010000101100\n" "100000100101101101000\n"
        "101110101010001111111\n" "101110100101010100010\n" "101110101001011101001\n" "100000101011110001011\n"
        "111111100001011100001\n";
    check(helloQ.ok() && helloQ.version == 1 && rowsOf(helloQ) == helloQRows, "HELLO WORLD en Q : version 1, module pour module");

    struct Ref {
        std::string text;
        char level;
        int version, mask;
        char mode;
        std::uint64_t hash;
    };
    const std::string pompe = repeat("Pompe P3 : d\xC3\xA9" "bit nominal 12,5 m\xC2\xB3/h ; pression 4,2 bar. ", 12);
    const std::vector<Ref> refs = {
        {"https://example.com/armoire-a", 'M', 3, 0, 'B', 0x2D1E479687F46443ULL},
        {"Temp\xC3\xA9rature : 21,5 \xC2\xB0" "C", 'M', 2, 1, 'B', 0x5F1D9310EEA23307ULL},
        {"Vanne V12 : ouverte", 'H', 3, 2, 'B', 0x07B3364846E40342ULL},
        {"https://example.com/pompe/P3", 'L', 2, 6, 'B', 0x2E09EF4AD29193E9ULL},
        {std::string(100, 'x'), 'M', 6, 3, 'B', 0x5F79FBAF41A33CAFULL},
        {repeat("0123456789", 8), 'Q', 4, 0, 'N', 0x3F99B8386AE48CC3ULL},
        {"ARMOIRE A-12 / ZONE 3", 'H', 3, 3, 'A', 0xE39B4FB94E31E6BBULL},
        {std::string(213, 'z'), 'M', 10, 1, 'B', 0x08A05B8B23AB3B32ULL},
        {std::string(214, 'z'), 'M', 11, 2, 'B', 0x9A400787A257514BULL},
        {std::string(271, 'w'), 'L', 10, 4, 'B', 0xC8BF3C19F7C56F41ULL},
        {std::string(300, 'x'), 'Q', 16, 0, 'B', 0xCDC1928FA4FA29FFULL},
        {pompe, 'M', 21, 0, 'B', 0xAC5B4A5FA4096B67ULL},
        {"", 'M', 1, 3, 'B', 0x27F4A3A3DC113261ULL},
        {std::string(7089, '7'), 'L', 40, 2, 'N', 0x415BF85FFD4F745FULL},
        {repeat("A1 $%*+-./:", 390) + "ABCDEF", 'L', 40, 0, 'A', 0xE9C52951973E6533ULL},
        {std::string(2953, 'b'), 'L', 40, 2, 'B', 0x8D5C1FA886526A69ULL},
        {std::string(2331, 'm'), 'M', 40, 1, 'B', 0xCA363F0A1F2E98E8ULL},
        {std::string(1663, 'q'), 'Q', 40, 4, 'B', 0x3208AF3E21DB1CF1ULL},
        {std::string(1273, 'h'), 'H', 40, 1, 'B', 0xDAB641C4F3BA2D42ULL},
    };
    for (const auto& r : refs) {
        const auto q = encodeQr(r.text, r.level);
        const std::string label = (r.text.size() > 24 ? std::to_string(r.text.size()) + " octets" : "'" + r.text + "'") + " en " + r.level;
        check(q.ok() && q.version == r.version && q.mask == r.mask && q.mode == r.mode,
              label + " : version " + std::to_string(r.version) + ", masque " + std::to_string(r.mask) + ", mode " + r.mode + " (obtenu "
                  + std::to_string(q.version) + ", " + std::to_string(q.mask) + ", " + q.mode + ")");
        check(hashOf(q) == r.hash, label + " : la grille de reference");
        const auto back = readSymbol(q);
        check(back.ok && back.text == r.text, label + " : relu a l'identique" + (back.ok ? std::string{} : " (" + back.why + ")"));
    }
}

// ---- 2. chaque version, chaque niveau, chaque mode --------------------------------------------
void chaqueVersion() {
    std::printf("-- chaque version a chaque niveau : la norme relue (format, version, blocs, syndromes, texte)\n");
    int symbols = 0, good = 0;
    std::string firstBad;
    for (int v = 1; v <= 40; ++v) {
        // Les tables : celles de la norme.
        bool tables = qrAlignmentPositions(v) == kAlign[static_cast<std::size_t>(v)];
        for (int li = 0; li < 4; ++li) {
            const char lv = kLevels[li];
            tables = tables && qrDataCodewords(v, lv) == kData[v][li] && qrBlocks(v, lv) == kBlocksEcc[v][li][0]
                  && qrEccPerBlock(v, lv) == kBlocksEcc[v][li][1];
        }
        check(tables, "version " + std::to_string(v) + " : les tables 7 et 9 et les alignements de l'annexe E");
        for (int li = 0; li < 4; ++li) {
            const char lv = kLevels[li];
            // Les octets a chaque niveau ; les chiffres et l'alphanumerique a tour de role (un niveau par version).
            std::vector<char> modes = {'B'};
            if (v % 4 == li) modes = {'B', 'N', 'A'};
            for (const char mode : modes) {
                const int len = capacityOf(mode, v, li);
                std::string text;
                for (int i = 0; i < len; ++i) {
                    if (mode == 'N') text += static_cast<char>('0' + (i * 7 + v) % 10);
                    else if (mode == 'A') text += kAlnum[(i * 11 + v) % 45];
                    else text += static_cast<char>('a' + (i * 5 + v + li) % 26);
                }
                const auto q = encodeQr(text, lv);
                const auto r = readSymbol(q);
                ++symbols;
                const bool ok = q.ok() && q.version == v && q.mode == mode && q.size == 17 + 4 * v && r.ok && r.level == li
                             && r.mask == q.mask && r.mode == mode && r.text == text;
                if (ok) ++good;
                else if (firstBad.empty())
                    firstBad = "version " + std::to_string(v) + " " + lv + " " + mode + " : " + (q.ok() ? r.why : q.error) + " (version obtenue "
                             + std::to_string(q.version) + ")";
                // Un caractere de plus ne loge plus dans cette version.
                if (v < 40) {
                    const auto more = encodeQr(text + (mode == 'N' ? "1" : mode == 'A' ? "A" : "a"), lv);
                    if (!(more.ok() && more.version == v + 1)) {
                        if (firstBad.empty()) firstBad = "version " + std::to_string(v) + " " + lv + " " + mode + " : un de plus devrait passer en version suivante";
                        --good;
                    }
                }
            }
        }
    }
    check(good == symbols, std::to_string(symbols) + " symboles (40 versions x 4 niveaux, octets, chiffres, alphanumerique) : chacun relu juste"
                               + (firstBad.empty() ? std::string{} : " - " + firstBad));
}

// ---- 3. la capacite, la place qui reste, le texte trop long ---------------------------------
void place() {
    std::printf("-- la capacite de la norme, la place qui reste, le texte trop long\n");
    check(qrCapacity('L') == 2953 && qrCapacity('M') == 2331 && qrCapacity('Q') == 1663 && qrCapacity('H') == 1273,
          "qrCapacity : 2 953 / 2 331 / 1 663 / 1 273 octets en version 40 (avant : la version 10)");
    const int numeric[4] = {7089, 5596, 3993, 3057}, alnum[4] = {4296, 3391, 2420, 1852};
    bool maxima = true;
    for (int li = 0; li < 4; ++li)
        maxima = maxima && qrRoom("1", kLevels[li]).max == numeric[li] && qrRoom("A", kLevels[li]).max == alnum[li];
    check(maxima, "les chiffres (7 089 en L) et l'alphanumerique (4 296 en L) : la table 7 en version 40");
    const auto armoire = qrRoom("https://example.com/armoire-a", 'M');
    check(armoire.fits && armoire.mode == 'B' && armoire.version == 3 && armoire.used == 29 && armoire.left() == 2302,
          "le texte d'un objet neuf : 29 octets, version 3, il en reste 2 302");
    check(qrRoomText(armoire, 'M') == "reste 2\xC2\xA0" "302 octets en M, version 3", "... dit : " + qrRoomText(armoire, 'M'));
    const auto hello = qrRoom("HELLO WORLD", 'Q');
    check(qrRoomText(hello, 'Q') == "reste 2\xC2\xA0" "409 caract\xC3\xA8res en Q, version 1", "alphanumerique : " + qrRoomText(hello, 'Q'));
    const auto full = qrRoom(std::string(7089, '7'), 'L');
    check(full.fits && full.version == 40 && qrRoomText(full, 'L') == "reste 0 chiffre en L, version 40", "plein : " + qrRoomText(full, 'L'));
    const auto tooLong = qrRoom(std::string(2332, 'm'), 'M');
    check(!tooLong.fits && qrRoomText(tooLong, 'M') == "trop long : 2\xC2\xA0" "332 octets, 2\xC2\xA0" "331 au plus en M",
          "trop long : " + qrRoomText(tooLong, 'M'));
    const auto q = encodeQr(std::string(2332, 'm'), 'M');
    check(!q.ok() && q.error == qrRoomText(tooLong, 'M'), "l'encodeur le dit pareil (ce que l'objet montre)");
    check(!encodeQr(std::string(7090, '7'), 'L').ok() && encodeQr(std::string(7089, '7'), 'L').ok(), "7 089 chiffres en L passent, 7 090 non");
    check(qrRoom("abc", 'X').max == 0 && !encodeQr("abc", 'X').ok(), "un niveau inconnu : rien");
    check(encodeQr("A", 'M', -1, 5).version == 5, "une version minimale imposee");
    check(encodeQr("HELLO WORLD", 'M', 6).mask == 6, "un masque impose");
    check(qrModeOf("") == 'B' && qrModeOf("0042") == 'N' && qrModeOf("P-101 : 3 BAR") == 'A' && qrModeOf("p-101") == 'B'
              && qrModeOf("12,5") == 'B',
          "le mode : vide en octets, des chiffres, l'alphanumerique (majuscules), sinon les octets (minuscules, virgule)");
}

} // namespace

int main() {
    references();
    chaqueVersion();
    place();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
