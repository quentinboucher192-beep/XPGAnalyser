#include "Interpreter.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <unordered_map>

namespace sim {
namespace {

// ============================================================== lexer ========
enum class Tok : std::uint8_t {
    End, Identifier, Number, TimeLiteral, StringLiteral, Punct, Keyword,
};

struct Token {
    Tok           kind{Tok::End};
    std::string   text;      // upper-cased for keywords, verbatim otherwise
    std::string   raw;
    std::uint32_t line{1};
    Value         literal;
};

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c)  { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

const std::vector<std::string>& keywords() {
    static const std::vector<std::string> k = {
        "IF", "THEN", "ELSIF", "ELSE", "END_IF", "CASE", "OF", "END_CASE",
        "FOR", "TO", "BY", "DO", "END_FOR", "WHILE", "END_WHILE",
        "REPEAT", "UNTIL", "END_REPEAT", "EXIT", "RETURN",
        "AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE",
    };
    return k;
}

// TIME literals: T#500ms, T#1s500ms, TIME#2m30s, t#1h.
bool parseDuration(std::string_view body, std::int64_t& milliseconds) {
    std::int64_t total = 0;
    std::size_t i = 0;
    bool any = false;
    while (i < body.size()) {
        std::size_t start = i;
        while (i < body.size() && (std::isdigit(static_cast<unsigned char>(body[i])) || body[i] == '.')) ++i;
        if (i == start) return false;
        double amount = 0;
        try { amount = std::stod(std::string(body.substr(start, i - start))); }
        catch (...) { return false; }

        std::size_t unitStart = i;
        while (i < body.size() && std::isalpha(static_cast<unsigned char>(body[i]))) ++i;
        const auto unit = upperOf(body.substr(unitStart, i - unitStart));

        if      (unit == "MS") total += static_cast<std::int64_t>(amount);
        else if (unit == "S")  total += static_cast<std::int64_t>(amount * 1000.0);
        else if (unit == "M")  total += static_cast<std::int64_t>(amount * 60000.0);
        else if (unit == "H")  total += static_cast<std::int64_t>(amount * 3600000.0);
        else if (unit == "D")  total += static_cast<std::int64_t>(amount * 86400000.0);
        else return false;
        any = true;
    }
    if (!any) return false;
    milliseconds = total;
    return true;
}

class Lexer {
public:
    Lexer(std::string_view source, std::string section, bool dialect = false)
        : src_(source), section_(std::move(section)), dialect_(dialect) {}

    core::Result<std::vector<Token>> run() {
        std::vector<Token> out;
        for (;;) {
            skipTrivia();
            if (pos_ >= src_.size()) { out.push_back(Token{Tok::End, "", "", line_, {}}); break; }

            const char c = src_[pos_];
            const auto startLine = line_;

            if (identStart(c)) {
                const auto begin = pos_;
                while (pos_ < src_.size() && identChar(src_[pos_])) ++pos_;
                auto word = std::string(src_.substr(begin, pos_ - begin));
                const auto upper = upperOf(word);

                // 1.10 (dialecte IHM) : un litteral d'enumeration T_MODE#Auto - un nom,
                // lu par l'environnement (ni T#, ni TIME#, ni 16#, ni INT#3).
                if (dialect_ && pos_ + 1 < src_.size() && src_[pos_] == '#' && identStart(src_[pos_ + 1])
                    && upper != "T" && upper != "TIME" && typeFromName(upper) == Type::Unknown && upper != "LREAL"
                    && upper != "SINT" && upper != "USINT" && upper != "LINT" && upper != "ULINT") {
                    auto end = pos_ + 1;
                    while (end < src_.size() && identChar(src_[end])) ++end;
                    word += std::string(src_.substr(pos_, end - pos_));
                    pos_ = end;
                    out.push_back(Token{Tok::Identifier, word, word, startLine, {}});
                    continue;
                }
                // A typed literal begins with an identifier: T#, TIME#, 16#.
                if (pos_ < src_.size() && src_[pos_] == '#') {
                    ++pos_;
                    const auto bodyBegin = pos_;
                    while (pos_ < src_.size()
                           && (identChar(src_[pos_]) || src_[pos_] == '.' || src_[pos_] == '_'))
                        ++pos_;
                    const auto body = src_.substr(bodyBegin, pos_ - bodyBegin);

                    if (upper == "T" || upper == "TIME") {
                        std::int64_t ms = 0;
                        if (!parseDuration(body, ms))
                            return core::fail(core::ErrorCode::InvalidArgument,
                                              "line " + std::to_string(startLine)
                                                  + ": bad time literal '" + word + "#"
                                                  + std::string(body) + "'");
                        out.push_back(Token{Tok::TimeLiteral, "", "", startLine, Value::time(ms)});
                        continue;
                    }
                    // Based literal: 16#FF00, 2#1010, 8#777.
                    int base = 10;
                    if (std::all_of(upper.begin(), upper.end(),
                                    [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); }))
                        base = std::stoi(upper);
                    std::string digits;
                    for (char ch : body) if (ch != '_') digits.push_back(ch);
                    std::int64_t v = 0;
                    const auto* first = digits.data();
                    if (std::from_chars(first, first + digits.size(), v, base).ec != std::errc{})
                        return core::fail(core::ErrorCode::InvalidArgument,
                                          "line " + std::to_string(startLine)
                                              + ": bad based literal");
                    out.push_back(Token{Tok::Number, "", "", startLine, Value::integer(Type::DInt, v)});
                    continue;
                }

                if (upper == "TRUE" || upper == "FALSE") {
                    out.push_back(Token{Tok::Number, upper, word, startLine,
                                        Value::boolean(upper == "TRUE")});
                    continue;
                }
                const auto& kw = keywords();
                const bool isKeyword = std::find(kw.begin(), kw.end(), upper) != kw.end();
                out.push_back(Token{isKeyword ? Tok::Keyword : Tok::Identifier,
                                    isKeyword ? upper : word, word, startLine, {}});
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(c))) {
                const auto begin = pos_;
                bool isReal = false;
                while (pos_ < src_.size()
                       && (std::isdigit(static_cast<unsigned char>(src_[pos_]))
                           || src_[pos_] == '_'
                           || (src_[pos_] == '.' && pos_ + 1 < src_.size()
                               && src_[pos_ + 1] != '.')
                           || ((src_[pos_] == 'e' || src_[pos_] == 'E'))
                           || ((src_[pos_] == '+' || src_[pos_] == '-') && pos_ > begin
                               && (src_[pos_ - 1] == 'e' || src_[pos_ - 1] == 'E')))) {
                    if (src_[pos_] == '.' || src_[pos_] == 'e' || src_[pos_] == 'E') isReal = true;
                    ++pos_;
                }
                std::string digits;
                for (char ch : src_.substr(begin, pos_ - begin)) if (ch != '_') digits.push_back(ch);

                // A based literal may also be written 16#..., caught above; here
                // a '#' after digits means the same thing.
                if (pos_ < src_.size() && src_[pos_] == '#') {
                    const int base = std::stoi(digits);
                    ++pos_;
                    const auto bodyBegin = pos_;
                    while (pos_ < src_.size() && (identChar(src_[pos_]) || src_[pos_] == '_')) ++pos_;
                    std::string body;
                    for (char ch : src_.substr(bodyBegin, pos_ - bodyBegin))
                        if (ch != '_') body.push_back(ch);
                    std::int64_t v = 0;
                    const auto* first = body.data();
                    if (std::from_chars(first, first + body.size(), v, base).ec != std::errc{})
                        return core::fail(core::ErrorCode::InvalidArgument,
                                          "line " + std::to_string(startLine) + ": bad literal");
                    out.push_back(Token{Tok::Number, "", "", startLine, Value::integer(Type::DInt, v)});
                    continue;
                }
                out.push_back(Token{Tok::Number, "", digits, startLine,
                                    isReal ? Value::real(std::stod(digits))
                                           : Value::integer(Type::DInt, std::stoll(digits))});
                continue;
            }

            if (c == '\'') {
                ++pos_;
                std::string body;
                while (pos_ < src_.size() && src_[pos_] != '\'') {
                    if (src_[pos_] == '\n') ++line_;
                    // LES ECHAPPEMENTS DE LA NORME : $' pour une apostrophe, $$
                    // pour un dollar, $N $L $R $T $P, et $hh en hexadecimal.
                    // Control Expert les lit ; sans eux, un libelle comme
                    // 'Arret d$'urgence' coupait la chaine a l'apostrophe et la
                    // ligne entiere devenait illisible.
                    if (src_[pos_] == '$' && pos_ + 1 < src_.size()) {
                        const char e = src_[pos_ + 1];
                        const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(e)));
                        char esc = 0;
                        if (e == '\'' || e == '$' || e == '"') esc = e;
                        else if (up == 'N' || up == 'L') esc = '\n';
                        else if (up == 'R') esc = '\r';
                        else if (up == 'T') esc = '\t';
                        else if (up == 'P') esc = '\f';
                        if (esc != 0) {
                            body.push_back(esc);
                            pos_ += 2;
                            continue;
                        }
                        if (pos_ + 2 < src_.size()
                            && std::isxdigit(static_cast<unsigned char>(e))
                            && std::isxdigit(static_cast<unsigned char>(src_[pos_ + 2]))) {
                            body.push_back(static_cast<char>(
                                std::stoi(std::string(src_.substr(pos_ + 1, 2)), nullptr, 16)));
                            pos_ += 3;
                            continue;
                        }
                    }
                    body.push_back(src_[pos_++]);
                }
                if (pos_ < src_.size()) ++pos_;
                out.push_back(Token{Tok::StringLiteral, "", body, startLine, Value::text(body)});
                continue;
            }

            // Direct address: %MW1174, %I0.3, %Q0.1, %M100.
            if (c == '%') {
                const auto begin = pos_++;
                while (pos_ < src_.size() && (identChar(src_[pos_]) || src_[pos_] == '.')) ++pos_;
                out.push_back(Token{Tok::Identifier, std::string(src_.substr(begin, pos_ - begin)),
                                    std::string(src_.substr(begin, pos_ - begin)), startLine, {}});
                continue;
            }

            // Two-character operators first, so ':=' is not ':' then '='.
            static constexpr std::string_view kTwo[] = {":=", "=>", "<=", ">=", "<>", "**", ".."};
            bool matched = false;
            for (auto op : kTwo) {
                if (src_.compare(pos_, 2, op) == 0) {
                    out.push_back(Token{Tok::Punct, std::string(op), std::string(op), startLine, {}});
                    pos_ += 2;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
            // 1.10 (dialecte IHM) : += -= *= /= et le dereferencement p^.
            if (dialect_) {
                for (std::string_view op : {"+=", "-=", "*=", "/="}) {
                    if (src_.compare(pos_, 2, op) == 0) {
                        out.push_back(Token{Tok::Punct, std::string(op), std::string(op), startLine, {}});
                        pos_ += 2;
                        matched = true;
                        break;
                    }
                }
                if (matched) continue;
                if (c == '^') {
                    out.push_back(Token{Tok::Punct, "^", "^", startLine, {}});
                    ++pos_;
                    continue;
                }
            }

            if (std::string_view("+-*/<>=()[];,:.").find(c) != std::string_view::npos) {
                out.push_back(Token{Tok::Punct, std::string(1, c), std::string(1, c), startLine, {}});
                ++pos_;
                continue;
            }
            return core::fail(core::ErrorCode::InvalidArgument,
                              "line " + std::to_string(startLine) + ": unexpected character '"
                                  + std::string(1, c) + "'");
        }
        return out;
    }

private:
    void skipTrivia() {
        for (;;) {
            while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_]))) {
                if (src_[pos_] == '\n') ++line_;
                ++pos_;
            }
            if (src_.compare(pos_, 2, "(*") == 0) {
                const auto end = src_.find("*)", pos_ + 2);
                for (auto i = pos_; i < (end == std::string_view::npos ? src_.size() : end); ++i)
                    if (src_[i] == '\n') ++line_;
                pos_ = (end == std::string_view::npos) ? src_.size() : end + 2;
                continue;
            }
            if (src_.compare(pos_, 2, "//") == 0) {
                const auto end = src_.find('\n', pos_);
                pos_ = (end == std::string_view::npos) ? src_.size() : end;
                continue;
            }
            return;
        }
    }

    std::string_view src_;
    std::string      section_;
    std::size_t      pos_{0};
    std::uint32_t    line_{1};
    bool             dialect_{false};
};

} // namespace

// ================================================================= AST =======
struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct Expr {
    enum class Kind : std::uint8_t {
        Literal, Reference, Unary, Binary, Call, Index, Member,
        Deref, Null,          // 1.10 (dialecte IHM) : p^, NULL
    };
    Kind          kind{Kind::Literal};
    Value         literal;
    std::string   name;         // reference, call target, member name
    std::string   op;
    ExprPtr       lhs, rhs;
    // Lot 16 : le second indice d'un tableau a deux dimensions, Matrice[i, j]
    // (nul pour un seul indice). Le nom construit est alors "Matrice[2,7]".
    ExprPtr       rhs2;
    // Lot API 7 : les indices suivants (trois dimensions et plus) : Cube[i, j, k].
    std::vector<ExprPtr> more;
    std::vector<std::pair<std::string, ExprPtr>> arguments;   // name may be empty
    // `Out => target`: after the call, the named output pin is copied into the
    // target. Control Expert uses this constantly and it is not sugar - the
    // whole point of the call is often the output binding.
    std::vector<std::pair<std::string, ExprPtr>> outputs;
    std::uint32_t line{0};
};

struct Stmt;
using StmtPtr = std::shared_ptr<Stmt>;

struct CaseArm {
    std::vector<std::pair<std::int64_t, std::int64_t>> ranges;   // inclusive
    std::vector<StmtPtr> body;
    std::vector<std::string> names;   // 1.10 (dialecte IHM) : T_MODE#Auto, lus a l'execution
};

struct Stmt {
    enum class Kind : std::uint8_t {
        Assign, Call, If, Case, For, While, Repeat, Exit, Return,
        ForEach,              // 1.10 (dialecte IHM) : FOR EACH k, v IN m DO ... END_FOR
    };
    Kind          kind{Kind::Assign};
    ExprPtr       target, value, condition;
    // `%Q0.5.1 := cmd := F1;` assigns F1 to both. Control Expert allows it and
    // their code uses it, so the statement carries a list rather than one target.
    std::vector<ExprPtr> extraTargets;
    std::vector<std::pair<ExprPtr, std::vector<StmtPtr>>> branches;   // IF / ELSIF
    std::vector<StmtPtr> elseBody, body;
    std::vector<CaseArm> arms;
    std::string   loopVariable;
    ExprPtr       from, to, step;
    std::uint32_t line{0};
    // 1.10 (dialecte IHM) : FOR EACH k, v IN m (`loopValue` : v ; `from` : m) ;
    // `x += e` (`compound` : "+=") ; RETURN expr (`value`).
    std::string   loopValue;
    std::string   compound;
};

// 1.10 (dialecte IHM) : une declaration (parametre, variable locale).
struct VarDecl {
    enum class Mode : std::uint8_t { Input, InOut, Output, Local, Temp };
    std::string   name;
    TypeRef       type;
    ExprPtr       initial;
    Mode          mode{Mode::Local};
    bool          constant{false};
    std::uint32_t line{0};
};

// 1.10 (dialecte IHM) : une fonction interne d'un script (ou une fonction IHM
// du projet, ou un operateur, lus par parseFunction).
class Function {
public:
    std::string          name;
    std::vector<VarDecl> params;      // dans l'ordre : VAR_INPUT, VAR_IN_OUT, VAR_OUTPUT
    std::vector<VarDecl> locals;      // VAR, VAR_TEMP
    TypeRef              result;      // nul : pas de valeur rendue
    std::vector<StmtPtr> body;
    std::uint32_t        line{0}, endLine{0};
};

class Program {
public:
    std::string          name;
    std::vector<StmtPtr> body;
    // 1.10 : le dialecte IHM, ses fonctions internes et ses VAR de premier niveau.
    bool                                   dialect{false};
    std::vector<std::shared_ptr<Function>> functions;
    std::vector<VarDecl>                   locals;
    // Lot API 8 : le numero que le runtime donne a la section (qui a ecrit), et
    // ses lignes marquees d'un point d'arret ([ligne] != 0 ; vide : aucune).
    std::uint32_t              tag{0};
    std::vector<std::uint8_t>  breakLines;
};

// Lot API 8 : une expression seule (la condition d'un point d'arret).
class Expression {
public:
    ExprPtr root;
};

namespace {

// ============================================================== parser ======
class Parser {
public:
    Parser(std::vector<Token> tokens, std::string section, bool dialect = false)
        : tokens_(std::move(tokens)), section_(std::move(section)), dialect_(dialect) {}

    core::Result<std::shared_ptr<Program>> run() {
        auto program = std::make_shared<Program>();
        program->name = section_;
        program->dialect = dialect_;
        while (!at(Tok::End)) {
            // 1.10 (dialecte IHM) : les fonctions internes et les VAR du script,
            // au premier niveau seulement.
            if (dialect_ && atWord("FUNCTION")) {
                auto f = functionDeclaration();
                if (!f) return core::Err<core::Error>(f.error());
                for (const auto& other : program->functions)
                    if (upperOf(other->name) == upperOf((*f)->name))
                        return core::fail(core::ErrorCode::InvalidArgument,
                                          "line " + std::to_string((*f)->line) + ": function '" + (*f)->name
                                              + "' is declared twice (first at line " + std::to_string(other->line) + ")",
                                          section_);
                program->functions.push_back(*f);
                continue;
            }
            if (dialect_ && atVarBlock()) {
                auto decls = varBlock(/*inFunction*/ false);
                if (!decls) return core::Err<core::Error>(decls.error());
                for (auto& d : *decls) program->locals.push_back(std::move(d));
                continue;
            }
            auto s = statement();
            if (!s) return core::Err<core::Error>(s.error());
            if (*s) program->body.push_back(*s);
        }
        return program;
    }

    // 1.10 : un type seul (le type d'une variable de l'environnement).
    core::Result<TypeRef> soleType() {
        auto t = typeSpec();
        if (!t) return t;
        while (accept(";")) {}
        if (!at(Tok::End)) return error("expected the end of the type");
        return t;
    }

    // 1.10 : une FUNCTION seule (une fonction IHM du projet, un operateur).
    core::Result<std::shared_ptr<Function>> soleFunction() {
        while (accept(";")) {}
        if (!atWord("FUNCTION")) return error("expected FUNCTION");
        auto f = functionDeclaration();
        if (!f) return f;
        while (accept(";")) {}
        if (!at(Tok::End)) return error("expected the end of the text after END_FUNCTION");
        return f;
    }

    // Lot API 8 : une expression seule, jusqu'a la fin du texte (un ';' final
    // est tolere) - la condition d'un point d'arret.
    core::Result<ExprPtr> soleExpression() {
        if (at(Tok::End)) return error("expected a value");
        auto e = expression();
        if (!e) return e;
        while (accept(";")) {}
        if (!at(Tok::End)) return error("expected the end of the condition");
        return e;
    }

private:
    [[nodiscard]] const Token& peek(std::size_t ahead = 0) const {
        const auto i = std::min(pos_ + ahead, tokens_.size() - 1);
        return tokens_[i];
    }
    [[nodiscard]] bool at(Tok k) const { return peek().kind == k; }
    [[nodiscard]] bool atKeyword(std::string_view w) const {
        return peek().kind == Tok::Keyword && peek().text == w;
    }
    [[nodiscard]] bool atPunct(std::string_view p) const {
        return peek().kind == Tok::Punct && peek().text == p;
    }
    const Token& advance() { return tokens_[pos_ < tokens_.size() - 1 ? pos_++ : pos_]; }
    bool accept(std::string_view p) { if (atPunct(p)) { advance(); return true; } return false; }
    bool acceptKeyword(std::string_view w) { if (atKeyword(w)) { advance(); return true; } return false; }
    // 1.10 : un mot du dialecte (FUNCTION, VAR, EACH, IN, NULL...) : un
    // identifiant pour le lexer, sans casse ; ou un mot-cle (TO, OF).
    [[nodiscard]] bool atWord(std::string_view w, std::size_t ahead = 0) const {
        const auto& t = peek(ahead);
        if (t.kind == Tok::Keyword) return t.text == w;
        return t.kind == Tok::Identifier && upperOf(t.text) == w;
    }
    bool acceptWord(std::string_view w) { if (atWord(w)) { advance(); return true; } return false; }
    [[nodiscard]] bool atVarBlock() const {
        return atWord("VAR") || atWord("VAR_TEMP") || atWord("VAR_INPUT") || atWord("VAR_IN_OUT") || atWord("VAR_OUTPUT");
    }

