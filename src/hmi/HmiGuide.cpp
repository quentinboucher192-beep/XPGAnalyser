#include "HmiGuide.hpp"

#include <algorithm>
#include <utility>
#include <cctype>

namespace hmi::guide {

namespace {

// Les lettres accentuees du francais (UTF-8, deux octets) et leur base.
char baseOf(unsigned char lead, unsigned char next) {
    if (lead == 0xC3) {
        if (next >= 0x80 && next <= 0x85) return 'a';   // A majuscules
        if (next == 0x87) return 'c';
        if (next >= 0x88 && next <= 0x8B) return 'e';
        if (next >= 0x8C && next <= 0x8F) return 'i';
        if (next >= 0x92 && next <= 0x96) return 'o';
        if (next >= 0x99 && next <= 0x9C) return 'u';
        if (next >= 0xA0 && next <= 0xA5) return 'a';
        if (next == 0xA7) return 'c';
        if (next >= 0xA8 && next <= 0xAB) return 'e';
        if (next >= 0xAC && next <= 0xAF) return 'i';
        if (next >= 0xB2 && next <= 0xB6) return 'o';
        if (next >= 0xB9 && next <= 0xBC) return 'u';
    }
    if (lead == 0xC5 && (next == 0x93 || next == 0x92)) return 'o';   // oe
    return 0;
}

bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

} // namespace

namespace {
// Le texte replie, et pour chaque octet replie, l'octet de la source d'ou il
// vient : un passage trouve dans le texte replie se recoupe dans l'original.
std::string foldMapped(std::string_view s, std::vector<std::size_t>* map) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (map) map->push_back(i);
        if (c < 0x80) {
            out += static_cast<char>(std::tolower(c));
            continue;
        }
        if (i + 1 < s.size())
            if (const char b = baseOf(c, static_cast<unsigned char>(s[i + 1]))) {
                out += b;
                ++i;
                continue;
            }
        out += static_cast<char>(c);
    }
    return out;
}
} // namespace

std::string fold(std::string_view s) { return foldMapped(s, nullptr); }

std::string ihmWords(std::string_view text) {
    // Du plus precis au plus general (une regle ne defait pas la precedente).
    static const std::pair<std::string_view, std::string_view> kRules[] = {
        {" (IHM ou automate)", ""},
        {" (automate ou IHM)", ""},
        {" ou de l'automate", ""},
        {" dans l'automate ou dans l'\xC3\xA9quipement", " dans l'\xC3\xA9quipement"},
        {"variable IHM ou automate", "variable IHM"},
        {"la relie \xC3\xA0 l'automate", "la relie \xC3\xA0 une variable"},
        {"le relie \xC3\xA0 l'automate", "le relie \xC3\xA0 une variable"},
        {"les relie \xC3\xA0 l'automate", "les relie \xC3\xA0 des variables"},
        {"son adresse automate", "son adresse"},
        {"Variable API", "Variable"},
        {"L'automate", "L'\xC3\xA9quipement"},
        {"l'automate", "l'\xC3\xA9quipement"},
        {"d'automate", "d'\xC3\xA9quipement"},
        {"Un automate", "Un \xC3\xA9quipement"},
        {"un automate", "un \xC3\xA9quipement"},
        {"automates", "\xC3\xA9quipements"},
        {"Automates", "\xC3\x89quipements"},
        {"automate", "\xC3\xA9quipement"},
        {"Automate", "\xC3\x89quipement"},
    };
    std::string s(text);
    for (const auto& [from, to] : kRules)
        for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
            s.replace(at, from.size(), to);
    return s;
}

std::vector<Topic> ihmWorded(const std::vector<Topic>& in) {
    std::vector<Topic> out = in;
    for (auto& t : out) {
        t.title = ihmWords(t.title);
        t.summary = ihmWords(t.summary);
        for (auto& b : t.blocks) {
            b.text = ihmWords(b.text);
            b.label = ihmWords(b.label);
        }
        for (auto& p : t.params) {
            p.label = ihmWords(p.label);
            p.text = ihmWords(p.text);
        }
        for (auto& sh : t.shots) sh.caption = ihmWords(sh.caption);
        t.example = ihmWords(t.example);
        for (auto& st : t.tutorial) {
            st.title = ihmWords(st.title);
            st.text = ihmWords(st.text);
        }
    }
    return out;
}

std::string plain(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '`') continue;
        if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '*') { ++i; continue; }
        out += s[i];
    }
    return out;
}

std::vector<std::string> chapters() {
    std::vector<std::string> out;
    for (const auto& t : topics())
        if (std::find(out.begin(), out.end(), t.chapter) == out.end()) out.push_back(t.chapter);
    return out;
}

