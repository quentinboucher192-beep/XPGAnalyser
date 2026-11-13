// hmi/HmiScriptC.cpp - 1.50.0 : les scripts C et C++ traduits dans le ST de l'IHM, ligne pour ligne, puis
// executes par le moteur des scripts (voir HmiScriptC.hpp). 1.50.1 : les types du C suivis pendant la traduction
// (struct, enum, typedef, classes, pointeurs dans un tableau, new et delete, lambdas) et ce qu'il faut au debogueur.
#include "HmiScriptC.hpp"

#include "../sim/DialectLib.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>

namespace hmi::cscript {

namespace {

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Une faute du texte C : sa ligne, en francais. Levee dans la traduction, rattrapee par translate().
struct Fault {
    int         line;
    std::string message;
};

// ------------------------------------------------------------------ les jetons --
struct Tok {
    enum class K : std::uint8_t { Name, Number, String, Char, Punct, End } k{K::End};
    std::string  text;           // Name : le nom (std::sqrt, T_MODE::Auto) ; Number : le litteral ST ; String : les octets ; Punct
    int          line{1};
    bool         real{false};    // Number : un reel
    std::int64_t value{0};       // Number entier, Char : sa valeur
    bool         isFloat{false}; // Number reel : suffixe f (un float)
};

bool nameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool namePart(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Une macro : #define N 10 (un objet) ou #define CARRE(x) ((x) * (x)) (une fonction).
struct Macro {
    bool                     function{false};
    std::vector<std::string> params;
    std::vector<Tok>         body;
    int                      line{0};
};
using Macros = std::map<std::string, Macro>;

class Lexer {
public:
    Lexer(std::string_view src, Macros& macros) : s_(src), macros_(macros) {}

    std::vector<Tok> run() {
        std::vector<Tok> out;
        bool lineStart = true;
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (c == '\n') {
                ++line_;
                ++i_;
                lineStart = true;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
                ++i_;
                continue;
            }
            // Une partie ecartee (#if 0, #ifdef d'une macro absente) : ses lignes passent, sauf ses directives.
            if (!active()) {
                if (c == '#' && lineStart) {
                    directive();
                    continue;
                }
                inactive_.push_back(line_);
                while (i_ < s_.size() && s_[i_] != '\n') ++i_;
                continue;
            }
            if (c == '/' && at(1) == '/') {
                while (i_ < s_.size() && s_[i_] != '\n') ++i_;
                continue;
            }
            if (c == '/' && at(1) == '*') {
                const int from = line_;
                i_ += 2;
                while (i_ < s_.size() && !(s_[i_] == '*' && at(1) == '/')) {
                    if (s_[i_] == '\n') ++line_;
                    ++i_;
                }
                if (i_ >= s_.size()) throw Fault{from, "commentaire /* non ferm\xC3\xA9"};
                i_ += 2;
                continue;
            }
            if (c == '#' && lineStart && !sub_) {
                directive();
                continue;
            }
            lineStart = false;
            if (c == '$') {                              // un repere ($V[1]$) : transparent, comme en ST
                ++i_;
                continue;
            }
            if (nameStart(c) || (c == ':' && at(1) == ':' && nameStart(at(2)))) {
                out.push_back(name());
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && std::isdigit(static_cast<unsigned char>(at(1))))) {
                out.push_back(number());
                continue;
            }
            if (c == '"') {
                out.push_back(text());
                continue;
            }
            if (c == '\'') {
                out.push_back(character());
                continue;
            }
            out.push_back(punct());
        }
        if (!conds_.empty()) throw Fault{condLines_.back(), "#if / #ifdef sans #endif"};
        Tok end;
        end.k = Tok::K::End;
        end.line = line_;
        out.push_back(end);
        return out;
    }

private:
    char at(std::size_t ahead) const { return i_ + ahead < s_.size() ? s_[i_ + ahead] : '\0'; }
    bool active() const {
        for (const auto& c : conds_)
            if (!c.taking) return false;
        return true;
    }

    Tok name() {
        Tok t;
        t.k = Tok::K::Name;
        t.line = line_;
        if (s_[i_] == ':') i_ += 2;                      // ::nom : le nom global
        std::size_t a = i_;
        while (i_ < s_.size() && namePart(s_[i_])) ++i_;
        t.text = std::string(s_.substr(a, i_ - a));
        // std::sqrt, T_MODE::Auto : un seul nom
        while (at(0) == ':' && at(1) == ':' && nameStart(at(2))) {
            i_ += 2;
            a = i_;
            while (i_ < s_.size() && namePart(s_[i_])) ++i_;
            t.text += "::" + std::string(s_.substr(a, i_ - a));
        }
        return t;
    }

    Tok number() {
        Tok t;
        t.k = Tok::K::Number;
        t.line = line_;
        std::string digits;
        int base = 10;
        if (s_[i_] == '0' && (at(1) == 'x' || at(1) == 'X')) {
            base = 16;
            i_ += 2;
            while (i_ < s_.size() && (std::isxdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '\'')) {
                if (s_[i_] != '\'') digits += s_[i_];
                ++i_;
            }
        } else if (s_[i_] == '0' && (at(1) == 'b' || at(1) == 'B')) {
            base = 2;
            i_ += 2;
            while (i_ < s_.size() && (s_[i_] == '0' || s_[i_] == '1' || s_[i_] == '\'')) {
                if (s_[i_] != '\'') digits += s_[i_];
                ++i_;
            }
        } else {
            bool dot = false, exp = false;
            while (i_ < s_.size()) {
                const char c = s_[i_];
                if (std::isdigit(static_cast<unsigned char>(c)) || c == '\'') {
                    if (c != '\'') digits += c;
                } else if (c == '.' && !dot && !exp) {
                    dot = true;
                    digits += c;
                } else if ((c == 'e' || c == 'E') && !exp
                           && (std::isdigit(static_cast<unsigned char>(at(1))) || ((at(1) == '+' || at(1) == '-') && std::isdigit(static_cast<unsigned char>(at(2)))))) {
                    exp = true;
                    digits += 'E';
                    if (at(1) == '+' || at(1) == '-') {
                        digits += at(1);
                        ++i_;
                    }
                } else {
                    break;
                }
                ++i_;
            }
            t.real = dot || exp;
            if (!t.real && digits.size() > 1 && digits[0] == '0') base = 8;
        }
        bool floatSuffix = false;
        while (i_ < s_.size() && std::isalpha(static_cast<unsigned char>(s_[i_]))) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s_[i_])));
            if (c == 'f' && base == 10) floatSuffix = true;
            else if (c != 'u' && c != 'l') throw Fault{line_, "nombre illisible : suffixe '" + std::string(1, s_[i_]) + "'"};
            ++i_;
        }
        if (digits.empty()) throw Fault{line_, "nombre illisible"};
        if (t.real || floatSuffix) {
            t.real = true;
            t.isFloat = floatSuffix;
            std::string d = digits;
            const auto e = d.find('E');
            std::string mant = e == std::string::npos ? d : d.substr(0, e);
            const std::string ex = e == std::string::npos ? std::string{} : d.substr(e);
            if (mant.find('.') == std::string::npos) mant += ".0";
            if (mant.front() == '.') mant = "0" + mant;
            if (mant.back() == '.') mant += "0";
            t.text = mant + ex;
            return t;
        }
        t.value = static_cast<std::int64_t>(std::strtoull(digits.c_str(), nullptr, base));
        t.text = base == 16 ? "16#" + digits : base == 2 ? "2#" + digits : base == 8 ? "8#" + digits.substr(1) : digits;
        return t;
    }

    // Un echappement du C (\n, \x41, \101...) : l'octet.
    char escape() {
        ++i_;                                            // la barre
        if (i_ >= s_.size()) throw Fault{line_, "\\ en fin de texte"};
        const char c = s_[i_++];
        switch (c) {
            case 'n': return '\n';
            case 't': return '\t';
            case 'r': return '\r';
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
                int v = c - '0';
                for (int k = 0; k < 2 && i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '7'; ++k) v = v * 8 + (s_[i_++] - '0');
                return static_cast<char>(v);
            }
            case 'x': {
                int v = 0, n = 0;
                while (i_ < s_.size() && std::isxdigit(static_cast<unsigned char>(s_[i_])) && n < 2) {
                    const char h = s_[i_++];
                    v = v * 16 + (std::isdigit(static_cast<unsigned char>(h)) ? h - '0' : std::tolower(static_cast<unsigned char>(h)) - 'a' + 10);
                    ++n;
                }
                return static_cast<char>(v);
            }
            case 'a': return '\a';
            case 'b': return '\b';
            case 'f': return '\f';
            case 'v': return '\v';
            default: return c;                           // \\ \" \' \?
        }
    }

    Tok text() {
        Tok t;
        t.k = Tok::K::String;
        t.line = line_;
        ++i_;
        while (true) {
            if (i_ >= s_.size() || s_[i_] == '\n') throw Fault{t.line, "cha\xC3\xAEne non ferm\xC3\xA9" "e"};
            if (s_[i_] == '"') break;
            if (s_[i_] == '\\') t.text += escape();
            else t.text += s_[i_++];
        }
        ++i_;
        return t;
    }

    Tok character() {
        Tok t;
        t.k = Tok::K::Char;
        t.line = line_;
        ++i_;
        if (i_ >= s_.size() || s_[i_] == '\n' || s_[i_] == '\'') throw Fault{t.line, "caract\xC3\xA8re vide ou non ferm\xC3\xA9"};
        const char c = s_[i_] == '\\' ? escape() : s_[i_++];
        if (i_ >= s_.size() || s_[i_] != '\'') throw Fault{t.line, "un caract\xC3\xA8re s'\xC3\xA9" "crit 'a' (un seul)"};
        ++i_;
        t.value = static_cast<unsigned char>(c);
        t.text = std::to_string(t.value);
        return t;
    }

    Tok punct() {
        static const char* const kLong[] = {"<<=", ">>=", "...", "->*", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
                                            "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "::", "##"};
        Tok t;
        t.k = Tok::K::Punct;
        t.line = line_;
        for (const char* p : kLong) {
            const std::string_view w(p);
            if (s_.substr(i_, w.size()) == w) {
                t.text = std::string(w);
                i_ += w.size();
                if (t.text == "->*") throw Fault{line_, "->* (un pointeur de membre) n'est pas pris en charge"};
                return t;
            }
        }
        const char c = s_[i_];
        if (std::string_view("{}[]();,.?:~!%^&*-+=<>|/#").find(c) == std::string_view::npos)
            throw Fault{line_, "caract\xC3\xA8re inattendu : '" + std::string(1, c) + "'"};
        t.text = std::string(1, c);
        ++i_;
        return t;
    }

    // ---- le preprocesseur
    struct Cond {
        bool taking{true};       // la partie en cours est prise
        bool done{false};        // une partie a deja ete prise (#elif, #else n'en prennent plus)
        bool parentOn{true};     // le groupe englobant est pris
    };

    // La valeur d'une condition #if : des nombres, defined(X), des macros (une macro sans valeur ou absente : 0),
    // ! ~ * / % + - << >> < > <= >= == != & ^ | && || ?: et des parentheses.
    class CondEval {
    public:
        CondEval(const std::string& s, const Macros& m, int line) : s_(s), m_(m), line_(line) {}
        std::int64_t run() {
            const std::int64_t v = ternary();
            skip();
            if (k_ < s_.size()) throw Fault{line_, "#if : la condition ne se lit pas (" + s_ + ")"};
            return v;
        }
    private:
        void skip() { while (k_ < s_.size() && (s_[k_] == ' ' || s_[k_] == '\t')) ++k_; }
        bool eat(const char* p) {
            skip();
            const std::string_view w(p);
            if (s_.compare(k_, w.size(), w) != 0) return false;
            // < n'est pas <=, & n'est pas &&
            if (w.size() == 1 && k_ + 1 < s_.size() && (s_[k_ + 1] == '=' || (s_[k_ + 1] == s_[k_] && std::string_view("&|<>").find(s_[k_]) != std::string_view::npos))) return false;
            k_ += w.size();
            return true;
        }
        std::int64_t ternary() {
            const std::int64_t c = binary(0);
            if (!eat("?")) return c;
            const std::int64_t a = ternary();
            if (!eat(":")) throw Fault{line_, "#if : ':' attendu"};
            const std::int64_t b = ternary();
            return c ? a : b;
        }
        std::int64_t binary(int level) {
            static const std::vector<std::vector<const char*>> ops = {{"||"}, {"&&"}, {"|"}, {"^"}, {"&"}, {"==", "!="}, {"<=", ">=", "<", ">"},
                                                                      {"<<", ">>"}, {"+", "-"}, {"*", "/", "%"}};
            if (level >= static_cast<int>(ops.size())) return unary();
            std::int64_t l = binary(level + 1);
            while (true) {
                const char* found = nullptr;
                for (const char* o : ops[static_cast<std::size_t>(level)])
                    if (eat(o)) {
                        found = o;
                        break;
                    }
                if (!found) return l;
                const std::int64_t r = binary(level + 1);
                const std::string o(found);
                if (o == "||") l = l || r;
                else if (o == "&&") l = l && r;
                else if (o == "|") l = l | r;
                else if (o == "^") l = l ^ r;
                else if (o == "&") l = l & r;
                else if (o == "==") l = l == r;
                else if (o == "!=") l = l != r;
                else if (o == "<=") l = l <= r;
                else if (o == ">=") l = l >= r;
                else if (o == "<") l = l < r;
                else if (o == ">") l = l > r;
                else if (o == "<<") l = l << (r & 63);
                else if (o == ">>") l = l >> (r & 63);
                else if (o == "+") l = l + r;
                else if (o == "-") l = l - r;
                else if (o == "*") l = l * r;
                else if (r == 0) throw Fault{line_, "#if : division par z\xC3\xA9ro"};
                else if (o == "/") l = l / r;
                else l = l % r;
            }
        }
        std::int64_t unary() {
            if (eat("!")) return !unary();
            if (eat("~")) return ~unary();
            if (eat("-")) return -unary();
            if (eat("+")) return unary();
            if (eat("(")) {
                const std::int64_t v = ternary();
                if (!eat(")")) throw Fault{line_, "#if : ')' attendu"};
                return v;
            }
            skip();
            if (k_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[k_]))) {
                std::size_t e = k_;
                while (e < s_.size() && std::isalnum(static_cast<unsigned char>(s_[e]))) ++e;
                std::string d = s_.substr(k_, e - k_);
                k_ = e;
                while (!d.empty() && (d.back() == 'u' || d.back() == 'U' || d.back() == 'l' || d.back() == 'L')) d.pop_back();
                return static_cast<std::int64_t>(std::strtoll(d.c_str(), nullptr, 0));
            }
            if (k_ < s_.size() && nameStart(s_[k_])) {
                std::size_t e = k_;
                while (e < s_.size() && namePart(s_[e])) ++e;
                const std::string w = s_.substr(k_, e - k_);
                k_ = e;
                if (w == "defined") {
                    const bool paren = eat("(");
                    skip();
                    std::size_t f = k_;
                    while (f < s_.size() && namePart(s_[f])) ++f;
                    const std::string n = s_.substr(k_, f - k_);
                    k_ = f;
                    if (paren && !eat(")")) throw Fault{line_, "#if defined(...) : ')' attendu"};
                    return m_.count(n) ? 1 : 0;
                }
                if (w == "true") return 1;
                const auto it = m_.find(w);
                if (it == m_.end() || it->second.function || it->second.body.size() != 1 || it->second.body[0].k != Tok::K::Number || it->second.body[0].real)
                    return 0;
                return it->second.body[0].value;
            }
            throw Fault{line_, "#if : la condition ne se lit pas (" + s_ + ")"};
        }
        const std::string& s_;
        const Macros&      m_;
        int                line_;
        std::size_t        k_{0};
    };

    // #include, #pragma : ignores ; #define, #undef ; #if, #ifdef, #ifndef, #elif, #else, #endif ; #error ; le reste : refuse.
    void directive() {
        const int line = line_;
        std::string body;
        ++i_;
        while (i_ < s_.size() && s_[i_] != '\n') {
            if (s_[i_] == '\\' && at(1) == '\n') {
                i_ += 2;
                ++line_;
                body += ' ';
                continue;
            }
            if (s_[i_] == '\\' && at(1) == '\r' && at(2) == '\n') {
                i_ += 3;
                ++line_;
                body += ' ';
                continue;
            }
            if (s_[i_] == '/' && at(1) == '/') {               // un commentaire a la fin de la directive
                while (i_ < s_.size() && s_[i_] != '\n') ++i_;
                break;
            }
            if (s_[i_] == '/' && at(1) == '*') {
                i_ += 2;
                while (i_ < s_.size() && !(s_[i_] == '*' && at(1) == '/')) {
                    if (s_[i_] == '\n') ++line_;
                    ++i_;
                }
                if (i_ < s_.size()) i_ += 2;
                body += ' ';
                continue;
            }
            body += s_[i_++];
        }
        std::size_t k = 0;
        while (k < body.size() && (body[k] == ' ' || body[k] == '\t')) ++k;
        std::size_t e = k;
        while (e < body.size() && namePart(body[e])) ++e;
        const std::string word = body.substr(k, e - k);
        std::string rest = body.substr(e);
        while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t' || rest.back() == '\r')) rest.pop_back();
        const auto firstName = [&]() {
            std::size_t a = 0;
            while (a < rest.size() && (rest[a] == ' ' || rest[a] == '\t')) ++a;
            std::size_t b = a;
            while (b < rest.size() && namePart(rest[b])) ++b;
            return rest.substr(a, b - a);
        };
        // les conditions d'abord : elles comptent meme dans une partie ecartee
        if (word == "if" || word == "ifdef" || word == "ifndef") {
            Cond c;
            c.parentOn = active();
            if (c.parentOn) {
                bool v = false;
                if (word == "if") v = CondEval(rest, macros_, line).run() != 0;
                else {
                    const std::string n = firstName();
                    if (n.empty()) throw Fault{line, "#" + word + " : un nom est attendu"};
                    v = (macros_.count(n) > 0) == (word == "ifdef");
                }
                c.taking = v;
                c.done = v;
            } else {
                c.taking = false;
                c.done = true;
            }
            conds_.push_back(c);
            condLines_.push_back(line);
            return;
        }
        if (word == "elif" || word == "else" || word == "endif") {
            if (conds_.empty()) throw Fault{line, "#" + word + " sans #if"};
            auto& c = conds_.back();
            if (word == "endif") {
                conds_.pop_back();
                condLines_.pop_back();
                return;
            }
            if (word == "else") {
                c.taking = c.parentOn && !c.done;
                c.done = true;
                return;
            }
            if (c.parentOn && !c.done && CondEval(rest, macros_, line).run() != 0) {
                c.taking = true;
                c.done = true;
            } else {
                c.taking = false;
            }
            return;
        }
        if (!active()) return;
        if (word == "include" || word == "pragma" || word.empty() || word == "line" || word == "warning") return;
        if (word == "error") throw Fault{line, "#error" + rest};
        if (word == "undef") {
            macros_.erase(firstName());
            return;
        }
        if (word == "define") {
            std::size_t a = e;
            while (a < body.size() && (body[a] == ' ' || body[a] == '\t')) ++a;
            std::size_t b = a;
            while (b < body.size() && namePart(body[b])) ++b;
            const std::string nm = body.substr(a, b - a);
            if (nm.empty() || !nameStart(nm[0])) throw Fault{line, "#define : un nom est attendu (#define N 10)"};
            Macro m;
            m.line = line;
            std::size_t from = b;
            if (b < body.size() && body[b] == '(') {         // une macro a parametres
                m.function = true;
                std::size_t c = b + 1;
                while (true) {
                    while (c < body.size() && (body[c] == ' ' || body[c] == '\t')) ++c;
                    if (c < body.size() && body[c] == ')') {
                        ++c;
                        break;
                    }
                    if (body.compare(c, 3, "...") == 0)
                        throw Fault{line, "#define " + nm + "(...) : une macro \xC3\xA0 param\xC3\xA8tres variables n'est pas prise en charge"};
                    std::size_t d = c;
                    while (d < body.size() && namePart(body[d])) ++d;
                    if (d == c) throw Fault{line, "#define " + nm + "( : un nom de param\xC3\xA8tre est attendu"};
                    m.params.push_back(body.substr(c, d - c));
                    c = d;
                    while (c < body.size() && (body[c] == ' ' || body[c] == '\t')) ++c;
                    if (c < body.size() && body[c] == ',') {
                        ++c;
                        continue;
                    }
                    if (c < body.size() && body[c] == ')') {
                        ++c;
                        break;
                    }
                    throw Fault{line, "#define " + nm + "(...) : ',' ou ')' attendu"};
                }
                from = c;
            }
            Macros none;
            Lexer sub(std::string_view(body).substr(from), none);
            sub.line_ = line;
            sub.sub_ = true;
            auto toks = sub.run();
            toks.pop_back();                             // la fin
            for (const auto& t : toks)
                if (t.k == Tok::K::Punct && (t.text == "#" || t.text == "##"))
                    throw Fault{line, "#define " + nm + " : # et ## (coller, mettre entre guillemets) ne sont pas pris en charge"};
            m.body = std::move(toks);
            macros_[nm] = std::move(m);
            return;
        }
        throw Fault{line, "#" + word + " : directive non prise en charge (#include, #define, #undef, #if, #ifdef, #ifndef, #else, #endif le sont)"};
    }

    std::string_view   s_;
    Macros&            macros_;
    std::size_t        i_{0};
    int                line_{1};
    bool               sub_{false};       // le texte d'un #define : pas de directive dedans
public:
    std::vector<int>   inactive_;         // les lignes ecartees (#if 0...)
private:
    std::vector<Cond>  conds_;
    std::vector<int>   condLines_;
};

// Les macros remplacees : un nom de macro par son texte (une macro a parametres, ses arguments mis a la place de ses
// parametres), puis relu (une macro n'est pas remplacee dans son propre texte). Les jetons gardent la ligne de l'appel.
void expandInto(const std::vector<Tok>& in, std::size_t& k, std::size_t end, const Macros& macros, std::set<std::string>& hide,
                std::vector<Tok>& out, int depth) {
    if (depth > 64) throw Fault{in[std::min(k, in.size() - 1)].line, "macro trop imbriqu\xC3\xA9" "e (r\xC3\xA9" "cursive ?)"};
    while (k < end) {
        const Tok& t = in[k];
        const auto it = t.k == Tok::K::Name && !hide.count(t.text) ? macros.find(t.text) : macros.end();
        if (it == macros.end()) {
            out.push_back(t);
            ++k;
            continue;
        }
        const Macro& m = it->second;
        const int line = t.line;
        std::vector<Tok> body;
        if (!m.function) {
            body = m.body;
            ++k;
        } else {
            if (k + 1 >= end || in[k + 1].k != Tok::K::Punct || in[k + 1].text != "(") {   // CARRE sans ( : un nom
                out.push_back(t);
                ++k;
                continue;
            }
            // les arguments, separes par les virgules du premier niveau
            std::vector<std::vector<Tok>> args(1);
            std::size_t j = k + 2;
            int paren = 0;
            for (; j < end; ++j) {
                const Tok& x = in[j];
                if (x.k == Tok::K::End) throw Fault{line, t.text + "( : la parenth\xC3\xA8se de la macro n'est pas ferm\xC3\xA9" "e"};
                if (x.k == Tok::K::Punct && (x.text == "(" || x.text == "[" || x.text == "{")) ++paren;
                if (x.k == Tok::K::Punct && (x.text == ")" || x.text == "]" || x.text == "}")) {
                    if (paren == 0 && x.text == ")") break;
                    --paren;
                }
                if (paren == 0 && x.k == Tok::K::Punct && x.text == ",") {
                    args.emplace_back();
                    continue;
                }
                args.back().push_back(x);
            }
            if (j >= end) throw Fault{line, t.text + "( : la parenth\xC3\xA8se de la macro n'est pas ferm\xC3\xA9" "e"};
            k = j + 1;
            if (args.size() == 1 && args[0].empty() && m.params.empty()) args.clear();
            if (args.size() != m.params.size())
                throw Fault{line, t.text + " : " + std::to_string(m.params.size()) + " argument(s) attendu(s), " + std::to_string(args.size()) + " donn\xC3\xA9(s)"};
            // chaque argument remplace d'abord (comme le C), puis mis a la place de son parametre
            std::vector<std::vector<Tok>> ready;
            for (const auto& a : args) {
                std::vector<Tok> x;
                std::size_t q = 0;
                expandInto(a, q, a.size(), macros, hide, x, depth + 1);
                ready.push_back(std::move(x));
            }
            for (const auto& b : m.body) {
                bool done = false;
                if (b.k == Tok::K::Name)
                    for (std::size_t p = 0; p < m.params.size(); ++p)
                        if (m.params[p] == b.text) {
                            for (const auto& x : ready[p]) body.push_back(x);
                            done = true;
                            break;
                        }
                if (!done) body.push_back(b);
            }
        }
        for (auto& b : body) b.line = line;
        hide.insert(t.text);
        std::size_t q = 0;
        expandInto(body, q, body.size(), macros, hide, out, depth + 1);
        hide.erase(t.text);
    }
}

// ------------------------------------------------------------------ les types --
const std::map<std::string, std::string>& stElementary() {
    static const std::map<std::string, std::string> k = {
        {"BOOL", "BOOL"},   {"SINT", "SINT"},   {"INT", "INT"},     {"DINT", "DINT"},   {"LINT", "LINT"},   {"USINT", "USINT"},
        {"UINT", "UINT"},   {"UDINT", "UDINT"}, {"ULINT", "ULINT"}, {"BYTE", "BYTE"},   {"WORD", "WORD"},   {"DWORD", "DWORD"},
        {"LWORD", "LWORD"}, {"REAL", "REAL"},   {"LREAL", "LREAL"}, {"STRING", "STRING"}, {"WSTRING", "WSTRING"}, {"TIME", "TIME"},
        {"DATE", "DATE"},   {"TOD", "TOD"},     {"DT", "DT"},       {"CHAR", "CHAR"}};
    return k;
}
const std::map<std::string, std::string>& cNamedTypes() {
    static const std::map<std::string, std::string> k = {
        {"int8_t", "SINT"},    {"int16_t", "INT"},     {"int32_t", "DINT"},    {"int64_t", "LINT"},     {"uint8_t", "USINT"},
        {"uint16_t", "UINT"},  {"uint32_t", "UDINT"},  {"uint64_t", "ULINT"},  {"size_t", "UDINT"},     {"std::size_t", "UDINT"},
        {"std::int8_t", "SINT"}, {"std::int16_t", "INT"}, {"std::int32_t", "DINT"}, {"std::int64_t", "LINT"},
        {"std::uint8_t", "USINT"}, {"std::uint16_t", "UINT"}, {"std::uint32_t", "UDINT"}, {"std::uint64_t", "ULINT"},
        {"std::string", "STRING"}, {"string", "STRING"}, {"_Bool", "BOOL"}, {"ptrdiff_t", "DINT"}, {"std::ptrdiff_t", "DINT"},
        {"intptr_t", "LINT"}, {"uintptr_t", "ULINT"}};
    return k;
}
bool typeKeyword(const std::string& w) {
    static const std::set<std::string> k = {"void", "char", "short", "int", "long", "float", "double", "bool", "_Bool", "signed",
                                            "unsigned", "auto", "const", "constexpr", "static", "volatile", "register", "extern",
                                            "inline", "struct", "enum", "union", "wchar_t", "class", "virtual", "explicit", "mutable"};
    return k.count(w) > 0;
}
// Ce qu'un C ou un C++ ecrit et que le moteur ne prend pas : la raison, dite a sa ligne.
std::string refusedWord(const std::string& w) {
    static const std::map<std::string, std::string> k = {
        {"union", "union n'est pas pris en charge (une struct l'est)"},
        {"namespace", "namespace n'est pas pris en charge (les espaces de noms du projet le sont, en ST)"},
        {"template", "template n'est pas pris en charge (une fonction ou une classe par type)"},
        {"typename", "typename n'est pas pris en charge (pas de template)"},
        {"try", "try / catch n'est pas pris en charge : les exceptions du C++ non plus (un script ST a TRY ... CATCH)"},
        {"catch", "try / catch n'est pas pris en charge : les exceptions du C++ non plus (un script ST a TRY ... CATCH)"},
        {"throw", "throw n'est pas pris en charge : les exceptions du C++ non plus (un script ST a LEVER)"},
        {"goto", "goto n'est pas pris en charge (break, continue, return)"},
        {"operator", "operator : la surcharge des op\xC3\xA9rateurs n'est pas prise en charge (une m\xC3\xA9thode nomm\xC3\xA9" "e l'est)"},
        {"asm", "asm n'est pas pris en charge"},
        {"realloc", "realloc n'est pas pris en charge (un nouveau malloc, puis free)"},
        {"dynamic_cast", "dynamic_cast n'est pas pris en charge ici (une m\xC3\xA9thode virtuelle l'est)"},
        {"reinterpret_cast", "reinterpret_cast n'est pas pris en charge"},
        {"friend", "friend n'est pas pris en charge (une m\xC3\xA9thode publique l'est)"},
        {"co_await", "co_await n'est pas pris en charge"},
        {"co_yield", "co_yield n'est pas pris en charge"},
        {"co_return", "co_return n'est pas pris en charge"},
        {"setjmp", "setjmp n'est pas pris en charge"},
        {"longjmp", "longjmp n'est pas pris en charge"},
        {"wchar_t", "wchar_t n'est pas pris en charge (char, std::string)"}};
    const auto it = k.find(w);
    return it == k.end() ? std::string{} : w + " : " + it->second;
}

// Les fonctions de la bibliotheque du C et du C++ : leur nom ST.
const std::map<std::string, std::string>& libraryCalls() {
    static const std::map<std::string, std::string> k = {
        {"sqrt", "SQRT"}, {"sqrtf", "SQRT"}, {"fabs", "ABS"}, {"fabsf", "ABS"}, {"abs", "ABS"}, {"labs", "ABS"}, {"llabs", "ABS"},
        {"pow", "EXPT"}, {"powf", "EXPT"}, {"exp", "EXP"}, {"expf", "EXP"}, {"log", "LN"}, {"logf", "LN"}, {"log10", "LOG"},
        {"sin", "SIN"}, {"cos", "COS"}, {"tan", "TAN"}, {"asin", "ASIN"}, {"acos", "ACOS"}, {"atan", "ATAN"}, {"atan2", "ATAN2"},
        {"sinf", "SIN"}, {"cosf", "COS"}, {"tanf", "TAN"}, {"floor", "FLOOR"}, {"floorf", "FLOOR"}, {"ceil", "CEIL"}, {"ceilf", "CEIL"},
        {"round", "ROUND"}, {"roundf", "ROUND"}, {"trunc", "TRUNC"}, {"truncf", "TRUNC"}, {"fmod", "FMOD"}, {"hypot", "HYPOT"},
        {"fmin", "MIN"}, {"fmax", "MAX"}, {"fminf", "MIN"}, {"fmaxf", "MAX"}, {"min", "MIN"}, {"max", "MAX"}, {"strlen", "LEN"},
        {"to_string", "TO_STRING"}, {"atoi", "STRING_TO_DINT"}, {"atol", "STRING_TO_DINT"}, {"atof", "STRING_TO_REAL"},
        {"stoi", "STRING_TO_DINT"}, {"stol", "STRING_TO_DINT"}, {"stof", "STRING_TO_REAL"}, {"stod", "STRING_TO_REAL"}};
    return k;
}
std::string mappedCall(const std::string& name) {
    std::string n = name;
    if (n.rfind("std::", 0) == 0) n = n.substr(5);
    const auto& k = libraryCalls();
    const auto it = k.find(n);
    return it == k.end() ? std::string{} : it->second;
}
// Le type rendu par une fonction de la bibliotheque (son nom ST) : un reel, un entier, un texte.
enum class LibResult : std::uint8_t { Real, Int, Text, Same };
LibResult libraryResult(const std::string& st) {
    if (st == "LEN" || st == "STRING_TO_DINT") return LibResult::Int;
    if (st == "TO_STRING") return LibResult::Text;
    if (st == "ABS" || st == "MIN" || st == "MAX") return LibResult::Same;
    return LibResult::Real;
}

// Un texte du C (ses octets) en litteral ST : 'a$'b', $N, $T, $$.
std::string stString(const std::string& raw) {
    std::string out = "'";
    for (const char c : raw) {
        switch (c) {
            case '\'': out += "$'"; break;
            case '$': out += "$$"; break;
            case '\n': out += "$N"; break;
            case '\t': out += "$T"; break;
            case '\r': out += "$R"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* hex = "0123456789ABCDEF";
                    out += '$';
                    out += hex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                    out += hex[static_cast<unsigned char>(c) & 0xF];
                } else {
                    out += c;
                }
        }
    }
    return out + "'";
}