    // 1.10.2 : la ligne d'une erreur. Ce qui manque se signale la ou il manque :
    // a la fin du texte (rien a montrer), ou quand le mot trouve a la place est
    // sur une ligne plus loin ("... attendu"), c'est la ligne du dernier mot
    // ecrit. Le client : "d" seul a la ligne 2 d'un script, "':=' ou un appel
    // attendu (fin du script atteinte)" etait mis a la ligne 3, apres le
    // dernier retour a la ligne.
    [[nodiscard]] std::uint32_t errorLine(std::string_view what) const {
        const auto here = peek().line;
        if (pos_ == 0 || pos_ > tokens_.size() - 1) return here;
        const auto before = tokens_[pos_ - 1].line;
        if (peek().kind == Tok::End) return before;
        if (what.rfind("expected ", 0) == 0 && before < here) return before;
        return here;
    }

    core::Err<core::Error> error(std::string what) const {
        const auto line = errorLine(what);
        return core::fail(core::ErrorCode::InvalidArgument,
                          "line " + std::to_string(line) + ": " + std::move(what)
                              + " (found '" + (peek().raw.empty() ? peek().text : peek().raw) + "')",
                          section_);
    }

    core::Result<StmtPtr> statement() {
        while (accept(";")) {}
        if (at(Tok::End)) return StmtPtr{};
        if (dialect_ && atWord("FUNCTION"))
            return error("FUNCTION is only allowed at the top level of the script, not inside another block");
        if (dialect_ && atVarBlock())
            return error("declarations (VAR ... END_VAR) must come before the statements");

        if (atKeyword("IF"))     return ifStatement();
        if (atKeyword("CASE"))   return caseStatement();
        if (atKeyword("FOR"))    return forStatement();
        if (atKeyword("WHILE"))  return whileStatement();
        if (atKeyword("REPEAT")) return repeatStatement();
        if (atKeyword("EXIT")) {
            auto s = std::make_shared<Stmt>();
            s->kind = Stmt::Kind::Exit;
            s->line = advance().line;
            accept(";");
            return s;
        }
        if (atKeyword("RETURN")) {
            auto s = std::make_shared<Stmt>();
            s->kind = Stmt::Kind::Return;
            s->line = advance().line;
            // 1.10 (dialecte IHM) : RETURN expr ; rend la valeur de la fonction.
            if (dialect_ && !atPunct(";") && !at(Tok::End) && peek().line == s->line && !atBlockEnd()) {
                auto v = expression();
                if (!v) return core::Err<core::Error>(v.error());
                s->value = *v;
            }
            accept(";");
            return s;
        }
        if (peek().kind == Tok::Keyword)
            return error("'" + peek().text + "' is not supported by the simulator here");

        // Either an assignment or a bare call.
        const auto line = peek().line;
        auto lhs = expression();
        if (!lhs) return core::Err<core::Error>(lhs.error());

        auto s = std::make_shared<Stmt>();
        s->line = line;
        // 1.10 (dialecte IHM) : x += e ; x -= e ; x *= e ; x /= e.
        if (dialect_ && (atPunct("+=") || atPunct("-=") || atPunct("*=") || atPunct("/="))) {
            s->compound = advance().text;
            auto rhs = expression();
            if (!rhs) return core::Err<core::Error>(rhs.error());
            s->kind   = Stmt::Kind::Assign;
            s->target = *lhs;
            s->value  = *rhs;
            accept(";");
            return s;
        }
        if (accept(":=")) {
            auto rhs = expression();
            if (!rhs) return core::Err<core::Error>(rhs.error());
            s->kind   = Stmt::Kind::Assign;
            s->target = *lhs;
            s->value  = *rhs;
            // Right-associative: everything before the last ':=' is a target.
            while (accept(":=")) {
                s->extraTargets.push_back(s->value);
                auto next = expression();
                if (!next) return core::Err<core::Error>(next.error());
                s->value = *next;
            }
        } else {
            if ((*lhs)->kind != Expr::Kind::Call)
                return error("expected ':=' or a call");
            s->kind  = Stmt::Kind::Call;
            s->value = *lhs;
        }
        accept(";");
        return s;
    }

    // A CASE arm ends where the next label begins, and a label is a number
    // followed by ':' or by a range. Without this the body of the first arm
    // swallowed every later label as if it were a statement.
    //
    // OU PAR UNE VIRGULE : "4, 5:" est une liste de labels, que Control
    // Expert accepte et que caseStatement() savait deja lire. Seul ce test
    // l'ignorait - le corps du bras precedent avalait alors "4," et le
    // parseur s'arretait sur "expected ':='".
    [[nodiscard]] bool atCaseLabel() const {
        // 1.10 (dialecte IHM) : T_MODE#Auto: et Auto: (le nom seul : le CASE porte sur une enumeration).
        if (dialect_ && peek().kind == Tok::Identifier && peek(1).kind == Tok::Punct
            && (peek(1).text == ":" || peek(1).text == ","))
            return true;
        return peek().kind == Tok::Number && peek(1).kind == Tok::Punct
            && (peek(1).text == ":" || peek(1).text == ".." || peek(1).text == ",");
    }

    core::Result<std::vector<StmtPtr>> block(std::initializer_list<std::string_view> terminators,
                                             bool stopAtCaseLabel = false) {
        std::vector<StmtPtr> out;
        for (;;) {
            // L'INSTRUCTION VIDE ";" se saute ICI, avant les tests de fin :
            // statement() la sautait lui-meme, puis lisait le label suivant
            // ("1:") comme une instruction - "0: ; 1: ..." ne se lisait pas.
            while (accept(";")) {}
            if (at(Tok::End)) return out;                  // the caller names the missing keyword
            for (auto t : terminators) if (atKeyword(t) || (dialect_ && atWord(t))) return out;
            if (stopAtCaseLabel && atCaseLabel()) return out;
            auto s = statement();
            if (!s) return core::Err<core::Error>(s.error());
            if (*s) out.push_back(*s);
            else return out;
        }
    }

    core::Result<StmtPtr> ifStatement() {
        auto s = std::make_shared<Stmt>();
        s->kind = Stmt::Kind::If;
        s->line = advance().line;                       // IF
        for (;;) {
            auto cond = expression();
            if (!cond) return core::Err<core::Error>(cond.error());
            if (!acceptKeyword("THEN")) return error("expected THEN");
            auto body = block({"ELSIF", "ELSE", "END_IF"});
            if (!body) return core::Err<core::Error>(body.error());
            s->branches.emplace_back(*cond, *body);
            if (acceptKeyword("ELSIF")) continue;
            break;
        }
        if (acceptKeyword("ELSE")) {
            auto body = block({"END_IF"});
            if (!body) return core::Err<core::Error>(body.error());
            s->elseBody = *body;
        }
        if (!acceptKeyword("END_IF")) return error("expected END_IF");
        accept(";");
        return s;
    }

    core::Result<StmtPtr> caseStatement() {
        auto s = std::make_shared<Stmt>();
        s->kind = Stmt::Kind::Case;
        s->line = advance().line;                       // CASE
        auto selector = expression();
        if (!selector) return core::Err<core::Error>(selector.error());
        s->condition = *selector;
        if (!acceptKeyword("OF")) return error("expected OF");

        while (!atKeyword("END_CASE") && !atKeyword("ELSE") && !at(Tok::End)) {
            CaseArm arm;
            for (;;) {
                if (dialect_ && peek().kind == Tok::Identifier) {
                    arm.names.push_back(advance().text);
                    if (!accept(",")) break;
                    continue;
                }
                if (peek().kind != Tok::Number) return error("expected a case label");
                const auto low = advance().literal.asInteger();
                std::int64_t high = low;
                if (accept("..")) {
                    if (peek().kind != Tok::Number) return error("expected the end of the range");
                    high = advance().literal.asInteger();
                }
                arm.ranges.emplace_back(low, high);
                if (!accept(",")) break;
            }
            if (!accept(":")) return error("expected ':' after the case labels");
            auto body = block({"END_CASE", "ELSE"}, /*stopAtCaseLabel*/ true);
            if (!body) return core::Err<core::Error>(body.error());
            arm.body = *body;
            s->arms.push_back(std::move(arm));
            // No break: a CASE has as many arms as it has label lists, and
            // stopping after the first one was why 1..3 arrived as a statement.
        }
        if (acceptKeyword("ELSE")) {
            auto body = block({"END_CASE"});
            if (!body) return core::Err<core::Error>(body.error());
            s->elseBody = *body;
        }
        if (!acceptKeyword("END_CASE")) return error("expected END_CASE");
        accept(";");
        return s;
    }

    core::Result<StmtPtr> forStatement() {
        auto s = std::make_shared<Stmt>();
        s->kind = Stmt::Kind::For;
        s->line = advance().line;                       // FOR
        // 1.10 (dialecte IHM) : FOR EACH k, v IN m DO ... END_FOR ; FOR EACH x IN t DO.
        if (dialect_ && atWord("EACH") && peek(1).kind == Tok::Identifier) {
            advance();
            s->kind = Stmt::Kind::ForEach;
            s->loopVariable = advance().raw;
            if (accept(",")) {
                if (peek().kind != Tok::Identifier) return error("expected the name of the value after ','");
                s->loopValue = advance().raw;
            }
            if (!acceptWord("IN")) return error("expected IN");
            auto from = expression();
            if (!from) return core::Err<core::Error>(from.error());
            s->from = *from;
            if (!acceptKeyword("DO")) return error("expected DO");
            auto body = block({"END_FOR"});
            if (!body) return core::Err<core::Error>(body.error());
            s->body = *body;
            if (!acceptKeyword("END_FOR")) return error("expected END_FOR");
            accept(";");
            return s;
        }
        if (peek().kind != Tok::Identifier) return error("expected the loop variable");
        s->loopVariable = advance().raw;
        if (!accept(":=")) return error("expected ':=' after the loop variable");
        auto from = expression();
        if (!from) return core::Err<core::Error>(from.error());
        s->from = *from;
        if (!acceptKeyword("TO")) return error("expected TO");
        auto to = expression();
        if (!to) return core::Err<core::Error>(to.error());
        s->to = *to;
        if (acceptKeyword("BY")) {
            auto step = expression();
            if (!step) return core::Err<core::Error>(step.error());
            s->step = *step;
        }
        if (!acceptKeyword("DO")) return error("expected DO");
        auto body = block({"END_FOR"});
        if (!body) return core::Err<core::Error>(body.error());
        s->body = *body;
        if (!acceptKeyword("END_FOR")) return error("expected END_FOR");
        accept(";");
        return s;
    }

    core::Result<StmtPtr> whileStatement() {
        auto s = std::make_shared<Stmt>();
        s->kind = Stmt::Kind::While;
        s->line = advance().line;
        auto cond = expression();
        if (!cond) return core::Err<core::Error>(cond.error());
        s->condition = *cond;
        if (!acceptKeyword("DO")) return error("expected DO");
        auto body = block({"END_WHILE"});
        if (!body) return core::Err<core::Error>(body.error());
        s->body = *body;
        if (!acceptKeyword("END_WHILE")) return error("expected END_WHILE");
        accept(";");
        return s;
    }

    core::Result<StmtPtr> repeatStatement() {
        auto s = std::make_shared<Stmt>();
        s->kind = Stmt::Kind::Repeat;
        s->line = advance().line;
        auto body = block({"UNTIL"});
        if (!body) return core::Err<core::Error>(body.error());
        s->body = *body;
        if (!acceptKeyword("UNTIL")) return error("expected UNTIL");
        auto cond = expression();
        if (!cond) return core::Err<core::Error>(cond.error());
        s->condition = *cond;
        if (!acceptKeyword("END_REPEAT")) return error("expected END_REPEAT");
        accept(";");
        return s;
    }

    // --- expressions, lowest precedence first ------------------------------
    core::Result<ExprPtr> expression() { return orExpr(); }

    core::Result<ExprPtr> binaryLevel(
        const std::vector<std::string_view>& ops,
        core::Result<ExprPtr> (Parser::*next)()) {
        auto lhs = (this->*next)();
        if (!lhs) return lhs;
        for (;;) {
            std::string matched;
            for (auto op : ops) {
                if ((peek().kind == Tok::Keyword || peek().kind == Tok::Punct)
                    && peek().text == op) { matched = std::string(op); break; }
            }
            if (matched.empty()) return lhs;
            const auto line = advance().line;
            auto rhs = (this->*next)();
            if (!rhs) return rhs;
            auto node = std::make_shared<Expr>();
            node->kind = Expr::Kind::Binary;
            node->op   = matched;
            node->lhs  = *lhs;
            node->rhs  = *rhs;
            node->line = line;
            lhs = node;
        }
    }

    core::Result<ExprPtr> orExpr()      { return binaryLevel({"OR"}, &Parser::xorExpr); }
    core::Result<ExprPtr> xorExpr()     { return binaryLevel({"XOR"}, &Parser::andExpr); }
    core::Result<ExprPtr> andExpr()     { return binaryLevel({"AND"}, &Parser::compareExpr); }
    core::Result<ExprPtr> compareExpr() {
        return binaryLevel({"=", "<>", "<", ">", "<=", ">="}, &Parser::addExpr);
    }
    core::Result<ExprPtr> addExpr()     { return binaryLevel({"+", "-"}, &Parser::mulExpr); }
    core::Result<ExprPtr> mulExpr()     { return binaryLevel({"*", "/", "MOD"}, &Parser::powerExpr); }
    core::Result<ExprPtr> powerExpr()   { return binaryLevel({"**"}, &Parser::unaryExpr); }

    core::Result<ExprPtr> unaryExpr() {
        if (atKeyword("NOT") || atPunct("-") || atPunct("+")) {
            auto node = std::make_shared<Expr>();
            node->kind = Expr::Kind::Unary;
            node->op   = peek().text;
            node->line = advance().line;
            auto operand = unaryExpr();
            if (!operand) return operand;
            node->lhs = *operand;
            return node;
        }
        return postfixExpr();
    }

    core::Result<ExprPtr> postfixExpr() {
        auto base = primaryExpr();
        if (!base) return base;
        for (;;) {
            // 1.10 (dialecte IHM) : p^ (la cible d'une reference ou d'un pointeur).
            if (dialect_ && atPunct("^")) {
                advance();
                auto node = std::make_shared<Expr>();
                node->kind = Expr::Kind::Deref;
                node->lhs  = *base;
                node->line = (*base)->line;
                base = node;
                continue;
            }
            if (accept("[")) {
                auto index = expression();
                if (!index) return index;
                ExprPtr second;
                std::vector<ExprPtr> more;
                if (accept(",")) {
                    auto j = expression();
                    if (!j) return j;
                    second = *j;
                    while (accept(",")) {
                        auto k = expression();
                        if (!k) return k;
                        more.push_back(*k);
                    }
                }
                if (!accept("]")) return error("expected ']'");
                auto node = std::make_shared<Expr>();
                node->kind = Expr::Kind::Index;
                node->lhs  = *base;
                node->rhs  = *index;
                node->rhs2 = second;
                node->more = std::move(more);
                node->line = (*base)->line;
                base = node;
                continue;
            }
            // A member is either a field name or a bit number: Control Expert
            // writes %MW0[i].0 for bit 0 of a word, and their code does exactly
            // that. Both become a member whose name is the text after the dot.
            if (atPunct(".") && (peek(1).kind == Tok::Identifier || peek(1).kind == Tok::Number)) {
                advance();
                auto node = std::make_shared<Expr>();
                node->kind = Expr::Kind::Member;
                node->lhs  = *base;
                const auto& member = advance();
                node->name = member.kind == Tok::Number
                                 ? std::to_string(member.literal.asInteger())
                                 : member.raw;
                node->line = (*base)->line;
                base = node;
                continue;
            }
            if (atPunct("(")) {
                auto node = std::make_shared<Expr>();
                node->kind = Expr::Kind::Call;
                node->lhs  = *base;
                node->line = peek().line;
                advance();
                if (!atPunct(")")) {
                    for (;;) {
                        std::string argName;
                        // A named argument: IN := x. Two tokens of lookahead
                        // distinguish it from an expression that starts with a
                        // name.
                        const bool isOutput = peek().kind == Tok::Identifier
                                           && peek(1).kind == Tok::Punct && peek(1).text == "=>";
                        if (peek().kind == Tok::Identifier && peek(1).kind == Tok::Punct
                            && (peek(1).text == ":=" || peek(1).text == "=>")) {
                            argName = advance().raw;
                            advance();
                        }
                        auto arg = expression();
                        if (!arg) return arg;
                        if (isOutput) node->outputs.emplace_back(argName, *arg);
                        else          node->arguments.emplace_back(argName, *arg);
                        if (!accept(",")) break;
                    }
                }
                if (!accept(")")) return error("expected ')'");
                base = node;
                continue;
            }
            return base;
        }
    }

    core::Result<ExprPtr> primaryExpr() {
        const auto& t = peek();
        if (t.kind == Tok::Number || t.kind == Tok::TimeLiteral || t.kind == Tok::StringLiteral) {
            auto node = std::make_shared<Expr>();
            node->kind    = Expr::Kind::Literal;
            node->literal = t.literal;
            node->line    = t.line;
            advance();
            return node;
        }
        if (dialect_ && t.kind == Tok::Identifier && upperOf(t.text) == "NULL") {
            auto node = std::make_shared<Expr>();
            node->kind = Expr::Kind::Null;
            node->line = t.line;
            advance();
            return node;
        }
        if (t.kind == Tok::Identifier) {
            auto node = std::make_shared<Expr>();
            node->kind = Expr::Kind::Reference;
            node->name = t.raw;
            node->line = t.line;
            advance();
            return node;
        }
        if (accept("(")) {
            auto inner = expression();
            if (!inner) return inner;
            if (!accept(")")) return error("expected ')'");
            return inner;
        }
        return error("expected a value");
    }

    // ---- 1.10 : le dialecte IHM ---------------------------------------------
    // La fin d'un bloc (RETURN seul en fin de branche : "RETURN END_IF").
    [[nodiscard]] bool atBlockEnd() const {
        for (std::string_view w : {"END_IF", "ELSIF", "ELSE", "END_CASE", "END_FOR", "END_WHILE", "UNTIL", "END_REPEAT", "END_FUNCTION"})
            if (atWord(w)) return true;
        return false;
    }

    // Une borne constante de tableau : -3, 10, 2*5, 16#FF (sans nom).
    core::Result<std::int64_t> constantBound() {
        auto e = addExpr();
        if (!e) return core::Err<core::Error>(e.error());
        std::int64_t v = 0;
        if (!constantValue(**e, v)) return error("expected a constant array bound");
        return v;
    }
    static bool constantValue(const Expr& e, std::int64_t& out) {
        switch (e.kind) {
            case Expr::Kind::Literal:
                if (!isInteger(e.literal.type())) return false;
                out = e.literal.asInteger();
                return true;
            case Expr::Kind::Unary: {
                std::int64_t v = 0;
                if (!e.lhs || !constantValue(*e.lhs, v)) return false;
                if (e.op == "-") v = -v;
                else if (e.op != "+") return false;
                out = v;
                return true;
            }
            case Expr::Kind::Binary: {
                std::int64_t a = 0, b = 0;
                if (!e.lhs || !e.rhs || !constantValue(*e.lhs, a) || !constantValue(*e.rhs, b)) return false;
                if (e.op == "+") out = a + b;
                else if (e.op == "-") out = a - b;
                else if (e.op == "*") out = a * b;
                else if (e.op == "/" && b != 0) out = a / b;
                else return false;
                return true;
            }
            default: return false;
        }
    }

