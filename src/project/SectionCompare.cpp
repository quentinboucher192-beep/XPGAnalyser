// =============================================================================
//  project/SectionCompare.cpp - 1.8.0 : voir SectionCompare.hpp
// =============================================================================
#include "SectionCompare.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace project::compare {

namespace {

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

bool identLike(std::string_view s) {
    if (s.empty()) return false;
    for (const char c : s)
        if (!identChar(c)) return false;
    return true;
}

std::string lower(std::string_view s) {
    std::string o(s);
    for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

struct Token {
    std::size_t begin{0}, end{0};
    enum Kind : std::uint8_t { Ident, Number, Space, Comment, String, Index, Punct } kind{Punct};
};

// Les jetons d'une ligne ; `inComment` : un commentaire (* ... *) continue.
std::vector<Token> tokenize(std::string_view line, bool& inComment) {
    std::vector<Token> out;
    std::size_t i = 0;
    const std::size_t n = line.size();
    while (i < n) {
        const std::size_t start = i;
        if (inComment) {
            const auto e = line.find("*)", i);
            i = e == std::string_view::npos ? n : e + 2;
            if (e != std::string_view::npos) inComment = false;
            out.push_back({start, i, Token::Comment});
            continue;
        }
        const char c = line[i];
        if (c == '(' && i + 1 < n && line[i + 1] == '*') {
            inComment = true;
            i += 2;
            const auto e = line.find("*)", i);
            i = e == std::string_view::npos ? n : e + 2;
            if (e != std::string_view::npos) inComment = false;
            out.push_back({start, i, Token::Comment});
            continue;
        }
        if (c == '\'' || c == '"') {
            const auto e = line.find(c, i + 1);
            i = e == std::string_view::npos ? n : e + 1;
            out.push_back({start, i, Token::String});
            continue;
        }
        if (c == ' ' || c == '\t') {
            while (i < n && (line[i] == ' ' || line[i] == '\t')) ++i;
            out.push_back({start, i, Token::Space});
            continue;
        }
        if (c == '[') {
            // [0], [12] : un indice constant, un seul jeton (l'equivalence [0] <-> [1]).
            std::size_t j = i + 1;
            while (j < n && std::isdigit(static_cast<unsigned char>(line[j]))) ++j;
            if (j > i + 1 && j < n && line[j] == ']') {
                i = j + 1;
                out.push_back({start, i, Token::Index});
                continue;
            }
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            while (i < n && (identChar(line[i]) || line[i] == '.')) ++i;
            out.push_back({start, i, Token::Number});
            continue;
        }
        if (identChar(c)) {
            while (i < n && (identChar(line[i]) || line[i] == '#')) ++i;
            out.push_back({start, i, Token::Ident});
            continue;
        }
        ++i;
        out.push_back({start, i, Token::Punct});
    }
    return out;
}

// Un identifiant, avec les equivalences : leur texte a la fin d'un morceau
// (fin du nom, avant un '_' ou un chiffre), et apres au moins un caractere.
std::string applyIdentEquivalences(std::string t, const Options& o) {
    for (std::size_t k = 0; k < o.equivalences.size(); ++k) {
        const auto& e = o.equivalences[k];
        if (!e.enabled || !identLike(e.left) || !identLike(e.right)) continue;
        const std::string mark = std::string("\x01") + static_cast<char>('A' + static_cast<int>(k % 26));
        for (const auto& raw : {e.left, e.right}) {
            const std::string what = o.ignoreCase ? lower(raw) : raw;
            std::size_t at = 1;
            while ((at = t.find(what, at)) != std::string::npos) {
                const std::size_t after = at + what.size();
                const bool before = at > 0 && identChar(t[at - 1]);
                const bool boundary = after == t.size() || t[after] == '_' || std::isdigit(static_cast<unsigned char>(t[after]));
                if (before && boundary) {
                    t.replace(at, what.size(), mark);
                    at += mark.size();
                } else {
                    ++at;
                }
            }
        }
    }
    return t;
}

// La forme comparee d'un jeton ; vide : ignore (espace, commentaire au choix).
std::string canonical(std::string_view line, const Token& tk, const Options& o) {
    std::string_view text = line.substr(tk.begin, tk.end - tk.begin);
    switch (tk.kind) {
        case Token::Space: return o.ignoreSpaces ? std::string{} : std::string(" ");
        case Token::Comment:
            if (o.ignoreComments) return {};
            if (o.ignoreSpaces) {
                std::string s;
                for (const char c : text)
                    if (c != ' ' && c != '\t') s += c;
                return o.ignoreCase ? lower(s) : s;
            }
            return o.ignoreCase ? lower(text) : std::string(text);
        case Token::Ident: return applyIdentEquivalences(o.ignoreCase ? lower(text) : std::string(text), o);
        case Token::Index:
            for (std::size_t k = 0; k < o.equivalences.size(); ++k) {
                const auto& e = o.equivalences[k];
                if (e.enabled && (text == e.left || text == e.right) && !identLike(e.left))
                    return std::string("\x02") + static_cast<char>('A' + static_cast<int>(k % 26));
            }
            return std::string(text);
        default: return o.ignoreCase ? lower(text) : std::string(text);
    }
}

std::vector<std::string> canonicalLines(const std::vector<std::string>& lines, const Options& o) {
    std::vector<std::string> out;
    out.reserve(lines.size());
    bool inComment = false;
    for (const auto& l : lines) {
        std::string c;
        for (const auto& tk : tokenize(l, inComment)) {
            c += canonical(l, tk, o);
        }
        if (!o.ignoreSpaces) {
            // Les espaces en fin de ligne ne comptent jamais.
            while (!c.empty() && c.back() == ' ') c.pop_back();
        }
        // Les equivalences d'autre forme (plusieurs jetons : Armoires[0]) : sur la ligne entiere.
        for (std::size_t k = 0; k < o.equivalences.size(); ++k) {
            const auto& e = o.equivalences[k];
            if (!e.enabled || identLike(e.left) || e.left.empty() || e.right.empty()) continue;
            if (e.left.front() == '[' && e.left.back() == ']' && e.right.front() == '[' && e.right.back() == ']') continue;   // deja jeton par jeton
            const std::string mark = std::string("\x03") + static_cast<char>('A' + static_cast<int>(k % 26));
            for (const auto& raw : {e.left, e.right}) {
                const std::string what = o.ignoreCase ? lower(raw) : raw;
                std::size_t at = 0;
                while ((at = c.find(what, at)) != std::string::npos) {
                    c.replace(at, what.size(), mark);
                    at += mark.size();
                }
            }
        }
        out.push_back(std::move(c));
    }
    return out;
}

// La plus longue sous-suite commune : '=' (les deux), '-' (gauche), '+' (droite).
struct Op { char kind; int left, right; };

std::vector<Op> align(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::vector<Op> ops;
    const std::size_t n = a.size(), m = b.size();
    // Le debut et la fin identiques, sans table.
    std::size_t pre = 0;
    while (pre < n && pre < m && a[pre] == b[pre]) ++pre;
    std::size_t suf = 0;
    while (suf < n - pre && suf < m - pre && a[n - 1 - suf] == b[m - 1 - suf]) ++suf;
    for (std::size_t i = 0; i < pre; ++i) ops.push_back({'=', static_cast<int>(i), static_cast<int>(i)});
    const std::size_t N = n - pre - suf, Mm = m - pre - suf;
    if (N * Mm > 16u * 1000u * 1000u || N >= 65535u || Mm >= 65535u) {
        // Trop grand pour la table : ligne a ligne, dans l'ordre (rare : deux sections enormes et tres differentes).
        const std::size_t k = std::min(N, Mm);
        for (std::size_t i = 0; i < k; ++i)
            ops.push_back(a[pre + i] == b[pre + i] ? Op{'=', static_cast<int>(pre + i), static_cast<int>(pre + i)}
                                                   : Op{'~', static_cast<int>(pre + i), static_cast<int>(pre + i)});
        for (std::size_t i = k; i < N; ++i) ops.push_back({'-', static_cast<int>(pre + i), -1});
        for (std::size_t j = k; j < Mm; ++j) ops.push_back({'+', -1, static_cast<int>(pre + j)});
    } else if (N > 0 || Mm > 0) {
        // 16 bits suffisent (moins de 65 535 lignes de chaque cote) : 32 Mo au plus.
        std::vector<std::uint16_t> T((N + 1) * (Mm + 1), 0);
        const auto at = [&](std::size_t i, std::size_t j) -> std::uint16_t& { return T[i * (Mm + 1) + j]; };
        for (std::size_t i = N; i-- > 0;)
            for (std::size_t j = Mm; j-- > 0;)
                at(i, j) = a[pre + i] == b[pre + j] ? static_cast<std::uint16_t>(at(i + 1, j + 1) + 1) : std::max(at(i + 1, j), at(i, j + 1));
        std::size_t i = 0, j = 0;
        while (i < N && j < Mm) {
            if (a[pre + i] == b[pre + j]) {
                ops.push_back({'=', static_cast<int>(pre + i), static_cast<int>(pre + j)});
                ++i;
                ++j;
            } else if (at(i + 1, j) >= at(i, j + 1)) {
                ops.push_back({'-', static_cast<int>(pre + i), -1});
                ++i;
            } else {
                ops.push_back({'+', -1, static_cast<int>(pre + j)});
                ++j;
            }
        }
        for (; i < N; ++i) ops.push_back({'-', static_cast<int>(pre + i), -1});
        for (; j < Mm; ++j) ops.push_back({'+', -1, static_cast<int>(pre + j)});
    }
    for (std::size_t s = 0; s < suf; ++s) ops.push_back({'=', static_cast<int>(n - suf + s), static_cast<int>(m - suf + s)});
    return ops;
}

std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + ' ' + (n > 1 ? many : one); }

} // namespace