// ------------------------------------------------------------ les types du C --
// Un type du C vu par la traduction : sa forme (un entier, un reel, une structure, un tableau, un pointeur...), son
// type ST, son texte C (le debogueur le montre) et sa taille (sizeof : celle d'une machine 64 bits, alignee).
struct CT;
using CTP = std::shared_ptr<const CT>;
struct CT {
    enum class K : std::uint8_t { Unknown, Void, Int, Real, Bool, Text, Record, Array, Pointer, Vector, Func };
    K            k{K::Unknown};
    std::string  st;              // le type ST
    std::string  shown;           // le type C ecrit
    int          size{0}, align{1};
    bool         isUnsigned{false}, isChar{false};
    std::string  rec;             // Record : son nom C (vide : un type IHM, ses membres inconnus ici)
    std::string  enumName;        // Int : un enum du script
    CTP          elem;            // Array, Pointer, Vector : l'element, la cible
    std::int64_t n{0};            // Array : le nombre de cases
    bool         stdArray{false}; // Array : un std::array (ses methodes)
    std::vector<std::pair<std::string, CTP>> params;   // Func : les parametres
    CTP          ret;             // Func : le retour (nul : void)
};

CTP makeCT(CT c) { return std::make_shared<const CT>(std::move(c)); }
CTP scalarCT(CT::K k, const std::string& st, const std::string& shown, int size, bool isUnsigned = false, bool isChar = false) {
    CT c;
    c.k = k;
    c.st = st;
    c.shown = shown;
    c.size = size;
    c.align = std::max(1, std::min(size, 8));
    c.isUnsigned = isUnsigned;
    c.isChar = isChar;
    return makeCT(std::move(c));
}
// Le type C d'un type elementaire du ST (REAL, DINT, STRING...).
CTP fromSt(const std::string& st, const std::string& shown = {}) {
    const std::string s = shown.empty() ? st : shown;
    if (st == "BOOL") return scalarCT(CT::K::Bool, st, s, 1);
    if (st == "SINT") return scalarCT(CT::K::Int, st, s, 1);
    if (st == "INT") return scalarCT(CT::K::Int, st, s, 2);
    if (st == "DINT") return scalarCT(CT::K::Int, st, s, 4);
    if (st == "LINT") return scalarCT(CT::K::Int, st, s, 8);
    if (st == "USINT" || st == "BYTE") return scalarCT(CT::K::Int, st, s, 1, true);
    if (st == "UINT" || st == "WORD") return scalarCT(CT::K::Int, st, s, 2, true);
    if (st == "UDINT" || st == "DWORD") return scalarCT(CT::K::Int, st, s, 4, true);
    if (st == "ULINT" || st == "LWORD") return scalarCT(CT::K::Int, st, s, 8, true);
    if (st == "REAL") return scalarCT(CT::K::Real, st, s, 4);
    if (st == "LREAL") return scalarCT(CT::K::Real, st, s, 8);
    if (st == "STRING" || st == "WSTRING") return scalarCT(CT::K::Text, st, s, 32);
    if (st == "CHAR") return scalarCT(CT::K::Int, st, s, 1, false, true);
    if (st == "TIME" || st == "DATE" || st == "TOD" || st == "DT") return scalarCT(CT::K::Int, st, s, 8);
    CT c;                                                 // un type IHM (T_Four, T_MODE) : ses membres ne sont pas connus ici
    c.k = CT::K::Record;
    c.st = st;
    c.shown = s;
    c.size = 8;
    c.align = 8;
    return makeCT(std::move(c));
}
const CTP& tUnknown() { static const CTP t = makeCT(CT{}); return t; }
const CTP& tVoid() { static const CTP t = [] { CT c; c.k = CT::K::Void; c.shown = "void"; return makeCT(c); }(); return t; }
const CTP& tInt() { static const CTP t = fromSt("DINT", "int"); return t; }
const CTP& tLong() { static const CTP t = fromSt("LINT", "long long"); return t; }
const CTP& tDouble() { static const CTP t = fromSt("LREAL", "double"); return t; }
const CTP& tFloat() { static const CTP t = fromSt("REAL", "float"); return t; }
const CTP& tBool() { static const CTP t = fromSt("BOOL", "bool"); return t; }
const CTP& tChar() { static const CTP t = scalarCT(CT::K::Int, "SINT", "char", 1, false, true); return t; }
const CTP& tText() { static const CTP t = [] { CT c = *fromSt("STRING", "const char*"); c.size = 8; c.align = 8; return makeCT(c); }(); return t; }
const CTP& tString() { static const CTP t = fromSt("STRING", "std::string"); return t; }
CTP textN(std::int64_t n) {
    CT c = *fromSt("STRING", "char[" + std::to_string(n) + "]");
    c.size = static_cast<int>(n);
    c.align = 1;
    return makeCT(std::move(c));
}
CTP arrayCT(const CTP& elem, std::int64_t n, bool stdArray = false) {
    CT c;
    c.k = CT::K::Array;
    c.elem = elem;
    c.n = n;
    c.stdArray = stdArray;
    c.st = "ARRAY[0.." + std::to_string(n - 1) + "] OF " + elem->st;
    // int[3][4] : les dimensions dans l'ordre du C
    if (stdArray) c.shown = "std::array<" + elem->shown + ", " + std::to_string(n) + ">";
    else {
        const auto b = elem->shown.find('[');
        c.shown = b == std::string::npos || elem->k != CT::K::Array ? elem->shown + "[" + std::to_string(n) + "]"
                                                                     : elem->shown.substr(0, b) + "[" + std::to_string(n) + "]" + elem->shown.substr(b);
    }
    c.size = static_cast<int>(std::min<std::int64_t>(n * elem->size, 1 << 30));
    c.align = elem->align;
    c.rec = elem->rec;
    c.enumName = elem->enumName;
    return makeCT(std::move(c));
}
CTP pointerCT(const CTP& target) {
    CT c;
    c.k = CT::K::Pointer;
    c.elem = target;
    c.st = "POINTER TO " + (target->st.empty() ? std::string("DINT") : target->st);
    c.shown = target->shown + "*";
    c.size = 8;
    c.align = 8;
    c.rec = target->rec;
    c.enumName = target->enumName;
    return makeCT(std::move(c));
}
CTP vectorCT(const CTP& elem) {
    CT c;
    c.k = CT::K::Vector;
    c.elem = elem;
    c.st = "VECTOR OF " + elem->st;
    c.shown = "std::vector<" + elem->shown + ">";
    c.size = 24;
    c.align = 8;
    c.rec = elem->rec;
    return makeCT(std::move(c));
}
CTP funcCT(std::vector<std::pair<std::string, CTP>> params, const CTP& ret) {
    CT c;
    c.k = CT::K::Func;
    std::string ps, shownPs;
    for (std::size_t k = 0; k < params.size(); ++k) {
        if (params[k].first.empty()) params[k].first = "p" + std::to_string(k + 1);
        ps += (k ? "; " : "") + params[k].first + " : " + params[k].second->st;
        shownPs += (k ? ", " : "") + params[k].second->shown;
    }
    c.st = "CALLBACK (" + ps + ")" + (ret && ret->k != CT::K::Void ? " : " + ret->st : std::string{});
    c.shown = (ret ? ret->shown : std::string("void")) + "(*)(" + shownPs + ")";
    c.params = std::move(params);
    c.ret = ret && ret->k != CT::K::Void ? ret : nullptr;
    c.size = 8;
    c.align = 8;
    return makeCT(std::move(c));
}
bool isNum(const CTP& t) { return t && (t->k == CT::K::Int || t->k == CT::K::Real || t->k == CT::K::Bool); }
bool isPtr(const CTP& t) { return t && t->k == CT::K::Pointer; }
bool isArr(const CTP& t) { return t && t->k == CT::K::Array; }
bool isRec(const CTP& t) { return t && t->k == CT::K::Record; }
bool isText(const CTP& t) { return t && t->k == CT::K::Text; }
// Le type commun d'un calcul (les conversions usuelles du C) : un reel s'il y en a un, sinon l'entier le plus large.
CTP arith(const CTP& a, const CTP& b) {
    if (!isNum(a) || !isNum(b)) return isNum(a) ? a : isNum(b) ? b : tUnknown();
    if (a->k == CT::K::Real || b->k == CT::K::Real) {
        if (a->k == CT::K::Real && b->k == CT::K::Real) return a->size >= b->size ? a : b;
        return a->k == CT::K::Real ? a : b;
    }
    const CTP& x = a->size >= 4 ? a : tInt();
    const CTP& y = b->size >= 4 ? b : tInt();
    if (x->size != y->size) return x->size > y->size ? x : y;
    return x->isUnsigned ? x : y;
}
// La valeur nulle d'un type, en litteral ST (une structure : ses membres ; un tableau : ses cases).
std::string zeroOf(const CTP& t) {
    if (!t) return "0";
    switch (t->k) {
        case CT::K::Bool: return "FALSE";
        case CT::K::Real: return "0.0";
        case CT::K::Text: return "''";
        case CT::K::Pointer: return "NULL";
        default: return "0";
    }
}

// ------------------------------------------------------------- la traduction --
struct X {                       // une expression traduite
    std::string st;
    bool        compound{false}; // un operateur au sommet : des parentheses quand elle devient un operande
    bool        boolish{false};  // un BOOL sans conversion (une comparaison, &&, !...)
    enum class Lit : std::uint8_t { None, Int, Real, Bool, Text } lit{Lit::None};
    bool        lvalue{false};
    bool        call{false};     // un appel f(...) au sommet : une instruction a lui seul
    std::int64_t intValue{0};    // Lit::Int
    CTP         ty;              // son type C (nul : inconnu - une variable IHM)
    bool        isNull{false};   // NULL, nullptr
    bool        isConst{false};  // une variable const (ne s'ecrit pas)
    std::string derefSt;         // un pointeur dont la cible a un nom ST (this) : *p, p->m le lisent
    std::string arrBase, arrIndex;   // une case t[i] : &t[i] -> CPTR_AT(t, i)
    std::string fnName;          // une fonction du script donnee comme valeur (un rappel) : son nom C
    std::string varC;            // une variable : son nom C (les messages)
    bool        statement{false};    // une affectation entiere (au niveau d'une instruction)
};

std::string wrap(const X& x) { return x.compound ? "(" + x.st + ")" : x.st; }
std::string boolOf(const X& x) {
    if (x.boolish) return x.st;
    if (isPtr(x.ty)) return wrap(x) + " <> NULL";
    return "TO_BOOL(" + x.st + ")";
}
std::string boolWrap(const X& x) {
    if (x.boolish) return wrap(x);
    if (isPtr(x.ty)) return "(" + wrap(x) + " <> NULL)";
    return "TO_BOOL(" + x.st + ")";
}

// Un passage de switch a refaire sous la forme generale (un case qui continue, un break au milieu, default avant un case).
struct SwitchRetry {};