    // Un type : REAL, T_FOUR, STRING[20], ARRAY[0..3, 0..9] OF REAL,
    // MAP[STRING] OF T, REF_TO T, REFERENCE TO T, POINTER TO T, MAP_ITERATOR.
    core::Result<TypeRef> typeSpec() {
        if (acceptWord("ARRAY")) {
            if (!accept("[")) return error("expected '[' after ARRAY");
            auto t = std::make_shared<TypeDesc>();
            t->kind = TypeDesc::Kind::Array;
            for (;;) {
                auto lo = constantBound();
                if (!lo) return core::Err<core::Error>(lo.error());
                if (!accept("..")) return error("expected '..' in the array bounds");
                auto hi = constantBound();
                if (!hi) return core::Err<core::Error>(hi.error());
                if (*hi < *lo) return error("the upper bound is below the lower bound");
                t->bounds.emplace_back(*lo, *hi);
                if (!accept(",")) break;
            }
            if (!accept("]")) return error("expected ']' after the array bounds");
            if (!acceptKeyword("OF")) return error("expected OF after the array bounds");
            auto e = typeSpec();
            if (!e) return e;
            // ARRAY[..] OF ARRAY[..] OF X : un tableau de tableaux garde ses deux etages.
            t->element = *e;
            std::int64_t cells = t->count();
            if (cells > 1000000) return error("the array is too large (more than 1000000 cells)");
            return TypeRef(t);
        }
        if (acceptWord("MAP")) {
            if (!accept("[")) return error("expected '[' after MAP");
            auto k = typeSpec();
            if (!k) return k;
            // 1.10 (decision 15) : MAP[T_MODE] OF ... - une cle d'enumeration est un DINT.
            const bool enumKey = (*k)->kind == TypeDesc::Kind::Struct;
            if (!enumKey && ((*k)->kind != TypeDesc::Kind::Scalar || ((*k)->scalar != Type::String && !isInteger((*k)->scalar))))
                return error("a MAP key is a STRING or an integer");
            if (!accept("]")) return error("expected ']' after the key type");
            if (!acceptKeyword("OF")) return error("expected OF after MAP[...]");
            auto e = typeSpec();
            if (!e) return e;
            auto t = std::make_shared<TypeDesc>();
            t->kind = TypeDesc::Kind::Map;
            t->key = enumKey ? Type::DInt : (*k)->scalar;
            if (enumKey) t->name = (*k)->name;          // la cle garde le nom de son enumeration (TO_STRING(k))
            t->element = *e;
            return TypeRef(t);
        }
        if (acceptWord("REF_TO") || (atWord("REFERENCE") && atWord("TO", 1) && (advance(), advance(), true))) {
            auto e = typeSpec();
            if (!e) return e;
            auto t = std::make_shared<TypeDesc>();
            t->kind = TypeDesc::Kind::Ref;
            t->element = *e;
            return TypeRef(t);
        }
        if (atWord("POINTER") && atWord("TO", 1)) {
            advance();
            advance();
            auto e = typeSpec();
            if (!e) return e;
            auto t = std::make_shared<TypeDesc>();
            t->kind = TypeDesc::Kind::Pointer;
            t->element = *e;
            return TypeRef(t);
        }
        if (acceptWord("MAP_ITERATOR") || acceptWord("ITERATOR")) {
            auto t = std::make_shared<TypeDesc>();
            t->kind = TypeDesc::Kind::Iterator;
            return TypeRef(t);
        }
        if (peek().kind != Tok::Identifier) return error("expected a type");
        const std::string name = advance().raw;
        const std::string up = upperOf(name);
        // STRING[20] : la longueur est acceptee (et ignoree).
        if (up == "STRING" && accept("[")) {
            if (peek().kind == Tok::Number) advance();
            if (!accept("]")) return error("expected ']' after the string length");
        }
        Type scalar = typeFromName(up);
        if (up == "LREAL") scalar = Type::Real;
        else if (up == "SINT" || up == "LINT") scalar = up == "SINT" ? Type::Int : Type::DInt;
        else if (up == "USINT" || up == "ULINT") scalar = up == "USINT" ? Type::UInt : Type::UDInt;
        else if (up == "EBOOL") scalar = Type::Bool;
        if (scalar != Type::Unknown) return scalarType(scalar, up);
        auto t = std::make_shared<TypeDesc>();
        t->kind = TypeDesc::Kind::Struct;           // resolue a l'execution (Environment::structMembers)
        t->name = name;
        return TypeRef(t);
    }

    // "a, b : T := init" (sans le ';').
    core::Result<std::vector<VarDecl>> declarationGroup(VarDecl::Mode mode, bool constant) {
        std::vector<VarDecl> out;
        for (;;) {
            if (peek().kind != Tok::Identifier) return error("expected a variable name");
            VarDecl d;
            d.line = peek().line;
            d.name = advance().raw;
            d.mode = mode;
            d.constant = constant;
            out.push_back(std::move(d));
            if (!accept(",")) break;
        }
        if (!accept(":")) return error("expected ':' and a type after the name");
        auto t = typeSpec();
        if (!t) return core::Err<core::Error>(t.error());
        ExprPtr initial;
        if (accept(":=")) {
            auto e = expression();
            if (!e) return core::Err<core::Error>(e.error());
            initial = *e;
        }
        for (auto& d : out) {
            d.type = *t;
            d.initial = initial;
        }
        return out;
    }

    // VAR [CONSTANT|RETAIN] a : T; ... END_VAR (et VAR_TEMP, VAR_INPUT, VAR_IN_OUT, VAR_OUTPUT).
    core::Result<std::vector<VarDecl>> varBlock(bool inFunction) {
        const std::string word = upperOf(peek().text);
        const auto line = advance().line;
        VarDecl::Mode mode = word == "VAR" ? VarDecl::Mode::Local
                           : word == "VAR_TEMP" ? VarDecl::Mode::Temp
                           : word == "VAR_INPUT" ? VarDecl::Mode::Input
                           : word == "VAR_IN_OUT" ? VarDecl::Mode::InOut : VarDecl::Mode::Output;
        if (!inFunction && mode != VarDecl::Mode::Local && mode != VarDecl::Mode::Temp)
            return core::fail(core::ErrorCode::InvalidArgument,
                              "line " + std::to_string(line) + ": " + word + " is only allowed in a function", section_);
        bool constant = false;
        for (;;) {
            if (acceptWord("CONSTANT")) { constant = true; continue; }
            if (acceptWord("RETAIN")) continue;
            break;
        }
        std::vector<VarDecl> out;
        while (!atWord("END_VAR")) {
            if (at(Tok::End)) return core::fail(core::ErrorCode::InvalidArgument,
                                                "line " + std::to_string(line) + ": " + word + " without END_VAR", section_);
            auto group = declarationGroup(mode, constant);
            if (!group) return core::Err<core::Error>(group.error());
            for (auto& d : *group) out.push_back(std::move(d));
            if (!accept(";")) return error("expected ';' after the declaration");
        }
        advance();                                      // END_VAR
        accept(";");
        return out;
    }

    // FUNCTION Nom [(a : T; VAR_IN_OUT b : U, c : V)] [: R] {VAR ...} instructions END_FUNCTION
    core::Result<std::shared_ptr<Function>> functionDeclaration() {
        auto f = std::make_shared<Function>();
        f->line = advance().line;                       // FUNCTION
        if (peek().kind != Tok::Identifier) return error("expected the function name");
        f->name = advance().raw;
        const auto addParam = [&](VarDecl d) -> core::Result<bool> {
            for (const auto& p : f->params)
                if (upperOf(p.name) == upperOf(d.name))
                    return core::fail(core::ErrorCode::InvalidArgument,
                                      "line " + std::to_string(d.line) + ": parameter '" + d.name + "' is declared twice", section_);
            f->params.push_back(std::move(d));
            return true;
        };
        if (accept("(")) {
            while (!atPunct(")")) {
                VarDecl::Mode mode = VarDecl::Mode::Input;
                if (acceptWord("VAR_IN_OUT")) mode = VarDecl::Mode::InOut;
                else if (acceptWord("VAR_OUTPUT")) mode = VarDecl::Mode::Output;
                else if (acceptWord("VAR_INPUT")) mode = VarDecl::Mode::Input;
                const bool constant = acceptWord("CONSTANT");
                auto group = declarationGroup(mode, constant);
                if (!group) return core::Err<core::Error>(group.error());
                for (auto& d : *group) {
                    auto ok = addParam(std::move(d));
                    if (!ok) return core::Err<core::Error>(ok.error());
                }
                if (accept(";") || accept(",")) continue;
                if (!atPunct(")")) return error("expected ')' or ';' after the parameter");
            }
            advance();                                  // )
        }
        if (accept(":")) {
            auto t = typeSpec();
            if (!t) return core::Err<core::Error>(t.error());
            f->result = *t;
        }
        accept(";");
        while (atVarBlock()) {
            auto decls = varBlock(/*inFunction*/ true);
            if (!decls) return core::Err<core::Error>(decls.error());
            for (auto& d : *decls) {
                if (upperOf(d.name) == upperOf(f->name))
                    return core::fail(core::ErrorCode::InvalidArgument,
                                      "line " + std::to_string(d.line) + ": a variable cannot have the name of its function ('"
                                          + d.name + "')", section_);
                if (d.mode == VarDecl::Mode::Local || d.mode == VarDecl::Mode::Temp) {
                    for (const auto& o : f->locals)
                        if (upperOf(o.name) == upperOf(d.name))
                            return core::fail(core::ErrorCode::InvalidArgument,
                                              "line " + std::to_string(d.line) + ": variable '" + d.name + "' is declared twice", section_);
                    for (const auto& o : f->params)
                        if (upperOf(o.name) == upperOf(d.name))
                            return core::fail(core::ErrorCode::InvalidArgument,
                                              "line " + std::to_string(d.line) + ": variable '" + d.name + "' is already a parameter", section_);
                    f->locals.push_back(std::move(d));
                } else {
                    auto ok = addParam(std::move(d));
                    if (!ok) return core::Err<core::Error>(ok.error());
                }
            }
        }
        auto body = block({"END_FUNCTION"});
        if (!body) return core::Err<core::Error>(body.error());
        f->body = *body;
        if (!atWord("END_FUNCTION")) return error("expected END_FUNCTION");
        f->endLine = advance().line;
        accept(";");
        return f;
    }

    std::vector<Token> tokens_;
    std::string        section_;
    std::size_t        pos_{0};
    bool               dialect_{false};
};

} // namespace

// ============================================================ execution =====
namespace {

// 1.10 : un type ecrit ("ARRAY[0..9] OF REAL", "T_FOUR"), lu par le parseur du dialecte.
core::Result<TypeRef> parseTypeText(std::string_view text);

struct Flow { enum class Kind : std::uint8_t { Normal, Exit, Return, Aborted } kind{Kind::Normal}; };

// Lot API 8 : les noms d'une ligne (point d'arret) - une expression, ou le nom
// d'une variable de boucle (FOR i := ...), dans l'ordre ou le code les ecrit.
using LineName = std::pair<const Expr*, std::string>;

void collectExpr(const ExprPtr& e, std::uint32_t line, std::vector<LineName>& out, bool callee = false) {
    if (!e) return;
    switch (e->kind) {
        case Expr::Kind::Literal: return;
        case Expr::Kind::Null: return;
        case Expr::Kind::Deref: collectExpr(e->lhs, line, out); return;
        case Expr::Kind::Unary: collectExpr(e->lhs, line, out); return;
        case Expr::Kind::Binary:
            collectExpr(e->lhs, line, out);
            collectExpr(e->rhs, line, out);
            return;
        case Expr::Kind::Call:
            // Ce qu'on appelle n'a pas de valeur a lui (une fonction, une
            // instance) ; ses indices et ses arguments, si.
            collectExpr(e->lhs, line, out, /*callee*/ true);
            for (const auto& a : e->arguments) collectExpr(a.second, line, out);
            for (const auto& o : e->outputs) collectExpr(o.second, line, out);
            return;
        case Expr::Kind::Reference:
        case Expr::Kind::Member:
        case Expr::Kind::Index:
            if (!callee && e->line == line) out.emplace_back(e.get(), std::string{});
            // Les indices de la chaine : Armoires[i].etat lit aussi i.
            for (const Expr* x = e.get(); x;) {
                if (x->kind == Expr::Kind::Index) {
                    collectExpr(x->rhs, line, out);
                    collectExpr(x->rhs2, line, out);
                    for (const auto& m : x->more) collectExpr(m, line, out);
                } else if (x->kind == Expr::Kind::Call) {
                    for (const auto& a : x->arguments) collectExpr(a.second, line, out);
                    break;
                } else if (x->kind != Expr::Kind::Member) {
                    break;
                }
                x = x->lhs.get();
            }
            return;
    }
}

void collectStmts(const std::vector<StmtPtr>& body, std::uint32_t line, std::vector<LineName>& out);

void collectStmt(const Stmt& s, std::uint32_t line, std::vector<LineName>& out) {
    if (s.kind == Stmt::Kind::For && s.line == line) out.emplace_back(nullptr, s.loopVariable);
    collectExpr(s.target, line, out);
    for (const auto& t : s.extraTargets) collectExpr(t, line, out);
    collectExpr(s.value, line, out);
    collectExpr(s.condition, line, out);
    collectExpr(s.from, line, out);
    collectExpr(s.to, line, out);
    collectExpr(s.step, line, out);
    for (const auto& [cond, body] : s.branches) {
        collectExpr(cond, line, out);
        collectStmts(body, line, out);
    }
    for (const auto& arm : s.arms) collectStmts(arm.body, line, out);
    collectStmts(s.body, line, out);
    collectStmts(s.elseBody, line, out);
}

void collectStmts(const std::vector<StmtPtr>& body, std::uint32_t line, std::vector<LineName>& out) {
    for (const auto& s : body)
        if (s) collectStmt(*s, line, out);
}

void statementLinesOf(const std::vector<StmtPtr>& body, std::vector<std::uint32_t>& out) {
    for (const auto& s : body) {
        if (!s) continue;
        out.push_back(s->line);
        for (const auto& branch : s->branches) statementLinesOf(branch.second, out);
        for (const auto& arm : s->arms) statementLinesOf(arm.body, out);
        statementLinesOf(s->body, out);
        statementLinesOf(s->elseBody, out);
    }
}

// Lot API 8 : la section en cours pour la trace, rendue a l'appelant en sortant
// (meme sur une sortie anticipee : RETURN, halte).
class TraceScope {
public:
    TraceScope(ExecTrace* trace, std::uint32_t tag) : trace_(trace) {
        if (!trace_) return;
        saved_ = *trace_;
        trace_->tag = tag;
    }
    ~TraceScope() { if (trace_) *trace_ = saved_; }
    TraceScope(const TraceScope&) = delete;
    TraceScope& operator=(const TraceScope&) = delete;
private:
    ExecTrace* trace_;
    ExecTrace  saved_{};
};

const std::vector<std::uint8_t>& noBreakLines() {
    static const std::vector<std::uint8_t> none;
    return none;
}

class Runner {
public:
    Runner(Environment& env, const RunLimits& limits, std::string section)
        : env_(env), limits_(limits), section_(std::move(section)) {}

    RunResult run(const Program& program) {
        TraceScope traced(limits_.trace, program.tag);      // lot API 8
        program_ = &program;
        breaks_ = &program.breakLines;
        // 1.10 : le dialecte IHM - le cadre du script (ses VAR, ses fonctions).
        if (program.dialect && !startDialect(program)) return {statements_, false};
        std::uint32_t context = 0;
        for (const auto& s : program.body) {
            const auto flow = statement(*s, context);
            context = s->line;
            if (flow.kind == Flow::Kind::Aborted) return {statements_, false};
            if (flow.kind == Flow::Kind::Return)  return {statements_, true, true};
        }
        return {statements_, true};
    }

    // Lot API 8 : une expression, sans rien ecrire ni rien dire (la condition
    // d'un point d'arret) : une erreur revient dans le resultat.
    core::Result<Value> evaluateQuietly(const Expr& e) {
        const bool was = probing_;
        probing_ = true;
        auto v = evaluate(e);
        probing_ = was;
        return v;
    }

    // Lot API 8 : les variables de la ligne, lues au passage d'un point d'arret.
    std::vector<std::pair<std::string, std::string>> valuesOnLine(std::uint32_t line) {
        std::vector<std::pair<std::string, std::string>> out;
        if (!program_) return out;
        std::vector<LineName> names;
        collectStmts(program_->body, line, names);
        const bool was = probing_;
        probing_ = true;
        for (const auto& [expr, plain] : names) {
            std::string name = plain;
            if (expr) {
                auto q = qualify(*expr);
                if (!q) continue;
                name = *q;
            }
            if (name.empty()) continue;
            if (std::any_of(out.begin(), out.end(), [&](const auto& kv) { return kv.first == name; })) continue;
            Value v;
            out.emplace_back(name, env_.read(name, v) ? v.display() : std::string{});
        }
        probing_ = was;
        return out;
    }

private:
    void fail(std::uint32_t line, std::string message) {
        // Lot API 8 : une lecture au passage (point d'arret) ne dit rien au
        // journal et n'arrete pas le cycle : l'erreur revient en valeur.
        if (probing_) return;
        // 1.10 : dans le dialecte IHM, le message seul (sans "invalid argument: ") ;
        // dans une fonction du projet, sa ligne a elle, l'appel a la ligne de l'appelant.
        if (dialect_) {
            for (std::string_view prefix : {"invalid argument: ", "not implemented: "})
                if (message.rfind(prefix, 0) == 0) message.erase(0, prefix.size());
            if (!calleeLabel_.empty()) {
                message = "fonction " + calleeLabel_ + ", ligne " + std::to_string(line) + " : " + message;
                line = calleeLine_;
            }
        }
        env_.report(Diagnostic{Diagnostic::Severity::Error, std::move(message), line, section_});
        aborted_ = true;
    }

    // Lot API 8 : une ligne marquee d'un point d'arret est atteinte.
    void reachLine(std::uint32_t line) {
        class Probe final : public LineProbe {
        public:
            Probe(Runner& runner, std::uint32_t at) : runner_(runner), at_(at) {}
            std::vector<std::pair<std::string, std::string>> lineValues() override { return runner_.valuesOnLine(at_); }
        private:
            Runner&       runner_;
            std::uint32_t at_;
        } probe(*this, line);
        env_.breakpointReached(program_ ? program_->tag : 0, line, probe);
    }

    [[nodiscard]] bool budgetExhausted(std::uint32_t line) {
        if (limits_.scanStatements) {
            constexpr std::uint64_t kSpent = std::uint64_t{1} << 62;   // le budget est epuise : tout s'arrete
            const auto n = ++*limits_.scanStatements;
            if (n > kSpent) { aborted_ = true; return true; }             // deja dit une fois
            if (limits_.maxScanStatements && n > limits_.maxScanStatements) {
                *limits_.scanStatements = kSpent + 1;
                fail(line, "the scan exceeded " + std::to_string(limits_.maxScanStatements)
                               + " statements and was stopped; a loop is probably not terminating");
                return true;
            }
            if (limits_.hasDeadline && (n & 1023u) == 0 && monotonicMicros() > limits_.deadlineMicros) {
                *limits_.scanStatements = kSpent + 1;
                fail(line, "the scan ran for more than its time limit and was stopped; a loop is probably not terminating");
                return true;
            }
        }
        if (++statements_ <= limits_.maxStatementsPerScan) return false;
        fail(line, "the scan exceeded " + std::to_string(limits_.maxStatementsPerScan)
                       + " statements and was stopped; a loop is probably not terminating");
        return true;
    }

