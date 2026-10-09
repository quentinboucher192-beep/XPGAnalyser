// =============================================================================
//  hmi/HmiDecl.cpp - 1.11.18 (refonte des scripts, lot 2) : les declarations d'un
//  code ST, lues sans perte (voir HmiDecl.hpp)
// =============================================================================
#include "HmiDecl.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace hmi::decl {

std::string_view keyword(Section s) noexcept {
    switch (s) {
        case Section::Var:    return "VAR";
        case Section::Temp:   return "VAR_TEMP";
        case Section::Input:  return "VAR_INPUT";
        case Section::InOut:  return "VAR_IN_OUT";
        case Section::Output: return "VAR_OUTPUT";
    }
    return "VAR";
}

bool isParameter(Section s) noexcept { return s == Section::Input || s == Section::InOut || s == Section::Output; }

namespace {

constexpr std::size_t npos = static_cast<std::size_t>(-1);

bool identStart(char c) noexcept { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool identChar(char c) noexcept { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool isBlank(char c) noexcept { return std::isspace(static_cast<unsigned char>(c)) != 0; }

bool sameWord(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

std::string upper(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}

bool sectionOf(std::string_view w, Section& out) noexcept {
    for (const Section s : {Section::Var, Section::Temp, Section::Input, Section::InOut, Section::Output})
        if (sameWord(w, keyword(s))) {
            out = s;
            return true;
        }
    return false;
}

// Les blancs resserres en une espace, aux bouts otes.
std::string oneLine(std::string_view s) {
    std::string out;
    bool gap = false;
    for (const char c : s) {
        if (isBlank(c)) {
            gap = true;
            continue;
        }
        if (gap && !out.empty()) out += ' ';
        gap = false;
        out += c;
    }
    return out;
}

// Deux morceaux d'un commentaire rattache, bout a bout.
void join(std::string& into, const std::string& piece) {
    if (piece.empty()) return;
    if (!into.empty()) into += ' ';
    into += piece;
}

std::string quoted(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

ScriptDiagnostic fault(int line, int column, int length, std::string message) {
    ScriptDiagnostic d;
    d.severity = ScriptDiagnostic::Severity::Error;
    d.line = line;
    d.column = column;
    d.length = length;
    d.message = std::move(message);
    return d;
}

// La ligne et la colonne d'un octet.
class Lines {
public:
    explicit Lines(std::string_view code) {
        starts_.push_back(0);
        for (std::size_t i = 0; i < code.size(); ++i)
            if (code[i] == '\n') starts_.push_back(i + 1);
    }
    [[nodiscard]] int line(std::size_t at) const noexcept {
        return static_cast<int>(std::upper_bound(starts_.begin(), starts_.end(), at) - starts_.begin());
    }
    [[nodiscard]] int column(std::size_t at) const noexcept {
        return static_cast<int>(at - starts_[static_cast<std::size_t>(line(at) - 1)]) + 1;
    }

private:
    std::vector<std::size_t> starts_;
};

// Ou s'arrete une declaration.
enum class Stop : std::uint8_t {
    Semicolon,   // a son ';'
    Comma,       // a la ',' qui suit son type (l'en-tete d'une fonction)
    Close,       // a la ')' de l'en-tete
    EndVar,      // a END_VAR : son ';' manque
    Keyword,     // a un mot qui ouvre autre chose (VAR..., FUNCTION, END_FUNCTION) : END_VAR manque
    End          // au bout du code
};
struct Statement {
    std::size_t first{0};   // ses jetons [first, last), sans le ';'
    std::size_t last{0};
    std::size_t next{0};    // le jeton d'apres (apres le ';' ou la ',')
    Stop        stop{Stop::End};
};

class Reader {
public:
    Reader(std::string_view code, const std::vector<Token>& toks, std::vector<ScriptDiagnostic>& errors)
        : code_(code), toks_(toks), errors_(errors) {}

    // Un mot-cle : un mot qui ne suit ni un '.' (un membre) ni un '$' (un repere), colles.
    [[nodiscard]] bool keywordAt(std::size_t k) const noexcept {
        if (k >= toks_.size() || toks_[k].kind != TokenKind::Word) return false;
        if (k > 0 && toks_[k - 1].kind == TokenKind::Punct && toks_[k - 1].end == toks_[k].begin) {
            const auto p = text(k - 1);
            if (p == "." || p == "$") return false;
        }
        return true;
    }
    [[nodiscard]] bool isWord(std::size_t k, std::string_view w) const noexcept { return keywordAt(k) && sameWord(text(k), w); }
    [[nodiscard]] bool isSection(std::size_t k, Section& s) const noexcept { return keywordAt(k) && sectionOf(text(k), s); }

    // Un bloc VAR... END_VAR, depuis son mot (le jeton `k`) : le bloc a la fin de
    // `blocks`, ses declarations a la fin de `decls`. Rend le jeton d'apres.
    std::size_t block(std::size_t k, std::vector<Block>& blocks, std::vector<Decl>& decls);
    // FUNCTION Nom [(parametres)] [: Retour] blocs... END_FUNCTION, depuis le jeton `k`.
    std::size_t function(std::size_t k, std::vector<InnerFunction>& out);

private:
    [[nodiscard]] std::string_view text(std::size_t k) const noexcept { return toks_[k].text(code_); }
    [[nodiscard]] bool isPunct(std::size_t k, std::string_view p) const noexcept {
        return k < toks_.size() && toks_[k].kind == TokenKind::Punct && text(k) == p;
    }
    [[nodiscard]] bool isComment(std::size_t k) const noexcept { return k < toks_.size() && toks_[k].kind == TokenKind::Comment; }
    [[nodiscard]] std::size_t skipComments(std::size_t k) const noexcept {
        while (isComment(k)) ++k;
        return k;
    }
    // Rien que des espaces et tabulations entre deux octets (pas de fin de ligne).
    [[nodiscard]] bool sameLineGap(std::size_t from, std::size_t to) const noexcept {
        for (std::size_t i = from; i < to; ++i)
            if (code_[i] != ' ' && code_[i] != '\t') return false;
        return true;
    }
    // Un mot qui arrete un bloc dont END_VAR manque.
    [[nodiscard]] bool stopper(std::size_t k) const noexcept {
        Section s{};
        return isSection(k, s) || isWord(k, "FUNCTION") || isWord(k, "END_FUNCTION");
    }
    // Le texte des jetons [a, b), sans commentaires ; une espace la ou il y avait un blanc.
    [[nodiscard]] std::string flat(std::size_t a, std::size_t b) const {
        std::string out;
        bool gap = false;
        std::size_t prev = npos;
        for (std::size_t k = a; k < b; ++k) {
            if (toks_[k].kind == TokenKind::Comment) {
                gap = true;
                continue;
            }
            if (!out.empty() && (gap || (prev != npos && toks_[k].begin > toks_[prev].end))) out += ' ';
            out += text(k);
            gap = false;
            prev = k;
        }
        return out;
    }
    [[nodiscard]] ScriptDiagnostic statementFault(const Statement& st, std::string message) const {
        const Token& a = toks_[st.first];
        const Token& z = toks_[st.last - 1];
        const int length = z.lastLine == a.line ? static_cast<int>(z.end - a.begin) : 0;
        return fault(a.line, a.column, length, std::move(message));
    }
    void keep(Block& b, std::size_t comment) const {
        if (auto t = commentText(text(comment)); !t.empty()) b.comments.push_back(std::move(t));
    }
    // Les commentaires qui suivent un ';' (ou une ',') sur sa ligne : ceux de la declaration.
    std::string trailing(const Statement& st, std::size_t& q) const {
        std::string out;
        if (st.stop != Stop::Semicolon && st.stop != Stop::Comma) return out;
        const int line = toks_[st.next - 1].line;
        while (isComment(q) && toks_[q].line == line) join(out, commentText(text(q++)));
        return out;
    }

    Statement statement(std::size_t k, bool header) const;
    bool group(const Statement& st, Section section, bool constant, bool retain, std::size_t index, std::vector<Decl>& decls);

    std::string_view                  code_;
    const std::vector<Token>&         toks_;
    std::vector<ScriptDiagnostic>&    errors_;
    std::size_t                       groups_{0};
};

// Les jetons d'une declaration, depuis `k` : jusqu'a son ';' au premier niveau des
// parentheses et des crochets ; dans l'en-tete d'une fonction, aussi jusqu'a la ')'
// qui le ferme ou la ',' qui suit le type (F(a : INT, b : REAL)).
Statement Reader::statement(std::size_t k, bool header) const {
    int depth = 0;
    bool typed = false;
    std::size_t q = k;
    for (; q < toks_.size(); ++q) {
        const Token& t = toks_[q];
        if (t.kind == TokenKind::Word) {
            if (isWord(q, "END_VAR")) return {k, q, q, Stop::EndVar};
            if (stopper(q)) return {k, q, q, Stop::Keyword};
            continue;
        }
        if (t.kind != TokenKind::Punct) continue;
        const auto p = text(q);
        if (p == "(" || p == "[") ++depth;
        else if (p == ")" || p == "]") {
            if (depth > 0) --depth;
            else if (header && p == ")") return {k, q, q, Stop::Close};
        } else if (depth == 0 && p == ";") return {k, q, q + 1, Stop::Semicolon};
        else if (depth == 0 && p == ":") typed = true;
        else if (header && depth == 0 && typed && p == ",") return {k, q, q + 1, Stop::Comma};
    }
    return {k, q, q, Stop::End};
}

// "a, b : T := v" : ses noms a la fin de `decls`, avec ses commentaires de dedans ;
// faux : illisible (rien n'est ajoute).
bool Reader::group(const Statement& st, Section section, bool constant, bool retain, std::size_t index,
                   std::vector<Decl>& decls) {
    int depth = 0;
    std::size_t colon = npos;
    std::size_t assign = npos;
    for (std::size_t q = st.first; q < st.last; ++q) {
        if (toks_[q].kind != TokenKind::Punct) continue;
        const auto p = text(q);
        if (p == "(" || p == "[") ++depth;
        else if (p == ")" || p == "]") {
            if (depth > 0) --depth;
        } else if (depth == 0 && p == ":") {
            if (colon != npos) return false;              // deux ':' : un ';' manque entre deux declarations
            colon = q;
        } else if (depth == 0 && p == ":=" && colon != npos && assign == npos) assign = q;
    }
    if (colon == npos) return false;
    std::vector<std::size_t> names;
    bool want = true;
    for (std::size_t q = st.first; q < colon; ++q) {
        if (toks_[q].kind == TokenKind::Comment) continue;
        if (want && toks_[q].kind == TokenKind::Word) {
            names.push_back(q);
            want = false;
        } else if (!want && isPunct(q, ",")) want = true;
        else return false;
    }
    if (names.empty() || want) return false;
    const std::string type = flat(colon + 1, assign == npos ? st.last : assign);
    const std::string initial = assign == npos ? std::string{} : flat(assign + 1, st.last);
    if (type.empty() || (assign != npos && initial.empty())) return false;
    std::string comment;
    for (std::size_t q = st.first; q < st.last; ++q)
        if (toks_[q].kind == TokenKind::Comment) join(comment, commentText(text(q)));
    const std::size_t g = groups_++;
    for (const std::size_t q : names) {
        Decl d;
        d.name = std::string(text(q));
        d.type = type;
        d.initial = initial;
        d.comment = comment;
        d.section = section;
        d.constant = constant;
        d.retain = retain;
        d.line = toks_[q].line;
        d.column = toks_[q].column;
        d.block = index;
        d.group = g;
        decls.push_back(std::move(d));
    }
    return true;
}

std::size_t Reader::block(std::size_t k, std::vector<Block>& blocks, std::vector<Decl>& decls) {
    Block b;
    sectionOf(text(k), b.section);
    b.begin = toks_[k].begin;
    b.firstLine = toks_[k].line;
    b.closed = false;
    const std::size_t index = blocks.size();
    const std::string word = upper(text(k));
    int head = toks_[k].line;                  // la ligne du mot et de ses qualificatifs
    std::vector<std::size_t> pending;          // des commentaires sur leurs lignes, pas encore rattaches
    std::vector<ScriptDiagnostic> faults;
    std::size_t q = k + 1;
    // Les qualificatifs ; un commentaire sur la ligne du mot : celui du bloc.
    for (; q < toks_.size(); ++q) {
        if (isComment(q)) {
            if (toks_[q].line == head) keep(b, q);
            else pending.push_back(q);
            continue;
        }
        if (!keywordAt(q)) break;
        const auto w = text(q);
        if (sameWord(w, "CONSTANT")) b.constant = true;
        else if (sameWord(w, "RETAIN") || sameWord(w, "PERSISTENT")) b.retain = true;
        else if (!sameWord(w, "NON_RETAIN")) break;
        b.qualifiers.emplace_back(w);
        head = toks_[q].line;
    }
    bool any = false;                          // une declaration lue
    std::size_t end = code_.size();
    for (;;) {
        for (; isComment(q); ++q) {
            if (!any && toks_[q].line == head) keep(b, q);
            else pending.push_back(q);
        }
        if (q >= toks_.size()) break;
        if (isWord(q, "END_VAR")) {
            b.closed = true;
            end = toks_[q].end;
            if (isPunct(q + 1, ";") && sameLineGap(toks_[q].end, toks_[q + 1].begin)) end = toks_[++q].end;
            ++q;
            break;
        }
        if (stopper(q)) {
            end = toks_[q].begin;
            break;
        }
        const Statement st = statement(q, false);
        q = st.next;
        if (st.first == st.last) continue;     // un ';' seul
        any = true;
        // Les commentaires juste au-dessus (sans ligne vide) : les siens ; plus haut : ceux du bloc.
        std::size_t cut = pending.size();
        for (int below = toks_[st.first].line; cut > 0 && toks_[pending[cut - 1]].lastLine + 1 >= below; --cut)
            below = toks_[pending[cut - 1]].line;
        for (std::size_t i = 0; i < cut; ++i) keep(b, pending[i]);
        std::string lead;
        for (std::size_t i = cut; i < pending.size(); ++i) join(lead, commentText(text(pending[i])));
        pending.clear();
        const std::string trail = trailing(st, q);
        const std::size_t from = decls.size();
        if (!group(st, b.section, b.constant, b.retain, index, decls)) {
            faults.push_back(statementFault(st, "d\xC3\xA9" "claration illisible : " + quoted(flat(st.first, st.last))
                                                    + " (nom : TYPE ;)"));
            // Ses commentaires restent au bloc.
            std::string inner;
            for (std::size_t i = st.first; i < st.last; ++i)
                if (isComment(i)) join(inner, commentText(text(i)));
            for (const auto& c : {lead, inner, trail})
                if (!c.empty()) b.comments.push_back(c);
            continue;
        }
        for (std::size_t i = from; i < decls.size(); ++i) {
            std::string c = lead;
            join(c, decls[i].comment);
            join(c, trail);
            decls[i].comment = std::move(c);
        }
        if (st.stop != Stop::Semicolon)
            faults.push_back(statementFault(st, "';' attendu apr\xC3\xA8s la d\xC3\xA9" "claration " + quoted(flat(st.first, st.last))));
    }
    for (const std::size_t c : pending) keep(b, c);
    b.end = end;
    b.lastLine = b.closed ? toks_[q - 1].line : (q > k + 1 ? toks_[q - 1].lastLine : b.firstLine);
    if (!b.closed) {
        // Comme splitDeclarations : un bloc ouvert ne dit que cela (la suite est sans doute du code).
        faults.clear();
        faults.push_back(fault(b.firstLine, toks_[k].column, static_cast<int>(word.size()), word + " sans END_VAR"));
    }
    b.errors = static_cast<int>(faults.size());
    errors_.insert(errors_.end(), faults.begin(), faults.end());
    blocks.push_back(std::move(b));
    return q;
}

std::size_t Reader::function(std::size_t k, std::vector<InnerFunction>& out) {
    InnerFunction f;
    f.begin = toks_[k].begin;
    f.firstLine = toks_[k].line;
    const Token& at = toks_[k];
    std::size_t q = skipComments(k + 1);
    Section s{};
    if (keywordAt(q) && !isSection(q, s) && !isWord(q, "END_FUNCTION")) f.name = std::string(text(q++));
    else errors_.push_back(fault(at.line, at.column, 8, "FUNCTION sans nom"));
    const std::string label = f.name.empty() ? std::string("FUNCTION") : "FUNCTION " + f.name;
    // L'en-tete : (a : T; VAR_IN_OUT b : U, c : V)
    q = skipComments(q);
    if (isPunct(q, "(")) {
        ++q;
        bool closed = false;
        std::string lead;
        for (;;) {
            for (; isComment(q); ++q) join(lead, commentText(text(q)));
            if (q >= toks_.size()) break;
            if (isPunct(q, ")")) {
                closed = true;
                ++q;
                break;
            }
            Section mode = Section::Input;
            bool constant = false;
            if (isSection(q, s) && isParameter(s)) {
                mode = s;
                q = skipComments(q + 1);
            }
            if (isWord(q, "CONSTANT")) {
                constant = true;
                q = skipComments(q + 1);
            }
            const Statement st = statement(q, true);
            q = st.next;
            if (st.first == st.last) {
                if (st.stop == Stop::Semicolon || st.stop == Stop::Comma || st.stop == Stop::Close) continue;
                break;
            }
            const std::string trail = trailing(st, q);
            const std::size_t from = f.decls.size();
            if (group(st, mode, constant, false, kHeader, f.decls)) {
                for (std::size_t i = from; i < f.decls.size(); ++i) {
                    std::string c = lead;
                    join(c, f.decls[i].comment);
                    join(c, trail);
                    f.decls[i].comment = std::move(c);
                }
            } else {
                errors_.push_back(statementFault(st, "param\xC3\xA8tre illisible : " + quoted(flat(st.first, st.last))
                                                         + " (nom : TYPE)"));
            }
            lead.clear();
            if (st.stop != Stop::Semicolon && st.stop != Stop::Comma && st.stop != Stop::Close) break;
        }
        if (!closed)
            errors_.push_back(fault(at.line, at.column, 8, label + " : ')' attendu apr\xC3\xA8s ses param\xC3\xA8tres"));
    }
    // Le retour : ": T" jusqu'au ';', a la fin de la ligne (sauf "ARRAY[..]" puis "OF T"
    // sur la suivante), ou au premier bloc.
    q = skipComments(q);
    if (isPunct(q, ":")) {
        const std::size_t a = q + 1;
        std::size_t e = a;
        std::size_t prev = npos;
        int depth = 0;
        for (; e < toks_.size(); ++e) {
            const Token& t = toks_[e];
            if (t.kind == TokenKind::Comment) continue;
            if (depth == 0 && prev != npos && t.line > toks_[prev].lastLine && !isWord(e, "OF") && !isWord(prev, "OF")
                && !isWord(prev, "TO") && !isWord(prev, "REF_TO"))
                break;
            if (t.kind == TokenKind::Punct) {
                const auto p = text(e);
                if (p == "(" || p == "[") ++depth;
                else if ((p == ")" || p == "]") && depth > 0) --depth;
                else if (depth == 0 && p == ";") break;
            } else if (stopper(e)) break;
            prev = e;
        }
        f.returnType = flat(a, e);
        q = e;
    }
    if (isPunct(q, ";")) ++q;
    // Ses blocs, avant son code.
    for (;;) {
        const std::size_t r = skipComments(q);
        if (!isSection(r, s)) break;
        q = block(r, f.blocks, f.decls);
    }
    while (q < toks_.size() && !isWord(q, "END_FUNCTION")) ++q;
    if (q < toks_.size()) {
        f.end = toks_[q].end;
        f.lastLine = toks_[q].line;
        if (isPunct(q + 1, ";") && sameLineGap(toks_[q].end, toks_[q + 1].begin)) f.end = toks_[++q].end;
        ++q;
    } else {
        errors_.push_back(fault(at.line, at.column, 8, label + " sans END_FUNCTION"));
        f.end = code_.size();
        f.lastLine = toks_.back().lastLine;
    }
    out.push_back(std::move(f));
    return q;
}

} // namespace

std::vector<Token> lex(std::string_view s) {
    std::vector<Token> out;
    const Lines lines(s);
    const std::size_t n = s.size();
    const auto push = [&](TokenKind kind, std::size_t b, std::size_t e) {
        Token t;
        t.kind = kind;
        t.begin = b;
        t.end = e;
        t.line = lines.line(b);
        t.column = lines.column(b);
        t.lastLine = e > b ? lines.line(e - 1) : t.line;
        out.push_back(t);
        return e;
    };
    std::size_t i = 0;
    while (i < n) {
        const char c = s[i];
        if (isBlank(c)) {
            ++i;
            continue;
        }
        if (c == '(' && i + 1 < n && s[i + 1] == '*') {
            const auto e = s.find("*)", i + 2);
            i = push(TokenKind::Comment, i, e == std::string_view::npos ? n : e + 2);
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            const auto e = s.find('\n', i);
            i = push(TokenKind::Comment, i, e == std::string_view::npos ? n : e);
            continue;
        }
        if (c == '\'' || c == '"') {
            std::size_t k = i + 1;
            while (k < n && s[k] != c) k += (s[k] == '$' && k + 1 < n) ? 2 : 1;
            i = push(TokenKind::String, i, k < n ? k + 1 : n);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            std::size_t k = i + 1;
            while (k < n && (identChar(s[k]) || s[k] == '#' || s[k] == '.')) ++k;
            i = push(TokenKind::Number, i, k);
            continue;
        }
        if (identStart(c)) {
            std::size_t k = i + 1;
            while (k < n && identChar(s[k])) ++k;
            if (k < n && s[k] == '#') {                    // T#5s, INT#3, E_Mode#Auto
                ++k;
                while (k < n && (identChar(s[k]) || s[k] == '.' || s[k] == '#')) ++k;
                i = push(TokenKind::Number, i, k);
                continue;
            }
            i = push(TokenKind::Word, i, k);
            continue;
        }
        std::size_t len = 1;
        if (i + 1 < n) {
            const char d = s[i + 1];
            if ((c == ':' && d == '=') || (c == '=' && d == '>') || (c == '<' && (d == '=' || d == '>')) || (c == '>' && d == '=')
                || (c == '*' && d == '*') || (c == '.' && d == '.'))
                len = 2;
        }
        i = push(TokenKind::Punct, i, i + len);
    }
    return out;
}

std::string commentText(std::string_view c) {
    if (c.substr(0, 2) == "(*") {
        c.remove_prefix(2);
        if (c.size() >= 2 && c.substr(c.size() - 2) == "*)") c.remove_suffix(2);
    } else if (c.substr(0, 2) == "//") c.remove_prefix(2);
    return oneLine(c);
}

std::vector<const Decl*> Extract::of(Section s) const {
    std::vector<const Decl*> out;
    for (const auto& d : decls)
        if (d.section == s) out.push_back(&d);
    return out;
}

std::vector<const Decl*> Extract::parameters() const {
    std::vector<const Decl*> out;
    for (const auto& d : decls)
        if (isParameter(d.section)) out.push_back(&d);
    return out;
}

const Decl* Extract::find(std::string_view name) const noexcept {
    for (const auto& d : decls)
        if (sameWord(d.name, name)) return &d;
    return nullptr;
}

Extract extract(std::string_view code) {
    Extract x;
    x.body.assign(code.begin(), code.end());
    const auto toks = lex(code);
    Reader r(code, toks, x.errors);
    for (std::size_t q = 0; q < toks.size();) {
        Section s{};
        if (r.isSection(q, s)) {
            q = r.block(q, x.blocks, x.decls);
            const Block& b = x.blocks.back();
            for (std::size_t i = b.begin; i < b.end && i < x.body.size(); ++i)
                if (x.body[i] != '\n') x.body[i] = ' ';
            continue;
        }
        if (r.isWord(q, "FUNCTION")) {
            q = r.function(q, x.functions);
            continue;
        }
        ++q;
    }
    return x;
}

std::string compose(const Block& b, const std::vector<Decl>& decls, std::size_t index, std::string_view indent) {
    std::string out(keyword(b.section));
    // Les qualificatifs tels qu'ecrits, accordes aux drapeaux du bloc.
    std::vector<std::string> q = b.qualifiers;
    const auto has = [&](std::string_view w) {
        return std::any_of(q.begin(), q.end(), [&](const std::string& x) { return sameWord(x, w); });
    };
    const auto drop = [&](std::string_view w) {
        q.erase(std::remove_if(q.begin(), q.end(), [&](const std::string& x) { return sameWord(x, w); }), q.end());
    };
    if (!b.constant) drop("CONSTANT");
    else if (!has("CONSTANT")) q.insert(q.begin(), "CONSTANT");
    if (!b.retain) {
        drop("RETAIN");
        drop("PERSISTENT");
    } else if (!has("RETAIN") && !has("PERSISTENT")) q.emplace_back("RETAIN");
    for (const auto& w : q) {
        out += ' ';
        out += w;
    }
    // Les commentaires du bloc : sur la ligne du mot ; un texte qui contient "*)" :
    // en // avant END_VAR (rattache a aucune declaration, il reste au bloc).
    std::vector<std::string> late;
    for (const auto& c : b.comments) {
        const std::string t = oneLine(c);
        if (t.empty()) continue;
        if (t.find("*)") != std::string::npos) late.push_back(t);
        else out += " (* " + t + " *)";
    }
    out += '\n';
    for (std::size_t i = 0; i < decls.size();) {
        const Decl& d = decls[i];
        if (d.block != index) {
            ++i;
            continue;
        }
        std::string line(indent);
        line += d.name;
        std::size_t j = i + 1;
        for (; j < decls.size(); ++j) {
            const Decl& e = decls[j];
            if (e.block != index || e.group != d.group || e.type != d.type || e.initial != d.initial || e.comment != d.comment) break;
            line += ", " + e.name;
        }
        line += " : " + d.type;
        if (!d.initial.empty()) line += " := " + d.initial;
        line += ';';
        if (const std::string c = oneLine(d.comment); !c.empty())
            line += c.find("*)") == std::string::npos ? "   (* " + c + " *)" : "   // " + c;
        out += line;
        out += '\n';
        i = j;
    }
    for (const auto& c : late) {
        out += indent;
        out += "// " + c + '\n';
    }
    out += "END_VAR";
    return out;
}

// ---- 1.11.18 (lot 3) : le pont -------------------------------------------------------
std::string_view blockOf(const Declaration& d, Role role) noexcept {
    switch (d.kind) {
        case DeclKind::Constant: return "VAR CONSTANT";
        case DeclKind::Parameter:
            return d.mode == PassMode::InOut ? "VAR_IN_OUT" : d.mode == PassMode::Out ? "VAR_OUTPUT" : "VAR_INPUT";
        case DeclKind::Variable:
            if (role != Role::Script) return "VAR";
            return d.storage == Storage::Execution ? "VAR_TEMP" : "VAR";
    }
    return "VAR";
}

namespace {
// Sur une ligne : le texte reconstruit ne doit pas decaler les lignes du corps.
std::string onOneLine(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        if (c == '\n' || c == '\r') c = ' ';
    return out;
}
} // namespace

Composed composeCode(std::string_view body, const std::vector<Declaration>& decls, Role role,
                     const std::vector<Declaration>* inherited) {
    Composed c;
    std::vector<const Declaration*> all;
    if (inherited)
        for (const auto& d : *inherited)
            if (d.kind == DeclKind::Parameter) all.push_back(&d);
    for (const auto& d : decls) all.push_back(&d);
    if (all.empty()) {
        c.text.assign(body.begin(), body.end());
        return c;
    }
    std::string_view open;
    for (const auto* d : all) {
        const auto block = blockOf(*d, role);
        if (block != open) {
            if (!open.empty()) c.text += "END_VAR ";
            c.text += block;
            c.text += ' ';
            open = block;
        }
        Composed::Span span;
        span.id = d->id;
        span.begin = c.text.size();
        c.text += onOneLine(d->name) + " : " + onOneLine(d->type);
        if (!d->value.empty()) c.text += " := " + onOneLine(d->value);
        c.text += "; ";
        span.end = c.text.size();
        c.spans.push_back(span);
    }
    c.text += "END_VAR ";
    c.prefix = c.text.size();
    c.text.append(body.begin(), body.end());
    return c;
}

bool Composed::inDeclarations(int line, int column) const noexcept {
    return prefix > 0 && line == 1 && column >= 1 && static_cast<std::size_t>(column) <= prefix;
}

int Composed::bodyColumn(int line, int column) const noexcept {
    if (prefix == 0 || line != 1 || column <= 0) return column;
    return static_cast<std::size_t>(column) > prefix ? column - static_cast<int>(prefix) : 1;
}

Id Composed::declarationAt(int line, int column) const noexcept {
    if (!inDeclarations(line, column)) return kNoId;
    const auto at = static_cast<std::size_t>(column - 1);
    for (const auto& s : spans)
        if (at >= s.begin && at < s.end) return s.id;
    return kNoId;
}

std::string codeOf(const Script& s) {
    if (s.decls.empty() || s.lang != ScriptLang::ST) return s.body;
    return composeCode(s.body, s.decls, Role::Script).text;
}

std::string codeOf(const HmiFunction& f) {
    if (f.decls.empty()) return f.body;
    return composeCode(f.body, f.decls, Role::Function).text;
}

const std::string& codeOf(const Script& s, std::string& storage) {
    if (s.decls.empty() || s.lang != ScriptLang::ST) return s.body;
    storage = composeCode(s.body, s.decls, Role::Script).text;
    return storage;
}

const std::string& codeOf(const HmiFunction& f, std::string& storage) {
    if (f.decls.empty()) return f.body;
    storage = composeCode(f.body, f.decls, Role::Function).text;
    return storage;
}

std::string codeOf(const FunctionOverride& o, const HmiFunction* base) {
    const bool inherits = base && std::any_of(base->decls.begin(), base->decls.end(),
                                              [](const Declaration& d) { return d.kind == DeclKind::Parameter; });
    if (o.decls.empty() && !inherits) return o.body;
    return composeCode(o.body, o.decls, Role::Function, base ? &base->decls : nullptr).text;
}

std::string codeOf(const HmiOperator& o) {
    if (o.decls.empty()) return o.body;
    return composeCode(o.body, o.decls, Role::Operator).text;
}

std::vector<ScriptDiagnostic> checkDeclarations(const std::vector<Declaration>& decls, Role role, std::string_view body,
                                                const TypeKnown& knownType, const std::vector<Declaration>* inherited,
                                                std::vector<Declaration>* valid) {
    std::vector<ScriptDiagnostic> out;
    bool ok = true;
    const auto fail = [&](const Declaration& d, const std::string& why) {
        out.push_back(fault(0, 0, 0, "d\xC3\xA9" "claration " + quoted(d.name.empty() ? std::string("?") : d.name) + " : " + why));
        ok = false;
    };
    // Les noms deja pris : les parametres herites (une redefinition), ceux des blocs du corps.
    std::vector<std::string> taken;
    if (inherited)
        for (const auto& d : *inherited)
            if (d.kind == DeclKind::Parameter) taken.push_back(upper(d.name));
    const auto inBody = extract(body);
    for (std::size_t i = 0; i < decls.size(); ++i) {
        const Declaration& d = decls[i];
        ok = true;
        if (d.name.empty()) {
            out.push_back(fault(0, 0, 0, "d\xC3\xA9" "claration sans nom"));
            continue;
        }
        bool ident = identStart(d.name[0]);
        for (const char c : d.name) ident = ident && identChar(c);
        if (!ident) fail(d, "nom illisible (lettres, chiffres, _ ; pas un chiffre en t\xC3\xAAte)");
        else if (isReservedWord(d.name)) fail(d, "nom r\xC3\xA9serv\xC3\xA9 du langage");
        const std::string u = upper(d.name);
        bool twice = std::find(taken.begin(), taken.end(), u) != taken.end();
        for (std::size_t j = 0; j < i && !twice; ++j) twice = sameWord(decls[j].name, d.name);
        if (twice) fail(d, "d\xC3\xA9" "clar\xC3\xA9" "e deux fois");
        else if (inBody.find(d.name))
            fail(d, "d\xC3\xA9" "clar\xC3\xA9" "e deux fois (aussi dans un bloc " + std::string(keyword(inBody.find(d.name)->section))
                        + " du code)");
        if (d.type.find_first_not_of(" \t") == std::string::npos) fail(d, "type manquant");
        else if (!localTypeSupported(d.type) && !richLocalType(d.type, knownType))
            fail(d, "type non pris en charge : " + d.type + " (BOOL, INT, DINT, REAL, TIME, STRING, un tableau, un type IHM...)");
        if (d.kind == DeclKind::Constant && d.value.find_first_not_of(" \t") == std::string::npos) fail(d, "une constante sans valeur");
        if (d.kind == DeclKind::Parameter && role == Role::Script) fail(d, "un param\xC3\xA8tre dans un script (seule une fonction en a)");
        if (d.kind == DeclKind::Parameter && role == Role::Operator)
            fail(d, "un param\xC3\xA8tre dans un op\xC3\xA9rateur (ses op\xC3\xA9randes sont A et B)");
        if (d.kind == DeclKind::Parameter && inherited)
            fail(d, "un param\xC3\xA8tre dans une red\xC3\xA9" "finition (elle garde ceux de sa fonction)");
        if (d.kind == DeclKind::Variable && role != Role::Script && d.storage != Storage::Execution)
            fail(d, "une fonction n'a pas de m\xC3\xA9moire : sa variable repart \xC3\xA0 chaque appel (stockage Ex\xC3\xA9" "cution)");
        if (ok && valid) valid->push_back(d);
    }
    return out;
}

std::string parameterSignature(const Extract& x) {
    std::string out;
    for (const Decl* d : x.parameters()) {
        if (!out.empty()) out += ' ';
        if (d->section != Section::Input) {
            out += keyword(d->section);
            out += ' ';
        }
        out += d->name + " : " + d->type;
        if (!d->initial.empty()) out += " := " + d->initial;
        out += ';';
    }
    return out;
}

} // namespace hmi::decl