std::optional<Equivalence> equivalenceFromNames(std::string_view a, std::string_view b, std::string_view ownerA, std::string_view ownerB) {
    const auto diffOf = [](std::string_view x, std::string_view y) -> std::optional<Equivalence> {
        std::size_t i = 0;
        while (i < x.size() && i < y.size() && x[i] == y[i]) ++i;
        std::size_t j = 0;
        while (j < x.size() - i && j < y.size() - i && x[x.size() - 1 - j] == y[y.size() - 1 - j]) ++j;
        const auto l = x.substr(i, x.size() - i - j), r = y.substr(i, y.size() - i - j);
        if (l.empty() || r.empty() || l.size() > 12 || r.size() > 12) return std::nullopt;
        if (!identLike(l) || !identLike(r)) return std::nullopt;
        Equivalence e;
        e.left = std::string(l);
        e.right = std::string(r);
        e.fromNames = true;
        return e;
    };
    if (a != b)
        if (auto e = diffOf(a, b)) return e;
    if (!ownerA.empty() && !ownerB.empty() && ownerA != ownerB)
        if (auto e = diffOf(ownerA, ownerB)) return e;
    return std::nullopt;
}

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto nl = text.find('\n', from);
        auto line = text.substr(from, (nl == std::string_view::npos ? text.size() : nl) - from);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.emplace_back(line);
        if (nl == std::string_view::npos) break;
        from = nl + 1;
    }
    // Une derniere ligne vide (le texte finit par un saut de ligne) n'en est pas une.
    if (out.size() > 1 && out.back().empty()) out.pop_back();
    return out;
}