std::string latestChange(const Topic& t) {
    // Les versions se comparent champ par champ, en nombres ("1.10" > "1.9").
    const auto newer = [](const std::string& a, const std::string& b) {
        std::size_t i = 0, j = 0;
        while (i < a.size() || j < b.size()) {
            long x = 0, y = 0;
            while (i < a.size() && a[i] != '.') x = x * 10 + (a[i++] - '0');
            while (j < b.size() && b[j] != '.') y = y * 10 + (b[j++] - '0');
            if (x != y) return x > y;
            ++i;
            ++j;
        }
        return false;
    };
    std::string best = t.since;
    for (const auto& b : t.blocks)
        if (!b.since.empty() && (best.empty() || newer(b.since, best))) best = b.since;
    return best;
}

const Topic* topic(std::string_view key) noexcept {
    for (const auto& t : topics()) if (t.key == key) return &t;
    return nullptr;
}

std::string topicForWord(std::string_view word) {
    if (word.empty()) return {};
    const auto w = fold(word);
    for (const auto& t : topics())
        for (const auto& k : t.words)
            if (fold(k) == w) return t.key;
    return {};
}

const Topic* topicForKind(std::string_view kindKey) noexcept {
    if (kindKey.empty()) return nullptr;
    for (const auto& t : topics()) if (t.kind == kindKey) return &t;
    return nullptr;
}

std::string paramHelp(std::string_view kindKey, std::string_view key) {
    for (const auto* t : {topicForKind(kindKey), topicForKind("*")}) {
        if (!t) continue;
        for (const auto& p : t->params)
            if (p.key == key) return plain(p.text);
    }
    return {};
}

std::string topicForPlace(std::string_view place) {
    if (place.empty()) return {};
    for (const auto& t : topics())
        for (const auto& p : t.places)
            if (p == place) return t.key;
    return {};
}

std::vector<Hit> search(std::string_view term, std::size_t limit) {
    std::vector<Hit> out;
    std::string needle = fold(term);
    while (!needle.empty() && needle.back() == ' ') needle.pop_back();
    while (!needle.empty() && needle.front() == ' ') needle.erase(needle.begin());
    if (needle.empty()) return out;
    const auto whole = [&](const std::string& text, std::size_t at) {
        const bool left = at == 0 || !isWordChar(text[at - 1]);
        const bool right = at + needle.size() >= text.size() || !isWordChar(text[at + needle.size()]);
        return left && right;
    };
    for (const auto& t : topics()) {
        Hit h;
        h.key = t.key;
        const auto title = fold(t.title);
        if (const auto at = title.find(needle); at != std::string::npos) h.score += whole(title, at) ? 120 : 90;
        for (const auto& w : t.words)
            if (fold(w) == needle) h.score += 100;
        if (fold(t.summary).find(needle) != std::string::npos) {
            h.score += 40;
            h.excerpt = plain(t.summary);
        }
        for (const auto& b : t.blocks) {
            const std::string text = plain(b.text);
            const auto f = fold(text);
            const auto at = f.find(needle);
            if (at == std::string::npos) continue;
            h.score += b.kind == BlockKind::Heading ? 30 : 8;
            if (h.excerpt.empty()) {
                // Le passage : la phrase (ou la ligne) qui contient le terme.
                std::vector<std::size_t> map;
                (void)foldMapped(text, &map);
                const std::size_t from = at < map.size() ? map[at] : 0;
                std::size_t begin = text.rfind(". ", from);
                begin = begin == std::string::npos ? 0 : begin + 2;
                const auto nl = text.rfind('\n', from);
                if (nl != std::string::npos && nl + 1 > begin) begin = nl + 1;
                std::size_t end = text.find(". ", from);
                const auto nl2 = text.find('\n', from);
                if (nl2 != std::string::npos && (end == std::string::npos || nl2 < end)) end = nl2;
                h.excerpt = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin + 1);
                for (auto& c : h.excerpt) if (c == '\t' || c == '\n') c = ' ';
                if (h.excerpt.size() > 160) {
                    std::size_t cut = 157;
                    while (cut > 0 && (static_cast<unsigned char>(h.excerpt[cut]) & 0xC0) == 0x80) --cut;
                    h.excerpt = h.excerpt.substr(0, cut) + "...";
                }
            }
        }
        if (h.score > 0) {
            if (h.excerpt.empty()) h.excerpt = plain(t.summary);
            out.push_back(std::move(h));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    if (limit && out.size() > limit) out.resize(limit);
    return out;
}

} // namespace hmi::guide
