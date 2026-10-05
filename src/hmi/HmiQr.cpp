#include "HmiQr.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>

//  L'algorithme suit la norme ISO/IEC 18004, tel que le decrit la reference
//  "QR Code generator" de Project Nayuki (licence MIT) : memes etapes, memes
//  tables, meme calcul des penalites - ce qui rend nos grilles identiques a
//  celles des generateurs courants (les tests le verifient).

namespace hmi {

namespace {

constexpr int kMaxVersion = 10;
// [niveau L M Q H][version 0..10] : codes correcteurs par bloc, nombre de blocs.
constexpr int kEccPerBlock[4][kMaxVersion + 1] = {
    {-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18},
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26},
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24},
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28},
};
constexpr int kBlocks[4][kMaxVersion + 1] = {
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4},
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5},
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8},
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8},
};
constexpr int kPenaltyN1 = 3, kPenaltyN2 = 3, kPenaltyN3 = 40, kPenaltyN4 = 10;

int levelIndex(char c) {
    switch (c) {
        case 'L': case 'l': return 0;
        case 'M': case 'm': return 1;
        case 'Q': case 'q': return 2;
        case 'H': case 'h': return 3;
        default: return -1;
    }
}
// Les deux bits du niveau dans le format : L 01, M 00, Q 11, H 10.
int formatBitsOf(int level) { return level == 0 ? 1 : level == 1 ? 0 : level == 2 ? 3 : 2; }

int rawDataModules(int ver) {
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        const int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7) result -= 36;
    }
    return result;
}
int dataCodewords(int ver, int level) { return rawDataModules(ver) / 8 - kEccPerBlock[level][ver] * kBlocks[level][ver]; }

// ---- Reed-Solomon sur GF(256), polynome 0x11D ----------------------------------
std::uint8_t gfMul(std::uint8_t x, std::uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return static_cast<std::uint8_t>(z);
}
std::vector<std::uint8_t> rsDivisor(int degree) {
    std::vector<std::uint8_t> result(static_cast<std::size_t>(degree), 0);
    result.back() = 1;
    std::uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (std::size_t j = 0; j < result.size(); ++j) {
            result[j] = gfMul(result[j], root);
            if (j + 1 < result.size()) result[j] ^= result[j + 1];
        }
        root = gfMul(root, 0x02);
    }
    return result;
}
std::vector<std::uint8_t> rsRemainder(const std::vector<std::uint8_t>& data, const std::vector<std::uint8_t>& divisor) {
    std::vector<std::uint8_t> result(divisor.size(), 0);
    for (const std::uint8_t b : data) {
        const auto factor = static_cast<std::uint8_t>(b ^ result.front());
        result.erase(result.begin());
        result.push_back(0);
        for (std::size_t i = 0; i < result.size(); ++i) result[i] ^= gfMul(divisor[i], factor);
    }
    return result;
}

bool bit(long x, int i) { return ((x >> i) & 1) != 0; }

// ---- la grille -------------------------------------------------------------------
class Symbol {
public:
    Symbol(int version, int level) : version_(version), level_(level), size_(version * 4 + 17),
        mod_(static_cast<std::size_t>(size_ * size_), false), fn_(static_cast<std::size_t>(size_ * size_), false) {}

