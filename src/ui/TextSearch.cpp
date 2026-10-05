// =============================================================================
//  ui/TextSearch.cpp - chercher comme on parle (lot recherche) : voir l'en-tete.
// =============================================================================
#include "TextSearch.hpp"

#include <algorithm>

namespace ui {

namespace {

bool isBlank(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

char lowerAscii(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// Les lettres latines accentuees de deux octets UTF-8 (C3 xx, et les ligatures
// C5 92 / C5 93) : ce qu'elles deviennent une fois pliees ; nul : pas une lettre
// a plier (elle reste telle quelle).
const char* fold2(unsigned char lead, unsigned char next) noexcept {
    if (lead == 0xC5) return next == 0x92 || next == 0x93 ? "oe" : nullptr;
    if (lead != 0xC3) return nullptr;
    switch (next) {
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85:
        case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5:
            return "a";
        case 0x86: case 0xA6:
            return "ae";
        case 0x87: case 0xA7:
            return "c";
        case 0x88: case 0x89: case 0x8A: case 0x8B:
        case 0xA8: case 0xA9: case 0xAA: case 0xAB:
            return "e";
        case 0x8C: case 0x8D: case 0x8E: case 0x8F:
        case 0xAC: case 0xAD: case 0xAE: case 0xAF:
            return "i";
        case 0x90: case 0xB0:
            return "d";
        case 0x91: case 0xB1:
            return "n";
        case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x98:
        case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB8:
            return "o";
        case 0x99: case 0x9A: case 0x9B: case 0x9C:
        case 0xB9: case 0xBA: case 0xBB: case 0xBC:
            return "u";
        case 0x9D: case 0xBD: case 0xBF:
            return "y";
        case 0x9F:
            return "ss";
        default:
            return nullptr;
    }
}

// La longueur d'un caractere UTF-8 d'apres son premier octet (1 pour un octet
// isole ou mal forme : on avance quand meme).
std::size_t utf8Length(unsigned char c) noexcept {
    if (c >= 0xF0) return 4;
    if (c >= 0xE0) return 3;
    if (c >= 0xC0) return 2;
    return 1;
}

bool isWordChar(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || u >= 0x80;
}

} // namespace

std::string foldForSearch(std::string_view text, std::vector<std::size_t>* starts) {
    std::string out;
    out.reserve(text.size());
    if (starts) {
        starts->clear();
        starts->reserve(text.size() + 1);
    }
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (starts) starts->push_back(i);
            out += lowerAscii(text[i]);
            ++i;
            continue;
        }
        if (i + 1 < text.size())
            if (const char* f = fold2(c, static_cast<unsigned char>(text[i + 1]))) {
                for (const char* p = f; *p; ++p) {
                    if (starts) starts->push_back(i);
                    out += *p;
                }
                i += 2;
                continue;
            }
        // Un autre caractere : recopie tel quel, octet par octet.
        const std::size_t len = std::min(utf8Length(c), text.size() - i);
        for (std::size_t k = 0; k < len; ++k) {
            if (starts) starts->push_back(i);
            out += text[i + k];
        }
        i += len;
    }
    if (starts) starts->push_back(text.size());
    return out;
}

bool containsFolded(std::string_view hay, std::string_view foldedNeedle) {
    if (foldedNeedle.empty()) return true;
    const bool ascii = std::all_of(hay.begin(), hay.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    if (ascii) {
        if (foldedNeedle.size() > hay.size()) return false;
        const auto it = std::search(hay.begin(), hay.end(), foldedNeedle.begin(), foldedNeedle.end(),
                                    [](char a, char b) { return lowerAscii(a) == b; });
        return it != hay.end();
    }
    return foldForSearch(hay).find(foldedNeedle) != std::string::npos;
}

// ============================================================ SearchQuery ===
SearchQuery::SearchQuery(std::string_view text) : text_(text) {
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isBlank(text[i])) ++i;
        if (i >= text.size()) break;
        // "-mot", -"une phrase" : a exclure. Un tiret seul reste un tiret.
        bool negative = false;
        if (text[i] == '-' && i + 1 < text.size() && !isBlank(text[i + 1])) {
            negative = true;
            ++i;
        }
        std::string_view term;
        if (text[i] == '"') {
            ++i;
            const auto close = text.find('"', i);
            term = text.substr(i, close == std::string_view::npos ? std::string_view::npos : close - i);
            i = close == std::string_view::npos ? text.size() : close + 1;
        } else {
            const auto start = i;
            while (i < text.size() && !isBlank(text[i])) ++i;
            term = text.substr(start, i - start);
        }
        // Une phrase garde ses espaces interieurs, pas ceux des bords.
        while (!term.empty() && isBlank(term.front())) term.remove_prefix(1);
        while (!term.empty() && isBlank(term.back())) term.remove_suffix(1);
        if (term.empty()) continue;
        auto folded = foldForSearch(term);
        auto& list = negative ? exclude_ : include_;
        if (std::find(list.begin(), list.end(), folded) == list.end()) list.push_back(std::move(folded));
    }
}