Result compare(const std::vector<std::string>& left, const std::vector<std::string>& right, const Options& o) {
    Result r;
    r.leftLines = left.size();
    r.rightLines = right.size();
    const auto ca = canonicalLines(left, o), cb = canonicalLines(right, o);
    const auto ops = align(ca, cb);
    std::size_t k = 0;
    while (k < ops.size()) {
        if (ops[k].kind == '=') {
            r.rows.push_back({Row::Same, ops[k].left, ops[k].right});
            ++k;
            continue;
        }
        if (ops[k].kind == '~') {
            r.rows.push_back({Row::Changed, ops[k].left, ops[k].right});
            ++k;
            continue;
        }
        std::vector<int> removed, added;
        while (k < ops.size() && (ops[k].kind == '-' || ops[k].kind == '+')) {
            if (ops[k].kind == '-') removed.push_back(ops[k].left);
            else added.push_back(ops[k].right);
            ++k;
        }
        const std::size_t pairs = std::min(removed.size(), added.size());
        for (std::size_t q = 0; q < pairs; ++q) r.rows.push_back({Row::Changed, removed[q], added[q]});
        for (std::size_t q = pairs; q < removed.size(); ++q) r.rows.push_back({Row::Removed, removed[q], -1});
        for (std::size_t q = pairs; q < added.size(); ++q) r.rows.push_back({Row::Added, -1, added[q]});
    }
    for (std::size_t i = 0; i < r.rows.size(); ++i) {
        switch (r.rows[i].kind) {
            case Row::Same: ++r.same; continue;
            case Row::Changed: ++r.changed; break;
            case Row::Removed: ++r.removed; break;
            case Row::Added: ++r.added; break;
        }
        if (!r.blocks.empty() && r.blocks.back().second + 1 == i) r.blocks.back().second = i;
        else r.blocks.emplace_back(i, i);
    }
    const std::size_t total = left.size() + right.size();
    r.similarity = total == 0 ? 100 : static_cast<int>((200 * r.same + total / 2) / total);
    return r;
}