    void drawFunctionPatterns() {
        for (int i = 0; i < size_; ++i) {
            setFunction(6, i, i % 2 == 0);
            setFunction(i, 6, i % 2 == 0);
        }
        drawFinder(3, 3);
        drawFinder(size_ - 4, 3);
        drawFinder(3, size_ - 4);
        const auto pos = alignmentPositions();
        const auto n = pos.size();
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j)
                if (!((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))) drawAlignment(pos[i], pos[j]);
        drawFormatBits(0);
        drawVersion();
    }

    void drawCodewords(const std::vector<std::uint8_t>& data) {
        std::size_t i = 0;
        for (int right = size_ - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;
            for (int vert = 0; vert < size_; ++vert)
                for (int j = 0; j < 2; ++j) {
                    const int x = right - j;
                    const bool upward = ((right + 1) & 2) == 0;
                    const int y = upward ? size_ - 1 - vert : vert;
                    if (!fn_[idx(x, y)] && i < data.size() * 8) {
                        mod_[idx(x, y)] = bit(data[i >> 3], 7 - static_cast<int>(i & 7));
                        ++i;
                    }
                }
        }
    }

    void applyMask(int mask) {
        for (int y = 0; y < size_; ++y)
            for (int x = 0; x < size_; ++x) {
                bool invert = false;
                switch (mask) {
                    case 0: invert = (x + y) % 2 == 0; break;
                    case 1: invert = y % 2 == 0; break;
                    case 2: invert = x % 3 == 0; break;
                    case 3: invert = (x + y) % 3 == 0; break;
                    case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                    case 5: invert = x * y % 2 + x * y % 3 == 0; break;
                    case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
                    case 7: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                    default: break;
                }
                if (invert && !fn_[idx(x, y)]) mod_[idx(x, y)] = !mod_[idx(x, y)];
            }
    }

    void drawFormatBits(int mask) {
        const int data = formatBitsOf(level_) << 3 | mask;
        int rem = data;
        for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        const int bits = (data << 10 | rem) ^ 0x5412;
        for (int i = 0; i <= 5; ++i) setFunction(8, i, bit(bits, i));
        setFunction(8, 7, bit(bits, 6));
        setFunction(8, 8, bit(bits, 7));
        setFunction(7, 8, bit(bits, 8));
        for (int i = 9; i < 15; ++i) setFunction(14 - i, 8, bit(bits, i));
        for (int i = 0; i < 8; ++i) setFunction(size_ - 1 - i, 8, bit(bits, i));
        for (int i = 8; i < 15; ++i) setFunction(8, size_ - 15 + i, bit(bits, i));
        setFunction(8, size_ - 8, true);
    }

    [[nodiscard]] long penalty() const {
        long result = 0;
        for (int y = 0; y < size_; ++y) {
            bool runColor = false;
            int runX = 0;
            std::array<int, 7> history{};
            for (int x = 0; x < size_; ++x) {
                if (mod_[idx(x, y)] == runColor) {
                    ++runX;
                    if (runX == 5) result += kPenaltyN1;
                    else if (runX > 5) ++result;
                } else {
                    addHistory(runX, history);
                    if (!runColor) result += countPatterns(history) * kPenaltyN3;
                    runColor = mod_[idx(x, y)];
                    runX = 1;
                }
            }
            result += terminateAndCount(runColor, runX, history) * kPenaltyN3;
        }
        for (int x = 0; x < size_; ++x) {
            bool runColor = false;
            int runY = 0;
            std::array<int, 7> history{};
            for (int y = 0; y < size_; ++y) {
                if (mod_[idx(x, y)] == runColor) {
                    ++runY;
                    if (runY == 5) result += kPenaltyN1;
                    else if (runY > 5) ++result;
                } else {
                    addHistory(runY, history);
                    if (!runColor) result += countPatterns(history) * kPenaltyN3;
                    runColor = mod_[idx(x, y)];
                    runY = 1;
                }
            }
            result += terminateAndCount(runColor, runY, history) * kPenaltyN3;
        }
        for (int y = 0; y < size_ - 1; ++y)
            for (int x = 0; x < size_ - 1; ++x) {
                const bool c = mod_[idx(x, y)];
                if (c == mod_[idx(x + 1, y)] && c == mod_[idx(x, y + 1)] && c == mod_[idx(x + 1, y + 1)]) result += kPenaltyN2;
            }
        long dark = 0;
        for (const bool m : mod_) dark += m ? 1 : 0;
        const long total = static_cast<long>(size_) * size_;
        const long k = (std::labs(dark * 20L - total * 10L) + total - 1) / total - 1;
        result += k * kPenaltyN4;
        return result;
    }

    [[nodiscard]] int size() const noexcept { return size_; }
    [[nodiscard]] const std::vector<bool>& modules() const noexcept { return mod_; }

private:
    [[nodiscard]] std::size_t idx(int x, int y) const noexcept { return static_cast<std::size_t>(y * size_ + x); }
    void setFunction(int x, int y, bool dark) {
        mod_[idx(x, y)] = dark;
        fn_[idx(x, y)] = true;
    }
    void drawFinder(int x, int y) {
        for (int dy = -4; dy <= 4; ++dy)
            for (int dx = -4; dx <= 4; ++dx) {
                const int dist = std::max(std::abs(dx), std::abs(dy));
                const int xx = x + dx, yy = y + dy;
                if (xx >= 0 && xx < size_ && yy >= 0 && yy < size_) setFunction(xx, yy, dist != 2 && dist != 4);
            }
    }
    void drawAlignment(int x, int y) {
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) setFunction(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
    }
    [[nodiscard]] std::vector<int> alignmentPositions() const {
        if (version_ == 1) return {};
        const int numAlign = version_ / 7 + 2;
        const int step = (version_ * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
        std::vector<int> result{6};
        for (int i = 0, pos = size_ - 7; i < numAlign - 1; ++i, pos -= step) result.insert(result.begin() + 1, pos);
        return result;
    }
    void drawVersion() {
        if (version_ < 7) return;
        int rem = version_;
        for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        const long bits = static_cast<long>(version_) << 12 | rem;
        for (int i = 0; i < 18; ++i) {
            const bool b = bit(bits, i);
            const int a = size_ - 11 + i % 3, c = i / 3;
            setFunction(a, c, b);
            setFunction(c, a, b);
        }
    }
    [[nodiscard]] int countPatterns(const std::array<int, 7>& h) const {
        const int n = h[1];
        const bool core = n > 0 && h[2] == n && h[3] == n * 3 && h[4] == n && h[5] == n;
        return (core && h[0] >= n * 4 && h[6] >= n ? 1 : 0) + (core && h[6] >= n * 4 && h[0] >= n ? 1 : 0);
    }
    int terminateAndCount(bool runColor, int runLength, std::array<int, 7>& h) const {
        if (runColor) {
            addHistory(runLength, h);
            runLength = 0;
        }
        runLength += size_;
        addHistory(runLength, h);
        return countPatterns(h);
    }
    void addHistory(int runLength, std::array<int, 7>& h) const {
        if (h[0] == 0) runLength += size_;
        std::copy_backward(h.begin(), h.end() - 1, h.end());
        h[0] = runLength;
    }

    int version_, level_, size_;
    std::vector<bool> mod_, fn_;
};

} // namespace