// Une empreinte du texte (le nom ST des structures d'un script : deux scripts ne se melent pas).
std::string fingerprint(std::string_view s) {
    std::uint32_t h = 2166136261u;
    for (const char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    char buf[16];
    std::snprintf(buf, sizeof buf, "%06X", static_cast<unsigned>(h & 0xFFFFFFu));
    return buf;
}

class Translator {
public:
    Translator(std::string_view code, ScriptLang lang, const std::vector<std::string>& entryNames)
        : code_(code), lang_(lang), entryNames_(entryNames), print_(fingerprint(code)) {}

    // 1.50.1 : une expression a traduire dans la portee de la ligne `line` (0 : le niveau du script, a la fin).
    void probe(int line, std::string_view expression) {
        probeLine_ = line;
        probeText_ = std::string(expression);
        probing_ = true;
    }
    const std::string& probeResult() const { return probeResult_; }
    const std::string& probeError() const { return probeError_; }

    Translation run();

private:
    // ---- les jetons
    const Tok& peek(std::size_t ahead = 0) const { return toks_[std::min(i_ + ahead, toks_.size() - 1)]; }
    bool at(Tok::K k) const { return peek().k == k; }
    bool punct(const char* p, std::size_t ahead = 0) const { return peek(ahead).k == Tok::K::Punct && peek(ahead).text == p; }
    bool word(const char* w, std::size_t ahead = 0) const { return peek(ahead).k == Tok::K::Name && peek(ahead).text == w; }
    const Tok& next() { return toks_[i_ < toks_.size() - 1 ? i_++ : i_]; }
    bool accept(const char* p) {
        if (!punct(p)) return false;
        next();
        return true;
    }
    [[noreturn]] void fail(const std::string& message, int line = 0) const { throw Fault{line ? line : peek().line, message}; }
    void expect(const char* p, const char* what) {
        if (accept(p)) return;
        fail(std::string("'") + p + "' attendu " + what + found());
    }
    std::string found() const {
        const Tok& t = peek();
        if (t.k == Tok::K::End) return " (fin du script atteinte)";
        if (t.k == Tok::K::String) return " (trouv\xC3\xA9 : une cha\xC3\xAEne)";
        return " (trouv\xC3\xA9 : '" + t.text + "')";
    }
    bool isCpp() const { return lang_ == ScriptLang::Cpp; }

    // ---- la sortie : des morceaux, chacun sur la ligne de son texte C (jamais avant le precedent). Un morceau peut
    // tenir sur plusieurs lignes (une lambda) : "\x02N\x02" l'amene a la ligne N, '\n' passe a la suivante.
    struct Frag {
        int         line;
        std::string text;
    };
    std::size_t emit(int line, std::string text) {
        last_ = std::max(last_, line);
        const int from = last_;
        int cursor = from;
        std::string out;
        for (std::size_t k = 0; k < text.size(); ++k) {
            if (text[k] == '\x02') {
                const auto e = text.find('\x02', k + 1);
                const int want = std::atoi(text.substr(k + 1, e - k - 1).c_str());
                while (cursor < want) {
                    out += '\n';
                    ++cursor;
                }
                k = e;
                continue;
            }
            if (text[k] == '\n') ++cursor;
            out += text[k];
        }
        frags_.push_back({from, std::move(out)});
        last_ = cursor;
        return frags_.size() - 1;
    }
    // Une instruction et les effets de ses expressions (++, une affectation au milieu) : avant, puis apres.
    void emitStatement(int line, const std::string& text) {
        auto& f = fx_.back();
        std::string all;
        for (const auto& p : f.pre) all += p + " ";
        all += text;
        for (const auto& p : f.post) all += " " + p;
        f.pre.clear();
        f.post.clear();
        emit(line, all);
    }

    // ---- les effets : ce qu'une expression fait en plus de sa valeur (x++, ++x, une affectation au milieu, new)
    struct Fx {
        std::vector<std::string> pre, post;
        bool                     allowed{true};
        std::string              why;
    };
    std::vector<Fx> fx_;
    void addPre(const std::string& s, int line) {
        if (fx_.empty() || !fx_.back().allowed) fail(fx_.empty() ? std::string("un effet (++, =) n'est pas permis ici") : fx_.back().why, line);
        fx_.back().pre.push_back(s);
    }
    void addPost(const std::string& s, int line) {
        if (fx_.empty() || !fx_.back().allowed) fail(fx_.empty() ? std::string("un effet (++, =) n'est pas permis ici") : fx_.back().why, line);
        fx_.back().post.push_back(s);
    }
    struct FxScope {
        Translator& t;
        FxScope(Translator& tr, bool allowed, std::string why) : t(tr) {
            Fx f;
            f.allowed = allowed;
            f.why = std::move(why);
            t.fx_.push_back(std::move(f));
        }
        ~FxScope() { t.fx_.pop_back(); }
        Fx& fx() { return t.fx_.back(); }
    };
    // Une partie qui ne s'execute pas toujours (la droite de &&, ||, un cote de ?:) : pas d'effet dedans.
    X conditional(const std::function<X()>& f, const char* where) {
        const bool outerAllowed = !fx_.empty() && fx_.back().allowed;
        FxScope s(*this, false, std::string("++, -- ou une affectation ") + where + " n'est pas pris en charge (il ne s'ex\xC3\xA9" "cute pas toujours) : \xC3\xA9" "cris-le \xC3\xA0 part");
        (void)outerAllowed;
        return f();
    }

    // ---- les noms
    struct Var {
        std::string st;
        CTP         type;
        bool        isConst{false};
        bool        autoDeref{false};    // une reference locale (T& r = x) : un POINTER TO, lu r^
        std::string c;
    };
    struct Hoisted {
        std::string st, type, init;
        bool        keep{false};             // static : gardee d'une execution a l'autre
        int         line{0};                 // sa declaration C
    };
    struct Code {
        std::vector<std::map<std::string, Var>> scopes;
        std::vector<Hoisted>                    hoisted;
        std::set<std::string>                   taken;     // les noms ST pris (majuscules)
        CTP                                     result;    // le type rendu (nul : void)
        bool                                    function{false};
        bool                                    lambda{false};
        bool                                    lambdaCopies{false};   // [=] : les locales de l'englobant se lisent
        CTP                                     deduced;               // une lambda sans -> T : le type de son premier return
        std::string                             stName;    // "" : le script ; une fonction : son nom ST
        std::string                             recordC;   // une methode : sa classe (le nom C)
        Code*                                   outer{nullptr};
        std::vector<DebugVar>                   dbg;       // ses variables, pour le debogueur
        int                                     temps{0};
    };
    struct FnInfo {
        std::string st;
        std::size_t params{0};
        bool        defined{false}, declared{false};
        CTP         ret;
        std::vector<std::pair<std::string, CTP>> ptypes;  // les types des parametres
        std::vector<int>                         modes;   // 0 : valeur ; 1 : VAR_IN_OUT (reference, tableau) ; 2 : pointeur
    };

    static bool reservedSt(const std::string& name) {
        const std::string u = upperOf(name);
        if (isReservedWord(u) || stElementary().count(u)) return true;
        static const std::set<std::string> more = {"VAR", "VAR_TEMP", "VAR_INPUT", "VAR_OUTPUT", "VAR_IN_OUT", "END_VAR", "FUNCTION",
                                                   "END_FUNCTION", "CONSTANT", "ARRAY", "OF", "MAP", "LIST", "VECTOR", "SET", "RING",
                                                   "TUPLE", "REF_TO", "POINTER", "REFERENCE", "TO", "BY", "DO", "THEN", "EACH", "IN",
                                                   "AND", "OR", "XOR", "NOT", "MOD", "ENTRE", "ET", "LDATE", "LTIME", "LTOD", "LDT",
                                                   "TIME_OF_DAY", "DATE_AND_TIME", "ANY", "OBJECT", "FONT", "TEXTURE", "SOUND", "STYLE",
                                                   "LAMBDA", "END_LAMBDA", "CALLBACK", "SELF", "THIS", "SUPER", "CPTR_AT", "CPTR_ADD",
                                                   "CPTR_DIFF", "CPTR_EQ", "CNEW", "CDELETE", "ADR", "REF", "SIZEOF", "NULL"};
        return more.count(u) > 0 || (name.size() > 1 && name[0] == '_' && name[1] == '_');
    }
    std::string fresh(const std::string& base, const std::set<std::string>& taken) const {
        const std::string b = base.size() > 1 && base[0] == '_' && base[1] == '_' ? "u" + base : base;
        if (!reservedSt(b) && !taken.count(upperOf(b)) && !allNames_.count(upperOf(b))) return b;
        for (int n = 2;; ++n) {
            const std::string c = b + "_" + std::to_string(n);
            if (!taken.count(upperOf(c)) && !allNames_.count(upperOf(c))) return c;
        }
    }

    // ---- les structures et les classes
    struct Member {
        std::string c, st;
        CTP         type;
        std::string access{"public"};
        int         line{0};
        std::string owner;                   // la classe qui le declare (son nom C)
        std::size_t initFrom{0}, initTo{0};  // sa valeur par defaut (C++ : int x = 0;) : ses jetons
        bool        isStatic{false};
        std::string staticSt;                // static : la variable du script qui le porte
        int         offset{0};
    };
    struct Method {
        std::string c, st, owner;
        std::vector<std::pair<std::string, CTP>> params;
        std::vector<int>                         modes;   // comme FnInfo::modes
        CTP         ret;
        bool        isVirtual{false}, pure{false}, isStatic{false}, ctor{false}, dtor{false}, defined{false};
        std::string access{"public"};
        int         line{0};
        std::size_t bodyAt{0};               // le jeton de son corps ({ ou : d'une liste d'initialisation), defini dans la classe
        std::string virtualRoot;             // une methode virtuelle : la classe ou elle l'est devenue
    };
    struct Record {
        std::string c, st;
        bool        cpp{false}, isClass{false}, complete{false}, abstract{false};
        std::string base;
        std::vector<Member> members;
        std::vector<Method> methods;
        int         size{0}, align{1};
        int         tag{0};
        bool        polymorphic{false};
        int         line{0}, endLine{0};
        std::string root;
        bool        needsInit{false};        // un constructeur implicite (des valeurs par defaut, __vt, des membres a construire)
        std::string initSt;                  // son nom ST
        bool        hasDtor{false};
    };
    std::map<std::string, Record> records_;
    std::vector<std::string>      recordOrder_;
    int                           tags_{0};
    int                           anon_{0};
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> fat_;   // racine -> (membre ST, type ST)
    std::map<std::string, std::set<std::string>>                            fatNames_;

    Record* record(const std::string& c) {
        const auto it = records_.find(c);
        return it == records_.end() ? nullptr : &it->second;
    }
    const Record* record(const std::string& c) const {
        const auto it = records_.find(c);
        return it == records_.end() ? nullptr : &it->second;
    }
    bool derivesFrom(const std::string& cls, const std::string& base) const {
        for (const Record* r = record(cls); r; r = r->base.empty() ? nullptr : record(r->base))
            if (r->c == base) return true;
        return false;
    }
    // Un membre de la classe (ou d'un parent) : le membre et la classe qui le declare.
    const Member* findMember(const std::string& cls, const std::string& name) const {
        for (const Record* r = record(cls); r; r = r->base.empty() ? nullptr : record(r->base))
            for (const auto& m : r->members)
                if (m.c == name) return &m;
        return nullptr;
    }
    // Les methodes de ce nom : celles de la premiere classe (en remontant) qui le declare (le C++ cache les autres).
    std::vector<const Method*> findMethods(const std::string& cls, const std::string& name) const {
        std::vector<const Method*> out;
        for (const Record* r = record(cls); r && out.empty(); r = r->base.empty() ? nullptr : record(r->base))
            for (const auto& m : r->methods)
                if (m.c == name && !m.ctor && !m.dtor) out.push_back(&m);
        return out;
    }
    CTP recordType(const std::string& c) const {
        const Record* r = record(c);
        CT t;
        t.k = CT::K::Record;
        t.rec = c;
        t.st = r ? r->st : c;
        t.shown = r && !r->cpp ? "struct " + c : c;
        t.size = r ? r->size : 8;
        t.align = r ? r->align : 8;
        return makeCT(std::move(t));
    }

    // ---- les enum et les typedef
    struct EnumInfo {
        std::string                                    name;
        bool                                           scoped{false};   // enum class
        std::vector<std::pair<std::string, std::int64_t>> values;
    };
    std::map<std::string, EnumInfo>                          enums_;
    std::map<std::string, std::pair<std::string, std::int64_t>> enumerators_;   // nom -> (enum, valeur) : les enum du C
    std::map<std::string, CTP>                               typedefs_;
    CTP enumType(const std::string& name) const {
        CT c = *fromSt("DINT", (isCpp() ? "" : "enum ") + name);
        c.enumName = name;
        return makeCT(std::move(c));
    }

    // Les fonctions du script, vues avant leur premier appel (au niveau du script : type nom ( ... ) { ou ;).
    void scanFunctions() {
        int depth = 0;
        for (std::size_t k = 0; k + 1 < toks_.size(); ++k) {
            const Tok& t = toks_[k];
            if (t.k == Tok::K::Punct) {
                if (t.text == "{") ++depth;
                else if (t.text == "}") --depth;
                continue;
            }
            if (depth != 0 || t.k != Tok::K::Name || k == 0) continue;
            if (t.text.find("::") != std::string::npos) continue;            // Pompe::demarrer : une methode
            const Tok& prev = toks_[k - 1];
            if (!(toks_[k + 1].k == Tok::K::Punct && toks_[k + 1].text == "(")) continue;
            if (!(prev.k == Tok::K::Name || (prev.k == Tok::K::Punct && (prev.text == "*" || prev.text == "&" || prev.text == ">")))) continue;
            if (prev.k == Tok::K::Name
                && (prev.text == "return" || prev.text == "else" || prev.text == "case" || prev.text == "do" || prev.text == "sizeof"
                    || prev.text == "new" || prev.text == "delete" || prev.text == "throw" || prev.text == "goto" || prev.text == "struct"
                    || prev.text == "class" || prev.text == "enum" || prev.text == "typedef"))
                continue;
            int paren = 0;
            std::size_t e = k + 1;
            std::size_t params = 0;
            bool any = false, expression = false;
            std::size_t pieceTokens = 0;
            bool pieceType = false;
            const auto endPiece = [&] {
                // un parametre : un type et un nom (int x, T* p), ou un type seul (int) ; un nom seul : une valeur (Pompe p(n);)
                if (pieceTokens == 1 && !pieceType) expression = true;
                pieceTokens = 0;
                pieceType = false;
            };
            for (; e < toks_.size(); ++e) {
                const Tok& x = toks_[e];
                if (x.k == Tok::K::Punct && x.text == "(") ++paren;
                else if (x.k == Tok::K::Punct && x.text == ")") {
                    if (--paren == 0) {
                        endPiece();
                        break;
                    }
                } else if (paren == 1) {
                    if (x.k == Tok::K::Punct && x.text == ",") {
                        ++params;
                        endPiece();
                    } else if (!(x.k == Tok::K::Name && x.text == "void")) {
                        any = true;
                        ++pieceTokens;
                        if (x.k == Tok::K::Number || x.k == Tok::K::String || x.k == Tok::K::Char) expression = true;
                        if (x.k == Tok::K::Punct && x.text != "*" && x.text != "&" && x.text != "[" && x.text != "]" && x.text != "..." && x.text != "="
                            && x.text != "(" && x.text != ")" && x.text != "::")
                            expression = true;
                        if (x.k == Tok::K::Name && (typeKeyword(x.text) || cNamedTypes().count(x.text) || stElementary().count(x.text))) pieceType = true;
                        if (x.k == Tok::K::Punct && (x.text == "*" || x.text == "&")) pieceType = true;
                    }
                }
            }
            if (expression) continue;
            if (e + 1 >= toks_.size()) continue;
            std::size_t after = e + 1;
            while (toks_[after].k == Tok::K::Name && (toks_[after].text == "const" || toks_[after].text == "noexcept")) ++after;
            if (toks_[after].k == Tok::K::Punct && toks_[after].text == "->") {      // auto f() -> int
                while (after < toks_.size() && !(toks_[after].k == Tok::K::Punct && (toks_[after].text == "{" || toks_[after].text == ";"))) ++after;
            }
            const bool body = toks_[after].k == Tok::K::Punct && toks_[after].text == "{";
            const bool proto = toks_[after].k == Tok::K::Punct && toks_[after].text == ";";
            if (!body && !proto) continue;
            const std::size_t count = any ? params + 1 : 0;
            auto& list = functions_[t.text];
            FnInfo* info = nullptr;
            for (auto& f : list)
                if (f.params == count) info = &f;
            if (!info) {
                list.emplace_back();
                info = &list.back();
                info->params = count;
                const std::string u = upperOf(t.text);
                const bool clash = reservedSt(t.text) || !mappedCall(t.text).empty() || sim::lib::native(u) || isHmiFunction(u);
                std::string st = clash ? fresh(t.text + "_c", {}) : t.text;
                if (list.size() > 1) st = fresh(t.text + "_" + std::to_string(count), {});   // une surcharge (C++) : un autre nom
                info->st = st;
                allNames_.insert(upperOf(st));
            }
            info->defined = info->defined || body;
        }
    }
    // La fonction de ce nom pour `count` arguments (une surcharge par le nombre de ses parametres).
    FnInfo* findFunction(const std::string& name, std::size_t count, int line) {
        const auto it = functions_.find(name);
        if (it == functions_.end() || it->second.empty()) return nullptr;
        for (auto& f : it->second)
            if (f.params == count) return &f;
        if (it->second.size() == 1) return &it->second.front();       // le moteur dira le nombre d'arguments
        fail(name + " : aucune surcharge ne prend " + std::to_string(count) + " argument(s)", line);
    }
    bool isFunction(const std::string& name) const { return functions_.count(name) > 0; }

    const Var* findVar(const std::string& name) const { return findVarIn(cur_, name, true); }
    const Var* findVarIn(const Code* code, const std::string& name, bool script) const {
        for (auto it = code->scopes.rbegin(); it != code->scopes.rend(); ++it)
            if (const auto f = it->find(name); f != it->end()) return &f->second;
        if (code->lambda && code->outer && code->lambdaCopies)
            if (const Var* v = findVarIn(code->outer, name, false)) return v;
        if (script && code != &script_ && !script_.scopes.empty())     // une fonction voit les declarations du script
            if (const auto f = script_.scopes.front().find(name); f != script_.scopes.front().end()) return &f->second;
        return nullptr;
    }

    // Une variable declaree ici : son nom ST (le nom C, ou un autre s'il est pris ou reserve en ST).
    std::string declare(const std::string& name, const CTP& type, int line, bool forEach = false) {
        auto& scope = cur_->scopes.back();
        if (scope.count(name)) fail(name + " est d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "clar\xC3\xA9" "e dans ce bloc", line);
        if (isFunction(name) && cur_->scopes.size() == 1 && cur_ == &script_) fail(name + " est d\xC3\xA9j\xC3\xA0 une fonction du script", line);
        std::string st = name;
        const bool visible = findVar(name) != nullptr;
        // Un frere d'un autre bloc, du meme type : le meme nom ST (for (int i...) deux fois).
        const Hoisted* same = nullptr;
        for (const auto& h : cur_->hoisted)
            if (upperOf(h.st) == upperOf(name)) same = &h;
        const bool reuse = same && !visible && same->type == type->st && !same->keep && !forEach;
        // le ST ne distingue pas r et R (une variable IHM) : un nom ecrit aussi autrement dans le script se renomme
        const auto sp = spellings_.find(upperOf(name));
        const bool otherCase = sp != spellings_.end() && sp->second.size() > 1;
        if (reservedSt(name) || visible || (cur_->taken.count(upperOf(name)) && !reuse) || isFunction(name) || otherCase) st = fresh(name, cur_->taken);
        Var v;
        v.st = st;
        v.type = type;
        v.c = name;
        scope[name] = v;
        if (!(reuse && st == name)) cur_->taken.insert(upperOf(st));
        return st;
    }
    // Une variable de la traduction (un temporaire) : un nom a elle, rangee en tete, cachee au debogueur.
    std::string temp(const std::string& stType, int line, const std::string& base = "t") {
        std::string st;
        for (int n = ++cur_->temps;; n = ++cur_->temps) {
            st = "c" + base + std::to_string(n) + "_";
            if (!cur_->taken.count(upperOf(st)) && !allNames_.count(upperOf(st))) break;
        }
        cur_->taken.insert(upperOf(st));
        cur_->hoisted.push_back(Hoisted{st, stType, {}, false, line});
        DebugVar d;
        d.st = st;
        d.hidden = true;
        cur_->dbg.push_back(d);
        return st;
    }
    // Une variable gardee d'une execution a l'autre, au niveau du script (un temporaire gardee, un static).
    std::string scriptVar(const std::string& base, const std::string& stType, const std::string& init, int line, bool hidden,
                          const std::string& cName = {}, const CTP& type = nullptr) {
        const std::string st = fresh(base, script_.taken);
        script_.taken.insert(upperOf(st));
        allNames_.insert(upperOf(st));
        script_.hoisted.push_back(Hoisted{st, stType, init, true, line});
        DebugVar d;
        d.st = st;
        d.c = cName;
        d.hidden = hidden;
        if (type) {
            d.type = type->shown;
            d.record = type->rec;
            d.enumName = type->enumName;
        }
        script_.dbg.push_back(d);
        return st;
    }
    void noteVar(const std::string& st, const std::string& c, const CTP& type) {
        DebugVar d;
        d.st = st;
        d.c = c;
        d.type = type ? type->shown : std::string{};
        d.record = type ? type->rec : std::string{};
        d.enumName = type ? type->enumName : std::string{};
        for (auto& x : cur_->dbg)
            if (upperOf(x.st) == upperOf(st)) {
                if (x.c != c) x.c = x.c + " / " + c;     // deux blocs, deux noms C pour un nom ST (for (int i...) deux fois)
                return;
            }
        cur_->dbg.push_back(std::move(d));
    }

    // ---- les types ecrits
    struct Spec {
        CTP  type;
        bool isConst{false}, isStatic{false}, isAuto{false}, isVirtual{false}, isExplicit{false}, isInline{false};
        bool definedType{false};             // struct { ... }, enum { ... } : une declaration peut s'arreter la (;)
        int  line{0};
    };

    // Le > qui ferme un modele (std::vector<std::vector<int>> : le >> se coupe en deux).
    void closeAngle(const char* what) {
        if (punct(">>")) {
            toks_[i_].text = ">";
            return;
        }
        expect(">", what);
    }

    // Un nom deja connu comme type : un typedef, une struct / classe / un enum (en C++ sans le mot struct).
    bool knownTypeName(const std::string& w) const {
        if (typedefs_.count(w)) return true;
        if (isCpp() && (records_.count(w) || enums_.count(w))) return true;
        return false;
    }

    bool startsType(std::size_t k = 0) const {
        const Tok& t = peek(k);
        if (t.k != Tok::K::Name) return false;
        static const std::set<std::string> statements = {"return", "delete", "new", "goto", "case", "default", "else", "do", "if", "while",
                                                         "for", "switch", "break", "continue", "throw", "sizeof", "this", "using", "typedef",
                                                         "true", "false", "nullptr", "NULL", "static_cast", "const_cast", "printf", "puts"};
        if (statements.count(t.text)) return false;
        if (typeKeyword(t.text) && t.text != "struct" && t.text != "enum" && t.text != "union" && t.text != "class") return true;
        if (t.text == "struct" || t.text == "enum" || t.text == "class" || t.text == "union") return true;
        if (cNamedTypes().count(t.text) || stElementary().count(t.text)) return true;
        if (knownTypeName(t.text)) return !(peek(k + 1).k == Tok::K::Punct && (peek(k + 1).text == "(" || peek(k + 1).text == "{"));
        if (t.text.rfind("std::", 0) == 0 && punct("<", k + 1)) return true;        // std::vector<int>, std::array<int, 3>
        // T x ; T *x ; T &x : un type du projet
        if (isFunction(t.text) || findVar(t.text) || enumerators_.count(t.text)) return false;
        const Tok& n = peek(k + 1);
        if (n.k == Tok::K::Name && !refusedWord(n.text).size() && n.text != "const" && t.text.find("::") == std::string::npos) return true;
        if (n.k == Tok::K::Name && n.text == "const") return true;
        if (n.k == Tok::K::Punct && (n.text == "*" || n.text == "&") && peek(k + 2).k == Tok::K::Name
            && (punct("=", k + 3) || punct(";", k + 3) || punct(",", k + 3) || punct("[", k + 3) || punct(")", k + 3)))
            return true;
        return false;
    }

    Spec parseSpec(bool member = false) {
        Spec sp;
        sp.line = peek().line;
        int longs = 0;
        bool isUnsigned = false, isSigned = false, isShort = false, isInt = false, isChar = false, isFloat = false, isDouble = false,
             isBool = false, isVoid = false;
        CTP named;
        while (peek().k == Tok::K::Name) {
            const std::string w = peek().text;
            const bool none = !named && !isUnsigned && !isSigned && !isShort && !longs && !isInt && !isChar && !isFloat && !isDouble && !isBool
                              && !isVoid && !sp.isAuto;
            if (w == "const" || w == "constexpr") sp.isConst = true;
            else if (w == "static") sp.isStatic = true;
            else if (w == "volatile" || w == "register" || w == "inline" || w == "mutable") sp.isInline = sp.isInline || w == "inline";
            else if (w == "virtual") {
                if (!member) fail("virtual : seulement pour une m\xC3\xA9thode d'une classe");
                sp.isVirtual = true;
            } else if (w == "explicit") {
                if (!member) fail("explicit : seulement pour un constructeur");
                sp.isExplicit = true;
            } else if (w == "extern") fail("extern : les variables IHM sont d\xC3\xA9j\xC3\xA0 connues par leur nom (retire extern)");
            else if (w == "typedef") fail("typedef : seulement au d\xC3\xA9" "but d'une d\xC3\xA9" "claration");
            else if (w == "unsigned") isUnsigned = true;
            else if (w == "signed") isSigned = true;
            else if (w == "short") isShort = true;
            else if (w == "long") ++longs;
            else if (w == "int") isInt = true;
            else if (w == "char") isChar = true;
            else if (w == "float") isFloat = true;
            else if (w == "double") isDouble = true;
            else if (w == "bool" || w == "_Bool") isBool = true;
            else if (w == "void") isVoid = true;
            else if (w == "auto") {
                if (!isCpp()) fail("auto : \xC3\xA9" "cris le type (int, float...)");
                sp.isAuto = true;
            } else if ((w == "struct" || w == "class" || w == "union" || w == "enum") && none) {
                if (w == "union") fail(refusedWord("union"));
                if (w == "class" && !isCpp()) fail("class est du C++ (en C : struct)");
                next();
                if (w == "enum") named = enumSpec(sp);
                else named = recordSpec(w == "class", sp);
                continue;
            } else if (none && w.rfind("std::", 0) == 0 && punct("<", 1)) {
                named = templateType();
                continue;
            } else if (none && (cNamedTypes().count(w) || stElementary().count(w))) {
                if (const auto it = cNamedTypes().find(w); it != cNamedTypes().end())
                    named = it->second == "STRING" ? tString() : fromSt(it->second, w);
                else named = fromSt(stElementary().at(w));
            } else if (none && typedefs_.count(w)) {
                named = typedefs_.at(w);
            } else if (none && isCpp() && records_.count(w)) {
                named = recordType(w);
            } else if (none && isCpp() && enums_.count(w)) {
                named = enumType(w);
            } else if (none) {
                if (w.rfind("std::", 0) == 0)
                    fail(w + " : non pris en charge (un tableau T t[N], std::array, std::vector, un texte std::string ; une liste : une variable IHM LIST OF T)");
                if (const std::string why = refusedWord(w); !why.empty()) fail(why);
                if (w.find("::") != std::string::npos) fail(w + " : un nom de type simple est attendu");
                named = fromSt(w);                       // un type IHM (T_Four, T_MODE)
            } else {
                break;
            }
            next();
        }
        if (punct("<")) fail((named ? named->shown + "<...>" : std::string("<")) + " : les mod\xC3\xA8les (template) ne sont pas pris en charge");
        if (named) sp.type = named;
        else if (isVoid) sp.type = tVoid();
        else if (sp.isAuto) sp.type = tUnknown();
        else if (isBool) sp.type = tBool();
        else if (isFloat) sp.type = tFloat();
        else if (isDouble) sp.type = longs ? fromSt("LREAL", "long double") : tDouble();
        else if (isChar) sp.type = isUnsigned ? fromSt("USINT", "unsigned char") : isSigned ? fromSt("SINT", "signed char") : tChar();
        else if (isShort) sp.type = isUnsigned ? fromSt("UINT", "unsigned short") : fromSt("INT", "short");
        else if (longs >= 2) sp.type = isUnsigned ? fromSt("ULINT", "unsigned long long") : tLong();
        else if (longs == 1) sp.type = isUnsigned ? fromSt("UDINT", "unsigned long") : fromSt("DINT", "long");
        else if (isInt || isUnsigned || isSigned) sp.type = isUnsigned ? fromSt("UDINT", "unsigned int") : tInt();
        else fail("un type est attendu (int, float, bool...)" + found());
        return sp;
    }

    // Un type ecrit seul (sizeof(T), (T)x, static_cast<T>, new T, un argument de modele) : T, T*, T&.
    CTP typeName() {
        Spec sp = parseSpec();
        CTP t = sp.type;
        if (sp.isAuto) fail("auto : un type est attendu ici");
        while (punct("*") || punct("&") || word("const")) {
            if (punct("*")) t = isText(t) && t->size != 8 && t->shown != "std::string" ? t : (t->isChar ? tText() : pointerCT(t));
            next();
        }
        return t;
    }

    // std::array<T, N>, std::vector<T>, std::function<R(A, B)>.
    CTP templateType() {
        const std::string w = next().text;
        expect("<", ("apr\xC3\xA8s " + w).c_str());
        if (w == "std::array") {
            const CTP elem = typeName();
            expect(",", "dans std::array<T, N>");
            ++templateArg_;
            const std::int64_t n = constantSize(peek().line);
            --templateArg_;
            if (n <= 0 || n > 100000) fail("std::array<T, " + std::to_string(n) + "> : de 1 \xC3\xA0 100000 cases");
            closeAngle("apr\xC3\xA8s std::array<T, N");
            return arrayCT(elem, n, true);
        }
        if (w == "std::vector") {
            const CTP elem = typeName();
            closeAngle("apr\xC3\xA8s std::vector<T");
            if (elem->k == CT::K::Vector || elem->k == CT::K::Array) fail("std::vector d'un tableau ou d'un vecteur n'est pas pris en charge");
            return vectorCT(elem);
        }
        if (w == "std::function") {
            const CTP ret = typeName();
            expect("(", "dans std::function<R(A, B)>");
            std::vector<std::pair<std::string, CTP>> params;
            if (word("void") && punct(")", 1)) next();
            while (!punct(")")) {
                params.emplace_back(std::string{}, typeName());
                if (!accept(",")) break;
            }
            expect(")", "dans std::function<R(A, B)>");
            closeAngle("apr\xC3\xA8s std::function<R(A, B)");
            return funcCT(std::move(params), ret);
        }
        fail(w + "<...> : non pris en charge (std::array, std::vector, std::function, std::string le sont)");
    }

    std::int64_t constantSize(int line) {
        FxScope s(*this, false, "un effet dans une taille n'est pas permis");
        X e = ternary();
        if (e.lit == X::Lit::Int) return e.intValue;
        if (const auto it = constInts_.find(e.st); it != constInts_.end()) return it->second;
        for (const auto& [c, v] : constInts_)
            if (findVar(c) && findVar(c)->st == e.st) return v;
        fail("la taille d'un tableau est une constante enti\xC3\xA8re (un nombre, #define N 10, const int N = 10)", line);
    }

    // Un declarateur : [*|&] nom [N][M]... ; une fonction nom(params) ; un pointeur de fonction (*nom)(params).
    struct Decl {
        std::string name;
        CTP         type;
        bool        reference{false}, function{false}, unsized{false};
        std::vector<std::pair<std::string, CTP>> params;
        std::vector<int>                         modes;
        int         line{0};
        std::vector<std::int64_t> dims;
    };
    Decl declarator(const Spec& base, bool parameter, bool nameOptional = false) {
        Decl d;
        d.line = peek().line;
        int stars = 0;
        while (punct("*") || punct("&") || punct("&&")) {
            if (punct("*")) ++stars;
            else if (punct("&&")) fail("&& (une r\xC3\xA9" "f\xC3\xA9rence \xC3\xA0 une valeur temporaire) n'est pas prise en charge");
            else {
                if (!isCpp()) fail("& dans une d\xC3\xA9" "claration est du C++ (en C : un pointeur T* p)");
                d.reference = true;
            }
            next();
            while (word("const")) next();
        }
        // (*nom)(params) : un pointeur de fonction
        if (punct("(") && punct("*", 1)) {
            next();
            next();
            if (peek().k == Tok::K::Name) d.name = next().text;
            else if (!nameOptional) fail("un nom est attendu" + found());
            expect(")", "apr\xC3\xA8s (*nom");
            CTP ret = base.type;
            for (int s = 0; s < stars; ++s) ret = ret->isChar ? tText() : pointerCT(ret);
            std::vector<std::pair<std::string, CTP>> params;
            std::vector<int> modes;
            parameters(params, modes);
            d.type = funcCT(params, ret);
            return d;
        }
        if (peek().k != Tok::K::Name) {
            if (!nameOptional) fail("un nom est attendu" + found());
        } else {
            d.name = next().text;
            if (const std::string why = refusedWord(d.name); !why.empty()) fail(why, d.line);
        }
        CTP t = base.type;
        // char* p : un texte ; char** p : un pointeur vers un texte
        for (int s = 0; s < stars; ++s) {
            if (t->isChar && t->k == CT::K::Int) t = tText();
            else {
                if (t->k == CT::K::Void) fail("void* : un pointeur sans type n'est pas pris en charge (T* p)", d.line);
                const bool constTarget = s == 0 && base.isConst;
                t = pointerCT(t);
                if (constTarget) {                                   // const int* p : montre tel quel
                    CT c = *t;
                    c.shown = "const " + c.shown;
                    t = makeCT(std::move(c));
                }
            }
        }
        if (punct("(") && !parameter && (punct(")", 1) || startsType(1) || word("void", 1) || punct("...", 1) || !isCpp())) {
            d.function = true;
            parameters(d.params, d.modes);
            d.type = t;
            return d;
        }
        while (accept("[")) {
            if (punct("]")) {
                if (!d.dims.empty()) fail("[] : seule la premi\xC3\xA8re dimension d'un tableau peut s'omettre", d.line);
                d.unsized = true;
                d.dims.push_back(0);
                next();
                continue;
            }
            const std::int64_t n = constantSize(d.line);
            if (n <= 0 || n > 100000) fail("taille de tableau " + std::to_string(n) + " : de 1 \xC3\xA0 100000", d.line);
            d.dims.push_back(n);
            expect("]", "apr\xC3\xA8s la taille du tableau");
        }
        if (!d.dims.empty()) {
            // char s[N] : un texte ; char t[N][M] : N textes
            std::size_t dims = d.dims.size();
            if (t->isChar && t->k == CT::K::Int && stars == 0) {
                t = textN(d.dims.back() ? d.dims.back() : 0);
                --dims;
                if (dims == 0 && d.unsized) d.unsized = false;      // char s[] = "..." : un texte
            }
            for (std::size_t k = dims; k-- > 0;) {
                if (k == 0 && d.unsized) break;                       // la taille viendra de la liste (ou un parametre : un pointeur)
                t = arrayCT(t, d.dims[k]);
            }
        }
        d.type = t;
        return d;
    }

    // Les parametres d'une fonction (d'un pointeur de fonction) : leur nom, leur type, leur passage.
    void parameters(std::vector<std::pair<std::string, CTP>>& params, std::vector<int>& modes) {
        expect("(", "");
        if (word("void") && punct(")", 1)) next();
        while (!punct(")")) {
            if (punct("...")) fail("les param\xC3\xA8tres variables (...) ne sont pas pris en charge");
            const Spec pt = parseSpec();
            if (pt.type->k == CT::K::Void && !punct("*")) fail("void : un param\xC3\xA8tre a un type");
            if (pt.isAuto) fail("auto : \xC3\xA9" "cris le type du param\xC3\xA8tre");
            Decl pd = declarator(pt, true, true);
            if (punct("=")) fail("une valeur par d\xC3\xA9" "faut n'est pas prise en charge (passe-la \xC3\xA0 l'appel)");
            int mode = 0;
            CTP type = pd.type;
            if (pd.unsized) {                                        // T t[] : un pointeur
                type = pointerCT(type);
                mode = 2;
            } else if (!pd.dims.empty() && !(isText(type) && pd.dims.size() == 1)) {
                mode = 1;                                            // T t[N] : le tableau de l'appelant
            } else if (pd.reference) {
                mode = pt.isConst ? 0 : 1;
            } else if (isPtr(type)) {
                mode = 2;
            }
            params.emplace_back(pd.name, type);
            modes.push_back(mode);
            if (!accept(",")) break;
        }
        expect(")", "apr\xC3\xA8s les param\xC3\xA8tres");
    }

    // [[nodiscard]], [[maybe_unused]] : un attribut du C++ (ou du C23) - sans effet ici, saute.
    void attributes() {
        while (punct("[") && punct("[", 1)) {
            next();
            next();
            int depth = 2;
            while (depth > 0 && !at(Tok::K::End)) {
                if (punct("[")) ++depth;
                else if (punct("]")) --depth;
                next();
            }
        }
    }

    // enum Nom { A, B = 5, C } ; enum class Nom : int { ... } ; enum Nom (deja defini).
    CTP enumSpec(Spec& sp) {
        const int line = peek().line;
        bool scoped = false;
        if (word("class") || word("struct")) {
            if (!isCpp()) fail("enum class est du C++");
            next();
            scoped = true;
        }
        std::string name;
        if (peek().k == Tok::K::Name && !punct("{", 0)) name = next().text;
        if (accept(":")) (void)typeName();                    // le type des valeurs : un entier
        if (!punct("{")) {
            if (name.empty() || !enums_.count(name)) fail("enum " + name + " : inconnu (d\xC3\xA9" "fini plus haut ?)", line);
            return enumType(name);
        }
        if (name.empty()) name = "enum" + std::to_string(++anon_);
        if (enums_.count(name)) fail("enum " + name + " est d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "fini", line);
        next();                                                // {
        EnumInfo info;
        info.name = name;
        info.scoped = scoped;
        enums_[name] = info;                                   // ses premieres valeurs servent aux suivantes
        std::int64_t v = 0;
        while (!punct("}")) {
            if (peek().k != Tok::K::Name) fail("un nom de valeur est attendu dans enum " + name + found());
            const Tok& t = next();
            const std::string n = t.text;
            if (accept("=")) v = constantSize(t.line);
            if (enumerators_.count(n) && !scoped) fail(n + " : d\xC3\xA9j\xC3\xA0 une valeur d'un enum", t.line);
            enums_[name].values.emplace_back(n, v);
            if (!scoped) enumerators_[n] = {name, v};
            ++v;
            if (!accept(",")) break;
        }
        expect("}", ("\xC3\xA0 la fin de enum " + name).c_str());
        sp.definedType = true;
        return enumType(name);
    }

    // struct Nom ; struct Nom { ... } ; struct { ... } ; class Nom : public Base { ... }.
    CTP recordSpec(bool isClass, Spec& sp) {
        const int line = peek().line;
        std::string name;
        if (peek().k == Tok::K::Name && (punct("{", 1) || punct(":", 1) || punct(";", 1) || peek(1).k == Tok::K::Name || punct("*", 1)
                                          || punct("&", 1) || punct(")", 1) || punct(",", 1) || punct("[", 1)))
            name = next().text;
        else if (peek().k == Tok::K::Name && !punct("{", 0)) name = next().text;
        if (word("final")) next();
        if (!punct("{") && !punct(":")) {
            if (name.empty()) fail("struct : un nom ou { ... } est attendu" + found(), line);
            if (!records_.count(name)) {                       // struct Noeud; struct Noeud* suivant : declaree plus tard
                Record r;
                r.c = name;
                r.cpp = isCpp();
                r.isClass = isClass;
                r.line = line;
                r.st = name + "_" + print_;
                r.root = name;
                allNames_.insert(upperOf(r.st));
                records_[name] = r;
                recordOrder_.push_back(name);
            }
            return recordType(name);
        }
        if (name.empty()) name = "struct" + std::to_string(++anon_);
        sp.definedType = true;
        recordDefinition(name, isClass, line);
        return recordType(name);
    }

    // Saute un bloc { ... } (ou ( ... )) equilibre ; rend l'indice du jeton qui le suit.
    std::size_t skipBalanced() {
        const std::string open = peek().text;
        const std::string close = open == "{" ? "}" : open == "(" ? ")" : "]";
        const int line = peek().line;
        int depth = 0;
        while (!at(Tok::K::End)) {
            if (punct(open.c_str())) ++depth;
            else if (punct(close.c_str())) {
                if (--depth == 0) {
                    next();
                    return i_;
                }
            }
            next();
        }
        fail("'" + close + "' attendu : le bloc ouvert ligne " + std::to_string(line) + " n'est pas ferm\xC3\xA9", line);
    }

    void recordDefinition(const std::string& name, bool isClass, int line) {
        if (records_.count(name) && records_.at(name).complete) fail("struct " + name + " est d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "finie", line);
        if (!records_.count(name)) recordOrder_.push_back(name);
        Record& r = records_[name];
        r.c = name;
        r.cpp = isCpp();
        r.isClass = isClass;
        r.line = line;
        r.st = name + "_" + print_;
        r.root = name;
        allNames_.insert(upperOf(r.st));
        if (accept(":")) {
            if (!isCpp()) fail("l'h\xC3\xA9ritage est du C++", line);
            if (word("public") || word("private") || word("protected")) next();
            if (word("virtual")) fail("l'h\xC3\xA9ritage virtuel n'est pas pris en charge", line);
            if (peek().k != Tok::K::Name) fail("le nom de la classe parente est attendu" + found(), line);
            const std::string base = next().text;
            const Record* b = record(base);
            if (!b || !b->complete) fail(base + " : classe parente inconnue (d\xC3\xA9" "finis-la plus haut)", line);
            if (punct(",")) fail("l'h\xC3\xA9ritage multiple n'est pas pris en charge (une seule classe parente)", line);
            records_[name].base = base;
            records_[name].root = b->root;
        }
        expect("{", "(le corps de la structure)");
        std::string access = isClass ? "private" : "public";
        std::vector<std::size_t> bodies;                       // les methodes definies dans la classe
        while (!punct("}")) {
            if (at(Tok::K::End)) fail("'}' attendu : la fin de " + name, line);
            attributes();
            if (accept(";")) continue;
            if ((word("public") || word("private") || word("protected")) && punct(":", 1)) {
                if (!isCpp()) fail(peek().text + " : est du C++");
                access = next().text;
                next();
                continue;
            }
            if (word("friend") || word("template") || word("operator")) fail(refusedWord(peek().text));
            if (word("static_assert")) {
                next();
                skipBalanced();
                accept(";");
                continue;
            }
            if (word("typedef")) {
                typedefDeclaration();
                continue;
            }
            if (word("using")) {
                usingDeclaration();
                continue;
            }
            if (word("enum")) {
                Spec es;
                next();
                (void)enumSpec(es);
                expect(";", "apr\xC3\xA8s enum { ... }");
                continue;
            }
            if ((word("struct") || word("class")) && (punct("{", 2) || punct(":", 2)))
                fail("une structure dans une structure : d\xC3\xA9" "finis-la avant (au niveau du script)");
            Record& rr = records_[name];
            // le constructeur, le destructeur
            std::size_t save = i_;
            bool isExplicit = false, isVirtual = false;
            while (word("explicit") || word("virtual") || word("inline") || word("constexpr")) {
                isExplicit = isExplicit || word("explicit");
                isVirtual = isVirtual || word("virtual");
                next();
            }
            if (isCpp() && ((word(name.c_str()) && punct("(", 1)) || (punct("~") && word(name.c_str(), 1)))) {
                const bool dtor = accept("~");
                const int ml = next().line;
                Method m;
                m.c = dtor ? "~" + name : name;
                m.owner = name;
                m.ctor = !dtor;
                m.dtor = dtor;
                m.isVirtual = isVirtual;
                m.access = access;
                m.line = ml;
                parameters(m.params, m.modes);
                if (dtor && !m.params.empty()) fail("un destructeur n'a pas de param\xC3\xA8tre", ml);
                while (word("noexcept") || word("override")) next();
                if (accept("=")) {
                    if (word("default") || word("delete")) next();
                    else fail("= default ou = delete attendu", ml);
                    expect(";", "apr\xC3\xA8s = default");
                    if (m.ctor && m.params.empty()) continue;      // le constructeur par defaut : implicite
                    continue;
                }
                if (punct(":") || punct("{")) {
                    m.bodyAt = i_;
                    m.defined = true;
                    if (accept(":")) {                             // la liste d'initialisation : x(1), Base(2), y{3}
                        while (true) {
                            if (peek().k != Tok::K::Name) fail("un membre est attendu dans la liste d'initialisation" + found());
                            next();
                            if (!punct("(") && !punct("{")) fail("'(' attendu apr\xC3\xA8s le membre de la liste d'initialisation" + found());
                            skipBalanced();
                            if (accept(",")) continue;
                            break;
                        }
                        if (!punct("{")) fail("'{' attendu : le corps du constructeur" + found());
                    }
                    skipBalanced();
                    bodies.push_back(rr.methods.size());
                } else {
                    expect(";", "apr\xC3\xA8s la d\xC3\xA9" "claration du constructeur");
                }
                if (dtor) rr.hasDtor = true;
                addMethod(rr, std::move(m), ml);
                continue;
            }
            i_ = save;
            Spec sp = parseSpec(true);
            bool first = true;
            while (true) {
                Decl d = declarator(sp, false);
                if (d.function) {
                    if (!isCpp()) fail("une fonction dans une struct est du C++ (en C : une fonction qui prend un pointeur)", d.line);
                    if (!first) fail("une m\xC3\xA9thode se d\xC3\xA9" "clare seule", d.line);
                    Method m;
                    m.c = d.name;
                    m.owner = name;
                    m.params = d.params;
                    m.modes = d.modes;
                    m.ret = sp.type->k == CT::K::Void ? nullptr : d.type;
                    if (d.reference) fail("une m\xC3\xA9thode qui rend une r\xC3\xA9" "f\xC3\xA9rence n'est pas prise en charge", d.line);
                    m.isVirtual = sp.isVirtual;
                    m.isStatic = sp.isStatic;
                    m.access = access;
                    m.line = d.line;
                    while (word("const") || word("noexcept") || word("override") || word("final")) next();
                    if (accept("=")) {
                        if (punct("0") || (peek().k == Tok::K::Number && peek().value == 0)) {
                            next();
                            if (!m.isVirtual) fail("= 0 : seulement pour une m\xC3\xA9thode virtuelle", d.line);
                            m.pure = true;
                        } else if (word("default") || word("delete")) {
                            next();
                        } else {
                            fail("= 0 attendu (une m\xC3\xA9thode virtuelle pure)", d.line);
                        }
                        expect(";", "apr\xC3\xA8s la d\xC3\xA9" "claration de la m\xC3\xA9thode");
                    } else if (punct("{")) {
                        m.bodyAt = i_;
                        m.defined = true;
                        skipBalanced();
                        bodies.push_back(rr.methods.size());
                    } else {
                        expect(";", "apr\xC3\xA8s la d\xC3\xA9" "claration de la m\xC3\xA9thode");
                    }
                    addMethod(rr, std::move(m), d.line);
                    break;
                }
                if (sp.isAuto) fail("auto : \xC3\xA9" "cris le type du membre", d.line);
                if (d.reference) fail("un membre r\xC3\xA9" "f\xC3\xA9rence (T& m) n'est pas pris en charge (un pointeur T* l'est)", d.line);
                if (d.unsized) fail("un membre tableau sans taille n'est pas pris en charge", d.line);
                if (sp.type->k == CT::K::Void) fail("void : un membre a un type", d.line);
                if (isRec(d.type) && d.type->rec == name) fail(name + " ne peut pas se contenir (un pointeur " + name + "* le peut)", d.line);
                if (isRec(d.type) && !d.type->rec.empty() && !record(d.type->rec)->complete)
                    fail(d.type->rec + " : structure encore incompl\xC3\xA8te (d\xC3\xA9" "finis-la avant)", d.line);
                for (const auto& m : rr.members)
                    if (m.c == d.name) fail(d.name + " : d\xC3\xA9j\xC3\xA0 un membre de " + name, d.line);
                Member m;
                m.c = d.name;
                m.type = d.type;
                m.access = access;
                m.line = d.line;
                m.owner = name;
                m.isStatic = sp.isStatic;
                if (punct("=") || punct("{")) {                // une valeur par defaut (C++) : relue par les constructeurs
                    if (!isCpp()) fail("une valeur dans la struct est du C++ (en C : \xC3\xA0 l'initialisation de la variable)", d.line);
                    const bool brace = punct("{");
                    if (!brace) next();
                    m.initFrom = i_;
                    if (brace) skipBalanced();
                    else {
                        int depth = 0;
                        while (!at(Tok::K::End)) {
                            if (punct("(") || punct("{") || punct("[")) ++depth;
                            if (punct(")") || punct("}") || punct("]")) --depth;
                            if (depth == 0 && (punct(",") || punct(";"))) break;
                            next();
                        }
                    }
                    m.initTo = i_;
                }
                if (m.isStatic) {
                    m.staticSt = scriptVar(name + "_" + d.name, d.type->st, {}, d.line, false, name + "::" + d.name, d.type);
                }
                rr.members.push_back(std::move(m));
                first = false;
                if (accept(",")) continue;
                expect(";", "apr\xC3\xA8s le membre");
                break;
            }
        }
        r.endLine = next().line;                               // }
        completeRecord(name);
        if (cur_ != &script_ && (!bodies.empty() || records_[name].needsInit))
            fail(name + " : une classe \xC3\xA0 m\xC3\xA9thodes (ou \xC3\xA0 valeurs par d\xC3\xA9" "faut) se d\xC3\xA9" "finit au niveau du script, pas dans une fonction", line);
        // Les methodes definies dans la classe : traduites maintenant (elles voient tous les membres).
        const std::size_t after = i_;
        for (const std::size_t k : bodies) {
            Record& rec = records_[name];
            if (k >= rec.methods.size()) continue;
            i_ = rec.methods[k].bodyAt;
            methodBody(name, k, rec.methods[k].line);
        }
        i_ = after;
        implicitConstructor(name);
    }

    void addMethod(Record& r, Method m, int line) {
        // un nom ST : Classe__methode ; une surcharge (un autre nombre de parametres) : Classe__methode_N
        int same = 0;
        for (const auto& x : r.methods)
            if (x.c == m.c) {
                if (x.params.size() == m.params.size()) fail(m.c + " : d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "clar\xC3\xA9" "e avec " + std::to_string(m.params.size()) + " param\xC3\xA8tre(s) (une surcharge se distingue par le nombre de ses param\xC3\xA8tres)", line);
                ++same;
            }
        const std::string base = r.c + "__" + (m.dtor ? std::string("detruire") : m.c);
        m.st = fresh(same ? base + "_" + std::to_string(m.params.size()) : base, {});
        allNames_.insert(upperOf(m.st));
        // virtuelle : declaree ici, ou deja chez un parent (le meme nom, le meme nombre de parametres)
        if (!m.ctor && !m.isStatic) {
            for (const Record* b = r.base.empty() ? nullptr : record(r.base); b; b = b->base.empty() ? nullptr : record(b->base))
                for (const auto& x : b->methods)
                    if ((x.c == m.c || (x.dtor && m.dtor)) && x.params.size() == m.params.size() && x.isVirtual) {
                        m.isVirtual = true;
                        m.virtualRoot = x.virtualRoot;
                    }
            if (m.isVirtual && m.virtualRoot.empty()) m.virtualRoot = r.c;
        }
        if (m.isVirtual && m.isStatic) fail("une m\xC3\xA9thode static ne peut pas \xC3\xAAtre virtuelle", line);
        r.methods.push_back(std::move(m));
    }

    // La disposition d'une structure (ses tailles, comme un compilateur 64 bits) et sa place dans la structure ST de sa
    // hierarchie : la racine porte les membres de toutes ses classes (une Base* peut viser une Derivee).
    void completeRecord(const std::string& name) {
        Record& r = records_[name];
        int offset = 0, align = 1;
        if (!r.base.empty()) {
            const Record* b = record(r.base);
            offset = b->size;
            align = b->align;
            if (b->size == 1 && b->members.empty()) offset = 0;          // une base vide
        }
        bool anyVirtual = false;
        for (const auto& m : r.methods) anyVirtual = anyVirtual || m.isVirtual;
        r.polymorphic = anyVirtual || (!r.base.empty() && record(r.base)->polymorphic);
        if (r.polymorphic && r.base.empty()) offset = 8;                  // le pointeur de table virtuelle
        for (auto& m : r.members) {
            if (m.isStatic) continue;
            const int a = std::max(1, m.type->align);
            offset = (offset + a - 1) / a * a;
            m.offset = offset;
            offset += m.type->size;
            align = std::max(align, a);
        }
        if (r.polymorphic) align = std::max(align, 8);
        r.size = offset == 0 ? 1 : (offset + align - 1) / align * align;
        r.align = align;
        r.complete = true;
        if (r.polymorphic) r.tag = ++tags_;
        for (const auto& m : r.methods) r.abstract = r.abstract || m.pure;
        // la structure ST de la racine : ses membres, puis ceux de chaque classe derivee (un nom pris : Classe_membre)
        const std::string root = r.root;
        auto& fat = fat_[root];
        auto& names = fatNames_[root];
        r.st = record(root)->st;
        for (auto& m : r.members) {
            if (m.isStatic) continue;
            std::string st = m.c;
            if (reservedSt(st) || names.count(upperOf(st))) st = fresh(r.c + "_" + m.c, names);
            m.st = st;
            names.insert(upperOf(st));
            fat.emplace_back(st, m.type->st);
        }
        // une classe abstraite : ses methodes pures heritees non redefinies
        for (const Record* b = r.base.empty() ? nullptr : record(r.base); b; b = b->base.empty() ? nullptr : record(b->base))
            for (const auto& x : b->methods)
                if (x.pure) {
                    bool done = false;
                    for (const Record* c = &r; c && c != b; c = c->base.empty() ? nullptr : record(c->base))
                        for (const auto& y : c->methods)
                            done = done || (y.c == x.c && y.params.size() == x.params.size() && !y.pure);
                    if (!done) r.abstract = true;
                }
        // un constructeur implicite : sa table virtuelle, des valeurs par defaut, des membres ou un parent a construire
        bool userCtor = false;
        for (const auto& m : r.methods) userCtor = userCtor || m.ctor;
        bool needs = r.polymorphic;
        for (const auto& m : r.members) needs = needs || (!m.isStatic && (m.initTo > m.initFrom || needsConstruction(m.type)));
        if (!r.base.empty()) needs = needs || needsConstruction(recordType(r.base));
        r.needsInit = needs && !userCtor;
    }

    // Un objet de ce type doit-il etre construit (un constructeur, une table virtuelle, des valeurs par defaut) ?
    bool needsConstruction(const CTP& t) const {
        if (!t) return false;
        if (t->k == CT::K::Array) return needsConstruction(t->elem);
        if (t->k != CT::K::Record || t->rec.empty()) return false;
        const Record* r = record(t->rec);
        if (!r) return false;
        if (r->needsInit) return true;
        for (const auto& m : r->methods)
            if (m.ctor) return true;
        return false;
    }

    // typedef T Nom; typedef struct { ... } Nom; typedef int (*Rappel)(int);
    void typedefDeclaration() {
        const int line = next().line;                          // typedef
        Spec sp = parseSpec();
        while (true) {
            Decl d = declarator(sp, true);
            if (d.name.empty()) fail("typedef : un nom est attendu", line);
            if (d.unsized) fail("typedef d'un tableau sans taille n'est pas pris en charge", line);
            CTP t = d.type;
            // typedef struct { ... } Point; typedef enum { ... } Mode; : le type prend ce nom (le debogueur le montre)
            if (isRec(t) && !t->rec.empty() && t->rec.rfind("struct", 0) == 0 && std::isdigit(static_cast<unsigned char>(t->rec.back()))) {
                Record r = records_[t->rec];
                records_.erase(t->rec);
                std::replace(recordOrder_.begin(), recordOrder_.end(), t->rec, d.name);
                if (!records_.count(d.name)) {
                    r.c = d.name;
                    for (auto& m : r.members) m.owner = d.name;
                    if (r.root == t->rec) r.root = d.name;
                    if (fat_.count(t->rec)) {
                        fat_[d.name] = fat_[t->rec];
                        fatNames_[d.name] = fatNames_[t->rec];
                        fat_.erase(t->rec);
                        fatNames_.erase(t->rec);
                    }
                    records_[d.name] = r;
                }
                t = recordType(d.name);
                CT shown = *t;
                shown.shown = d.name;
                t = makeCT(shown);
            } else if (!t->enumName.empty() && t->enumName.rfind("enum", 0) == 0 && t->k == CT::K::Int && !isPtr(t)) {
                CT shown = *t;
                shown.shown = d.name;
                t = makeCT(shown);
            } else if (d.reference) {
                fail("typedef d'une r\xC3\xA9" "f\xC3\xA9rence n'est pas pris en charge", line);
            } else {
                CT shown = *t;
                shown.shown = d.name;
                t = makeCT(shown);
            }
            if (typedefs_.count(d.name) && typedefs_.at(d.name)->st != t->st) fail(d.name + " : d\xC3\xA9j\xC3\xA0 un autre type", line);
            typedefs_[d.name] = t;
            if (!accept(",")) break;
        }
        expect(";", "apr\xC3\xA8s le typedef");
    }

    // using Nom = T; using namespace std; using std::string;
    void usingDeclaration() {
        const int line = next().line;                          // using
        if (word("namespace")) {
            while (!at(Tok::K::End) && !accept(";")) next();
            return;
        }
        if (peek().k == Tok::K::Name && punct("=", 1)) {
            const std::string name = next().text;
            next();
            CTP t = typeName();
            CT shown = *t;
            shown.shown = name;
            typedefs_[name] = makeCT(shown);
            expect(";", "apr\xC3\xA8s using Nom = type");
            return;
        }
        if (peek().k == Tok::K::Name && peek().text.rfind("std::", 0) == 0) {     // using std::string;
            next();
            expect(";", "apr\xC3\xA8s using");
            return;
        }
        fail("using : using Nom = type; ou using namespace std; sont pris en charge", line);
    }

    // ---- le niveau du script : des fonctions, des declarations, des instructions
    void topLevel() {
        attributes();
        if (accept(";")) return;
        if (word("using")) {
            usingDeclaration();
            return;
        }
        if (word("typedef")) {
            typedefDeclaration();
            return;
        }
        if (word("namespace") || word("template")) fail(refusedWord(peek().text));
        if (word("extern") && peek(1).k == Tok::K::String) {      // extern "C" { ... } : le bloc est pris tel quel
            next();
            next();
            if (accept("{")) {
                while (!punct("}") && !at(Tok::K::End)) topLevel();
                expect("}", "\xC3\xA0 la fin de extern \"C\" { ... }");
            }
            return;
        }
        // Classe::methode(...) { ... } et Classe::Classe(...) : ... { ... } : une methode definie hors de sa classe
        if (peek().k == Tok::K::Name && outOfClass(0)) {
            methodOutside();
            return;
        }
        if (startsType()) {
            declaration(true);
            return;
        }
        topStatements_ = true;
        statement();
    }

    // Le jeton k commence-t-il Classe::m( (une methode definie hors de sa classe, sans type rendu : un constructeur) ?
    bool outOfClass(std::size_t k) const {
        const Tok& t = peek(k);
        if (t.k != Tok::K::Name) return false;
        if (records_.count(t.text) && punct("::", k + 1) && punct("~", k + 2)) return true;   // Classe::~Classe (le lexeur coupe avant ~)
        const auto sep = t.text.rfind("::");
        if (sep == std::string::npos) return false;
        const std::string cls = t.text.substr(0, sep);
        if (!records_.count(cls)) return false;
        return punct("(", k + 1) && t.text.substr(sep + 2) == cls;              // Classe::Classe(
    }

    // int a = 1, t[3] = {1, 2, 3}; static int n = 0; Point p = {.x = 1}; Pompe pompe(3); et, au niveau du script, une fonction.
    void declaration(bool topLevel) {
        const Spec base = parseSpec();
        if (base.definedType && accept(";")) return;           // struct X { ... }; enum { ... };
        bool first = true;
        while (true) {
            if (base.type->k == CT::K::Void && !(peek().k == Tok::K::Name && punct("(", 1)) && !punct("*") && !punct("("))
                fail("void : une variable a un type (int, float...)");
            const Decl d = declarator(base, false);
            if (d.function) {
                if (!first || !topLevel || cur_ != &script_ || script_.scopes.size() != 1)
                    fail("une fonction se d\xC3\xA9" "finit au niveau du script, pas dans un bloc", d.line);
                if (!d.dims.empty() || d.reference) fail("une fonction ne rend pas un tableau ni une r\xC3\xA9" "f\xC3\xA9rence", d.line);
                if (d.name.find("::") != std::string::npos) methodOutside(&base, &d);
                else function(base, d);
                return;
            }
            if (d.name.find("::") != std::string::npos) staticMemberDefinition(d);
            else {
                FxScope fx(*this, true, {});
                variable(base, d, topLevel);
            }
            first = false;
            if (accept(",")) continue;
            expect(";", "apr\xC3\xA8s la d\xC3\xA9" "claration");
            return;
        }
    }

    // int Pompe::compte = 0; : la valeur de depart d'un membre static.
    void staticMemberDefinition(const Decl& d) {
        const auto sep = d.name.rfind("::");
        const Member* m = findMember(d.name.substr(0, sep), d.name.substr(sep + 2));
        if (!m || !m->isStatic) fail(d.name + " : pas un membre static d'une classe", d.line);
        if (accept("=")) {
            FxScope fx(*this, false, "une valeur de d\xC3\xA9part s'\xC3\xA9" "crit en constante");
            const X v = ternary();
            if (v.lit == X::Lit::None) fail(d.name + " : sa valeur de d\xC3\xA9part est une constante", d.line);
            for (auto& h : script_.hoisted)
                if (h.st == m->staticSt) h.init = v.st;
        }
    }

    // Une valeur initiale : ses cases (chemin ST, valeur ST) et, si elle est constante et simple (un nombre, un tableau
    // de nombres), son litteral ST.
    struct Cell {
        std::string path, st;
    };
    static bool aggregate(const CTP& t) { return t && ((t->k == CT::K::Array) || (t->k == CT::K::Record && !t->rec.empty())); }

    std::vector<const Member*> layoutMembers(const std::string& rec) const {
        std::vector<const Member*> out;
        std::vector<const Record*> chain;
        for (const Record* r = record(rec); r; r = r->base.empty() ? nullptr : record(r->base)) chain.push_back(r);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            for (const auto& m : (*it)->members)
                if (!m.isStatic) out.push_back(&m);
        return out;
    }

    // Une valeur pour `path` (de type t) : { ... } ou une expression.
    std::string initValue(const CTP& t, const std::string& path, std::vector<Cell>& cells, bool& constant, int line) {
        if (punct("{")) return initBraces(t, path, cells, constant, line);
        X v = valueFor(t, ternary(), line);
        cells.push_back({path, v.st});
        const bool simple = !aggregate(t) && v.lit != X::Lit::None;
        constant = constant && simple;
        return simple ? v.st : std::string{};
    }
    // Une suite de valeurs sans accolades pour un element agrege (int m[2][2] = {1, 2, 3, 4} : les accolades omises).
    std::string initElided(const CTP& t, const std::string& path, std::vector<Cell>& cells, bool& constant, int line) {
        if (isArr(t)) {
            std::vector<std::string> lits(static_cast<std::size_t>(t->n), zeroOf(t->elem));
            for (std::int64_t k = 0; k < t->n; ++k) {
                if (punct("}")) break;
                if (k > 0 && !accept(",")) break;
                if (punct("}")) break;
                const std::string here = path + "[" + std::to_string(k) + "]";
                lits[static_cast<std::size_t>(k)] = aggregate(t->elem) && !punct("{") ? initElided(t->elem, here, cells, constant, line)
                                                                                    : initValue(t->elem, here, cells, constant, line);
            }
            std::string l = "[";
            for (std::size_t k = 0; k < lits.size(); ++k) l += (k ? ", " : "") + lits[k];
            return l + "]";
        }
        const auto members = layoutMembers(t->rec);
        constant = false;
        for (std::size_t k = 0; k < members.size(); ++k) {
            if (punct("}")) break;
            if (k > 0 && !accept(",")) break;
            if (punct("}")) break;
            const std::string here = path + "." + members[k]->st;
            if (aggregate(members[k]->type) && !punct("{")) (void)initElided(members[k]->type, here, cells, constant, line);
            else (void)initValue(members[k]->type, here, cells, constant, line);
        }
        return {};
    }
    std::string initBraces(const CTP& t, const std::string& path, std::vector<Cell>& cells, bool& constant, int line) {
        expect("{", "(une liste de valeurs)");
        if (isArr(t)) {
            std::vector<std::string> lits(static_cast<std::size_t>(t->n), zeroOf(t->elem));
            std::int64_t k = 0;
            bool any = false;
            while (!punct("}")) {
                if (punct("[")) {                                // [4] = 29 : un initialiseur designe
                    next();
                    k = constantSize(line);
                    expect("]", "apr\xC3\xA8s l'indice d\xC3\xA9sign\xC3\xA9");
                    expect("=", "apr\xC3\xA8s [indice]");
                }
                if (k < 0 || k >= t->n) fail("trop de valeurs pour un tableau de " + std::to_string(t->n) + " (case " + std::to_string(k) + ")", line);
                const std::string here = path + "[" + std::to_string(k) + "]";
                if (isText(t->elem) && peek().k == Tok::K::String) lits[static_cast<std::size_t>(k)] = initValue(t->elem, here, cells, constant, line);
                else if (aggregate(t->elem) && !punct("{")) lits[static_cast<std::size_t>(k)] = initElided(t->elem, here, cells, constant, line);
                else lits[static_cast<std::size_t>(k)] = initValue(t->elem, here, cells, constant, line);
                any = true;
                ++k;
                if (!accept(",")) break;
            }
            expect("}", "\xC3\xA0 la fin de la liste");
            if (!any) return {};                                 // {} : tout a zero (le depart de la variable)
            std::string l = "[";
            for (std::size_t q = 0; q < lits.size(); ++q) l += (q ? ", " : "") + lits[q];
            return l + "]";
        }
        if (isRec(t) && !t->rec.empty()) {
            const auto members = layoutMembers(t->rec);
            const Record* r = record(t->rec);
            if (r && r->cpp) {
                for (const auto& m : r->methods)
                    if (m.ctor) fail(t->rec + " a un constructeur : \xC3\xA9" "cris " + t->rec + " x(...) (pas une liste { ... })", line);
            }
            std::size_t k = 0;
            while (!punct("}")) {
                std::string here;
                CTP mt;
                if (punct(".")) {                                // .x = 1 ; .pos.x = 2 : un initialiseur designe
                    std::string p = path;
                    CTP cur = t;
                    while (accept(".")) {
                        if (peek().k != Tok::K::Name) fail("un nom de membre est attendu apr\xC3\xA8s '.'" + found(), line);
                        const std::string mn = next().text;
                        if (!isRec(cur) || cur->rec.empty()) fail("." + mn + " : pas une structure", line);
                        const Member* m = findMember(cur->rec, mn);
                        if (!m || m->isStatic) fail(mn + " n'est pas un membre de " + cur->rec, line);
                        if (cur == t)
                            for (std::size_t q = 0; q < members.size(); ++q)
                                if (members[q] == m) k = q;
                        p += "." + m->st;
                        cur = m->type;
                    }
                    while (punct("[")) {
                        next();
                        const std::int64_t q = constantSize(line);
                        expect("]", "apr\xC3\xA8s l'indice d\xC3\xA9sign\xC3\xA9");
                        if (!isArr(cur) || q < 0 || q >= cur->n) fail("indice d\xC3\xA9sign\xC3\xA9 hors du tableau", line);
                        p += "[" + std::to_string(q) + "]";
                        cur = cur->elem;
                    }
                    expect("=", "apr\xC3\xA8s le membre d\xC3\xA9sign\xC3\xA9");
                    here = p;
                    mt = cur;
                } else {
                    if (k >= members.size()) fail("trop de valeurs pour " + t->shown + " (" + std::to_string(members.size()) + " membres)", line);
                    here = path + "." + members[k]->st;
                    mt = members[k]->type;
                }
                if (aggregate(mt) && !punct("{") && !(isText(mt)) && !(isRec(mt) && !punct("{") && peek().k == Tok::K::Name && findVar(peek().text)))
                    (void)initElided(mt, here, cells, constant, line);
                else (void)initValue(mt, here, cells, constant, line);
                ++k;
                if (!accept(",")) break;
            }
            expect("}", "\xC3\xA0 la fin de la liste");
            constant = false;
            return {};
        }
        if (t && t->k == CT::K::Vector) {
            std::string l = "[";
            bool firstV = true;
            while (!punct("}")) {
                const X v = valueFor(t->elem, ternary(), line);
                l += (firstV ? "" : ", ") + v.st;
                firstV = false;
                if (!accept(",")) break;
            }
            expect("}", "\xC3\xA0 la fin de la liste");
            l += "]";
            if (!firstV) cells.push_back({path, l});
            constant = false;
            return {};
        }
        // un scalaire : {} (zero) ou {valeur}
        if (accept("}")) {
            cells.push_back({path, zeroOf(t)});
            return zeroOf(t);
        }
        const std::string v = initValue(t, path, cells, constant, line);
        accept(",");
        expect("}", "\xC3\xA0 la fin de la liste");
        return v;
    }

    // Une valeur rangee dans une place de type t : un tableau passe a un pointeur (t -> &t[0]), une fonction a un rappel.
    X valueFor(const CTP& t, X v, int line) {
        if (t && t->k == CT::K::Func && v.st.rfind("ADR(", 0) == 0 && !v.varC.empty()) {   // &f d'une fonction IHM : la fonction
            v.st = v.varC;
            v.ty = t;
        }
        if (isPtr(t) && isArr(v.ty) && v.lvalue) {
            v.st = "CPTR_AT(" + v.st + ", 0)";
            v.ty = pointerCT(v.ty->elem);
            v.lvalue = false;
        }
        if (isPtr(t) && v.lit == X::Lit::Int && v.intValue == 0) {     // p = 0 : NULL
            v.st = "NULL";
            v.isNull = true;
        }
        if (t && t->k == CT::K::Bool && !v.boolish && isNum(v.ty) && v.lit == X::Lit::None) v.st = "TO_BOOL(" + v.st + ")";
        if (isRec(t) && isRec(v.ty) && !t->rec.empty() && !v.ty->rec.empty() && t->rec != v.ty->rec && !derivesFrom(v.ty->rec, t->rec))
            fail("un " + v.ty->shown + " ne va pas dans un " + t->shown, line);
        return v;
    }

    // L'appel d'un constructeur pour `path` (de type t) avec ces arguments ; vide : rien a construire.
    std::string construct(const CTP& t, const std::string& path, const std::vector<X>& args, int line, bool asBase = false) {
        if (!t) return {};
        if (t->k == CT::K::Array) {
            if (!needsConstruction(t->elem)) return {};
            if (!args.empty()) fail("un tableau d'objets se construit sans arguments (le constructeur sans param\xC3\xA8tre)", line);
            const std::string i = temp("DINT", line, "i");
            return "FOR " + i + " := 0 TO " + std::to_string(t->n - 1) + " DO " + construct(t->elem, path + "[" + i + "]", {}, line) + " END_FOR;";
        }
        if (t->k != CT::K::Record || t->rec.empty()) return {};
        const Record* r = record(t->rec);
        if (!r) return {};
        if (r->abstract && !asBase) fail(r->c + " est abstraite (une m\xC3\xA9thode virtuelle pure) : elle ne se cr\xC3\xA9" "e pas, une classe d\xC3\xA9riv\xC3\xA9" "e oui", line);
        std::vector<const Method*> ctors;
        for (const auto& m : r->methods)
            if (m.ctor) ctors.push_back(&m);
        if (ctors.empty()) {
            if (!args.empty()) fail(r->c + " n'a pas de constructeur \xC3\xA0 " + std::to_string(args.size()) + " argument(s)", line);
            return r->needsInit ? r->initSt + "(" + path + ");" : std::string{};
        }
        const Method* chosen = nullptr;
        for (const auto* m : ctors)
            if (m->params.size() == args.size()) chosen = m;
        if (!chosen) {
            if (args.empty()) fail(r->c + " n'a pas de constructeur sans param\xC3\xA8tre : \xC3\xA9" "cris " + r->c + " x(...)", line);
            fail(r->c + " n'a pas de constructeur \xC3\xA0 " + std::to_string(args.size()) + " argument(s)", line);
        }
        checkAccess(chosen->access, chosen->owner, chosen->c, line);
        return chosen->st + "(" + path + argumentText(chosen->params, chosen->modes, args, line, true) + ");";
    }

    // Les arguments d'un appel, convertis pour chaque parametre (un tableau vers un pointeur, &x, une reference).
    std::string argumentText(const std::vector<std::pair<std::string, CTP>>& params, const std::vector<int>& modes, const std::vector<X>& args,
                             int line, bool leadingComma) {
        std::string out;
        for (std::size_t k = 0; k < args.size(); ++k) {
            X a = args[k];
            const CTP pt = k < params.size() ? params[k].second : nullptr;
            const int mode = k < modes.size() ? modes[k] : 0;
            if (mode == 1 && !a.lvalue)
                fail("le param\xC3\xA8tre " + (k < params.size() ? params[k].first : std::string("?")) + " est une r\xC3\xA9" "f\xC3\xA9rence : il faut une variable", line);
            if (pt) a = valueFor(pt, a, line);
            out += (out.empty() && !leadingComma ? "" : ", ") + a.st;
        }
        return out;
    }

    void variable(const Spec& base, const Decl& dIn, bool topLevel) {
        Decl d = dIn;
        const int line = d.line;
        const bool keep = base.isStatic;
        if (d.reference) {
            localReference(base, d);
            return;
        }
        if (isRec(d.type) && !d.type->rec.empty() && !record(d.type->rec)->complete)
            fail(d.type->rec + " : structure incompl\xC3\xA8te (d\xC3\xA9" "clar\xC3\xA9" "e, pas encore d\xC3\xA9" "finie)", line);
        // L'initialisation : lue avant de declarer (int x = x; lit l'autre x, comme en C apres sa declaration... ici la valeur d'avant).
        std::vector<Cell> cells;
        std::vector<X> ctorArgs;
        bool hasInit = false, constant = true, ctor = false, braces = false;
        std::string literal;
        CTP type = d.type;
        const std::string pathMark = "\x01";                     // le nom ST de la variable, mis a la fin
        const bool classType = isRec(type) && !type->rec.empty() && record(type->rec)->cpp && hasConstructors(type->rec);
        struct Expect {
            std::vector<CTP>& v;
            bool              on;
            ~Expect() {
                if (on) v.pop_back();
            }
        } expectGuard{expectPtr_, isPtr(type)};
        if (isPtr(type)) expectPtr_.push_back(type);
        if (accept("=")) {
            hasInit = true;
            if (punct("{")) {
                braces = true;
                if (d.unsized) type = arrayCT(type, countInit());
                literal = initBraces(type, pathMark, cells, constant, line);
            } else if (classType && word(type->rec.c_str()) && (punct("(", 1) || punct("{", 1))) {   // Pompe p = Pompe(3);
                next();
                ctor = true;
                ctorArgs = constructorArgs();
            } else {
                X init = ternary();
                if (base.isAuto) {
                    type = autoType(init, d.name, line);
                    d.type = type;
                }
                if (d.unsized) {
                    if (init.lit == X::Lit::Text && isText(type)) {}
                    else fail("[] : la taille du tableau vient de sa liste { ... }", line);
                }
                if (isArr(type) && !(isArr(init.ty) && init.lvalue)) fail("un tableau s'initialise par { ... }", line);
                init = valueFor(type, init, line);
                cells.push_back({pathMark, init.st});
                const bool simple = !aggregate(type) && init.lit != X::Lit::None;
                constant = simple;
                literal = simple ? init.st : std::string{};
            }
        } else if (punct("(") && isCpp()) {                      // C++ : int x(5); Pompe p(3); std::vector<int> v(5);
            hasInit = true;
            if (classType || (isRec(type) && !type->rec.empty())) {
                ctor = true;
                ctorArgs = constructorArgs();
            } else if (type->k == CT::K::Vector) {
                next();
                const X n = ternary();
                std::string fill;
                if (accept(",")) fill = ", " + ternary().st;
                expect(")", "apr\xC3\xA8s std::vector<T> v(n");
                cells.push_back({"\x03", "VECTOR_RESIZE(" + pathMark + ", " + n.st + fill + ");"});
                constant = false;
            } else {
                next();
                X init = valueFor(type, ternary(), line);
                expect(")", "apr\xC3\xA8s la valeur");
                if (base.isAuto) {
                    type = autoType(init, d.name, line);
                    d.type = type;
                }
                cells.push_back({pathMark, init.st});
                constant = !aggregate(type) && init.lit != X::Lit::None;
                literal = constant ? init.st : std::string{};
            }
        } else if (punct("{") && isCpp()) {                      // C++ : int x{5}; Point p{1, 2}; Pompe p{3};
            hasInit = true;
            if (classType) {
                ctor = true;
                ctorArgs = constructorArgs();
            } else {
                braces = true;
                if (d.unsized) type = arrayCT(type, countInit());
                literal = initBraces(type, pathMark, cells, constant, line);
            }
        } else if (base.isAuto) {
            fail("auto " + d.name + " : une valeur initiale est attendue", line);
        } else if (d.unsized) {
            fail("[] : la taille du tableau est attendue (T t[10]) ou vient de sa liste { ... }", line);
        }
        if (type->k == CT::K::Void || type->k == CT::K::Unknown) fail(d.name + " : son type ne se d\xC3\xA9" "duit pas (\xC3\xA9" "cris-le)", line);
        if (isRec(type) && !type->rec.empty() && record(type->rec)->abstract)
            fail(type->rec + " est abstraite (une m\xC3\xA9thode virtuelle pure) : elle ne se cr\xC3\xA9" "e pas, une classe d\xC3\xA9riv\xC3\xA9" "e oui", line);
        if (isRec(type) && !type->rec.empty() && record(type->rec)->hasDtor && !keep)
            fail(type->rec + " a un destructeur : un objet local ne l'appellerait pas ici - cr\xC3\xA9" "e-le par new et lib\xC3\xA8re-le par delete", line);
        // static dans une fonction : une variable du script, gardee (son nom C dans le debogueur : f::n)
        std::string st;
        const bool fnStatic = keep && cur_ != &script_;
        if (fnStatic) {
            st = scriptVar(cur_->stName + "_" + d.name, type->st, {}, line, false, cur_->stName + "::" + d.name, type);
            Var v;
            v.st = st;
            v.type = type;
            v.isConst = base.isConst;
            v.c = d.name;
            if (cur_->scopes.back().count(d.name)) fail(d.name + " est d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "clar\xC3\xA9" "e dans ce bloc", line);
            cur_->scopes.back()[d.name] = v;
        } else {
            st = declare(d.name, type, line);
            cur_->scopes.back()[d.name].isConst = base.isConst;
            noteVar(st, d.name, type);
        }
        if (base.isConst && hasInit && !braces && !ctor) {
            for (const auto& c : cells)
                if (c.path == pathMark && !literal.empty() && type->k == CT::K::Int) {
                    char* end = nullptr;
                    const long long v = std::strtoll(literal.c_str(), &end, 10);
                    if (end && *end == '\0') constInts_[d.name] = v;
                }
        }
        // les textes : le nom ST de la variable a sa place
        std::string text;
        for (const auto& c : cells) {
            std::string p = c.path, v = c.st;
            for (std::string* s : {&p, &v})
                for (std::size_t at = s->find(pathMark); at != std::string::npos; at = s->find(pathMark)) s->replace(at, 1, st);
            text += (text.empty() ? "" : " ") + (p == "\x03" ? v : p + " := " + v + ";");
        }
        // un objet : son constructeur (ses arguments, ou celui sans parametre ; un tableau d'objets : chacun)
        const std::string made = ctor || needsConstruction(type) ? construct(type, st, ctorArgs, line) : std::string{};
        // une liste partielle dans une boucle : les autres cases repartent de zero (la variable est rangee en tete)
        if (braces && !loops_.empty() && !keep && aggregate(type)) text = st + " := " + temp(type->st, line, "z") + "; " + text;
        if (!made.empty()) text = made + (text.empty() ? "" : " " + text);
        if (!fnStatic) {
            const bool sameScriptVar = keep && cur_ == &script_;
            std::size_t index = cur_->hoisted.size();
            for (std::size_t k = 0; k < cur_->hoisted.size(); ++k)
                if (upperOf(cur_->hoisted[k].st) == upperOf(st)) index = k;
            if (index == cur_->hoisted.size()) cur_->hoisted.push_back(Hoisted{st, type->st, {}, sameScriptVar, line});
            if (sameScriptVar) {
                // static au niveau du script : sa valeur une fois (une constante) ; sinon calculee a la premiere execution
                if (constant && !literal.empty() && !made.size()) cur_->hoisted[index].init = literal;
                else if (!text.empty()) emitStatement(line, guardedOnce(text, line));
                return;
            }
            std::size_t assign = static_cast<std::size_t>(-1);
            if (!text.empty()) assign = emitStatementIndex(line, text);
            else if (!fx_.empty() && (!fx_.back().pre.empty() || !fx_.back().post.empty())) assign = emitStatementIndex(line, "");
            if (topLevel && cur_ == &script_)
                globals_.push_back({index, assign, constant && made.empty() && !(braces && literal.empty() && !cells.empty()) ? literal : std::string{}, line, hasInit || !made.empty(),
                                    (!hasInit && made.empty()) || (constant && made.empty() && (!literal.empty() || cells.empty()))});
            return;
        }
        // static dans une fonction : une constante a la declaration, sinon la premiere fois que la fonction passe la
        if (constant && !literal.empty() && made.empty()) {
            for (auto& h : script_.hoisted)
                if (h.st == st) h.init = literal;
        } else if (!text.empty()) {
            emitStatement(line, guardedOnce(text, line));
        }
    }
    std::size_t emitStatementIndex(int line, const std::string& text) {
        emitStatement(line, text);
        return frags_.size() - 1;
    }
    // Une initialisation faite une seule fois (un static calcule) : un drapeau garde.
    std::string guardedOnce(const std::string& text, int line) {
        const std::string flag = scriptVar("cinit", "BOOL", {}, line, true);
        return "IF NOT " + flag + " THEN " + flag + " := TRUE; " + text + " END_IF;";
    }
    bool hasConstructors(const std::string& rec) const {
        const Record* r = record(rec);
        if (!r) return false;
        for (const auto& m : r->methods)
            if (m.ctor) return true;
        return false;
    }
    // Les arguments d'un constructeur : ( ... ) ou { ... }.
    std::vector<X> constructorArgs() {
        const bool brace = punct("{");
        expect(brace ? "{" : "(", "");
        std::vector<X> args;
        while (!punct(brace ? "}" : ")")) {
            args.push_back(ternary());
            if (!accept(",")) break;
        }
        expect(brace ? "}" : ")", "apr\xC3\xA8s les arguments du constructeur");
        return args;
    }
    // Le nombre de valeurs d'une liste { ... } (int t[] = {1, 2, 3}) : sans la lire.
    std::int64_t countInit() {
        std::int64_t n = 0, depth = 0, maxIndex = -1, k = 0;
        bool any = false;
        for (std::size_t j = i_; j < toks_.size(); ++j) {
            const Tok& t = toks_[j];
            if (t.k == Tok::K::End) break;
            if (t.k == Tok::K::Punct && (t.text == "{" || t.text == "(" || t.text == "[")) {
                if (depth == 1 && t.text == "[" && j + 1 < toks_.size() && toks_[j + 1].k == Tok::K::Number) {
                    k = toks_[j + 1].value;                    // [4] = 29
                }
                ++depth;
                if (depth == 2) any = true;
                continue;
            }
            if (t.k == Tok::K::Punct && (t.text == "}" || t.text == ")" || t.text == "]")) {
                if (--depth == 0) break;
                continue;
            }
            if (depth == 1 && t.k == Tok::K::Punct && t.text == ",") {
                maxIndex = std::max(maxIndex, k);
                ++k;
                any = false;
                continue;
            }
            if (depth == 1) any = true;
        }
        if (any) maxIndex = std::max(maxIndex, k);
        n = maxIndex + 1;
        if (n <= 0) fail("[] : une liste vide ne donne pas de taille");
        return n;
    }
    CTP autoType(const X& init, const std::string& name, int line) {
        if (init.ty && init.ty->k != CT::K::Unknown && init.ty->k != CT::K::Void) {
            if (init.ty->k == CT::K::Array) return pointerCT(init.ty->elem);         // auto p = t; : un pointeur
            return init.ty;
        }
        switch (init.lit) {
            case X::Lit::Int: return tInt();
            case X::Lit::Real: return tDouble();
            case X::Lit::Bool: return tBool();
            case X::Lit::Text: return tText();
            default: break;
        }
        fail("auto " + name + " : \xC3\xA9" "cris son type (le moteur ne le d\xC3\xA9" "duit pas de cette valeur)", line);
    }

    // T& r = x; : un POINTER TO T lu r^ (la reference suit x).
    void localReference(const Spec& base, const Decl& d) {
        if (!accept("=")) fail("une r\xC3\xA9" "f\xC3\xA9rence (" + d.name + ") s'initialise : T& r = x;", d.line);
        X target = ternary();
        if (!target.lvalue) fail("une r\xC3\xA9" "f\xC3\xA9rence (" + d.name + ") vise une variable", d.line);
        const std::string addr = addressOf(target, d.line).st;
        const CTP t = d.type;
        const std::string st = declare(d.name, t, d.line);
        auto& v = cur_->scopes.back()[d.name];
        v.autoDeref = true;
        v.isConst = base.isConst;
        noteVar(st, d.name + " (&)", t);
        cur_->hoisted.push_back(Hoisted{st, "POINTER TO " + t->st, {}, false, d.line});
        emitStatement(d.line, st + " := " + addr + ";");
    }

    // ---- les fonctions et les methodes
    std::string paramDecl(const std::string& st, const CTP& type, int mode) const {
        return std::string(mode == 1 ? "VAR_IN_OUT " : "") + st + " : " + type->st;
    }
    // Les parametres declares dans le code d'une fonction : leur texte ST.
    std::string declareParams(const std::vector<std::pair<std::string, CTP>>& params, const std::vector<int>& modes, int line) {
        std::string out;
        for (std::size_t k = 0; k < params.size(); ++k) {
            std::string name = params[k].first;
            if (name.empty()) name = "p" + std::to_string(k + 1);
            if (params[k].second->k == CT::K::Void) fail("void : un param\xC3\xA8tre a un type", line);
            const std::string pst = declare(name, params[k].second, line);
            DebugVar dv;
            noteVar(pst, name, params[k].second);
            out += (out.empty() ? "" : "; ") + paramDecl(pst, params[k].second, k < modes.size() ? modes[k] : 0);
        }
        return out;
    }
    // Un parametre objet passe par valeur (une copie) : sa table virtuelle est celle de son type (le C++ le tranche).
    std::string slicedParams(const std::vector<std::pair<std::string, CTP>>& params, const std::vector<int>& modes) {
        std::string out;
        for (std::size_t k = 0; k < params.size(); ++k) {
            const CTP& t = params[k].second;
            if ((k < modes.size() && modes[k] != 0) || !isRec(t) || t->rec.empty()) continue;
            const Record* r = record(t->rec);
            if (!r || !r->polymorphic) continue;
            const Var* v = findVar(params[k].first.empty() ? "p" + std::to_string(k + 1) : params[k].first);
            if (v) out += v->st + ".cvt_ := " + std::to_string(r->tag) + "; ";
        }
        return out;
    }

    // type nom(params) { corps } : une FUNCTION interne ; un prototype (;) : son type.
    void function(const Spec& ret, const Decl& d) {
        if (ret.isAuto) fail("auto : \xC3\xA9" "cris le type rendu par " + d.name, d.line);
        FnInfo* info = findFunction(d.name, d.params.size(), d.line);
        if (!info) {                                            // pas vue par le premier passage : la creer
            auto& list = functions_[d.name];
            list.emplace_back();
            info = &list.back();
            info->params = d.params.size();
            info->st = fresh(d.name + "_c", {});
        }
        info->ret = d.type && d.type->k != CT::K::Void ? d.type : nullptr;
        info->ptypes = d.params;
        info->modes = d.modes;
        info->declared = true;
        const std::string stName = info->st;
        if (!punct("{") && !punct(";")) fail("'{' attendu : le corps de la fonction" + found());
        if (accept(";")) return;                               // un prototype
        if (info->defined && definedNow_.count(stName)) fail(d.name + " est d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "finie", d.line);
        definedNow_.insert(stName);
        Code fn;
        fn.function = true;
        fn.result = info->ret;
        fn.stName = stName;
        fn.scopes.emplace_back();
        fn.taken.insert(upperOf(stName));
        for (const auto& [cname, list] : functions_)
            for (const auto& fi : list) fn.taken.insert(upperOf(fi.st));
        Code* saved = cur_;
        cur_ = &fn;
        const std::string params = declareParams(d.params, d.modes, d.line);
        const std::size_t header = emit(d.line, "FUNCTION " + stName + "(" + params + ")" + (info->ret ? " : " + info->ret->st : std::string{}));
        if (const std::string sliced = slicedParams(d.params, d.modes); !sliced.empty()) emit(d.line, sliced);
        functionBody(d.name, d.line);
        finishCode(fn, header, d.name, d.line);
        cur_ = saved;
    }
    // Le corps { ... } de la fonction en cours ; END_FUNCTION a sa ligne.
    void functionBody(const std::string& name, int line) {
        expect("{", "(le corps de la fonction)");
        cur_->scopes.emplace_back();
        while (!punct("}")) {
            if (at(Tok::K::End)) fail("'}' attendu : la fin de la fonction " + name, line);
            statement();
        }
        probeHere(peek().line, true);
        const int close = next().line;
        cur_->scopes.pop_back();
        std::string end;
        end.swap(endPrefix_);
        emit(close, end + "END_FUNCTION");
    }
    void finishCode(Code& fn, std::size_t header, const std::string& cname, int line) {
        (void)line;
        const std::string decls = declarationBlock(fn, false);
        if (!decls.empty()) frags_[header].text += " " + decls;
        note(fn, frags_[header].line);
        debug_->functions[upperOf(fn.stName)] = cname;
        auto& vars = debug_->vars[upperOf(fn.stName)];
        vars.insert(vars.end(), fn.dbg.begin(), fn.dbg.end());
    }

    // Une methode : FUNCTION Classe__m(VAR_IN_OUT this_c : Racine; params) ; un constructeur : le parent, la table
    // virtuelle, les membres (la liste d'initialisation, les valeurs par defaut), puis le corps.
    void methodBody(const std::string& cls, std::size_t index, int line) {
        Record& r = records_[cls];
        Method m = r.methods[index];
        if (definedNow_.count(m.st)) fail(cls + "::" + m.c + " est d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "finie", line);
        definedNow_.insert(m.st);
        r.methods[index].defined = true;
        Code fn;
        fn.function = true;
        fn.result = m.ret;
        fn.stName = m.st;
        fn.recordC = cls;
        fn.scopes.emplace_back();
        fn.taken.insert(upperOf(m.st));
        fn.taken.insert("THIS_C");
        for (const auto& [cname, list] : functions_)
            for (const auto& fi : list) fn.taken.insert(upperOf(fi.st));
        Code* saved = cur_;
        cur_ = &fn;
        std::string params = m.isStatic ? std::string{} : "VAR_IN_OUT this_c : " + r.st;
        if (!m.isStatic) {
            DebugVar dv;
            dv.st = "this_c";
            dv.c = "this";
            dv.type = cls + "*";
            dv.record = cls;
            fn.dbg.push_back(dv);
        }
        const std::string more = declareParams(m.params, m.modes, line);
        if (!more.empty()) params += (params.empty() ? "" : "; ") + more;
        const std::size_t header = emit(line, "FUNCTION " + m.st + "(" + params + ")" + (m.ret ? " : " + m.ret->st : std::string{}));
        if (const std::string sliced = slicedParams(m.params, m.modes); !sliced.empty()) emit(line, sliced);
        if (m.ctor) constructorPrologue(cls, line);
        if (m.dtor && !r.base.empty())                         // le destructeur du parent, apres le corps (comme le C++)
            for (const Record* b = record(r.base); b; b = b->base.empty() ? nullptr : record(b->base)) {
                bool found = false;
                for (const auto& x : b->methods)
                    if (x.dtor) {
                        endPrefix_ = x.st + "(this_c); ";
                        found = true;
                    }
                if (found) break;
            }
        functionBody(cls + "::" + m.c, line);
        finishCode(fn, header, cls + "::" + m.c, line);
        cur_ = saved;
    }

    // Le debut d'un constructeur (au jeton : ou {) : la liste d'initialisation lue, puis le parent, la table virtuelle et
    // les membres, dans l'ordre de leur declaration (comme le C++).
    void constructorPrologue(const std::string& cls, int line) {
        std::map<std::string, std::size_t> given;              // membre (ou parent) -> jeton de ses arguments
        if (accept(":")) {
            while (true) {
                const std::string n = next().text;
                if (given.count(n)) fail(n + " : deux fois dans la liste d'initialisation", line);
                given[n] = i_;
                skipBalanced();
                if (!accept(",")) break;
            }
        }
        const std::size_t body = i_;
        const Record& r = records_[cls];
        std::string text;
        FxScope fx(*this, true, {});
        if (!r.base.empty()) {
            std::vector<X> args;
            if (const auto it = given.find(r.base); it != given.end()) {
                i_ = it->second;
                args = constructorArgs();
                given.erase(it);
            }
            text += construct(recordType(r.base), "this_c", args, line, true) + " ";
        }
        if (r.polymorphic) text += "this_c.cvt_ := " + std::to_string(r.tag) + "; ";
        for (const auto& mem : r.members) {
            if (mem.isStatic) continue;
            const std::string path = "this_c." + mem.st;
            if (const auto it = given.find(mem.c); it != given.end()) {
                i_ = it->second;
                given.erase(it);
                if (isRec(mem.type) && hasConstructors(mem.type->rec)) {
                    text += construct(mem.type, path, constructorArgs(), line) + " ";
                } else if (punct("{") && aggregate(mem.type)) {
                    std::vector<Cell> cells;
                    bool constant = true;
                    (void)initBraces(mem.type, path, cells, constant, line);
                    for (const auto& c : cells) text += c.path + " := " + c.st + "; ";
                } else {
                    const bool brace = punct("{");
                    next();
                    if (!punct(brace ? "}" : ")")) {
                        const X v = valueFor(mem.type, ternary(), line);
                        text += path + " := " + v.st + "; ";
                    }
                    expect(brace ? "}" : ")", "apr\xC3\xA8s la valeur du membre");
                }
                continue;
            }
            text += memberDefault(mem, path, line);
        }
        if (!given.empty()) fail(given.begin()->first + " : ni un membre ni la classe parente de " + cls, line);
        i_ = body;
        std::string all;
        for (const auto& p : fx.fx().pre) all += p + " ";
        all += text;
        for (const auto& p : fx.fx().post) all += " " + p;
        fx.fx().pre.clear();
        fx.fx().post.clear();
        if (!all.empty() && all.find_first_not_of(' ') != std::string::npos) emit(line, all);
    }
    // La valeur par defaut d'un membre (int x = 5; dans la classe), ou sa construction ; vide : rien.
    std::string memberDefault(const Member& mem, const std::string& path, int line) {
        std::string text;
        if (mem.initTo > mem.initFrom) {
            const std::size_t save = i_;
            i_ = mem.initFrom;
            std::vector<Cell> cells;
            bool constant = true;
            if (punct("{") && !aggregate(mem.type)) {
                next();
                if (!punct("}")) cells.push_back({path, valueFor(mem.type, ternary(), line).st});
                else cells.push_back({path, zeroOf(mem.type)});
            } else {
                (void)initValue(mem.type, path, cells, constant, line);
            }
            i_ = save;
            for (const auto& c : cells) text += c.path + " := " + c.st + "; ";
        } else if (needsConstruction(mem.type)) {
            text += construct(mem.type, path, {}, line) + " ";
        }
        return text;
    }

    // Le constructeur implicite d'une classe qui en a besoin (sa table virtuelle, des valeurs par defaut), a sa fin.
    void implicitConstructor(const std::string& cls) {
        Record& r = records_[cls];
        if (!r.needsInit) return;
        r.initSt = fresh(cls + "__init", {});
        allNames_.insert(upperOf(r.initSt));
        Code fn;
        fn.function = true;
        fn.stName = r.initSt;
        fn.recordC = cls;
        fn.scopes.emplace_back();
        Code* saved = cur_;
        cur_ = &fn;
        const int line = r.endLine;
        const std::size_t header = emit(line, "FUNCTION " + r.initSt + "(VAR_IN_OUT this_c : " + r.st + ")");
        FxScope fx(*this, true, {});
        std::string text;
        if (!r.base.empty()) text += construct(recordType(r.base), "this_c", {}, line, true) + " ";
        if (r.polymorphic) text += "this_c.cvt_ := " + std::to_string(r.tag) + "; ";
        for (const auto& mem : r.members)
            if (!mem.isStatic) text += memberDefault(mem, "this_c." + mem.st, line);
        std::string all;
        for (const auto& p : fx.fx().pre) all += p + " ";
        emit(line, all + text + " END_FUNCTION");
        finishCode(fn, header, cls + "::" + cls + " (implicite)", line);
        debug_->hidden.insert(upperOf(r.initSt));
        cur_ = saved;
    }

    // void Pompe::demarrer() { ... }, Pompe::Pompe(int n) : x(n) { ... }, Pompe::~Pompe() { ... } : definie hors de la classe.
    void methodOutside(const Spec* ret = nullptr, const Decl* d = nullptr) {
        std::string cls, name;
        std::vector<std::pair<std::string, CTP>> params;
        std::vector<int> modes;
        int line = peek().line;
        if (d) {
            const auto sep = d->name.rfind("::");
            cls = d->name.substr(0, sep);
            name = d->name.substr(sep + 2);
            params = d->params;
            line = d->line;
            (void)ret;
        } else {
            const Tok& t = next();
            line = t.line;
            if (accept("::")) {                                  // Classe::~Classe
                expect("~", "");
                cls = t.text;
                next();
                name = "~" + cls;
            } else {
                const auto sep = t.text.rfind("::");
                cls = t.text.substr(0, sep);
                name = t.text.substr(sep + 2);
            }
            parameters(params, modes);
        }
        Record* r = record(cls);
        if (!r || !r->complete) fail(cls + " : classe inconnue (d\xC3\xA9" "finis-la plus haut)", line);
        std::size_t index = r->methods.size();
        for (std::size_t k = 0; k < r->methods.size(); ++k)
            if (r->methods[k].c == name && r->methods[k].params.size() == params.size()) index = k;
        if (index == r->methods.size()) fail(cls + "::" + name + " n'est pas d\xC3\xA9" "clar\xC3\xA9" "e dans la classe " + cls, line);
        if (r->methods[index].defined) fail(cls + "::" + name + " est d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "finie", line);
        // les noms des parametres : ceux de la definition
        for (std::size_t k = 0; k < params.size(); ++k) r->methods[index].params[k].first = params[k].first;
        while (word("const") || word("noexcept") || word("override") || word("final")) next();
        if (cur_ != &script_ || script_.scopes.size() != 1) fail("une m\xC3\xA9thode se d\xC3\xA9" "finit au niveau du script", line);
        methodBody(cls, index, line);
    }

    void note(const Code& c, int line) {
        if (c.hoisted.empty()) return;
        declarationLines_.push_back(line);
        for (const auto& h : c.hoisted) declared_.emplace_back(upperOf(h.st), h.line);
        for (const auto& h : c.hoisted) {                    // le type nomme (un type inconnu se dit par son nom)
            std::string t = h.type;
            while (t.rfind("ARRAY[", 0) == 0 && t.find(" OF ") != std::string::npos) t = t.substr(t.find(" OF ") + 4);
            if (t.rfind("POINTER TO ", 0) == 0) t = t.substr(11);
            if (!stElementary().count(t)) declared_.emplace_back(upperOf(t), h.line);
        }
    }

    std::string declarationBlock(const Code& c, bool script) const {
        std::string keep, temp;
        for (const auto& h : c.hoisted) {
            const std::string one = h.st + " : " + h.type + (h.init.empty() ? std::string{} : " := " + h.init) + "; ";
            (h.keep ? keep : temp) += one;
        }
        std::string out;
        if (!keep.empty()) out += "VAR " + keep + "END_VAR";
        if (!temp.empty()) out += std::string(out.empty() ? "" : " ") + (script ? "VAR_TEMP " : "VAR ") + temp + "END_VAR";
        return out;
    }

    // ---- les instructions
    struct Loop {
        enum class K : std::uint8_t { Loop, Switch, SwitchGeneral } k{K::Loop};
        std::string step;            // un for : son pas (ecrit avant CONTINUE) ; un do while a condition calculee : son calcul
        int         stepLine{0};
        std::string cont;            // un switch general : son drapeau continue (vide : pas employe)
        bool        contUsed{false};
    };
    std::vector<Loop> loops_;

    void block() {
        expect("{", "");
        cur_->scopes.emplace_back();
        const int open = peek().line;
        while (!punct("}")) {
            if (at(Tok::K::End)) fail("'}' attendu : le bloc ouvert ligne " + std::to_string(open) + " n'est pas ferm\xC3\xA9");
            statement();
        }
        probeHere(peek().line, true);
        next();
        cur_->scopes.pop_back();
    }

    // Une instruction seule (le corps d'un if sans accolades) : un bloc a elle (ses declarations n'en sortent pas).
    void body() {
        cur_->scopes.emplace_back();
        statement();
        cur_->scopes.pop_back();
    }

    void statement() {
        if (++depth_ > 200) fail("trop d'imbrications");
        struct Down {
            int& d;
            ~Down() { --d; }
        } down{depth_};
        attributes();
        const Tok& t = peek();
        const int line = t.line;
        probeHere(line, false);
        FxScope fx(*this, true, {});
        if (accept(";")) return;
        if (punct("{")) {
            block();
            return;
        }
        if (t.k == Tok::K::Name) {
            const std::string w = t.text;
            if (w == "if") return ifStatement();
            if (w == "while") return whileStatement();
            if (w == "do") return doStatement();
            if (w == "for") return forStatement();
            if (w == "switch") return switchStatement();
            if (w == "break") {
                next();
                expect(";", "apr\xC3\xA8s break");
                if (loops_.empty()) fail("break hors d'une boucle ou d'un switch", line);
                if (loops_.back().k == Loop::K::Switch) throw SwitchRetry{};
                emit(line, "EXIT;");
                return;
            }
            if (w == "continue") {
                next();
                expect(";", "apr\xC3\xA8s continue");
                Loop* loop = nullptr;
                Loop* sw = nullptr;
                for (auto it = loops_.rbegin(); it != loops_.rend(); ++it) {
                    if (it->k == Loop::K::Loop) {
                        loop = &*it;
                        break;
                    }
                    if (it->k == Loop::K::Switch) throw SwitchRetry{};       // continue dans un switch : la forme generale
                    if (!sw) sw = &*it;
                }
                if (!loop) fail("continue hors d'une boucle", line);
                if (sw) {                                    // dans un switch traduit en REPEAT : sortir, puis continuer
                    sw->contUsed = true;
                    emit(line, sw->cont + " := TRUE; EXIT;");
                    return;
                }
                emit(line, loop->step + (loop->step.empty() ? "" : " ") + "CONTINUE;");
                return;
            }
            if (w == "return") return returnStatement();
            if (w == "using") {
                usingDeclaration();
                return;
            }
            if (w == "typedef") {
                typedefDeclaration();
                return;
            }
            if (w == "delete") return deleteStatement();
            if (w == "else") fail("else sans if", line);
            if (w == "case" || w == "default") fail(w + " hors d'un switch", line);
            if (w == "std::cout" || w == "cout" || w == "std::cerr" || w == "cerr") return coutStatement();
            if (const std::string why = refusedWord(w); !why.empty()) fail(why, line);
            if (peek(1).k == Tok::K::Punct && peek(1).text == ":" && !punct("::", 1) && t.text.find("::") == std::string::npos && !findVar(w))
                fail(w + ": une \xC3\xA9tiquette (goto) n'est pas prise en charge", line);
            if ((w == "struct" || w == "class") && (punct("{", 1) || punct("{", 2) || punct(":", 2)) && cur_ != &script_) {
                // une struct definie dans une fonction : sans methode (elle vit au niveau du script)
            }
            if (startsType()) {
                declaration(false);
                return;
            }
        }
        expressionStatement(line, true);
    }

    // La condition d'un if, d'un while... : son texte BOOL ; ses effets (x++ > 3) avant, un temporaire s'il y en a apres.
    struct Cond {
        std::string pre, text;
        bool        effects{false};
    };
    Cond condition(int line) {
        FxScope fx(*this, true, {});
        const X c = expression();
        Cond out;
        out.text = boolOf(c);
        for (const auto& p : fx.fx().pre) out.pre += p + " ";
        if (!fx.fx().post.empty()) {
            const std::string tmp = temp("BOOL", line, "c");
            out.pre += tmp + " := " + out.text + "; ";
            for (const auto& p : fx.fx().post) out.pre += p + " ";
            out.text = tmp;
        }
        out.effects = !out.pre.empty();
        return out;
    }

    void ifStatement() {
        const int line = next().line;
        expect("(", "apr\xC3\xA8s if");
        const Cond c = condition(line);
        expect(")", "apr\xC3\xA8s la condition du if");
        emit(line, c.pre + "IF " + c.text + " THEN");
        body();
        if (word("else")) {
            emit(next().line, "ELSE");
            body();
        }
        emit(peekPrevLine(), "END_IF;");
    }
    int peekPrevLine() const { return i_ > 0 ? toks_[i_ - 1].line : 1; }

    void whileStatement() {
        const int line = next().line;
        expect("(", "apr\xC3\xA8s while");
        const Cond c = condition(line);
        expect(")", "apr\xC3\xA8s la condition du while");
        if (c.effects) emit(line, "WHILE TRUE DO " + c.pre + "IF NOT (" + c.text + ") THEN EXIT; END_IF;");
        else emit(line, "WHILE " + c.text + " DO");
        loops_.push_back({});
        body();
        loops_.pop_back();
        emit(peekPrevLine(), "END_WHILE;");
    }

    void doStatement() {
        const int line = next().line;
        emit(line, "REPEAT");
        // la condition se lit apres le corps : un continue doit la calculer avant de sauter a UNTIL (Loop::step)
        const std::size_t loopIndex = loops_.size();
        loops_.push_back({});
        const std::size_t markFrag = frags_.size();
        (void)markFrag;
        // le corps d'abord ; la condition calculee (avec ses effets) se range ensuite dans le pas, qu'un continue ecrit
        const std::size_t bodyStart = i_;
        // pre-lecture de la condition pour savoir si elle a des effets : on traduit le corps, puis la condition
        body();
        (void)bodyStart;
        if (!word("while")) fail("while (...) ; attendu apr\xC3\xA8s le corps du do" + found());
        const int wl = next().line;
        expect("(", "apr\xC3\xA8s while");
        const Cond c = condition(wl);
        expect(")", "apr\xC3\xA8s la condition");
        expect(";", "apr\xC3\xA8s do ... while (...)");
        loops_.erase(loops_.begin() + static_cast<std::ptrdiff_t>(loopIndex), loops_.end());
        emit(wl, c.pre + "UNTIL NOT " + (c.effects ? c.text : "(" + c.text + ")") + " END_REPEAT;");
    }

    void forStatement() {
        const int line = next().line;
        expect("(", "apr\xC3\xA8s for");
        cur_->scopes.emplace_back();
        // C++ : for (T x : tableau) -> FOR EACH
        if (isCpp() && startsType()) {
            const std::size_t save = i_;
            const Spec ty = parseSpec();
            bool byRef = false;
            if (punct("&") || punct("*")) {
                byRef = punct("&");
                next();
            }
            if (peek().k == Tok::K::Name && punct(":", 1)) {
                const std::string nm = next().text;
                next();                                  // :
                const X coll = expression();
                expect(")", "apr\xC3\xA8s la collection du for");
                CTP et = ty.type;
                if (ty.isAuto) et = coll.ty && (coll.ty->k == CT::K::Array || coll.ty->k == CT::K::Vector) && coll.ty->elem ? coll.ty->elem : nullptr;
                // for (T& x : t) : x EST la case (une ecriture la change) - une boucle sur les indices
                if (byRef && !ty.isConst && coll.lvalue && coll.ty && (coll.ty->k == CT::K::Array || coll.ty->k == CT::K::Vector)) {
                    const std::string i = temp("DINT", line, "i");
                    const std::string last = coll.ty->k == CT::K::Array ? std::to_string(coll.ty->n - 1) : "VECTOR_SIZE(" + coll.st + ") - 1";
                    Var v;
                    v.st = coll.st + "[" + i + "]";
                    v.type = et ? et : coll.ty->elem;
                    v.c = nm;
                    cur_->scopes.back()[nm] = v;
                    emit(line, "FOR " + i + " := 0 TO " + last + " DO");
                    loops_.push_back({});
                    body();
                    loops_.pop_back();
                    emit(peekPrevLine(), "END_FOR;");
                    cur_->scopes.pop_back();
                    return;
                }
                const std::string st = declare(nm, et ? et : tUnknown(), line, true);
                noteVar(st, nm, et);
                (void)byRef;
                emit(line, "FOR EACH " + st + " IN " + coll.st + " DO");
                loops_.push_back({});
                body();
                loops_.pop_back();
                emit(peekPrevLine(), "END_FOR;");
                cur_->scopes.pop_back();
                return;
            }
            i_ = save;
        }
        // l'initialisation
        if (!accept(";")) {
            if (startsType()) declaration(false);
            else {
                FxScope fx(*this, true, {});
                expressionStatement(line, false);
                while (accept(",")) expressionStatement(line, false);
                expect(";", "apr\xC3\xA8s l'initialisation du for");
            }
        }
        // la condition
        Cond cond;
        cond.text = "TRUE";
        if (!punct(";")) cond = condition(line);
        expect(";", "apr\xC3\xA8s la condition du for");
        // le pas : traduit maintenant, ecrit a la fin du corps (et avant chaque continue)
        std::string step;
        if (!punct(")")) {
            const std::size_t mark = frags_.size();
            const int keepLast = last_;
            FxScope fx(*this, true, {});
            expressionStatement(line, false);
            while (accept(",")) expressionStatement(line, false);
            for (std::size_t k = mark; k < frags_.size(); ++k) step += (step.empty() ? "" : " ") + frags_[k].text;
            frags_.resize(mark);
            last_ = keepLast;
        }
        expect(")", "apr\xC3\xA8s le pas du for");
        if (cond.effects) emit(line, "WHILE TRUE DO " + cond.pre + "IF NOT (" + cond.text + ") THEN EXIT; END_IF;");
        else emit(line, "WHILE " + cond.text + " DO");
        loops_.push_back({Loop::K::Loop, step, line, {}, false});
        body();
        loops_.pop_back();
        const int end = peekPrevLine();
        probeHere(end, true);
        emit(end, step + (step.empty() ? "" : " ") + "END_WHILE;");
        cur_->scopes.pop_back();
    }

    void returnStatement() {
        const int line = next().line;
        if (accept(";")) {
            if (cur_->function && cur_->result && !cur_->lambda) fail("return sans valeur dans une fonction qui en rend une", line);
            emit(line, "RETURN;");
            return;
        }
        X v = assignment(false);
        expect(";", "apr\xC3\xA8s return");
        if (cur_->lambda && !cur_->result && !cur_->deduced) cur_->deduced = v.ty ? v.ty : v.lit == X::Lit::Real ? tDouble() : v.lit == X::Lit::Bool ? tBool() : v.lit == X::Lit::Text ? tText() : tInt();
        const CTP want = cur_->lambda ? (cur_->result ? cur_->result : cur_->deduced) : cur_->result;
        if (cur_->function && !want) fail("return avec une valeur dans une fonction void", line);
        if (!cur_->function) {                                   // le script : sa valeur ne va nulle part
            emitStatement(line, "RETURN;");
            return;
        }
        v = valueFor(want, v, line);
        auto& f = fx_.back();
        if (!f.post.empty()) {
            const std::string tmp = temp(want->st, line, "r");
            emitStatement(line, tmp + " := " + v.st + ";");
            emit(line, "RETURN " + tmp + ";");
            return;
        }
        emitStatement(line, "RETURN " + v.st + ";");
    }

    // delete p; delete[] p; (et free(p) en C : un appel)
    void deleteStatement() {
        const int line = next().line;
        if (!isCpp()) fail("delete est du C++ (en C : free(p))", line);
        bool array = false;
        if (punct("[") && punct("]", 1)) {
            next();
            next();
            array = true;
        }
        const X p = unary();
        expect(";", "apr\xC3\xA8s delete");
        if (!isPtr(p.ty) && p.ty && p.ty->k != CT::K::Unknown) fail("delete : un pointeur est attendu (" + p.st + ")", line);
        std::string text;
        if (isPtr(p.ty) && isRec(p.ty->elem) && !p.ty->elem->rec.empty()) {
            const Record* r = record(p.ty->elem->rec);
            if (r && r->hasDtor) {
                if (array) fail("delete[] d'objets qui ont un destructeur n'est pas pris en charge (un delete par objet)", line);
                text = "IF " + p.st + " <> NULL THEN " + destructorCall(*r, p.st + "^", line) + " END_IF; ";
            } else {
                // un destructeur dans une classe derivee : l'objet vise peut en etre une
                for (const auto& [n, rr] : records_)
                    if (rr.hasDtor && r && derivesFrom(n, r->c) && n != r->c)
                        fail("delete d'un " + r->c + "* : " + n + " (d\xC3\xA9riv\xC3\xA9" "e) a un destructeur, " + r->c + " n'en a pas de virtuel", line);
            }
        }
        emitStatement(line, text + "CDELETE(" + p.st + ", " + (array ? "TRUE" : "FALSE") + ");");
    }
    // L'appel du destructeur d'un objet (virtuel : celui de son type reel).
    std::string destructorCall(const Record& r, const std::string& obj, int line) {
        for (const auto& m : r.methods)
            if (m.dtor) {
                if (m.isVirtual) return dispatcherOf(r.c, m, line) + "(" + obj + ");";
                return m.st + "(" + obj + ");";
            }
        for (const Record* b = r.base.empty() ? nullptr : record(r.base); b; b = b->base.empty() ? nullptr : record(b->base))
            for (const auto& m : b->methods)
                if (m.dtor) return (m.isVirtual ? dispatcherOf(b->c, m, line) : m.st) + "(" + obj + ");";
        return {};
    }

    // std::cout << "x = " << x << std::endl; -> IHM_JOURNAL('x = ' + TO_STRING(x))
    void coutStatement() {
        const int line = next().line;
        std::vector<std::string> parts;
        while (accept("<<")) {
            if (word("std::endl") || word("endl") || word("std::flush")) {
                next();
                continue;
            }
            const X v = logical(8);
            if (v.lit == X::Lit::Text) {
                std::string s = v.st;
                if (s == "'$N'") continue;
                if (s.size() > 4 && s.compare(s.size() - 3, 3, "$N'") == 0) s = s.substr(0, s.size() - 3) + "'";
                parts.push_back(s);
            } else if (isText(v.ty)) {
                parts.push_back(wrap(v));
            } else if (v.ty && v.ty->isChar && v.lit == X::Lit::Int) {
                if (v.intValue == '\n') continue;
                parts.push_back(stString(std::string(1, static_cast<char>(v.intValue))));
            } else {
                parts.push_back("TO_STRING(" + v.st + ")");
            }
        }
        expect(";", "apr\xC3\xA8s std::cout << ...");
        std::string text;
        for (const auto& p : parts) text += (text.empty() ? "" : " + ") + p;
        if (text.empty()) text = "''";
        emitStatement(line, "IHM_JOURNAL(" + text + ");");
    }

    // switch (x) { case 1: case 2: ...; break; default: ...; } -> CASE x OF 1, 2: ... ELSE ... END_CASE; ; un case qui
    // continue dans le suivant, un break au milieu, default avant un case : la forme generale (switchGeneral).
    void switchStatement() {
        // l'etat d'avant : un essai rate (SwitchRetry) y revient
        const std::size_t saveI = i_, saveFrags = frags_.size(), saveGlobals = globals_.size();
        const int saveLast = last_;
        const Code saveCode = *cur_;
        const auto saveLoops = loops_;
        const auto saveFx = fx_;
        const auto saveConst = constInts_;
        try {
            switchSimple();
        } catch (const SwitchRetry&) {
            i_ = saveI;
            frags_.resize(saveFrags);
            globals_.resize(saveGlobals);
            last_ = saveLast;
            *cur_ = saveCode;
            loops_ = saveLoops;
            fx_ = saveFx;
            constInts_ = saveConst;
            switchGeneral();
        }
    }

    void switchSimple() {
        const int line = next().line;
        expect("(", "apr\xC3\xA8s switch");
        X v;
        std::string pre;
        {
            FxScope fx(*this, true, {});
            v = expression();
            for (const auto& p : fx.fx().pre) pre += p + " ";
            if (!fx.fx().post.empty()) throw SwitchRetry{};
        }
        expect(")", "apr\xC3\xA8s la valeur du switch");
        expect("{", "apr\xC3\xA8s switch (...)");
        emit(line, pre + "CASE " + v.st + " OF");
        cur_->scopes.emplace_back();
        loops_.push_back({Loop::K::Switch, {}, 0, {}, false});
        std::string labels;                  // les etiquettes en attente (1, 2)
        int labelLine = line;
        bool open = false, ended = false, elseSeen = false;
        while (!punct("}")) {
            if (at(Tok::K::End)) fail("'}' attendu : la fin du switch", line);
            if (word("case") || word("default")) {
                const bool dflt = word("default");
                const int ll = next().line;
                if (open && !ended) throw SwitchRetry{};            // le case continue dans le suivant
                if (elseSeen) throw SwitchRetry{};                  // un case apres default
                open = false;
                if (!dflt) {
                    const X lab = caseLabel(ll);
                    if (labels.empty()) labelLine = ll;
                    labels += (labels.empty() ? "" : ", ") + lab.st;
                }
                expect(":", "apr\xC3\xA8s l'\xC3\xA9tiquette du case");
                if (dflt) {                          // case 3: default: -> ELSE (le 3 y tombe aussi)
                    labels.clear();
                    emit(ll, "ELSE");
                    elseSeen = open = true;
                    ended = false;
                    labelLine = ll;
                }
                continue;
            }
            if (!open) {
                if (labels.empty()) fail("une instruction avant le premier case");
                emit(labelLine, labels + ":");
                labels.clear();
                open = true;
                ended = false;
            }
            if (ended) throw SwitchRetry{};                     // du code apres le break (jamais execute)
            if (word("break") && punct(";", 1)) {
                next();
                next();
                ended = true;
                continue;
            }
            const bool leaves = word("return") || word("continue");
            statement();
            if (leaves) ended = true;
        }
        if (!labels.empty()) emit(labelLine, labels + ":");      // des etiquettes sans instruction
        next();                                                    // }
        loops_.pop_back();
        cur_->scopes.pop_back();
        emit(peekPrevLine(), "END_CASE;");
    }

    X caseLabel(int line) {
        FxScope fx(*this, false, "un effet dans une \xC3\xA9tiquette de case n'est pas permis");
        X lab = ternary();
        if (accept("...")) fail("case a ... b (une plage) n'est pas pris en charge : un case par valeur", line);
        const bool name = !lab.st.empty() && (std::isalpha(static_cast<unsigned char>(lab.st[0])) || lab.st[0] == '_');
        if (lab.lit != X::Lit::Int && !name) fail("une \xC3\xA9tiquette de case est une constante (un nombre, 'a', une valeur d'\xC3\xA9num\xC3\xA9ration)", line);
        return lab;
    }

    // La forme generale : la valeur une fois, puis un IF par groupe d'etiquettes, que la suite traverse (le drapeau
    // "deja entre") ; break : EXIT de la boucle d'un tour qui les entoure.
    //   cswN_ := x; cftN_ := FALSE; REPEAT
    //   IF cftN_ OR cswN_ = 1 THEN cftN_ := TRUE; ... END_IF;
    //   IF cftN_ OR NOT (cswN_ = 1 OR ...) THEN cftN_ := TRUE; ... (default) END_IF;
    //   UNTIL TRUE END_REPEAT;
    void switchGeneral() {
        const int line = next().line;
        expect("(", "apr\xC3\xA8s switch");
        X v;
        std::string pre, post;
        {
            FxScope fx(*this, true, {});
            v = expression();
            for (const auto& p : fx.fx().pre) pre += p + " ";
            for (const auto& p : fx.fx().post) post += p + " ";
        }
        expect(")", "apr\xC3\xA8s la valeur du switch");
        expect("{", "apr\xC3\xA8s switch (...)");
        std::string value = v.st;
        const bool simple = v.lvalue && v.st.find('(') == std::string::npos && post.empty();
        if (!simple) {
            if (!v.ty || v.ty->k == CT::K::Unknown || v.ty->st.empty()) fail("switch (" + v.st + ") : \xC3\xA9" "cris la valeur dans une variable d'abord", line);
            value = temp(v.ty->st, line, "sw");
            pre += value + " := " + v.st + "; " + post;
        }
        const std::string through = temp("BOOL", line, "ft");
        const std::string cont = temp("BOOL", line, "ct");
        emit(line, pre + through + " := FALSE; " + cont + " := FALSE; REPEAT");
        cur_->scopes.emplace_back();
        loops_.push_back({Loop::K::SwitchGeneral, {}, 0, cont, false});
        std::vector<std::string> all;          // toutes les etiquettes (le default : aucune d'elles)
        std::vector<std::size_t> defaults;     // le morceau du default, complete a la fin
        bool open = false;
        while (!punct("}")) {
            if (at(Tok::K::End)) fail("'}' attendu : la fin du switch", line);
            if (word("case") || word("default")) {
                // les etiquettes qui se suivent : un seul IF
                std::vector<std::string> labels;
                bool dflt = false;
                const int ll = peek().line;
                while (word("case") || word("default")) {
                    if (word("default")) {
                        next();
                        dflt = true;
                    } else {
                        next();
                        const X lab = caseLabel(ll);
                        labels.push_back(lab.st);
                    }
                    expect(":", "apr\xC3\xA8s l'\xC3\xA9tiquette du case");
                }
                std::string test;
                for (const auto& l : labels) {
                    test += (test.empty() ? "" : " OR ") + value + " = " + l;
                    all.push_back(l);
                }
                const std::string close = open ? "END_IF; " : "";
                const std::size_t at = emit(ll, close + "IF " + through + " OR " + (dflt ? std::string("\x04") : "") + (test.empty() ? "" : (dflt ? " OR " : "") + test) + " THEN " + through + " := TRUE;");
                if (dflt) defaults.push_back(at);
                open = true;
                continue;
            }
            if (!open) fail("une instruction avant le premier case");
            statement();
        }
        const int close = next().line;                             // }
        // le default : aucune des etiquettes
        std::string none = "NOT (";
        for (std::size_t k = 0; k < all.size(); ++k) none += (k ? " OR " : "") + value + " = " + all[k];
        none += ")";
        if (all.empty()) none = "TRUE";
        for (const std::size_t at : defaults) {
            auto& t = frags_[at].text;
            if (const auto p = t.find('\x04'); p != std::string::npos) t.replace(p, 1, none);
        }
        const Loop me = loops_.back();
        loops_.pop_back();
        cur_->scopes.pop_back();
        std::string end = std::string(open ? "END_IF; " : "") + "UNTIL TRUE END_REPEAT;";
        if (me.contUsed) {
            const Loop* loop = nullptr;
            for (auto it = loops_.rbegin(); it != loops_.rend(); ++it)
                if (it->k == Loop::K::Loop) {
                    loop = &*it;
                    break;
                }
            Loop* outerSwitch = nullptr;
            for (auto it = loops_.rbegin(); it != loops_.rend() && it->k != Loop::K::Loop; ++it)
                if (it->k == Loop::K::SwitchGeneral) {
                    outerSwitch = &*it;
                    break;
                }
            if (outerSwitch) {
                outerSwitch->contUsed = true;
                end += " IF " + cont + " THEN " + outerSwitch->cont + " := TRUE; EXIT; END_IF;";
            } else if (loop) {
                end += " IF " + cont + " THEN " + loop->step + (loop->step.empty() ? "" : " ") + "CONTINUE; END_IF;";
            }
        }
        emit(close, end);
    }

    // x = e;  x += e;  x++;  f(a);  (`alone` : une instruction ; sinon un morceau d'un for)
    void expressionStatement(int line, bool alone) {
        X e = assignment(true);
        if (!e.statement) {
            if (!e.call) {
                // x++ seul, ++x seul : leur effet est l'instruction
                auto& f = fx_.back();
                if (f.pre.empty() && f.post.empty())
                    fail("une instruction sans effet : une affectation (x = ...) ou un appel f(...) est attendu" + found(), line);
                emitStatement(line, "");
            } else {
                emitStatement(line, e.st + ";");
            }
        } else {
            emitStatement(line, e.st);
        }
        if (alone) expect(";", "apr\xC3\xA8s l'instruction");
    }

    // Une affectation (= += -= ... ; a = b = 4) ; `top` : l'instruction elle-meme (sa valeur ne sert pas), sinon une
    // expression dont l'affectation part dans les effets d'avant (if ((x = f()) > 0)).
    X assignment(bool top) {
        static const std::map<std::string, std::string> compound = {
            {"+=", "+"}, {"-=", "-"}, {"*=", "*"}, {"/=", "/"}, {"%=", "MOD"}, {"&=", "AND"}, {"|=", "OR"}, {"^=", "XOR"}, {"<<=", "SHL"}, {">>=", "SHR"}};
        const int line = peek().line;
        X lhs = ternary();
        const bool plain = punct("=");
        const bool comp = peek().k == Tok::K::Punct && compound.count(peek().text);
        if (!plain && !comp) return lhs;
        const std::string op = next().text;
        if (!lhs.lvalue) fail(op + " : une affectation vise une variable" + (lhs.st.empty() ? std::string{} : " (" + lhs.st + " n'en est pas une)"), line);
        if (lhs.isConst) fail((lhs.varC.empty() ? lhs.st : lhs.varC) + " est const : elle ne s'\xC3\xA9" "crit pas", line);
        if (isPtr(lhs.ty)) expectPtr_.push_back(lhs.ty);
        X rhs = assignment(false);
        if (isPtr(lhs.ty)) expectPtr_.pop_back();
        std::string text;
        if (plain) {
            if (isArr(lhs.ty) && !lhs.ty->stdArray && !(isText(lhs.ty))) fail("un tableau ne s'affecte pas en entier en C (une boucle, ou memcpy)", line);
            rhs = valueFor(lhs.ty, rhs, line);
            if (isRec(lhs.ty) && isRec(rhs.ty) && lhs.ty->rec != rhs.ty->rec && !lhs.ty->rec.empty()) {
                // une Derivee copiee dans une Base : la copie est une Base (sa table virtuelle)
                const Record* r = record(lhs.ty->rec);
                text = lhs.st + " := " + rhs.st + ";" + (r && r->polymorphic ? " " + lhs.st + ".cvt_ := " + std::to_string(r->tag) + ";" : std::string{});
            } else {
                text = lhs.st + " := " + rhs.st + ";";
            }
        } else {
            const std::string& stOp = compound.at(op);
            if (isPtr(lhs.ty)) {
                if (op != "+=" && op != "-=") fail(op + " sur un pointeur n'a pas de sens", line);
                text = lhs.st + " := CPTR_ADD(" + lhs.st + ", " + (op == "-=" ? "-(" + rhs.st + ")" : rhs.st) + ");";
            } else if (isText(lhs.ty) && op == "+=") {
                text = lhs.st + " := " + lhs.st + " + " + textOf(rhs) + ";";
            } else if (op == "+=" || op == "-=" || op == "*=" || op == "/=") {
                const bool intDiv = op == "/=" && lhs.ty && lhs.ty->k == CT::K::Int && rhs.ty && rhs.ty->k == CT::K::Real;
                text = intDiv ? lhs.st + " := TRUNC(" + lhs.st + " / " + wrap(rhs) + ");" : lhs.st + " " + op + " " + rhs.st + ";";
            } else if (op == "<<=" || op == ">>=") {
                text = lhs.st + " := " + stOp + "(" + lhs.st + ", " + rhs.st + ");";
            } else {
                text = lhs.st + " := " + lhs.st + " " + stOp + " " + wrap(rhs) + ";";
            }
        }
        if (top) {
            X out;
            out.st = text;
            out.statement = true;
            return out;
        }
        addPre(text, line);
        X out = lhs;
        return out;
    }

    // ---- les expressions (du plus faible au plus fort)
    X expression() {
        X e = assignment(false);
        if (punct(",")) fail("l'op\xC3\xA9rateur virgule n'est pas pris en charge");
        return e;
    }

    struct Depth {
        int& d;
        explicit Depth(int& x, int line) : d(x) {
            if (++d > 200) throw Fault{line, "expression trop imbriqu\xC3\xA9" "e"};
        }
        ~Depth() { --d; }
    };

    X ternary() {
        Depth guard(exprDepth_, peek().line);
        X c = logical(0);
        if (!punct("?")) return c;
        next();
        X a = conditional([&] { return assignment(false); }, "dans c ? a : b");
        expect(":", "dans c ? a : b");
        X b = conditional([&] { return ternary(); }, "dans c ? a : b");
        X out;
        if (isPtr(a.ty) || isPtr(b.ty)) {
            if (a.isNull) a.st = "NULL";
            if (b.isNull) b.st = "NULL";
        }
        out.st = boolWrap(c) + " ? " + wrap(a) + " : " + wrap(b);
        out.compound = true;
        out.boolish = a.boolish && b.boolish;
        out.ty = isNum(a.ty) && isNum(b.ty) ? arith(a.ty, b.ty) : a.ty ? a.ty : b.ty;
        if (a.lit == X::Lit::Text && b.lit == X::Lit::Text) out.ty = tText();
        return out;
    }

    // Le texte d'une valeur pour + sur un texte (un caractere 'a' : son texte).
    std::string textOf(const X& v) const {
        if (isText(v.ty) || v.lit == X::Lit::Text) return wrap(v);
        if (v.ty && v.ty->isChar && v.lit == X::Lit::Int) return stString(std::string(1, static_cast<char>(v.intValue)));
        return "TO_STRING(" + v.st + ")";
    }

    // Les operateurs binaires du C, par niveau (le plus faible d'abord).
    X logical(int level) {
        static const std::vector<std::vector<std::string>> levels = {
            {"||"}, {"&&"}, {"|"}, {"^"}, {"&"}, {"==", "!="}, {"<", ">", "<=", ">="}, {"<<", ">>"}, {"+", "-"}, {"*", "/", "%"}};
        if (level >= static_cast<int>(levels.size())) return unary();
        X lhs = logical(level + 1);
        while (peek().k == Tok::K::Punct) {
            const auto& ops = levels[static_cast<std::size_t>(level)];
            const std::string op = peek().text;
            if (std::find(ops.begin(), ops.end(), op) == ops.end()) break;
            if (templateArg_ > 0 && (op == ">" || op == ">>")) break;          // std::array<int, N> : le > ferme le modele
            const int line = next().line;
            X rhs = op == "||" || op == "&&" ? conditional([&] { return logical(level + 1); }, ("dans la partie droite de " + op).c_str())
                                             : logical(level + 1);
            lhs = binary(op, lhs, rhs, line);
        }
        return lhs;
    }

    static std::int64_t foldInt(const std::string& op, std::int64_t a, std::int64_t b, bool& ok) {
        ok = true;
        if (op == "+") return a + b;
        if (op == "-") return a - b;
        if (op == "*") return a * b;
        if (op == "/" && b != 0) return a / b;
        if (op == "%" && b != 0) return a % b;
        if (op == "<<" && b >= 0 && b < 63) return a << b;
        if (op == ">>" && b >= 0 && b < 63) return a >> b;
        if (op == "&") return a & b;
        if (op == "|") return a | b;
        if (op == "^") return a ^ b;
        ok = false;
        return 0;
    }

    X binary(const std::string& op, X lhs, X rhs, int line) {
        X out;
        out.compound = true;
        // un tableau dans un calcul : un pointeur sur sa premiere case (t + 1)
        const auto decay = [&](X& x) {
            if (isArr(x.ty) && x.lvalue && !x.ty->stdArray && !isText(x.ty)) {
                x.st = "CPTR_AT(" + x.st + ", 0)";
                x.ty = pointerCT(x.ty->elem);
                x.lvalue = false;
                x.compound = false;
            }
        };
        if (op == "||" || op == "&&") {
            out.st = boolWrap(lhs) + (op == "||" ? " OR " : " AND ") + boolWrap(rhs);
            out.boolish = true;
            out.ty = tBool();
            return out;
        }
        if (op == "+" || op == "-" || op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
            decay(lhs);
            decay(rhs);
        }
        // les pointeurs : l'arithmetique dans un tableau, les comparaisons
        if (isPtr(lhs.ty) || isPtr(rhs.ty)) {
            const bool lp = isPtr(lhs.ty), rp = isPtr(rhs.ty);
            if ((op == "+" || op == "-") && lp && !rp && !rhs.isNull) {
                out.st = "CPTR_ADD(" + lhs.st + ", " + (op == "-" ? "-" + wrap(rhs) : rhs.st) + ")";
                out.compound = false;
                out.ty = lhs.ty;
                return out;
            }
            if (op == "+" && rp && !lp) {
                out.st = "CPTR_ADD(" + rhs.st + ", " + lhs.st + ")";
                out.compound = false;
                out.ty = rhs.ty;
                return out;
            }
            if (op == "-" && lp && rp) {
                out.st = "CPTR_DIFF(" + lhs.st + ", " + rhs.st + ")";
                out.compound = false;
                out.ty = tLong();
                return out;
            }
            if (op == "==" || op == "!=") {
                out.boolish = true;
                out.ty = tBool();
                if (lhs.isNull || rhs.isNull || (lhs.lit == X::Lit::Int && lhs.intValue == 0) || (rhs.lit == X::Lit::Int && rhs.intValue == 0)) {
                    const X& p = lhs.isNull || lhs.lit == X::Lit::Int ? rhs : lhs;
                    out.st = wrap(p) + (op == "==" ? " = NULL" : " <> NULL");
                    return out;
                }
                out.st = std::string(op == "!=" ? "NOT " : "") + "CPTR_EQ(" + lhs.st + ", " + rhs.st + ")";
                out.compound = op == "!=";
                return out;
            }
            if ((op == "<" || op == ">" || op == "<=" || op == ">=") && lp && rp) {
                out.st = "CPTR_DIFF(" + lhs.st + ", " + rhs.st + ") " + op + " 0";
                out.boolish = true;
                out.ty = tBool();
                return out;
            }
            fail("'" + op + "' sur un pointeur n'est pas pris en charge (p + n, p - n, p - q, ==, <...)", line);
        }
        if (op == "<<" || op == ">>") {
            out.st = std::string(op == "<<" ? "SHL(" : "SHR(") + lhs.st + ", " + rhs.st + ")";
            out.compound = false;
            out.ty = lhs.ty;
            if (lhs.lit == X::Lit::Int && rhs.lit == X::Lit::Int) {
                bool ok = false;
                const std::int64_t v = foldInt(op, lhs.intValue, rhs.intValue, ok);
                if (ok) {
                    out.lit = X::Lit::Int;
                    out.intValue = v;
                    out.st = v < 0 ? "(" + std::to_string(v) + ")" : std::to_string(v);
                }
            }
            return out;
        }
        // un texte : + concatene, == compare
        if (op == "+" && (isText(lhs.ty) || isText(rhs.ty) || lhs.lit == X::Lit::Text || rhs.lit == X::Lit::Text)) {
            if (!isCpp() || (lhs.lit == X::Lit::Text && rhs.lit == X::Lit::Text))
                if (!(isText(lhs.ty) && lhs.ty->shown == "std::string") && !(isText(rhs.ty) && rhs.ty->shown == "std::string"))
                    fail("+ sur deux textes du C n'est pas pris en charge (strcat, ou std::string en C++)", line);
            out.st = textOf(lhs) + " + " + textOf(rhs);
            out.ty = tString();
            return out;
        }
        static const std::map<std::string, std::string> st = {{"|", "OR"}, {"^", "XOR"}, {"&", "AND"}, {"==", "="}, {"!=", "<>"},
                                                              {"<", "<"}, {">", ">"}, {"<=", "<="}, {">=", ">="}, {"+", "+"},
                                                              {"-", "-"}, {"*", "*"}, {"/", "/"}, {"%", "MOD"}};
        out.st = wrap(lhs) + " " + st.at(op) + " " + wrap(rhs);
        out.boolish = op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">="
                      || ((op == "|" || op == "&" || op == "^") && lhs.boolish && rhs.boolish);
        out.ty = out.boolish ? tBool() : arith(lhs.ty ? lhs.ty : tUnknown(), rhs.ty ? rhs.ty : tUnknown());
        if (!lhs.ty && !rhs.ty && !out.boolish) out.ty = nullptr;
        if (lhs.lit == X::Lit::Real || rhs.lit == X::Lit::Real) {
            if (!out.boolish && (!out.ty || out.ty->k != CT::K::Real)) out.ty = tDouble();
        }
        // un calcul de constantes entieres (t[N - 1], case 2 + 1, sizeof(t) / sizeof(t[0])) garde sa valeur
        if (lhs.lit == X::Lit::Int && rhs.lit == X::Lit::Int && !out.boolish) {
            bool ok = false;
            const std::int64_t v = foldInt(op, lhs.intValue, rhs.intValue, ok);
            if (ok) {
                out.lit = X::Lit::Int;
                out.intValue = v;
                if (op == "/" || op == "%") {                   // la division du C : le resultat ecrit tel quel
                    out.st = v < 0 ? "(" + std::to_string(v) + ")" : std::to_string(v);
                    out.compound = false;
                }
            }
        }
        // == et != entre deux BOOL, deux textes, deux structures : tels quels ; un BOOL et un entier : en entier
        if ((op == "==" || op == "!=") && lhs.boolish != rhs.boolish && (lhs.lit == X::Lit::Int || rhs.lit == X::Lit::Int)) {
            const X& b = lhs.boolish ? lhs : rhs;
            const X& n = lhs.boolish ? rhs : lhs;
            out.st = "TO_DINT(" + b.st + ") " + st.at(op) + " " + wrap(n);
        }
        return out;
    }

    // ---- les operateurs d'un seul operande
    std::string stepText(const X& v, bool inc) const {
        if (isPtr(v.ty)) return v.st + " := CPTR_ADD(" + v.st + ", " + (inc ? "1" : "-1") + ");";
        return v.st + (inc ? " += 1;" : " -= 1;");
    }

    X unary() {
        Depth guard(exprDepth_, peek().line);
        const int line = peek().line;
        if (punct("-") || punct("+")) {
            const bool minus = next().text == "-";
            X v = unary();
            if (!minus) return v;
            X out;
            out.ty = v.ty;
            if (v.lit == X::Lit::Int || v.lit == X::Lit::Real) {
                out = v;
                out.st = v.st.rfind('-', 0) == 0 ? v.st.substr(1) : "-" + v.st;
                out.intValue = -v.intValue;
                out.compound = false;
                out.lvalue = false;
                return out;
            }
            out.st = "-" + wrap(v);
            out.compound = true;
            return out;
        }
        if (accept("!")) {
            X v = unary();
            X out;
            out.st = "NOT " + boolWrap(v);
            out.compound = true;
            out.boolish = true;
            out.ty = tBool();
            return out;
        }
        if (accept("~")) {
            X v = unary();
            X out;
            out.st = "NOT " + wrap(v);
            out.compound = true;
            out.ty = v.ty;
            if (v.lit == X::Lit::Int) {
                out.lit = X::Lit::Int;
                out.intValue = ~v.intValue;
            }
            return out;
        }
        if (punct("*")) {
            next();
            return deref(unary(), line);
        }
        if (punct("&")) {
            next();
            return addressOf(unary(), line);
        }
        if (punct("++") || punct("--")) {                // ++x : avant
            const bool inc = next().text == "++";
            X v = unary();
            if (!v.lvalue) fail(std::string(inc ? "++" : "--") + " : une variable est attendue", line);
            if (v.isConst) fail((v.varC.empty() ? v.st : v.varC) + " est const : elle ne s'\xC3\xA9" "crit pas", line);
            addPre(stepText(v, inc), line);
            return v;
        }
        if (word("sizeof")) return sizeofExpr();
        if (word("new")) return newExpr();
        if (word("delete")) fail("delete s'\xC3\xA9" "crit seul, comme une instruction : delete p;", line);
        // (type) x : une conversion
        if (punct("(") && startsCast()) {
            next();
            const CTP ty = typeName();
            expect(")", "apr\xC3\xA8s le type de la conversion");
            if (isPtr(ty)) expectPtr_.push_back(ty);
            X v = unary();
            if (isPtr(ty)) expectPtr_.pop_back();
            return cast(ty, v, line);
        }
        return postfix();
    }

    X deref(const X& v, int line) {
        X out;
        out.lvalue = true;
        if (v.ty && v.ty->k == CT::K::Func) return v;            // (*f)(x) : la fonction elle-meme
        if (isPtr(v.ty)) {
            out.st = !v.derefSt.empty() ? v.derefSt : v.compound ? "(" + v.st + ")^" : v.st + "^";
            out.ty = v.ty->elem;
            out.boolish = out.ty && out.ty->k == CT::K::Bool;
            return out;
        }
        if (isArr(v.ty)) {                                       // *t : t[0]
            out.st = wrap(v) + "[0]";
            out.ty = v.ty->elem;
            out.arrBase = v.st;
            out.arrIndex = "0";
            return out;
        }
        if (isText(v.ty)) fail("*" + v.st + " : un caract\xC3\xA8re d'un texte n'est pas pris en charge (le texte entier l'est)", line);
        if (v.ty && v.ty->k != CT::K::Unknown && !(isRec(v.ty) && v.ty->rec.empty()))
            fail("*" + (v.varC.empty() ? v.st : v.varC) + " : seul un pointeur se d\xC3\xA9r\xC3\xA9" "f\xC3\xA9rence", line);
        out.st = v.st + "^";                                     // un pointeur IHM (POINTER TO d'une variable IHM)
        return out;
    }

    X addressOf(const X& v, int line) {
        X out;
        if (!v.fnName.empty()) return v;                         // &f : la fonction (un rappel)
        if (!v.lvalue) fail("&" + v.st + " : une variable est attendue", line);
        if (!v.arrBase.empty()) {
            out.st = "CPTR_AT(" + v.arrBase + ", " + v.arrIndex + ")";
            out.ty = pointerCT(v.ty ? v.ty : tUnknown());
            return out;
        }
        if (v.st.size() > 1 && v.st.back() == '^' && v.st.find('.') == std::string::npos && v.st.find('[') == std::string::npos) {
            out.st = v.st.substr(0, v.st.size() - 1);           // &*p : p
            if (out.st.front() == '(' && out.st.back() == ')') out.st = out.st.substr(1, out.st.size() - 2);
            out.ty = pointerCT(v.ty ? v.ty : tUnknown());
            return out;
        }
        if (v.st.size() > 1 && v.st.back() == '^' && v.st.rfind("CPTR_ADD(", 0) == 0) {
            out.st = v.st.substr(0, v.st.size() - 1);           // &p[i] : p + i
            out.ty = pointerCT(v.ty ? v.ty : tUnknown());
            return out;
        }
        if (isText(v.ty) && v.ty->size != 8 && v.ty->shown != "std::string" && v.ty->shown != "const char*") {
            out = v;                                             // &s d'un char s[N] : le texte
            return out;
        }
        out.st = "ADR(" + v.st + ")";
        out.ty = pointerCT(v.ty ? v.ty : tUnknown());
        out.varC = v.st;                                         // passe a une fonction IHM (VAR_IN_OUT) : la variable
        return out;
    }

    // sizeof(type), sizeof x, sizeof(x) : la taille du C (une constante).
    X sizeofExpr() {
        const int line = next().line;
        int size = 0;
        if (punct("(") && (startsTypeAfterParen())) {
            next();
            CTP t = typeName();
            while (accept("[")) {
                const std::int64_t n = constantSize(line);
                expect("]", "apr\xC3\xA8s la taille");
                t = arrayCT(t, n);
            }
            expect(")", "apr\xC3\xA8s le type de sizeof");
            size = t->size;
            if (t->k == CT::K::Void) fail("sizeof(void) n'a pas de sens", line);
        } else {
            X v;
            {
                FxScope fx(*this, false, "sizeof ne calcule pas son expression : pas d'effet dedans");
                const std::size_t mark = frags_.size();
                v = unary();
                frags_.resize(mark);
            }
            if (!v.ty || v.ty->k == CT::K::Unknown || (isRec(v.ty) && v.ty->rec.empty()))
                fail("sizeof " + v.st + " : son type n'est pas connu ici (une variable IHM ? \xC3\xA9" "cris sizeof(type))", line);
            size = v.ty->size;
        }
        X out;
        out.lit = X::Lit::Int;
        out.intValue = size;
        out.st = std::to_string(size);
        out.ty = fromSt("UDINT", "size_t");
        return out;
    }
    bool startsTypeAfterParen() const {
        const Tok& t = peek(1);
        if (t.k != Tok::K::Name) return false;
        if (typeKeyword(t.text) || cNamedTypes().count(t.text) || stElementary().count(t.text) || typedefs_.count(t.text)) return !findVar(t.text);
        if (records_.count(t.text) || enums_.count(t.text)) return !findVar(t.text) && isCpp();
        if (t.text.rfind("std::", 0) == 0 && punct("<", 2)) return true;
        return false;
    }
    bool startsCast() const {
        const Tok& t = peek(1);
        if (t.k != Tok::K::Name) return false;
        if (findVar(t.text)) return false;
        if (typeKeyword(t.text) && t.text != "auto" && t.text != "static" && t.text != "virtual" && t.text != "explicit") return true;
        if (cNamedTypes().count(t.text) || stElementary().count(t.text) || typedefs_.count(t.text) || (isCpp() && (records_.count(t.text) || enums_.count(t.text))))
            return punct(")", 2) || punct("*", 2) || punct("&", 2);
        return false;
    }

    X cast(const CTP& ty, const X& v, int line) {
        X out;
        out.ty = ty;
        if (ty->k == CT::K::Void) {                               // (void)f(); : la valeur jetee
            out = v;
            out.ty = tVoid();
            return out;
        }
        if (isPtr(ty)) {
            if (v.isNull || (v.lit == X::Lit::Int && v.intValue == 0)) {
                out.st = "NULL";
                out.isNull = true;
                return out;
            }
            if (isPtr(v.ty) && (v.ty->elem->st == ty->elem->st || v.ty->elem->k == CT::K::Void)) {
                out = v;
                out.ty = ty;
                return out;
            }
            if (isPtr(v.ty) && isRec(v.ty->elem) && isRec(ty->elem) && !ty->elem->rec.empty() && !v.ty->elem->rec.empty()
                && (derivesFrom(ty->elem->rec, v.ty->elem->rec) || derivesFrom(v.ty->elem->rec, ty->elem->rec))) {
                out = v;                                         // (Derivee*)base : la meme structure ST
                out.ty = ty;
                return out;
            }
            fail("une conversion de pointeur vers " + ty->shown + " n'est pas prise en charge", line);
        }
        if (isRec(ty) && !ty->rec.empty()) fail("une conversion vers " + ty->shown + " n'est pas prise en charge", line);
        if (isPtr(v.ty)) fail("un pointeur ne se convertit pas en nombre", line);
        const std::string& t = ty->st;
        if (!ty->enumName.empty() || t.empty()) {               // vers un enum : l'entier
            out.st = v.st;
            out.compound = v.compound;
            out.lit = v.lit;
            out.intValue = v.intValue;
            return out;
        }
        if (t == "BOOL") {
            out.st = boolOf(v);
            out.boolish = true;
            out.compound = v.boolish && v.compound;
        } else if (t == "REAL" || t == "LREAL" || t == "STRING") {
            out.st = "TO_" + t + "(" + v.st + ")";
        } else if (t == "LINT" || t == "ULINT") {
            out.st = v.ty && v.ty->k == CT::K::Real ? "TO_" + t + "(TRUNC(" + v.st + "))" : "TO_" + t + "(" + v.st + ")";
        } else if (stElementary().count(t) || cNamedTypes().count(t)) {
            // vers un entier : tronque vers zero, comme le C ((int)2.7 = 2)
            out.st = v.ty && v.ty->k != CT::K::Real && v.lit != X::Lit::Real ? "TO_" + t + "(" + v.st + ")" : "TO_" + t + "(TRUNC(" + v.st + "))";
            if (v.lit == X::Lit::Int) {
                out.lit = X::Lit::Int;
                out.intValue = v.intValue;
            }
        } else {
            out.st = "TO_" + t + "(" + v.st + ")";
        }
        return out;
    }

    // new T, new T(args), new T{...}, new T[n] : un objet (un tableau) du tas.
    X newExpr() {
        const int line = next().line;
        if (!isCpp()) fail("new est du C++ (en C : malloc)", line);
        Spec sp = parseSpec();
        CTP t = sp.type;
        while (accept("*")) t = t->isChar ? tText() : pointerCT(t);
        if (t->k == CT::K::Void || t->k == CT::K::Unknown) fail("new : un type est attendu", line);
        if (isRec(t) && !t->rec.empty() && record(t->rec)->abstract) fail(t->rec + " est abstraite : elle ne se cr\xC3\xA9" "e pas (une classe d\xC3\xA9riv\xC3\xA9" "e oui)", line);
        X out;
        out.ty = pointerCT(t);
        if (accept("[")) {
            const X n = expression();
            expect("]", "apr\xC3\xA8s new T[n");
            const std::string call = "CNEW('" + t->st + "', " + n.st + ")";
            if (needsConstruction(t)) {
                const std::string p = temp(out.ty->st, line, "n");
                const std::string i = temp("DINT", line, "i");
                addPre(p + " := " + call + "; FOR " + i + " := 0 TO " + wrap(n) + " - 1 DO " + construct(t, "CPTR_ADD(" + p + ", " + i + ")^", {}, line) + " END_FOR;", line);
                out.st = p;
                return out;
            }
            out.st = call;
            out.call = true;
            return out;
        }
        const std::string call = "CNEW('" + t->st + "')";
        const bool hasArgs = punct("(") || punct("{");
        if (!hasArgs && !needsConstruction(t)) {
            out.st = call;
            out.call = true;
            return out;
        }
        const std::string p = temp(out.ty->st, line, "n");
        std::string text = p + " := " + call + "; ";
        if (isRec(t) && !t->rec.empty() && (hasConstructors(t->rec) || !punct("{"))) {
            const std::vector<X> args = hasArgs ? constructorArgs() : std::vector<X>{};
            text += construct(t, p + "^", args, line);
        } else if (punct("{")) {
            std::vector<Cell> cells;
            bool constant = true;
            (void)initBraces(t, p + "^", cells, constant, line);
            for (const auto& c : cells) text += c.path + " := " + c.st + "; ";
        } else {
            next();
            if (!punct(")")) text += p + "^ := " + valueFor(t, ternary(), line).st + ";";
            expect(")", "apr\xC3\xA8s new T(valeur");
        }
        addPre(text, line);
        out.st = p;
        return out;
    }

    // ---- les suffixes : t[i], s.m, p->m, f(x), x++
    void checkAccess(const std::string& access, const std::string& owner, const std::string& name, int line) const {
        if (access == "public" || owner.empty()) return;
        const std::string& here = cur_->recordC;
        if (access == "private" && here == owner) return;
        if (access == "protected" && !here.empty() && derivesFrom(here, owner)) return;
        fail(name + " est " + (access == "private" ? std::string("priv\xC3\xA9") : std::string("prot\xC3\xA9g\xC3\xA9")) + " dans " + owner + " : seules ses m\xC3\xA9thodes"
                 + (access == "protected" ? std::string(" (et celles de ses classes d\xC3\xA9riv\xC3\xA9" "es)") : std::string{}) + " y acc\xC3\xA8" "dent",
             line);
    }

    X postfix() {
        X e = primary();
        while (true) {
            const int line = peek().line;
            if (accept("[")) {
                X i = expression();
                expect("]", "apr\xC3\xA8s l'indice");
                X out;
                out.lvalue = true;
                if (isPtr(e.ty)) {
                    out.st = i.lit == X::Lit::Int && i.intValue == 0 ? (e.derefSt.empty() ? e.st + "^" : e.derefSt) : "CPTR_ADD(" + e.st + ", " + i.st + ")^";
                    out.ty = e.ty->elem;
                } else if (isText(e.ty)) {
                    fail(e.st + "[...] : un caract\xC3\xA8re d'un texte n'est pas pris en charge (le texte entier l'est ; std::string : s.substr(i, 1))", line);
                } else {
                    if (e.ty && e.ty->k != CT::K::Unknown && !isArr(e.ty) && e.ty->k != CT::K::Vector && !(isRec(e.ty) && e.ty->rec.empty()))
                        fail((e.varC.empty() ? e.st : e.varC) + "[...] : ni un tableau ni un pointeur", line);
                    out.st = wrap(e) + "[" + i.st + "]";
                    out.ty = e.ty && (isArr(e.ty) || e.ty->k == CT::K::Vector) ? e.ty->elem : nullptr;
                    if (!e.ty || isArr(e.ty) || (isRec(e.ty) && e.ty->rec.empty())) {
                        out.arrBase = e.st;
                        out.arrIndex = i.st;
                    }
                    out.isConst = e.isConst;
                }
                out.boolish = out.ty && out.ty->k == CT::K::Bool;
                e = out;
                continue;
            }
            if (punct(".") || punct("->")) {
                const bool arrow = next().text == "->";
                if (peek().k != Tok::K::Name) fail("un membre est attendu apr\xC3\xA8s " + std::string(arrow ? "->" : "."), line);
                const std::string m = next().text;
                X obj = e;
                if (arrow) {
                    if (!isPtr(e.ty) && e.ty && e.ty->k != CT::K::Unknown && !(isRec(e.ty) && e.ty->rec.empty()))
                        fail((e.varC.empty() ? e.st : e.varC) + "->" + m + " : seul un pointeur s'emploie avec ->", line);
                    obj = deref(e, line);
                }
                e = member(obj, m, line);
                continue;
            }
            if (punct("(") && (e.ty && e.ty->k == CT::K::Func)) {   // (*f)(x), t[i](x) : un rappel appele
                const std::vector<X> args = callArgs();
                X out;
                out.st = wrap(e) + "(" + argumentText(e.ty->params, {}, args, line, false) + ")";
                out.call = true;
                out.ty = e.ty->ret;
                e = out;
                continue;
            }
            if (punct("++") || punct("--")) {                    // x++ : apres
                const bool inc = next().text == "++";
                if (!e.lvalue) fail(std::string(inc ? "++" : "--") + " : une variable est attendue", line);
                if (e.isConst) fail((e.varC.empty() ? e.st : e.varC) + " est const : elle ne s'\xC3\xA9" "crit pas", line);
                addPost(stepText(e, inc), line);
                continue;
            }
            break;
        }
        return e;
    }

    // obj.m : un membre (une structure du script : verifie), une methode, une methode d'un conteneur du C++.
    X member(const X& obj, const std::string& m, int line) {
        X out;
        out.lvalue = true;
        if (punct("(") && obj.ty && (obj.ty->k == CT::K::Vector || isText(obj.ty) || (isArr(obj.ty) && obj.ty->stdArray)))
            return stdMethod(obj, m, line);
        if (isRec(obj.ty) && !obj.ty->rec.empty()) {
            const std::string& rec = obj.ty->rec;
            if (punct("(")) return methodCall(obj, rec, m, line, false);
            const Member* mem = findMember(rec, m);
            if (!mem) {
                if (!findMethods(rec, m).empty()) fail(m + " est une m\xC3\xA9thode de " + rec + " : appelle-la, " + m + "(...)", line);
                fail(m + " n'est pas un membre de " + obj.ty->shown, line);
            }
            checkAccess(mem->access, mem->owner, m, line);
            if (mem->isStatic) {
                out.st = mem->staticSt;
            } else {
                if (!obj.lvalue) fail("." + m + " : sur une valeur calcul\xC3\xA9" "e, mets-la d'abord dans une variable", line);
                out.st = wrap(obj) + "." + mem->st;
            }
            out.ty = mem->type;
            out.boolish = mem->type->k == CT::K::Bool;
            out.isConst = obj.isConst;
            return out;
        }
        if (obj.ty && obj.ty->k != CT::K::Unknown && !(isRec(obj.ty)))
            fail("." + m + " : " + (obj.varC.empty() ? obj.st : obj.varC) + " n'est pas une structure (" + obj.ty->shown + ")", line);
        // une structure IHM (T_Four), une variable IHM : tels quels
        out.st = wrap(obj) + "." + m;
        if (punct("(")) {
            const std::vector<X> args = callArgs();
            out.st += "(" + plainArgs(args) + ")";
            out.call = true;
            out.lvalue = false;
        }
        return out;
    }

    // Les arguments d'un appel, lus.
    std::vector<X> callArgs() {
        expect("(", "");
        std::vector<X> args;
        while (!punct(")")) {
            args.push_back(assignment(false));
            if (!accept(",")) break;
        }
        expect(")", "apr\xC3\xA8s les arguments");
        return args;
    }
    // Les arguments d'une fonction inconnue ici (IHM) : &x passe la variable (un parametre VAR_IN_OUT).
    std::string plainArgs(const std::vector<X>& args) const {
        std::string out;
        for (std::size_t k = 0; k < args.size(); ++k) {
            const X& a = args[k];
            std::string s = a.st;
            if (s.rfind("ADR(", 0) == 0 && !a.varC.empty()) s = a.varC;
            else if (isArr(a.ty) && false) s = a.st;
            out += (k ? ", " : "") + s;
        }
        return out;
    }

    // obj.methode(args) : la methode (virtuelle : par son aiguillage, selon le type reel de l'objet).
    X methodCall(const X& obj, const std::string& rec, const std::string& name, int line, bool qualified) {
        const std::vector<X> args = callArgs();
        const auto list = findMethods(rec, name);
        if (list.empty()) {
            if (findMember(rec, name)) {
                const Member* mem = findMember(rec, name);
                if (mem->type->k == CT::K::Func) {             // un membre rappel appele
                    X out;
                    out.st = wrap(obj) + "." + mem->st + "(" + argumentText(mem->type->params, {}, args, line, false) + ")";
                    out.call = true;
                    out.ty = mem->type->ret;
                    return out;
                }
                fail(name + " est un membre de " + rec + ", pas une m\xC3\xA9thode", line);
            }
            fail(name + " n'est pas une m\xC3\xA9thode de " + rec, line);
        }
        const Method* m = nullptr;
        for (const auto* x : list)
            if (x->params.size() == args.size()) m = x;
        if (!m) fail(rec + "::" + name + " : aucune version ne prend " + std::to_string(args.size()) + " argument(s)", line);
        checkAccess(m->access, m->owner, name, line);
        X out;
        out.call = true;
        out.ty = m->ret;
        out.boolish = m->ret && m->ret->k == CT::K::Bool;
        if (m->isStatic) {
            out.st = m->st + "(" + argumentText(m->params, m->modes, args, line, false) + ")";
            return out;
        }
        if (!obj.lvalue) fail(name + "() : sur une valeur calcul\xC3\xA9" "e, mets-la d'abord dans une variable", line);
        const std::string fn = m->isVirtual && !qualified ? dispatcherOf(m->owner, *m, line) : m->st;
        out.st = fn + "(" + obj.st + argumentText(m->params, m->modes, args, line, true) + ")";
        return out;
    }

    // L'aiguillage d'une methode virtuelle (cree a la fin : il connait toutes les classes derivees).
    std::map<std::string, std::string> dispatchers_;       // "Racine::m/N" -> nom ST
    std::string dispatcherOf(const std::string& owner, const Method& m, int line) {
        (void)line;
        (void)owner;
        const std::string key = m.virtualRoot + "::" + m.c + "/" + std::to_string(m.params.size());
        auto it = dispatchers_.find(key);
        if (it == dispatchers_.end()) {
            const std::string st = fresh(m.virtualRoot + "__" + (m.dtor ? std::string("detruire") : m.c) + "_v", {});
            allNames_.insert(upperOf(st));
            it = dispatchers_.emplace(key, st).first;
        }
        return it->second;
    }

    // std::vector, std::string, std::array : leurs methodes.
    X stdMethod(const X& obj, const std::string& m, int line) {
        const std::vector<X> args = callArgs();
        const auto argc = [&](std::size_t n) {
            if (args.size() != n) fail(m + "() : " + std::to_string(n) + " argument(s) attendu(s)", line);
        };
        X out;
        out.call = true;
        const std::string& v = obj.st;
        if (obj.ty->k == CT::K::Vector) {
            if (m == "push_back" || m == "emplace_back") { argc(1); out.st = "VECTOR_PUSH(" + v + ", " + valueFor(obj.ty->elem, args[0], line).st + ")"; return out; }
            if (m == "pop_back") { argc(0); out.st = "VECTOR_POP(" + v + ")"; return out; }
            if (m == "size") { argc(0); out.st = "VECTOR_SIZE(" + v + ")"; out.ty = fromSt("UDINT", "size_t"); out.call = false; return out; }
            if (m == "empty") { argc(0); out.st = "(VECTOR_SIZE(" + v + ") = 0)"; out.boolish = true; out.ty = tBool(); out.call = false; return out; }
            if (m == "clear") { argc(0); out.st = "VECTOR_CLEAR(" + v + ")"; return out; }
            if (m == "resize") {
                if (args.empty() || args.size() > 2) argc(1);
                out.st = "VECTOR_RESIZE(" + v + ", " + args[0].st + (args.size() == 2 ? ", " + args[1].st : std::string{}) + ")";
                return out;
            }
            out.call = false;
            out.lvalue = true;
            out.ty = obj.ty->elem;
            if (m == "front") { argc(0); out.st = v + "[0]"; return out; }
            if (m == "back") { argc(0); out.st = v + "[VECTOR_SIZE(" + v + ") - 1]"; return out; }
            if (m == "at") { argc(1); out.st = v + "[" + args[0].st + "]"; return out; }
            fail("std::vector::" + m + " n'est pas pris en charge (push_back, pop_back, size, empty, clear, resize, front, back, at, [i] le sont)", line);
        }
        if (isArr(obj.ty)) {
            out.call = false;
            if (m == "size") { argc(0); out.st = std::to_string(obj.ty->n); out.lit = X::Lit::Int; out.intValue = obj.ty->n; out.ty = fromSt("UDINT", "size_t"); return out; }
            if (m == "empty") { argc(0); out.st = "FALSE"; out.lit = X::Lit::Bool; out.boolish = true; out.ty = tBool(); return out; }
            if (m == "data") { argc(0); out.st = "CPTR_AT(" + v + ", 0)"; out.ty = pointerCT(obj.ty->elem); return out; }
            if (m == "fill") {
                argc(1);
                const std::string i = temp("DINT", line, "i");
                out.st = "FOR " + i + " := 0 TO " + std::to_string(obj.ty->n - 1) + " DO " + v + "[" + i + "] := " + valueFor(obj.ty->elem, args[0], line).st + "; END_FOR;";
                out.statement = true;
                return out;
            }
            out.lvalue = true;
            out.ty = obj.ty->elem;
            if (m == "front") { argc(0); out.st = v + "[0]"; return out; }
            if (m == "back") { argc(0); out.st = v + "[" + std::to_string(obj.ty->n - 1) + "]"; return out; }
            if (m == "at") { argc(1); out.st = v + "[" + args[0].st + "]"; out.arrBase = v; out.arrIndex = args[0].st; return out; }
            fail("std::array::" + m + " n'est pas pris en charge (size, at, front, back, fill, data, [i] le sont)", line);
        }
        // std::string
        out.call = false;
        if (m == "length" || m == "size") { argc(0); out.st = "LEN(" + v + ")"; out.ty = fromSt("UDINT", "size_t"); return out; }
        if (m == "empty") { argc(0); out.st = "(LEN(" + v + ") = 0)"; out.boolish = true; out.ty = tBool(); return out; }
        if (m == "c_str" || m == "data") { argc(0); out = obj; out.lvalue = false; out.ty = tText(); return out; }
        if (m == "substr") {
            if (args.empty() || args.size() > 2) argc(1);
            const std::string pos = args[0].st;
            const std::string n = args.size() == 2 ? args[1].st : "LEN(" + v + ") - (" + pos + ")";
            out.st = "MID(" + v + ", " + n + ", " + wrap(args[0]) + " + 1)";
            out.ty = tString();
            return out;
        }
        if (m == "find") { argc(1); out.st = "(FIND(" + v + ", " + textOf(args[0]) + ") - 1)"; out.ty = tLong(); return out; }
        if (m == "append" || m == "push_back") {
            argc(1);
            if (!obj.lvalue) fail(m + "() : sur une variable", line);
            out.st = v + " := " + v + " + " + textOf(args[0]) + ";";
            out.statement = true;
            return out;
        }
        if (m == "clear") {
            argc(0);
            out.st = v + " := '';";
            out.statement = true;
            return out;
        }
        if (m == "compare") {
            argc(1);
            out.st = "(" + v + " > " + textOf(args[0]) + " ? 1 : (" + v + " < " + textOf(args[0]) + " ? -1 : 0))";
            out.ty = tInt();
            return out;
        }
        fail("std::string::" + m + " n'est pas pris en charge (length, size, empty, substr, find, append, clear, compare, c_str le sont)", line);
    }

    // printf("Niveau %d : %.1f\n", n, x) -> IHM_JOURNAL('Niveau ' + TO_STRING(n) + ' : ' + TO_STRING(x))
    X printfCall(int line) {
        expect("(", "apr\xC3\xA8s printf");
        if (!at(Tok::K::String)) fail("printf : le format est un texte entre guillemets", line);
        std::string fmt;
        while (at(Tok::K::String)) fmt += next().text;
        std::vector<X> args;
        while (accept(",")) args.push_back(assignment(false));
        expect(")", "apr\xC3\xA8s les arguments de printf");
        std::vector<std::string> parts;
        std::string lit;
        std::size_t used = 0;
        for (std::size_t k = 0; k < fmt.size(); ++k) {
            if (fmt[k] != '%') {
                lit += fmt[k];
                continue;
            }
            if (k + 1 < fmt.size() && fmt[k + 1] == '%') {
                lit += '%';
                ++k;
                continue;
            }
            std::size_t e = k + 1;
            while (e < fmt.size() && std::string_view("-+ #0123456789.lhzjt").find(fmt[e]) != std::string_view::npos) ++e;
            if (e >= fmt.size() || std::string_view("diufFgGeEsScxXop").find(fmt[e]) == std::string_view::npos)
                fail("printf : format %" + fmt.substr(k + 1, e - k) + " non pris en charge", line);
            if (used >= args.size()) fail("printf : il manque une valeur pour %" + fmt.substr(k + 1, e - k), line);
            if (!lit.empty()) parts.push_back(stString(lit));
            lit.clear();
            const X& a = args[used++];
            if (fmt[e] == 's') parts.push_back(wrap(a));
            else if (fmt[e] == 'c' && a.lit == X::Lit::Int) parts.push_back(stString(std::string(1, static_cast<char>(a.intValue))));
            else parts.push_back("TO_STRING(" + a.st + ")");
            k = e;
        }
        if (used < args.size()) fail("printf : une valeur de trop (" + std::to_string(args.size()) + " pour " + std::to_string(used) + " %)", line);
        while (!lit.empty() && (lit.back() == '\n' || lit.back() == '\r')) lit.pop_back();
        if (!lit.empty() || parts.empty()) parts.push_back(stString(lit));
        X out;
        out.st = "IHM_JOURNAL(";
        for (std::size_t k = 0; k < parts.size(); ++k) out.st += (k ? " + " : "") + parts[k];
        out.st += ")";
        out.call = true;
        return out;
    }

    // ---- les valeurs
    X primary() {
        Depth guard(exprDepth_, peek().line);
        const Tok& t = peek();
        const int line = t.line;
        X out;
        switch (t.k) {
            case Tok::K::Number:
                next();
                out.st = t.text;
                out.lit = t.real ? X::Lit::Real : X::Lit::Int;
                out.intValue = t.value;
                out.ty = t.real ? (t.isFloat ? tFloat() : tDouble()) : (t.value > 2147483647LL ? tLong() : tInt());
                return out;
            case Tok::K::Char:
                next();
                out.st = t.text;
                out.lit = X::Lit::Int;
                out.intValue = t.value;
                out.ty = tChar();
                return out;
            case Tok::K::String: {
                std::string s;
                while (at(Tok::K::String)) s += next().text;
                out.st = stString(s);
                out.lit = X::Lit::Text;
                out.ty = textN(static_cast<std::int64_t>(s.size()) + 1);
                return out;
            }
            case Tok::K::End: fail("une valeur est attendue (fin du script atteinte)", line);
            case Tok::K::Punct:
                if (accept("(")) {
                    X v = assignment(false);
                    if (punct(",")) fail("l'op\xC3\xA9rateur virgule n'est pas pris en charge");
                    expect(")", "(une parenth\xC3\xA8se ouverte n'est pas ferm\xC3\xA9" "e)");
                    if (v.lvalue || !v.compound) return v;          // (x), (*p) : la variable elle-meme
                    v.st = "(" + v.st + ")";
                    v.compound = false;
                    v.call = false;
                    return v;
                }
                if (punct("[")) {
                    if (!isCpp()) fail("[ : une lambda est du C++", line);
                    return lambdaExpr();
                }
                if (punct("{")) fail("{ ... } : une liste de valeurs n'initialise qu'une d\xC3\xA9" "claration (T x = { ... };)", line);
                fail("une valeur est attendue" + found(), line);
            case Tok::K::Name: break;
        }
        const std::string name = next().text;
        if (name == "true" || name == "false" || name == "TRUE" || name == "FALSE") {
            out.st = upperOf(name);
            out.lit = X::Lit::Bool;
            out.boolish = true;
            out.ty = tBool();
            return out;
        }
        if (name == "NULL" || name == "nullptr") {
            out.st = "NULL";
            out.isNull = true;
            return out;
        }
        if (name == "std::string::npos" || name == "string::npos") {
            out.st = "-1";
            out.lit = X::Lit::Int;
            out.intValue = -1;
            out.ty = tLong();
            return out;
        }
        if (name == "this") {
            if (cur_->recordC.empty() || cur_->stName.empty()) fail("this : seulement dans une m\xC3\xA9thode d'une classe", line);
            out.st = "ADR(this_c)";
            out.derefSt = "this_c";
            out.ty = pointerCT(recordType(cur_->recordC));
            return out;
        }
        if (const std::string why = refusedWord(name); !why.empty()) fail(why, line);
        if (name == "static_cast" || name == "const_cast") {
            expect("<", ("apr\xC3\xA8s " + name).c_str());
            const CTP ty = typeName();
            closeAngle(("apr\xC3\xA8s le type de " + name).c_str());
            expect("(", ("apr\xC3\xA8s " + name + "<...>").c_str());
            if (isPtr(ty)) expectPtr_.push_back(ty);
            const X v = expression();
            if (isPtr(ty)) expectPtr_.pop_back();
            expect(")", ("apr\xC3\xA8s la valeur de " + name).c_str());
            return name == "const_cast" ? v : cast(ty, v, line);
        }
        if (name == "printf" || name == "std::printf") return printfCall(line);
        if (name == "puts" || name == "std::puts") {
            const auto args = callArgs();
            if (args.size() != 1) fail("puts(texte) : un argument", line);
            out.st = "IHM_JOURNAL(" + args[0].st + ")";
            out.call = true;
            return out;
        }
        if (punct("(")) return namedCall(name, line);
        // Point{1, 2} : un objet temporaire (C++), ses membres ou son constructeur
        if (punct("{") && isCpp() && records_.count(name) && !findVar(name)) {
            const CTP t = recordType(name);
            const std::string tmp = temp(t->st, line, "o");
            if (hasConstructors(name)) {
                addPre(construct(t, tmp, constructorArgs(), line), line);
            } else {
                std::vector<Cell> cells;
                bool constant = true;
                (void)initBraces(t, tmp, cells, constant, line);
                std::string text = construct(t, tmp, {}, line);
                for (const auto& c : cells) text += (text.empty() ? "" : " ") + c.path + " := " + c.st + ";";
                if (!text.empty()) addPre(text, line);
            }
            out.st = tmp;
            out.ty = t;
            out.lvalue = true;
            return out;
        }
        // T_MODE::Auto, Mode::Marche (enum class), Pompe::compte (un membre static)
        if (const auto sep = name.rfind("::"); sep != std::string::npos) {
            const std::string scope = name.substr(0, sep), item = name.substr(sep + 2);
            if (const auto e = enums_.find(scope); e != enums_.end()) {
                for (const auto& [n, v] : e->second.values)
                    if (n == item) return enumValue(scope, v);
                fail(item + " n'est pas une valeur de " + scope, line);
            }
            if (records_.count(scope)) {
                const Member* m = findMember(scope, item);
                if (m && m->isStatic) {
                    checkAccess(m->access, m->owner, item, line);
                    out.st = m->staticSt;
                    out.lvalue = true;
                    out.ty = m->type;
                    return out;
                }
                if (const auto e2 = enumerators_.find(item); e2 != enumerators_.end()) return enumValue(e2->second.first, e2->second.second);
                fail(name + " : ni un membre static ni une valeur de " + scope, line);
            }
            if (name.rfind("std::", 0) == 0) fail(name + " : non pris en charge (IHM_JOURNAL, printf ou std::cout pour \xC3\xA9" "crire au journal)", line);
            if (name.find("::", sep + 2) != std::string::npos || scope.find("::") != std::string::npos) fail(name + " : un nom simple est attendu", line);
            out.st = scope + "#" + item;                         // T_MODE::Auto (une enumeration IHM) -> T_MODE#Auto
            return out;
        }
        if (const Var* v = findVar(name)) {
            out.st = v->autoDeref ? v->st + "^" : v->st;
            out.lvalue = true;
            out.ty = v->type;
            out.boolish = v->type && v->type->k == CT::K::Bool;
            out.isConst = v->isConst;
            out.varC = name;
            return out;
        }
        if (cur_->lambda) {                                      // une lambda sans capture ne voit pas les locales d'autour
            for (const Code* o = cur_->outer; o; o = o->outer)
                if (findVarIn(o, name, false))
                    fail(name + " : une lambda sans capture ne voit pas les variables locales d'autour (\xC3\xA9" "cris [=] pour les copier)", line);
        }
        if (const auto e = enumerators_.find(name); e != enumerators_.end()) return enumValue(e->second.first, e->second.second);
        // un membre de la classe, dans une methode : this->m
        if (!cur_->recordC.empty())
            if (const Member* m = findMember(cur_->recordC, name)) {
                if (m->isStatic) out.st = m->staticSt;
                else {
                    if (cur_->stName.empty() || isStaticMethodHere()) fail(name + " : un membre de l'objet, hors d'une m\xC3\xA9thode de l'objet", line);
                    out.st = "this_c." + m->st;
                }
                out.lvalue = true;
                out.ty = m->type;
                out.boolish = m->type->k == CT::K::Bool;
                out.varC = name;
                return out;
            }
        if (const auto f = functions_.find(name); f != functions_.end() && !f->second.empty()) {   // une fonction donnee comme valeur
            const FnInfo& fi = f->second.front();
            out.st = fi.st;
            out.fnName = name;
            out.ty = funcCT(fi.ptypes, fi.ret);
            return out;
        }
        // une variable IHM, une constante de l'IHM, une valeur d'enumeration : tels quels
        out.st = name;
        out.lvalue = true;
        out.varC = name;
        return out;
    }
    bool isStaticMethodHere() const {
        const Record* r = record(cur_->recordC);
        if (!r) return false;
        for (const auto& m : r->methods)
            if (m.st == cur_->stName) return m.isStatic;
        return false;
    }
    X enumValue(const std::string& e, std::int64_t v) const {
        X out;
        out.st = v < 0 ? "(" + std::to_string(v) + ")" : std::to_string(v);
        out.lit = X::Lit::Int;
        out.intValue = v;
        out.ty = enumType(e);
        return out;
    }

    // nom(args) : une fonction du script, une methode de la classe (dans une methode), un constructeur (un objet
    // temporaire), une conversion, un rappel, une fonction de la bibliotheque, ou de l'IHM.
    X namedCall(const std::string& name, int line) {
        X out;
        // une variable rappel (un pointeur de fonction, std::function, une lambda rangee)
        if (const Var* v = findVar(name); v && v->type && v->type->k == CT::K::Func) {
            const auto args = callArgs();
            out.st = v->st + "(" + argumentText(v->type->params, {}, args, line, false) + ")";
            out.call = true;
            out.ty = v->type->ret;
            return out;
        }
        if (const Var* v = findVar(name); v && v->type && isPtr(v->type) && v->type->elem->k == CT::K::Func) {
            fail(name + "(...) : un pointeur vers un pointeur de fonction n'est pas pris en charge", line);
        }
        // une methode de la classe, dans une de ses methodes : this->m(...)
        if (!cur_->recordC.empty() && !findVar(name) && !findMethods(cur_->recordC, name).empty()) {
            X self;
            self.st = "this_c";
            self.lvalue = true;
            self.ty = recordType(cur_->recordC);
            const auto list = findMethods(cur_->recordC, name);
            bool allStatic = true;
            for (const auto* m : list) allStatic = allStatic && m->isStatic;
            if (!allStatic && (isStaticMethodHere() || cur_->stName.empty())) fail(name + "() : une m\xC3\xA9thode de l'objet, hors d'une m\xC3\xA9thode de l'objet", line);
            return methodCall(self, cur_->recordC, name, line, false);
        }
        // Classe::methode(...) : static, ou Base::m(...) dans une derivee (sans aiguillage)
        if (const auto sep = name.rfind("::"); sep != std::string::npos && records_.count(name.substr(0, sep))) {
            const std::string cls = name.substr(0, sep), m = name.substr(sep + 2);
            const auto list = findMethods(cls, m);
            if (list.empty()) fail(m + " n'est pas une m\xC3\xA9thode de " + cls, line);
            bool anyStatic = false;
            for (const auto* x : list) anyStatic = anyStatic || x->isStatic;
            X self;
            if (!anyStatic) {
                if (cur_->recordC.empty() || !derivesFrom(cur_->recordC, cls)) fail(name + "() : appelle-la sur un objet (x." + m + "(...))", line);
                self.st = "this_c";
                self.lvalue = true;
                self.ty = recordType(cls);
            }
            return methodCall(self, cls, m, line, true);
        }
        // une fonction du script
        if (isFunction(name) && !findVar(name)) {
            const auto args = callArgs();
            FnInfo* fi = findFunction(name, args.size(), line);
            out.st = fi->st + "(" + (fi->declared ? argumentText(fi->ptypes, fi->modes, args, line, false) : plainArgs(args)) + ")";
            out.call = true;
            out.ty = fi->ret;
            out.boolish = fi->ret && fi->ret->k == CT::K::Bool;
            return out;
        }
        // Point(1, 2) : un objet temporaire (C++)
        if (isCpp() && records_.count(name) && !findVar(name)) {
            const CTP t = recordType(name);
            const auto args = callArgs();
            const std::string tmp = temp(t->st, line, "o");
            if (hasConstructors(name) || needsConstruction(t)) {
                addPre(construct(t, tmp, args, line), line);
            } else {
                const auto members = layoutMembers(name);
                if (args.size() > members.size()) fail("trop de valeurs pour " + name, line);
                std::string text;
                for (std::size_t k = 0; k < args.size(); ++k) text += tmp + "." + members[k]->st + " := " + valueFor(members[k]->type, args[k], line).st + "; ";
                if (!text.empty()) addPre(text, line);
            }
            out.st = tmp;
            out.ty = t;
            out.lvalue = true;
            return out;
        }
        // int(x), Entier(x) : une conversion (C++)
        if (isCpp() && (typedefs_.count(name) || name == "int" || name == "float" || name == "double" || name == "bool")) {
            const CTP ty = typedefs_.count(name) ? typedefs_.at(name) : name == "int" ? tInt() : name == "float" ? tFloat() : name == "double" ? tDouble() : tBool();
            next();
            const X v = expression();
            expect(")", "apr\xC3\xA8s la valeur");
            return cast(ty, v, line);
        }
        if (name == "std::move" || name == "std::forward") {
            const auto args = callArgs();
            if (args.size() != 1) fail(name + "(x) : un argument", line);
            return args[0];
        }
        if (name == "std::swap" || name == "swap") {
            const auto args = callArgs();
            if (args.size() != 2 || !args[0].lvalue || !args[1].lvalue) fail("std::swap(a, b) : deux variables", line);
            if (!args[0].ty || args[0].ty->st.empty()) fail("std::swap : le type de " + args[0].st + " n'est pas connu ici", line);
            const std::string tmp = temp(args[0].ty->st, line, "s");
            out.st = tmp + " := " + args[0].st + "; " + args[0].st + " := " + args[1].st + "; " + args[1].st + " := " + tmp + ";";
            out.statement = true;
            return out;
        }
        if (name == "std::clamp" || name == "clamp") {      // std::clamp(x, lo, hi) -> LIMIT(lo, x, hi)
            const auto args = callArgs();
            if (args.size() != 3) fail("std::clamp(x, bas, haut) : trois arguments", line);
            out.st = "LIMIT(" + args[1].st + ", " + args[0].st + ", " + args[2].st + ")";
            out.call = true;
            out.ty = arith(args[0].ty ? args[0].ty : tUnknown(), args[1].ty ? args[1].ty : tUnknown());
            return out;
        }
        // le tas du C : malloc, calloc, free
        if (name == "malloc" || name == "calloc" || name == "free") {
            const auto args = callArgs();
            if (name == "free") {
                if (args.size() != 1) fail("free(p) : un argument", line);
                out.st = "CDELETE(" + args[0].st + ")";
                out.call = true;
                return out;
            }
            if (expectPtr_.empty() || !expectPtr_.back()->elem || expectPtr_.back()->elem->k == CT::K::Void)
                fail(name + " : \xC3\xA9" "cris le type du pointeur qui le re\xC3\xA7oit (int *t = " + name + "(...); ou (int*)" + name + "(...))", line);
            const CTP elem = expectPtr_.back()->elem;
            if (name == "malloc" && args.size() != 1) fail("malloc(taille) : un argument", line);
            if (name == "calloc" && args.size() != 2) fail("calloc(nombre, taille) : deux arguments", line);
            const std::string bytes = name == "malloc" ? wrap(args[0]) : wrap(args[0]) + " * " + wrap(args[1]);
            out.st = "CNEW('" + elem->st + "', " + bytes + " / " + std::to_string(std::max(1, elem->size)) + ")";
            out.call = true;
            out.ty = pointerCT(elem);
            return out;
        }
        if (name == "strcpy" || name == "strcat" || name == "strcmp" || name == "strncpy") {
            const auto args = callArgs();
            if (args.size() < 2) fail(name + " : deux arguments", line);
            if (name == "strcmp") {
                out.st = "(" + args[0].st + " > " + args[1].st + " ? 1 : (" + args[0].st + " < " + args[1].st + " ? -1 : 0))";
                out.ty = tInt();
                return out;
            }
            if (!args[0].lvalue) fail(name + " : le premier argument est un texte \xC3\xA0 \xC3\xA9" "crire", line);
            out.statement = true;
            if (name == "strncpy") out.st = args[0].st + " := LEFT(" + args[1].st + ", " + (args.size() > 2 ? args[2].st : std::string("255")) + ");";
            else out.st = args[0].st + " := " + (name == "strcat" ? args[0].st + " + " : std::string{}) + textOf(args[1]) + ";";
            return out;
        }
        if (name == "memcpy" || name == "memset" || name == "memmove") fail(name + " n'est pas pris en charge (une boucle, ou une affectation de structure)", line);
        if (name == "exit" || name == "abort") fail(name + " n'est pas pris en charge (return)", line);
        // la bibliotheque mathematique
        if (const std::string m = mappedCall(name); !m.empty()) {
            const auto args = callArgs();
            out.st = m + "(" + plainArgs(args) + ")";
            out.call = true;
            switch (libraryResult(m)) {
                case LibResult::Int: out.ty = tInt(); break;
                case LibResult::Text: out.ty = tString(); break;
                case LibResult::Same: out.ty = args.empty() ? tDouble() : args.size() == 1 ? args[0].ty : arith(args[0].ty ? args[0].ty : tUnknown(), args[1].ty ? args[1].ty : tUnknown()); break;
                default: out.ty = tDouble(); break;
            }
            if (m == "TO_STRING" && args.size() == 1 && args[0].ty && args[0].ty->k == CT::K::Real) out.st = "TO_STRING(" + args[0].st + ")";
            return out;
        }
        if (name.rfind("std::", 0) == 0) fail(name + " : non pris en charge (les fonctions math\xC3\xA9matiques std::sqrt, std::min... le sont)", line);
        if (name.find("::") != std::string::npos) fail(name + " : un nom simple est attendu", line);
        // une fonction de l'IHM (ou du projet)
        const auto args = callArgs();
        out.st = name + "(" + plainArgs(args) + ")";
        out.call = true;
        return out;
    }

    // [](int x) { return x * 2; } -> LAMBDA (x : DINT) : DINT ... END_LAMBDA, sur ses lignes. [=] : les locales copiees
    // (la LAMBDA du ST les copie a sa creation) ; [&] : refuse.
    X lambdaExpr() {
        const int line = next().line;                            // [
        bool copies = false;
        while (!punct("]")) {
            if (at(Tok::K::End)) fail("']' attendu : la capture de la lambda", line);
            if (punct("&") || punct("&&")) fail("une lambda qui capture par r\xC3\xA9" "f\xC3\xA9rence ([&]) n'est pas prise en charge : [=] (une copie) ou un param\xC3\xA8tre", line);
            if (word("this")) fail("une lambda qui capture this n'est pas prise en charge", line);
            copies = true;
            next();
        }
        next();                                                  // ]
        std::vector<std::pair<std::string, CTP>> params;
        std::vector<int> modes;
        if (punct("(")) parameters(params, modes);
        for (const int m : modes)
            if (m == 1) fail("un param\xC3\xA8tre r\xC3\xA9" "f\xC3\xA9rence (T&) d'une lambda n'est pas pris en charge", line);
        while (word("mutable") || word("noexcept") || word("constexpr")) next();
        CTP ret;
        if (accept("->")) ret = typeName();
        if (!punct("{")) fail("'{' attendu : le corps de la lambda" + found(), line);
        Code fn;
        fn.function = true;
        fn.lambda = true;
        fn.lambdaCopies = copies;
        fn.stName = "LAMBDA";
        fn.recordC = cur_->recordC;
        fn.result = ret && ret->k != CT::K::Void ? ret : nullptr;
        fn.outer = cur_;
        fn.scopes.emplace_back();
        fn.taken = cur_->taken;
        Code* saved = cur_;
        struct Back {
            Translator& t;
            Code*       c;
            ~Back() { t.cur_ = c; }
        } back{*this, saved};
        cur_ = &fn;
        std::string ps;
        for (std::size_t k = 0; k < params.size(); ++k) {
            const std::string name = params[k].first.empty() ? "p" + std::to_string(k + 1) : params[k].first;
            const std::string pst = declare(name, params[k].second, line);
            noteVar(pst, name, params[k].second);
            ps += (ps.empty() ? "" : "; ") + pst + " : " + params[k].second->st;
        }
        // le corps : ses morceaux pris a part (puis remis dans le texte de la lambda, ligne pour ligne)
        const std::size_t mark = frags_.size();
        const int keepLast = last_;
        last_ = std::max(last_, line);
        const auto savedLoops = loops_;
        loops_.clear();
        std::vector<Fx> savedFx;
        savedFx.swap(fx_);
        fx_.push_back(Fx{});
        functionBodyLambda(line);
        fx_.swap(savedFx);
        loops_ = savedLoops;
        const int endLine = last_;
        std::map<int, std::string> lines;
        for (std::size_t k = mark; k < frags_.size(); ++k) {
            int l = frags_[k].line;
            std::string& tx = frags_[k].text;
            std::size_t from = 0;
            while (true) {
                const auto nl = tx.find('\n', from);
                const std::string part = tx.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
                if (!part.empty()) lines[l] += (lines[l].empty() ? "" : " ") + part;
                if (nl == std::string::npos) break;
                from = nl + 1;
                ++l;
            }
        }
        frags_.resize(mark);
        last_ = keepLast;
        const CTP result = fn.result ? fn.result : fn.deduced;
        std::string header = "LAMBDA (" + ps + ")" + (result ? " : " + result->st : std::string{});
        const std::string decls = declarationBlock(fn, false);
        if (!decls.empty()) header += " " + decls;
        std::string text = "\x02" + std::to_string(line) + "\x02" + header;
        int cursor = line;
        for (const auto& [l, s] : lines) {
            while (cursor < l) {
                text += "\n";
                ++cursor;
            }
            text += " " + s;
        }
        while (cursor < endLine) {
            text += "\n";
            ++cursor;
        }
        text += " END_LAMBDA";
        auto& vars = debug_->vars["LAMBDA"];
        vars.insert(vars.end(), fn.dbg.begin(), fn.dbg.end());
        X out;
        out.st = text;
        out.ty = funcCT(params, result);
        return out;
    }
    // Le corps d'une lambda : comme une fonction, sans END_FUNCTION (END_LAMBDA vient apres).
    void functionBodyLambda(int line) {
        expect("{", "(le corps de la lambda)");
        cur_->scopes.emplace_back();
        while (!punct("}")) {
            if (at(Tok::K::End)) fail("'}' attendu : la fin de la lambda", line);
            statement();
        }
        last_ = std::max(last_, peek().line);
        next();
        cur_->scopes.pop_back();
    }

    // ---- le debogueur : une expression traduite dans la portee d'une ligne
    void probeHere(int line, bool closing) {
        (void)closing;
        if (!probing_ || probeDone_ || probeLine_ <= 0 || line < probeLine_) return;
        doProbe();
    }
    void doProbe() {
        probeDone_ = true;
        try {
            Macros none = macros_;
            Lexer lex(probeText_, none);
            const auto raw = lex.run();
            std::vector<Tok> toks;
            std::size_t q = 0;
            std::set<std::string> hide;
            expandInto(raw, q, raw.size(), macros_, hide, toks, 0);
            if (toks.empty() || toks.back().k != Tok::K::End) {
                Tok end;
                end.k = Tok::K::End;
                toks.push_back(end);
            }
            std::swap(toks, toks_);
            const std::size_t saveI = i_;
            i_ = 0;
            struct Back {
                Translator&       t;
                std::vector<Tok>& toks;
                std::size_t       i;
                ~Back() {
                    std::swap(toks, t.toks_);
                    t.i_ = i;
                }
            } back{*this, toks, saveI};
            if (at(Tok::K::End)) throw Fault{1, "une expression est attendue"};
            FxScope fx(*this, false, "un espion ou une condition ne change rien : pas de ++, de -- ni d'affectation");
            const std::size_t mark = frags_.size();
            const X v = expression();
            frags_.resize(mark);
            if (!at(Tok::K::End)) throw Fault{1, "la fin de l'expression est attendue" + found()};
            probeResult_ = probeCondition_ ? boolOf(v) : v.st;
        } catch (const Fault& f) {
            probeError_ = f.message;
        } catch (const SwitchRetry&) {
            probeError_ = "expression illisible";
        }
    }

public:
    void probeAsCondition(bool c) { probeCondition_ = c; }
    std::shared_ptr<DebugInfo> debugInfo() const { return debug_; }

private:
    std::string_view                       code_;
    ScriptLang                             lang_;
    std::vector<std::string>               entryNames_;
    std::string                            print_;
    Macros                                 macros_;
    std::vector<Tok>                       toks_;
    std::size_t                            i_{0};
    std::vector<Frag>                      frags_;
    int                                    last_{1};
    std::set<std::string>                  allNames_;
    std::map<std::string, std::set<std::string>> spellings_;   // un nom (majuscules) -> ses ecritures dans le script
    std::map<std::string, std::vector<FnInfo>> functions_;
    std::set<std::string>                  definedNow_;
    std::map<std::string, std::int64_t>    constInts_;
    std::vector<CTP>                       expectPtr_;          // le type du pointeur qui recoit (malloc)
    int                                    templateArg_{0};     // dans std::array<T, N> : > ferme le modele
    std::string                            endPrefix_;          // un destructeur : celui du parent, avant END_FUNCTION
    Code                                   script_;
    struct Global {
        std::size_t hoisted;
        std::size_t assign;
        std::string literal;
        int         line;
        bool        hasInit, constant;
    };
    std::vector<Global>                    globals_;
    std::vector<std::pair<std::string, int>> declared_;
    std::vector<int>                       declarationLines_;
    Code*                                  cur_{nullptr};
    bool                                   topStatements_{false};
    int                                    depth_{0};
    int                                    exprDepth_{0};
    std::shared_ptr<DebugInfo>             debug_ = std::make_shared<DebugInfo>();
    // le debogueur
    bool                                   probing_{false}, probeDone_{false}, probeCondition_{false};
    int                                    probeLine_{0};
    std::string                            probeText_, probeResult_, probeError_;
};

Translation Translator::run() {
    Translation out;
    int lines = 1;
    for (const char c : code_)
        if (c == '\n') ++lines;
    try {
        Lexer lex(code_, macros_);
        const auto raw = lex.run();
        std::size_t q = 0;
        std::set<std::string> hide;
        expandInto(raw, q, raw.size(), macros_, hide, toks_, 0);
        if (toks_.empty() || toks_.back().k != Tok::K::End) {
            Tok end;
            end.k = Tok::K::End;
            end.line = lines;
            toks_.push_back(end);
        }
        for (const auto& tk : toks_)
            if (tk.k == Tok::K::Name) {
                allNames_.insert(upperOf(tk.text));
                spellings_[upperOf(tk.text)].insert(tk.text);
            }
        scanFunctions();
        cur_ = &script_;
        script_.scopes.emplace_back();
        frags_.push_back({1, ""});                   // les declarations du script, patchees a la fin
        fx_.push_back(Fx{});
        while (!at(Tok::K::End)) topLevel();
        if (probing_ && !probeDone_) doProbe();
        // Rien que des fonctions (et des declarations) : un fichier C - ses variables globales sont gardees d'une
        // execution a l'autre, initialisees une fois (comme en C : une constante dans leur declaration, sinon a la
        // premiere execution, avant main). Avec des instructions au niveau du script, ce niveau est le corps d'une
        // fonction appelee a chaque execution : ses variables repartent (static : gardees).
        bool anyFunction = false;
        for (const auto& [cname, list] : functions_)
            for (const auto& fi : list) anyFunction = anyFunction || fi.defined;
        std::string once;
        if (!topStatements_ && anyFunction)
            for (const auto& g : globals_) {
                auto& h = script_.hoisted[g.hoisted];
                h.keep = true;
                if (g.constant) h.init = g.literal;
                if (g.assign < frags_.size()) {
                    if (!g.constant) once += (once.empty() ? "" : " ") + frags_[g.assign].text;
                    frags_[g.assign].text.clear();
                }
            }
        // Les structures du script, pour le moteur : celle de chaque racine porte les membres de ses classes derivees.
        std::map<std::string, bool> rootPoly;
        for (const auto& [n, r] : records_) rootPoly[r.root] = rootPoly[r.root] || r.polymorphic;
        for (const auto& name : recordOrder_) {
            const Record* r = record(name);
            if (!r || r->root != name || !r->complete) continue;
            std::vector<std::pair<std::string, std::string>> members;
            if (rootPoly[name]) members.emplace_back("cvt_", "DINT");
            for (const auto& m : fat_[name]) members.push_back(m);
            if (members.empty()) members.emplace_back("cvide_", "BOOL");
            out.types.emplace_back(r->st, std::move(members));
        }
        // Les aiguillages des methodes virtuelles (a la fin : toutes les classes derivees sont connues).
        std::string tail;
        for (const auto& [key, st] : dispatchers_) {
            const auto colon = key.find("::");
            const auto slash = key.rfind('/');
            const std::string rootCls = key.substr(0, colon);
            const std::string mname = key.substr(colon + 2, slash - colon - 2);
            const std::size_t arity = static_cast<std::size_t>(std::atoi(key.substr(slash + 1).c_str()));
            const bool dtor = !mname.empty() && mname[0] == '~';
            const Method* vm = nullptr;
            for (const auto& m : records_[rootCls].methods)
                if ((m.c == mname || (dtor && m.dtor)) && m.params.size() == arity) vm = &m;
            if (!vm) continue;
            std::string params = "VAR_IN_OUT this_c : " + records_[rootCls].st, args = "this_c";
            for (std::size_t k = 0; k < vm->params.size(); ++k) {
                const std::string pn = "p" + std::to_string(k + 1) + "_";
                params += "; " + paramDecl(pn, vm->params[k].second, k < vm->modes.size() ? vm->modes[k] : 0);
                args += ", " + pn;
            }
            std::string body = "FUNCTION " + st + "(" + params + ")" + (vm->ret ? " : " + vm->ret->st : std::string{}) + " CASE this_c.cvt_ OF";
            for (const auto& name : recordOrder_) {
                const Record& c = records_[name];
                if (!c.polymorphic || !c.tag || !derivesFrom(name, rootCls)) continue;
                const Method* impl = nullptr;
                for (const Record* x = &c; x && !impl; x = x->base.empty() ? nullptr : record(x->base))
                    for (const auto& y : x->methods)
                        if ((y.c == mname || (dtor && y.dtor)) && y.params.size() == arity && !y.pure && !y.isStatic) impl = &y;
                if (!impl) continue;
                body += " " + std::to_string(c.tag) + ": " + (vm->ret ? "RETURN " + impl->st + "(" + args + ");" : impl->st + "(" + args + ");");
            }
            body += " END_CASE; END_FUNCTION";
            tail += (tail.empty() ? "" : " ") + body;
            debug_->hidden.insert(upperOf(st));
            debug_->functions[upperOf(st)] = rootCls + "::" + mname + " (virtuelle)";
        }
        // Le point d'entree : aucune instruction au niveau du script -> main(), le nom du script, son evenement.
        if (!topStatements_) {
            std::vector<std::string> wanted = {"main"};
            for (const auto& n : entryNames_) wanted.push_back(n);
            for (const auto& w : wanted) {
                for (const auto& [cname, list] : functions_)
                    for (const auto& fi : list)
                        if (upperOf(cname) == upperOf(w) && fi.params == 0 && fi.defined && out.entry.empty()) out.entry = cname;
                if (!out.entry.empty()) break;
            }
        }
        std::string end;
        if (!once.empty()) {
            const std::string flag = scriptVar("cdemarre", "BOOL", {}, 1, true);
            end += "IF NOT " + flag + " THEN " + flag + " := TRUE; " + once + " END_IF;";
        }
        if (!out.entry.empty()) end += (end.empty() ? "" : " ") + findFunction(out.entry, 0, 1)->st + "();";
        if (!tail.empty()) end += (end.empty() ? "" : " ") + tail;
        if (!end.empty()) emit(std::max(last_, lines), end);
        // Les declarations du script, en tete de la ligne 1.
        frags_[0].text = declarationBlock(script_, true);
        note(script_, 1);
        std::vector<std::string> text(static_cast<std::size_t>(lines));
        for (const auto& f : frags_) {
            if (f.text.empty()) continue;
            int l = std::clamp(f.line, 1, lines);
            std::size_t from = 0;
            while (true) {
                const auto nl = f.text.find('\n', from);
                const std::string part = f.text.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
                auto& slot = text[static_cast<std::size_t>(l - 1)];
                if (!part.empty() && part.find_first_not_of(' ') != std::string::npos) slot += (slot.empty() ? "" : " ") + part;
                if (nl == std::string::npos) break;
                from = nl + 1;
                l = std::min(l + 1, lines);
            }
        }
        for (std::size_t k = 0; k < text.size(); ++k) out.st += (k ? "\n" : "") + text[k];
        out.declared = std::move(declared_);
        out.declarationLines = std::move(declarationLines_);
        // le debogueur : les variables du script, les structures, les enum
        auto& vars = debug_->vars[""];
        vars.insert(vars.end(), script_.dbg.begin(), script_.dbg.end());
        for (const auto& name : recordOrder_) {
            const Record& r = records_[name];
            if (!r.complete) continue;
            debug_->recordOf[name] = upperOf(r.st);
            if (!r.base.empty()) debug_->parents[name] = r.base;
            if (r.tag) debug_->tags[r.tag] = name;
            auto& dr = debug_->records[upperOf(r.st)];
            if (dr.cname.empty() || r.root == name) dr.cname = r.root;
            for (const auto& m : r.members) {
                if (m.isStatic) continue;
                DebugRecord::Member dm;
                dm.st = m.st;
                dm.c = m.c;
                dm.owner = name;
                dm.type = m.type->shown;
                dm.record = m.type->rec;
                dm.enumName = m.type->enumName;
                dr.members.push_back(std::move(dm));
            }
        }
        for (const auto& [n, e] : enums_)
            for (const auto& [vn, v] : e.values) debug_->enums[n][v] = e.scoped ? n + "::" + vn : vn;
        if (!out.entry.empty()) debug_->functions[upperOf(findFunction(out.entry, 0, 1)->st)] = out.entry;
    } catch (const Fault& f) {
        ScriptDiagnostic d;
        d.severity = ScriptDiagnostic::Severity::Error;
        d.line = std::max(1, f.line);
        d.message = f.message;
        out.errors.push_back(std::move(d));
        out.st.clear();
        out.entry.clear();
        out.types.clear();
    } catch (const SwitchRetry&) {
        ScriptDiagnostic d;
        d.severity = ScriptDiagnostic::Severity::Error;
        d.line = std::max(1, peek().line);
        d.message = "switch illisible";
        out.errors.push_back(std::move(d));
        out.st.clear();
    }
    out.debug = debug_;
    return out;
}

} // namespace

// ------------------------------------------------------------- les structures --
namespace {
std::mutex& registryMutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>>& registry() {
    static std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> r;
    return r;
}
void registerTypes(const Translation& t) {
    if (t.types.empty()) return;
    std::lock_guard<std::mutex> lock(registryMutex());
    auto& r = registry();
    if (r.size() > 20000) r.clear();                     // une longue seance de frappe : les scripts se re-traduisent
    for (const auto& [name, members] : t.types) r[upperOf(name)] = members;
}
} // namespace

bool structMembers(std::string_view stType, std::vector<std::pair<std::string, std::string>>& out) {
    std::lock_guard<std::mutex> lock(registryMutex());
    const auto it = registry().find(upperOf(stType));
    if (it == registry().end()) return false;
    out = it->second;
    return true;
}

bool isScriptType(std::string_view stType) {
    std::lock_guard<std::mutex> lock(registryMutex());
    return registry().count(upperOf(stType)) > 0;
}

Translation translate(std::string_view code, ScriptLang lang, const std::vector<std::string>& entryNames) {
    Translator t(code, lang == ScriptLang::ST ? ScriptLang::C : lang, entryNames);
    Translation out = t.run();
    registerTypes(out);
    return out;
}

Translation translated(std::string_view code, ScriptLang lang, const std::vector<std::string>& entryNames) {
    static std::mutex m;
    static std::unordered_map<std::string, Translation> cache;
    std::string key(1, static_cast<char>('0' + static_cast<int>(lang)));
    for (const auto& n : entryNames) key += n + "\x01";
    key += '\x02';
    key += code;
    {
        std::lock_guard<std::mutex> lock(m);
        if (const auto it = cache.find(key); it != cache.end()) {
            registerTypes(it->second);                   // le moteur les connait (le registre a pu se vider)
            return it->second;
        }
    }
    Translation t = translate(code, lang, entryNames);
    std::lock_guard<std::mutex> lock(m);
    if (cache.size() > 256) cache.clear();
    cache.emplace(std::move(key), t);
    return t;
}

// --------------------------------------------------------------- les messages --
namespace {
// Remplace chaque mot `from` (sans casse, un mot entier) par `to`.
void replaceWord(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    const std::string uf = upperOf(from);
    std::string us = upperOf(s);
    for (std::size_t at = us.find(uf); at != std::string::npos; at = us.find(uf, at)) {
        const bool before = at == 0 || !namePart(s[at - 1]);
        const bool after = at + from.size() >= s.size() || !namePart(s[at + from.size()]);
        if (before && after) {
            s.replace(at, from.size(), to);
            us.replace(at, from.size(), upperOf(to));
            at += to.size();
        } else {
            at += from.size();
        }
    }
}
} // namespace

std::string cMessage(const Translation& t, std::string_view message) {
    std::string out(message);
    if (!t.debug) return out;
    for (const auto& [st, r] : t.debug->records) replaceWord(out, st, r.cname);
    for (const auto& [st, c] : t.debug->functions)
        if (!c.empty() && c.find(' ') == std::string::npos) replaceWord(out, st, c);
    replaceWord(out, "this_c", "this");
    return out;
}

void placeFault(const Translation& t, ScriptDiagnostic& d) {
    d.message = cMessage(t, d.message);
    if (std::find(t.declarationLines.begin(), t.declarationLines.end(), d.line) == t.declarationLines.end()) return;
    const std::string m = upperOf(d.message);
    for (const auto& [name, line] : t.declared) {
        for (std::size_t at = m.find(name); at != std::string::npos; at = m.find(name, at + 1)) {
            const bool before = at == 0 || !namePart(m[at - 1]);
            const bool after = at + name.size() >= m.size() || !namePart(m[at + name.size()]);
            if (before && after) {
                d.line = line;
                d.column = 0;
                d.length = 0;
                return;
            }
        }
    }
}

std::vector<std::string> entryNamesOf(const Script& sc) {
    std::vector<std::string> out;
    if (!sc.name.empty()) out.push_back(sc.name);
    if (!sc.event.empty()) out.push_back(sc.event);
    return out;
}

// --------------------------------------------------------------- le debogueur --
std::string expressionAt(std::string_view code, ScriptLang lang, const std::vector<std::string>& entryNames, int line,
                         std::string_view expression, std::string* why, bool condition) {
    static std::mutex m;
    static std::unordered_map<std::string, std::pair<std::string, std::string>> cache;
    std::string key = std::to_string(line) + (condition ? "c" : "v") + std::string(1, static_cast<char>('0' + static_cast<int>(lang)));
    for (const auto& n : entryNames) key += n + "\x01";
    key += '\x02';
    key += expression;
    key += '\x03';
    key += code;
    {
        std::lock_guard<std::mutex> lock(m);
        if (const auto it = cache.find(key); it != cache.end()) {
            if (why) *why = it->second.second;
            return it->second.first;
        }
    }
    Translator t(code, lang == ScriptLang::ST ? ScriptLang::C : lang, entryNames);
    t.probe(line, expression);
    t.probeAsCondition(condition);
    const Translation tr = t.run();
    std::string result, error;
    if (!tr.ok()) error = "le script ne se traduit pas (ligne " + std::to_string(tr.errors.front().line) + " : " + tr.errors.front().message + ")";
    else if (!t.probeError().empty()) error = t.probeError();
    else result = t.probeResult();
    std::lock_guard<std::mutex> lock(m);
    if (cache.size() > 512) cache.clear();
    cache[key] = {result, error};
    if (why) *why = error;
    return result;
}

std::string shownFunction(const Translation& t, std::string_view stFunction) {
    if (!t.debug || stFunction.empty()) return std::string(stFunction);
    const auto it = t.debug->functions.find(upperOf(stFunction));
    if (it != t.debug->functions.end()) return it->second;
    if (upperOf(stFunction) == "LAMBDA") return "lambda";
    return std::string(stFunction);
}

bool hiddenFunction(const Translation& t, std::string_view stFunction) {
    return t.debug && t.debug->hidden.count(upperOf(stFunction)) > 0;
}

namespace {
const DebugVar* findDebugVar(const DebugInfo& d, std::string_view fn, std::string_view st) {
    const std::string uf = upperOf(fn), us = upperOf(st);
    for (const std::string& key : {uf, std::string{}}) {
        const auto it = d.vars.find(key);
        if (it == d.vars.end()) continue;
        for (const auto& v : it->second)
            if (upperOf(v.st) == us) return &v;
        if (key.empty()) break;
    }
    return nullptr;
}

bool classIsA(const DebugInfo& d, const std::string& cls, const std::string& base) {
    for (std::string c = cls; !c.empty();) {
        if (c == base) return true;
        const auto it = d.parents.find(c);
        c = it == d.parents.end() ? std::string{} : it->second;
    }
    return false;
}

std::string enumText(const DebugInfo& d, const std::string& enumName, const std::string& value) {
    const auto e = d.enums.find(enumName);
    if (e == d.enums.end()) return value;
    char* end = nullptr;
    const long long v = std::strtoll(value.c_str(), &end, 10);
    if (!end || *end != '\0' || value.empty()) return value;
    const auto it = e->second.find(v);
    return it == e->second.end() ? value : it->second + " (" + value + ")";
}

// Une valeur montree en C : les membres d'une structure (selon son type reel), les cases, la cible d'un pointeur.
void showValue(const DebugInfo& d, sim::LineProbe::Local& l, const std::string& recordC, const std::string& enumName, int depth) {
    if (depth > 8) return;
    if (!enumName.empty() && l.children.empty()) l.value = enumText(d, enumName, l.value);
    if (l.children.empty()) return;
    const bool cells = !l.children.empty() && !l.children.front().name.empty() && l.children.front().name[0] == '[';
    if (cells || (l.children.size() == 1 && l.children.front().name == "*")) {
        // le type C d'une case (int[4] -> int ; int[2][3] -> int[3]) ou de la cible (int* -> int)
        std::string inner;
        const std::string& t = l.type;
        if (cells) {
            if (const auto b = t.find('['); b != std::string::npos) {
                const auto e = t.find(']', b);
                if (e != std::string::npos) inner = t.substr(0, b) + t.substr(e + 1);
            } else if (t.rfind("std::array<", 0) == 0 || t.rfind("std::vector<", 0) == 0) {
                const auto lt = t.find('<');
                const auto comma = t.rfind(',');
                inner = t.substr(lt + 1, (t.back() == '>' && t.rfind("std::vector<", 0) == 0 ? t.size() - 1 : comma) - lt - 1);
            }
        } else if (!t.empty() && t.back() == '*') {
            inner = t.substr(0, t.size() - 1);
            if (inner.rfind("const ", 0) == 0) inner = inner.substr(6);
        }
        for (auto& c : l.children) {
            if (!inner.empty()) c.type = inner;
            showValue(d, c, recordC, enumName, depth + 1);
        }
        return;
    }
    // une structure : son type reel (cvt_), ses membres par leur nom C, sans ceux des autres classes de sa hierarchie
    std::string actual = recordC;
    for (const auto& c : l.children)
        if (upperOf(c.name) == "CVT_") {
            char* end = nullptr;
            const long tag = std::strtol(c.value.c_str(), &end, 10);
            if (const auto it = d.tags.find(static_cast<int>(tag)); it != d.tags.end()) actual = it->second;
        }
    const auto rit = d.recordOf.find(actual);
    const DebugRecord* r = rit == d.recordOf.end() ? nullptr : &d.records.at(rit->second);
    std::vector<sim::LineProbe::Local> kept;
    for (auto& c : l.children) {
        const std::string u = upperOf(c.name);
        if (u == "CVT_" || u == "CVIDE_") continue;
        const DebugRecord::Member* m = nullptr;
        if (r)
            for (const auto& x : r->members)
                if (upperOf(x.st) == u) m = &x;
        if (r && m && !actual.empty() && !classIsA(d, actual, m->owner)) continue;
        if (m) {
            c.name = m->c;
            if (!m->type.empty()) c.type = m->type;
            showValue(d, c, m->record, m->enumName, depth + 1);
        }
        kept.push_back(std::move(c));
    }
    l.children = std::move(kept);
    if (!actual.empty() && actual != recordC && !recordC.empty()) l.type = actual + " (" + l.type + ")";
    // la valeur en une ligne : les membres montres, sous leur nom C ({x: 7, y: 8})
    if (r) {
        std::string v = "{";
        for (std::size_t k = 0; k < l.children.size(); ++k) v += (k ? ", " : "") + l.children[k].name + ": " + l.children[k].value;
        v += "}";
        if (v.size() > 160) v = v.substr(0, 157) + "...}";
        l.value = v;
    }
}
} // namespace

void showLocals(const Translation& t, std::string_view stFunction, std::vector<sim::LineProbe::Local>& locals) {
    if (!t.debug) return;
    const DebugInfo& d = *t.debug;
    std::vector<sim::LineProbe::Local> out;
    for (auto& l : locals) {
        if (!stFunction.empty() && upperOf(l.name) == upperOf(stFunction)) continue;     // le resultat de la fonction (son nom ST)
        const DebugVar* v = findDebugVar(d, stFunction, l.name);
        if (v && v->hidden) continue;
        if (!v) {
            const std::string u = upperOf(l.name);
            if (u.size() > 1 && u.back() == '_' && u[0] == 'C' && u.find_first_of("0123456789") != std::string::npos) continue;   // un temporaire
        }
        if (v) {
            if (!v->c.empty()) l.name = v->c;
            if (!v->type.empty()) l.type = v->type;
            showValue(d, l, v->record, v->enumName, 0);
        }
        out.push_back(std::move(l));
    }
    locals = std::move(out);
}

std::string shownName(const Translation& t, std::string_view stFunction, std::string_view stText) {
    if (!t.debug) return std::string(stText);
    const DebugInfo& d = *t.debug;
    // les mots, un par un : un nom de variable -> son nom C ; un temporaire : rien a montrer
    std::string out;
    std::string s(stText);
    for (std::size_t k = 0; k < s.size();) {
        if (nameStart(s[k])) {
            std::size_t e = k;
            while (e < s.size() && namePart(s[e])) ++e;
            const std::string w = s.substr(k, e - k);
            const bool member = k > 0 && s[k - 1] == '.';
            std::string shown = w;
            if (!member) {
                const DebugVar* v = findDebugVar(d, stFunction, w);
                if (v && v->hidden) return {};
                if (v && !v->c.empty()) shown = v->c;
                if (upperOf(w) == "THIS_C") shown = "this";
                if (upperOf(w) == "CPTR_ADD" || upperOf(w) == "CPTR_AT") return {};
            } else {
                for (const auto& [st, r] : d.records)
                    for (const auto& m : r.members)
                        if (upperOf(m.st) == upperOf(w)) shown = m.c;
                if (upperOf(w) == "CVT_") return {};
            }
            out += shown;
            k = e;
            continue;
        }
        out += s[k++];
    }
    // p^.x -> p->x ; p^ -> *p ; this.x -> this->x
    for (std::size_t at = out.find("^."); at != std::string::npos; at = out.find("^.")) out.replace(at, 2, "->");
    for (std::size_t at = out.find("this."); at != std::string::npos; at = out.find("this.", at + 6)) out.replace(at, 5, "this->");
    if (!out.empty() && out.back() == '^') out = "*" + out.substr(0, out.size() - 1);
    return out;
}

std::string withoutInactive(std::string_view code) {
    std::string out(code);
    try {
        Macros macros;
        Lexer lex(code, macros);
        (void)lex.run();
        if (lex.inactive_.empty()) return out;
        std::set<int> gone(lex.inactive_.begin(), lex.inactive_.end());
        int line = 1;
        for (auto& c : out) {
            if (c == '\n') {
                ++line;
                continue;
            }
            if (gone.count(line) && c != '\r') c = ' ';
        }
    } catch (const Fault&) {
    }
    return out;
}

std::string supportedSubset(ScriptLang lang) {
    const bool cpp = lang == ScriptLang::Cpp;
    return std::string(cpp ? "C++" : "C")
         + " : compil\xC3\xA9 et ex\xC3\xA9" "cut\xC3\xA9 par le moteur des scripts de l'IHM (le m\xC3\xAAme que le ST), pas \xC3\xA0 pas compris (points d'arr\xC3\xAAt, F10, F11), un "
           "sous-ensemble : int, float, double, bool, char, les intN_t, un texte (const char*"
         + std::string(cpp ? ", std::string" : "") + "), les types IHM et du ST (REAL, DINT...), les tableaux t[N][M], struct, enum"
         + std::string(cpp ? " (et enum class)" : "") + ", typedef" + std::string(cpp ? ", using" : "") + ", const, sizeof, #define (et les macros \xC3\xA0 param\xC3\xA8tres) ; "
           "if, switch/case (un case qui continue dans le suivant compris), while, do, for"
         + std::string(cpp ? " (et for (T x : tableau))" : "") + ", break, continue, return ; "
           "les fonctions du script (par valeur, pointeur T* p"
         + std::string(cpp ? ", r\xC3\xA9" "f\xC3\xA9rence T& r" : "") + ") ; les op\xC3\xA9rateurs du C (x++ au milieu d'une expression compris) ; sqrt, pow, sin, min, max, printf (au journal) ; "
           "les pointeurs (T* p = &x ; *p ; p->m ; NULL) et leur arithm\xC3\xA9tique dans un tableau (p + i, p++, p[i], p - q) ; "
         + std::string(cpp ? "les classes (membres, m\xC3\xA9thodes, constructeurs, this, public/private, l'h\xC3\xA9ritage simple, les m\xC3\xA9thodes virtuelles), "
                             "new et delete (un tas g\xC3\xA9r\xC3\xA9 : delete deux fois, un objet employ\xC3\xA9 apr\xC3\xA8s delete sont des erreurs), les lambdas (sans capture, ou [=]), "
                             "std::array, std::vector, std::string, std::function, std::cout ; "
                           : "malloc, calloc et free (un tas g\xC3\xA9r\xC3\xA9 : free deux fois, un objet employ\xC3\xA9 apr\xC3\xA8s free sont des erreurs) ; ")
         + "les variables et les fonctions IHM par leur nom. Pas de goto, "
         + std::string(cpp ? "templates, exceptions (try/catch/throw), h\xC3\xA9ritage multiple, surcharge des op\xC3\xA9rateurs, lambdas qui capturent par r\xC3\xA9" "f\xC3\xA9rence, "
                           : "")
         + "union, realloc, ni d'arithm\xC3\xA9tique sur un pointeur qui ne vise pas un tableau.";
}

std::string compiledNote(ScriptLang lang) {
    return std::string(lang == ScriptLang::Cpp ? "C++" : "C")
         + " : compil\xC3\xA9 pour le moteur des scripts de l'IHM (un sous-ensemble du " + (lang == ScriptLang::Cpp ? "C++" : "C") + "), ex\xC3\xA9" "cut\xC3\xA9 en simulation, pas \xC3\xA0 pas compris";
}

} // namespace hmi::cscript