    // --- names -------------------------------------------------------------
    // A qualified name is built textually: Motor1.start, table[3], a[2].b. The
    // environment owns the storage and decides what those mean.
    core::Result<std::string> qualify(const Expr& e) {
        switch (e.kind) {
            case Expr::Kind::Reference: return e.name;
            case Expr::Kind::Member: {
                auto base = qualify(*e.lhs);
                if (!base) return base;
                return *base + "." + e.name;
            }
            case Expr::Kind::Index: {
                auto base = qualify(*e.lhs);
                if (!base) return base;
                auto index = evaluate(*e.rhs);
                if (!index) return core::Err<core::Error>(index.error());
                if (e.rhs2) {
                    auto second = evaluate(*e.rhs2);
                    if (!second) return core::Err<core::Error>(second.error());
                    std::string cell = *base + "[" + std::to_string(index->asInteger()) + "," + std::to_string(second->asInteger());
                    for (const auto& k : e.more) {
                        auto next = evaluate(*k);
                        if (!next) return core::Err<core::Error>(next.error());
                        cell += "," + std::to_string(next->asInteger());
                    }
                    return cell + "]";
                }
                return *base + "[" + std::to_string(index->asInteger()) + "]";
            }
            default:
                return core::fail(core::ErrorCode::InvalidArgument, "not something that can be assigned");
        }
    }

    core::Result<Value> evaluate(const Expr& e) {
        // 1.10 : le dialecte IHM evalue tout par sa voie riche, puis demande un scalaire.
        if (dialect_) {
            auto r = rich(e);
            if (!r) return core::Err<core::Error>(r.error());
            return scalarOf(*r, e);
        }
        switch (e.kind) {
            case Expr::Kind::Literal: return e.literal;

            case Expr::Kind::Reference: {
                // Lot API 7 : un nom simple se lit tel quel, sans copie.
                Value out;
                if (!env_.read(e.name, out))
                    return core::fail(core::ErrorCode::InvalidArgument,
                                      "'" + e.name + "' is not declared");
                return out;
            }
            case Expr::Kind::Member:
            case Expr::Kind::Index: {
                auto name = qualify(e);
                if (!name) return core::Err<core::Error>(name.error());
                Value out;
                if (!env_.read(*name, out))
                    return core::fail(core::ErrorCode::InvalidArgument,
                                      "'" + *name + "' is not declared");
                return out;
            }

            case Expr::Kind::Unary: {
                auto operand = evaluate(*e.lhs);
                if (!operand) return operand;
                if (e.op == "NOT") {
                    if (operand->type() == Type::Bool) return Value::boolean(!operand->isTruthy());
                    return Value::integer(operand->type(), ~operand->asInteger());
                }
                if (e.op == "-") {
                    if (operand->type() == Type::Real) return Value::real(-operand->asReal());
                    return Value::integer(operand->type(), -operand->asInteger());
                }
                return operand;
            }

            case Expr::Kind::Binary:  return binary(e);
            case Expr::Kind::Call:    return call(e);
            case Expr::Kind::Deref:
            case Expr::Kind::Null:    break;      // 1.10 : le dialecte seulement
        }
        return core::fail(core::ErrorCode::NotImplemented, "unsupported expression");
    }

    core::Result<Value> binary(const Expr& e) {
        auto lhs = evaluate(*e.lhs);
        if (!lhs) return lhs;

        // Short-circuit, as ST does: the right side of an AND whose left is
        // false must not be evaluated, or a guarded array access explodes.
        if (e.op == "AND" && !lhs->isTruthy()) return Value::boolean(false);
        if (e.op == "OR"  &&  lhs->isTruthy() && lhs->type() == Type::Bool)
            return Value::boolean(true);

        auto rhs = evaluate(*e.rhs);
        if (!rhs) return rhs;
        return combine(e, *lhs, *rhs);
    }

    // Les deux operandes evalues : le calcul (1.10 : partage avec le dialecte IHM).
    core::Result<Value> combine(const Expr& e, const Value& a, const Value& b) {
        if (e.op == "=")  return Value::boolean(a.compare(b) == 0);
        if (e.op == "<>") return Value::boolean(a.compare(b) != 0);
        if (e.op == "<")  return Value::boolean(a.compare(b) < 0);
        if (e.op == ">")  return Value::boolean(a.compare(b) > 0);
        if (e.op == "<=") return Value::boolean(a.compare(b) <= 0);
        if (e.op == ">=") return Value::boolean(a.compare(b) >= 0);

        if (e.op == "AND" || e.op == "OR" || e.op == "XOR") {
            if (a.type() == Type::Bool && b.type() == Type::Bool) {
                const bool x = a.isTruthy(), y = b.isTruthy();
                return Value::boolean(e.op == "AND" ? (x && y) : e.op == "OR" ? (x || y) : (x != y));
            }
            const auto x = a.asInteger(), y = b.asInteger();
            const auto t = bitWidth(a.type()) >= bitWidth(b.type()) ? a.type() : b.type();
            return Value::integer(t, e.op == "AND" ? (x & y) : e.op == "OR" ? (x | y) : (x ^ y));
        }

        if (a.type() == Type::String || b.type() == Type::String) {
            if (e.op == "+") return Value::text(a.asString() + b.asString());
            return core::fail(core::ErrorCode::InvalidArgument, "'" + e.op + "' on a string");
        }

        const bool useReal = a.type() == Type::Real || b.type() == Type::Real;
        if (useReal) {
            const double x = a.asReal(), y = b.asReal();
            if (e.op == "+") return Value::real(x + y);
            if (e.op == "-") return Value::real(x - y);
            if (e.op == "*") return Value::real(x * y);
            if (e.op == "**") return Value::real(std::pow(x, y));
            if (e.op == "/") {
                if (y == 0.0) {
                    fail(e.line, "division by zero");
                    return core::fail(core::ErrorCode::InvalidArgument, "division by zero");
                }
                return Value::real(x / y);
            }
        }

        // The wider of the two operands decides the result width, and the result
        // wraps in it. That is the PLC's arithmetic, not C's.
        const auto t = bitWidth(a.type()) >= bitWidth(b.type()) ? a.type() : b.type();
        const auto x = a.asInteger(), y = b.asInteger();
        if (e.op == "+") return Value::integer(t, x + y);
        if (e.op == "-") return Value::integer(t, x - y);
        if (e.op == "*") return Value::integer(t, x * y);
        if (e.op == "**") return Value::integer(t, static_cast<std::int64_t>(std::pow(
                                                     static_cast<double>(x), static_cast<double>(y))));
        if (e.op == "/" || e.op == "MOD") {
            if (y == 0) {
                // The thing the user asked the simulator to catch. Reported with
                // its line, and the scan stops rather than producing a number
                // that never existed.
                fail(e.line, e.op == "MOD" ? "modulo by zero" : "division by zero");
                return core::fail(core::ErrorCode::InvalidArgument, "division by zero");
            }
            return Value::integer(t, e.op == "/" ? x / y : x % y);
        }
        return core::fail(core::ErrorCode::NotImplemented, "operator '" + e.op + "'");
    }

    core::Result<Value> call(const Expr& e) {
        // The callee is either a plain name (a function) or a member/reference
        // naming a function-block instance.
        auto target = qualify(*e.lhs);
        if (!target) return core::Err<core::Error>(target.error());

        // 1.10.2 (SIM) : ASCII_TO_STRING(tableau) - le tableau n'a pas de valeur a
        // lui : son NOM est passe a l'environnement, a la place de l'instance
        // (voir Runtime::call). Une seule case (un INT) suit le chemin ordinaire.
        if (e.arguments.size() == 1 && e.outputs.empty() && e.arguments.front().second
            && upperOf(*target) == "ASCII_TO_STRING") {
            const auto& expr = *e.arguments.front().second;
            if (expr.kind == Expr::Kind::Reference || expr.kind == Expr::Kind::Member
                || expr.kind == Expr::Kind::Index) {
                if (auto from = qualify(expr); from && !env_.exists(*from) && *from != *target) {
                    Value text;
                    if (env_.call(*target, *from, {}, text)) return text;
                }
            }
        }

        std::vector<std::pair<std::string, Value>> arguments;
        arguments.reserve(e.arguments.size());
        // Ce qui a ete passe PAR UNE VARIABLE, pour pouvoir le rendre apres
        // l'appel : voir "LE RETOUR DES InOut" plus bas.
        struct Passed { std::string pin; std::string source; bool aggregate; Value value; };
        std::vector<Passed> passed;
        for (const auto& [name, expr] : e.arguments) {
            const bool variable = !name.empty()
                && (expr->kind == Expr::Kind::Reference || expr->kind == Expr::Kind::Member
                    || expr->kind == Expr::Kind::Index);
            // A whole structure passed as an argument - ECRITURE_MEMOIRE(config
            // := ConfigsGaz[i]) - has no single value to evaluate. It is copied
            // into the pin member by member instead, which is what the PLC does.
            if (variable) {
                auto from = qualify(*expr);
                // Lot API 8 : une lecture au passage n'ecrit rien - pas meme les
                // broches d'une instance.
                if (probing_ && from && !env_.exists(*from))
                    return core::fail(core::ErrorCode::NotImplemented, "an aggregate argument while probing");
                if (from && !env_.exists(*from)
                    && env_.assignAggregate(*target + "." + name, *from)) {
                    passed.push_back({name, *from, true, {}});
                    continue;
                }
            }
            auto v = evaluate(*expr);
            if (!v) return v;
            if (variable)
                if (auto from = qualify(*expr)) passed.push_back({name, *from, false, *v});
            arguments.emplace_back(name, *v);
        }

        Value result;
        // An instance call and a function call look identical here; the
        // environment knows which names are instances.
        if (!env_.call(*target, *target, arguments, result)) {
            // Lot API 7 : l'environnement peut choisir de continuer - l'appel
            // rend 0, rien n'est rendu aux variables passees, le cycle va au bout.
            if (env_.tolerateUnknownCall(*target, e.line)) return Value::integer(Type::DInt, 0);
            fail(e.line, "'" + *target + "' is not a function or a block this simulator knows");
            return core::fail(core::ErrorCode::NotImplemented, *target);
        }
        // Lot API 8 : une lecture au passage ne rend rien aux variables passees.
        if (probing_) return result;

        // LE RETOUR DES InOut. Un parametre InOut n'est pas une copie : le bloc
        // travaille SUR la variable de l'appelant. Le simulateur copiait
        // l'argument dans l'instance et s'arretait la - si bien que
        // `PMP(Eq := Pompes)` calculait la pompe dans sa copie et que
        // `Pompes[0].Run` ne bougeait jamais, et qu'un `Current := Mode` repartait
        // a chaque cycle de la valeur perimee de l'appelant.
        //
        // Ce qui a ete passe par une variable est donc rendu apres l'appel. Une
        // ENTREE n'a pas pu etre modifiee par le bloc (Control Expert l'interdit),
        // elle reviendrait identique : pour un scalaire on ne rend que ce qui a
        // CHANGE, ce qui evite aussi qu'une meme variable passee a deux broches
        // soit ecrasee par la broche qui ne l'a pas touchee. Un agregat se
        // recopie en entier - le comparer couterait autant que le copier.
        // Une ecriture refusee (constante, variable forcee) est ignoree : le
        // forcage gagne, comme sur l'automate.
        for (const auto& p : passed) {
            const auto pin = *target + "." + p.pin;
            if (p.aggregate) {
                (void)env_.assignAggregate(p.source, pin);
                continue;
            }
            Value now;
            if (!env_.read(pin, now)) continue;
            if (now.type() == p.value.type() && now.compare(p.value) == 0) continue;
            (void)env_.write(p.source, now);
        }

        // Output bindings are performed after the call, reading the pin the
        // block just wrote.
        for (const auto& [pin, lvalue] : e.outputs) {
            Value pinValue;
            if (!env_.read(*target + "." + pin, pinValue)) {
                // Lot API 7 : une sortie TABLEAU ou STRUCTURE (Out => Mat, un
                // ARRAY[0..15] OF BOOL) n'a pas de valeur a elle : elle se recopie
                // case par case, comme une affectation d'agregat.
                if (auto dest = qualify(*lvalue); dest && env_.assignAggregate(*dest, *target + "." + pin)) continue;
                fail(e.line, "'" + *target + "' has no output called '" + pin + "'");
                return core::fail(core::ErrorCode::InvalidArgument, pin);
            }
            auto name = qualify(*lvalue);
            if (!name) return core::Err<core::Error>(name.error());
            if (!env_.write(*name, pinValue)) {
                fail(e.line, "'" + *name + "' cannot be written");
                return core::fail(core::ErrorCode::InvalidArgument, *name);
            }
        }
        return result;
    }

    // --- statements ---------------------------------------------------------
    // `context` : la ligne de l'instruction qui precede dans le meme bloc, ou de
    // celle qui la contient (0 : aucune).
    Flow statement(const Stmt& s, std::uint32_t context) {
        if (aborted_) return {Flow::Kind::Aborted};
        if (budgetExhausted(s.line)) return {Flow::Kind::Aborted};
        // Lot API 8 : la ligne en cours (qui a ecrit) ; un point d'arret compte
        // une fois par entree dans sa ligne - pas pour chaque instruction de
        // `a := 1; b := 2;`, ni pour le corps d'un IF ecrit sur la meme ligne.
        if (limits_.trace) limits_.trace->line = s.line;
        if (!breaks_->empty() && s.line != context && s.line < breaks_->size() && (*breaks_)[s.line] != 0) reachLine(s.line);

        switch (s.kind) {
            case Stmt::Kind::Assign: {
                if (dialect_) return assignRich(s);         // 1.10
                // A whole-array or whole-structure copy has no single value to
                // evaluate, so it is tried first and only then the scalar path.
                if (s.extraTargets.empty()
                    && (s.value->kind == Expr::Kind::Reference
                        || s.value->kind == Expr::Kind::Member
                        || s.value->kind == Expr::Kind::Index)) {
                    auto from = qualify(*s.value);
                    auto to   = qualify(*s.target);
                    if (from && to && !env_.exists(*from) && env_.assignAggregate(*to, *from))
                        return {};
                }
                // 1.10.2 (SIM) : tableau := STRING_TO_ASCII(chaine). Le resultat est
                // un tableau : l'environnement en ecrit les cases (son NOM passe a
                // la place de l'instance, voir Runtime::call). Avant, l'appel
                // inconnu rendait 0 et `stockage_nom` devenait une variable a
                // part : le nom du gaz se perdait (code 41, page 151).
                if (s.extraTargets.empty() && s.value->kind == Expr::Kind::Call && s.value->lhs
                    && s.value->arguments.size() == 1 && s.value->outputs.empty()
                    && s.value->arguments.front().second) {
                    auto callee = qualify(*s.value->lhs);
                    if (callee && upperOf(*callee) == "STRING_TO_ASCII") {
                        auto to = qualify(*s.target);
                        if (to && !env_.exists(*to) && *to != *callee) {
                            auto text = evaluate(*s.value->arguments.front().second);
                            if (!text) {
                                if (!aborted_) fail(s.line, text.error().message());
                                return {Flow::Kind::Aborted};
                            }
                            Value ignored;
                            if (env_.call(*callee, *to, {{std::string{}, *text}}, ignored)) return {};
                        }
                    }
                }
                auto value = evaluate(*s.value);
                if (!value) {
                    if (!aborted_) fail(s.line, value.error().message());
                    return {Flow::Kind::Aborted};
                }
                auto store = [&](const Expr& target) {
                    auto name = qualify(target);
                    if (!name) { fail(s.line, name.error().message()); return false; }
                    if (!env_.write(*name, *value)) {
                        fail(s.line, "'" + *name + "' cannot be written");
                        return false;
                    }
                    return true;
                };
                if (!store(*s.target)) return {Flow::Kind::Aborted};
                for (const auto& extra : s.extraTargets)
                    if (!store(*extra)) return {Flow::Kind::Aborted};
                return {};
            }
            case Stmt::Kind::Call: {
                auto v = evaluate(*s.value);
                if (!v && !aborted_) fail(s.line, v.error().message());
                return aborted_ ? Flow{Flow::Kind::Aborted} : Flow{};
            }
            case Stmt::Kind::If: {
                for (const auto& [cond, body] : s.branches) {
                    auto c = evaluate(*cond);
                    if (!c) { if (!aborted_) fail(s.line, c.error().message()); return {Flow::Kind::Aborted}; }
                    if (c->isTruthy()) return sequence(body, s.line);
                }
                return sequence(s.elseBody, s.line);
            }
            case Stmt::Kind::Case: {
                auto selector = evaluate(*s.condition);
                if (!selector) { if (!aborted_) fail(s.line, selector.error().message()); return {Flow::Kind::Aborted}; }
                const auto v = selector->asInteger();
                for (const auto& arm : s.arms)
                    for (const auto& [low, high] : arm.ranges)
                        if (v >= low && v <= high) return sequence(arm.body, s.line);
                // 1.10 (dialecte IHM) : les valeurs d'enumeration d'un bras (T_MODE#Auto, ou
                // Auto seul : le type est celui du selecteur).
                if (dialect_) {
                    std::string selectorType;
                    bool typed = false;
                    for (const auto& arm : s.arms)
                        for (const auto& name : arm.names) {
                            std::string full = name;
                            if (name.find('#') == std::string::npos) {
                                if (!typed) {
                                    selectorType = enumTypeOf(*s.condition);
                                    typed = true;
                                }
                                if (selectorType.empty()) {
                                    fail(s.line, "CASE : '" + name + "' n'est une valeur que si le CASE porte sur une \xC3\xA9"
                                                 "num\xC3\xA9" "ration (\xC3\xA9" "cris T_TYPE#" + name + ")");
                                    return {Flow::Kind::Aborted};
                                }
                                full = selectorType + "#" + name;
                            }
                            Value lit;
                            if (!env_.read(full, lit)) {
                                fail(s.line, "valeur d'\xC3\xA9" "num\xC3\xA9" "ration inconnue : " + full);
                                return {Flow::Kind::Aborted};
                            }
                            if (lit.asInteger() == v) return sequence(arm.body, s.line);
                        }
                }
                return sequence(s.elseBody, s.line);
            }
            case Stmt::Kind::For:    return forLoop(s);
            case Stmt::Kind::While:  return whileLoop(s);
            case Stmt::Kind::Repeat: return repeatLoop(s);
            case Stmt::Kind::Exit:   return {Flow::Kind::Exit};
            case Stmt::Kind::Return:
                if (dialect_ && s.value) return returnValue(s);   // 1.10 : RETURN expr
                return {Flow::Kind::Return};
            case Stmt::Kind::ForEach: return forEach(s);          // 1.10
        }
        return {};
    }

    Flow sequence(const std::vector<StmtPtr>& body, std::uint32_t parentLine) {
        std::uint32_t context = parentLine;
        for (const auto& s : body) {
            const auto flow = statement(*s, context);
            context = s->line;
            if (flow.kind != Flow::Kind::Normal) return flow;
        }
        return {};
    }

    Flow forLoop(const Stmt& s) {
        auto from = evaluate(*s.from);
        auto to   = evaluate(*s.to);
        if (!from || !to) { if (!aborted_) fail(s.line, "bad loop bounds"); return {Flow::Kind::Aborted}; }
        std::int64_t step = 1;
        if (s.step) {
            auto v = evaluate(*s.step);
            if (!v) return {Flow::Kind::Aborted};
            step = v->asInteger();
        }
        if (step == 0) { fail(s.line, "FOR loop with a step of zero would never end"); return {Flow::Kind::Aborted}; }

        std::uint32_t iterations = 0;
        for (std::int64_t i = from->asInteger();
             step > 0 ? i <= to->asInteger() : i >= to->asInteger(); i += step) {
            if (++iterations > limits_.maxIterationsPerLoop) {
                fail(s.line, "FOR loop ran more than "
                                 + std::to_string(limits_.maxIterationsPerLoop) + " times");
                return {Flow::Kind::Aborted};
            }
            if (limits_.trace) limits_.trace->line = s.line;   // lot API 8 : i s'ecrit sur la ligne du FOR
            if (dialect_) {                                     // 1.10 : une variable de boucle locale
                if (!assignName(s.loopVariable, Value::integer(Type::DInt, i), s.line)) return {Flow::Kind::Aborted};
            } else {
                env_.write(s.loopVariable, Value::integer(Type::DInt, i));
            }
            const auto flow = sequence(s.body, s.line);
            if (flow.kind == Flow::Kind::Exit)   break;
            if (flow.kind != Flow::Kind::Normal) return flow;
        }
        return {};
    }