namespace {

// Les trois formes de matches() : la meme boucle, sans copier les textes.
template <class Range>
bool matchesAll(const SearchQuery& q, const Range& texts) {
    if (q.empty()) return true;
    for (const auto& ex : q.excluded())
        for (const auto& t : texts)
            if (containsFolded(t, ex)) return false;
    for (const auto& in : q.terms()) {
        bool any = false;
        for (const auto& t : texts)
            if (containsFolded(t, in)) {
                any = true;
                break;
            }
        if (!any) return false;
    }
    return true;
}

} // namespace

bool SearchQuery::matches(std::initializer_list<std::string_view> texts) const { return matchesAll(*this, texts); }
bool SearchQuery::matches(const std::vector<std::string>& texts) const { return matchesAll(*this, texts); }
bool SearchQuery::matches(const std::vector<std::string_view>& texts) const { return matchesAll(*this, texts); }

void SearchQuery::feed(Progress& p, std::string_view text) const {
    if (p.excluded) return;
    for (const auto& ex : exclude_)
        if (containsFolded(text, ex)) {
            p.excluded = true;
            return;
        }
    const std::size_t n = std::min<std::size_t>(include_.size(), 64);
    for (std::size_t k = 0; k < n; ++k) {
        const std::uint64_t bit = std::uint64_t{1} << k;
        if ((p.found & bit) == 0 && containsFolded(text, include_[k])) p.found |= bit;
    }
}

bool SearchQuery::satisfied(const Progress& p) const noexcept {
    if (p.excluded) return false;
    const std::size_t n = std::min<std::size_t>(include_.size(), 64);
    const std::uint64_t all = n >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << n) - 1;
    return (p.found & all) == all;
}

std::vector<std::pair<std::size_t, std::size_t>> SearchQuery::ranges(std::string_view text) const {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    if (include_.empty() || text.empty()) return out;
    std::vector<std::size_t> starts;
    const std::string folded = foldForSearch(text, &starts);
    for (const auto& term : include_) {
        if (term.empty()) continue;
        for (auto at = folded.find(term); at != std::string::npos; at = folded.find(term, at + term.size())) {
            // La fin : le debut du caractere qui suit le dernier octet trouve (un
            // "oe" plie d'une ligature est un seul caractere de l'original).
            std::size_t end = at + term.size();
            while (end < folded.size() && starts[end] == starts[end - 1]) ++end;
            const std::size_t a = starts[at], b = starts[end];
            if (b > a) out.emplace_back(a, b);
        }
    }
    std::sort(out.begin(), out.end());
    std::vector<std::pair<std::size_t, std::size_t>> merged;
    for (const auto& r : out) {
        if (!merged.empty() && r.first <= merged.back().second) merged.back().second = std::max(merged.back().second, r.second);
        else merged.push_back(r);
    }
    return merged;
}

int SearchQuery::rank(std::string_view text) const {
    if (include_.empty()) return -1;
    const std::string folded = foldForSearch(text);
    const auto at = folded.find(include_.front());
    if (at == std::string::npos) return -1;
    if (at == 0) return 0;
    return isWordChar(folded[at - 1]) ? 2 : 1;
}

} // namespace ui