int qrCapacity(char level) {
    const int li = levelIndex(level);
    if (li < 0) return 0;
    return (dataCodewords(kMaxVersion, li) * 8 - 4 - 16) / 8;
}

QrCode encodeQr(std::string_view text, char level, int forceMask, int minVersion) {
    QrCode out;
    const int li = levelIndex(level);
    if (li < 0) {
        out.error = "niveau de correction inconnu (L, M, Q ou H)";
        return out;
    }
    out.level = static_cast<char>(level >= 'a' ? level - 32 : level);
    // La plus petite version qui loge le texte (mode octets).
    const auto len = static_cast<int>(text.size());
    int version = 0;
    for (int v = std::clamp(minVersion, 1, kMaxVersion); v <= kMaxVersion; ++v) {
        const int ccBits = v <= 9 ? 8 : 16;
        if (4 + ccBits + len * 8 <= dataCodewords(v, li) * 8) { version = v; break; }
    }
    if (version == 0) {
        out.error = "texte trop long : " + std::to_string(len) + " octets, " + std::to_string(qrCapacity(out.level))
                  + " au plus au niveau " + std::string(1, out.level);
        return out;
    }
    // Les bits : mode octets (0100), la longueur, les octets, puis le bourrage.
    std::vector<bool> bits;
    const auto put = [&](long value, int count) { for (int i = count - 1; i >= 0; --i) bits.push_back(bit(value, i)); };
    put(4, 4);
    put(len, version <= 9 ? 8 : 16);
    for (const char c : text) put(static_cast<unsigned char>(c), 8);
    const std::size_t capacity = static_cast<std::size_t>(dataCodewords(version, li)) * 8;
    put(0, static_cast<int>(std::min<std::size_t>(4, capacity - bits.size())));
    put(0, static_cast<int>((8 - bits.size() % 8) % 8));
    for (std::uint8_t pad = 0xEC; bits.size() < capacity; pad ^= 0xEC ^ 0x11) put(pad, 8);
    std::vector<std::uint8_t> data(bits.size() / 8, 0);
    for (std::size_t i = 0; i < bits.size(); ++i)
        if (bits[i]) data[i >> 3] = static_cast<std::uint8_t>(data[i >> 3] | (1 << (7 - static_cast<int>(i & 7))));

    // Les blocs, leurs codes correcteurs, l'entrelacement.
    const int numBlocks = kBlocks[li][version], eccLen = kEccPerBlock[li][version];
    const int rawCodewords = rawDataModules(version) / 8;
    const int numShort = numBlocks - rawCodewords % numBlocks;
    const int shortLen = rawCodewords / numBlocks;
    const auto divisor = rsDivisor(eccLen);
    std::vector<std::vector<std::uint8_t>> blocks;
    std::size_t k = 0;
    for (int i = 0; i < numBlocks; ++i) {
        const std::size_t n = static_cast<std::size_t>(shortLen - eccLen + (i < numShort ? 0 : 1));
        std::vector<std::uint8_t> block(data.begin() + static_cast<std::ptrdiff_t>(k), data.begin() + static_cast<std::ptrdiff_t>(k + n));
        k += n;
        const auto ecc = rsRemainder(block, divisor);
        if (i < numShort) block.push_back(0);
        block.insert(block.end(), ecc.begin(), ecc.end());
        blocks.push_back(std::move(block));
    }
    std::vector<std::uint8_t> all;
    for (std::size_t i = 0; i < blocks.front().size(); ++i)
        for (std::size_t j = 0; j < blocks.size(); ++j)
            if (i != static_cast<std::size_t>(shortLen - eccLen) || static_cast<int>(j) >= numShort) all.push_back(blocks[j][i]);

    Symbol s(version, li);
    s.drawFunctionPatterns();
    s.drawCodewords(all);
    int mask = forceMask;
    if (mask < 0 || mask > 7) {
        long best = LONG_MAX;
        for (int m = 0; m < 8; ++m) {
            s.applyMask(m);
            s.drawFormatBits(m);
            const long p = s.penalty();
            if (p < best) { best = p; mask = m; }
            s.applyMask(m);
        }
    }
    s.applyMask(mask);
    s.drawFormatBits(mask);
    out.version = version;
    out.size = s.size();
    out.mask = mask;
    out.modules = s.modules();
    return out;
}

} // namespace hmi