    Flow whileLoop(const Stmt& s) {
        std::uint32_t iterations = 0;
        for (;;) {
            auto c = evaluate(*s.condition);
            if (!c) { if (!aborted_) fail(s.line, c.error().message()); return {Flow::Kind::Aborted}; }
            if (!c->isTruthy()) break;
            if (++iterations > limits_.maxIterationsPerLoop) {
                fail(s.line, "WHILE loop ran more than "
                                 + std::to_string(limits_.maxIterationsPerLoop)
                                 + " times without its condition becoming false");
                return {Flow::Kind::Aborted};
            }
            const auto flow = sequence(s.body, s.line);
            if (flow.kind == Flow::Kind::Exit)   break;
            if (flow.kind != Flow::Kind::Normal) return flow;
        }
        return {};
    }

    Flow repeatLoop(const Stmt& s) {
        std::uint32_t iterations = 0;
        for (;;) {
            const auto flow = sequence(s.body, s.line);
            if (flow.kind == Flow::Kind::Exit)   break;
            if (flow.kind != Flow::Kind::Normal) return flow;
            if (++iterations > limits_.maxIterationsPerLoop) {
                fail(s.line, "REPEAT loop ran more than "
                                 + std::to_string(limits_.maxIterationsPerLoop) + " times");
                return {Flow::Kind::Aborted};
            }
            auto c = evaluate(*s.condition);
            if (!c) { if (!aborted_) fail(s.line, c.error().message()); return {Flow::Kind::Aborted}; }
            if (c->isTruthy()) break;
        }
        return {};
    }

    // ======================================================================
    //  1.10 : LE DIALECTE IHM (jamais pour le ST de l'automate : dialect_ faux)
    // ======================================================================
public:
    // Une valeur : un scalaire (o nul) ou un objet riche (tableau, structure,
    // MAP, reference, pointeur, iterateur).
    struct RV {
        Value  v;
        ObjRef o;
    };
    // Un emplacement : un objet de l'interpreteur, ou un nom de l'environnement.
    struct Place {
        ObjRef      obj;
        std::string name;
        bool        constant{false};
        bool        temp{false};          // une valeur calculee (le retour d'un appel...)
    };

    // Appeler une fonction du dialecte (une fonction IHM du projet, un operateur)
    // depuis l'exterieur : les arguments sont des valeurs.
    core::Result<RV> invoke(const Function& f, const std::vector<RV>& args, bool isolated) {
        dialect_ = true;
        if (frames_.empty()) frames_.emplace_back();
        std::vector<Bound> bound;
        for (const auto& a : args) bound.push_back(Bound{a, {}, false});
        return runFunction(f, bound, f.line, isolated);
    }

private:
    struct Scope {
        std::map<std::string, Place, std::less<>> names;      // en majuscules
    };
    struct Frame {
        const Function*    fn{nullptr};
        std::vector<Scope> scopes;          // [0] : parametres et locales ; puis les FOR EACH
        bool               seesScript{true};
        ObjRef             result;          // la valeur rendue
    };
    // Un argument prepare : sa valeur, ou l'emplacement qu'il designe.
    struct Bound {
        RV    value;
        Place place;
        bool  isPlace{false};
    };

    core::Err<core::Error> dialectError(std::string message) const {
        return core::fail(core::ErrorCode::InvalidArgument, std::move(message));
    }

    // ---- les types ---------------------------------------------------------
    // Une structure (type IHM, DDT) : ses membres, par l'environnement.
    core::Result<TypeRef> resolve(const TypeRef& t, int depth = 0) {
        if (!t) return dialectError("type inconnu");
        if (depth > 32) return dialectError("type trop imbriqu\xC3\xA9 (circulaire ?) : " + t->text());
        switch (t->kind) {
            case TypeDesc::Kind::Scalar:
            case TypeDesc::Kind::Iterator:
                return t;
            case TypeDesc::Kind::Struct: {
                if (!t->members.empty()) return t;
                const std::string key = upperOf(t->name);
                if (const auto it = structs_.find(key); it != structs_.end()) return it->second;
                std::vector<std::pair<std::string, std::string>> members;
                if (!env_.structMembers(t->name, members) || members.empty())
                    return dialectError("type inconnu : " + t->name);
                // 1.10 (decision 15) : une enumeration repond par ses valeurs ("#n") :
                // une variable de ce type est un DINT qui garde le nom de son type.
                if (isEnumeration(members)) {
                    TypeRef out = scalarType(Type::DInt, t->name);
                    structs_[key] = out;
                    return out;
                }
                auto d = std::make_shared<TypeDesc>(*t);
                for (const auto& [mname, mtype] : members) {
                    auto mt = typeFromText(mtype);
                    if (!mt) return core::Err<core::Error>(mt.error());
                    auto r = resolve(*mt, depth + 1);
                    if (!r) return r;
                    d->members.emplace_back(mname, *r);
                }
                TypeRef out = d;
                structs_[key] = out;
                return out;
            }
            case TypeDesc::Kind::Array:
            case TypeDesc::Kind::Map: {
                auto e = resolve(t->element, depth + 1);
                if (!e) return e;
                if (*e == t->element) return t;
                auto d = std::make_shared<TypeDesc>(*t);
                d->element = *e;
                return TypeRef(d);
            }
            case TypeDesc::Kind::Ref:
            case TypeDesc::Kind::Pointer:
                return t;            // la cible n'est pas instanciee : resolue a l'emploi
        }
        return t;
    }

    // Un type ecrit ("ARRAY[0..9] OF REAL", "T_FOUR") : lu par le parseur du dialecte.
    core::Result<TypeRef> typeFromText(std::string_view text) {
        const std::string key(text);
        if (const auto it = typeTexts_.find(key); it != typeTexts_.end()) return it->second;
        auto t = parseTypeText(text);
        if (!t) return t;
        typeTexts_[key] = *t;
        return t;
    }

    core::Result<ObjRef> instantiate(const TypeRef& t) {
        auto r = resolve(t);
        if (!r) return core::Err<core::Error>(r.error());
        return makeObj(*r);
    }

    // ---- 1.10 (decision 15) : les enumerations ------------------------------
    // L'environnement decrit une enumeration par ses valeurs : (nom, "#nombre").
    static bool isEnumeration(const std::vector<std::pair<std::string, std::string>>& members) {
        if (members.empty()) return false;
        for (const auto& m : members)
            if (m.second.empty() || m.second[0] != '#') return false;
        return true;
    }
    bool enumValues(std::string_view type, std::vector<std::pair<std::string, std::int64_t>>& out) {
        std::vector<std::pair<std::string, std::string>> members;
        if (type.empty() || !env_.structMembers(type, members) || !isEnumeration(members)) return false;
        for (const auto& [n, v] : members) out.emplace_back(n, std::strtoll(v.c_str() + 1, nullptr, 10));
        return true;
    }
    // Le type d'enumeration d'une expression simple (vide : aucun ou inconnu) :
    // un litteral T_MODE#Auto, une locale de type T_MODE, une variable IHM.
    std::string enumTypeOf(const Expr& e) {
        if (e.kind == Expr::Kind::Reference) {
            if (const auto hash = e.name.find('#'); hash != std::string::npos) return e.name.substr(0, hash);
        }
        if (!isDesignator(e)) return {};
        if (needsLocate(e)) {
            auto p = locate(e, false);
            if (!p) return {};
            if (p->obj && p->obj->type && p->obj->type->kind == TypeDesc::Kind::Scalar && !p->obj->type->name.empty()) {
                std::vector<std::pair<std::string, std::int64_t>> values;
                if (enumValues(p->obj->type->name, values)) return p->obj->type->name;
            }
            if (p->obj) return {};
            const std::string t = env_.declaredType(p->name);
            std::vector<std::pair<std::string, std::int64_t>> values;
            return enumValues(t, values) ? t : std::string{};
        }
        auto name = qualify(e);
        if (!name) return {};
        const std::string t = env_.declaredType(*name);
        std::vector<std::pair<std::string, std::int64_t>> values;
        return enumValues(t, values) ? t : std::string{};
    }
    // Le nom seul d'une valeur (Auto) quand le type attendu est l'enumeration
    // `enumType` : sa valeur. Une vraie variable du meme nom passe avant.
    bool bareEnum(const std::string& enumType, const Expr& e, Value& out) {
        if (enumType.empty() || e.kind != Expr::Kind::Reference || e.name.find('#') != std::string::npos) return false;
        if (findLocal(e.name) || env_.exists(e.name)) return false;
        std::vector<std::pair<std::string, std::int64_t>> values;
        if (!enumValues(enumType, values)) return false;
        const std::string u = upperOf(e.name);
        for (const auto& [vname, number] : values)
            if (upperOf(vname) == u) {
                out = Value::integer(Type::DInt, number);
                return true;
            }
        return false;
    }
    static bool bareName(const Expr& e) {
        return e.kind == Expr::Kind::Reference && e.name.find('#') == std::string::npos;
    }
    // Le nom d'enumeration d'un type declare (nul : aucun).
    std::string enumNameOf(const TypeRef& t) {
        if (!t || (t->kind != TypeDesc::Kind::Struct && t->kind != TypeDesc::Kind::Scalar) || t->name.empty()) return {};
        std::vector<std::pair<std::string, std::int64_t>> values;
        return enumValues(t->name, values) ? t->name : std::string{};
    }
    // Un objet d'une vue qui est une instance de symbole (Vue_A.Pompe1) : le nom
    // du symbole (l'environnement le dit), sinon vide.
    std::string symbolTypeOf(const Expr& e, std::string* name) {
        if (!isDesignator(e) || needsLocate(e)) return {};
        auto q = qualify(e);
        if (!q) return {};
        Value probe;
        if (env_.read(*q, probe)) return {};
        const std::string t = env_.declaredType(*q);
        if (t.empty()) return {};
        std::vector<std::pair<std::string, std::string>> members;
        if (env_.structMembers(t, members)) return {};          // une structure, une enumeration : pas un symbole
        if (typeFromName(t) != Type::Unknown || t.rfind("ARRAY", 0) == 0) return {};
        if (name) *name = env_.canonicalName(*q);
        return t;
    }

    // Les conversions standard sans prefixe : TO_REAL(5), TO_STRING(3.5), TO_DINT(m)...
    static bool standardConversion(const std::string& u, const Value& v, Value& out) {
        const std::string target = u.substr(3);
        if (target == "STRING") {
            out = v.type() == Type::String ? v : Value::text(v.type() == Type::Real ? v.display() : v.display());
            if (v.type() == Type::String) out = v;
            return true;
        }
        if (target == "REAL" || target == "LREAL") {
            out = Value::real(v.type() == Type::String ? std::strtod(v.asString().c_str(), nullptr) : v.asReal());
            return true;
        }
        if (target == "BOOL") {
            out = Value::boolean(v.type() == Type::String ? (v.asString() == "TRUE" || v.asString() == "1") : v.isTruthy());
            return true;
        }
        Type t = typeFromName(target);
        if (target == "SINT" || target == "LINT") t = target == "SINT" ? Type::Int : Type::DInt;
        if (target == "USINT" || target == "ULINT") t = target == "USINT" ? Type::UInt : Type::UDInt;
        if (t == Type::Unknown || t == Type::String) return false;
        std::int64_t n = 0;
        if (v.type() == Type::String) n = std::strtoll(v.asString().c_str(), nullptr, 10);
        else if (v.type() == Type::Real) n = static_cast<std::int64_t>(std::llround(v.asReal()));
        else n = v.asInteger();
        out = t == Type::Time ? Value::time(n) : Value::integer(t, n);
        return true;
    }

    // ---- les noms ----------------------------------------------------------
    Place* findLocal(std::string_view name) {
        if (frames_.empty()) return nullptr;
        const std::string u = upperOf(name);
        auto search = [&](Frame& f) -> Place* {
            for (auto it = f.scopes.rbegin(); it != f.scopes.rend(); ++it)
                if (const auto p = it->names.find(u); p != it->names.end()) return &p->second;
            return nullptr;
        };
        if (auto* p = search(frames_.back())) return p;
        if (frames_.size() > 1 && frames_.back().seesScript) return search(frames_.front());
        return nullptr;
    }

    const Function* findFunction(std::string_view name) const {
        if (!program_) return nullptr;
        const std::string u = upperOf(name);
        for (const auto& f : program_->functions)
            if (upperOf(f->name) == u) return f.get();
        return nullptr;
    }

    // Faut-il passer par les emplacements (un nom local, un ^, un appel en tete) ?
    bool needsLocate(const Expr& e) {
        const Expr* x = &e;
        while (x) {
            switch (x->kind) {
                case Expr::Kind::Reference: return findLocal(x->name) != nullptr;
                case Expr::Kind::Member:
                case Expr::Kind::Index: x = x->lhs.get(); continue;
                case Expr::Kind::Deref:
                case Expr::Kind::Call: return true;
                default: return false;
            }
        }
        return false;
    }

    // Le debut : les VAR du script (gardees par l'appelant), le cadre de premier niveau.
    bool startDialect(const Program& program) {
        dialect_ = true;
        frames_.clear();
        frames_.emplace_back();
        frames_.back().scopes.emplace_back();
        Locals* store = limits_.locals ? limits_.locals : &ownLocals_;
        auto& names = frames_.back().scopes.back().names;
        for (const auto& d : program.locals) {
            ObjRef obj = store->find(d.name);
            auto t = resolve(d.type);
            if (!t) {
                fail(d.line, d.name + " : " + t.error().message());
                return false;
            }
            // VAR : gardee d'une execution a l'autre (si son type n'a pas change) ;
            // VAR_TEMP : repart de sa valeur initiale.
            const bool keep = obj && d.mode == VarDecl::Mode::Local && obj->type && sameType(*obj->type, **t);
            if (!keep) {
                obj = makeObj(*t);
                if (d.initial && !initialize(*obj, *d.initial, d.line)) return false;
                store->set(d.name, obj);
            }
            names[upperOf(d.name)] = Place{obj, {}, d.constant, false};
        }
        return true;
    }

    // La valeur initiale d'une declaration : une expression, ou une liste
    // [1, 2, 3] pour un tableau (une seule valeur : toutes les cases).
    bool initialize(Obj& obj, const Expr& init, std::uint32_t line) {
        auto v = rich(init);
        if (!v) {
            fail(line, v.error().message());
            return false;
        }
        std::string why;
        if (!v->o && obj.type && obj.type->kind == TypeDesc::Kind::Array) {
            for (auto& item : obj.items)
                if (item && item->type && item->type->kind == TypeDesc::Kind::Scalar) item->value.assignFrom(v->v);
            return true;
        }
        if (!store(obj, *v, &why)) {
            fail(line, why);
            return false;
        }
        return true;
    }

    // Ecrire une valeur dans un objet (copie).
    bool store(Obj& into, const RV& v, std::string* why) {
        if (!into.type) {
            if (why) *why = "variable sans type";
            return false;
        }
        if (!v.o) {
            if (into.type->kind == TypeDesc::Kind::Scalar) {
                into.value.assignFrom(v.v);
                return true;
            }
            if (why) *why = "une valeur simple ne va pas dans un " + into.type->text();
            return false;
        }
        if (&into == v.o.get()) return true;
        // NULL dans une reference ou un pointeur.
        if (v.o->type && v.o->type->kind == TypeDesc::Kind::Pointer && !v.o->type->element
            && (into.type->kind == TypeDesc::Kind::Ref || into.type->kind == TypeDesc::Kind::Pointer)) {
            into.bound = false;
            into.target.reset();
            into.targetName.clear();
            return true;
        }
        // Une reference et un pointeur de meme cible s'echangent (REF_TO T := ADR(x)).
        if ((into.type->kind == TypeDesc::Kind::Ref || into.type->kind == TypeDesc::Kind::Pointer) && v.o->type
            && (v.o->type->kind == TypeDesc::Kind::Ref || v.o->type->kind == TypeDesc::Kind::Pointer)) {
            if (into.type->element && v.o->type->element && !compatibleTarget(*into.type->element, *v.o->type->element)) {
                if (why) *why = "un " + v.o->type->text() + " ne va pas dans un " + into.type->text();
                return false;
            }
            into.bound = v.o->bound;
            into.target = v.o->target;
            into.targetName = v.o->targetName;
            return true;
        }
        return assignObj(into, *v.o, why);
    }

    static bool compatibleTarget(const TypeDesc& want, const TypeDesc& got) {
        if (want.kind == TypeDesc::Kind::Scalar && got.kind == TypeDesc::Kind::Scalar)
            return want.scalar == got.scalar || (isNumeric(want.scalar) && isNumeric(got.scalar));
        // 1.10 : une cible pas encore resolue (REF_TO T_MODE, POINTER TO T_Point) : par son nom
        // (une enumeration resolue est un DINT qui garde le nom de son type).
        if ((want.kind == TypeDesc::Kind::Struct && got.kind == TypeDesc::Kind::Scalar)
            || (want.kind == TypeDesc::Kind::Scalar && got.kind == TypeDesc::Kind::Struct))
            return !want.name.empty() && upperOf(want.name) == upperOf(got.name);
        return sameType(want, got);
    }

    // ---- lire et ecrire un emplacement ------------------------------------
    core::Result<RV> readPlace(const Place& p) {
        if (p.obj) {
            if (p.obj->type && p.obj->type->kind == TypeDesc::Kind::Scalar) return RV{p.obj->value, nullptr};
            return RV{{}, p.obj};
        }
        Value v;
        if (env_.read(p.name, v)) return RV{v, nullptr};
        // Une structure ou un tableau de l'environnement : sa copie.
        auto obj = materialize(p.name);
        if (obj) return RV{{}, *obj};
        return dialectError("'" + p.name + "' is not declared");
    }

    // Une variable composee de l'environnement (structure ou tableau IHM, DDT),
    // copiee dans un objet : case par case, par son nom.
    core::Result<ObjRef> materialize(const std::string& name) {
        const std::string text = env_.declaredType(name);
        if (text.empty()) return dialectError("'" + name + "' is not declared");
        auto t = typeFromText(text);
        if (!t) return core::Err<core::Error>(t.error());
        auto obj = instantiate(*t);
        if (!obj) return obj;
        if (!loadFromEnv(**obj, name)) return dialectError("'" + name + "' ne se lit pas en entier");
        return obj;
    }

    bool loadFromEnv(Obj& o, const std::string& name) {
        if (!o.type) return false;
        switch (o.type->kind) {
            case TypeDesc::Kind::Scalar: {
                Value v;
                if (!env_.read(name, v)) return false;
                o.value.assignFrom(v);
                return true;
            }
            case TypeDesc::Kind::Array:
                for (std::size_t k = 0; k < o.items.size(); ++k)
                    if (!loadFromEnv(*o.items[k], name + indexText(*o.type, k))) return false;
                return true;
            case TypeDesc::Kind::Struct:
                for (std::size_t k = 0; k < o.items.size() && k < o.type->members.size(); ++k)
                    if (!loadFromEnv(*o.items[k], name + "." + o.type->members[k].first)) return false;
                return true;
            default: return false;
        }
    }

    bool storeToEnv(const std::string& name, const Obj& o, std::string* why) {
        if (!o.type) return false;
        switch (o.type->kind) {
            case TypeDesc::Kind::Scalar:
                if (env_.write(name, o.value)) return true;
                if (why) *why = "'" + name + "' cannot be written";
                return false;
            case TypeDesc::Kind::Array:
                for (std::size_t k = 0; k < o.items.size(); ++k)
                    if (!storeToEnv(name + indexText(*o.type, k), *o.items[k], why)) return false;
                return true;
            case TypeDesc::Kind::Struct:
                for (std::size_t k = 0; k < o.items.size() && k < o.type->members.size(); ++k)
                    if (!storeToEnv(name + "." + o.type->members[k].first, *o.items[k], why)) return false;
                return true;
            default:
                if (why) *why = "un " + o.type->text() + " ne s'\xC3\xA9" "crit pas dans '" + name + "' (variable de l'IHM ou de l'automate)";
                return false;
        }
    }