void inlineDiff(std::string_view left, std::string_view right, const Options& o, std::vector<Span>& ls, std::vector<Span>& rs) {
    ls.clear();
    rs.clear();
    bool cl = false, cr = false;
    const auto ta = tokenize(left, cl), tb = tokenize(right, cr);
    // Les jetons compares (sans ceux que les options ignorent), et leur forme.
    std::vector<std::size_t> ia, ib;
    std::vector<std::string> fa, fb;
    for (std::size_t i = 0; i < ta.size(); ++i)
        if (auto c = canonical(left, ta[i], o); !c.empty()) { ia.push_back(i); fa.push_back(std::move(c)); }
    for (std::size_t i = 0; i < tb.size(); ++i)
        if (auto c = canonical(right, tb[i], o); !c.empty()) { ib.push_back(i); fb.push_back(std::move(c)); }
    const auto ops = align(fa, fb);
    const auto mark = [](std::vector<Span>& out, const Token& t) {
        if (!out.empty() && out.back().end >= t.begin) out.back().end = std::max(out.back().end, t.end);
        else out.push_back({t.begin, t.end});
    };
    for (const auto& op : ops) {
        if (op.kind == '-' || op.kind == '~') mark(ls, ta[ia[static_cast<std::size_t>(op.left)]]);
        if (op.kind == '+' || op.kind == '~') mark(rs, tb[ib[static_cast<std::size_t>(op.right)]]);
    }
}

int similarity(const std::vector<std::string>& a, const std::vector<std::string>& b, const Options& o) { return compare(a, b, o).similarity; }

std::string report(std::string_view leftTitle, std::string_view rightTitle, const std::vector<std::string>& left,
                   const std::vector<std::string>& right, const Options& o, const Result& r) {
    std::string out;
    out += "Comparaison : ";
    out += leftTitle;
    out += "  <->  ";
    out += rightTitle;
    out += '\n';
    std::string how;
    for (const auto& e : o.equivalences)
        if (e.enabled) how += (how.empty() ? "" : ", ") + e.left + " <-> " + e.right;
    if (!how.empty()) how = "noms rapproch\xC3\xA9s : " + how;
    const auto add = [&how](const char* s) { how += (how.empty() ? "" : " ; ") + std::string(s); };
    if (o.ignoreSpaces) add("espaces ignor\xC3\xA9s");
    if (o.ignoreCase) add("casse ignor\xC3\xA9" "e");
    if (o.ignoreComments) add("commentaires ignor\xC3\xA9s");
    if (!how.empty()) out += "(" + how + ")\n";
    out += std::to_string(r.leftLines) + " <-> " + std::to_string(r.rightLines) + " lignes : " + plural(r.same, "identique", "identiques") + ", "
         + plural(r.changed, "modifi\xC3\xA9" "e", "modifi\xC3\xA9" "es") + ", " + plural(r.added, "ajout\xC3\xA9" "e", "ajout\xC3\xA9" "es") + ", "
         + plural(r.removed, "retir\xC3\xA9" "e", "retir\xC3\xA9" "es") + " (" + std::to_string(r.similarity) + " % semblables)\n";
    if (r.blocks.empty()) {
        out += "\nAucune diff\xC3\xA9rence.\n";
        return out;
    }
    std::size_t n = 0;
    for (const auto& [first, last] : r.blocks) {
        ++n;
        int l0 = -1, l1 = -1, r0 = -1, r1 = -1;
        for (std::size_t i = first; i <= last; ++i) {
            const auto& row = r.rows[i];
            if (row.left >= 0) { if (l0 < 0) l0 = row.left; l1 = row.left; }
            if (row.right >= 0) { if (r0 < 0) r0 = row.right; r1 = row.right; }
        }
        const auto range = [](int a, int b) {
            if (a < 0) return std::string("\xE2\x80\x94");
            return a == b ? std::to_string(a + 1) : std::to_string(a + 1) + "-" + std::to_string(b + 1);
        };
        out += "\n--- diff\xC3\xA9rence " + std::to_string(n) + " / " + std::to_string(r.blocks.size()) + " : lignes " + range(l0, l1) + " <-> " + range(r0, r1) + '\n';
        for (std::size_t i = first; i <= last; ++i)
            if (r.rows[i].left >= 0) out += "- " + std::to_string(r.rows[i].left + 1) + "\t" + left[static_cast<std::size_t>(r.rows[i].left)] + '\n';
        for (std::size_t i = first; i <= last; ++i)
            if (r.rows[i].right >= 0) out += "+ " + std::to_string(r.rows[i].right + 1) + "\t" + right[static_cast<std::size_t>(r.rows[i].right)] + '\n';
    }
    return out;
}

} // namespace project::compare