    // "[2]", "[1,7]" : les indices de la case `flat` d'un tableau.
    static std::string indexText(const TypeDesc& t, std::size_t flat) {
        std::vector<std::int64_t> idx(t.bounds.size(), 0);
        for (std::size_t d = t.bounds.size(); d-- > 0;) {
            const auto n = static_cast<std::size_t>(t.bounds[d].second - t.bounds[d].first + 1);
            idx[d] = t.bounds[d].first + static_cast<std::int64_t>(flat % n);
            flat /= n;
        }
        std::string s = "[";
        for (std::size_t d = 0; d < idx.size(); ++d) s += (d ? "," : "") + std::to_string(idx[d]);
        return s + "]";
    }

    bool writePlace(const Place& p, const RV& v, std::string* why) {
        if (p.constant) {
            if (why) *why = "une constante ne s'\xC3\xA9" "crit pas";
            return false;
        }
        if (p.obj) return store(*p.obj, v, why);
        if (!v.o) {
            if (env_.write(p.name, v.v)) return true;
            if (why) *why = "'" + p.name + "' cannot be written";
            return false;
        }
        return storeToEnv(p.name, *v.o, why);
    }

    // Suivre une reference ou un pointeur jusqu'a sa cible.
    core::Result<Place> follow(const ObjRef& o) {
        if (!o->bound) return dialectError(o->type && o->type->kind == TypeDesc::Kind::Ref
                                               ? "r\xC3\xA9" "f\xC3\xA9rence nulle (elle ne vise rien)"
                                               : "pointeur nul (NULL) : il ne vise rien");
        if (!o->targetName.empty()) return Place{nullptr, o->targetName, false, false};
        auto t = o->target.lock();
        if (!t) return dialectError(o->type && o->type->kind == TypeDesc::Kind::Ref
                                        ? "r\xC3\xA9" "f\xC3\xA9rence vers une variable disparue (la variable locale d'une fonction termin\xC3\xA9" "e ?)"
                                        : "pointeur vers une variable disparue (la variable locale d'une fonction termin\xC3\xA9" "e ?)");
        return Place{t, {}, false, false};
    }

    static bool isIndirect(const ObjRef& o) {
        return o && o->type && (o->type->kind == TypeDesc::Kind::Ref || o->type->kind == TypeDesc::Kind::Pointer);
    }

    // Une cle de MAP, convertie au type des cles.
    core::Result<bool> mapKey(const Obj& map, const RV& k, std::string& text, std::int64_t& integer) {
        if (k.o) return dialectError("une cl\xC3\xA9 de MAP est une valeur simple, pas un " + k.o->type->text());
        if (isInteger(map.type->key)) {
            if (!isInteger(k.v.type()) && k.v.type() != Type::Bool)
                return dialectError("cl\xC3\xA9 du mauvais type : " + std::string(toString(k.v.type())) + " pour une "
                                    + map.type->text() + " (cl\xC3\xA9s enti\xC3\xA8res)");
            integer = k.v.asInteger();
            return false;
        }
        if (k.v.type() != Type::String)
            return dialectError("cl\xC3\xA9 du mauvais type : " + std::string(toString(k.v.type())) + " pour une "
                                + map.type->text() + " (cl\xC3\xA9s STRING)");
        text = k.v.asString();
        return true;
    }

    static std::string keyText(bool isText, const std::string& t, std::int64_t i) {
        return isText ? "'" + t + "'" : std::to_string(i);
    }

    // L'emplacement que designe une expression. `create` : une cle absente
    // d'une MAP est creee (une ecriture).
    core::Result<Place> locate(const Expr& e, bool create) {
        switch (e.kind) {
            case Expr::Kind::Reference: {
                if (auto* p = findLocal(e.name)) return *p;
                return Place{nullptr, e.name, false, false};
            }
            case Expr::Kind::Deref: {
                auto base = rich(*e.lhs);
                if (!base) return core::Err<core::Error>(base.error());
                if (!isIndirect(base->o)) return dialectError("'^' : ce n'est ni une r\xC3\xA9" "f\xC3\xA9rence ni un pointeur");
                return follow(base->o);
            }
            case Expr::Kind::Call: {
                auto v = rich(e);
                if (!v) return core::Err<core::Error>(v.error());
                if (!v->o) {
                    auto tmp = makeObj(scalarType(v->v.type()));
                    tmp->value = v->v;
                    return Place{tmp, {}, true, true};
                }
                return Place{v->o, {}, false, true};
            }
            case Expr::Kind::Member: {
                auto base = locate(*e.lhs, create);
                if (!base) return base;
                Place p = *base;
                if (!p.obj) return Place{nullptr, p.name + "." + e.name, p.constant, false};
                // Une reference ou un pointeur se suit tout seul : r.Membre = r^.Membre.
                while (isIndirect(p.obj)) {
                    auto t = follow(p.obj);
                    if (!t) return t;
                    if (!t->obj) return Place{nullptr, t->name + "." + e.name, false, false};
                    p = *t;
                }
                const auto& type = *p.obj->type;
                const std::string m = upperOf(e.name);
                if (type.kind == TypeDesc::Kind::Struct) {
                    for (std::size_t k = 0; k < type.members.size() && k < p.obj->items.size(); ++k)
                        if (upperOf(type.members[k].first) == m) return Place{p.obj->items[k], {}, p.constant, p.temp};
                    return dialectError(type.name + " n'a pas de membre '" + e.name + "'");
                }
                if (type.kind == TypeDesc::Kind::Iterator) {
                    auto map = p.obj->map.lock();
                    if (!map) return dialectError("it\xC3\xA9rateur sans MAP (MAP_BEGIN(m) d'abord)");
                    if (p.obj->atEnd) return dialectError("it\xC3\xA9rateur au bout de la MAP (MAP_END(it) est vrai)");
                    if (m == "KEY") {
                        auto k = makeObj(scalarType(isInteger(map->type->key) ? map->type->key : Type::String, map->type->name));
                        k->value = isInteger(map->type->key) ? Value::integer(map->type->key, p.obj->intKey) : Value::text(p.obj->textKey);
                        return Place{k, {}, true, true};
                    }
                    if (m == "VALUE") {
                        ObjRef entry;
                        if (isInteger(map->type->key)) {
                            if (const auto it = map->intKeys.find(p.obj->intKey); it != map->intKeys.end()) entry = it->second;
                        } else if (const auto it = map->textKeys.find(p.obj->textKey); it != map->textKeys.end()) {
                            entry = it->second;
                        }
                        if (!entry) return dialectError("la cl\xC3\xA9 de l'it\xC3\xA9rateur a \xC3\xA9t\xC3\xA9 retir\xC3\xA9" "e de la MAP");
                        return Place{entry, {}, false, false};
                    }
                    return dialectError("un it\xC3\xA9rateur a .Key et .Value, pas '" + e.name + "'");
                }
                // Les proprietes d'un tableau, d'une MAP : en lecture seule.
                std::int64_t value = 0;
                bool known = false;
                if (type.kind == TypeDesc::Kind::Array) {
                    if (m == "LENGTH" || m == "SIZE" || m == "COUNT" || m == "MAXSIZE") { value = type.count(); known = true; }
                    else if (m == "LOW" && !type.bounds.empty()) { value = type.bounds[0].first; known = true; }
                    else if (m == "HIGH" && !type.bounds.empty()) { value = type.bounds[0].second; known = true; }
                    else if (m == "ROWS" && !type.bounds.empty()) { value = type.bounds[0].second - type.bounds[0].first + 1; known = true; }
                    else if ((m == "COLUMNS" || m == "COLS") && type.bounds.size() > 1) { value = type.bounds[1].second - type.bounds[1].first + 1; known = true; }
                    else if (m == "DIMENSIONS") { value = static_cast<std::int64_t>(type.bounds.size()); known = true; }
                } else if (type.kind == TypeDesc::Kind::Map) {
                    if (m == "LENGTH" || m == "SIZE" || m == "COUNT") {
                        value = static_cast<std::int64_t>(p.obj->textKeys.size() + p.obj->intKeys.size());
                        known = true;
                    }
                }
                if (known) {
                    auto tmp = makeObj(scalarType(Type::DInt));
                    tmp->value = Value::integer(Type::DInt, value);
                    return Place{tmp, {}, true, true};
                }
                return dialectError("'" + e.name + "' : un " + type.text() + " n'a pas ce membre");
            }
            case Expr::Kind::Index: {
                auto base = locate(*e.lhs, create);
                if (!base) return base;
                Place p = *base;
                std::vector<RV> indices;
                auto one = rich(*e.rhs);
                if (!one) return core::Err<core::Error>(one.error());
                indices.push_back(*one);
                if (e.rhs2) {
                    auto two = rich(*e.rhs2);
                    if (!two) return core::Err<core::Error>(two.error());
                    indices.push_back(*two);
                }
                for (const auto& k : e.more) {
                    auto x = rich(*k);
                    if (!x) return core::Err<core::Error>(x.error());
                    indices.push_back(*x);
                }
                if (!p.obj) {
                    std::string cell = p.name + "[";
                    for (std::size_t i = 0; i < indices.size(); ++i) {
                        if (indices[i].o) return dialectError("un indice est une valeur simple");
                        cell += (i ? "," : "") + std::to_string(indices[i].v.asInteger());
                    }
                    return Place{nullptr, cell + "]", p.constant, false};
                }
                while (isIndirect(p.obj)) {
                    auto t = follow(p.obj);
                    if (!t) return t;
                    if (!t->obj) {
                        std::string cell = t->name + "[";
                        for (std::size_t i = 0; i < indices.size(); ++i)
                            cell += (i ? "," : "") + std::to_string(indices[i].v.asInteger());
                        return Place{nullptr, cell + "]", false, false};
                    }
                    p = *t;
                }
                const auto& type = *p.obj->type;
                if (type.kind == TypeDesc::Kind::Array) {
                    std::vector<std::int64_t> idx;
                    for (const auto& i : indices) {
                        if (i.o || !(isInteger(i.v.type()) || i.v.type() == Type::Bool))
                            return dialectError("un indice de tableau est un entier");
                        idx.push_back(i.v.asInteger());
                    }
                    std::size_t flat = 0;
                    std::string why;
                    if (!flatIndex(type, idx, flat, &why)) return dialectError(why);
                    return Place{p.obj->items[flat], {}, p.constant, p.temp};
                }
                if (type.kind == TypeDesc::Kind::Map) {
                    if (indices.size() != 1) return dialectError("une MAP prend une seule cl\xC3\xA9 : m[cl\xC3\xA9]");
                    std::string text;
                    std::int64_t integer = 0;
                    auto isText = mapKey(*p.obj, indices[0], text, integer);
                    if (!isText) return core::Err<core::Error>(isText.error());
                    ObjRef entry;
                    if (*isText) {
                        if (const auto it = p.obj->textKeys.find(text); it != p.obj->textKeys.end()) entry = it->second;
                    } else if (const auto it = p.obj->intKeys.find(integer); it != p.obj->intKeys.end()) {
                        entry = it->second;
                    }
                    if (!entry) {
                        if (!create)
                            return dialectError("cl\xC3\xA9 absente de la MAP : " + keyText(*isText, text, integer)
                                                + " (MAP_HAS(m, cl\xC3\xA9) pour tester, MAP_GET(m, cl\xC3\xA9, d\xC3\xA9" "faut) pour une valeur par d\xC3\xA9" "faut)");
                        if (p.constant) return dialectError("une constante ne s'\xC3\xA9" "crit pas");
                        auto made = instantiate(type.element);
                        if (!made) return core::Err<core::Error>(made.error());
                        entry = *made;
                        if (*isText) p.obj->textKeys[text] = entry;
                        else p.obj->intKeys[integer] = entry;
                    }
                    return Place{entry, {}, p.constant, p.temp};
                }
                return dialectError("'[...]' : un " + type.text() + " n'est ni un tableau ni une MAP");
            }
            default:
                return dialectError("ce n'est pas une variable (une valeur calcul\xC3\xA9" "e ne se d\xC3\xA9signe pas)");
        }
    }

    // ---- evaluer -----------------------------------------------------------
    core::Result<Value> scalarOf(const RV& r, const Expr& e) {
        if (!r.o) return r.v;
        (void)e;
        return dialectError("un " + (r.o->type ? r.o->type->text() : std::string("objet"))
                            + " est employ\xC3\xA9 l\xC3\xA0 o\xC3\xB9 une valeur simple est attendue");
    }

    core::Result<RV> rich(const Expr& e) {
        switch (e.kind) {
            case Expr::Kind::Literal: return RV{e.literal, nullptr};
            case Expr::Kind::Null: {
                static const TypeRef nullType = [] {
                    auto t = std::make_shared<TypeDesc>();
                    t->kind = TypeDesc::Kind::Pointer;            // sans cible : NULL
                    return TypeRef(t);
                }();
                return RV{{}, makeObj(nullType)};
            }
            case Expr::Kind::Unary: {
                auto operand = rich(*e.lhs);
                if (!operand) return operand;
                if (operand->o) return dialectError("'" + e.op + "' sur un " + operand->o->type->text());
                const Value& v = operand->v;
                if (e.op == "NOT") {
                    if (v.type() == Type::Bool) return RV{Value::boolean(!v.isTruthy()), nullptr};
                    return RV{Value::integer(v.type(), ~v.asInteger()), nullptr};
                }
                if (e.op == "-") {
                    if (v.type() == Type::Real) return RV{Value::real(-v.asReal()), nullptr};
                    return RV{Value::integer(v.type(), -v.asInteger()), nullptr};
                }
                return operand;
            }
            case Expr::Kind::Binary: return binaryRich(e);
            case Expr::Kind::Call: return callRich(e);
            case Expr::Kind::Reference:
            case Expr::Kind::Member:
            case Expr::Kind::Index:
            case Expr::Kind::Deref: {
                if (needsLocate(e)) {
                    auto p = locate(e, false);
                    if (!p) return core::Err<core::Error>(p.error());
                    return readPlace(*p);
                }
                // Un nom de l'environnement : comme avant ; une structure ou un
                // tableau entier de l'IHM : sa copie.
                auto name = qualify(e);
                if (!name) return core::Err<core::Error>(name.error());
                Value out;
                if (env_.read(*name, out)) return RV{out, nullptr};
                if (!env_.exists(*name)) {
                    auto obj = materialize(*name);
                    if (obj) return RV{{}, *obj};
                }
                return dialectError("'" + *name + "' is not declared");
            }
        }
        return dialectError("expression non prise en charge");
    }

    // Le nom de type d'une valeur, pour chercher un operateur (S2).
    static std::string typeNameOf(const RV& v) {
        if (!v.o) return std::string(toString(v.v.type()));
        return v.o->type ? (v.o->type->kind == TypeDesc::Kind::Struct ? v.o->type->name : v.o->type->text()) : std::string{};
    }

    // Un operateur du projet (S2) : sa fonction, lue une fois.
    std::shared_ptr<const Function> projectOperator(std::string_view op, const std::string& left, const std::string& right,
                                                    std::string* owner) {
        OperatorSource src;
        if (!env_.findOperator(op, left, right, src)) return nullptr;
        if (owner) *owner = src.owner;
        if (const auto it = operatorCache().find(src.key); it != operatorCache().end()) return it->second;
        auto f = parseFunction(src.function, src.owner.empty() ? std::string(op) : src.owner);
        if (!f) {
            fail(0, (src.owner.empty() ? std::string(op) : src.owner) + " : " + f.error().message());
            return nullptr;
        }
        operatorCache()[src.key] = *f;
        return *f;
    }
    static std::map<std::string, std::shared_ptr<const Function>>& operatorCache() {
        static std::map<std::string, std::shared_ptr<const Function>> cache;
        return cache;
    }

    core::Result<RV> binaryRich(const Expr& e) {
        // 1.10 (decision 15) : m = Auto (le nom seul, du type de l'autre cote).
        if ((e.op == "=" || e.op == "<>" || e.op == "<" || e.op == ">" || e.op == "<=" || e.op == ">=")
            && (bareName(*e.rhs) || bareName(*e.lhs))) {
            Value lit;
            if (bareName(*e.rhs) && !findLocal(e.rhs->name) && !env_.exists(e.rhs->name)
                && bareEnum(enumTypeOf(*e.lhs), *e.rhs, lit)) {
                auto lhs = rich(*e.lhs);
                if (!lhs) return lhs;
                return combineRich(e, e.op, *lhs, RV{lit, nullptr});
            }
            if (bareName(*e.lhs) && !findLocal(e.lhs->name) && !env_.exists(e.lhs->name)
                && bareEnum(enumTypeOf(*e.rhs), *e.lhs, lit)) {
                auto rhs = rich(*e.rhs);
                if (!rhs) return rhs;
                return combineRich(e, e.op, RV{lit, nullptr}, *rhs);
            }
        }
        auto lhs = rich(*e.lhs);
        if (!lhs) return lhs;
        if (!lhs->o) {
            if (e.op == "AND" && !lhs->v.isTruthy()) return RV{Value::boolean(false), nullptr};
            if (e.op == "OR" && lhs->v.isTruthy() && lhs->v.type() == Type::Bool) return RV{Value::boolean(true), nullptr};
        }
        auto rhs = rich(*e.rhs);
        if (!rhs) return rhs;
        return combineRich(e, e.op, *lhs, *rhs);
    }

    core::Result<RV> combineRich(const Expr& e, const std::string& op, const RV& a, const RV& b) {
        if (!a.o && !b.o) {
            auto v = combine(e, a.v, b.v);
            if (!v) return core::Err<core::Error>(v.error());
            return RV{*v, nullptr};
        }
        // Un operateur du projet (S2) : sur une structure IHM, un symbole...
        const std::string left = typeNameOf(a), right = typeNameOf(b);
        if (auto f = projectOperator(op, left, right, nullptr)) {
            auto r = runFunction(*f, {Bound{a, {}, false}, Bound{b, {}, false}}, e.line, true);
            if (!r) return r;
            return r;
        }
        // = et <> entre deux tableaux, structures, MAP, references (meme cible), NULL.
        if (op == "=" || op == "<>") {
            bool equal = false;
            if (a.o && b.o) {
                const bool aNull = isIndirect(a.o) && !a.o->bound;
                const bool bNull = isIndirect(b.o) && !b.o->bound;
                if (isIndirect(a.o) && isIndirect(b.o)) equal = (aNull && bNull) || (!aNull && !bNull && deepEquals(*a.o, *b.o))
                                                              || (!aNull && !bNull && sameTarget(*a.o, *b.o));
                else equal = deepEquals(*a.o, *b.o);
            }
            return RV{Value::boolean(op == "=" ? equal : !equal), nullptr};
        }
        return dialectError("pas d'op\xC3\xA9rateur " + op + " pour " + left + " et " + right);
    }

    static bool sameTarget(const Obj& a, const Obj& b) {
        if (!a.targetName.empty() || !b.targetName.empty()) return upperOf(a.targetName) == upperOf(b.targetName);
        return a.target.lock() == b.target.lock();
    }

    // ---- les appels --------------------------------------------------------
    // Un argument : par valeur (sa valeur), ou par reference (son emplacement).
    core::Result<RV> callRich(const Expr& e) {
        std::string callee;
        if (e.lhs && e.lhs->kind == Expr::Kind::Reference) callee = e.lhs->name;
        const std::string u = upperOf(callee);
        // Les fonctions internes du script, puis les fonctions IHM du projet (dialecte).
        // 1.11.10 : Vue_Vannes.Vanne_3.Vecteur() - la fonction d'une instance de symbole,
        // aux types riches (une structure rendue, VAR_IN_OUT) : l'environnement la donne.
        if (callee.empty() && e.lhs && e.lhs->kind == Expr::Kind::Member)
            if (auto q = qualify(*e.lhs))
                if (auto f = env_.dialectFunction(*q)) return callDeclared(*f, e, true);
        if (!callee.empty()) {
            if (const Function* f = findFunction(callee)) return callDeclared(*f, e, false);
            if (auto builtin = dialectBuiltin(u, e)) return std::move(*builtin);
            if (auto f = env_.dialectFunction(callee)) return callDeclared(*f, e, true);
            // TO_xxx(objet) : une conversion du projet (S2).
            if (u.rfind("TO_", 0) == 0 && e.arguments.size() == 1 && e.outputs.empty()) {
                // 1.10 : TO_STRING(Vue_A.Pompe1) - une instance de symbole, par son nom (S2).
                std::string where;
                if (const std::string sym = symbolTypeOf(*e.arguments[0].second, &where); !sym.empty()) {
                    if (auto f = projectOperator(u, sym, {}, nullptr))
                        return runFunction(*f, {Bound{{}, Place{nullptr, where, true, false}, true}}, e.line, true);
                    return dialectError("pas de conversion " + callee + " pour le symbole " + sym);
                }
                auto arg = rich(*e.arguments[0].second);
                if (!arg) return arg;
                if (arg->o) {
                    std::string owner;
                    if (auto f = projectOperator(u, typeNameOf(*arg), {}, &owner))
                        return runFunction(*f, {Bound{*arg, {}, false}}, e.line, true);
                    return dialectError("pas de conversion " + callee + " pour un " + typeNameOf(*arg));
                }
                // Un scalaire : la conversion du projet (le type d'une enumeration compte :
                // TO_STRING(m) est le toString de T_MODE), puis la conversion standard.
                const std::string enumType = enumTypeOf(*e.arguments[0].second);
                if (auto f = projectOperator(u, enumType.empty() ? typeNameOf(*arg) : enumType, {}, nullptr))
                    return runFunction(*f, {Bound{*arg, {}, false}}, e.line, true);
                // TO_T_MODE(3) : un entier vers une valeur, controle.
                std::vector<std::pair<std::string, std::int64_t>> values;
                if (enumValues(callee.substr(3), values)) {
                    if (arg->v.type() == Type::String)
                        return dialectError("pas de conversion " + callee + " pour un STRING (le fromString de " + callee.substr(3)
                                            + " : un op\xC3\xA9rateur du type)");
                    const std::int64_t n = arg->v.asInteger();
                    std::string list;
                    for (const auto& [vname, number] : values) {
                        if (number == n) return RV{Value::integer(Type::DInt, n), nullptr};
                        list += (list.empty() ? "" : ", ") + vname + " = " + std::to_string(number);
                    }
                    return dialectError(std::to_string(n) + " n'est pas une valeur de " + callee.substr(3) + " (" + list + ")");
                }
                if (!isStandardConversionTarget(u)) {
                    // laisse la main aux fonctions de l'automate (INT_TO_REAL...).
                } else {
                    Value out;
                    if (standardConversion(u, arg->v, out)) return RV{out, nullptr};
                }
            }
        }
        // Le reste : comme avant (fonctions standard, IHM_..., blocs de l'automate).
        auto v = call(e);
        if (!v) return core::Err<core::Error>(v.error());
        return RV{*v, nullptr};
    }

    static bool isStandardConversionTarget(const std::string& u) {
        static const char* kTargets[] = {"TO_STRING", "TO_REAL", "TO_LREAL", "TO_BOOL", "TO_INT", "TO_DINT", "TO_UINT",
                                         "TO_UDINT", "TO_SINT", "TO_USINT", "TO_LINT", "TO_ULINT", "TO_WORD", "TO_DWORD",
                                         "TO_BYTE", "TO_TIME"};
        for (const char* t : kTargets)
            if (u == t) return true;
        return false;
    }

    // Les arguments d'un appel, rapproches des parametres.
    core::Result<RV> callDeclared(const Function& f, const Expr& e, bool isolated) {
        std::vector<int> slot(f.params.size(), -1);
        std::vector<Bound> bound(f.params.size());
        std::size_t next = 0;
        for (std::size_t a = 0; a < e.arguments.size() + e.outputs.size(); ++a) {
            const bool output = a >= e.arguments.size();
            const auto& [pname, expr] = output ? e.outputs[a - e.arguments.size()] : e.arguments[a];
            std::size_t k = f.params.size();
            if (pname.empty()) {
                while (next < f.params.size() && slot[next] >= 0) ++next;
                if (next >= f.params.size())
                    return dialectError(f.name + " : trop d'arguments (" + std::to_string(e.arguments.size()) + " pour "
                                        + std::to_string(f.params.size()) + ")");
                k = next++;
            } else {
                for (std::size_t j = 0; j < f.params.size(); ++j)
                    if (upperOf(f.params[j].name) == upperOf(pname)) k = j;
                if (k == f.params.size()) return dialectError(f.name + " : param\xC3\xA8tre inconnu : " + pname);
                if (slot[k] >= 0) return dialectError(f.name + " : param\xC3\xA8tre donn\xC3\xA9 deux fois : " + pname);
            }
            slot[k] = static_cast<int>(a);
            const auto& param = f.params[k];
            const bool byPlace = param.mode != VarDecl::Mode::Input || output
                              || (param.type && param.type->kind == TypeDesc::Kind::Ref);
            if (byPlace) {
                // VAR_IN_OUT, VAR_OUTPUT : une variable ; REF_TO : une variable (sa
                // reference est prise) ou une reference.
                if (param.mode == VarDecl::Mode::Input) {
                    auto v = rich(*expr);
                    if (!v) return v;
                    if (isIndirect(v->o) || (v->o && v->o->type && v->o->type->kind == TypeDesc::Kind::Pointer)) {
                        bound[k] = Bound{*v, {}, false};
                        continue;
                    }
                }
                if (!isDesignator(*expr))
                    return dialectError(f.name + " : '" + param.name + "' est pass\xC3\xA9 par r\xC3\xA9" "f\xC3\xA9rence : il faut une variable, pas une valeur calcul\xC3\xA9" "e (r\xC3\xA9" "f\xC3\xA9rence vers un temporaire)");
                auto p = locate(*expr, true);
                if (!p) return core::Err<core::Error>(p.error());
                if (p->temp)
                    return dialectError(f.name + " : '" + param.name + "' est pass\xC3\xA9 par r\xC3\xA9" "f\xC3\xA9rence : r\xC3\xA9" "f\xC3\xA9rence vers un temporaire");
                if (p->constant && param.mode != VarDecl::Mode::Input)
                    return dialectError(f.name + " : '" + param.name + "' modifierait une constante");
                bound[k] = Bound{{}, *p, true};
                continue;
            }
            Value lit;
            if (bareName(*expr) && bareEnum(enumNameOf(param.type), *expr, lit)) {      // 1.10 : Suivant(Auto)
                bound[k] = Bound{RV{lit, nullptr}, {}, false};
                continue;
            }
            // 1.10 : une instance de symbole passee a un parametre de ce symbole : par son nom.
            if (param.type && param.type->kind == TypeDesc::Kind::Struct && isDesignator(*expr)) {
                std::string where;
                if (!symbolTypeOf(*expr, &where).empty()) {
                    bound[k] = Bound{{}, Place{nullptr, where, true, false}, true};
                    continue;
                }
            }
            auto v = rich(*expr);
            if (!v) return v;
            bound[k] = Bound{*v, {}, false};
        }
        // Les parametres non donnes : leur valeur initiale ; un VAR_IN_OUT manque.
        for (std::size_t k = 0; k < f.params.size(); ++k)
            if (slot[k] < 0 && f.params[k].mode == VarDecl::Mode::InOut)
                return dialectError(f.name + " : le param\xC3\xA8tre VAR_IN_OUT '" + f.params[k].name + "' n'est pas donn\xC3\xA9");
        std::vector<Bound> args;
        std::vector<bool> given;
        for (std::size_t k = 0; k < f.params.size(); ++k) {
            args.push_back(bound[k]);
            given.push_back(slot[k] >= 0);
        }
        return runFunction(f, args, e.line, isolated, &given);
    }

    static bool isDesignator(const Expr& e) {
        switch (e.kind) {
            case Expr::Kind::Reference:
            case Expr::Kind::Member:
            case Expr::Kind::Index:
            case Expr::Kind::Deref:
                return true;
            default:
                return false;
        }
    }

    // Executer une fonction du dialecte : un cadre neuf (parametres, locales,
    // la valeur rendue), son corps, puis les sorties rendues.
    core::Result<RV> runFunction(const Function& f, const std::vector<Bound>& args, std::uint32_t line, bool isolated,
                                 const std::vector<bool>* given = nullptr) {
        if (depth_ >= limits_.maxCallDepth) {
            const std::string why = "appels imbriqu\xC3\xA9s trop profonds : plus de " + std::to_string(limits_.maxCallDepth)
                                  + " (r\xC3\xA9" "cursion sans fin dans " + f.name + " ?)";
            fail(line, why);
            return dialectError(why);
        }
        Frame frame;
        frame.fn = &f;
        frame.seesScript = !isolated;
        frame.scopes.emplace_back();
        auto& names = frame.scopes.back().names;
        std::vector<std::pair<Place, ObjRef>> outputs;     // VAR_OUTPUT : rendu apres l'appel
        for (std::size_t k = 0; k < f.params.size(); ++k) {
            const auto& param = f.params[k];
            const bool isGiven = given ? (*given)[k] : k < args.size();
            const Bound* b = k < args.size() ? &args[k] : nullptr;
            if (param.mode == VarDecl::Mode::InOut) {
                if (!isGiven || !b || !b->isPlace)
                    return dialectError(f.name + " : le param\xC3\xA8tre VAR_IN_OUT '" + param.name + "' attend une variable");
                // Le type : le meme (ou un nombre pour un nombre).
                if (b->place.obj && param.type && b->place.obj->type) {
                    auto want = resolve(param.type);
                    if (!want) return core::Err<core::Error>(want.error());
                    if (!compatibleTarget(**want, *b->place.obj->type))
                        return dialectError(f.name + " : '" + param.name + "' attend un " + (*want)->text() + ", pas un "
                                            + b->place.obj->type->text());
                }
                names[upperOf(param.name)] = b->place;
                continue;
            }
            auto obj = instantiate(param.type);
            if (!obj && b && b->isPlace && param.mode == VarDecl::Mode::Input) {
                // 1.10 : l'operande d'un symbole (une instance, par son nom) : A.Debit lit son argument.
                names[upperOf(param.name)] = Place{nullptr, b->place.name, true, false};
                continue;
            }
            if (!obj) return core::Err<core::Error>(obj.error());
            if (param.type && param.type->kind == TypeDesc::Kind::Ref && b && b->isPlace) {
                // REF_TO T : la reference de la variable passee.
                (*obj)->bound = true;
                if (b->place.obj) (*obj)->target = b->place.obj;
                else (*obj)->targetName = env_.canonicalName(b->place.name);
            } else if (param.mode == VarDecl::Mode::Output) {
                if (b && b->isPlace) outputs.emplace_back(b->place, *obj);
            } else if (isGiven && b) {
                std::string why;
                RV value = b->value;
                if (b->isPlace) {
                    auto r = readPlace(b->place);
                    if (!r) return r;
                    value = *r;
                }
                if (!store(**obj, value, &why)) return dialectError(f.name + " : '" + param.name + "' : " + why);
            }
            names[upperOf(param.name)] = Place{*obj, {}, param.constant, false};
        }
        ObjRef result;
        if (f.result) {
            auto r = instantiate(f.result);
            if (!r) return core::Err<core::Error>(r.error());
            result = *r;
            names[upperOf(f.name)] = Place{result, {}, false, false};
        }
        frame.result = result;
        frames_.push_back(std::move(frame));
        // Les parametres non donnes et les locales : leur valeur initiale, dans le cadre neuf.
        bool ok = true;
        for (std::size_t k = 0; k < f.params.size() && ok; ++k) {
            const auto& param = f.params[k];
            const bool isGiven = given ? (*given)[k] : k < args.size();
            if (!isGiven && param.initial && param.mode == VarDecl::Mode::Input)
                ok = initialize(*frames_.back().scopes.front().names[upperOf(param.name)].obj, *param.initial, param.line);
        }
        for (const auto& l : f.locals) {
            if (!ok) break;
            auto obj = instantiate(l.type);
            if (!obj) {
                fail(l.line, l.name + " : " + obj.error().message());
                ok = false;
                break;
            }
            if (l.initial) ok = initialize(**obj, *l.initial, l.line);
            frames_.back().scopes.front().names[upperOf(l.name)] = Place{*obj, {}, l.constant, false};
        }
        ++depth_;
        const std::string savedLabel = calleeLabel_;
        const std::uint32_t savedLine = calleeLine_;
        if (isolated) {
            env_.enterFunction(f.name);
            if (calleeLabel_.empty()) calleeLine_ = line;     // la ligne de l'appel, dans le script
            calleeLabel_ = f.name;
        }
        Flow flow{};
        if (ok) flow = sequence(f.body, f.line);
        if (isolated) {
            env_.leaveFunction(f.name);
            calleeLabel_ = savedLabel;
            calleeLine_ = savedLine;
        }
        --depth_;
        frames_.pop_back();
        if (!ok || flow.kind == Flow::Kind::Aborted || aborted_)
            return dialectError(f.name + " : l'appel a \xC3\xA9" "chou\xC3\xA9");
        for (const auto& [place, obj] : outputs) {
            std::string why;
            if (!writePlace(place, obj->type && obj->type->kind == TypeDesc::Kind::Scalar ? RV{obj->value, nullptr} : RV{{}, obj}, &why))
                return dialectError(f.name + " : sortie : " + why);
        }
        if (!result) return RV{Value::boolean(true), nullptr};
        if (result->type && result->type->kind == TypeDesc::Kind::Scalar) return RV{result->value, nullptr};
        return RV{{}, result};
    }

    // RETURN expr : la valeur rendue par la fonction en cours.
    Flow returnValue(const Stmt& s) {
        if (frames_.size() < 2 || !frames_.back().fn) {
            fail(s.line, "RETURN avec une valeur : seulement dans une fonction");
            return {Flow::Kind::Aborted};
        }
        auto& top = frames_.back();
        if (!top.result) {
            fail(s.line, top.fn->name + " ne rend pas de valeur (pas de type de retour) : RETURN seul");
            return {Flow::Kind::Aborted};
        }
        // 1.10 (decision 15) : RETURN Auto (le nom seul, du type rendu).
        Value lit;
        core::Result<RV> v = bareName(*s.value) && bareEnum(enumNameOf(top.fn->result), *s.value, lit)
                                 ? core::Result<RV>(RV{lit, nullptr}) : rich(*s.value);
        if (!v) {
            if (!aborted_) fail(s.line, v.error().message());
            return {Flow::Kind::Aborted};
        }
        std::string why;
        if (!store(*top.result, *v, &why)) {
            fail(s.line, top.fn->name + " : retour du mauvais type : " + why);
            return {Flow::Kind::Aborted};
        }
        return {Flow::Kind::Return};
    }

    // Une variable de boucle FOR : locale si elle l'est, sinon comme avant.
    bool assignName(const std::string& name, const Value& v, std::uint32_t line) {
        if (auto* p = findLocal(name)) {
            std::string why;
            if (!writePlace(*p, RV{v, nullptr}, &why)) {
                fail(line, why);
                return false;
            }
            return true;
        }
        env_.write(name, v);
        return true;
    }

    // L'affectation du dialecte : x := e, x += e (operateur du projet ou calcul).
    Flow assignRich(const Stmt& s) {
        // Deux variables composees de l'environnement : la copie en bloc, comme avant.
        if (s.extraTargets.empty() && s.compound.empty() && isDesignator(*s.value) && !needsLocate(*s.value)
            && !needsLocate(*s.target)) {
            auto from = qualify(*s.value);
            auto to   = qualify(*s.target);
            if (from && to && !env_.exists(*from) && env_.assignAggregate(*to, *from)) return {};
        }
        RV value;
        if (!s.compound.empty()) {
            auto cur = rich(*s.target);
            if (!cur) {
                if (!aborted_) fail(s.line, cur.error().message());
                return {Flow::Kind::Aborted};
            }
            auto rhs = rich(*s.value);
            if (!rhs) {
                if (!aborted_) fail(s.line, rhs.error().message());
                return {Flow::Kind::Aborted};
            }
            // Un operateur += du projet : il modifie la variable lui-meme.
            if (cur->o) {
                if (auto f = projectOperator(s.compound, typeNameOf(*cur), typeNameOf(*rhs), nullptr)) {
                    auto place = locate(*s.target, false);
                    if (!place) { fail(s.line, place.error().message()); return {Flow::Kind::Aborted}; }
                    auto r = runFunction(*f, {Bound{{}, *place, true}, Bound{*rhs, {}, false}}, s.line, true);
                    if (!r) { if (!aborted_) fail(s.line, r.error().message()); return {Flow::Kind::Aborted}; }
                    return {};
                }
            }
            const std::string op = s.compound.substr(0, 1);
            Expr node;
            node.kind = Expr::Kind::Binary;
            node.op = op;
            node.line = s.line;
            auto combined = combineRich(node, op, *cur, *rhs);
            if (!combined) {
                if (!aborted_) fail(s.line, combined.error().message());
                return {Flow::Kind::Aborted};
            }
            value = *combined;
        } else {
            // 1.10 (decision 15) : m := Auto (le nom seul, du type de la cible).
            Value lit;
            if (bareName(*s.value) && !findLocal(s.value->name) && !env_.exists(s.value->name)
                && bareEnum(enumTypeOf(*s.target), *s.value, lit)) {
                value = RV{lit, nullptr};
            } else {
                auto v = rich(*s.value);
                if (!v) {
                    if (!aborted_) fail(s.line, v.error().message());
                    return {Flow::Kind::Aborted};
                }
                value = *v;
            }
        }
        // Un objet rendu par la valeur meme (b := a) : copie, pour que a et b restent deux.
        auto storeInto = [&](const Expr& target) {
            auto place = locate(target, true);
            if (!place) {
                fail(s.line, place.error().message());
                return false;
            }
            if (place->temp) {
                fail(s.line, "une valeur calcul\xC3\xA9" "e ne s'affecte pas");
                return false;
            }
            std::string why;
            if (!writePlace(*place, value, &why)) {
                fail(s.line, why);
                return false;
            }
            return true;
        };
        if (!storeInto(*s.target)) return {Flow::Kind::Aborted};
        for (const auto& extra : s.extraTargets)
            if (!storeInto(*extra)) return {Flow::Kind::Aborted};
        return {};
    }

    // FOR EACH k, v IN m / FOR EACH x IN t : `v`, `x` designent la case (modifiable).
    Flow forEach(const Stmt& s) {
        auto where = locate(*s.from, false);
        if (!where) {
            fail(s.line, where.error().message());
            return {Flow::Kind::Aborted};
        }
        Place p = *where;
        while (p.obj && isIndirect(p.obj)) {
            auto t = follow(p.obj);
            if (!t) { fail(s.line, t.error().message()); return {Flow::Kind::Aborted}; }
            p = *t;
        }
        const bool two = !s.loopValue.empty();
        std::uint32_t iterations = 0;
        const auto runBody = [&](Place key, Place value) -> int {   // 0 continuer, 1 sortir, 2 rendre le flot
            if (++iterations > limits_.maxIterationsPerLoop) {
                fail(s.line, "FOR loop ran more than " + std::to_string(limits_.maxIterationsPerLoop) + " times");
                return 2;
            }
            auto& scope = frames_.back().scopes.emplace_back();
            if (two) {
                scope.names[upperOf(s.loopVariable)] = std::move(key);
                scope.names[upperOf(s.loopValue)] = std::move(value);
            } else {
                scope.names[upperOf(s.loopVariable)] = std::move(value);
            }
            lastFlow_ = sequence(s.body, s.line);
            frames_.back().scopes.pop_back();
            if (lastFlow_.kind == Flow::Kind::Exit) return 1;
            if (lastFlow_.kind != Flow::Kind::Normal) return 2;
            return 0;
        };
        const auto scalar = [&](Value v, const std::string& typeName = {}) {
            auto o = makeObj(scalarType(v.type(), typeName));
            o->value = std::move(v);
            return Place{o, {}, true, true};
        };
        if (!p.obj) {
            // Un tableau de l'environnement (une variable IHM) : case par case, par son nom.
            // 1.10 (decision 15) : FOR EACH v IN T_MODE : les valeurs, dans l'ordre de declaration.
            std::vector<std::pair<std::string, std::int64_t>> values;
            if (enumValues(p.name, values)) {
                for (const auto& [vname, number] : values) {
                    (void)vname;
                    const int r = runBody(scalar(Value::integer(Type::DInt, number), p.name), scalar(Value::integer(Type::DInt, number), p.name));
                    if (r == 1) break;
                    if (r == 2) return lastFlow_.kind == Flow::Kind::Normal ? Flow{Flow::Kind::Aborted} : lastFlow_;
                }
                return {};
            }
            const std::string text = env_.declaredType(p.name);
            auto t = text.empty() ? core::Result<TypeRef>(dialectError("x")) : typeFromText(text);
            if (!t || (*t)->kind != TypeDesc::Kind::Array) {
                fail(s.line, "FOR EACH : '" + p.name + "' n'est ni une MAP ni un tableau");
                return {Flow::Kind::Aborted};
            }
            for (std::int64_t k = 0; k < (*t)->count(); ++k) {
                const int r = runBody(scalar(Value::integer(Type::DInt, k + ((*t)->bounds.size() == 1 ? (*t)->bounds[0].first : 0))),
                                      Place{nullptr, p.name + indexText(**t, static_cast<std::size_t>(k)), false, false});
                if (r == 1) break;
                if (r == 2) return lastFlow_.kind == Flow::Kind::Normal ? Flow{Flow::Kind::Aborted} : lastFlow_;
            }
            return {};
        }
        const auto& type = *p.obj->type;
        if (type.kind == TypeDesc::Kind::Map) {
            // Les cles d'abord (ordre croissant) : le corps peut retirer ou ajouter.
            if (isInteger(type.key)) {
                std::vector<std::int64_t> keys;
                for (const auto& kv : p.obj->intKeys) keys.push_back(kv.first);
                for (const auto k : keys) {
                    const auto it = p.obj->intKeys.find(k);
                    if (it == p.obj->intKeys.end()) continue;
                    const int r = runBody(scalar(Value::integer(type.key, k), type.name), two ? Place{it->second, {}, p.constant, false}
                                                                                             : scalar(Value::integer(type.key, k), type.name));
                    if (r == 1) break;
                    if (r == 2) return lastFlow_.kind == Flow::Kind::Normal ? Flow{Flow::Kind::Aborted} : lastFlow_;
                }
            } else {
                std::vector<std::string> keys;
                for (const auto& kv : p.obj->textKeys) keys.push_back(kv.first);
                for (const auto& k : keys) {
                    const auto it = p.obj->textKeys.find(k);
                    if (it == p.obj->textKeys.end()) continue;
                    const int r = runBody(scalar(Value::text(k)), two ? Place{it->second, {}, p.constant, false} : scalar(Value::text(k)));
                    if (r == 1) break;
                    if (r == 2) return lastFlow_.kind == Flow::Kind::Normal ? Flow{Flow::Kind::Aborted} : lastFlow_;
                }
            }
            return {};
        }
        if (type.kind == TypeDesc::Kind::Array) {
            const auto items = p.obj->items;          // les cases restent (taille fixe)
            for (std::size_t k = 0; k < items.size(); ++k) {
                const std::int64_t index = static_cast<std::int64_t>(k) + (type.bounds.size() == 1 ? type.bounds[0].first : 0);
                const int r = runBody(scalar(Value::integer(Type::DInt, index)), Place{items[k], {}, p.constant, false});
                if (r == 1) break;
                if (r == 2) return lastFlow_.kind == Flow::Kind::Normal ? Flow{Flow::Kind::Aborted} : lastFlow_;
            }
            return {};
        }
        fail(s.line, "FOR EACH : un " + type.text() + " n'est ni une MAP ni un tableau");
        return {Flow::Kind::Aborted};
    }

    // ---- les fonctions du dialecte (MAP_..., REF, ADR, bornes) ---------------
    std::optional<core::Result<RV>> dialectBuiltin(const std::string& u, const Expr& e) {
        static const char* kNames[] = {"MAP_HAS", "MAP_GET", "MAP_REMOVE", "MAP_SIZE", "MAP_CLEAR", "MAP_KEYS",
                                       "MAP_BEGIN", "MAP_NEXT", "MAP_END", "REF", "ADR", "LOWER_BOUND", "UPPER_BOUND", "SIZEOF",
                                       "TO_UPPER", "TO_LOWER"};
        bool known = false;
        for (const char* n : kNames) if (u == n) known = true;
        if (!known) return std::nullopt;
        const auto argc = e.arguments.size();
        // 1.10 : TO_UPPER(s), TO_LOWER(s) (le fromString d'une enumeration compare sans casse).
        if (u == "TO_UPPER" || u == "TO_LOWER") {
            if (argc != 1) return core::Result<RV>(dialectError("mauvais nombre d'arguments : " + u + "(texte)"));
            auto v = evaluate(*e.arguments[0].second);
            if (!v) return core::Result<RV>(core::Err<core::Error>(v.error()));
            std::string t = v->type() == Type::String ? v->asString() : v->display();
            for (auto& c : t)
                c = static_cast<char>(u == "TO_UPPER" ? std::toupper(static_cast<unsigned char>(c)) : std::tolower(static_cast<unsigned char>(c)));
            return core::Result<RV>(RV{Value::text(t), nullptr});
        }
        const auto want = [&](std::size_t lo, std::size_t hi) -> bool { return argc >= lo && argc <= hi; };
        const auto bad = [&](std::string_view sig) {
            return core::Result<RV>(dialectError("mauvais nombre d'arguments : " + std::string(sig)));
        };
        // Le premier argument designe (sans copie) : une MAP, un tableau, un iterateur.
        const auto subject = [&]() -> core::Result<ObjRef> {
            const Expr& a = *e.arguments[0].second;
            Place p;
            if (isDesignator(a)) {
                auto where = locate(a, false);
                if (!where) return core::Err<core::Error>(where.error());
                p = *where;
            } else {
                auto v = rich(a);
                if (!v) return core::Err<core::Error>(v.error());
                if (!v->o) return dialectError(u + " : une MAP, un tableau ou un it\xC3\xA9rateur est attendu");
                p.obj = v->o;
            }
            while (p.obj && isIndirect(p.obj)) {
                auto t = follow(p.obj);
                if (!t) return core::Err<core::Error>(t.error());
                p = *t;
            }
            if (!p.obj) {
                auto m = materialize(p.name);
                if (!m) return m;
                return *m;
            }
            return p.obj;
        };
        const auto needMap = [&](const ObjRef& o) -> bool { return o && o->type && o->type->kind == TypeDesc::Kind::Map; };
        if (u == "REF" || u == "ADR") {
            if (!want(1, 1)) return bad(u + "(variable)");
            const Expr& a = *e.arguments[0].second;
            if (!isDesignator(a))
                return core::Result<RV>(dialectError(u + " : il faut une variable, pas une valeur calcul\xC3\xA9" "e (r\xC3\xA9" "f\xC3\xA9rence vers un temporaire)"));
            auto where = locate(a, false);
            if (!where) return core::Result<RV>(core::Err<core::Error>(where.error()));
            if (where->temp)
                return core::Result<RV>(dialectError(u + " : r\xC3\xA9" "f\xC3\xA9rence vers un temporaire (une valeur calcul\xC3\xA9" "e)"));
            auto t = std::make_shared<TypeDesc>();
            t->kind = u == "REF" ? TypeDesc::Kind::Ref : TypeDesc::Kind::Pointer;
            auto o = std::make_shared<Obj>();
            o->bound = true;
            if (where->obj) {
                t->element = where->obj->type;
                o->target = where->obj;
            } else {
                o->targetName = env_.canonicalName(where->name);
                Value probe;
                if (env_.read(where->name, probe)) {
                    t->element = scalarType(probe.type());
                } else {
                    const std::string text = env_.declaredType(where->name);
                    auto dt = text.empty() ? core::Result<TypeRef>(dialectError("'" + where->name + "' is not declared"))
                                           : typeFromText(text);
                    if (!dt) return core::Result<RV>(core::Err<core::Error>(dt.error()));
                    t->element = *dt;
                }
            }
            o->type = t;
            return core::Result<RV>(RV{{}, o});
        }
        if (argc == 0) return bad(u + "(...)");
        auto obj = subject();
        if (!obj) return core::Result<RV>(core::Err<core::Error>(obj.error()));
        const ObjRef o = *obj;
        if (u == "LOWER_BOUND" || u == "UPPER_BOUND" || u == "SIZEOF") {
            if (!o->type || o->type->kind != TypeDesc::Kind::Array) {
                if (u == "SIZEOF" && needMap(o))
                    return core::Result<RV>(RV{Value::integer(Type::DInt, static_cast<std::int64_t>(o->textKeys.size() + o->intKeys.size())), nullptr});
                return core::Result<RV>(dialectError(u + " : un tableau est attendu"));
            }
            if (u == "SIZEOF") {
                if (!want(1, 1)) return bad("SIZEOF(tableau)");
                return core::Result<RV>(RV{Value::integer(Type::DInt, o->type->count()), nullptr});
            }
            if (!want(1, 2)) return bad(u + "(tableau, dimension)");
            std::int64_t d = 1;
            if (argc == 2) {
                auto dv = evaluate(*e.arguments[1].second);
                if (!dv) return core::Result<RV>(core::Err<core::Error>(dv.error()));
                d = dv->asInteger();
            }
            if (d < 1 || d > static_cast<std::int64_t>(o->type->bounds.size()))
                return core::Result<RV>(dialectError(u + " : dimension " + std::to_string(d) + " (le tableau en a "
                                                     + std::to_string(o->type->bounds.size()) + ")"));
            const auto& b = o->type->bounds[static_cast<std::size_t>(d - 1)];
            return core::Result<RV>(RV{Value::integer(Type::DInt, u == "LOWER_BOUND" ? b.first : b.second), nullptr});
        }
        if (u == "MAP_NEXT" || u == "MAP_END") {
            if (!want(1, 1)) return bad(u + "(it)");
            if (!o->type || o->type->kind != TypeDesc::Kind::Iterator)
                return core::Result<RV>(dialectError(u + " : un it\xC3\xA9rateur est attendu (it := MAP_BEGIN(m))"));
            if (u == "MAP_END") return core::Result<RV>(RV{Value::boolean(o->atEnd || o->map.expired()), nullptr});
            auto map = o->map.lock();
            if (!map || o->atEnd) return core::Result<RV>(RV{Value::boolean(false), nullptr});
            if (isInteger(map->type->key)) {
                const auto it = map->intKeys.upper_bound(o->intKey);
                o->atEnd = it == map->intKeys.end();
                if (!o->atEnd) o->intKey = it->first;
            } else {
                const auto it = map->textKeys.upper_bound(o->textKey);
                o->atEnd = it == map->textKeys.end();
                if (!o->atEnd) o->textKey = it->first;
            }
            return core::Result<RV>(RV{Value::boolean(!o->atEnd), nullptr});
        }
        if (!needMap(o)) return core::Result<RV>(dialectError(u + " : une MAP est attendue, pas un " + (o->type ? o->type->text() : std::string("?"))));
        const bool textual = !isInteger(o->type->key);
        if (u == "MAP_SIZE") {
            if (!want(1, 1)) return bad("MAP_SIZE(m)");
            return core::Result<RV>(RV{Value::integer(Type::DInt, static_cast<std::int64_t>(o->textKeys.size() + o->intKeys.size())), nullptr});
        }
        if (u == "MAP_CLEAR") {
            if (!want(1, 1)) return bad("MAP_CLEAR(m)");
            o->textKeys.clear();
            o->intKeys.clear();
            return core::Result<RV>(RV{Value::boolean(true), nullptr});
        }
        if (u == "MAP_KEYS") {
            if (!want(1, 1)) return bad("MAP_KEYS(m)");
            auto t = std::make_shared<TypeDesc>();
            t->kind = TypeDesc::Kind::Array;
            const auto n = static_cast<std::int64_t>(o->textKeys.size() + o->intKeys.size());
            t->bounds.emplace_back(0, n - 1);
            t->element = scalarType(textual ? Type::String : o->type->key);
            auto arr = makeObj(t);
            std::size_t k = 0;
            for (const auto& kv : o->textKeys) arr->items[k++]->value = Value::text(kv.first);
            for (const auto& kv : o->intKeys) arr->items[k++]->value = Value::integer(o->type->key, kv.first);
            return core::Result<RV>(RV{{}, arr});
        }
        if (u == "MAP_BEGIN") {
            if (!want(1, 1)) return bad("MAP_BEGIN(m)");
            auto t = std::make_shared<TypeDesc>();
            t->kind = TypeDesc::Kind::Iterator;
            auto it = makeObj(t);
            it->map = o;
            if (textual) {
                it->atEnd = o->textKeys.empty();
                if (!it->atEnd) it->textKey = o->textKeys.begin()->first;
            } else {
                it->atEnd = o->intKeys.empty();
                if (!it->atEnd) it->intKey = o->intKeys.begin()->first;
            }
            return core::Result<RV>(RV{{}, it});
        }
        // MAP_HAS(m, k), MAP_REMOVE(m, k), MAP_GET(m, k, defaut)
        if (u == "MAP_GET" ? !want(3, 3) : !want(2, 2))
            return bad(u == "MAP_GET" ? "MAP_GET(m, cl\xC3\xA9, d\xC3\xA9" "faut)" : u + "(m, cl\xC3\xA9)");
        auto k = rich(*e.arguments[1].second);
        if (!k) return core::Result<RV>(core::Err<core::Error>(k.error()));
        std::string text;
        std::int64_t integer = 0;
        auto isText = mapKey(*o, *k, text, integer);
        if (!isText) return core::Result<RV>(core::Err<core::Error>(isText.error()));
        ObjRef entry;
        if (*isText) {
            if (const auto it = o->textKeys.find(text); it != o->textKeys.end()) entry = it->second;
        } else if (const auto it = o->intKeys.find(integer); it != o->intKeys.end()) {
            entry = it->second;
        }
        if (u == "MAP_HAS") return core::Result<RV>(RV{Value::boolean(entry != nullptr), nullptr});
        if (u == "MAP_REMOVE") {
            if (*isText) o->textKeys.erase(text);
            else o->intKeys.erase(integer);
            return core::Result<RV>(RV{Value::boolean(entry != nullptr), nullptr});
        }
        // MAP_GET
        if (entry) return core::Result<RV>(readPlace(Place{entry, {}, false, false}));
        return core::Result<RV>(rich(*e.arguments[2].second));
    }

    bool                                    dialect_{false};
    std::vector<Frame>                      frames_;
    Locals                                  ownLocals_;
    std::uint32_t                           depth_{0};
    Flow                                    lastFlow_{};
    std::map<std::string, TypeRef>          structs_;
    std::map<std::string, TypeRef>          typeTexts_;
    std::string                             calleeLabel_;      // la fonction du projet en cours (ses fautes)
    std::uint32_t                           calleeLine_{0};

private:
    Environment&  env_;
    RunLimits     limits_;
    std::string   section_;
    std::uint32_t statements_{0};
    bool          aborted_{false};
    // Lot API 8 : la section executee (ses lignes, ses points d'arret) ; une
    // lecture au passage (probing_) ne dit rien et n'ecrit rien.
    const Program*                   program_{nullptr};
    const std::vector<std::uint8_t>* breaks_{&noBreakLines()};
    bool                             probing_{false};
};

} // namespace

core::Result<std::shared_ptr<Program>> parse(std::string_view source, std::string sectionName) {
    Lexer lexer(source, sectionName);
    auto tokens = lexer.run();
    if (!tokens) return core::Err<core::Error>(tokens.error());
    Parser parser(std::move(*tokens), std::move(sectionName));
    return parser.run();
}

// ---- 1.10 : le dialecte IHM ----
core::Result<std::shared_ptr<Program>> parse(std::string_view source, std::string sectionName, const ParseOptions& options) {
    Lexer lexer(source, sectionName, options.hmiDialect);
    auto tokens = lexer.run();
    if (!tokens) return core::Err<core::Error>(tokens.error());
    Parser parser(std::move(*tokens), std::move(sectionName), options.hmiDialect);
    return parser.run();
}

bool isDialect(const Program& program) noexcept { return program.dialect; }

namespace {
core::Result<TypeRef> parseTypeText(std::string_view text) {
    Lexer lexer(text, "type", true);
    auto tokens = lexer.run();
    if (!tokens) return core::Err<core::Error>(tokens.error());
    Parser parser(std::move(*tokens), "type", true);
    return parser.soleType();
}
} // namespace

core::Result<std::shared_ptr<const Function>> parseFunction(std::string_view text, std::string name) {
    Lexer lexer(text, name, true);
    auto tokens = lexer.run();
    if (!tokens) return core::Err<core::Error>(tokens.error());
    Parser parser(std::move(*tokens), std::move(name), true);
    auto f = parser.soleFunction();
    if (!f) return core::Err<core::Error>(f.error());
    return std::shared_ptr<const Function>(*f);
}

const std::string& functionName(const Function& f) noexcept { return f.name; }

bool functionIsSimple(const Function& f) noexcept {
    const auto scalar = [](const TypeRef& t) { return !t || t->kind == TypeDesc::Kind::Scalar; };
    if (!scalar(f.result)) return false;
    for (const auto& p : f.params)
        if (p.mode != VarDecl::Mode::Input || !scalar(p.type)) return false;
    for (const auto& l : f.locals)
        if (!scalar(l.type)) return false;
    return true;
}

std::string functionSignature(const Function& f) {
    std::string s = f.name + "(";
    for (std::size_t k = 0; k < f.params.size(); ++k) {
        const auto& p = f.params[k];
        s += (k ? "; " : "");
        if (p.mode == VarDecl::Mode::InOut) s += "VAR_IN_OUT ";
        else if (p.mode == VarDecl::Mode::Output) s += "VAR_OUTPUT ";
        s += p.name + " : " + (p.type ? p.type->text() : std::string("?"));
    }
    s += ")";
    if (f.result) s += " : " + f.result->text();
    return s;
}

RunResult execute(const Program& program, Environment& env, const RunLimits& limits) {
    Runner runner(env, limits, program.name);
    return runner.run(program);
}

// ---- Lot API 8 : ce que le runtime demande a une section preparee ----
void setProgramTag(Program& program, std::uint32_t tag) noexcept { program.tag = tag; }
std::uint32_t programTag(const Program& program) noexcept { return program.tag; }
const std::string& programName(const Program& program) noexcept { return program.name; }

std::vector<std::uint32_t> statementLines(const Program& program) {
    std::vector<std::uint32_t> out;
    statementLinesOf(program.body, out);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void setBreakLines(Program& program, const std::vector<std::uint32_t>& lines) {
    program.breakLines.clear();
    if (lines.empty()) {
        program.breakLines.shrink_to_fit();
        return;
    }
    program.breakLines.assign(static_cast<std::size_t>(*std::max_element(lines.begin(), lines.end())) + 1, 0);
    for (const auto l : lines) program.breakLines[l] = 1;
}

core::Result<std::shared_ptr<const Expression>> parseExpression(std::string_view source) {
    Lexer lexer(source, "condition");
    auto tokens = lexer.run();
    if (!tokens) return core::Err<core::Error>(tokens.error());
    Parser parser(std::move(*tokens), "condition");
    auto root = parser.soleExpression();
    if (!root) return core::Err<core::Error>(root.error());
    auto out = std::make_shared<Expression>();
    out->root = *root;
    return std::shared_ptr<const Expression>(std::move(out));
}

core::Result<Value> evaluate(const Expression& expression, Environment& env) {
    if (!expression.root) return core::fail(core::ErrorCode::InvalidArgument, "empty expression");
    Runner runner(env, RunLimits{}, "condition");
    return runner.evaluateQuietly(*expression.root);
}

core::Result<Value> evaluateExpression(std::string_view source, Environment& env) {
    auto program = parse("__result := " + std::string(source) + ";", "expression");
    if (!program) return core::Err<core::Error>(program.error());
    (void)execute(**program, env);      // lot 16 : le resultat est lu dans __result (-Werror : unused-result)
    Value out;
    if (!env.read("__result", out))
        return core::fail(core::ErrorCode::InvalidArgument, "the expression produced nothing");
    return out;
}

std::int64_t monotonicMicros() noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace sim
