#include "Interpreter.hpp"
#include "DialectLib.hpp"   // 1.12.7 : templates, enumerations natives, natives des conteneurs

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <set>
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

                // 1.12.1 (dialecte IHM) : D#2026-10-09, TOD#14:30:00, DT#2026-10-09-14:30:00 (et
                // DATE#, TIME_OF_DAY#, DATE_AND_TIME#).
                if (dialect_ && pos_ + 1 < src_.size() && src_[pos_] == '#' && std::isdigit(static_cast<unsigned char>(src_[pos_ + 1]))) {
                    const Type dateType = upper == "D" || upper == "DATE" ? Type::Date
                                        : upper == "TOD" || upper == "TIME_OF_DAY" ? Type::Tod
                                        : upper == "DT" || upper == "DATE_AND_TIME" ? Type::Dt : Type::Unknown;
                    if (dateType != Type::Unknown) {
                        auto end = pos_ + 1;
                        while (end < src_.size() && (std::isdigit(static_cast<unsigned char>(src_[end])) || src_[end] == '-' || src_[end] == ':'
                                                     || src_[end] == '.'))
                            ++end;
                        while (end > pos_ + 1 && (src_[end - 1] == '-' || src_[end - 1] == ':' || src_[end - 1] == '.')) --end;   // DT#...-T#1h
                        const auto body = src_.substr(pos_ + 1, end - pos_ - 1);
                        std::int64_t ms = 0;
                        if (!parseDateText(dateType, body, ms))
                            return core::fail(core::ErrorCode::InvalidArgument,
                                              "line " + std::to_string(startLine) + ": bad date literal '" + word + "#" + std::string(body) + "'");
                        pos_ = end;
                        out.push_back(Token{Tok::TimeLiteral, "", "", startLine, Value::integer(dateType, ms)});
                        continue;
                    }
                }
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
                    // 1.11.25 : un signe apres le type (INT#-3, REAL#-0.5) - refuse avant
                    // ("bad based literal") ; pas apres une base (16#-1 n'existe pas).
                    const bool typed = !std::all_of(upper.begin(), upper.end(),
                                                    [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
                    if (typed && upper != "T" && upper != "TIME" && pos_ < src_.size() && (src_[pos_] == '-' || src_[pos_] == '+')) ++pos_;
                    while (pos_ < src_.size()
                           && (identChar(src_[pos_]) || src_[pos_] == '.' || src_[pos_] == '_'
                               // 1.5E+3 dans un litteral type reel
                               || ((src_[pos_] == '+' || src_[pos_] == '-') && pos_ > bodyBegin
                                   && (src_[pos_ - 1] == 'e' || src_[pos_ - 1] == 'E') && (upper == "REAL" || upper == "LREAL"))))
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
                    // 1.11.25 : REAL#1.5 (lu 1 : la partie decimale etait perdue), BOOL#1 et BOOL#TRUE
                    // (un BOOL, pas l'entier 1).
                    if (upper == "REAL" || upper == "LREAL") {
                        std::string digits;
                        for (char ch : body) if (ch != '_') digits.push_back(ch);
                        char* end = nullptr;
                        const double d = std::strtod(digits.c_str(), &end);
                        if (digits.empty() || end != digits.c_str() + digits.size())
                            return core::fail(core::ErrorCode::InvalidArgument,
                                              "line " + std::to_string(startLine) + ": bad based literal");
                        out.push_back(Token{Tok::Number, upper, "", startLine, Value::real(d)});
                        continue;
                    }
                    if (upper == "BOOL") {
                        const auto b = upperOf(std::string(body));
                        if (b != "0" && b != "1" && b != "TRUE" && b != "FALSE")
                            return core::fail(core::ErrorCode::InvalidArgument,
                                              "line " + std::to_string(startLine) + ": bad based literal");
                        out.push_back(Token{Tok::Number, upper, "", startLine, Value::boolean(b == "1" || b == "TRUE")});
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
                    const bool negative = !digits.empty() && digits.front() == '-';
                    if (!digits.empty() && (digits.front() == '-' || digits.front() == '+')) digits.erase(0, 1);
                    const auto* first = digits.data();
                    const auto parsed = std::from_chars(first, first + digits.size(), v, base);
                    // 1.11.25 : tout le texte, ou rien (INT#1.5 n'est plus lu 1).
                    if (parsed.ec != std::errc{} || parsed.ptr != first + digits.size())
                        return core::fail(core::ErrorCode::InvalidArgument,
                                          "line " + std::to_string(startLine)
                                              + ": bad based literal");
                    if (negative) v = -v;
                    // 1.11.20 : INT#5 garde le nom de son type (16#FF : un nombre seul).
                    // 1.11.25 : 16#FFFF_FFFF (au-dela de DINT) est un UDINT, pas -1.
                    // 1.12.5 (D16) : au-dela de 32 bits, un LINT (avant : replie sur 32 bits).
                    const Type numberType = v > 4294967295LL || v < -2147483648LL ? Type::LInt
                                          : base != 10 && v > 2147483647LL ? Type::UDInt : Type::DInt;
                    out.push_back(Token{Tok::Number, base == 10 ? upper : std::string{}, "", startLine, Value::integer(numberType, v)});
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
                    // 1.11.25 : 16#FFFF_FFFF (au-dela de DINT) est un UDINT, pas -1.
                    out.push_back(Token{Tok::Number, "", "", startLine,
                                        Value::integer(v > 4294967295LL ? Type::LInt : v > 2147483647LL ? Type::UDInt : Type::DInt, v)});
                    continue;
                }
                // 1.11.25 : un entier au-dela de DINT (3000000000) est un UDINT ; avant, il
                // devenait negatif (-1294967296) dans une expression.
                if (!isReal) {
                    const long long v = std::stoll(digits);
                    out.push_back(Token{Tok::Number, "", digits, startLine,
                                        Value::integer(v > 4294967295LL ? Type::LInt : v > 2147483647LL ? Type::UDInt : Type::DInt, v)});
                    continue;
                }
                out.push_back(Token{Tok::Number, "", digits, startLine, Value::real(std::stod(digits))});
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
            // 1.12.1 : ?? (une valeur de secours) et ? (c ? a : b).
            if (dialect_) {
                for (std::string_view op : {"+=", "-=", "*=", "/=", "??"}) {
                    if (src_.compare(pos_, 2, op) == 0) {
                        out.push_back(Token{Tok::Punct, std::string(op), std::string(op), startLine, {}});
                        pos_ += 2;
                        matched = true;
                        break;
                    }
                }
                if (matched) continue;
                if (c == '^' || c == '?') {
                    out.push_back(Token{Tok::Punct, std::string(1, c), std::string(1, c), startLine, {}});
                    ++pos_;
                    continue;
                }
                if (c == '&') {                                 // 1.12.1 : & s'ecrit pour AND (la fiche le disait)
                    out.push_back(Token{Tok::Keyword, "AND", "&", startLine, {}});
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
        // 1.12.1 (dialecte IHM) : c ? a : b (lhs, rhs, rhs2) ; a ?? b (lhs, rhs) ;
        // x IN [a, b, c..d] (lhs, more : une plage est un Between sans lhs) ;
        // x ENTRE a ET b (lhs, rhs, rhs2).
        Conditional, Coalesce, InList, Between,
        // 1.12.2 (dialecte IHM) : un litteral de collection (les elements dans more) -
        // [a, b, c] (op vide), ['a' := 1, 'b' := 2] (op "MAP" : des Binary ":=" cle, valeur),
        // (a, b) (op "TUPLE") ; 1.12.14 : (Nom := 'A', Niveau := 2.5) (op "STRUCT" : les membres
        // dans arguments, nom et valeur ; leurs valeurs aussi dans more, pour qui parcourt l'arbre).
        Collection,
        // 1.12.19 (dialecte IHM) : LAMBDA (x : REAL) : BOOL => x > 5 ; LAMBDA (...) ... END_LAMBDA (fn : ses
        // parametres, son retour, son corps - la forme courte est un RETURN expr).
        Lambda,
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
    // 1.11.20 : un litteral type (INT#5 : "INT") ; vide : un nombre ecrit seul (5, 16#FF), dont
    // le type se choisit a l'appel d'une surcharge.
    std::string   literalType;
    // 1.12.19 : une LAMBDA (Kind::Lambda) ; les types donnes a un appel generique, Limiter<INT>(...) (sur le
    // nom appele : la Reference en tete de l'appel).
    std::shared_ptr<Function> fn;
    std::vector<std::string>  typeArgs;
};

struct Stmt;
using StmtPtr = std::shared_ptr<Stmt>;

struct CaseArm {
    std::vector<std::pair<std::int64_t, std::int64_t>> ranges;   // inclusive
    std::vector<StmtPtr> body;
    std::vector<std::string> names;   // 1.10 (dialecte IHM) : T_MODE#Auto, lus a l'execution
    std::vector<std::string> texts;   // 1.12.1 (dialecte IHM) : 'Auto': un CASE sur un texte
};

struct Stmt {
    enum class Kind : std::uint8_t {
        Assign, Call, If, Case, For, While, Repeat, Exit, Return,
        ForEach,              // 1.10 (dialecte IHM) : FOR EACH k, v IN m DO ... END_FOR
        // 1.12.1 (dialecte IHM) : CONTINUE ; TRY body CATCH [loopVariable] elseBody END_TRY.
        Continue, Try,
        // 1.13.0 (dialecte IHM) : LEVER('Code' [, 'message']) (`value` : code, `condition` : message) ;
        // RELEVER (reemet l'erreur du CATCH courant).
        Raise, Rethrow,
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
    // 1.12.19 : TEMPLATE <T : ANY_NUM, U> FUNCTION ... - ses types generiques et leur contrainte ("" : aucune) ;
    // T est deduit des arguments a l'appel (ou donne : Limiter<INT>(...)).
    std::vector<std::pair<std::string, std::string>> typeParams;
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
            if (dialect_ && (atWord("FUNCTION") || atWord("TEMPLATE"))) {
                auto f = templatedFunction();
                if (!f) return core::Err<core::Error>(f.error());
                // 1.11.20 : deux fonctions internes du meme nom (des surcharges) si leurs
                // parametres different (nombre, modes, types ecrits) ; le controle (hmi::overload)
                // refuse aussi deux types que le moteur calcule pareil (REAL et LREAL).
                for (const auto& other : program->functions)
                    if (upperOf(other->name) == upperOf((*f)->name) && sameParameters(*other, **f))
                        return core::fail(core::ErrorCode::InvalidArgument,
                                          "line " + std::to_string((*f)->line) + ": function '" + (*f)->name
                                              + "' is declared twice with the same parameters (first at line "
                                              + std::to_string(other->line) + ")",
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

    static bool sameParameters(const Function& a, const Function& b) {
        if (a.params.size() != b.params.size()) return false;
        for (std::size_t k = 0; k < a.params.size(); ++k) {
            const auto& x = a.params[k];
            const auto& y = b.params[k];
            if (x.mode != y.mode) return false;
            if (upperOf(x.type ? x.type->text() : std::string{}) != upperOf(y.type ? y.type->text() : std::string{})) return false;
        }
        return true;
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
        if (!atWord("FUNCTION") && !atWord("TEMPLATE")) return error("expected FUNCTION");
        auto f = templatedFunction();
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

    // 1.12.7 : une faute dite en francais, sans le mot trouve (les templates).
    core::Err<core::Error> errorHere(std::string what) const {
        return core::fail(core::ErrorCode::InvalidArgument, "line " + std::to_string(peek().line) + ": " + std::move(what), section_);
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
        // 1.12.1 (dialecte IHM) : CONTINUE (le tour suivant de la boucle) ; TRY ... CATCH ... END_TRY.
        if (dialect_ && atWord("CONTINUE") && !(peek(1).kind == Tok::Punct && (peek(1).text == ":=" || peek(1).text == "("))) {
            if (loops_ == 0) return error("CONTINUE is only allowed inside a loop (FOR, WHILE, REPEAT)");
            auto s = std::make_shared<Stmt>();
            s->kind = Stmt::Kind::Continue;
            s->line = advance().line;
            accept(";");
            return s;
        }
        if (dialect_ && atWord("TRY") && !(peek(1).kind == Tok::Punct && (peek(1).text == ":=" || peek(1).text == "("))) return tryStatement();
        // 1.13.0 (dialecte IHM) : LEVER('Code' [, 'message']) leve une erreur a soi ; RELEVER reemet
        // l'erreur du CATCH courant. LEVER prend des parentheses (sinon : un nom ordinaire) ; RELEVER
        // aucune (RELEVER := x ou RELEVER(...) : un nom ordinaire, comme CONTINUE/TRY).
        if (dialect_ && atWord("LEVER") && peek(1).kind == Tok::Punct && peek(1).text == "(") return raiseStatement();
        if (dialect_ && atWord("RELEVER") && !(peek(1).kind == Tok::Punct && (peek(1).text == ":=" || peek(1).text == "("))) {
            auto s = std::make_shared<Stmt>();
            s->kind = Stmt::Kind::Rethrow;
            s->line = advance().line;
            accept(";");
            return s;
        }
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
        // 1.12.1 : 'Auto': (un texte) ; -5: et -10..-1: (un nombre negatif).
        if (dialect_ && peek().kind == Tok::StringLiteral && peek(1).kind == Tok::Punct && (peek(1).text == ":" || peek(1).text == ","))
            return true;
        if (dialect_ && peek().kind == Tok::Punct && peek().text == "-" && peek(1).kind == Tok::Number && peek(2).kind == Tok::Punct
            && (peek(2).text == ":" || peek(2).text == ".." || peek(2).text == ","))
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

    // 1.12.1 (dialecte IHM) : TRY ... CATCH [Erreur] ... END_TRY. Une erreur du corps (une
    // division par zero, un nom inconnu, une case hors du tableau, ASSERT) n'arrete plus le
    // script : le CATCH s'execute, le message dans la variable STRING nommee apres CATCH.
    core::Result<StmtPtr> tryStatement() {
        auto s = std::make_shared<Stmt>();
        s->kind = Stmt::Kind::Try;
        s->line = advance().line;                       // TRY
        auto body = block({"CATCH", "END_TRY"});
        if (!body) return core::Err<core::Error>(body.error());
        s->body = *body;
        if (atWord("CATCH")) {
            const auto catchLine = advance().line;
            // CATCH Erreur : un nom seul sur la ligne du CATCH (ou suivi de ';') - pas
            // le debut d'une instruction (CATCH Etat := 0;).
            if (peek().kind == Tok::Identifier && peek().line == catchLine
                && (peek(1).line != catchLine || peek(1).kind == Tok::End || (peek(1).kind == Tok::Punct && peek(1).text == ";"))) {
                s->loopVariable = advance().raw;
                accept(";");
            }
            auto handler = block({"END_TRY"});
            if (!handler) return core::Err<core::Error>(handler.error());
            s->elseBody = *handler;
        }
        if (!acceptWord("END_TRY")) return error("expected END_TRY");
        accept(";");
        return s;
    }

    // 1.13.0 (dialecte IHM) : LEVER('E_Code' [, 'message {trou}']) - leve une erreur a soi, d'un code
    // et d'un message (optionnel). Le code et le message sont des expressions (lues a l'execution) ;
    // `value` porte le code, `condition` le message (nul : aucun).
    core::Result<StmtPtr> raiseStatement() {
        auto s = std::make_shared<Stmt>();
        s->kind = Stmt::Kind::Raise;
        s->line = advance().line;                       // LEVER
        if (!accept("(")) return error("expected '(' after LEVER");
        auto code = expression();
        if (!code) return core::Err<core::Error>(code.error());
        s->value = *code;
        if (accept(",")) {
            auto msg = expression();
            if (!msg) return core::Err<core::Error>(msg.error());
            s->condition = *msg;
        }
        if (!accept(")")) return error("expected ')' to close LEVER");
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
                if (dialect_ && peek().kind == Tok::StringLiteral) {      // 1.12.1 : 'Auto':
                    arm.texts.push_back(advance().literal.asString());
                    if (!accept(",")) break;
                    continue;
                }
                const bool negative = dialect_ && accept("-");             // 1.12.1 : -10..-1:
                if (peek().kind != Tok::Number) return error("expected a case label");
                const auto low = (negative ? -1 : 1) * advance().literal.asInteger();
                std::int64_t high = low;
                if (accept("..")) {
                    const bool below = dialect_ && accept("-");
                    if (peek().kind != Tok::Number) return error("expected the end of the range");
                    high = (below ? -1 : 1) * advance().literal.asInteger();
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
            ++loops_;
            auto body = block({"END_FOR"});
            --loops_;
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
        ++loops_;
        auto body = block({"END_FOR"});
        --loops_;
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
        ++loops_;
        auto body = block({"END_WHILE"});
        --loops_;
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
        ++loops_;
        auto body = block({"UNTIL"});
        --loops_;
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
    core::Result<ExprPtr> expression() { return dialect_ ? conditionalExpr() : orExpr(); }

    // 1.12.1 (dialecte IHM) : c ? a : b, le plus faible (a droite : c ? a : d ? e : f) ;
    // puis a ?? b (b quand a ne se lit pas : un nom inconnu, une division par zero, NULL).
    core::Result<ExprPtr> conditionalExpr() {
        auto cond = coalesceExpr();
        if (!cond || !atPunct("?")) return cond;
        const auto line = advance().line;
        auto yes = conditionalExpr();
        if (!yes) return yes;
        if (!accept(":")) return error("expected ':' after the value of '?' (c ? a : b)");
        auto no = conditionalExpr();
        if (!no) return no;
        auto node = std::make_shared<Expr>();
        node->kind = Expr::Kind::Conditional;
        node->lhs  = *cond;
        node->rhs  = *yes;
        node->rhs2 = *no;
        node->line = line;
        return node;
    }
    core::Result<ExprPtr> coalesceExpr() {
        auto lhs = orExpr();
        if (!lhs) return lhs;
        while (atPunct("??")) {
            const auto line = advance().line;
            auto rhs = orExpr();
            if (!rhs) return rhs;
            auto node = std::make_shared<Expr>();
            node->kind = Expr::Kind::Coalesce;
            node->lhs  = *lhs;
            node->rhs  = *rhs;
            node->line = line;
            lhs = node;
        }
        return lhs;
    }

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
        auto lhs = binaryLevel({"=", "<>", "<", ">", "<=", ">="}, &Parser::addExpr);
        if (!lhs || !dialect_) return lhs;
        // 1.12.1 (dialecte IHM) : x IN [1, 3, 5..9] ; x ENTRE 0 ET 100 (bornes comprises).
        if (atWord("IN") && peek(1).kind == Tok::Punct && peek(1).text == "[") {
            const auto line = advance().line;
            advance();                                          // [
            auto node = std::make_shared<Expr>();
            node->kind = Expr::Kind::InList;
            node->lhs  = *lhs;
            node->line = line;
            if (!atPunct("]"))
                for (;;) {
                    auto item = addExpr();
                    if (!item) return item;
                    if (accept("..")) {
                        auto high = addExpr();
                        if (!high) return high;
                        auto range = std::make_shared<Expr>();
                        range->kind = Expr::Kind::Between;
                        range->rhs  = *item;
                        range->rhs2 = *high;
                        range->line = line;
                        node->more.push_back(range);
                    } else {
                        node->more.push_back(*item);
                    }
                    if (!accept(",")) break;
                }
            if (!accept("]")) return error("expected ']' at the end of the list of IN");
            return node;
        }
        if (atWord("ENTRE")) {
            const auto line = advance().line;
            auto low = addExpr();
            if (!low) return low;
            if (!acceptWord("ET")) return error("expected ET (x ENTRE a ET b)");
            auto high = addExpr();
            if (!high) return high;
            auto node = std::make_shared<Expr>();
            node->kind = Expr::Kind::Between;
            node->lhs  = *lhs;
            node->rhs  = *low;
            node->rhs2 = *high;
            node->line = line;
            return node;
        }
        return lhs;
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
            // 1.11.25 : -2147483648 reste le plus petit DINT : 2147483648 seul est un UDINT
            // (au-dela de DINT), son oppose se replie en DINT.
            if (node->op == "-" && (*operand)->kind == Expr::Kind::Literal && (*operand)->literal.type() == Type::UDInt
                && (*operand)->literal.asInteger() <= 2147483648LL) {
                auto folded = std::make_shared<Expr>(**operand);
                folded->literal = Value::integer(Type::DInt, -(*operand)->literal.asInteger());
                return folded;
            }
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
            if (t.kind == Tok::Number && !t.text.empty() && t.text != "TRUE" && t.text != "FALSE") node->literalType = t.text;   // 1.11.20
            advance();
            return node;
        }
        // 1.12.19 : LAMBDA (x : REAL) : BOOL => x > 5, ou LAMBDA (...) ... END_LAMBDA.
        if (dialect_ && t.kind == Tok::Identifier && upperOf(t.text) == "LAMBDA") return lambdaExpr();
        // 1.12.19 : l'appel d'une fonction generique, ses types donnes - Limiter<INT>(x, 0, 100) ;
        // seulement NOM < TYPE [, TYPE] > ( : a < b reste une comparaison.
        if (dialect_ && t.kind == Tok::Identifier && !lib::templateInfo(t.text) && peek(1).kind == Tok::Punct && peek(1).text == "<") {
            std::size_t k = 2;
            std::vector<std::string> types;
            bool closed = false;
            while (peek(k).kind == Tok::Identifier) {
                types.push_back(upperOf(peek(k).text));
                ++k;
                if (peek(k).kind == Tok::Punct && peek(k).text == ",") { ++k; continue; }
                if (peek(k).kind == Tok::Punct && peek(k).text == ">") { ++k; closed = true; }
                break;
            }
            if (closed && !types.empty() && peek(k).kind == Tok::Punct && peek(k).text == "(") {
                auto node = std::make_shared<Expr>();
                node->kind = Expr::Kind::Reference;
                node->name = t.raw;
                node->line = t.line;
                node->typeArgs = std::move(types);
                for (std::size_t i = 0; i < k; ++i) advance();
                return node;
            }
        }
        if (dialect_ && t.kind == Tok::Identifier && upperOf(t.text) == "NULL") {
            auto node = std::make_shared<Expr>();
            node->kind = Expr::Kind::Null;
            node->line = t.line;
            advance();
            return node;
        }
        // 1.12.7 (dialecte IHM) : un template - NUMERIC_LIMITS<UINT>.MAX(), NUMBERS<REAL>.PI, SATURATE_CAST<INT>(x),
        // ADD_SAT<INT>(a, b). Seulement pour ces noms, et seulement NOM < TYPE > : a < b ne change pas.
        if (dialect_ && t.kind == Tok::Identifier && lib::templateInfo(t.text) && peek(1).kind == Tok::Punct && peek(1).text == "<"
            && peek(2).kind == Tok::Identifier && peek(3).kind == Tok::Punct && peek(3).text == ">") {
            const auto* info = lib::templateInfo(t.text);
            const std::string type = upperOf(peek(2).text);
            const std::string head = std::string(info->name) + "<" + type + ">";
            const auto exampleFor = [&] {
                std::string x(info->example);
                const auto a = x.find('<'), b = x.find('>');
                return a == std::string::npos || b == std::string::npos ? x : x.substr(0, a + 1) + type + x.substr(b);
            };
            advance();
            // 1.12.20 : STATIC_CAST<T_MODE>, DYNAMIC_CAST<T> (le T d'une fonction generique) - un nom qui
            // n'est pas un type du langage se resout en marche (castNative).
            if (!lib::templateAccepts(*info, type) && !(lib::castTemplate(info->name) && !lib::elementaryTypeName(type))) {
                // Un nom qui n'est pas un type, proche d'un type accepte (UNIT) : « UNIT n'est pas un type -
                // veux-tu dire UINT ? » (la maquette) ; un type du langage ou du projet : le type attendu.
                if (!lib::elementaryTypeName(type)) {
                    const std::string near = lib::closest(type, std::vector<std::string>(info->types.begin(), info->types.end()));
                    if (!near.empty()) return errorHere((peek(1).raw.empty() ? peek(1).text : peek(1).raw) + " n'est pas un type" + near);
                }
                return errorHere(head + " : " + std::string(info->want));
            }
            advance();
            advance();
            advance();
            auto node = std::make_shared<Expr>();
            node->kind = Expr::Kind::Reference;
            node->name = head;
            node->line = t.line;
            if (info->isClass) {
                if (!(atPunct(".") && peek(1).kind == Tok::Identifier)) return errorHere(head + " : choisis un membre \xE2\x80\x94 " + exampleFor());
                const std::string member = peek(1).raw.empty() ? peek(1).text : peek(1).raw;
                const auto* m = lib::templateMember(info->name, member);
                const bool called = peek(2).kind == Tok::Punct && peek(2).text == "(";
                if (!m) {
                    std::vector<std::string> pool;
                    for (const auto& x : info->name == "NUMBERS" ? lib::numbersMembers() : lib::limitsMembers()) pool.emplace_back(x.name);
                    return errorHere(head + " n'a pas de membre " + member + lib::closest(member, pool));
                }
                if (m->function && !called) return errorHere(std::string(m->name) + " est une fonction : " + head + "." + std::string(m->name) + "()");
                if (!m->function && called) return errorHere(std::string(m->name) + " est une constante : sans parenth\xC3\xA8ses");
            } else if (!atPunct("(")) {
                return errorHere(head + " s'appelle : " + exampleFor());
            }
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
            const auto line = t.line;
            // 1.12.14 (dialecte IHM) : (Nom := 'Cuve A', Niveau := 2.5) - une structure, membre par membre
            // (ceux qui manquent gardent leur valeur par defaut) ; dans une liste, une MAP, un tableau aussi.
            if (dialect_ && peek().kind == Tok::Identifier && peek(1).kind == Tok::Punct && peek(1).text == ":=") {
                auto node = std::make_shared<Expr>();
                node->kind = Expr::Kind::Collection;
                node->op   = "STRUCT";
                node->line = line;
                for (;;) {
                    const Token& m = peek();
                    if (m.kind != Tok::Identifier) return error("expected a member name: (Name := value, ...)");
                    const std::string member = m.raw;
                    advance();
                    if (!accept(":=")) return error("expected ':=' after the member " + member);
                    auto value = expression();
                    if (!value) return value;
                    node->arguments.emplace_back(member, *value);
                    node->more.push_back(*value);
                    if (!accept(",")) break;
                }
                if (!accept(")")) return error("expected ')' at the end of the structure (Name := value, ...)");
                return node;
            }
            auto inner = expression();
            if (!inner) return inner;
            // 1.12.2 (dialecte IHM) : (a, b, c) - un tuple.
            if (dialect_ && atPunct(",")) {
                auto node = std::make_shared<Expr>();
                node->kind = Expr::Kind::Collection;
                node->op   = "TUPLE";
                node->line = line;
                node->more.push_back(*inner);
                while (accept(",")) {
                    auto item = expression();
                    if (!item) return item;
                    node->more.push_back(*item);
                }
                if (!accept(")")) return error("expected ')' at the end of the tuple (a, b)");
                return node;
            }
            if (!accept(")")) return error("expected ')'");
            return inner;
        }
        // 1.12.2 (dialecte IHM) : [a, b, c] (une liste, un tableau), ['a' := 1, 'b' := 2] (une MAP),
        // [] (vide).
        if (dialect_ && atPunct("[")) {
            auto node = std::make_shared<Expr>();
            node->kind = Expr::Kind::Collection;
            node->line = advance().line;
            bool map = false;
            if (!atPunct("]"))
                for (;;) {
                    auto item = expression();
                    if (!item) return item;
                    if (atPunct(":=")) {
                        if (!node->more.empty() && !map) return error("a list mixes values and 'key := value' entries");
                        map = true;
                        advance();
                        auto value = expression();
                        if (!value) return value;
                        auto entry = std::make_shared<Expr>();
                        entry->kind = Expr::Kind::Binary;
                        entry->op   = ":=";
                        entry->lhs  = *item;
                        entry->rhs  = *value;
                        entry->line = (*item)->line;
                        node->more.push_back(entry);
                    } else {
                        if (map) return error("expected 'key := value' (a MAP literal: ['a' := 1, 'b' := 2])");
                        node->more.push_back(*item);
                    }
                    if (!accept(",")) break;
                }
            if (!accept("]")) return error("expected ']' at the end of the list");
            if (map) node->op = "MAP";
            return node;
        }
        return error("expected a value");
    }

    // ---- 1.10 : le dialecte IHM ---------------------------------------------
    // La fin d'un bloc (RETURN seul en fin de branche : "RETURN END_IF").
    [[nodiscard]] bool atBlockEnd() const {
        for (std::string_view w : {"END_IF", "ELSIF", "ELSE", "END_CASE", "END_FOR", "END_WHILE", "UNTIL", "END_REPEAT", "END_FUNCTION",
                                   "CATCH", "END_TRY", "END_LAMBDA"})
            if (atWord(w)) return true;
        return false;
    }

    // Une borne constante de tableau : -3, 10, 2*5, 16#FF ; 1.12.2 : le nom d'une constante
    // entiere declaree plus haut (VAR CONSTANT N : INT := 10; ... ARRAY[1..N] OF REAL).
    core::Result<std::int64_t> constantBound() {
        auto e = addExpr();
        if (!e) return core::Err<core::Error>(e.error());
        std::int64_t v = 0;
        if (!constantValue(**e, v))
            return error((*e)->kind == Expr::Kind::Reference
                             ? "expected a constant array bound ('" + (*e)->name + "' is not an integer constant declared above)"
                             : std::string("expected a constant array bound"));
        return v;
    }
    bool constantValue(const Expr& e, std::int64_t& out) const {
        switch (e.kind) {
            case Expr::Kind::Literal:
                if (!isInteger(e.literal.type())) return false;
                out = e.literal.asInteger();
                return true;
            case Expr::Kind::Reference: {
                const auto it = constants_.find(upperOf(e.name));
                if (it == constants_.end()) return false;
                out = it->second;
                return true;
            }
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
        // 1.12.19 (dialecte IHM) : CALLBACK (x : REAL; y : INT) : BOOL - une fonction donnee comme une valeur ;
        // les parametres nommes ou non ((REAL, INT)), le retour facultatif (CALLBACK () : rien a rendre).
        if (dialect_ && atWord("CALLBACK")) {
            advance();
            std::vector<std::pair<std::string, TypeRef>> params;
            if (accept("(")) {
                while (!atPunct(")")) {
                    std::string pname;
                    if (peek().kind == Tok::Identifier && peek(1).kind == Tok::Punct && peek(1).text == ":") {
                        pname = advance().raw;
                        advance();
                    }
                    auto pt = typeSpec();
                    if (!pt) return pt;
                    params.emplace_back(pname, *pt);
                    if (accept(";") || accept(",")) continue;
                    if (!atPunct(")")) return errorHere("CALLBACK (...) : ';' ou ')' attendu apr\xC3\xA8s un param\xC3\xA8tre \xE2\x80\x94 CALLBACK (x : REAL) : BOOL");
                }
                advance();
            }
            TypeRef result;
            if (accept(":")) {
                auto rt = typeSpec();
                if (!rt) return rt;
                result = *rt;
            }
            return callbackType(std::move(params), result);
        }
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
        // 1.12.2 : LIST OF T, VECTOR OF T (taille variable, indices a partir de 0) ;
        // TUPLE(T1, T2...) : des valeurs de types fixes (t.Item1, t.Item2...).
        if ((atWord("LIST") || atWord("VECTOR")) && atWord("OF", 1)) {
            const bool vector = atWord("VECTOR");
            advance();
            advance();
            auto e = typeSpec();
            if (!e) return e;
            return listType(*e, vector);
        }
        // 1.12.7 : SET OF T (std::set : rangee, chaque valeur une fois) ; RING[N] OF T (les N
        // dernieres valeurs, N une constante entiere de 1 a kRingMax). Un nom seul (SET, RING) reste
        // un nom de type ; une variable nommee Set ou Ring ne change pas (ce sont des types ici).
        if (atWord("SET") && atWord("OF", 1)) {
            advance();
            advance();
            auto e = typeSpec();
            if (!e) return e;
            return setType(*e);
        }
        if (atWord("RING") && ((peek(1).kind == Tok::Punct && peek(1).text == "[") || atWord("OF", 1))) {
            advance();
            if (!accept("["))
                return errorHere("RING : sa capacit\xC3\xA9 est attendue, le nombre de valeurs gard\xC3\xA9" "es \xE2\x80\x94 RING[100] OF REAL");
            auto n = addExpr();
            if (!n) return core::Err<core::Error>(n.error());
            std::int64_t capacity = 0;
            if (!constantValue(**n, capacity))
                return errorHere("RING[N] : N est une constante enti\xC3\xA8re"
                             + ((*n)->kind == Expr::Kind::Reference ? " ('" + (*n)->name + "' n'est pas une constante enti\xC3\xA8re d\xC3\xA9" "clar\xC3\xA9" "e plus haut)"
                                                                   : std::string(" (RING[100] OF REAL)")));
            if (capacity < 1 || capacity > kRingMax)
                return errorHere("RING[" + std::to_string(capacity) + "] : la capacit\xC3\xA9 va de 1 \xC3\xA0 " + std::to_string(kRingMax));
            if (!accept("]")) return errorHere("RING[N] : ']' attendu apr\xC3\xA8s la capacit\xC3\xA9");
            if (!acceptKeyword("OF")) return errorHere("RING[N] : OF attendu apr\xC3\xA8s la capacit\xC3\xA9 (RING[100] OF REAL)");
            auto e = typeSpec();
            if (!e) return e;
            return ringType(*e, capacity);
        }
        if (atWord("TUPLE") && peek(1).kind == Tok::Punct && peek(1).text == "(") {
            advance();
            advance();
            std::vector<TypeRef> items;
            if (!atPunct(")"))
                for (;;) {
                    auto e = typeSpec();
                    if (!e) return e;
                    items.push_back(*e);
                    if (!accept(",")) break;
                }
            if (!accept(")")) return error("expected ')' after the types of TUPLE(...)");
            if (items.empty()) return error("a TUPLE has at least one type: TUPLE(INT, STRING)");
            if (items.size() > 16) return error("a TUPLE has at most 16 values");
            return tupleType(std::move(items));
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
        else if (up == "SINT") scalar = Type::Int;     // 1.12.5 (D16) : LINT, ULINT, LWORD sur 64 bits (typeFromName)
        else if (up == "USINT") scalar = Type::UInt;
        else if (up == "EBOOL") scalar = Type::Bool;
        if (scalar != Type::Unknown) return scalarType(scalar, up);
        auto t = std::make_shared<TypeDesc>();
        t->kind = TypeDesc::Kind::Struct;           // resolue a l'execution (Environment::structMembers)
        t->name = name;
        // 1.12.20 (lot G) : T_Mesure<REAL> - l'instance d'une structure generique de l'IHM ; ses types se lisent ici,
        // l'environnement donne ses membres (T devenu REAL).
        if (dialect_ && atPunct("<")) {
            advance();
            std::string args;
            for (;;) {
                auto a = typeSpec();
                if (!a) return a;
                args += (args.empty() ? "" : ", ") + (*a)->text();
                if (!accept(",")) break;
            }
            if (!accept(">")) return errorHere(name + "<\xE2\x80\xA6> : '>' attendu apr\xC3\xA8s ses types (T_Mesure<REAL>)");
            t->name = name + "<" + args + ">";
        }
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
            // 1.12.2 : une constante entiere peut borner un tableau declare plus bas.
            std::int64_t v = 0;
            if (constant && initial && (*t)->kind == TypeDesc::Kind::Scalar && isInteger((*t)->scalar) && constantValue(*initial, v))
                constants_[upperOf(d.name)] = v;
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

    // 1.12.19 : [TEMPLATE <T [: ANY_NUM], U>] FUNCTION ... - les types generiques d'une fonction.
    core::Result<std::shared_ptr<Function>> templatedFunction() {
        std::vector<std::pair<std::string, std::string>> params;
        if (atWord("TEMPLATE")) {
            advance();
            if (!accept("<")) return errorHere("TEMPLATE : '<' attendu \xE2\x80\x94 TEMPLATE <T : ANY_NUM> FUNCTION ...");
            for (;;) {
                if (peek().kind != Tok::Identifier) return errorHere("TEMPLATE : le nom d'un type attendu (T)");
                std::string name = advance().raw;
                std::string constraint;
                if (accept(":")) {
                    if (peek().kind != Tok::Identifier) return errorHere("TEMPLATE <" + name + " : ...> : un g\xC3\xA9n\xC3\xA9rique ANY_* attendu (ANY_NUM, ANY_INT...)");
                    constraint = upperOf(advance().text);
                    if (constraint.rfind("ANY", 0) != 0)
                        return errorHere("TEMPLATE <" + name + " : " + constraint + "> : la contrainte est un g\xC3\xA9n\xC3\xA9rique ANY_* (ANY_NUM, ANY_INT, ANY_REAL, ANY_STRING...)");
                }
                for (const auto& p : params)
                    if (upperOf(p.first) == upperOf(name)) return errorHere("TEMPLATE : " + name + " est d\xC3\xA9j\xC3\xA0 un type de la fonction");
                params.emplace_back(std::move(name), std::move(constraint));
                if (accept(",")) continue;
                break;
            }
            if (!accept(">")) return errorHere("TEMPLATE <...> : '>' attendu");
            if (!atWord("FUNCTION")) return errorHere("TEMPLATE <...> : FUNCTION attendue ensuite");
        }
        auto f = functionDeclaration();
        if (!f) return f;
        (*f)->typeParams = std::move(params);
        return f;
    }

    // 1.12.19 : LAMBDA (x : REAL; y : INT) : BOOL => expression
    //           LAMBDA (x : REAL) : BOOL VAR ... END_VAR instructions (RETURN valeur;) END_LAMBDA
    core::Result<ExprPtr> lambdaExpr() {
        auto f = std::make_shared<Function>();
        f->line = advance().line;                       // LAMBDA
        f->name = "LAMBDA";
        if (!accept("(")) return errorHere("LAMBDA : '(' attendu \xE2\x80\x94 LAMBDA (x : REAL) : BOOL => x > 5");
        while (!atPunct(")")) {
            auto group = declarationGroup(VarDecl::Mode::Input, false);
            if (!group) return core::Err<core::Error>(group.error());
            for (auto& d : *group) {
                for (const auto& p : f->params)
                    if (upperOf(p.name) == upperOf(d.name)) return errorHere("LAMBDA : le param\xC3\xA8tre " + d.name + " est d\xC3\xA9" "clar\xC3\xA9 deux fois");
                f->params.push_back(std::move(d));
            }
            if (accept(";") || accept(",")) continue;
            if (!atPunct(")")) return errorHere("LAMBDA : ';' ou ')' attendu apr\xC3\xA8s un param\xC3\xA8tre");
        }
        advance();                                      // )
        if (accept(":")) {
            auto t = typeSpec();
            if (!t) return core::Err<core::Error>(t.error());
            f->result = *t;
        }
        if (accept("=>")) {
            auto e = expression();
            if (!e) return e;
            auto ret = std::make_shared<Stmt>();
            ret->line = f->line;
            if (f->result) {
                ret->kind = Stmt::Kind::Return;
                ret->value = *e;
            } else {
                // Sans type de retour : l'expression est une instruction (un appel) - LAMBDA () => Arreter().
                if ((*e)->kind != Expr::Kind::Call) return errorHere("LAMBDA sans type de retour : apr\xC3\xA8s =>, un appel (LAMBDA () => Arreter())");
                ret->kind = Stmt::Kind::Call;
                ret->value = *e;
            }
            f->body.push_back(ret);
            f->endLine = f->line;
        } else {
            while (atVarBlock()) {
                auto decls = varBlock(/*inFunction*/ true);
                if (!decls) return core::Err<core::Error>(decls.error());
                for (auto& d : *decls) {
                    if (d.mode != VarDecl::Mode::Local && d.mode != VarDecl::Mode::Temp) return errorHere("LAMBDA : ses param\xC3\xA8tres s'\xC3\xA9" "crivent entre ses parenth\xC3\xA8ses");
                    f->locals.push_back(std::move(d));
                }
            }
            auto body = block({"END_LAMBDA"});
            if (!body) return core::Err<core::Error>(body.error());
            f->body = *body;
            if (!atWord("END_LAMBDA")) return errorHere("LAMBDA : END_LAMBDA attendu (ou => et une expression)");
            f->endLine = advance().line;
        }
        auto node = std::make_shared<Expr>();
        node->kind = Expr::Kind::Lambda;
        node->line = f->line;
        node->fn = f;
        return ExprPtr(node);
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
    int                loops_{0};          // 1.12.1 : la profondeur de boucle (CONTINUE)
    std::map<std::string, std::int64_t> constants_;   // 1.12.2 : les constantes entieres deja declarees (bornes)
};

} // namespace

// ============================================================ execution =====
namespace {

// 1.10 : un type ecrit ("ARRAY[0..9] OF REAL", "T_FOUR"), lu par le parseur du dialecte.
core::Result<TypeRef> parseTypeText(std::string_view text);

// ---- 1.11.20 : LE TYPE D'UNE EXPRESSION, SANS L'EVALUER (le choix d'une surcharge) ----
// Les litteraux, les noms (une locale : son type declare ; sinon l'environnement), les
// membres, les cases, p^, les operateurs (comme combine() les calcule : un reel l'emporte,
// sinon l'entier le plus large ; un nombre seul compte pour un DINT, comme le moteur le
// lit), les appels (leur type rendu). Vide : inconnu. Le controle (callSites) et le moteur
// (Runner::shapesOf) s'en servent tous deux : ils choisissent la meme surcharge.
std::string valueTypeName(Type t) {
    switch (t) {
        case Type::Bool:   return "BOOL";
        case Type::Byte:   return "BYTE";
        case Type::Word:   return "WORD";
        case Type::DWord:  return "DWORD";
        case Type::Int:    return "INT";
        case Type::DInt:   return "DINT";
        case Type::UInt:   return "UINT";
        case Type::UDInt:  return "UDINT";
        case Type::Real:   return "REAL";
        case Type::Time:   return "TIME";
        case Type::String: return "STRING";
        case Type::Date:   return "DATE";            // 1.12.1
        case Type::Tod:    return "TIME_OF_DAY";
        case Type::Dt:     return "DATE_AND_TIME";
        case Type::LInt:   return "LINT";            // 1.12.5 (D16)
        case Type::ULInt:  return "ULINT";
        case Type::LWord:  return "LWORD";
        case Type::Unknown: break;
    }
    return {};
}

int integerBits(const std::string& u) {
    if (u == "SINT" || u == "USINT" || u == "BYTE") return 8;
    if (u == "INT" || u == "UINT" || u == "WORD") return 16;
    if (u == "DINT" || u == "UDINT" || u == "DWORD") return 32;
    if (u == "LINT" || u == "ULINT" || u == "LWORD") return 64;
    return 0;
}

// "Vue_A.Vanne_3.Ouvrir" : une chaine de noms et de membres (sans case) ; vide sinon.
std::string dottedName(const Expr& e) {
    if (e.kind == Expr::Kind::Reference) return e.name;
    if (e.kind == Expr::Kind::Member && e.lhs) {
        const std::string base = dottedName(*e.lhs);
        return base.empty() ? std::string{} : base + "." + e.name;
    }
    return {};
}

// Le texte court d'un argument, pour les messages ("x + 1", "Tab[...]", "F(...)").
std::string exprText(const Expr& e, int depth = 0) {
    if (depth > 6) return "...";
    switch (e.kind) {
        case Expr::Kind::Literal:
            if (e.literal.type() == Type::String) return "'" + e.literal.asString() + "'";
            if (e.literal.type() == Type::Bool) return e.literal.isTruthy() ? "TRUE" : "FALSE";
            if (e.literal.type() == Type::Real) {
                char b[48];
                std::snprintf(b, sizeof b, "%g", e.literal.asReal());
                return b;
            }
            return (e.literalType.empty() ? std::string{} : e.literalType + "#") + std::to_string(e.literal.asInteger());
        case Expr::Kind::Null: return "NULL";
        case Expr::Kind::Reference: return e.name;
        case Expr::Kind::Member: return (e.lhs ? exprText(*e.lhs, depth + 1) : std::string{}) + "." + e.name;
        case Expr::Kind::Index: return (e.lhs ? exprText(*e.lhs, depth + 1) : std::string{}) + "[...]";
        case Expr::Kind::Deref: return (e.lhs ? exprText(*e.lhs, depth + 1) : std::string{}) + "^";
        case Expr::Kind::Unary: return e.op + (e.op.size() > 1 ? " " : "") + (e.lhs ? exprText(*e.lhs, depth + 1) : std::string{});
        case Expr::Kind::Binary:
            return (e.lhs ? exprText(*e.lhs, depth + 1) : std::string{}) + " " + e.op + " " + (e.rhs ? exprText(*e.rhs, depth + 1) : std::string{});
        case Expr::Kind::Call: return (e.lhs ? exprText(*e.lhs, depth + 1) : std::string{}) + "(...)";
        case Expr::Kind::Conditional:
            return (e.lhs ? exprText(*e.lhs, depth + 1) : std::string{}) + " ? " + (e.rhs ? exprText(*e.rhs, depth + 1) : std::string{}) + " : "
                 + (e.rhs2 ? exprText(*e.rhs2, depth + 1) : std::string{});
        case Expr::Kind::Coalesce:
            return (e.lhs ? exprText(*e.lhs, depth + 1) : std::string{}) + " ?? " + (e.rhs ? exprText(*e.rhs, depth + 1) : std::string{});
        case Expr::Kind::InList: return (e.lhs ? exprText(*e.lhs, depth + 1) : std::string{}) + " IN [...]";
        case Expr::Kind::Collection: return e.op == "TUPLE" || e.op == "STRUCT" ? "(...)" : "[...]";
        case Expr::Kind::Between:
            return (e.lhs ? exprText(*e.lhs, depth + 1) + " ENTRE " : std::string{}) + (e.rhs ? exprText(*e.rhs, depth + 1) : std::string{})
                 + (e.lhs ? " ET " : "..") + (e.rhs2 ? exprText(*e.rhs2, depth + 1) : std::string{});
    }
    return "...";
}

class Typer {
public:
    using LocalType = std::function<std::string(std::string_view)>;     // le type d'une locale ; "" : pas une locale
    Typer(Environment& env, const Program* program, LocalType local) : env_(env), program_(program), local_(std::move(local)) {}

    std::string typeOf(const Expr& e, int depth = 0) {
        if (depth > 32) return {};
        switch (e.kind) {
            case Expr::Kind::Literal:
                if (!e.literalType.empty()) return upperOf(e.literalType);
                if (e.literal.type() != Type::Time && e.literal.type() != Type::Bool && isInteger(e.literal.type())) return "DINT";
                return valueTypeName(e.literal.type());
            case Expr::Kind::Null: return {};
            case Expr::Kind::Reference: {
                if (const auto hash = e.name.find('#'); hash != std::string::npos) return e.name.substr(0, hash);   // T_MODE#Auto
                if (std::string t = local_ ? local_(e.name) : std::string{}; !t.empty()) return t;
                return env_.declaredType(e.name);
            }
            case Expr::Kind::Member: {
                const std::string base = e.lhs ? typeOf(*e.lhs, depth + 1) : std::string{};
                if (!base.empty()) {
                    std::vector<std::pair<std::string, std::string>> members;
                    if (env_.structMembers(base, members))
                        for (const auto& [name, type] : members)
                            if (upperOf(name) == upperOf(e.name)) return type.rfind('#', 0) == 0 ? std::string{} : type;
                    // 1.12.2 : t.Item2 d'un tuple ; L.Count d'une liste, d'un tableau, d'une MAP.
                    if (auto t = parseTypeText(base); t && *t) {
                        const std::string m = upperOf(e.name);
                        if ((*t)->kind == TypeDesc::Kind::Tuple)
                            for (const auto& [name, type] : (*t)->members)
                                if (upperOf(name) == m) return type ? type->text() : std::string{};
                        if (((*t)->kind == TypeDesc::Kind::List || (*t)->kind == TypeDesc::Kind::Map || (*t)->kind == TypeDesc::Kind::Array)
                            && (m == "COUNT" || m == "LENGTH" || m == "SIZE"))
                            return "DINT";
                        if ((*t)->kind == TypeDesc::Kind::List && (m == "FIRST" || m == "LAST") && (*t)->element) return (*t)->element->text();
                        if ((*t)->kind == TypeDesc::Kind::List && m == "EMPTY") return "BOOL";
                        if (isRingType(**t) && (m == "CAPACITY" || m == "MAXSIZE")) return "DINT";      // 1.12.7 ; 1.12.14 : MaxSize
                        if (isRingType(**t) && m == "FULL") return "BOOL";
                    }
                }
                if (const std::string dotted = dottedName(e); !dotted.empty()) return env_.declaredType(dotted);
                return {};
            }
            case Expr::Kind::Index:
            case Expr::Kind::Deref: {
                const std::string base = e.lhs ? typeOf(*e.lhs, depth + 1) : std::string{};
                if (base.empty()) return {};
                auto t = parseTypeText(base);
                if (!t || !*t || !(*t)->element) return {};
                const auto k = (*t)->kind;
                const bool ok = e.kind == Expr::Kind::Index ? (k == TypeDesc::Kind::Array || k == TypeDesc::Kind::Map || k == TypeDesc::Kind::List)
                                                            : (k == TypeDesc::Kind::Ref || k == TypeDesc::Kind::Pointer);
                return ok ? (*t)->element->text() : std::string{};
            }
            case Expr::Kind::Unary: return e.lhs ? typeOf(*e.lhs, depth + 1) : std::string{};
            case Expr::Kind::Binary: return binaryType(e, depth);
            case Expr::Kind::Call: return callType(e, depth);
            case Expr::Kind::Conditional:
            case Expr::Kind::Coalesce: {                        // le type d'un cote, sinon de l'autre
                std::string t = e.rhs ? typeOf(*e.rhs, depth + 1) : std::string{};
                if (t.empty()) t = e.kind == Expr::Kind::Conditional ? (e.rhs2 ? typeOf(*e.rhs2, depth + 1) : std::string{})
                                                                     : (e.lhs ? typeOf(*e.lhs, depth + 1) : std::string{});
                return t;
            }
            case Expr::Kind::InList:
            case Expr::Kind::Between: return "BOOL";
            case Expr::Kind::Collection: return {};        // 1.12.2 : un litteral prend le type de sa destination
        }
        return {};
    }

    ArgShape shapeOf(const std::string& name, bool output, const Expr& a, int depth = 0) {
        ArgShape s;
        s.name = name;
        s.output = output;
        const Expr* x = &a;
        bool negative = false;
        if (x->kind == Expr::Kind::Unary && (x->op == "-" || x->op == "+") && x->lhs && x->lhs->kind == Expr::Kind::Literal) {
            negative = x->op == "-";
            x = x->lhs.get();
        }
        const Type lt = x->literal.type();
        if (x->kind == Expr::Kind::Literal && x->literalType.empty() && lt != Type::Time && lt != Type::Bool && isInteger(lt)) {
            s.literal = true;
            s.value = negative ? -x->literal.asInteger() : x->literal.asInteger();
            s.text = std::to_string(s.value);
            return s;
        }
        s.designator = (a.kind == Expr::Kind::Reference && a.name.find('#') == std::string::npos) || a.kind == Expr::Kind::Member
                    || a.kind == Expr::Kind::Index || a.kind == Expr::Kind::Deref;
        s.type = typeOf(a, depth);
        s.text = exprText(a);
        return s;
    }

    std::vector<ArgShape> shapesOf(const Expr& call, int depth = 0) {
        std::vector<ArgShape> out;
        for (const auto& [name, x] : call.arguments)
            if (x) out.push_back(shapeOf(name, false, *x, depth));
        for (const auto& [name, x] : call.outputs)
            if (x) out.push_back(shapeOf(name, true, *x, depth));
        return out;
    }

private:
    std::string binaryType(const Expr& e, int depth) {
        const std::string& op = e.op;
        if (op == "=" || op == "<>" || op == "<" || op == ">" || op == "<=" || op == ">=") return "BOOL";
        const std::string a = upperOf(e.lhs ? typeOf(*e.lhs, depth + 1) : std::string{});
        const std::string b = upperOf(e.rhs ? typeOf(*e.rhs, depth + 1) : std::string{});
        if (a.empty() || b.empty()) return {};
        if (op == "AND" || op == "OR" || op == "XOR") {
            if (a == "BOOL" && b == "BOOL") return "BOOL";
            return integerBits(a) >= integerBits(b) ? a : b;
        }
        if (a == "STRING" || b == "STRING") return op == "+" ? "STRING" : std::string{};
        if (a == "LREAL" || b == "LREAL") return "LREAL";
        if (a == "REAL" || b == "REAL") return "REAL";
        // 1.12.1 : DT + TIME (DT), DT - DT (TIME), TOD + TIME (TOD), DATE - DATE (TIME).
        const auto dateName = [](const std::string& n) {
            return n == "DATE" || n == "TIME_OF_DAY" || n == "TOD" || n == "DATE_AND_TIME" || n == "DT";
        };
        if (dateName(a) || dateName(b)) {
            if (dateName(a) && b == "TIME") return a;
            if (a == "TIME" && dateName(b) && op == "+") return b;
            if (op == "-" && dateName(a) && dateName(b)) return "TIME";
            return {};
        }
        if (a == "TIME" || b == "TIME") return "TIME";
        const int x = integerBits(a), y = integerBits(b);
        if (!x || !y) return {};
        return x >= y ? a : b;
    }

    std::string callType(const Expr& e, int depth) {
        if (!e.lhs) return {};
        const std::string callee = e.lhs->kind == Expr::Kind::Reference ? e.lhs->name : dottedName(*e.lhs);
        if (callee.empty()) return {};
        const std::string u = upperOf(callee);
        if (program_) {
            std::vector<const Function*> inner;
            for (const auto& f : program_->functions)
                if (upperOf(f->name) == u) inner.push_back(f.get());
            if (!inner.empty()) {
                if (inner.size() == 1) return inner.front()->result ? inner.front()->result->text() : std::string{};
                std::string why;
                const int k = env_.chooseInner(inner, shapesOf(e, depth + 1), why);
                return k >= 0 && inner[static_cast<std::size_t>(k)]->result ? inner[static_cast<std::size_t>(k)]->result->text() : std::string{};
            }
        }
        if (const auto to = u.find("_TO_"); to != std::string::npos && to > 0) return u.substr(to + 4);   // INT_TO_REAL
        if (u.rfind("TO_", 0) == 0 && u.size() > 3) return callee.substr(3);                           // TO_REAL, TO_T_MODE
        return env_.resultType(callee, shapesOf(e, depth + 1));
    }

    Environment&   env_;
    const Program* program_;
    LocalType      local_;
};

// Les appels d'une section, dans l'ordre du texte (le controle des surcharges).
class CallWalker {
public:
    CallWalker(Environment& env, const Program* program) : env_(env), program_(program) {}

    void walkProgram(const Program& p) {
        scope_ = &p.locals;
        fn_ = nullptr;
        within_.clear();
        stmts(p.body);
        for (const auto& f : p.functions) {
            fn_ = f.get();
            within_ = f->name;
            stmts(f->body);
        }
        fn_ = nullptr;
        within_.clear();
    }
    void walkExpression(const ExprPtr& e) { expr(e); }

    std::vector<CallSite> finish() {
        std::stable_sort(sites_.begin(), sites_.end(), [](const CallSite& a, const CallSite& b) { return a.line < b.line; });
        std::map<std::pair<std::string, std::uint32_t>, std::uint32_t> seen;
        for (auto& c : sites_) c.rank = ++seen[{upperOf(c.callee), c.line}];
        return std::move(sites_);
    }

private:
    std::string localType(std::string_view n) const {
        const std::string u = upperOf(n);
        if (fn_) {
            for (const auto& prm : fn_->params)
                if (upperOf(prm.name) == u) return prm.type ? prm.type->text() : std::string{};
            for (const auto& l : fn_->locals)
                if (upperOf(l.name) == u) return l.type ? l.type->text() : std::string{};
            if (upperOf(fn_->name) == u && fn_->result) return fn_->result->text();
        }
        if (scope_)
            for (const auto& l : *scope_)
                if (upperOf(l.name) == u) return l.type ? l.type->text() : std::string{};
        return {};
    }
    void expr(const ExprPtr& e) {
        if (!e) return;
        switch (e->kind) {
            case Expr::Kind::Literal:
            case Expr::Kind::Null:
            case Expr::Kind::Reference:
                return;
            case Expr::Kind::Member:
            case Expr::Kind::Deref:
            case Expr::Kind::Unary:
                expr(e->lhs);
                return;
            case Expr::Kind::Index:
                expr(e->lhs);
                expr(e->rhs);
                expr(e->rhs2);
                for (const auto& m : e->more) expr(m);
                return;
            case Expr::Kind::Binary:
                expr(e->lhs);
                expr(e->rhs);
                return;
            case Expr::Kind::Conditional:                // 1.12.1
            case Expr::Kind::Coalesce:
            case Expr::Kind::InList:
            case Expr::Kind::Between:
            case Expr::Kind::Collection:                 // 1.12.2
                expr(e->lhs);
                expr(e->rhs);
                expr(e->rhs2);
                for (const auto& m : e->more) expr(m);
                return;
            case Expr::Kind::Call: {
                CallSite c;
                if (e->lhs) c.callee = e->lhs->kind == Expr::Kind::Reference ? e->lhs->name : dottedName(*e->lhs);
                c.line = e->lhs ? e->lhs->line : e->line;
                c.within = within_;
                Typer typer(env_, program_, [this](std::string_view n) { return localType(n); });
                c.args = typer.shapesOf(*e);
                if (!c.callee.empty()) sites_.push_back(std::move(c));
                if (e->lhs && e->lhs->kind != Expr::Kind::Reference) expr(e->lhs);
                for (const auto& a : e->arguments) expr(a.second);
                for (const auto& o : e->outputs) expr(o.second);
                return;
            }
        }
    }
    void stmts(const std::vector<StmtPtr>& body) {
        for (const auto& s : body)
            if (s) stmt(*s);
    }
    void stmt(const Stmt& s) {
        if (s.kind == Stmt::Kind::Repeat) {          // REPEAT ... UNTIL c : le corps est ecrit avant
            stmts(s.body);
            expr(s.condition);
            return;
        }
        expr(s.target);
        for (const auto& t : s.extraTargets) expr(t);
        expr(s.value);
        expr(s.condition);
        expr(s.from);
        expr(s.to);
        expr(s.step);
        for (const auto& [cond, body] : s.branches) {
            expr(cond);
            stmts(body);
        }
        for (const auto& arm : s.arms) stmts(arm.body);
        stmts(s.body);
        stmts(s.elseBody);
    }

    Environment&                env_;
    const Program*              program_;
    const std::vector<VarDecl>* scope_{nullptr};
    const Function*             fn_{nullptr};
    std::string                 within_;
    std::vector<CallSite>       sites_;
};

struct Flow { enum class Kind : std::uint8_t { Normal, Exit, Return, Aborted, Continue /* 1.12.1 */ } kind{Kind::Normal}; };

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
        case Expr::Kind::Conditional:                    // 1.12.1
        case Expr::Kind::Coalesce:
        case Expr::Kind::InList:
        case Expr::Kind::Between:
        case Expr::Kind::Collection:                     // 1.12.2
            collectExpr(e->lhs, line, out);
            collectExpr(e->rhs, line, out);
            collectExpr(e->rhs2, line, out);
            for (const auto& m : e->more) collectExpr(m, line, out);
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

// 1.11.17 : le Runner qui fait un appel de l'environnement, le temps de cet appel
// (readCallerLocal). Un par fil ; un appel en fait d'autres : chacun remet le precedent.
class Runner;
thread_local Runner* t_caller = nullptr;
struct CallerScope {
    Runner* outer;
    explicit CallerScope(Runner* r) noexcept : outer(t_caller) { t_caller = r; }
    ~CallerScope() { t_caller = outer; }
    CallerScope(const CallerScope&) = delete;
    CallerScope& operator=(const CallerScope&) = delete;
};

class Runner {
public:
    Runner(Environment& env, const RunLimits& limits, std::string section)
        : env_(env), limits_(limits), section_(std::move(section)) {}

    // 1.12.19 : un texte de type, lu (typeOfText).
    TypeRef typeOnly(std::string_view type, std::string* why) {
        dialect_ = true;
        frames_.clear();
        frames_.emplace_back();
        frames_.back().scopes.emplace_back();
        auto t = typeFromText(type);
        if (!t) {
            if (why) *why = t.error().message();
            return nullptr;
        }
        return *t;
    }

    // 1.12.2 : un objet neuf de ce type, a la valeur de ce litteral (makeRichValue).
    ObjRef richValue(std::string_view type, std::string_view initial, std::string* why) {
        dialect_ = true;
        frames_.clear();
        frames_.emplace_back();
        frames_.back().scopes.emplace_back();
        const auto failWith = [&](std::string w) -> ObjRef {
            if (why) *why = std::move(w);
            return nullptr;
        };
        auto t = typeFromText(type);
        if (!t) return failWith(t.error().message());
        auto obj = instantiate(*t);
        if (!obj) return failWith(obj.error().message());
        bool blank = true;
        for (const char c : initial)
            if (!std::isspace(static_cast<unsigned char>(c))) blank = false;
        if (blank) return *obj;
        Lexer lexer(initial, "valeur", true);
        auto tokens = lexer.run();
        if (!tokens) return failWith(tokens.error().message());
        Parser parser(std::move(*tokens), "valeur", true);
        auto e = parser.soleExpression();
        if (!e) return failWith(e.error().message());
        auto v = rich(**e);
        if (!v) return failWith(v.error().message());
        std::string w;
        if (!store(**obj, *v, &w)) return failWith(w);
        return *obj;
    }

    // 1.11.17 : une locale de valeur simple (readCallerLocal).
    bool readLocal(std::string_view name, Value& out) {
        const Place* p = findLocal(name);
        if (!p || !p->obj || (p->obj->type && p->obj->type->aggregate())) return false;
        out = p->obj->value;
        return true;
    }

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
        // 1.12.1 : le cote gauche de a ?? b non plus (son erreur choisit b).
        if (probing_ || quiet_ > 0) return;
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
        // 1.12.1 : dans un TRY, l'erreur est gardee pour son CATCH (pas au journal) - sauf une
        // boucle sans fin, un cycle trop long : ceux-la arretent tout, comme avant.
        if (catching_ > 0 && !caught_.any && message.find(" loop ran more than ") == std::string::npos
            && message.find("the scan exceeded") == std::string::npos) {
            caught_ = Caught{true, {}, std::move(message), line};   // 1.13.0 : code vide (erreur moteur)
            aborted_ = true;
            return;
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
        // 1.12.26 : un script detache rend la main (sa tranche de temps) ; arrete pendant ce temps : sans erreur.
        if (limits_.slice && (++sliced_ & 255u) == 0 && !limits_.slice()) {
            aborted_ = true;
            return true;
        }
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
            case Expr::Kind::Null:                // 1.10 : le dialecte seulement
            case Expr::Kind::Conditional:         // 1.12.1 : le dialecte seulement
            case Expr::Kind::Coalesce:
            case Expr::Kind::InList:
            case Expr::Kind::Between:
            case Expr::Kind::Collection: break;   // 1.12.2 : le dialecte seulement
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

        // 1.12.1 (le dialecte de l'IHM) : les dates. DT + TIME, DT - TIME, TOD + TIME (modulo un
        // jour), TIME + DT ; DT - DT, DATE - DATE, TOD - TOD : une duree.
        if (isDateType(a.type()) || isDateType(b.type())) {
            constexpr std::int64_t kDay = 86400000;
            const Type ta = a.type(), tb = b.type();
            const auto x = a.asInteger(), y = b.asInteger();
            const auto shifted = [&](Type t, std::int64_t r) {
                return Value::integer(t, t == Type::Tod ? ((r % kDay) + kDay) % kDay : r);
            };
            if ((ta == Type::Dt || ta == Type::Tod) && tb == Type::Time && (e.op == "+" || e.op == "-"))
                return shifted(ta, e.op == "+" ? x + y : x - y);
            if (ta == Type::Time && (tb == Type::Dt || tb == Type::Tod) && e.op == "+") return shifted(tb, x + y);
            if (ta == tb && e.op == "-") return Value::time(x - y);
            return core::fail(core::ErrorCode::InvalidArgument,
                              "'" + e.op + "' entre " + std::string(toString(ta)) + " et " + std::string(toString(tb))
                                  + " : on \xC3\xA9" "crit DT + TIME, DT - TIME, TOD + TIME, DT - DT, DATE - DATE, TOD - TOD");
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

    core::Result<Value> call(const Expr& e, std::string_view chosen = {}) {
        // The callee is either a plain name (a function) or a member/reference
        // naming a function-block instance.
        auto target = qualify(*e.lhs);
        if (!target) return core::Err<core::Error>(target.error());
        // 1.11.20 : une fonction du projet surchargee - la sienne, sous sa cle ("Convertir#615") ;
        // `chosen` : deja choisie (le dialecte).
        if (!chosen.empty()) {
            *target = std::string(chosen);
        } else if (env_.overloads(*target) > 1) {
            std::string key, why;
            if (!env_.chooseOverload(*target, shapesOf(e), key, why)) {
                fail(e.line, why);
                return core::fail(core::ErrorCode::InvalidArgument, why);
            }
            *target = key;
        }

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
        // 1.11.17 : pendant l'appel, l'appele peut lire nos locales (readCallerLocal).
        bool called = false;
        {
            const CallerScope caller(this);
            called = env_.call(*target, *target, arguments, result);
        }
        if (!called) {
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
                if (dialect_) {                      // 1.12.19 : un appel seul rend ce qu'il veut (une liste...) : c'est ignore
                    // 1.13.0 : WAIT_UNTIL(...) seul en instruction - sa valeur est ignoree : un delai
                    // depasse LEVE E_ATTENTE (contre FALSE quand la valeur est employee).
                    if (isNamedCall(*s.value, "WAIT_UNTIL")) waitUntilIgnored_ = true;
                    auto r = rich(*s.value);
                    waitUntilIgnored_ = false;
                    if (!r && !aborted_) fail(s.line, r.error().message());
                    return aborted_ ? Flow{Flow::Kind::Aborted} : Flow{};
                }
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
                // 1.12.1 (dialecte IHM) : un CASE sur un texte - ses bras 'Auto', 'Manu': (a la casse pres).
                if (dialect_ && selector->type() == Type::String) {
                    const std::string text = selector->asString();
                    for (const auto& arm : s.arms)
                        for (const auto& t : arm.texts)
                            if (t == text) return sequence(arm.body, s.line);
                    return sequence(s.elseBody, s.line);
                }
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
            case Stmt::Kind::Continue: return {Flow::Kind::Continue};   // 1.12.1
            case Stmt::Kind::Try: return tryBlock(s);                  // 1.12.1
            case Stmt::Kind::Raise:   return raiseStmt(s);             // 1.13.0
            case Stmt::Kind::Rethrow: return rethrowStmt(s);          // 1.13.0
        }
        return {};
    }

    // 1.13.0 : une erreur gardee par TRY. `code` vide : une erreur du moteur (traduite en E_* par
    // l'environnement au moment du CATCH) ; non vide : un LEVER (ou E_ATTENTE) au message deja final.
    struct Caught { bool any{false}; std::string code; std::string message; std::uint32_t line{0}; };

    // 1.13.0 : le code et le message (final, trous remplis) d'une erreur telle que CATCH e la lit :
    // un LEVER (ou E_ATTENTE) garde les siens ; une erreur du moteur recoit son code E_* et son
    // message francais de l'environnement (caughtCode, caughtMessage).
    std::string errorCode(const Caught& c) { return c.code.empty() ? env_.caughtCode(c.message) : c.code; }
    std::string errorMessage(const Caught& c) { return c.code.empty() ? env_.caughtMessage(c.message) : c.message; }

    // 1.13.0 : le type natif T_Erreur (Code STRING, Message STRING, Ligne DINT), en lecture seule.
    static const TypeRef& errorType() {
        static const TypeRef t = [] {
            auto d = std::make_shared<TypeDesc>();
            d->kind = TypeDesc::Kind::Struct;
            d->name = "T_Erreur";
            d->members = {{"Code", scalarType(Type::String)}, {"Message", scalarType(Type::String)},
                          {"Ligne", scalarType(Type::DInt)}};
            return TypeRef(d);
        }();
        return t;
    }
    // 1.13.0 : lie `name` (non declare) a un T_Erreur en lecture seule dans la portee du CATCH.
    void bindError(const std::string& name, const Caught& c) {
        auto obj = makeObj(errorType());
        obj->items[0]->value = Value::text(errorCode(c));
        obj->items[1]->value = Value::text(errorMessage(c));
        obj->items[2]->value = Value::integer(Type::DInt, static_cast<std::int64_t>(c.line));
        frames_.back().scopes.back().names[upperOf(name)] = Place{obj, {}, /*constant*/true, false};
    }

    // 1.12.1 : TRY ... CATCH [Erreur] ... END_TRY. Le corps s'execute ; une erreur (fail) y
    // est gardee au lieu d'arreter le script : le CATCH s'execute, le message dans Erreur.
    // 1.13.0 : CATCH e (un nom non declare) lie e a un T_Erreur (e.Code, e.Message, e.Ligne ;
    // e seul = e.Message) ; un nom deja declare (STRING) recoit le message comme en 1.12.1.
    Flow tryBlock(const Stmt& s) {
        const Caught outer = caught_;
        caught_ = {};
        ++catching_;
        const Flow flow = sequence(s.body, s.line);
        --catching_;
        const Caught got = caught_;
        caught_ = outer;
        if (!got.any) return flow;                       // rien : Normal, EXIT, RETURN, CONTINUE...
        aborted_ = false;
        // 1.13.0 : un nom deja declare (une locale, ou une variable du projet) recoit le message comme
        // en 1.12.1 (CATCH Erreur) ; un nom NON declare (le cas recommande, CATCH e) devient le T_Erreur.
        const bool declared = !s.loopVariable.empty() && (findLocal(s.loopVariable) != nullptr || env_.exists(s.loopVariable));
        if (!s.loopVariable.empty() && declared
            && !assignName(s.loopVariable, Value::text(errorMessage(got)), s.line))
            return {Flow::Kind::Aborted};
        if (!s.loopVariable.empty() && !declared) bindError(s.loopVariable, got);
        handling_.push_back(got);                        // 1.13.0 : RELEVER reemet cette erreur
        const Flow out = sequence(s.elseBody, s.line);
        handling_.pop_back();
        if (!s.loopVariable.empty() && !declared)
            frames_.back().scopes.back().names.erase(upperOf(s.loopVariable));   // e ne vit que le handler
        return out;
    }

    // 1.13.0 : un appel nomme (WAIT_UNTIL(...)) ecrit tel quel, au premier plan de l'expression.
    static bool isNamedCall(const Expr& e, std::string_view upperName) {
        return e.kind == Expr::Kind::Call && e.lhs && e.lhs->kind == Expr::Kind::Reference
               && upperOf(e.lhs->name) == upperName;
    }

    // 1.13.0 : LEVER('E_Code' [, 'message']) - leve une erreur a soi. Dans un TRY : gardee pour son
    // CATCH (comme une erreur du moteur) ; hors d'un TRY : le script s'arrete, la Console la montre.
    Flow raiseStmt(const Stmt& s) {
        auto codeV = evaluate(*s.value);
        if (!codeV) { if (!aborted_) fail(s.line, codeV.error().message()); return {Flow::Kind::Aborted}; }
        if (codeV->type() != Type::String)
            return failAt(s.line, "LEVER : le code est un texte (ex. 'E_Niveau'), pas " + std::string(toString(codeV->type())));
        std::string code = codeV->asString();
        if (code.empty()) return failAt(s.line, "LEVER : le code ne doit pas \xC3\xAAtre vide (ex. 'E_Niveau')");
        std::string message;
        if (s.condition) {
            auto msgV = evaluate(*s.condition);
            if (!msgV) { if (!aborted_) fail(s.line, msgV.error().message()); return {Flow::Kind::Aborted}; }
            if (msgV->type() != Type::String)
                return failAt(s.line, "LEVER : le message est un texte, pas " + std::string(toString(msgV->type())));
            // Les trous {expr[:format]} remplis ici, comme IHM_JOURNAL : le texte final est fige.
            const CallerScope caller(this);
            message = env_.interpolateMessage(msgV->asString());
        }
        return raise(Caught{true, std::move(code), std::move(message), s.line});
    }

    // 1.13.0 : RELEVER - reemet l'erreur du CATCH courant (meme code, meme message, meme ligne) vers
    // le TRY englobant, ou arrete le script si aucun. Hors d'un CATCH : refuse (le controle le dit).
    Flow rethrowStmt(const Stmt& s) {
        if (handling_.empty())
            return failAt(s.line, "RELEVER n'est permis que dans un CATCH");
        return raise(handling_.back());
    }

    // 1.13.0 : garde l'erreur pour le TRY englobant (son CATCH), ou - aucun - l'ecrit a la Console et
    // arrete le script, avec son code en tete (E_Niveau : niveau hors plage...).
    Flow raise(Caught c) {
        if (catching_ > 0 && !caught_.any) {
            caught_ = std::move(c);
            aborted_ = true;
            return {Flow::Kind::Aborted};
        }
        std::string shown = c.code.empty() ? errorMessage(c) : c.code + (c.message.empty() ? "" : " : " + c.message);
        env_.report(Diagnostic{Diagnostic::Severity::Error, std::move(shown), c.line, section_});
        aborted_ = true;
        return {Flow::Kind::Aborted};
    }

    // 1.13.0 : une erreur du dialecte signalee a une ligne (LEVER mal forme, RELEVER hors CATCH) :
    // comme fail, elle est gardee par un TRY ou arrete le script.
    Flow failAt(std::uint32_t line, std::string message) {
        fail(line, std::move(message));
        return {Flow::Kind::Aborted};
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
            if (flow.kind != Flow::Kind::Normal && flow.kind != Flow::Kind::Continue) return flow;
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
            if (flow.kind != Flow::Kind::Normal && flow.kind != Flow::Kind::Continue) return flow;
        }
        return {};
    }

    Flow repeatLoop(const Stmt& s) {
        std::uint32_t iterations = 0;
        for (;;) {
            const auto flow = sequence(s.body, s.line);
            if (flow.kind == Flow::Kind::Exit)   break;
            if (flow.kind != Flow::Kind::Normal && flow.kind != Flow::Kind::Continue) return flow;
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
        bool        setItem{false};       // 1.12.7 : un element d'un SET (S[0], FOR EACH x IN S) : il se lit, ne s'ecrit pas
    };

    // 1.12.19 : un rappel appele depuis l'exterieur (callCallback) : ses valeurs, ce qu'il rend.
    core::Result<Value> callFromOutside(const Obj& cb, const std::vector<Value>& args) {
        dialect_ = true;
        if (frames_.empty()) {
            frames_.emplace_back();
            frames_.back().scopes.emplace_back();
        }
        std::vector<RV> values;
        for (const auto& v : args) values.push_back(RV{v, nullptr});
        auto r = invokeCallback(cb, values, 0, section_);
        if (!r) return core::Err<core::Error>(r.error());
        if (r->o) return Value::boolean(true);
        return r->v;
    }

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
    // 1.12.7 : SET OF T_Lot - une structure, une liste, un TUPLE ne se rangent pas dans un SET.
    static std::string setElementRefused(const TypeDesc& t) {
        return t.text() + " : un SET range des valeurs simples (des nombres, des textes, des dur\xC3\xA9" "es, une \xC3\xA9num\xC3\xA9ration)"
               " \xE2\x80\x94 pour des structures, une LIST et LIST_SORT_BY";
    }
    static constexpr const char* kSetItemRefused =
        "un \xC3\xA9l\xC3\xA9ment d'un SET ne s'\xC3\xA9" "crit pas en place \xE2\x80\x94 SET_REMOVE(S, ancienne) puis SET_ADD(S, nouvelle) :"
        " le SET reste rang\xC3\xA9, chaque valeur une fois";
    // Ce qu'un SET refuse : l'ecrire a une place (il se range tout seul, chaque valeur une fois).
    core::Err<core::Error> setRefused(const std::string& u) const {
        return dialectError(u + " : un SET se range tout seul (chaque valeur une fois, dans l'ordre) \xE2\x80\x94 SET_ADD et SET_REMOVE l'\xC3\xA9" "crivent");
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
                // 1.12.20 (lot G) : T_Mesure<T> dans une fonction generique : son T deduit (T_Mesure<REAL>).
                if (!typeBindings_.empty() && t->name.find('<') != std::string::npos)
                    if (std::string bound = boundGenericName(t->name); bound != t->name) {
                        auto d = std::make_shared<TypeDesc>(*t);
                        d->name = std::move(bound);
                        return resolve(d, depth + 1);
                    }
                const std::string key = upperOf(t->name);
                // 1.12.19 : T d'une fonction generique en cours d'appel (TEMPLATE <T>) : le type deduit.
                for (auto it = typeBindings_.rbegin(); it != typeBindings_.rend(); ++it)
                    if (const auto b = it->find(key); b != it->end()) return b->second;
                // 1.12.19 : un type rappel nomme (T_Filtre) : sa forme ecrite (CALLBACK (x : REAL) : BOOL).
                if (const std::string alias = env_.typeAlias(t->name); !alias.empty()) {
                    auto a = typeFromText(alias);
                    if (!a) return core::Err<core::Error>(a.error());
                    auto r = resolve(*a, depth + 1);
                    if (!r) return r;
                    if ((*r)->kind == TypeDesc::Kind::Callback) {
                        auto named = std::make_shared<TypeDesc>(**r);
                        named->name = t->name;
                        structs_[key] = named;
                        return TypeRef(named);
                    }
                    structs_[key] = *r;
                    return r;
                }
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
            case TypeDesc::Kind::Map:
            case TypeDesc::Kind::List: {
                if (!t->element) return t;           // un litteral sans type commun ([1, 'a'])
                auto e = resolve(t->element, depth + 1);
                if (!e) return e;
                // 1.12.7 : un SET range des valeurs simples (std::set les compare par '<').
                if (isSetType(*t) && (*e)->kind != TypeDesc::Kind::Scalar) return dialectError(setElementRefused(*t));
                if (*e == t->element) return t;
                auto d = std::make_shared<TypeDesc>(*t);
                d->element = *e;
                return TypeRef(d);
            }
            case TypeDesc::Kind::Tuple: {           // 1.12.2 : ses types, dans l'ordre
                bool same = true;
                auto d = std::make_shared<TypeDesc>(*t);
                for (auto& m : d->members) {
                    auto r = resolve(m.second, depth + 1);
                    if (!r) return r;
                    same = same && *r == m.second;
                    m.second = *r;
                }
                return same ? t : TypeRef(d);
            }
            case TypeDesc::Kind::Ref:
            case TypeDesc::Kind::Pointer:
                return t;            // la cible n'est pas instanciee : resolue a l'emploi
            case TypeDesc::Kind::Callback: {        // 1.12.19 : ses parametres et son retour
                bool same = true;
                auto d = std::make_shared<TypeDesc>(*t);
                for (auto& m : d->members) {
                    if (!m.second) continue;
                    auto r = resolve(m.second, depth + 1);
                    if (!r) return r;
                    same = same && *r == m.second;
                    m.second = *r;
                }
                if (d->element) {
                    auto r = resolve(d->element, depth + 1);
                    if (!r) return r;
                    same = same && *r == d->element;
                    d->element = *r;
                }
                return same ? t : TypeRef(d);
            }
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
        if (target == "SINT") t = Type::Int;
        if (target == "USINT") t = Type::UInt;               // 1.12.5 (D16) : TO_LINT, TO_ULINT, TO_LWORD sur 64 bits
        if (t == Type::Unknown || t == Type::String) return false;
        std::int64_t n = 0;
        if (v.type() == Type::String) n = std::strtoll(v.asString().c_str(), nullptr, 10);
        else if (v.type() == Type::Real) n = static_cast<std::int64_t>(std::llround(v.asReal()));
        else n = v.asInteger();
        // 1.11.25 : SINT et USINT ramenes a 8 bits (TO_SINT(200) = -56), comme X_TO_SINT.
        if (target == "SINT") n = static_cast<std::int8_t>(static_cast<std::uint8_t>(n & 0xFF));
        if (target == "USINT") n &= 0xFF;
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

    // 1.11.20 : les fonctions internes de ce nom (plusieurs : des surcharges).
    std::vector<const Function*> findFunctions(std::string_view name) const {
        std::vector<const Function*> out;
        if (!program_) return out;
        const std::string u = upperOf(name);
        for (const auto& f : program_->functions)
            if (upperOf(f->name) == u) out.push_back(f.get());
        return out;
    }

    // 1.11.20 : les arguments d'un appel, vus sans les evaluer (le choix d'une surcharge) :
    // comme le controle les voit (Typer), et une variable au type inconnu prend celui de sa
    // valeur (la lire n'ecrit rien).
    std::vector<ArgShape> shapesOf(const Expr& call) {
        Typer typer(env_, program_, [this](std::string_view n) -> std::string {
            const Place* p = findLocal(n);
            if (!p) return {};
            if (p->obj && p->obj->type) return p->obj->type->text();
            if (!p->name.empty()) return env_.declaredType(p->name);
            return {};
        });
        auto shapes = typer.shapesOf(call);
        std::size_t k = 0;
        const auto fill = [&](const ExprPtr& x) {
            ArgShape& sh = shapes[k++];
            if (!sh.designator || !sh.type.empty() || !x) return;
            const std::string name = dottedName(*x);
            Value v;
            if (!name.empty() && !findLocal(name) && env_.read(name, v)) sh.type = valueTypeName(v.type());
        };
        for (const auto& a : call.arguments)
            if (a.second) fill(a.second);
        for (const auto& o : call.outputs)
            if (o.second) fill(o.second);
        return shapes;
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
                case Expr::Kind::Reference:
                    // 1.12.4 : ou un nom qui porte des membres objets (Recette.Etapes).
                    return findLocal(x->name) != nullptr || (dialect_ && (env_.richVariable(x->name) || env_.hasRichMembers(x->name)));
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
                // 1.12.7 : une valeur calculee au demarrage passe avant la valeur ecrite.
                if (ObjRef given = limits_.initials ? limits_.initials->find(d.name) : nullptr) {
                    std::string why;
                    if (!assignObj(*obj, *given, &why)) {
                        fail(d.line, d.name + " : " + why);
                        return false;
                    }
                } else if (d.initial && !initialize(*obj, *d.initial, d.line)) {
                    return false;
                }
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
        // 1.12.19 : un rappel recoit aussi le nom d'une fonction en texte ('Carre') - la maquette (decision 6).
        if (!v.o && into.type->kind == TypeDesc::Kind::Callback) return storeCallback(into, v, why);
        if (!v.o) {
            if (into.type->kind == TypeDesc::Kind::Scalar) {
                into.value.assignFrom(v.v);
                return true;
            }
            if (why) *why = "une valeur simple ne va pas dans un " + into.type->text();
            return false;
        }
        if (&into == v.o.get()) return true;
        // 1.12.19 : un rappel recoit une fonction (son nom, une LAMBDA, un autre rappel) ou NULL.
        if (into.type->kind == TypeDesc::Kind::Callback) return storeCallback(into, v, why);
        // NULL dans une reference ou un pointeur.
        if (v.o->type && v.o->type->kind == TypeDesc::Kind::Pointer && !v.o->type->element
            && (into.type->kind == TypeDesc::Kind::Ref || into.type->kind == TypeDesc::Kind::Pointer)) {
            into.bound = false;
            into.target.reset();
            into.targetName.clear();
            return true;
        }
        // 1.12.2 : un litteral ([1, 2], ['a' := 1], (1, 'x')) se convertit dans le type de la
        // destination ; une liste, un vecteur et un tableau s'echangent leurs elements.
        if (v.o->type && (v.o->type->literal || convertibleSequences(*into.type, *v.o->type))) return fromSequence(into, *v.o, why);
        // Une reference et un pointeur de meme cible s'echangent (REF_TO T := ADR(x)).
        if ((into.type->kind == TypeDesc::Kind::Ref || into.type->kind == TypeDesc::Kind::Pointer) && v.o->type
            && (v.o->type->kind == TypeDesc::Kind::Ref || v.o->type->kind == TypeDesc::Kind::Pointer)) {
            // 1.13.0 (lot J1) : la MONTEE - une REF_TO sous-type va dans une REF_TO parent, et une REF_TO d'un
            // type va dans une REF_TO d'une interface qu'il implemente (une liste heterogene par le contrat).
            if (into.type->element && v.o->type->element && !compatibleTarget(*into.type->element, *v.o->type->element)
                && !env_.typeConvertsTo(v.o->type->element->text(), into.type->element->text())) {
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

    // ---- 1.12.2 : les litteraux et les suites ------------------------------
    static bool isSequence(const TypeDesc& t) {
        return t.kind == TypeDesc::Kind::List || t.kind == TypeDesc::Kind::Array || t.kind == TypeDesc::Kind::Tuple;
    }
    // Une liste vers un tableau (et l'inverse), d'elements simples de types differents : convertis un a un.
    static bool convertibleSequences(const TypeDesc& into, const TypeDesc& from) {
        if (!isSequence(into) || !isSequence(from) || into.kind == TypeDesc::Kind::Tuple || from.kind == TypeDesc::Kind::Tuple) return false;
        if (into.kind == from.kind && sameType(into, from)) return false;      // la copie ordinaire
        if (into.kind == TypeDesc::Kind::Array && from.kind == TypeDesc::Kind::Array) return false;   // bornes differentes : refuse
        return true;
    }
    static RV itemValue(const ObjRef& o) {
        if (o && o->type && o->type->kind == TypeDesc::Kind::Scalar) return RV{o->value, nullptr};
        return RV{{}, o};
    }
    // Les elements d'un litteral a ranger dans un tableau a plusieurs dimensions :
    // [[1, 2], [3, 4]] se lit ligne par ligne, comme [1, 2, 3, 4].
    static void flattenItems(const Obj& o, std::size_t levels, std::vector<ObjRef>& out) {
        for (const auto& i : o.items) {
            if (levels > 1 && i && i->type && i->type->literal && i->type->kind == TypeDesc::Kind::List) flattenItems(*i, levels - 1, out);
            else out.push_back(i);
        }
    }
    // Un element d'un litteral dans une case simple : un texte ne devient pas un nombre en cachette
    // ([1, 'a'] pour une LIST OF REAL : refuse) ; le reste se convertit comme une affectation.
    static bool itemFits(const Obj& into, const RV& v, std::string* why) {
        if (!into.type || into.type->kind != TypeDesc::Kind::Scalar || v.o) return true;
        const Type want = into.type->scalar, got = v.v.type();
        if (got == Type::String && want != Type::String && want != Type::Unknown) {
            if (why) *why = "'" + v.v.asString() + "' (un texte) ne va pas dans un " + into.type->text();
            return false;
        }
        return true;
    }
    bool storeItem(Obj& into, const RV& v, std::string* why) { return itemFits(into, v, why) && store(into, v, why); }

    bool fromSequence(Obj& into, const Obj& from, std::string* why) {
        const auto& t = *into.type;
        const auto& ft = *from.type;
        const auto fail = [&](std::string w) {
            if (why) *why = std::move(w);
            return false;
        };
        // 1.12.14 : une structure ecrite (Nom := 'A', Niveau := 2.5) - membre par membre, par leur nom ;
        // ceux qui manquent gardent leur valeur.
        if (ft.kind == TypeDesc::Kind::Struct) {
            if (t.kind != TypeDesc::Kind::Struct)
                return fail("une structure (Membre := valeur, ...) ne va pas dans un " + t.text());
            for (std::size_t k = 0; k < ft.members.size() && k < from.items.size(); ++k) {
                std::size_t at = t.members.size();
                for (std::size_t j = 0; j < t.members.size(); ++j)
                    if (upperOf(t.members[j].first) == upperOf(ft.members[k].first)) at = j;
                if (at >= t.members.size() || at >= into.items.size() || !into.items[at])
                    return fail(t.name + " n'a pas de membre " + ft.members[k].first);
                if (!storeItem(*into.items[at], itemValue(from.items[k]), why)) {
                    if (why) *why = ft.members[k].first + " : " + *why;
                    return false;
                }
            }
            return true;
        }
        if (ft.kind == TypeDesc::Kind::Map) {
            if (t.kind != TypeDesc::Kind::Map) return fail("une MAP (['cl\xC3\xA9' := valeur]) ne va pas dans un " + t.text());
            std::map<std::string, ObjRef> texts;
            std::map<std::int64_t, ObjRef> ints;
            const auto put = [&](const RV& key, const ObjRef& value) -> bool {
                std::string text;
                std::int64_t integer = 0;
                auto isText = mapKey(into, key, text, integer);
                if (!isText) return fail(isText.error().message());
                auto made = instantiate(t.element);
                if (!made) return fail(made.error().message());
                if (!storeItem(**made, itemValue(value), why)) return false;
                if (*isText) texts[text] = *made;
                else ints[integer] = *made;
                return true;
            };
            for (const auto& [k, v] : from.textKeys)
                if (!put(RV{Value::text(k), nullptr}, v)) return false;
            for (const auto& [k, v] : from.intKeys)
                if (!put(RV{Value::integer(isInteger(ft.key) ? ft.key : Type::DInt, k), nullptr}, v)) return false;
            into.textKeys = std::move(texts);
            into.intKeys = std::move(ints);
            return true;
        }
        if (t.kind == TypeDesc::Kind::Map) {
            if (from.items.empty()) {                // [] : la MAP se vide
                into.textKeys.clear();
                into.intKeys.clear();
                return true;
            }
            return fail("une MAP s'\xC3\xA9" "crit ['cl\xC3\xA9' := valeur, ...], pas [valeur, ...]");
        }
        switch (t.kind) {
            case TypeDesc::Kind::List: {
                std::vector<ObjRef> items;
                items.reserve(from.items.size());
                for (const auto& i : from.items) {
                    auto made = instantiate(t.element);
                    if (!made) return fail(made.error().message());
                    if (!storeItem(**made, itemValue(i), why)) return false;
                    items.push_back(*made);
                }
                into.items = std::move(items);
                normalizeList(into);         // 1.12.7 : un SET se range (sans doublon), un RING garde les N dernieres
                return true;
            }
            case TypeDesc::Kind::Array: {
                std::vector<ObjRef> flat;
                flattenItems(from, t.bounds.size(), flat);
                if (flat.size() > into.items.size())
                    return fail("trop de valeurs : " + std::to_string(flat.size()) + " pour les " + std::to_string(into.items.size())
                                + " cases d'un " + t.text());
                for (std::size_t k = 0; k < into.items.size(); ++k) {
                    if (k < flat.size()) {
                        if (!storeItem(*into.items[k], itemValue(flat[k]), why)) return false;
                    } else {
                        auto made = instantiate(t.element);                 // les cases suivantes : leur valeur par defaut
                        if (!made) return fail(made.error().message());
                        into.items[k] = *made;
                    }
                }
                return true;
            }
            case TypeDesc::Kind::Tuple:
                if (from.items.size() != into.items.size())
                    return fail(std::to_string(from.items.size()) + " valeur(s) pour un " + t.text() + " (" + std::to_string(into.items.size())
                                + " attendue(s))");
                for (std::size_t k = 0; k < into.items.size(); ++k)
                    if (!storeItem(*into.items[k], itemValue(from.items[k]), why)) return false;
                return true;
            case TypeDesc::Kind::Scalar:
                return fail("une liste de valeurs ne va pas dans un " + t.text() + " (une valeur simple)");
            default:
                return fail("une liste de valeurs ne va pas dans un " + t.text());
        }
    }
    // Le type d'un litteral [a, b] : celui de ses elements s'ils en ont un commun (les
    // entiers : DINT ; un reel parmi eux : REAL), sinon aucun (il ne va que dans un tuple).
    static TypeRef commonElement(const std::vector<ObjRef>& items) {
        if (items.empty()) return nullptr;
        bool allScalar = true, anyReal = false, allNumeric = true;
        Type first = Type::Unknown;
        bool sameScalar = true;
        for (const auto& i : items) {
            if (!i || !i->type) return nullptr;
            if (i->type->kind != TypeDesc::Kind::Scalar) { allScalar = false; continue; }
            const Type ty = i->value.type();
            if (first == Type::Unknown) first = ty;
            else if (ty != first) sameScalar = false;
            if (ty == Type::Real) anyReal = true;
            if (!isNumeric(ty) || ty == Type::Time || ty == Type::Bool) allNumeric = false;
        }
        if (allScalar) {
            if (sameScalar) return scalarType(isInteger(first) && first != Type::Time && first != Type::Bool ? Type::DInt : first);
            if (allNumeric) return scalarType(anyReal ? Type::Real : Type::DInt);
            return nullptr;
        }
        for (const auto& i : items)
            if (i->type->kind == TypeDesc::Kind::Scalar || !sameType(*i->type, *items.front()->type)) return nullptr;
        return items.front()->type;
    }
    core::Result<RV> collection(const Expr& e) {
        auto t = std::make_shared<TypeDesc>();
        t->literal = true;
        auto o = std::make_shared<Obj>();
        if (e.op == "STRUCT") {                  // 1.12.14 : (Nom := 'A', Niveau := 2.5)
            t->kind = TypeDesc::Kind::Struct;
            for (const auto& [member, ex] : e.arguments) {
                if (!ex) continue;
                auto v = rich(*ex);
                if (!v) return v;
                ObjRef value = v->o ? v->o : makeObj(scalarType(v->v.type()));
                if (!v->o) value->value = v->v;
                t->members.emplace_back(member, value->type);
                o->items.push_back(value);
            }
            o->type = t;
            return RV{{}, o};
        }
        if (e.op == "MAP") {
            t->kind = TypeDesc::Kind::Map;
            bool textual = false, integral = false;
            std::vector<ObjRef> values;
            for (const auto& entry : e.more) {
                if (!entry || !entry->lhs || !entry->rhs) continue;
                auto k = rich(*entry->lhs);
                if (!k) return k;
                if (k->o) return dialectError("une cl\xC3\xA9 de MAP est une valeur simple, pas un " + typeNameOf(*k));
                auto v = rich(*entry->rhs);
                if (!v) return v;
                ObjRef value = v->o ? v->o : makeObj(scalarType(v->v.type()));
                if (!v->o) value->value = v->v;
                values.push_back(value);
                if (k->v.type() == Type::String) {
                    textual = true;
                    o->textKeys[k->v.asString()] = value;
                } else if (isInteger(k->v.type())) {
                    integral = true;
                    t->key = Type::DInt;
                    o->intKeys[k->v.asInteger()] = value;
                } else {
                    return dialectError("une cl\xC3\xA9 de MAP est un texte ou un entier, pas un " + typeNameOf(*k));
                }
                if (textual && integral) return dialectError("les cl\xC3\xA9s d'une MAP sont toutes des textes ou toutes des entiers");
            }
            if (!integral) t->key = Type::String;
            t->element = commonElement(values);
            o->type = t;
            return RV{{}, o};
        }
        t->kind = e.op == "TUPLE" ? TypeDesc::Kind::Tuple : TypeDesc::Kind::List;
        for (const auto& item : e.more) {
            if (!item) continue;
            auto v = rich(*item);
            if (!v) return v;
            ObjRef value = v->o ? v->o : makeObj(scalarType(v->v.type()));
            if (!v->o) value->value = v->v;
            o->items.push_back(value);
        }
        if (t->kind == TypeDesc::Kind::Tuple) {
            for (std::size_t k = 0; k < o->items.size(); ++k) t->members.emplace_back("Item" + std::to_string(k + 1), o->items[k]->type);
        } else {
            t->name = "LIST";
            t->element = commonElement(o->items);
        }
        o->type = t;
        return RV{{}, o};
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
    // 1.13.0 : e seul (un T_Erreur) se lit comme e.Message - compatible avec le CATCH Erreur de
    // la 1.12.1, qui recevait deja le message.
    static bool isErrorObj(const ObjRef& o) {
        return o && o->type && o->type->kind == TypeDesc::Kind::Struct && o->type->name == "T_Erreur";
    }
    core::Result<RV> readPlace(const Place& p) {
        if (p.obj) {
            if (isErrorObj(p.obj) && p.obj->items.size() > 1) return RV{p.obj->items[1]->value, nullptr};
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
        // 1.12.4 : un membre objet de l'environnement (une liste dans une structure IHM) : sa copie.
        if (dialect_ && o.type->kind != TypeDesc::Kind::Scalar)
            if (auto src = env_.richVariable(name)) {
                std::string why;
                return assignObj(o, *src, &why);
            }
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
            case TypeDesc::Kind::Tuple:
                for (std::size_t k = 0; k < o.items.size() && k < o.type->members.size(); ++k)
                    if (!loadFromEnv(*o.items[k], name + "." + o.type->members[k].first)) return false;
                return true;
            default: return false;
        }
    }

    bool storeToEnv(const std::string& name, const Obj& o, std::string* why) {
        if (!o.type) return false;
        // 1.12.4 : un membre objet de l'environnement : ecrit en place, entier.
        if (dialect_ && o.type->kind != TypeDesc::Kind::Scalar)
            if (auto dst = env_.richVariable(name)) return assignObj(*dst, o, why);
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
            case TypeDesc::Kind::Tuple:
                for (std::size_t k = 0; k < o.items.size() && k < o.type->members.size(); ++k)
                    if (!storeToEnv(name + "." + o.type->members[k].first, *o.items[k], why)) return false;
                return true;
            default:
                if (why) *why = "un " + o.type->text() + " ne s'\xC3\xA9" "crit pas dans '" + name + "' (une variable du projet de taille fixe)";
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
        if (p.setItem) {
            if (why) *why = kSetItemRefused;
            return false;
        }
        if (p.constant) {
            if (why) *why = isErrorObj(p.obj)       // 1.13.0 : e := ... (le T_Erreur entier)
                                ? "l'erreur attrap\xC3\xA9" "e est en lecture seule (Code, Message, Ligne)"
                                : "une constante ne s'\xC3\xA9" "crit pas";
            return false;
        }
        if (p.obj) return store(*p.obj, v, why);
        if (!v.o) {
            if (env_.write(p.name, v.v)) return true;
            if (why) *why = "'" + p.name + "' cannot be written";
            return false;
        }
        // 1.12.2 : un litteral ([1, 2, 3]) ou une liste vers un tableau, une structure de
        // l'environnement : converti dans son type, puis ecrit case par case.
        if (v.o->type && (v.o->type->literal || v.o->type->kind == TypeDesc::Kind::List)) {
            auto target = materialize(p.name);
            if (!target) {
                if (why) *why = target.error().message();
                return false;
            }
            if (!store(**target, v, why)) return false;
            return storeToEnv(p.name, **target, why);
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
                if (dialect_)                                   // 1.12.2 : une variable du projet a taille variable
                    if (auto o = env_.richVariable(e.name)) return Place{o, {}, env_.constantVariable(e.name), false};
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
                if (!p.obj) {
                    // 1.12.4 : un membre objet d'une structure de l'environnement (Recette.Etapes) : son objet.
                    std::string name = p.name + "." + e.name;
                    if (dialect_)
                        if (auto o = env_.richVariable(name)) return Place{o, {}, p.constant || env_.constantVariable(name), false};
                    return Place{nullptr, std::move(name), p.constant, false};
                }
                // Une reference ou un pointeur se suit tout seul : r.Membre = r^.Membre.
                while (isIndirect(p.obj)) {
                    auto t = follow(p.obj);
                    if (!t) return t;
                    if (!t->obj) return Place{nullptr, t->name + "." + e.name, false, false};
                    p = *t;
                }
                const auto& type = *p.obj->type;
                const std::string m = upperOf(e.name);
                if (type.kind == TypeDesc::Kind::Struct || type.kind == TypeDesc::Kind::Tuple) {
                    for (std::size_t k = 0; k < type.members.size() && k < p.obj->items.size(); ++k)
                        if (upperOf(type.members[k].first) == m) return Place{p.obj->items[k], {}, p.constant, p.temp};
                    if (type.kind == TypeDesc::Kind::Tuple)
                        return dialectError("un " + type.text() + " a Item1 \xC3\xA0 Item" + std::to_string(type.members.size()) + ", pas '" + e.name + "'");
                    if (type.name == "T_Erreur")            // 1.13.0 : l'erreur attrapee
                        return dialectError("T_Erreur n'a que Code, Message et Ligne, pas '" + e.name + "'");
                    return dialectError(type.name + " n'a pas de membre '" + e.name + "'");
                }
                // 1.12.2 : une liste, un vecteur - L.First, L.Last (le premier, le dernier element).
                if (type.kind == TypeDesc::Kind::List && (m == "FIRST" || m == "LAST")) {
                    if (p.obj->items.empty()) return dialectError("'" + e.name + "' : la liste est vide (L.Count = 0)");
                    if (create && isSetType(type)) return dialectError(kSetItemRefused);       // 1.12.7 : S.First se lit
                    return Place{m == "FIRST" ? p.obj->items.front() : p.obj->items.back(), {}, p.constant, p.temp, isSetType(type)};
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
                } else if (type.kind == TypeDesc::Kind::List) {
                    if (m == "LENGTH" || m == "SIZE" || m == "COUNT") { value = static_cast<std::int64_t>(p.obj->items.size()); known = true; }
                    else if (m == "LOW") { value = 0; known = true; }
                    else if (m == "HIGH") { value = static_cast<std::int64_t>(p.obj->items.size()) - 1; known = true; }
                    else if ((m == "CAPACITY" || m == "MAXSIZE") && isRingType(type)) { value = type.capacity; known = true; }     // 1.12.7 : RING[N] : N
                    else if (m == "EMPTY" || m == "ISEMPTY" || (m == "FULL" && isRingType(type))) {
                        auto tmp = makeObj(scalarType(Type::Bool));
                        tmp->value = Value::boolean(m == "FULL" ? static_cast<std::int64_t>(p.obj->items.size()) >= type.capacity : p.obj->items.empty());
                        return Place{tmp, {}, true, true};
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
                    cell += "]";
                    // 1.12.4 : la case d'un tableau de l'environnement qui est un objet (un membre objet).
                    if (dialect_)
                        if (auto o = env_.richVariable(cell)) return Place{o, {}, p.constant, false};
                    return Place{nullptr, std::move(cell), p.constant, false};
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
                if (type.kind == TypeDesc::Kind::List) {
                    // 1.12.2 : L[0] .. L[L.Count - 1] (une case au-dela : LIST_ADD, VECTOR_PUSH).
                    if (indices.size() != 1) return dialectError("une liste prend un seul indice : L[i]");
                    if (indices[0].o || !(isInteger(indices[0].v.type()) || indices[0].v.type() == Type::Bool))
                        return dialectError("un indice de liste est un entier");
                    const std::int64_t i = indices[0].v.asInteger();
                    const auto n = static_cast<std::int64_t>(p.obj->items.size());
                    // 1.12.7 : S[i] d'un SET se lit (la i-eme plus petite valeur), ne s'ecrit pas.
                    const bool setItem = isSetType(type);
                    if (setItem && create) return dialectError(kSetItemRefused);
                    if (i < 0 || i >= n)
                        return dialectError("indice " + std::to_string(i) + (n == 0 ? std::string(" : la liste est vide")
                                                                                   : " hors de la liste (0.." + std::to_string(n - 1) + ")"));
                    return Place{p.obj->items[static_cast<std::size_t>(i)], {}, p.constant, p.temp, setItem};
                }
                return dialectError("'[...]' : un " + type.text() + " n'est ni un tableau, ni une liste, ni une MAP");
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
            case Expr::Kind::Conditional: {
                // 1.12.1 : seul le cote choisi est lu (x <> 0 ? 100 / x : 0).
                auto c = rich(*e.lhs);
                if (!c) return c;
                if (c->o || c->v.type() != Type::Bool)
                    return dialectError("'?' : la condition doit \xC3\xAAtre un BOOL (c ? a : b), pas un " + typeNameOf(*c));
                return rich(c->v.isTruthy() ? *e.rhs : *e.rhs2);
            }
            case Expr::Kind::Coalesce: {
                // 1.12.1 : a ?? b - b quand a ne se lit pas (un nom inconnu, une case hors du
                // tableau, une cle absente, une division par zero) ou vaut NULL.
                ++quiet_;
                auto a = rich(*e.lhs);
                --quiet_;
                if (a && !(a->o && a->o->type && a->o->type->kind == TypeDesc::Kind::Pointer && !a->o->type->element)) return a;
                return rich(*e.rhs);
            }
            case Expr::Kind::InList: {
                auto x = rich(*e.lhs);
                if (!x) return x;
                for (const auto& item : e.more) {
                    if (!item) continue;
                    if (item->kind == Expr::Kind::Between && !item->lhs) {          // une plage a..b de la liste
                        auto in = between(*item, *x);
                        if (!in) return in;
                        if (in->v.isTruthy()) return RV{Value::boolean(true), nullptr};
                        continue;
                    }
                    Value lit;                                                      // Mode IN [Auto, Manu]
                    auto v = bareName(*item) && bareEnum(enumTypeOf(*e.lhs), *item, lit) ? core::Result<RV>(RV{lit, nullptr}) : rich(*item);
                    if (!v) return v;
                    auto same = compareRich(e, "=", *x, *v);
                    if (!same) return same;
                    if (same->v.isTruthy()) return RV{Value::boolean(true), nullptr};
                }
                return RV{Value::boolean(false), nullptr};
            }
            case Expr::Kind::Between: {
                if (!e.lhs) return dialectError("une plage a..b se lit dans une liste de IN");
                auto x = rich(*e.lhs);
                if (!x) return x;
                return between(e, *x);
            }
            case Expr::Kind::Collection: return collection(e);       // 1.12.2
            case Expr::Kind::Lambda: return makeLambda(e);           // 1.12.19
            case Expr::Kind::Reference:
            case Expr::Kind::Member:
            case Expr::Kind::Index:
            case Expr::Kind::Deref: {
                // 1.12.7 : NUMERIC_LIMITS<UINT>.DIGITS, NUMBERS<REAL>.PI, NUMBERS.PI (une constante).
                if (e.kind == Expr::Kind::Member)
                    if (auto c = templateMemberValue(e, false)) return std::move(*c);
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
                if (auto o = env_.richVariable(*name)) return RV{{}, o};      // 1.12.4 : un membre objet
                // 1.12.7 : COMPARAISON#Superieur, OPERATION#Somme, TRANSFORMATION#Carre (si l'environnement ne
                // les a pas lues : le rang de la valeur, un DINT).
                if (std::int64_t n = 0; e.kind == Expr::Kind::Reference && lib::dialectEnumValue(*name, n))
                    return RV{Value::integer(Type::DInt, n), nullptr};
                if (!env_.exists(*name)) {
                    auto obj = materialize(*name);
                    if (obj) return RV{{}, *obj};
                }
                // 1.12.19 : le nom d'une fonction, sans parentheses : un rappel vers elle (f := Est_Valide).
                if (e.kind == Expr::Kind::Reference || e.kind == Expr::Kind::Member)
                    if (auto cb = functionValue(*name)) return RV{{}, cb};
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

    // 1.13.0 (lot J1) : l'appel d'une methode d'un type IHM, chemin.Methode(args). Nul : ce n'est pas
    // une methode (l'appelant continue avec les autres resolutions). SELF (l'objet) est passe en premier
    // argument, en VAR_IN_OUT : une methode qui ecrit un membre le modifie pour de bon. Le type REEL de
    // l'objet (celui de l'objet evalue : a travers une REF_TO, la cible) choisit la methode - c'est le
    // dispatch virtuel, sans tag cache.
    std::optional<core::Result<RV>> callMethod(const Expr& e) {
        const Expr& obj = *e.lhs->lhs;
        const std::string& method = e.lhs->name;
        auto ov = rich(obj);
        if (!ov || !ov->o || !ov->o->type) return std::nullopt;     // pas un objet : pas une methode
        const std::string realType = ov->o->type->text();
        if (realType.empty()) return std::nullopt;
        OperatorSource src;
        if (!env_.findMethod(realType, method, src)) return std::nullopt;
        std::shared_ptr<const Function> f;
        if (const auto it = operatorCache().find(src.key); it != operatorCache().end()) f = it->second;
        else {
            auto parsed = parseFunction(src.function, src.owner);
            if (!parsed) return std::optional<core::Result<RV>>(dialectError(src.owner + " : " + parsed.error().message()));
            operatorCache()[src.key] = *parsed;
            f = *parsed;
        }
        // Synthetiser l'appel : SELF (l'objet) devant, puis les arguments donnes.
        Expr call2 = e;
        call2.lhs = nullptr;
        call2.arguments.clear();
        call2.arguments.emplace_back(std::string{}, e.lhs->lhs);
        for (const auto& a : e.arguments) call2.arguments.push_back(a);
        return std::optional<core::Result<RV>>(callDeclared(*f, call2, true));
    }

    // 1.12.1 : low <= x <= high (ENTRE ... ET, une plage de IN).
    core::Result<RV> between(const Expr& e, const RV& x) {
        auto low = rich(*e.rhs);
        if (!low) return low;
        auto high = rich(*e.rhs2);
        if (!high) return high;
        auto above = compareRich(e, ">=", x, *low);
        if (!above) return above;
        if (!above->v.isTruthy()) return RV{Value::boolean(false), nullptr};
        return compareRich(e, "<=", x, *high);
    }
    // Une comparaison faite pour IN, ENTRE : un noeud de comparaison a la ligne de `e`.
    core::Result<RV> compareRich(const Expr& e, const char* op, const RV& a, const RV& b) {
        Expr c;
        c.kind = Expr::Kind::Binary;
        c.op   = op;
        c.line = e.line;
        return combineRich(c, c.op, a, b);
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
        // 1.12.2 : L1 + L2, L + [x, y] - une liste neuve, du type du premier.
        if (op == "+" && a.o && b.o && a.o->type && b.o->type && a.o->type->kind == TypeDesc::Kind::List && isSequence(*b.o->type)) {
            auto out = makeObj(a.o->type);
            std::string why;
            if (!store(*out, a, &why)) return dialectError(why);
            auto tail = makeObj(a.o->type);
            if (!store(*tail, b, &why)) return dialectError(why);
            for (auto& i : tail->items) out->items.push_back(i);
            normalizeList(*out);             // 1.12.7 : S + [x] reste un SET, R + [x] un RING
            return RV{{}, out};
        }
        // = et <> entre deux tableaux, structures, MAP, references (meme cible), NULL.
        if (op == "=" || op == "<>") {
            bool equal = false;
            // 1.12.2 : un litteral prend le type de l'autre cote (L = [1, 2, 3]).
            if (a.o && b.o && a.o->type && b.o->type && (a.o->type->literal != b.o->type->literal)) {
                const RV& typed = a.o->type->literal ? b : a;
                const RV& lit = a.o->type->literal ? a : b;
                auto tmp = makeObj(typed.o->type);
                std::string why;
                equal = store(*tmp, lit, &why) && deepEquals(*tmp, *typed.o);
                return RV{Value::boolean(op == "=" ? equal : !equal), nullptr};
            }
            // 1.12.19 : un rappel compare a NULL (vide ?) ; deux rappels : la meme fonction.
            if (a.o && b.o && (isCallbackObj(a.o) || isCallbackObj(b.o))) {
                if (isNullObj(a.o) || isNullObj(b.o)) equal = !(isCallbackObj(a.o) ? a.o->bound : b.o->bound);
                else equal = isCallbackObj(a.o) && isCallbackObj(b.o) && deepEquals(*a.o, *b.o);
                return RV{Value::boolean(op == "=" ? equal : !equal), nullptr};
            }
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

    // ==== 1.12.19 : LES RAPPELS (CALLBACK, LAMBDA) ET LES GENERIQUES (TEMPLATE) ====================
    // Le code d'une LAMBDA et ce qu'elle a vu a sa creation : les locales copiees (comme [=] en C++ ; les
    // variables IHM et les parametres de vue se lisent par leur nom, tels quels a l'appel) et les types
    // generiques de la fonction qui l'a creee.
    struct LambdaCode {
        std::shared_ptr<const Function>  fn;
        std::shared_ptr<Locals>          captured;
        std::map<std::string, TypeRef>   bindings;
    };
    static bool isCallbackObj(const ObjRef& o) { return o && o->type && o->type->kind == TypeDesc::Kind::Callback; }
    static bool isNullObj(const ObjRef& o) {
        return o && o->type && o->type->kind == TypeDesc::Kind::Pointer && !o->type->element && !o->bound;
    }

    // La forme d'un rappel vers cette fonction : ses parametres (types resolus si possible) et son retour.
    TypeRef callbackTypeOf(const Function& f) {
        std::vector<std::pair<std::string, TypeRef>> params;
        const bool generic = !f.typeParams.empty();
        for (const auto& p : f.params) {
            TypeRef t;
            if (p.type && !generic) {
                ++quiet_;
                auto r = resolve(p.type);
                --quiet_;
                if (r) t = *r;
            }
            params.emplace_back(p.name, t);
        }
        TypeRef result;
        if (f.result && !generic) {
            ++quiet_;
            auto r = resolve(f.result);
            --quiet_;
            if (r) result = *r;
        }
        auto d = std::make_shared<TypeDesc>(*callbackType(std::move(params), result));
        if (generic) d->literal = true;                      // une fonction generique : sa forme se voit a l'appel
        if (f.result && generic && !d->element) d->element = scalarType(Type::Unknown);
        return TypeRef(d);
    }

    // UN NOM DE FONCTION EMPLOYE COMME UNE VALEUR (f := Est_Valide ; LIST_FILTER(L, Est_Valide)) : un rappel
    // vers elle ; nul : pas une fonction (une fonction interne du script, du projet, d'une instance de symbole).
    ObjRef functionValue(const std::string& name) {
        if (name.empty()) return nullptr;
        TypeRef type;
        std::string shown = name;
        if (const auto inner = findFunctions(name); !inner.empty()) {
            type = callbackTypeOf(*inner.front());
            shown = inner.front()->name;
            if (inner.size() > 1) {                          // des surcharges : la forme se voit a l'appel
                auto d = std::make_shared<TypeDesc>(*type);
                d->literal = true;
                type = d;
            }
        } else {
            const int count = env_.overloads(name);
            if (count < 2)
                if (auto f = env_.dialectFunction(name)) type = callbackTypeOf(*f);
            if (!type) {
                if (count < 1) return nullptr;
                auto d = std::make_shared<TypeDesc>(*callbackType({}, nullptr));
                d->literal = true;                           // la forme d'une fonction simple du projet : a l'appel
                type = d;
            }
        }
        auto obj = makeObj(type);
        obj->bound = true;
        obj->callee = shown;
        return obj;
    }

    // Ecrire dans un rappel : une fonction (un rappel, son nom en texte), une LAMBDA, NULL.
    bool storeCallback(Obj& into, const RV& v, std::string* why) {
        const auto clear = [&] {
            into.bound = false;
            into.callee.clear();
            into.lambda.reset();
            into.lambdaWhere.clear();
            return true;
        };
        const std::string what = "une fonction, une LAMBDA ou NULL";
        if (!v.o) {
            if (v.v.type() == Type::String) {
                std::string name = v.v.asString();
                while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
                while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(0, 1);
                if (name.empty()) return clear();
                auto cb = functionValue(name);
                if (!cb) {
                    if (why) *why = "'" + name + "' n'est pas une fonction du projet (" + into.type->text() + " attend " + what + ")";
                    return false;
                }
                return storeCallback(into, RV{{}, cb}, why);
            }
            if (why) *why = "une valeur simple ne va pas dans un " + into.type->text() + " (" + what + ")";
            return false;
        }
        if (isNullObj(v.o)) return clear();
        if (!isCallbackObj(v.o)) {
            if (why) *why = "un " + typeNameOf(v) + " ne va pas dans un " + into.type->text() + " (" + what + ")";
            return false;
        }
        if (!v.o->type->literal && !callbackFits(*into.type, *v.o->type, why)) {
            if (why && v.o->bound) *why = (v.o->lambda ? std::string("la LAMBDA") : v.o->callee) + " ne convient pas : " + *why;
            return false;
        }
        into.bound = v.o->bound;
        into.callee = v.o->callee;
        into.lambda = v.o->lambda;
        into.lambdaWhere = v.o->lambdaWhere;
        return true;
    }

    // Une LAMBDA evaluee : un rappel neuf, ses locales copiees maintenant.
    core::Result<RV> makeLambda(const Expr& e) {
        if (!e.fn) return dialectError("LAMBDA sans corps");
        auto code = std::make_shared<LambdaCode>();
        code->fn = e.fn;
        code->captured = std::make_shared<Locals>();
        if (!typeBindings_.empty()) code->bindings = typeBindings_.back();
        if (!frames_.empty()) {
            const auto take = [&](const Frame& f) {
                for (auto it = f.scopes.rbegin(); it != f.scopes.rend(); ++it)
                    for (const auto& [n, place] : it->names)
                        if (place.obj && !code->captured->find(n)) code->captured->set(n, deepCopy(*place.obj));
            };
            take(frames_.back());
            if (frames_.size() > 1 && frames_.back().seesScript) take(frames_.front());
        }
        std::vector<std::pair<std::string, TypeRef>> params;
        for (const auto& p : e.fn->params) {
            auto r = resolve(p.type);
            if (!r) return core::Err<core::Error>(r.error());
            params.emplace_back(p.name, *r);
        }
        TypeRef result;
        if (e.fn->result) {
            auto r = resolve(e.fn->result);
            if (!r) return core::Err<core::Error>(r.error());
            result = *r;
        }
        auto obj = makeObj(callbackType(std::move(params), result));
        obj->bound = true;
        obj->lambda = code;
        obj->lambdaWhere = section_ + ", ligne " + std::to_string(e.line);
        return RV{{}, obj};
    }

    // Un rappel vide appele : rien ; la valeur par defaut de son retour ; l'environnement le dit (une fois).
    core::Result<RV> nullCall(const Obj& cb, const std::string& name, std::uint32_t line) {
        env_.nullCallback(name, section_ + ", ligne " + std::to_string(line));
        if (cb.type && cb.type->kind == TypeDesc::Kind::Callback && cb.type->element) {
            auto r = instantiate(cb.type->element);
            if (!r) return core::Err<core::Error>(r.error());
            if ((*r)->type && (*r)->type->kind == TypeDesc::Kind::Scalar) return RV{(*r)->value, nullptr};
            return RV{{}, *r};
        }
        return RV{Value::boolean(true), nullptr};
    }

    // Le rappel que designe la tete d'un appel (une variable, un parametre, une locale, un membre) ; nul : aucun.
    ObjRef callbackAt(const Expr& x) {
        if (x.kind == Expr::Kind::Reference) {
            if (!x.typeArgs.empty()) return nullptr;
            if (Place* p = findLocal(x.name)) return isCallbackObj(p->obj) ? p->obj : nullptr;
            // R2, R3 : une variable IHM rappel, un parametre de popup ou de symbole rappel - l'environnement les
            // tient. Une fonction interne du meme nom passe avant.
            if (dialect_ && findFunctions(x.name).empty())
                if (ObjRef o = env_.richVariable(x.name); isCallbackObj(o)) return o;
            return nullptr;
        }
        if ((x.kind == Expr::Kind::Member || x.kind == Expr::Kind::Index) && needsLocate(x)) {
            ++quiet_;
            auto p = locate(x, false);
            --quiet_;
            if (p && isCallbackObj(p->obj)) return p->obj;
        }
        return nullptr;
    }

    // f(x) : l'appel d'un rappel - sa fonction (par son nom, comme un appel ecrit) ou sa LAMBDA.
    core::Result<RV> callCallback(const Obj& cb, const Expr& e) {
        const std::string name = e.lhs ? dottedName(*e.lhs) : std::string("rappel");
        if (!cb.bound) return nullCall(cb, name, e.line);
        if (cb.lambda) {
            auto code = std::static_pointer_cast<const LambdaCode>(cb.lambda);
            return callDeclared(*code->fn, e, true, code->captured.get(), &code->bindings);
        }
        Expr call = e;
        auto head = std::make_shared<Expr>();
        head->kind = Expr::Kind::Reference;
        head->name = cb.callee;
        head->line = e.line;
        call.lhs = head;
        return callResolved(call);
    }

    // Appeler un rappel avec des valeurs (les natives LIST_FILTER, LIST_SORT, IHM_APRES...).
    core::Result<RV> invokeCallback(const Obj& cb, const std::vector<RV>& args, std::uint32_t line, const std::string& name) {
        if (!cb.bound) return nullCall(cb, name, line);
        std::vector<Bound> bound;
        for (const auto& a : args) bound.push_back(Bound{a, {}, false});
        if (cb.lambda) {
            auto code = std::static_pointer_cast<const LambdaCode>(cb.lambda);
            return runFunction(*code->fn, bound, line, true, nullptr, code->captured.get(), nullptr, &code->bindings);
        }
        std::vector<ArgShape> shapes;
        for (const auto& a : args) {
            ArgShape sh;
            sh.type = a.o ? typeNameOf(a) : valueTypeText(a.v.type());
            sh.text = a.o ? typeNameOf(a) : a.v.display();
            shapes.push_back(std::move(sh));
        }
        if (const auto inner = findFunctions(cb.callee); !inner.empty()) {
            std::size_t k = 0;
            if (inner.size() > 1) {
                std::string why;
                const int chosen = env_.chooseInner(inner, shapes, why);
                if (chosen < 0) return dialectError(why);
                k = static_cast<std::size_t>(chosen);
            }
            return runFunction(*inner[k], bound, line, false);
        }
        std::string key = cb.callee;
        if (env_.overloads(cb.callee) > 1) {
            std::string why;
            if (!env_.chooseOverload(cb.callee, shapes, key, why)) return dialectError(why);
        }
        if (auto f = env_.dialectFunction(key)) return runFunction(*f, bound, line, true);
        std::vector<std::pair<std::string, Value>> values;
        for (const auto& a : args) {
            if (a.o) return dialectError(cb.callee + " prend des valeurs simples, pas un " + typeNameOf(a));
            values.emplace_back(std::string{}, a.v);
        }
        Value result;
        if (!env_.call(key, key, values, result)) return dialectError("l'appel de " + cb.callee + " a \xC3\xA9" "chou\xC3\xA9");
        return RV{result, nullptr};
    }

    // ---- les generiques : T deduit des arguments (ou donne), verifie contre sa contrainte ----
    static bool genericAccepts(const std::string& c, const TypeDesc& t) {
        if (c.empty() || c == "ANY") return true;
        if (t.kind != TypeDesc::Kind::Scalar) return c == "ANY_DERIVED" || c == "ANY_STRUCT";
        const Type s = t.scalar;
        const bool time = s == Type::Time, date = s == Type::Date || s == Type::Tod || s == Type::Dt;
        const bool bits = s == Type::Bool || s == Type::Byte || s == Type::Word || s == Type::DWord || s == Type::LWord;
        const bool integer = isInteger(s) && !time && s != Type::Byte && s != Type::Word && s != Type::DWord && s != Type::LWord;
        const bool real = s == Type::Real;
        if (c == "ANY_NUM") return integer || real;
        if (c == "ANY_INT") return integer;
        if (c == "ANY_REAL") return real;
        if (c == "ANY_BIT") return bits;
        if (c == "ANY_STRING" || c == "ANY_CHARS") return s == Type::String;
        if (c == "ANY_DATE") return date;
        if (c == "ANY_DURATION") return time;
        if (c == "ANY_MAGNITUDE") return integer || real || time;
        if (c == "ANY_ELEMENTARY") return true;
        return true;                                         // une contrainte inconnue : Generer la dira
    }
    // 1.12.20 (lot G) : "T_Paire<STRING, T>" -> "T_Paire" et {"STRING", "T"} ; faux : pas un nom generique.
    static bool splitTypeArgs(const std::string& text, std::string& base, std::vector<std::string>& args) {
        const auto lt = text.find('<');
        if (lt == std::string::npos || text.back() != '>') return false;
        const auto trim = [](std::string s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
            return s;
        };
        base = trim(text.substr(0, lt));
        args.clear();
        int depth = 0;
        std::string cur;
        for (std::size_t k = lt + 1; k + 1 < text.size(); ++k) {
            const char c = text[k];
            if (c == '<' || c == '(' || c == '[') ++depth;
            if (c == '>' || c == ')' || c == ']') --depth;
            if (c == ',' && depth == 0) {
                args.push_back(trim(cur));
                cur.clear();
                continue;
            }
            cur += c;
        }
        args.push_back(trim(cur));
        return true;
    }
    // 1.12.20 (lot G) : les types d'une instance generique, ses T remplaces par ceux de la fonction generique en cours
    // (T_Mesure<T> -> T_Mesure<REAL>).
    std::string boundGenericName(const std::string& text) const {
        const auto lt = text.find('<');
        std::string out = text.substr(0, lt);
        for (std::size_t i = lt; i < text.size();) {
            if (!identStart(text[i]) || identChar(text[i - 1])) {
                out += text[i++];
                continue;
            }
            std::size_t j = i;
            while (j < text.size() && identChar(text[j])) ++j;
            const std::string w = text.substr(i, j - i);
            std::string with = w;
            for (auto it = typeBindings_.rbegin(); it != typeBindings_.rend(); ++it)
                if (const auto b = it->find(upperOf(w)); b != it->end()) {
                    with = b->second->text();
                    break;
                }
            out += with;
            i = j;
        }
        return out;
    }
    // 1.12.20 (lot G) : un parametre T_Mesure<T> et un argument T_Mesure<REAL> : T = REAL.
    void unifyGeneric(const TypeRef& p, const TypeRef& a, const std::set<std::string>& names, std::map<std::string, TypeRef>& out) {
        if (!p || !a || p->kind != TypeDesc::Kind::Struct || a->kind != TypeDesc::Kind::Struct) return;
        std::string pb, ab;
        std::vector<std::string> pa, aa;
        if (!splitTypeArgs(p->name, pb, pa) || !splitTypeArgs(a->name, ab, aa) || upperOf(pb) != upperOf(ab) || pa.size() != aa.size()) return;
        for (std::size_t k = 0; k < pa.size(); ++k) {
            const std::string key = upperOf(pa[k]);
            if (!names.count(key) || out.count(key)) continue;
            if (auto t = typeFromText(aa[k]))
                if (auto r = resolve(*t)) out[key] = *r;
        }
    }
    static void unify(const TypeRef& p, const TypeRef& a, const std::set<std::string>& names, std::map<std::string, TypeRef>& out) {
        if (!p || !a) return;
        if (p->kind == TypeDesc::Kind::Struct && p->members.empty() && names.count(upperOf(p->name))) {
            const std::string key = upperOf(p->name);
            const auto it = out.find(key);
            if (it == out.end()) out[key] = a;
            else if (it->second->kind == TypeDesc::Kind::Scalar && a->kind == TypeDesc::Kind::Scalar && isInteger(it->second->scalar)
                     && it->second->scalar != Type::Time && a->scalar == Type::Real)
                it->second = a;                              // 2 et 2.5 : REAL l'emporte
            return;
        }
        if (p->kind != a->kind) return;
        if (p->element && a->element) unify(p->element, a->element, names, out);
        for (std::size_t i = 0; i < p->members.size() && i < a->members.size(); ++i) unify(p->members[i].second, a->members[i].second, names, out);
    }
    core::Result<std::map<std::string, TypeRef>> bindTemplate(const Function& f, const std::vector<Bound>& args,
                                                             const std::vector<std::string>* typeArgs) {
        std::map<std::string, TypeRef> out;
        std::set<std::string> names;
        for (const auto& tp : f.typeParams) names.insert(upperOf(tp.first));
        if (typeArgs && !typeArgs->empty()) {
            if (typeArgs->size() > f.typeParams.size())
                return dialectError(f.name + " : " + std::to_string(typeArgs->size()) + " types donn\xC3\xA9s pour "
                                    + std::to_string(f.typeParams.size()) + " (TEMPLATE <" + f.typeParams.front().first + "...>)");
            for (std::size_t i = 0; i < typeArgs->size(); ++i) {
                auto t = typeFromText((*typeArgs)[i]);
                if (!t) return core::Err<core::Error>(t.error());
                auto r = resolve(*t);
                if (!r) return core::Err<core::Error>(r.error());
                out[upperOf(f.typeParams[i].first)] = *r;
            }
        }
        for (std::size_t k = 0; k < f.params.size() && k < args.size(); ++k) {
            const Bound& b = args[k];
            TypeRef argType;
            if (b.isPlace) {
                if (b.place.obj) argType = b.place.obj->type;
                else if (Value v; env_.read(b.place.name, v) && v.type() != Type::Unknown) argType = scalarType(v.type());
            } else if (b.value.o) {
                argType = b.value.o->type;
            } else if (b.value.v.type() != Type::Unknown) {
                argType = scalarType(b.value.v.type());
            }
            unify(f.params[k].type, argType, names, out);
            unifyGeneric(f.params[k].type, argType, names, out);    // 1.12.20 (lot G)
        }
        for (const auto& [n, c] : f.typeParams) {
            const auto it = out.find(upperOf(n));
            if (it == out.end())
                return dialectError(f.name + " : le type " + n + " ne se d\xC3\xA9" "duit pas des arguments \xE2\x80\x94 donne-le : "
                                    + f.name + "<INT>(...)");
            if (!genericAccepts(c, *it->second))
                return dialectError(f.name + " : " + n + " = " + it->second->text() + " n'est pas un " + c);
        }
        return out;
    }
    // ==== fin 1.12.19 ====

    // ---- les appels --------------------------------------------------------
    // Un argument : par valeur (sa valeur), ou par reference (son emplacement).
    core::Result<RV> callRich(const Expr& e) {
        // 1.12.7 : NUMERIC_LIMITS<UINT>.MAX() - une fonction d'un template de classe.
        if (e.lhs && e.lhs->kind == Expr::Kind::Member && e.arguments.empty() && e.outputs.empty())
            if (auto c = templateMemberValue(*e.lhs, true)) return std::move(*c);
        // 1.12.19 : un rappel (une variable, un parametre, une locale) s'appelle comme une fonction.
        if (e.lhs)
            if (ObjRef cb = callbackAt(*e.lhs)) return callCallback(*cb, e);
        return callResolved(e);
    }

    // L'appel d'une fonction par son nom (sans chercher de rappel de ce nom).
    core::Result<RV> callResolved(const Expr& e) {
        std::string callee;
        if (e.lhs && e.lhs->kind == Expr::Kind::Reference) callee = e.lhs->name;
        const std::string u = upperOf(callee);
        // Les fonctions internes du script, puis les fonctions IHM du projet (dialecte).
        // 1.11.10 : Vue_Vannes.Vanne_3.Vecteur() - la fonction d'une instance de symbole,
        // aux types riches (une structure rendue, VAR_IN_OUT) : l'environnement la donne.
        // 1.11.20 : une fonction du projet (ou d'un symbole) surchargee : la sienne, par sa cle.
        const auto chosen = [&](const std::string& name) -> std::optional<core::Result<RV>> {
            if (env_.overloads(name) < 2) return std::nullopt;
            std::string key, why;
            if (!env_.chooseOverload(name, shapesOf(e), key, why)) return core::Result<RV>(dialectError(why));
            if (auto f = env_.dialectFunction(key)) return callDeclared(*f, e, true);
            auto v = call(e, key);                     // une fonction simple : par l'environnement, sous sa cle
            if (!v) return core::Result<RV>(core::Err<core::Error>(v.error()));
            return core::Result<RV>(RV{*v, nullptr});
        };
        if (callee.empty() && e.lhs && e.lhs->kind == Expr::Kind::Member)
            if (auto q = qualify(*e.lhs)) {
                if (auto r = chosen(*q)) return std::move(*r);
                if (auto f = env_.dialectFunction(*q)) return callDeclared(*f, e, true);
            }
        // 1.13.0 (lot J1) : chemin.Methode(args) - une methode d'un type IHM, resolue sur le type reel
        // de l'objet (le sous-type, a travers une REF_TO). Apres les fonctions de symbole (ci-dessus).
        if (callee.empty() && e.lhs && e.lhs->kind == Expr::Kind::Member && e.lhs->lhs)
            if (auto r = callMethod(e)) return std::move(*r);
        if (!callee.empty()) {
            // 1.11.20 : des fonctions internes du meme nom (des surcharges) : la sienne.
            const auto inner = findFunctions(callee);
            if (inner.size() > 1) {
                std::string why;
                const int k = env_.chooseInner(inner, shapesOf(e), why);
                if (k < 0) return dialectError(why);
                return callDeclared(*inner[static_cast<std::size_t>(k)], e, false);
            }
            if (inner.size() == 1) return callDeclared(*inner.front(), e, false);
            // 1.13.0 (lot J1) : SUPER.M(...) a ete emis comme METHODE_<parent>_M(SELF, ...) ; on le resout.
            if (u.rfind("METHODE_", 0) == 0)
                if (OperatorSource src; env_.findMethodByInternal(callee, src)) {
                    std::shared_ptr<const Function> f;
                    if (const auto it = operatorCache().find(src.key); it != operatorCache().end()) f = it->second;
                    else {
                        auto parsed = parseFunction(src.function, src.owner);
                        if (!parsed) return dialectError(src.owner + " : " + parsed.error().message());
                        operatorCache()[src.key] = *parsed;
                        f = *parsed;
                    }
                    return callDeclared(*f, e, true);
                }
            if (auto mine = natives1220(callee, e)) return std::move(*mine);          // 1.12.20 : les dernieres natives a rappel
            if (auto mine = natives1219(callee, e)) return std::move(*mine);          // 1.12.19 : les natives a rappel
            if (auto mine = natives1127(callee, e)) return std::move(*mine);          // 1.12.7
            if (auto builtin = dialectBuiltin(u, e)) return std::move(*builtin);
            if (auto r = chosen(callee)) return std::move(*r);
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
        // 1.12.19 : une variable IHM rappel (T_Filtre) : sa fonction.
        if (!callee.empty())
            if (auto o = env_.richVariable(callee); isCallbackObj(o)) return callCallback(*o, e);
        // Le reste : comme avant (fonctions standard, IHM_..., blocs de l'automate).
        auto v = call(e);
        if (!v) return core::Err<core::Error>(v.error());
        return RV{*v, nullptr};
    }

    static bool isStandardConversionTarget(const std::string& u) {
        static const char* kTargets[] = {"TO_STRING", "TO_REAL", "TO_LREAL", "TO_BOOL", "TO_INT", "TO_DINT", "TO_UINT",
                                         "TO_UDINT", "TO_SINT", "TO_USINT", "TO_LINT", "TO_ULINT", "TO_WORD", "TO_DWORD",
                                         "TO_BYTE", "TO_TIME", "TO_LWORD"};   // 1.11.25 : TO_LWORD
        for (const char* t : kTargets)
            if (u == t) return true;
        return false;
    }

    // Les arguments d'un appel, rapproches des parametres.
    core::Result<RV> callDeclared(const Function& f, const Expr& e, bool isolated, const Locals* captured = nullptr,
                                  const std::map<std::string, TypeRef>* bindings = nullptr) {
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
        const std::vector<std::string>* typeArgs = e.lhs && !e.lhs->typeArgs.empty() ? &e.lhs->typeArgs : nullptr;
        return runFunction(f, args, e.line, isolated, &given, captured, typeArgs, bindings);
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
                                 const std::vector<bool>* given = nullptr, const Locals* captured = nullptr,
                                 const std::vector<std::string>* typeArgs = nullptr,
                                 const std::map<std::string, TypeRef>* lambdaBindings = nullptr) {
        // 1.12.19 : les types generiques de cet appel (T deduit, ou donne ; ceux d'une LAMBDA : de sa creation) ;
        // une fonction ordinaire a les siens vides (un type T du projet n'est pas cache par un appelant generique).
        struct BindingsScope {
            std::vector<std::map<std::string, TypeRef>>& stack;
            explicit BindingsScope(std::vector<std::map<std::string, TypeRef>>& s, std::map<std::string, TypeRef> b) : stack(s) { stack.push_back(std::move(b)); }
            ~BindingsScope() { stack.pop_back(); }
            BindingsScope(const BindingsScope&) = delete;
            BindingsScope& operator=(const BindingsScope&) = delete;
        };
        std::map<std::string, TypeRef> bindings;
        if (lambdaBindings) bindings = *lambdaBindings;
        if (!f.typeParams.empty()) {
            auto b = bindTemplate(f, args, typeArgs);
            if (!b) return core::Err<core::Error>(b.error());
            bindings = std::move(*b);
        } else if (typeArgs && !typeArgs->empty()) {
            return dialectError(f.name + " n'est pas gÃ©nÃ©rique (TEMPLATE <T>) : sans <" + typeArgs->front() + ">");
        }
        BindingsScope typed(typeBindings_, std::move(bindings));
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
        // 1.12.19 : une LAMBDA voit les locales copiees a sa creation (ses parametres passent devant).
        if (captured)
            for (const auto& [n, obj] : captured->all()) names[n] = Place{obj, {}, false, false};
        std::vector<std::pair<Place, ObjRef>> outputs;     // VAR_OUTPUT : rendu apres l'appel
        for (std::size_t k = 0; k < f.params.size(); ++k) {
            const auto& param = f.params[k];
            const bool isGiven = given ? (*given)[k] : k < args.size();
            const Bound* b = k < args.size() ? &args[k] : nullptr;
            if (param.mode == VarDecl::Mode::InOut) {
                if (!isGiven || !b || !b->isPlace)
                    return dialectError(f.name + " : le param\xC3\xA8tre VAR_IN_OUT '" + param.name + "' attend une variable");
                // Le type : le meme (ou un nombre pour un nombre). 1.13.0 (lot J1) : la MONTEE - un sous-type
                // passe la ou son parent est attendu (SELF d'une methode heritee, SUPER) ; sa tranche parente
                // est en tete, les champs du parent s'alignent.
                if (b->place.obj && param.type && b->place.obj->type) {
                    auto want = resolve(param.type);
                    if (!want) return core::Err<core::Error>(want.error());
                    if (!compatibleTarget(**want, *b->place.obj->type)
                        && !env_.typeConvertsTo(b->place.obj->type->text(), (*want)->text()))
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
            // 1.12.16 : le corps d'une fonction IHM - la valeur va dans le nom de la fonction.
            if (!limits_.returnTo.empty() && frames_.size() < 2) {
                auto v = rich(*s.value);
                if (!v) {
                    if (!aborted_) fail(s.line, v.error().message());
                    return {Flow::Kind::Aborted};
                }
                if (v->o) {
                    fail(s.line, limits_.returnTo + " : une fonction de ce type rend une valeur simple");
                    return {Flow::Kind::Aborted};
                }
                if (!assignName(limits_.returnTo, v->v, s.line)) return {Flow::Kind::Aborted};
                return {Flow::Kind::Return};
            }
            fail(s.line, "RETURN avec une valeur : seulement dans une fonction");
            return {Flow::Kind::Aborted};
        }
        // 1.12.19 : des copies - evaluer la valeur appelle peut-etre une fonction (RETURN g(v)), qui ajoute un
        // cadre : la pile des cadres peut se deplacer en memoire (une reference vers frames_.back() pendrait).
        const ObjRef result = frames_.back().result;
        const Function* fn = frames_.back().fn;
        if (!result) {
            fail(s.line, fn->name + " ne rend pas de valeur (pas de type de retour) : RETURN seul");
            return {Flow::Kind::Aborted};
        }
        // 1.10 (decision 15) : RETURN Auto (le nom seul, du type rendu).
        Value lit;
        core::Result<RV> v = bareName(*s.value) && bareEnum(enumNameOf(fn->result), *s.value, lit)
                                 ? core::Result<RV>(RV{lit, nullptr}) : rich(*s.value);
        if (!v) {
            if (!aborted_) fail(s.line, v.error().message());
            return {Flow::Kind::Aborted};
        }
        std::string why;
        if (!store(*result, *v, &why)) {
            fail(s.line, fn->name + " : retour du mauvais type : " + why);
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
            if (lastFlow_.kind == Flow::Kind::Continue) return 0;      // 1.12.1
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
        if (type.kind == TypeDesc::Kind::List) {
            // 1.12.2 : FOR EACH x IN L (les elements) ; FOR EACH i, x IN L (l'indice, a partir de 0).
            // Les elements d'abord : le corps peut ajouter ou retirer.
            // 1.12.7 : un SET, dans l'ordre (chaque valeur se lit, ne s'ecrit pas) ; un RING, de la plus ancienne a la plus recente.
            const auto items = p.obj->items;
            const bool setItems = isSetType(type);
            for (std::size_t k = 0; k < items.size(); ++k) {
                const int r = runBody(scalar(Value::integer(Type::DInt, static_cast<std::int64_t>(k))), Place{items[k], {}, p.constant, false, setItems});
                if (r == 1) break;
                if (r == 2) return lastFlow_.kind == Flow::Kind::Normal ? Flow{Flow::Kind::Aborted} : lastFlow_;
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
        fail(s.line, "FOR EACH : un " + type.text() + " n'est ni une MAP, ni un tableau, ni une liste");
        return {Flow::Kind::Aborted};
    }

    // ---- 1.12.2 : les listes et les vecteurs (LIST OF T, VECTOR OF T) -----------
    static bool isListFunction(const std::string& u) {
        static const char* kNames[] = {"LIST_ADD", "LIST_INSERT", "LIST_REMOVE_AT", "LIST_REMOVE", "LIST_CLEAR", "LIST_COUNT",
                                       "LIST_CONTAINS", "LIST_INDEX_OF", "LIST_FIRST", "LIST_LAST", "LIST_GET", "LIST_SORT",
                                       "LIST_REVERSE", "LIST_SUM", "LIST_MIN", "LIST_MAX", "LIST_AVG", "VECTOR_PUSH", "VECTOR_POP",
                                       "VECTOR_INSERT", "VECTOR_ERASE", "VECTOR_CLEAR", "VECTOR_SIZE", "VECTOR_RESIZE", "VECTOR_FRONT",
                                       "VECTOR_BACK", "JOIN", "SPLIT"};
        for (const char* n : kNames)
            if (u == n) return true;
        return false;
    }
    // Un element de la liste et une valeur : egaux (des valeurs simples comparees comme
    // par '=' ; des objets en profondeur, un litteral converti dans le type de l'element).
    bool sameItem(const ObjRef& item, const RV& x) {
        if (!item || !item->type) return false;
        if (item->type->kind == TypeDesc::Kind::Scalar) return !x.o && item->value.compare(x.v) == 0;
        if (!x.o) return false;
        if (x.o->type && x.o->type->literal) {
            auto tmp = makeObj(item->type);
            std::string why;
            return store(*tmp, x, &why) && deepEquals(*tmp, *item);
        }
        return deepEquals(*item, *x.o);
    }
    // 1.12.7 : LE RANG D'UNE VALEUR DANS UN SET (ses valeurs sont rangees) : la premiere place ou elle irait,
    // et si elle y est deja.
    static std::size_t setPlace(const Obj& set, const Value& v, bool& present) {
        const auto at = std::lower_bound(set.items.begin(), set.items.end(), v, [](const ObjRef& a, const Value& b) {
            return a && a->value.compare(b) < 0;
        });
        present = at != set.items.end() && *at && (*at)->value.compare(v) == 0;
        return static_cast<std::size_t>(at - set.items.begin());
    }
    // Ajouter a un SET (a sa place, si elle n'y est pas : vrai) ou a un RING (a la fin ; plein, la plus
    // ancienne part : vrai si une valeur est partie).
    static bool ringOrSetAdd(Obj& list, const ObjRef& item) {
        if (isSetType(*list.type)) {
            bool present = false;
            const auto at = setPlace(list, item->value, present);
            if (present) return false;
            list.items.insert(list.items.begin() + static_cast<std::ptrdiff_t>(at), item);
            return true;
        }
        list.items.push_back(item);
        return normalizeList(list) > 0;
    }
    core::Result<RV> listBuiltin(const std::string& u, const Expr& e) {
        const auto argc = e.arguments.size();
        const auto bad = [&](std::string_view sig) {
            return core::Result<RV>(dialectError("mauvais nombre d'arguments : " + std::string(sig)));
        };
        const auto integer = [](std::int64_t v) { return core::Result<RV>(RV{Value::integer(Type::DInt, v), nullptr}); };
        const auto boolean = [](bool v) { return core::Result<RV>(RV{Value::boolean(v), nullptr}); };
        if (u == "SPLIT") {
            if (argc != 2) return bad("SPLIT(texte, s\xC3\xA9parateur)");
            auto t = evaluate(*e.arguments[0].second);
            if (!t) return core::Err<core::Error>(t.error());
            auto sep = evaluate(*e.arguments[1].second);
            if (!sep) return core::Err<core::Error>(sep.error());
            const std::string text = t->type() == Type::String ? t->asString() : t->display();
            const std::string by = sep->type() == Type::String ? sep->asString() : sep->display();
            if (by.empty()) return dialectError("SPLIT : le s\xC3\xA9parateur est vide");
            auto out = makeObj(listType(scalarType(Type::String)));
            std::size_t from = 0;
            for (;;) {
                const auto at = text.find(by, from);
                auto item = makeObj(scalarType(Type::String));
                item->value = Value::text(text.substr(from, at == std::string::npos ? std::string::npos : at - from));
                out->items.push_back(item);
                if (at == std::string::npos) break;
                from = at + by.size();
            }
            return RV{{}, out};
        }
        if (argc == 0) return bad(u + "(liste...)");
        static const char* kWrites[] = {"LIST_ADD", "LIST_INSERT", "LIST_REMOVE_AT", "LIST_REMOVE", "LIST_CLEAR", "LIST_SORT", "LIST_REVERSE",
                                         "VECTOR_PUSH", "VECTOR_POP", "VECTOR_INSERT", "VECTOR_ERASE", "VECTOR_CLEAR", "VECTOR_RESIZE"};
        bool writes = false;
        for (const char* n : kWrites)
            if (u == n) writes = true;
        // La liste, designee (sans copie : on ecrit dedans). 1.12.7 : un ARRAY aussi, pour les 10 fonctions
        // qui ne changent pas sa taille (LIST_GET, LIST_SORT, JOIN...) ; un tableau de l'environnement est
        // copie, puis recopie apres une ecriture (LIST_SORT, LIST_REVERSE).
        auto designated = objArg(*e.arguments[0].second);
        if (!designated) return core::Err<core::Error>(designated.error());
        const ObjArg held = *designated;
        if (held.isScalar || !held.obj)
            return dialectError(u + " : une liste ou un vecteur est attendu, pas un " + std::string(toString(held.scalar.type())));
        Obj& list = *held.obj;
        const bool array = list.type && list.type->kind == TypeDesc::Kind::Array;
        if (!list.type || (list.type->kind != TypeDesc::Kind::List && !(array && lib::oldListAcceptsArray(u)))) {
            if (array && lib::oldListResizes(u))
                return dialectError(u + " : un ARRAY a une taille fixe (" + std::to_string(list.items.size())
                                    + " cases) \xE2\x80\x94 une LIST ou un VECTOR est attendu");
            return dialectError(u + " : une liste ou un vecteur est attendu (LIST OF T, VECTOR OF T), pas un "
                                + (list.type ? list.type->text() : std::string("?")));
        }
        if (writes && held.constant) return dialectError(u + " : une constante ne s'\xC3\xA9" "crit pas");
        // 1.12.7 : UN SET se range tout seul - il refuse ce qui ecrit a une place (LIST_INSERT, LIST_SORT...) ;
        // LIST_ADD (VECTOR_PUSH) y range la valeur, une seule fois. UN RING : LIST_ADD ajoute a la fin, et la
        // plus ancienne part quand il est plein. Apres une ecriture, la liste reprend sa forme (normalizeList).
        const bool isSet = list.type && isSetType(*list.type), isRing = list.type && isRingType(*list.type);
        if (isSet && (u == "LIST_INSERT" || u == "VECTOR_INSERT" || u == "LIST_SORT" || u == "LIST_REVERSE" || u == "VECTOR_RESIZE"))
            return setRefused(u);
        if ((isSet || isRing) && (u == "LIST_ADD" || u == "VECTOR_PUSH")) {
            if (argc != 2) return bad(u + "(liste, valeur)");
            auto v = rich(*e.arguments[1].second);
            if (!v) return v;
            auto made = fitTo(u, list.type->element, *v);
            if (!made) return core::Err<core::Error>(made.error());
            ringOrSetAdd(list, *made);
            return integer(static_cast<std::int64_t>(list.items.size()));
        }
        struct Reshape {
            Obj& o;
            bool on;
            ~Reshape() { if (on) normalizeList(o); }
        } reshape{list, writes && (isSet || isRing)};
        // 1.12.7 : un ARRAY se lit a ses indices (Tab.Low a Tab.High).
        std::int64_t low = 0;
        if (array && (u == "LIST_GET" || u == "LIST_INDEX_OF")) {
            auto l = lowOf(u, list);
            if (!l) return core::Err<core::Error>(l.error());
            low = *l;
        }
        const auto commit = [&]() -> core::Result<RV> {
            auto c = commitArg(u, held);
            if (!c) return core::Err<core::Error>(c.error());
            return RV{Value::boolean(true), nullptr};
        };
        const auto n = static_cast<std::int64_t>(list.items.size());
        const auto value = [&](std::size_t k) -> core::Result<RV> {
            auto v = rich(*e.arguments[k].second);
            if (!v) return v;
            return *v;
        };
        const auto element = [&](const RV& v) -> core::Result<ObjRef> {
            auto made = instantiate(list.type->element);
            if (!made) return made;
            std::string why;
            if (!store(**made, v, &why)) return dialectError(u + " : " + why);
            return made;
        };
        const auto index = [&](std::size_t k) -> core::Result<std::int64_t> {
            auto v = evaluate(*e.arguments[k].second);
            if (!v) return core::Err<core::Error>(v.error());
            if (!isInteger(v->type()) && v->type() != Type::Bool) return dialectError(u + " : un indice est un entier");
            return v->asInteger();
        };
        const auto empty = [&]() { return dialectError(u + " : la liste est vide"); };
        if (u == "LIST_COUNT" || u == "VECTOR_SIZE") {
            if (argc != 1) return bad(u + "(liste)");
            return integer(n);
        }
        if (u == "LIST_CLEAR" || u == "VECTOR_CLEAR") {
            if (argc != 1) return bad(u + "(liste)");
            list.items.clear();
            return boolean(true);
        }
        if (u == "LIST_ADD" || u == "VECTOR_PUSH") {
            if (argc != 2) return bad(u + "(liste, valeur)");
            auto v = value(1);
            if (!v) return v;
            auto made = element(*v);
            if (!made) return core::Err<core::Error>(made.error());
            list.items.push_back(*made);
            return integer(n + 1);
        }
        if (u == "LIST_INSERT" || u == "VECTOR_INSERT") {
            if (argc != 3) return bad(u + "(liste, indice, valeur)");
            auto i = index(1);
            if (!i) return core::Err<core::Error>(i.error());
            if (*i < 0 || *i > n) return dialectError(u + " : indice " + std::to_string(*i) + " hors de 0.." + std::to_string(n));
            auto v = value(2);
            if (!v) return v;
            auto made = element(*v);
            if (!made) return core::Err<core::Error>(made.error());
            list.items.insert(list.items.begin() + static_cast<std::ptrdiff_t>(*i), *made);
            normalizeList(list);                     // 1.12.7 : un RING plein : la plus ancienne part
            return integer(static_cast<std::int64_t>(list.items.size()));
        }
        if (u == "LIST_REMOVE_AT" || u == "VECTOR_ERASE") {
            if (argc != 2) return bad(u + "(liste, indice)");
            auto i = index(1);
            if (!i) return core::Err<core::Error>(i.error());
            if (*i < 0 || *i >= n) return boolean(false);
            list.items.erase(list.items.begin() + static_cast<std::ptrdiff_t>(*i));
            return boolean(true);
        }
        if (u == "LIST_REMOVE" || u == "LIST_CONTAINS" || u == "LIST_INDEX_OF") {
            if (argc != 2) return bad(u + "(liste, valeur)");
            auto v = value(1);
            if (!v) return v;
            std::int64_t found = -1;
            for (std::int64_t k = 0; k < n && found < 0; ++k)
                if (sameItem(list.items[static_cast<std::size_t>(k)], *v)) found = k;
            if (u == "LIST_INDEX_OF") return integer(found < 0 ? -1 : found + low);
            if (u == "LIST_CONTAINS") return boolean(found >= 0);
            if (found >= 0) list.items.erase(list.items.begin() + static_cast<std::ptrdiff_t>(found));
            return boolean(found >= 0);
        }
        if (u == "LIST_FIRST" || u == "LIST_LAST" || u == "VECTOR_FRONT" || u == "VECTOR_BACK" || u == "VECTOR_POP") {
            if (argc != 1) return bad(u + "(liste)");
            if (n == 0) return empty();
            const bool front = u == "LIST_FIRST" || u == "VECTOR_FRONT";
            const ObjRef item = front ? list.items.front() : list.items.back();
            if (u == "VECTOR_POP") list.items.pop_back();
            return readPlace(Place{item, {}, false, false});
        }
        if (u == "LIST_GET") {
            if (argc != 3) return bad("LIST_GET(liste, indice, d\xC3\xA9" "faut)");
            auto i = index(1);
            if (!i) return core::Err<core::Error>(i.error());
            if (*i - low >= 0 && *i - low < n) return readPlace(Place{list.items[static_cast<std::size_t>(*i - low)], {}, false, false});
            return value(2);
        }
        if (u == "VECTOR_RESIZE") {
            if (argc != 2 && argc != 3) return bad("VECTOR_RESIZE(vecteur, taille, valeur)");
            auto size = index(1);
            if (!size) return core::Err<core::Error>(size.error());
            if (*size < 0 || *size > 1000000) return dialectError("VECTOR_RESIZE : taille " + std::to_string(*size) + " hors de 0..1000000");
            if (isRing && *size > list.type->capacity)          // 1.12.7
                return dialectError(u + " : un " + list.type->text() + " garde au plus " + std::to_string(list.type->capacity) + " valeurs");
            if (*size < n) list.items.resize(static_cast<std::size_t>(*size));
            while (static_cast<std::int64_t>(list.items.size()) < *size) {
                ObjRef made;
                if (argc == 3) {
                    auto v = value(2);
                    if (!v) return v;
                    auto m = element(*v);
                    if (!m) return core::Err<core::Error>(m.error());
                    made = *m;
                } else {
                    auto m = instantiate(list.type->element);
                    if (!m) return core::Err<core::Error>(m.error());
                    made = *m;
                }
                list.items.push_back(made);
            }
            return integer(*size);
        }
        if (u == "LIST_REVERSE") {
            if (argc != 1) return bad("LIST_REVERSE(liste)");
            std::reverse(list.items.begin(), list.items.end());
            return commit();
        }
        // Les fonctions sur des valeurs simples : trier, sommer, le plus petit...
        // 1.12.7 : LIST_SORT d'une liste de structures dit LIST_SORT_BY (la maquette).
        if (u == "LIST_SORT" && list.type->element && list.type->element->kind == TypeDesc::Kind::Struct && !list.type->element->members.empty())
            return dialectError(u + " : des " + list.type->element->name + " \xE2\x80\x94 LIST_SORT_BY(L, '"
                                + list.type->element->members[list.type->element->members.size() > 1 ? 1 : 0].first + "') trie par un membre");
        for (const auto& i : list.items)
            if (!i || !i->type || i->type->kind != TypeDesc::Kind::Scalar)
                return dialectError(u + " : une liste de valeurs simples est attendue, pas un " + list.type->text());
        if (u == "LIST_SORT") {
            if (argc != 1 && argc != 2) return bad("LIST_SORT(liste, d\xC3\xA9" "croissant)");
            bool descending = false;
            if (argc == 2) {
                auto d = evaluate(*e.arguments[1].second);
                if (!d) return core::Err<core::Error>(d.error());
                descending = d->isTruthy();
            }
            std::stable_sort(list.items.begin(), list.items.end(), [descending](const ObjRef& x, const ObjRef& y) {
                const int c = x->value.compare(y->value);
                return descending ? c > 0 : c < 0;
            });
            return commit();
        }
        if (u == "JOIN") {
            if (argc != 1 && argc != 2) return bad("JOIN(liste, s\xC3\xA9parateur)");
            std::string by = ", ";
            if (argc == 2) {
                auto sep = evaluate(*e.arguments[1].second);
                if (!sep) return core::Err<core::Error>(sep.error());
                by = sep->type() == Type::String ? sep->asString() : sep->display();
            }
            std::string out;
            for (std::size_t k = 0; k < list.items.size(); ++k) {
                const Value& v = list.items[k]->value;
                out += (k ? by : std::string{}) + (v.type() == Type::String ? v.asString() : v.display());
            }
            return RV{Value::text(out), nullptr};
        }
        // LIST_SUM, LIST_MIN, LIST_MAX, LIST_AVG
        if (argc != 1) return bad(u + "(liste)");
        for (const auto& i : list.items)
            if (!isNumeric(i->value.type()) || i->value.type() == Type::Bool)
                return dialectError(u + " : une liste de nombres est attendue, pas un " + list.type->text());
        if (u == "LIST_SUM") {
            bool real = false;
            double r = 0;
            std::int64_t k = 0;
            for (const auto& i : list.items) {
                if (i->value.type() == Type::Real) real = true;
                r += i->value.asReal();
                k += i->value.asInteger();
            }
            if (real || (list.type->element && list.type->element->kind == TypeDesc::Kind::Scalar && list.type->element->scalar == Type::Real))
                return RV{Value::real(r), nullptr};
            return integer(k);
        }
        if (n == 0) return empty();
        if (u == "LIST_AVG") {
            double r = 0;
            for (const auto& i : list.items) r += i->value.asReal();
            return RV{Value::real(r / static_cast<double>(n)), nullptr};
        }
        ObjRef best = list.items.front();
        for (const auto& i : list.items) {
            const int c = i->value.compare(best->value);
            if (u == "LIST_MIN" ? c < 0 : c > 0) best = i;
        }
        return readPlace(Place{best, {}, false, false});
    }

    // ---- les fonctions du dialecte (MAP_..., REF, ADR, bornes) ---------------
    std::optional<core::Result<RV>> dialectBuiltin(const std::string& u, const Expr& e) {
        if (isListFunction(u)) return listBuiltin(u, e);          // 1.12.2
        static const char* kNames[] = {"MAP_HAS", "MAP_GET", "MAP_REMOVE", "MAP_SIZE", "MAP_CLEAR", "MAP_KEYS",
                                       "MAP_BEGIN", "MAP_NEXT", "MAP_END", "REF", "ADR", "LOWER_BOUND", "UPPER_BOUND", "SIZEOF",
                                       "TO_UPPER", "TO_LOWER", "ASSERT"};
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
        // 1.12.1 : ASSERT(condition, 'message') - faux : une erreur (le script s'arrete, la
        // Console le dit ; dans un TRY, son CATCH) ; vrai : rien.
        if (u == "ASSERT") {
            if (!want(1, 2)) return bad("ASSERT(condition, 'message')");
            auto c = evaluate(*e.arguments[0].second);
            if (!c) return core::Result<RV>(core::Err<core::Error>(c.error()));
            if (c->type() != Type::Bool) return core::Result<RV>(dialectError("ASSERT : la condition doit \xC3\xAAtre un BOOL"));
            if (c->isTruthy()) return core::Result<RV>(RV{Value::boolean(true), nullptr});
            std::string why = exprText(*e.arguments[0].second) + " est faux";
            if (argc == 2) {
                auto m = evaluate(*e.arguments[1].second);
                if (m) why = m->type() == Type::String ? m->asString() : m->display();
            }
            return core::Result<RV>(dialectError("ASSERT : " + why));
        }
        // Le premier argument designe (sans copie) : une MAP, un tableau, un iterateur.
        // 1.12.7 : `subjectConstant` - il designe une constante (VAR CONSTANT, une constante lue en
        // place) : MAP_REMOVE et MAP_CLEAR la refusent, comme listBuiltin (avant : ecrite sans un mot).
        bool subjectConstant = false;
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
            subjectConstant = p.constant;
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
            if (where->setItem) return core::Result<RV>(dialectError(u + " : " + kSetItemRefused));     // 1.12.7
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
                // 1.12.2 : une liste (ses elements, indices 0..Count-1), un tuple (ses valeurs).
                if (o->type && (o->type->kind == TypeDesc::Kind::List || (u == "SIZEOF" && o->type->kind == TypeDesc::Kind::Tuple))) {
                    const auto count = static_cast<std::int64_t>(o->items.size());
                    return core::Result<RV>(RV{Value::integer(Type::DInt, u == "SIZEOF" ? count : u == "LOWER_BOUND" ? 0 : count - 1), nullptr});
                }
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
            if (subjectConstant) return core::Result<RV>(dialectError(u + " : une constante ne s'\xC3\xA9" "crit pas"));
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
        if (u == "MAP_GET" ? !want(2, 3) : !want(2, 2))
            return bad(u == "MAP_GET" ? "MAP_GET(m, cl\xC3\xA9 [, d\xC3\xA9" "faut])" : u + "(m, cl\xC3\xA9)");
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
            if (subjectConstant) return core::Result<RV>(dialectError(u + " : une constante ne s'\xC3\xA9" "crit pas"));
            if (*isText) o->textKeys.erase(text);
            else o->intKeys.erase(integer);
            return core::Result<RV>(RV{Value::boolean(entry != nullptr), nullptr});
        }
        // MAP_GET ; 1.12.7 : sans defaut, une cle absente est une erreur.
        if (entry) return core::Result<RV>(readPlace(Place{entry, {}, false, false}));
        if (argc < 3) return core::Result<RV>(dialectError("MAP_GET : cl\xC3\xA9 absente : " + keyText(*isText, text, integer)));
        return core::Result<RV>(rich(*e.arguments[2].second));
    }

    // =====================================================================================
    // 1.12.7 : LES NATIVES DE LA 1.12.7 - les conteneurs (43 de plus), std::numeric, les
    // templates (NUMERIC_LIMITS<T>, NUMBERS<T>, SATURATE_CAST<T>, ADD_SAT<T>...) et <bit>.
    // Leur nombre d'arguments, leur signature : sim::lib::natives() (DialectLib).
    // =====================================================================================
    // Un argument objet : l'objet lui-meme quand l'interpreteur le garde (on ecrit dedans) ;
    // une variable de l'environnement (un tableau IHM) est copiee, puis recopiee apres une
    // ecriture (commitArg). Une valeur simple : `isScalar`.
    struct ObjArg {
        ObjRef      obj;
        std::string envName;
        bool        constant{false};
        bool        temp{false};
        bool        isScalar{false};
        Value       scalar;
    };
    core::Result<ObjArg> objArg(const Expr& a) {
        Place p;
        ObjArg out;
        if (isDesignator(a)) {
            auto where = locate(a, false);
            if (!where) return core::Err<core::Error>(where.error());
            p = *where;
        } else {
            auto v = rich(a);
            if (!v) return core::Err<core::Error>(v.error());
            if (!v->o) {
                out.isScalar = true;
                out.scalar = v->v;
                return out;
            }
            p.obj = v->o;
            p.temp = true;
        }
        while (p.obj && isIndirect(p.obj)) {
            auto t = follow(p.obj);
            if (!t) return core::Err<core::Error>(t.error());
            p = *t;
        }
        out.constant = p.constant;
        out.temp = p.temp;
        if (!p.obj) {
            Value v;
            if (env_.read(p.name, v)) {
                out.isScalar = true;
                out.scalar = v;
                return out;
            }
            auto m = materialize(p.name);
            if (!m) return core::Err<core::Error>(m.error());
            out.obj = *m;
            out.envName = p.name;
        } else {
            out.obj = p.obj;
        }
        if (out.obj && out.obj->type && out.obj->type->kind == TypeDesc::Kind::Scalar) {
            out.isScalar = true;
            out.scalar = out.obj->value;
        }
        return out;
    }
    static std::string kindName(const ObjArg& a) {
        if (a.isScalar || !a.obj || !a.obj->type) {
            const Type t = a.scalar.type();
            if (t == Type::String) return "une STRING";
            if (t == Type::Bool) return "un BOOL";
            if (isNumeric(t)) return "un nombre";
            return "une valeur";
        }
        const auto& t = *a.obj->type;
        switch (t.kind) {
            case TypeDesc::Kind::List:
                if (isSetType(t)) return "un SET";              // 1.12.7
                if (isRingType(t)) return "un RING";
                return upperOf(t.name) == "VECTOR" ? "un VECTOR" : "une LIST";
            case TypeDesc::Kind::Array: return "un ARRAY";
            case TypeDesc::Kind::Map: return "une MAP";
            case TypeDesc::Kind::Tuple: return "un TUPLE";
            case TypeDesc::Kind::Struct: return "une structure " + t.name;
            default: return "un " + t.text();
        }
    }
    core::Result<ObjArg> seqArg(const std::string& u, const Expr& a, bool arrOk) {
        auto s = objArg(a);
        if (!s) return s;
        const TypeDesc* t = s->obj && !s->isScalar ? s->obj->type.get() : nullptr;
        if (t && (t->kind == TypeDesc::Kind::List || (arrOk && t->kind == TypeDesc::Kind::Array))) return s;
        if (t && t->kind == TypeDesc::Kind::Array)
            return dialectError(u + " : un ARRAY a une taille fixe (" + std::to_string(s->obj->items.size())
                                + " cases) \xE2\x80\x94 une LIST ou un VECTOR est attendu");
        return dialectError(u + " : " + std::string(arrOk ? "une LIST, un VECTOR ou un ARRAY" : "une LIST ou un VECTOR") + " est attendu, pas "
                            + kindName(*s));
    }
    core::Result<ObjArg> mapArg(const std::string& u, const Expr& a) {
        auto s = objArg(a);
        if (!s) return s;
        if (s->obj && !s->isScalar && s->obj->type && s->obj->type->kind == TypeDesc::Kind::Map) return s;
        return dialectError(u + " : une MAP est attendue, pas " + kindName(*s));
    }
    core::Result<ObjArg> tupleArg(const std::string& u, const Expr& a) {
        auto s = objArg(a);
        if (!s) return s;
        if (s->obj && !s->isScalar && s->obj->type && s->obj->type->kind == TypeDesc::Kind::Tuple) return s;
        return dialectError(u + " : un TUPLE est attendu, pas " + kindName(*s));
    }
    core::Err<core::Error> constantRefused(const std::string& u) const { return dialectError(u + " : une constante ne s'\xC3\xA9" "crit pas"); }
    // Apres une ecriture : une variable de l'environnement copiee est recopiee.
    core::Result<bool> commitArg(const std::string& u, const ObjArg& a) {
        if (a.envName.empty()) return true;
        std::string why;
        if (!storeToEnv(a.envName, *a.obj, &why)) return dialectError(u + " : " + why);
        return true;
    }
    // Le premier indice : 0 pour une liste ; la borne basse d'un ARRAY (a une dimension).
    core::Result<std::int64_t> lowOf(const std::string& u, const Obj& L) {
        if (!L.type || L.type->kind != TypeDesc::Kind::Array) return std::int64_t{0};
        if (L.type->bounds.size() != 1)
            return dialectError(u + " : un ARRAY \xC3\xA0 une dimension est attendu (celui-ci en a " + std::to_string(L.type->bounds.size()) + ")");
        return L.type->bounds[0].first;
    }
    core::Result<std::int64_t> intArg(const std::string& u, const Expr& a, const std::string& what) {
        auto v = rich(a);
        if (!v) return core::Err<core::Error>(v.error());
        if (v->o || !(isInteger(v->v.type()) && v->v.type() != Type::Time && !isDateType(v->v.type())))
            return dialectError(u + " : " + what + " est un entier");
        return v->v.asInteger();
    }
    core::Result<bool> boolArg(const std::string& u, const Expr& a) {
        auto v = rich(a);
        if (!v) return core::Err<core::Error>(v.error());
        if (v->o || v->v.type() != Type::Bool) return dialectError(u + " : TRUE ou FALSE est attendu, pas " + (v->o ? typeNameOf(*v) : v->v.display()));
        return v->v.isTruthy();
    }
    static std::string scalarTypeName(const TypeDesc& t) {
        if (!t.name.empty() && t.kind == TypeDesc::Kind::Scalar) return upperOf(t.name);
        if (t.kind == TypeDesc::Kind::Scalar) return std::string(toString(t.scalar));
        return t.text();
    }
    // Une valeur vers le type d'un element : refusee comme l'IHM la refuserait (un texte n'est pas
    // un nombre, un reel a decimales n'est pas un entier, un entier hors des bornes de son type).
    core::Result<ObjRef> fitTo(const std::string& u, const TypeRef& elem, const RV& v) {
        if (!elem) return dialectError(u + " : le type des \xC3\xA9l\xC3\xA9ments est inconnu");
        auto made = instantiate(elem);
        if (!made) return made;
        if (elem->kind == TypeDesc::Kind::Scalar && !v.o) {
            const Type want = elem->scalar, got = v.v.type();
            const std::string tn = scalarTypeName(*elem);
            const std::string shown = v.v.display();
            if (got == Type::String && want != Type::String && want != Type::Unknown)
                return dialectError(u + " : " + shown + " n'est pas un " + tn);
            if (want == Type::String && got != Type::String)
                return dialectError(u + " : " + shown + " n'est pas une STRING : \xC3\xA9" "cris '" + shown + "'");
            if (want == Type::Bool && got != Type::Bool) return dialectError(u + " : " + shown + " n'est pas un BOOL");
            lib::IntType it;
            if (lib::intType(tn, it)) {
                if (got == Type::Real && v.v.asReal() != std::floor(v.v.asReal()))
                    return dialectError(u + " : " + shown + " n'est pas un entier (" + tn + ")");
                if (got == Type::Bool) return dialectError(u + " : " + shown + " n'est pas un entier (" + tn + ")");
                const Value whole = got == Type::Real ? Value::integer(Type::LInt, static_cast<std::int64_t>(v.v.asReal())) : v.v;
                if (!lib::fits(lib::wideOf(whole), it)) {
                    const Value hi = Value::integer(it.type, static_cast<std::int64_t>(it.hiBits));
                    return dialectError(u + " : " + whole.display() + " est hors des bornes d'un " + tn + " (" + std::to_string(it.lo) + ".."
                                        + hi.display() + ")");
                }
            }
        }
        std::string why;
        if (!storeItem(**made, v, &why)) return dialectError(u + " : " + why);
        return made;
    }
    // La valeur d'un element (une copie pour un objet : ce qui est rendu ne touche pas la liste).
    static RV valueOfItem(const ObjRef& o) {
        if (o && o->type && o->type->kind == TypeDesc::Kind::Scalar) return RV{o->value, nullptr};
        return RV{{}, o ? deepCopy(*o) : nullptr};
    }
    // COMPARAISON#... : son rang (0 Egal ... 5 SuperieurOuEgal).
    core::Result<int> comparisonArg(const std::string& u, const Expr& a) {
        const std::string want = u + " : une COMPARAISON est attendue (COMPARAISON#Superieur\xE2\x80\xA6), pas ";
        if (a.kind == Expr::Kind::Reference && a.name.find('#') != std::string::npos
            && upperOf(a.name.substr(0, a.name.find('#'))) != "COMPARAISON")
            return dialectError(want + a.name);
        auto v = rich(a);
        if (!v) return core::Err<core::Error>(v.error());
        if (v->o || !isInteger(v->v.type()) || v->v.type() == Type::Bool || v->v.asInteger() < 0 || v->v.asInteger() > 5)
            return dialectError(want + (v->o ? typeNameOf(*v) : v->v.display()));
        return static_cast<int>(v->v.asInteger());
    }
    static bool compares(int op, int c) {
        switch (op) {
            case 0: return c == 0;
            case 1: return c != 0;
            case 2: return c < 0;
            case 3: return c <= 0;
            case 4: return c > 0;
            default: return c >= 0;
        }
    }
    // L'element vu par une comparaison ou un tri : lui-meme (-1), ou un de ses membres (une
    // structure : 'Poids' ; un TUPLE : 'Item2'). `membre` nul : absent.
    core::Result<int> projection(const std::string& u, const Obj& L, const RV* membre) {
        TypeRef el = L.type ? L.type->element : nullptr;
        if (!L.items.empty() && L.items.front() && L.items.front()->type) el = L.items.front()->type;
        const bool structured = el && (el->kind == TypeDesc::Kind::Struct || el->kind == TypeDesc::Kind::Tuple);
        if (!membre) {
            if (el && el->kind == TypeDesc::Kind::Struct && !el->members.empty())
                return dialectError(u + " : des " + el->name + " \xE2\x80\x94 dis quel membre comparer : '"
                                    + el->members[el->members.size() > 1 ? 1 : 0].first + "'");
            if (el && el->kind == TypeDesc::Kind::Tuple)
                return dialectError(u + " : des " + el->text() + " \xE2\x80\x94 dis quel champ comparer : 'Item1'");
            return -1;
        }
        if (membre->o || membre->v.type() != Type::String) return dialectError(u + " : le membre s'\xC3\xA9" "crit entre apostrophes : 'Poids'");
        const std::string name = membre->v.asString();
        if (!structured) return dialectError(u + " : des " + (el ? el->text() : std::string("valeurs")) + " n'ont pas de membres");
        std::vector<std::string> pool;
        for (std::size_t k = 0; k < el->members.size(); ++k) {
            if (upperOf(el->members[k].first) == upperOf(name)) return static_cast<int>(k);
            pool.push_back(el->members[k].first);
        }
        if (el->kind == TypeDesc::Kind::Tuple)
            return dialectError(u + " : un " + el->text() + " a Item1 \xC3\xA0 Item" + std::to_string(el->members.size()) + ", pas '" + name + "'");
        return dialectError(el->name + " n'a pas de membre " + name + lib::closest(name, pool));
    }
    static const ObjRef& projected(const ObjRef& item, int member) {
        if (member < 0 || !item || static_cast<std::size_t>(member) >= item->items.size()) return item;
        return item->items[static_cast<std::size_t>(member)];
    }
    // Comparer un element (ou son membre) a une valeur simple ; deux objets : = et <> seulement.
    core::Result<int> compareItem(const std::string& u, const ObjRef& x, const RV& v) {
        if (x && x->type && x->type->kind == TypeDesc::Kind::Scalar && !v.o) return x->value.compare(v.v);
        if (sameItem(x, v)) return 0;
        if (x && x->type && x->type->kind != TypeDesc::Kind::Scalar)
            return dialectError(u + " : un " + x->type->text() + " ne se compare pas \xC3\xA0 une valeur (MEMBRE : lequel comparer)");
        return 1;
    }
    static std::string sigOf(const lib::NativeInfo& n) {
        const std::string s(n.signature);
        const auto at = s.find(" : ");
        return at == std::string::npos ? s : s.substr(0, at);
    }
    // Le type ecrit d'un argument designe ("SINT", "WORD"...) ; vide : inconnu.
    std::string writtenType(const Expr& a) {
        if (a.kind != Expr::Kind::Reference) return {};
        if (auto* p = findLocal(a.name)) {
            if (p->obj && p->obj->type && p->obj->type->kind == TypeDesc::Kind::Scalar) return scalarTypeName(*p->obj->type);
            if (!p->name.empty()) return upperOf(env_.declaredType(p->name));
            return {};
        }
        return upperOf(env_.declaredType(a.name));
    }
    static std::string valueTypeText(Type t) {
        switch (t) {
            case Type::Int: return "INT";
            case Type::DInt: return "DINT";
            case Type::LInt: return "LINT";
            case Type::UInt: return "UINT";
            case Type::UDInt: return "UDINT";
            case Type::ULInt: return "ULINT";
            case Type::Byte: return "BYTE";
            case Type::Word: return "WORD";
            case Type::DWord: return "DWORD";
            case Type::LWord: return "LWORD";
            case Type::Real: return "REAL";
            default: return std::string(toString(t));
        }
    }

    // ---- l'aiguillage : nul, pas une native de la 1.12.7 ---------------------------------
    // ==== 1.12.19 : LES NATIVES A RAPPEL - LIST_FILTER, LIST_MAP, LIST_FOR_EACH, MAP_FOR_EACH ; les formes
    // rappel de LIST_COUNT_IF, LIST_FIND_IF, LIST_ANY, LIST_ALL, LIST_NONE, LIST_REMOVE_IF (L, f), LIST_SORT (L, f),
    // LIST_TRANSFORM (S, D, f) ; IHM_APRES, IHM_CHAQUE, IHM_ANNULER (le minuteur de l'environnement). ====
    std::optional<core::Result<RV>> natives1219(const std::string& callee, const Expr& e) {
        const std::string u = upperOf(callee);
        const bool ifForm = u == "LIST_COUNT_IF" || u == "LIST_FIND_IF" || u == "LIST_ANY" || u == "LIST_ALL" || u == "LIST_NONE"
                         || u == "LIST_REMOVE_IF";
        const bool fresh = u == "LIST_FILTER" || u == "LIST_MAP" || u == "LIST_FOR_EACH" || u == "MAP_FOR_EACH";
        const bool timer = u == "IHM_APRES" || u == "IHM_CHAQUE" || u == "IHM_ANNULER";
        if (!ifForm && !fresh && !timer && u != "LIST_SORT" && u != "LIST_TRANSFORM") return std::nullopt;
        if (env_.overloads(callee) > 0 || env_.dialectFunction(callee)) return std::nullopt;     // une fonction du projet passe avant
        for (const auto& a : e.arguments)
            if (!a.first.empty()) return std::nullopt;
        const std::size_t argc = e.arguments.size();
        if (ifForm && argc != 2) return std::nullopt;
        if (u == "LIST_SORT" && argc != 2) return std::nullopt;
        if (u == "LIST_TRANSFORM" && argc != 3) return std::nullopt;
        using R = std::optional<core::Result<RV>>;
        const auto fail = [&](std::string why) -> R { return R(core::Result<RV>(dialectError(u + " : " + std::move(why)))); };
        if (u == "IHM_ANNULER") {
            if (argc != 1) return fail("son num\xC3\xA9ro (celui que rend IHM_APRES ou IHM_CHAQUE)");
            auto id = intArg(u, *e.arguments[0].second, "le num\xC3\xA9ro");
            if (!id) return R(core::Result<RV>(core::Err<core::Error>(id.error())));
            return R(core::Result<RV>(RV{Value::boolean(env_.cancelCallback(*id)), nullptr}));
        }
        if (argc != 2 && !(u == "LIST_TRANSFORM" && argc == 3)) return fail("deux arguments : " + u + (timer ? "(T#5s, Arreter)" : "(L, f)"));
        const std::size_t fnAt = u == "LIST_TRANSFORM" ? 2 : 1;
        auto fnv = rich(*e.arguments[fnAt].second);
        if (!fnv) return R(core::Result<RV>(core::Err<core::Error>(fnv.error())));
        if (!isCallbackObj(fnv->o)) {
            if (u == "LIST_SORT" || u == "LIST_TRANSFORM") return std::nullopt;   // DECROISSANT, TRANSFORMATION# : la forme d'avant
            if (ifForm)
                return fail("une COMPARAISON demande sa valeur (" + u + "(L, COMPARAISON#Superieur, 5)) ; sinon un rappel ("
                            + u + "(L, Est_Valide), " + u + "(L, LAMBDA (x : REAL) : BOOL => x > 5))");
            return fail("un rappel est attendu (une fonction, une LAMBDA), pas " + (fnv->o ? "un " + typeNameOf(*fnv) : fnv->v.display()));
        }
        const ObjRef f = fnv->o;
        const auto call = [&](std::vector<RV> args) { return invokeCallback(*f, args, e.line, u); };
        const auto truth = [&](const core::Result<RV>& r, bool& out) -> std::optional<std::string> {
            if (!r) return r.error().message();
            if (r->o || r->v.type() != Type::Bool) return std::string("le rappel doit rendre un BOOL, pas ") + (r->o ? "un " + typeNameOf(*r) : r->v.display());
            out = r->v.isTruthy();
            return std::nullopt;
        };
        if (timer) {
            auto d = rich(*e.arguments[0].second);
            if (!d) return R(core::Result<RV>(core::Err<core::Error>(d.error())));
            if (d->o || !isInteger(d->v.type())) return fail("le d\xC3\xA9lai est une dur\xC3\xA9" "e (T#5s) ou des millisecondes");
            const std::int64_t ms = d->v.asInteger();
            if (ms < 0 || (u == "IHM_CHAQUE" && ms < 10)) return fail(u == "IHM_CHAQUE" ? "une p\xC3\xA9riode de 10 ms au moins" : "un d\xC3\xA9lai positif");
            std::int64_t id = 0;
            std::string why;
            if (!env_.scheduleCallback(ms, u == "IHM_CHAQUE" ? ms : 0, deepCopy(*f), id, &why)) return fail(why);
            return R(core::Result<RV>(RV{Value::integer(Type::DInt, id), nullptr}));
        }
        if (u == "MAP_FOR_EACH") {
            auto m = mapArg(u, *e.arguments[0].second);
            if (!m) return R(core::Result<RV>(core::Err<core::Error>(m.error())));
            const ObjRef M = m->obj;                         // une copie des cles : le rappel peut changer la MAP
            std::vector<std::pair<RV, ObjRef>> items;
            for (const auto& [k, v] : M->textKeys) items.emplace_back(RV{Value::text(k), nullptr}, v);
            for (const auto& [k, v] : M->intKeys) items.emplace_back(RV{Value::integer(Type::DInt, k), nullptr}, v);
            for (const auto& [k, v] : items) {
                auto r = call({k, valueOfItem(v)});
                if (!r) return R(core::Result<RV>(core::Err<core::Error>(r.error())));
            }
            return R(core::Result<RV>(RV{Value::integer(Type::DInt, static_cast<std::int64_t>(items.size())), nullptr}));
        }
        if (u == "LIST_TRANSFORM") {
            auto src = seqArg(u, *e.arguments[0].second, true);
            if (!src) return R(core::Result<RV>(core::Err<core::Error>(src.error())));
            auto dst = seqArg(u, *e.arguments[1].second, false);
            if (!dst) return R(core::Result<RV>(core::Err<core::Error>(dst.error())));
            if (dst->constant) return R(core::Result<RV>(constantRefused(u)));
            Obj& D = *dst->obj;
            if (isSetType(*D.type)) return R(core::Result<RV>(setRefused(u)));
            const std::vector<ObjRef> from = src->obj->items;
            std::vector<ObjRef> made;
            for (const auto& x : from) {
                auto r = call({valueOfItem(x)});
                if (!r) return R(core::Result<RV>(core::Err<core::Error>(r.error())));
                auto m = fitTo(u, D.type->element, *r);
                if (!m) return R(core::Result<RV>(core::Err<core::Error>(m.error())));
                made.push_back(*m);
            }
            D.items = std::move(made);
            normalizeList(D);
            auto c = commitArg(u, *dst);
            if (!c) return R(core::Result<RV>(core::Err<core::Error>(c.error())));
            return R(core::Result<RV>(RV{Value::integer(Type::DInt, static_cast<std::int64_t>(from.size())), nullptr}));
        }
        const bool writes = u == "LIST_REMOVE_IF" || u == "LIST_SORT";
        auto list = seqArg(u, *e.arguments[0].second, !writes || u == "LIST_SORT");
        if (!list) return R(core::Result<RV>(core::Err<core::Error>(list.error())));
        Obj& L = *list->obj;
        if (writes && list->constant) return R(core::Result<RV>(constantRefused(u)));
        if (u == "LIST_SORT" && isSetType(*L.type)) return R(core::Result<RV>(setRefused(u)));
        if (u == "LIST_REMOVE_IF" && L.type->kind == TypeDesc::Kind::Array) return fail("un ARRAY a une taille fixe : une LIST ou un VECTOR");
        const std::vector<ObjRef> items = L.items;           // le rappel peut changer la liste : on parcourt une copie
        auto low = lowOf(u, L);
        if (!low) return R(core::Result<RV>(core::Err<core::Error>(low.error())));
        if (u == "LIST_SORT") {
            std::optional<std::string> trouble;
            std::vector<ObjRef> sorted = items;
            std::stable_sort(sorted.begin(), sorted.end(), [&](const ObjRef& a, const ObjRef& b) {
                if (trouble) return false;
                bool before = false;
                trouble = truth(call({valueOfItem(a), valueOfItem(b)}), before);
                return !trouble && before;
            });
            if (trouble) return fail(*trouble);
            L.items = std::move(sorted);
            auto c = commitArg(u, *list);
            if (!c) return R(core::Result<RV>(core::Err<core::Error>(c.error())));
            return R(core::Result<RV>(RV{Value::boolean(true), nullptr}));
        }
        if (u == "LIST_FOR_EACH") {
            for (const auto& x : items) {
                auto r = call({valueOfItem(x)});
                if (!r) return R(core::Result<RV>(core::Err<core::Error>(r.error())));
            }
            return R(core::Result<RV>(RV{Value::integer(Type::DInt, static_cast<std::int64_t>(items.size())), nullptr}));
        }
        if (u == "LIST_MAP") {
            TypeRef element = f->type->element;
            if (!element || (element->kind == TypeDesc::Kind::Scalar && element->scalar == Type::Unknown)) element = L.type->element;
            auto out = makeObj(listType(element));
            for (const auto& x : items) {
                auto r = call({valueOfItem(x)});
                if (!r) return R(core::Result<RV>(core::Err<core::Error>(r.error())));
                auto m = fitTo(u, element, *r);
                if (!m) return R(core::Result<RV>(core::Err<core::Error>(m.error())));
                out->items.push_back(*m);
            }
            return R(core::Result<RV>(RV{{}, out}));
        }
        // Un predicat par element : LIST_FILTER et les formes _IF.
        std::vector<bool> keep;
        keep.reserve(items.size());
        std::int64_t count = 0, first = -1;
        for (std::size_t k = 0; k < items.size(); ++k) {
            bool yes = false;
            if (auto why = truth(call({valueOfItem(items[k])}), yes)) return fail(*why);
            keep.push_back(yes);
            if (yes) {
                ++count;
                if (first < 0) first = static_cast<std::int64_t>(k);
                if (u == "LIST_FIND_IF" || u == "LIST_ANY" || u == "LIST_NONE") break;
            }
        }
        if (u == "LIST_FILTER") {
            auto out = makeObj(L.type->kind == TypeDesc::Kind::List ? L.type : listType(L.type->element));
            for (std::size_t k = 0; k < keep.size(); ++k)
                if (keep[k] && items[k]) out->items.push_back(deepCopy(*items[k]));
            normalizeList(*out);
            return R(core::Result<RV>(RV{{}, out}));
        }
        if (u == "LIST_REMOVE_IF") {
            std::vector<ObjRef> kept;
            for (std::size_t k = 0; k < items.size(); ++k)
                if (!keep[k]) kept.push_back(items[k]);
            L.items = std::move(kept);
            auto c = commitArg(u, *list);
            if (!c) return R(core::Result<RV>(core::Err<core::Error>(c.error())));
            return R(core::Result<RV>(RV{Value::integer(Type::DInt, count), nullptr}));
        }
        if (u == "LIST_FIND_IF") return R(core::Result<RV>(RV{Value::integer(Type::DInt, first < 0 ? -1 : first + *low), nullptr}));
        if (u == "LIST_COUNT_IF") return R(core::Result<RV>(RV{Value::integer(Type::DInt, count), nullptr}));
        if (u == "LIST_ANY") return R(core::Result<RV>(RV{Value::boolean(count > 0), nullptr}));
        if (u == "LIST_NONE") return R(core::Result<RV>(RV{Value::boolean(count == 0), nullptr}));
        return R(core::Result<RV>(RV{Value::boolean(count == static_cast<std::int64_t>(items.size())), nullptr}));   // LIST_ALL
    }
    // ==== fin 1.12.19 : les natives a rappel ====

    // ==== 1.12.20 : LES DERNIERES NATIVES A RAPPEL. MAP_REMOVE_IF (m, f) : f(cle, valeur) ; ACCUMULATE, REDUCE (T, init, f),
    // PARTIAL_SUM, INCLUSIVE_SCAN (T, f), EXCLUSIVE_SCAN (T, init, f) : f(a, b) a la place de l'OPERATION ;
    // TRANSFORM_REDUCE, TRANSFORM_INCLUSIVE_SCAN, TRANSFORM_EXCLUSIVE_SCAN : l'operation, la transformation (f(x)) ou
    // les deux en rappels ; LIST_SORT_BY (L, f [, decroissant]), LIST_MIN_INDEX, LIST_MAX_INDEX (L, f) : f(element)
    // donne la cle, a la place d'un membre ; IHM_SUR_CHANGEMENT (x, f) : f chaque fois que x change. Sans rappel : la
    // forme d'avant (natives1127). Avec un rappel, les elements peuvent etre autre chose que des nombres (des
    // structures) : l'accumulateur garde le type de init, comme le C++. ====
    // Le chemin d'une variable, ses indices evalues (Niveaux[i] -> "Niveaux[2]") ; vide : pas une variable.
    core::Result<std::string> watchedPath(const Expr& x, int depth = 0) {
        if (depth > 16) return std::string{};
        if (x.kind == Expr::Kind::Reference) return x.name.find('#') == std::string::npos ? x.name : std::string{};
        if (x.kind == Expr::Kind::Member && x.lhs) {
            auto base = watchedPath(*x.lhs, depth + 1);
            if (!base || base->empty()) return base;
            return *base + "." + x.name;
        }
        if (x.kind == Expr::Kind::Index && x.lhs && x.rhs) {
            auto base = watchedPath(*x.lhs, depth + 1);
            if (!base || base->empty()) return base;
            std::vector<const Expr*> at{x.rhs.get()};
            if (x.rhs2) at.push_back(x.rhs2.get());
            for (const auto& m : x.more)
                if (m) at.push_back(m.get());
            std::string out = *base + "[";
            for (std::size_t k = 0; k < at.size(); ++k) {
                auto v = rich(*at[k]);
                if (!v) return core::Err<core::Error>(v.error());
                if (v->o) return std::string{};
                out += (k ? "," : "") + v->v.display();
            }
            return out + "]";
        }
        return std::string{};
    }
    std::optional<core::Result<RV>> natives1220(const std::string& callee, const Expr& e) {
        const std::string u = upperOf(callee);
        const bool fold = u == "ACCUMULATE" || u == "REDUCE" || u == "TRANSFORM_REDUCE";
        const bool scan = u == "PARTIAL_SUM" || u == "INCLUSIVE_SCAN" || u == "EXCLUSIVE_SCAN" || u == "TRANSFORM_INCLUSIVE_SCAN"
                       || u == "TRANSFORM_EXCLUSIVE_SCAN";
        const bool keyed = u == "LIST_SORT_BY" || u == "LIST_MIN_INDEX" || u == "LIST_MAX_INDEX";
        if (!fold && !scan && !keyed && u != "MAP_REMOVE_IF" && u != "IHM_SUR_CHANGEMENT") return std::nullopt;
        if (env_.overloads(callee) > 0 || env_.dialectFunction(callee)) return std::nullopt;     // une fonction du projet passe avant
        for (const auto& a : e.arguments)
            if (!a.first.empty()) return std::nullopt;
        const std::size_t argc = e.arguments.size();
        using R = std::optional<core::Result<RV>>;
        const auto err = [](const core::Error& x) -> R { return R(core::Result<RV>(core::Err<core::Error>(x))); };
        const auto fail = [&](std::string why) -> R { return R(core::Result<RV>(dialectError(u + " : " + std::move(why)))); };
        const auto done = [](RV v) -> R { return R(core::Result<RV>(std::move(v))); };
        const auto arg = [&](std::size_t k) -> const Expr& { return *e.arguments[k].second; };
        // L'argument k s'il est un rappel ; nul sinon (la forme d'avant le lira).
        const auto callbackAt = [&](std::size_t k) -> core::Result<ObjRef> {
            auto v = rich(arg(k));
            if (!v) return core::Err<core::Error>(v.error());
            return isCallbackObj(v->o) ? v->o : ObjRef{};
        };
        const auto call = [&](const ObjRef& f, std::vector<RV> args) { return invokeCallback(*f, args, e.line, u); };
        const auto truth = [&](const core::Result<RV>& r, bool& out) -> std::optional<std::string> {
            if (!r) return r.error().message();
            if (r->o || r->v.type() != Type::Bool) return std::string("le rappel doit rendre un BOOL, pas ") + (r->o ? "un " + typeNameOf(*r) : r->v.display());
            out = r->v.isTruthy();
            return std::nullopt;
        };

        // ---- MAP_REMOVE_IF(m, f) : retire les paires pour lesquelles f(cle, valeur) rend TRUE ----
        if (u == "MAP_REMOVE_IF") {
            if (argc != 2) return std::nullopt;                       // (m, COMPARAISON, valeur) : la forme d'avant
            auto f = callbackAt(1);
            if (!f) return err(f.error());
            if (!*f)
                return fail("une COMPARAISON demande sa valeur (MAP_REMOVE_IF(m, COMPARAISON#Inferieur, 0.0)) ; sinon un rappel qui re\xC3\xA7oit "
                            "la cl\xC3\xA9 et la valeur (MAP_REMOVE_IF(m, LAMBDA (k : STRING; v : REAL) : BOOL => v <= 0.0))");
            auto m = mapArg(u, arg(0));
            if (!m) return err(m.error());
            if (m->constant) return R(core::Result<RV>(constantRefused(u)));
            Obj& M = *m->obj;
            std::vector<std::pair<RV, ObjRef>> items;               // une copie : le rappel peut changer la MAP
            for (const auto& [k, v] : M.textKeys) items.emplace_back(RV{Value::text(k), nullptr}, v);
            for (const auto& [k, v] : M.intKeys) items.emplace_back(RV{Value::integer(M.type->key, k), nullptr}, v);
            std::int64_t removed = 0;
            for (const auto& [k, v] : items) {
                bool yes = false;
                if (auto why = truth(call(*f, {k, valueOfItem(v)}), yes)) return fail(*why);
                if (!yes) continue;
                removed += static_cast<std::int64_t>(k.v.type() == Type::String ? M.textKeys.erase(k.v.asString()) : M.intKeys.erase(k.v.asInteger()));
            }
            auto c = commitArg(u, *m);
            if (!c) return err(c.error());
            return done(RV{Value::integer(Type::DInt, removed), nullptr});
        }

        // ---- IHM_SUR_CHANGEMENT(x, f) : f() ou f(nouvelle valeur), chaque fois que x change (vu a chaque tour de l'IHM) ----
        if (u == "IHM_SUR_CHANGEMENT") {
            if (argc != 2) return fail("deux arguments : IHM_SUR_CHANGEMENT(Mode_Pompe, Sur_Mode)");
            auto f = callbackAt(1);
            if (!f) return err(f.error());
            if (!*f) return fail("un rappel est attendu (une fonction, une LAMBDA) : IHM_SUR_CHANGEMENT(Mode_Pompe, Sur_Mode)");
            const std::size_t params = (*f)->type ? (*f)->type->members.size() : 0;
            if (params > 1) return fail("le rappel re\xC3\xA7oit la nouvelle valeur, ou rien : CALLBACK (v : INT) ou CALLBACK ()");
            auto path = watchedPath(arg(0));
            if (!path) return err(path.error());
            if (path->empty()) return fail("une variable est attendue (Mode_Pompe, Pompe.Mode, Niveaux[2]), pas " + exprText(arg(0)));
            const std::string root = path->substr(0, path->find_first_of(".["));
            if (findLocal(root)) return fail(root + " est une variable de ce code : elle dispara\xC3\xAEt avec lui \xE2\x80\x94 surveille une variable de l'IHM");
            auto now = rich(arg(0));
            if (!now) return err(now.error());
            if (now->o)
                return fail("une valeur simple se surveille (un nombre, un texte, un BOOL\xE2\x80\xA6), pas un " + typeNameOf(*now) + " \xE2\x80\x94 un de ses membres (Pompe.Mode)");
            std::int64_t id = 0;
            std::string why;
            if (!env_.watchCallback(*path, now->v, deepCopy(**f), params == 1, id, &why)) return fail(why);
            return done(RV{Value::integer(Type::DInt, id), nullptr});
        }

        // ---- LIST_SORT_BY(L, f [, decroissant]), LIST_MIN_INDEX(L, f), LIST_MAX_INDEX(L, f) : f(element) donne la cle ----
        if (keyed) {
            if (argc < 2 || argc > (u == "LIST_SORT_BY" ? 3u : 2u)) return std::nullopt;
            auto f = callbackAt(1);
            if (!f) return err(f.error());
            if (!*f) return std::nullopt;                              // 'Poids' : un membre, la forme d'avant
            const bool sorts = u == "LIST_SORT_BY";
            auto list = seqArg(u, arg(0), true);
            if (!list) return err(list.error());
            Obj& L = *list->obj;
            if (sorts && list->constant) return R(core::Result<RV>(constantRefused(u)));
            if (sorts && isSetType(*L.type)) return R(core::Result<RV>(setRefused(u)));
            bool descending = false;
            if (argc > 2) {
                auto d = boolArg(u, arg(2));
                if (!d) return err(d.error());
                descending = *d;
            }
            const std::vector<ObjRef> items = L.items;               // le rappel peut changer la liste : on parcourt une copie
            std::vector<Value> keys;
            keys.reserve(items.size());
            for (const auto& x : items) {
                auto r = call(*f, {valueOfItem(x)});
                if (!r) return err(r.error());
                if (r->o) return fail("la cl\xC3\xA9 est une valeur simple (un nombre, un texte, une dur\xC3\xA9" "e), pas un " + typeNameOf(*r));
                keys.push_back(r->v);
            }
            if (!sorts) {
                auto low = lowOf(u, L);
                if (!low) return err(low.error());
                std::int64_t best = -1;
                for (std::size_t k = 0; k < keys.size(); ++k) {
                    if (best < 0) {
                        best = static_cast<std::int64_t>(k);
                        continue;
                    }
                    const int c = keys[k].compare(keys[static_cast<std::size_t>(best)]);
                    if (u == "LIST_MIN_INDEX" ? c < 0 : c > 0) best = static_cast<std::int64_t>(k);
                }
                return done(RV{Value::integer(Type::DInt, best < 0 ? -1 : best + *low), nullptr});
            }
            std::vector<std::size_t> order(items.size());
            for (std::size_t k = 0; k < order.size(); ++k) order[k] = k;
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                const int c = keys[a].compare(keys[b]);
                return descending ? c > 0 : c < 0;
            });
            std::vector<ObjRef> sorted;
            sorted.reserve(order.size());
            for (const std::size_t k : order) sorted.push_back(items[k]);
            L.items = std::move(sorted);
            auto c = commitArg(u, *list);
            if (!c) return err(c.error());
            return done(RV{Value::boolean(true), nullptr});
        }

        // ---- std::numeric : un rappel a la place de l'OPERATION (f(a, b)) ou de la TRANSFORMATION (f(x)) ----
        const bool transforms = u.rfind("TRANSFORM_", 0) == 0;
        const bool withInit = u == "ACCUMULATE" || u == "REDUCE" || u == "TRANSFORM_REDUCE" || u == "EXCLUSIVE_SCAN" || u == "TRANSFORM_EXCLUSIVE_SCAN";
        const std::size_t opAt = withInit ? 2 : 1, trAt = opAt + 1;   // T, [init], op, [transformation]
        if (argc != (transforms ? trAt + 1 : opAt + 1)) {
            // ACCUMULATE(L, f), REDUCE(L, f), EXCLUSIVE_SCAN(L, f) : la valeur de depart manque.
            if (!transforms && withInit && argc == 2) {
                auto f = callbackAt(1);
                if (!f) return err(f.error());
                if (*f) return fail("la valeur de d\xC3\xA9part vient avant le rappel : " + u + "(L, 0.0, f)");
            }
            return std::nullopt;
        }
        auto opF = callbackAt(opAt);
        if (!opF) return err(opF.error());
        const ObjRef op = *opF;
        ObjRef tr;
        if (transforms) {
            auto t = callbackAt(trAt);
            if (!t) return err(t.error());
            tr = *t;
        }
        if (!op && !tr) return std::nullopt;                         // OPERATION#, TRANSFORMATION# : la forme d'avant
        int opRank = 0, trRank = 0;
        if (!op) {
            auto r = enumArg(u, arg(opAt), "OPERATION");
            if (!r) return err(r.error());
            opRank = *r;
        }
        if (transforms && !tr) {
            auto r = enumArg(u, arg(trAt), "TRANSFORMATION");
            if (!r) return err(r.error());
            trRank = *r;
        }
        auto seq = seqArg(u, arg(0), true);
        if (!seq) return err(seq.error());
        const std::vector<ObjRef> items = seq->obj->items;
        // Un element, transforme (par le rappel, ou par la TRANSFORMATION : un nombre).
        const auto transformed = [&](const ObjRef& item) -> core::Result<RV> {
            RV x = valueOfItem(item);
            if (!transforms) return x;
            if (tr) return call(tr, {x});
            if (x.o || !isNumeric(x.v.type()) || x.v.type() == Type::Time)
                return dialectError(u + " : une TRANSFORMATION prend des nombres, pas " + (x.o ? "un " + typeNameOf(x) : x.v.display())
                                    + " \xE2\x80\x94 un rappel (x) pour d'autres valeurs");
            return RV{transform(trRank, x.v), nullptr};
        };
        // Deux valeurs combinees (par le rappel, ou par l'OPERATION : des nombres).
        const auto combined = [&](const RV& a, const RV& b) -> core::Result<RV> {
            if (op) return call(op, {a, b});
            if (a.o || b.o)
                return dialectError(u + " : une OPERATION combine des nombres, pas un " + typeNameOf(a.o ? a : b) + " \xE2\x80\x94 un rappel (a, b) pour d'autres valeurs");
            auto r = applyOp(opRank, a.v, b.v, e.line);
            if (!r) return core::Err<core::Error>(r.error());
            return RV{*r, nullptr};
        };
        const auto number = [](Type t) { return isNumeric(t) && t != Type::Time && t != Type::Bool; };
        // Un resultat garde dans le type d'un autre (l'accumulateur garde le type de init, comme le C++).
        const auto kept = [&](const RV& like, const RV& got) -> core::Result<RV> {
            if (like.o) {
                auto m = fitTo(u, like.o->type, got);
                if (!m) return core::Err<core::Error>(m.error());
                return RV{{}, *m};
            }
            const Type lt = like.v.type(), gt = got.v.type();
            if (!got.o && number(lt) && number(gt)) return RV{asTypeOf(like.v, got.v), nullptr};
            if (!got.o && (gt == lt || lt == Type::Unknown)) return got;
            return dialectError(u + " : le rappel doit rendre un " + valueTypeText(lt) + ", pas " + (got.o ? "un " + typeNameOf(got) : got.v.display()));
        };
        const auto start = [&]() -> core::Result<RV> {
            auto init = rich(arg(1));
            if (!init) return init;
            return init->o ? RV{{}, deepCopy(*init->o)} : *init;
        };
        if (fold) {
            auto init = start();
            if (!init) return err(init.error());
            RV acc = *init;
            for (const auto& item : items) {
                auto y = transformed(item);
                if (!y) return err(y.error());
                auto r = combined(acc, *y);
                if (!r) return err(r.error());
                auto k = kept(*init, *r);
                if (!k) return err(k.error());
                acc = *k;
            }
            return done(acc);
        }
        // Les scans : un resultat par element.
        std::vector<RV> out;
        out.reserve(items.size());
        if (u == "EXCLUSIVE_SCAN" || u == "TRANSFORM_EXCLUSIVE_SCAN") {
            auto init = start();
            if (!init) return err(init.error());
            RV acc = *init;
            for (const auto& item : items) {
                out.push_back(acc);
                auto y = transformed(item);
                if (!y) return err(y.error());
                auto r = combined(acc, *y);
                if (!r) return err(r.error());
                auto k = kept(*init, *r);
                if (!k) return err(k.error());
                acc = *k;
            }
        } else {
            for (const auto& item : items) {
                auto y = transformed(item);
                if (!y) return err(y.error());
                if (out.empty()) {
                    out.push_back(*y);
                    continue;
                }
                auto r = combined(out.back(), *y);
                if (!r) return err(r.error());
                auto k = kept(out.front(), *r);
                if (!k) return err(k.error());
                out.push_back(*k);
            }
        }
        // "Comme T" si les resultats sont de son type (ou des nombres, convertis comme avec une OPERATION) ;
        // sinon une LIST de ce qu'ils sont (les poids cumules d'une liste de structures : une LIST OF REAL).
        const TypeRef te = seq->obj->type->element;
        TypeRef got = te;
        if (!out.empty()) got = out.front().o ? out.front().o->type : scalarType(out.front().v.type());
        const bool numbers = te && got && te->kind == TypeDesc::Kind::Scalar && got->kind == TypeDesc::Kind::Scalar && number(te->scalar) && number(got->scalar);
        const bool same = te && got && (numbers || sameType(*te, *got));
        const TypeRef outType = same ? (isSetType(*seq->obj->type) ? listType(te) : seq->obj->type) : listType(got);
        auto result = makeObj(outType);
        if (outType->kind == TypeDesc::Kind::List) result->items.clear();
        for (std::size_t k = 0; k < out.size(); ++k) {
            ObjRef item;
            if (numbers && !out[k].o) {
                item = makeObj(outType->element);
                item->value.assignFrom(out[k].v);
            } else {
                auto m = fitTo(u, outType->element, out[k]);
                if (!m) return err(m.error());
                item = *m;
            }
            if (outType->kind == TypeDesc::Kind::List) result->items.push_back(item);
            else if (k < result->items.size()) result->items[k] = item;
        }
        return done(RV{{}, result});
    }
    // ==== fin 1.12.20 : les dernieres natives a rappel ====

    std::optional<core::Result<RV>> natives1127(const std::string& callee, const Expr& e) {
        std::string u = upperOf(callee), tmpl;
        if (const auto lt = u.find('<'); lt != std::string::npos && u.back() == '>') {
            tmpl = u.substr(lt + 1, u.size() - lt - 2);
            u.resize(lt);
        }
        const auto* info = lib::native(u);
        if (!info) return std::nullopt;
        // Une fonction du projet du meme nom passe avant (ce qui tournait en 1.12.6 tourne pareil).
        if (tmpl.empty() && (env_.overloads(callee) > 0 || env_.dialectFunction(callee))) return std::nullopt;
        const auto argc = static_cast<int>(e.arguments.size());
        if (!e.outputs.empty() || argc < info->min || (info->max >= 0 && argc > info->max))
            return core::Result<RV>(dialectError("mauvais nombre d'arguments : " + sigOf(*info)));
        for (const auto& a : e.arguments)
            if (!a.first.empty()) return core::Result<RV>(dialectError(u + " : ses arguments se donnent dans l'ordre, sans nom (" + sigOf(*info) + ")"));
        const auto* t = lib::templateInfo(u);
        if (t && t->required && tmpl.empty()) return core::Result<RV>(dialectError(std::string(t->hint)));
        if (info->group == "numeric") return std::optional<core::Result<RV>>(numericNative(u, tmpl, e));
        if (info->group == "cast") return std::optional<core::Result<RV>>(castNative(u, tmpl, e));            // 1.12.20
        if (info->group == "bits") return std::optional<core::Result<RV>>(bitNative(u, e));
        if (info->group == "map") return std::optional<core::Result<RV>>(mapNative(u, e));
        if (info->group == "tuple") return std::optional<core::Result<RV>>(tupleNative(u, e));
        if (info->group == "set" || info->group == "ring") return std::optional<core::Result<RV>>(setNative(u, e));    // 1.12.7
        if (info->group == "math" || info->group == "texte" || info->group == "date")                                 // 1.12.16
            return std::optional<core::Result<RV>>(valueNative(u, e));
        if (info->group == "utility") return std::optional<core::Result<RV>>(utilityNative(u, e));                    // 1.12.16
        if (info->group == "fichiers") return std::optional<core::Result<RV>>(fileNative(u, e));                     // 1.12.26
        if (info->group == "attente")                                                                                // 1.12.26
            return std::optional<core::Result<RV>>(u == "WAIT_UNTIL" ? waitUntilNative(e) : waitNative(u, e));       // 1.13.0
        return std::optional<core::Result<RV>>(listNative(u, *info, e));
    }

    // ---- 1.12.7 : SET OF T (std::set) et RING[N] OF T (un tampon circulaire) ----------------
    core::Result<RV> setNative(const std::string& u, const Expr& e) {
        const auto arg = [&](std::size_t k) -> const Expr& { return *e.arguments[k].second; };
        // Un SET (SET_ADD, SET_REMOVE, SET_HAS), un RING (RING_ADD) : designe, ecrit en place.
        const auto holder = [&](bool ring) -> core::Result<ObjArg> {
            auto s = objArg(arg(0));
            if (!s) return s;
            const TypeDesc* t = s->obj && !s->isScalar ? s->obj->type.get() : nullptr;
            if (t && (ring ? isRingType(*t) : isSetType(*t))) return s;
            if (ring)
                return dialectError(u + " : un RING est attendu (RING[N] OF T), pas " + kindName(*s)
                                    + (t && t->kind == TypeDesc::Kind::List ? std::string(" \xE2\x80\x94 LIST_ADD ajoute \xC3\xA0 une liste") : std::string{}));
            return dialectError(u + " : un SET est attendu (SET OF T), pas " + kindName(*s)
                                + (t && t->kind == TypeDesc::Kind::List ? std::string(" \xE2\x80\x94 LIST_CONTAINS, LIST_ADD, LIST_REMOVE pour une liste")
                                                                        : std::string{}));
        };
        if (u == "SET_ADD" || u == "SET_REMOVE" || u == "SET_HAS" || u == "RING_ADD") {
            const bool ring = u == "RING_ADD";
            auto h = holder(ring);
            if (!h) return core::Err<core::Error>(h.error());
            Obj& S = *h->obj;
            if (u != "SET_HAS" && h->constant) return constantRefused(u);
            auto v = rich(arg(1));
            if (!v) return v;
            auto made = fitTo(u, S.type->element, *v);          // 'abc' dans un SET OF REAL : refuse
            if (!made) return core::Err<core::Error>(made.error());
            if (ring || u == "SET_ADD") {
                const bool changed = ringOrSetAdd(S, *made);
                auto c = commitArg(u, *h);
                if (!c) return core::Err<core::Error>(c.error());
                if (ring) return RV{Value::integer(Type::DInt, static_cast<std::int64_t>(S.items.size())), nullptr};
                return RV{Value::boolean(changed), nullptr};
            }
            bool present = false;
            const auto at = setPlace(S, (*made)->value, present);
            if (u == "SET_REMOVE" && present) {
                S.items.erase(S.items.begin() + static_cast<std::ptrdiff_t>(at));
                auto c = commitArg(u, *h);
                if (!c) return core::Err<core::Error>(c.error());
            }
            return RV{Value::boolean(present), nullptr};
        }
        // SET_UNION, SET_INTERSECTION, SET_DIFFERENCE (std::set_union...) : un SET neuf, du type de A. A et B :
        // des SET, ou toute suite de valeurs simples (une LIST, un RING, un ARRAY : rangee, sans doublon).
        const auto anySequence = [&](std::size_t k) -> core::Result<ObjArg> {
            auto s = objArg(arg(k));
            if (!s) return s;
            const TypeDesc* t = s->obj && !s->isScalar ? s->obj->type.get() : nullptr;
            if (t && (t->kind == TypeDesc::Kind::List || t->kind == TypeDesc::Kind::Array)) return s;
            return dialectError(u + " : un SET est attendu (ou une LIST, un ARRAY), pas " + kindName(*s));
        };
        auto A = anySequence(0);
        if (!A) return core::Err<core::Error>(A.error());
        auto B = anySequence(1);
        if (!B) return core::Err<core::Error>(B.error());
        const TypeRef elem = A->obj->type->element;
        if (!elem || elem->kind != TypeDesc::Kind::Scalar)
            return dialectError(u + " : un SET range des valeurs simples, pas des " + (elem ? elem->text() : std::string("valeurs sans type")));
        const auto asSet = [&](const Obj& from) -> core::Result<ObjRef> {
            auto out = makeObj(setType(elem));
            for (const auto& x : from.items) {
                auto m = fitTo(u, elem, valueOfItem(x));          // les valeurs de B dans le type de A
                if (!m) return core::Err<core::Error>(m.error());
                out->items.push_back(*m);
            }
            normalizeList(*out);
            return out;
        };
        auto sa = asSet(*A->obj);
        if (!sa) return core::Err<core::Error>(sa.error());
        auto sb = asSet(*B->obj);
        if (!sb) return core::Err<core::Error>(sb.error());
        const auto in = [](const Obj& set, const ObjRef& x) {
            bool present = false;
            (void)setPlace(set, x->value, present);
            return present;
        };
        // 1.12.16 : SET_INCLUDES (std::includes) - chaque valeur de B est dans A (un B vide : vrai).
        if (u == "SET_INCLUDES") {
            for (const auto& x : (*sb)->items)
                if (!in(**sa, x)) return RV{Value::boolean(false), nullptr};
            return RV{Value::boolean(true), nullptr};
        }
        auto out = makeObj(setType(elem));
        if (u == "SET_UNION") {
            out->items = (*sa)->items;
            for (const auto& x : (*sb)->items) out->items.push_back(x);
            normalizeList(*out);
        } else if (u == "SET_SYMMETRIC_DIFFERENCE") {      // 1.12.16 : les valeurs d'un seul des deux
            for (const auto& x : (*sa)->items)
                if (!in(**sb, x)) out->items.push_back(x);
            for (const auto& x : (*sb)->items)
                if (!in(**sa, x)) out->items.push_back(x);
            normalizeList(*out);
        } else {
            const bool keepCommon = u == "SET_INTERSECTION";
            for (const auto& x : (*sa)->items) {
                bool present = false;
                (void)setPlace(**sb, x->value, present);
                if (present == keepCommon) out->items.push_back(x);
            }
        }
        return RV{{}, out};
    }

    // ---- les listes, les vecteurs, les tableaux ------------------------------------------
    core::Result<RV> listNative(const std::string& u, const lib::NativeInfo& info, const Expr& e) {
        const auto argc = e.arguments.size();
        const auto arg = [&](std::size_t k) -> const Expr& { return *e.arguments[k].second; };
        const auto integer = [](std::int64_t v) { return core::Result<RV>(RV{Value::integer(Type::DInt, v), nullptr}); };
        const auto boolean = [](bool v) { return core::Result<RV>(RV{Value::boolean(v), nullptr}); };
        // LIST_COPY ecrit son second argument, pas le premier.
        if (u == "LIST_COPY") {
            auto src = seqArg(u, arg(0), true);
            if (!src) return core::Err<core::Error>(src.error());
            auto dst = seqArg(u, arg(1), true);
            if (!dst) return core::Err<core::Error>(dst.error());
            if (dst->constant) return constantRefused(u);
            if (isSetType(*dst->obj->type)) return setRefused(u);         // 1.12.7 : case par case, non
            const std::vector<ObjRef> from = src->obj->items;      // une copie des pointeurs : source et destination peuvent etre la meme
            const std::size_t n = std::min(from.size(), dst->obj->items.size());
            std::vector<ObjRef> made;
            for (std::size_t k = 0; k < n; ++k) {
                auto m = fitTo(u, dst->obj->type->element, valueOfItem(from[k]));
                if (!m) return core::Err<core::Error>(m.error());
                made.push_back(*m);
            }
            for (std::size_t k = 0; k < n; ++k) dst->obj->items[k] = made[k];
            auto c = commitArg(u, *dst);
            if (!c) return core::Err<core::Error>(c.error());
            return integer(static_cast<std::int64_t>(n));
        }
        auto L0 = seqArg(u, arg(0), info.arrayOk);
        if (!L0) return core::Err<core::Error>(L0.error());
        ObjArg La = *L0;
        Obj& L = *La.obj;
        if (info.writes && La.constant) return constantRefused(u);
        // 1.12.7 : un SET refuse ce qui ecrit a une place ; apres une ecriture, un SET ou un RING reprend sa forme.
        const bool flavored = L.type->kind == TypeDesc::Kind::List && (isSetType(*L.type) || isRingType(*L.type));
        if (isSetType(*L.type) && (u == "LIST_SET" || u == "LIST_FILL" || u == "LIST_INSERT_SORTED" || u == "LIST_SORT_BY" || u == "LIST_SWAP"
                                   || u == "LIST_MOVE" || u == "LIST_ROTATE"))
            return setRefused(u);
        struct Reshape {
            Obj& o;
            bool on;
            ~Reshape() { if (on) normalizeList(o); }
        } reshape{L, info.writes && flavored};
        const TypeRef elem = L.type->element;
        const auto n = static_cast<std::int64_t>(L.items.size());
        const auto commit = [&]() -> core::Result<RV> {
            auto c = commitArg(u, La);
            if (!c) return core::Err<core::Error>(c.error());
            return RV{};
        };
        const auto scalarsOnly = [&]() -> std::optional<core::Err<core::Error>> {
            for (const auto& i : L.items)
                if (!i || !i->type || i->type->kind != TypeDesc::Kind::Scalar)
                    return dialectError(u + " : une liste de valeurs simples est attendue, pas un " + L.type->text());
            if (n == 0 && elem && elem->kind != TypeDesc::Kind::Scalar)
                return dialectError(u + " : une liste de valeurs simples est attendue, pas un " + L.type->text());
            return std::nullopt;
        };
        // La comparaison et le membre des fonctions LIST_..._IF, LIST_ALL, LIST_ANY, LIST_NONE.
        struct Condition { int op{0}; RV value; int member{-1}; };
        const auto condition = [&]() -> core::Result<Condition> {
            Condition c;
            auto op = comparisonArg(u, arg(1));
            if (!op) return core::Err<core::Error>(op.error());
            c.op = *op;
            auto v = rich(arg(2));
            if (!v) return core::Err<core::Error>(v.error());
            c.value = *v;
            std::optional<RV> membre;
            if (argc > 3) {
                auto m = rich(arg(3));
                if (!m) return core::Err<core::Error>(m.error());
                membre = *m;
            }
            auto p = projection(u, L, membre ? &*membre : nullptr);
            if (!p) return core::Err<core::Error>(p.error());
            c.member = *p;
            return c;
        };
        const auto holds = [&](const Condition& c, const ObjRef& item) -> core::Result<bool> {
            auto r = compareItem(u, projected(item, c.member), c.value);
            if (!r) return core::Err<core::Error>(r.error());
            return compares(c.op, *r);
        };
        auto lowR = lowOf(u, L);
        // Les fonctions qui ne prennent pas d'indice lisent aussi un ARRAY a deux dimensions.
        const bool indexed = u == "LIST_SET" || u == "LIST_LAST_INDEX_OF" || u == "LIST_FIND_IF" || u == "LIST_MIN_INDEX" || u == "LIST_MAX_INDEX"
                          || u == "LIST_SWAP" || u == "LIST_MOVE" || u == "LIST_SLICE" || u == "LIST_LOWER_BOUND";     // 1.12.16
        if (!lowR && indexed) return core::Err<core::Error>(lowR.error());
        const std::int64_t low = lowR ? *lowR : 0;

        if (u == "LIST_SET") {
            auto i = intArg(u, arg(1), "un indice");
            if (!i) return core::Err<core::Error>(i.error());
            auto v = rich(arg(2));
            if (!v) return v;
            auto made = fitTo(u, elem, *v);
            if (!made) return core::Err<core::Error>(made.error());
            const std::int64_t at = *i - low;
            if (at < 0 || at >= n) return boolean(false);
            std::string why;
            if (!assignObj(*L.items[static_cast<std::size_t>(at)], **made, &why)) return dialectError(u + " : " + why);
            auto c = commit();
            if (!c) return c;
            return boolean(true);
        }
        if (u == "LIST_FILL") {
            auto v = rich(arg(1));
            if (!v) return v;
            auto made = fitTo(u, elem, *v);
            if (!made) return core::Err<core::Error>(made.error());
            for (auto& item : L.items) item = deepCopy(**made);
            auto c = commit();
            if (!c) return c;
            return integer(n);
        }
        if (u == "LIST_INSERT_SORTED") {
            if (auto bad = scalarsOnly()) return *bad;
            auto v = rich(arg(1));
            if (!v) return v;
            auto made = fitTo(u, elem, *v);
            if (!made) return core::Err<core::Error>(made.error());
            bool descending = false;
            if (argc > 2) {
                auto d = boolArg(u, arg(2));
                if (!d) return core::Err<core::Error>(d.error());
                descending = *d;
            }
            std::size_t i = 0;
            while (i < L.items.size()) {
                const int c = L.items[i]->value.compare((*made)->value);
                if (descending ? c < 0 : c > 0) break;
                ++i;
            }
            L.items.insert(L.items.begin() + static_cast<std::ptrdiff_t>(i), *made);
            const auto gone = static_cast<std::int64_t>(normalizeList(L));        // 1.12.7 : un RING plein
            return integer(static_cast<std::int64_t>(i) < gone ? -1 : static_cast<std::int64_t>(i) - gone);
        }
        if (u == "LIST_POP_FIRST") {
            if (n == 0) return dialectError(u + " : la liste est vide");
            const ObjRef first = L.items.front();
            L.items.erase(L.items.begin());
            return valueOfItem(first);
        }
        if (u == "LIST_REMOVE_ALL" || u == "LIST_LAST_INDEX_OF" || u == "LIST_COUNT_OF") {
            auto given = rich(arg(1));
            if (!given) return given;
            auto fitted = fitTo(u, elem, *given);            // 'abc' dans une liste de REAL : refuse
            if (!fitted) return core::Err<core::Error>(fitted.error());
            const core::Result<RV> v = valueOfItem(*fitted);
            if (u == "LIST_REMOVE_ALL") {
                const auto before = L.items.size();
                L.items.erase(std::remove_if(L.items.begin(), L.items.end(), [&](const ObjRef& x) { return sameItem(x, *v); }), L.items.end());
                return integer(static_cast<std::int64_t>(before - L.items.size()));
            }
            std::int64_t count = 0, last = -1;
            for (std::int64_t k = 0; k < n; ++k)
                if (sameItem(L.items[static_cast<std::size_t>(k)], *v)) {
                    ++count;
                    last = k;
                }
            if (u == "LIST_COUNT_OF") return integer(count);
            return integer(last < 0 ? -1 : last + low);
        }
        if (u == "LIST_REMOVE_IF" || u == "LIST_FIND_IF" || u == "LIST_COUNT_IF" || u == "LIST_ALL" || u == "LIST_ANY" || u == "LIST_NONE") {
            auto c = condition();
            if (!c) return core::Err<core::Error>(c.error());
            if (u == "LIST_REMOVE_IF") {
                std::vector<ObjRef> kept;
                for (const auto& item : L.items) {
                    auto h = holds(*c, item);
                    if (!h) return core::Err<core::Error>(h.error());
                    if (!*h) kept.push_back(item);
                }
                const auto removed = static_cast<std::int64_t>(L.items.size() - kept.size());
                L.items = std::move(kept);
                return integer(removed);
            }
            std::int64_t count = 0, first = -1;
            for (std::int64_t k = 0; k < n; ++k) {
                auto h = holds(*c, L.items[static_cast<std::size_t>(k)]);
                if (!h) return core::Err<core::Error>(h.error());
                if (!*h) continue;
                ++count;
                if (first < 0) first = k;
                if (u == "LIST_FIND_IF" || u == "LIST_ANY" || u == "LIST_NONE") break;
            }
            if (u == "LIST_FIND_IF") return integer(first < 0 ? -1 : first + low);
            if (u == "LIST_COUNT_IF") return integer(count);
            if (u == "LIST_ANY") return boolean(count > 0);
            if (u == "LIST_NONE") return boolean(count == 0);
            return boolean(count == n);                                   // LIST_ALL
        }
        if (u == "LIST_DISTINCT") {
            std::vector<ObjRef> kept;
            for (const auto& item : L.items) {
                bool seen = false;
                for (const auto& k : kept) seen = seen || deepEquals(*k, *item);
                if (!seen) kept.push_back(item);
            }
            const auto removed = static_cast<std::int64_t>(L.items.size() - kept.size());
            L.items = std::move(kept);
            return integer(removed);
        }
        if (u == "LIST_APPEND") {
            auto other = seqArg(u, arg(1), true);
            if (!other) return core::Err<core::Error>(other.error());
            const std::vector<ObjRef> from = other->obj->items;
            std::vector<ObjRef> made;
            for (const auto& x : from) {
                auto m = fitTo(u, elem, valueOfItem(x));
                if (!m) return core::Err<core::Error>(m.error());
                made.push_back(*m);
            }
            for (auto& m : made) L.items.push_back(m);
            normalizeList(L);                        // 1.12.7 : un SET (l'union), un RING (les N dernieres)
            return integer(static_cast<std::int64_t>(L.items.size()));
        }
        if (u == "LIST_MIN_INDEX" || u == "LIST_MAX_INDEX") {
            std::optional<RV> membre;
            if (argc > 1) {
                auto m = rich(arg(1));
                if (!m) return m;
                membre = *m;
            }
            auto p = projection(u, L, membre ? &*membre : nullptr);
            if (!p) return core::Err<core::Error>(p.error());
            std::int64_t best = -1;
            for (std::int64_t k = 0; k < n; ++k) {
                const ObjRef& x = projected(L.items[static_cast<std::size_t>(k)], *p);
                if (!x || !x->type || x->type->kind != TypeDesc::Kind::Scalar)
                    return dialectError(u + " : des valeurs simples sont attendues (MEMBRE : lequel comparer)");
                if (best < 0) { best = k; continue; }
                const int c = x->value.compare(projected(L.items[static_cast<std::size_t>(best)], *p)->value);
                if (u == "LIST_MIN_INDEX" ? c < 0 : c > 0) best = k;
            }
            return integer(best < 0 ? -1 : best + low);
        }
        if (u == "LIST_SORT_BY") {
            auto m = rich(arg(1));
            if (!m) return m;
            auto p = projection(u, L, &*m);
            if (!p) return core::Err<core::Error>(p.error());
            bool descending = false;
            if (argc > 2) {
                auto d = boolArg(u, arg(2));
                if (!d) return core::Err<core::Error>(d.error());
                descending = *d;
            }
            const int member = *p;
            for (const auto& item : L.items) {
                const ObjRef& x = projected(item, member);
                if (!x || !x->type || x->type->kind != TypeDesc::Kind::Scalar)
                    return dialectError(u + " : le membre tri\xC3\xA9 est une valeur simple (un nombre, un texte, une dur\xC3\xA9" "e)");
            }
            std::stable_sort(L.items.begin(), L.items.end(), [&](const ObjRef& a, const ObjRef& b) {
                const int c = projected(a, member)->value.compare(projected(b, member)->value);
                return descending ? c > 0 : c < 0;
            });
            auto c = commit();
            if (!c) return c;
            return boolean(true);
        }
        if (u == "LIST_IS_SORTED") {
            if (auto bad = scalarsOnly()) return *bad;
            bool descending = false;
            if (argc > 1) {
                auto d = boolArg(u, arg(1));
                if (!d) return core::Err<core::Error>(d.error());
                descending = *d;
            }
            for (std::size_t k = 1; k < L.items.size(); ++k) {
                const int c = L.items[k]->value.compare(L.items[k - 1]->value);
                if (descending ? c > 0 : c < 0) return boolean(false);
            }
            return boolean(true);
        }
        if (u == "LIST_SWAP" || u == "LIST_MOVE") {
            auto i = intArg(u, arg(1), "un indice");
            if (!i) return core::Err<core::Error>(i.error());
            auto j = intArg(u, arg(2), "un indice");
            if (!j) return core::Err<core::Error>(j.error());
            const std::int64_t a = *i - low, b = *j - low;
            if (a < 0 || b < 0 || a >= n || b >= n) return boolean(false);
            if (u == "LIST_SWAP") {
                std::swap(L.items[static_cast<std::size_t>(a)], L.items[static_cast<std::size_t>(b)]);
            } else {
                const ObjRef x = L.items[static_cast<std::size_t>(a)];
                L.items.erase(L.items.begin() + static_cast<std::ptrdiff_t>(a));
                L.items.insert(L.items.begin() + static_cast<std::ptrdiff_t>(b), x);
            }
            auto c = commit();
            if (!c) return c;
            return boolean(true);
        }
        if (u == "LIST_ROTATE") {
            auto k = intArg(u, arg(1), "le nombre de places");
            if (!k) return core::Err<core::Error>(k.error());
            if (n > 0) {
                const std::int64_t s = ((*k % n) + n) % n;
                std::rotate(L.items.begin(), L.items.begin() + static_cast<std::ptrdiff_t>(s), L.items.end());
                auto c = commit();
                if (!c) return c;
            }
            return boolean(true);
        }
        if (u == "LIST_SLICE" || u == "LIST_CONCAT" || u == "TO_LIST") {
            auto out = makeObj(listType(elem));
            if (u == "LIST_SLICE") {
                auto d = intArg(u, arg(1), "le d\xC3\xA9" "but");
                if (!d) return core::Err<core::Error>(d.error());
                auto count = intArg(u, arg(2), "le nombre");
                if (!count) return core::Err<core::Error>(count.error());
                const std::int64_t from = std::max<std::int64_t>(0, *d - low);
                const std::int64_t to = *count <= 0 ? from : std::min<std::int64_t>(n, std::max<std::int64_t>(0, *d - low + *count));
                for (std::int64_t k = from; k < to; ++k) out->items.push_back(deepCopy(*L.items[static_cast<std::size_t>(k)]));
                return RV{{}, out};
            }
            for (const auto& x : L.items) out->items.push_back(deepCopy(*x));
            if (u == "LIST_CONCAT") {
                auto b = seqArg(u, arg(1), true);
                if (!b) return core::Err<core::Error>(b.error());
                const TypeRef eb = b->obj->type->element;
                if (elem && eb && upperOf(elem->text()) != upperOf(eb->text()))
                    return dialectError(u + " : des " + eb->text() + " ne vont pas apr\xC3\xA8s des " + elem->text());
                for (const auto& x : b->obj->items) out->items.push_back(deepCopy(*x));
            }
            return RV{{}, out};
        }
        // ---- 1.12.16 : chercher dans une liste triee (std::lower_bound, std::binary_search) ----
        if (u == "LIST_LOWER_BOUND" || u == "LIST_BINARY_SEARCH") {
            if (auto bad = scalarsOnly()) return *bad;
            auto given = rich(arg(1));
            if (!given) return given;
            auto fitted = fitTo(u, elem, *given);            // 'abc' dans une liste de REAL : refuse
            if (!fitted) return core::Err<core::Error>(fitted.error());
            const Value& v = (*fitted)->value;
            // La premiere place dont l'element n'est pas plus petit que v (par moities).
            const auto at = std::lower_bound(L.items.begin(), L.items.end(), v, [](const ObjRef& x, const Value& y) { return x->value.compare(y) < 0; });
            if (u == "LIST_BINARY_SEARCH") return boolean(at != L.items.end() && (*at)->value.compare(v) == 0);
            return integer(static_cast<std::int64_t>(at - L.items.begin()) + low);     // aucun : L.Count (la fin)
        }
        // ---- 1.12.16 : les statistiques (la mediane, la variance, l'ecart type) ----
        if (u == "LIST_MEDIAN" || u == "LIST_VARIANCE" || u == "LIST_STDDEV") {
            std::vector<double> xs;
            for (const auto& i : L.items) {
                if (!i || !i->type || i->type->kind != TypeDesc::Kind::Scalar || !isNumeric(i->value.type()) || i->value.type() == Type::Time)
                    return dialectError(u + " : des nombres sont attendus, pas un " + L.type->text());
                xs.push_back(i->value.asReal());
            }
            bool sample = false;
            if (argc > 1) {
                auto d = boolArg(u, arg(1));
                if (!d) return core::Err<core::Error>(d.error());
                sample = *d;
            }
            if (xs.empty()) return dialectError(u + " : la liste est vide");
            const auto real = [](double r) { return core::Result<RV>(RV{Value::real(r), nullptr}); };
            if (u == "LIST_MEDIAN") {
                std::sort(xs.begin(), xs.end());
                const std::size_t mid = xs.size() / 2;
                return real(xs.size() % 2 ? xs[mid] : (xs[mid - 1] + xs[mid]) / 2.0);
            }
            if (sample && xs.size() < 2)
                return dialectError(u + " : un \xC3\xA9" "chantillon d'une seule valeur n'a pas de variance (n - 1 = 0)");
            // En deux passes : la moyenne, puis les carres des ecarts (plus juste qu'une seule somme des carres).
            double mean = 0.0;
            for (const double x : xs) mean += x;
            mean /= static_cast<double>(xs.size());
            double squares = 0.0;
            for (const double x : xs) squares += (x - mean) * (x - mean);
            const double variance = squares / static_cast<double>(sample ? xs.size() - 1 : xs.size());
            return real(u == "LIST_VARIANCE" ? variance : std::sqrt(variance));
        }
        // ---- 1.12.16 : LIST_TRANSFORM (std::ranges::transform) - une TRANSFORMATION ou une fonction du projet ----
        if (u == "LIST_TRANSFORM") return transformList(u, La, arg(1), arg(2), e.line);
        return dialectError(u + " : pas encore prise en charge");
    }

    // ---- les MAP --------------------------------------------------------------------------
    core::Result<RV> mapNative(const std::string& u, const Expr& e) {
        const auto argc = e.arguments.size();
        const auto arg = [&](std::size_t k) -> const Expr& { return *e.arguments[k].second; };
        const auto* info = lib::native(u);
        auto M0 = mapArg(u, arg(0));
        if (!M0) return core::Err<core::Error>(M0.error());
        ObjArg Ma = *M0;
        Obj& m = *Ma.obj;
        if (info && info->writes && Ma.constant) return constantRefused(u);
        const bool textual = !isInteger(m.type->key);
        const Type keyType = textual ? Type::String : m.type->key;
        const auto keyValue = [&](bool isText, const std::string& t, std::int64_t i) {
            return RV{isText ? Value::text(t) : Value::integer(keyType, i), nullptr};
        };
        const auto keyOf = [&](std::size_t k, std::string& text, std::int64_t& integer) -> core::Result<bool> {
            auto key = rich(arg(k));
            if (!key) return core::Err<core::Error>(key.error());
            auto isText = mapKey(m, *key, text, integer);
            if (!isText) return core::Err<core::Error>(core::fail(core::ErrorCode::InvalidArgument, u + " : " + isText.error().message()));
            return *isText;
        };
        const auto find = [&](bool isText, const std::string& text, std::int64_t integer) -> ObjRef {
            if (isText) {
                const auto it = m.textKeys.find(text);
                return it == m.textKeys.end() ? nullptr : it->second;
            }
            const auto it = m.intKeys.find(integer);
            return it == m.intKeys.end() ? nullptr : it->second;
        };
        const auto put = [&](bool isText, const std::string& text, std::int64_t integer, const ObjRef& v) {
            if (isText) m.textKeys[text] = v;
            else m.intKeys[integer] = v;
        };
        const std::size_t size = m.textKeys.size() + m.intKeys.size();
        // Les cles, dans l'ordre (une MAP n'a que des cles textes ou que des cles entieres).
        std::vector<std::pair<RV, ObjRef>> entries;
        for (const auto& [k, v] : m.textKeys) entries.push_back({keyValue(true, k, 0), v});
        for (const auto& [k, v] : m.intKeys) entries.push_back({keyValue(false, {}, k), v});

        if (u == "MAP_SET" || u == "MAP_INSERT") {
            std::string text;
            std::int64_t integer = 0;
            auto isText = keyOf(1, text, integer);
            if (!isText) return core::Err<core::Error>(isText.error());
            auto v = rich(arg(2));
            if (!v) return v;
            auto made = fitTo(u, m.type->element, *v);
            if (!made) return core::Err<core::Error>(made.error());
            const ObjRef old = find(*isText, text, integer);
            if (u == "MAP_INSERT" && old) return RV{Value::boolean(false), nullptr};
            if (old) {
                std::string why;
                if (!assignObj(*old, **made, &why)) return dialectError(u + " : " + why);
            } else {
                put(*isText, text, integer, *made);
            }
            return RV{Value::boolean(old == nullptr), nullptr};
        }
        if (u == "MAP_INCREMENT") {
            const TypeRef& vt = m.type->element;
            if (!vt || vt->kind != TypeDesc::Kind::Scalar || !isNumeric(vt->scalar) || vt->scalar == Type::Bool || vt->scalar == Type::Time)
                return dialectError(u + " : une MAP de nombres est attendue, pas un " + m.type->text());
            std::string text;
            std::int64_t integer = 0;
            auto isText = keyOf(1, text, integer);
            if (!isText) return core::Err<core::Error>(isText.error());
            RV step{Value::integer(Type::DInt, 1), nullptr};
            if (argc > 2) {
                auto x = rich(arg(2));
                if (!x) return x;
                auto fitted = fitTo(u, vt, *x);
                if (!fitted) return core::Err<core::Error>(fitted.error());
                step = RV{(*fitted)->value, nullptr};
            }
            ObjRef entry = find(*isText, text, integer);
            if (!entry) {
                auto made = instantiate(vt);
                if (!made) return core::Err<core::Error>(made.error());
                entry = *made;
                put(*isText, text, integer, entry);
            }
            Expr plus;
            plus.kind = Expr::Kind::Binary;
            plus.op = "+";
            plus.line = e.line;
            auto sum = combine(plus, entry->value, step.v);
            if (!sum) return core::Err<core::Error>(sum.error());
            entry->value.assignFrom(*sum);
            return RV{entry->value, nullptr};
        }
        if (u == "MAP_FIRST_KEY" || u == "MAP_LAST_KEY") {
            if (size == 0) {
                if (argc > 1) return rich(arg(1));
                return dialectError(u + " : la MAP est vide");
            }
            return u == "MAP_FIRST_KEY" ? entries.front().first : entries.back().first;
        }
        if (u == "MAP_FLOOR_KEY" || u == "MAP_CEIL_KEY") {
            std::string text;
            std::int64_t integer = 0;
            auto isText = keyOf(1, text, integer);
            if (!isText) return core::Err<core::Error>(isText.error());
            if (*isText) {
                if (u == "MAP_FLOOR_KEY") {
                    auto it = m.textKeys.upper_bound(text);
                    if (it != m.textKeys.begin()) return keyValue(true, std::prev(it)->first, 0);
                } else if (auto it = m.textKeys.lower_bound(text); it != m.textKeys.end()) {
                    return keyValue(true, it->first, 0);
                }
            } else if (u == "MAP_FLOOR_KEY") {
                auto it = m.intKeys.upper_bound(integer);
                if (it != m.intKeys.begin()) return keyValue(false, {}, std::prev(it)->first);
            } else if (auto it = m.intKeys.lower_bound(integer); it != m.intKeys.end()) {
                return keyValue(false, {}, it->first);
            }
            return rich(arg(2));
        }
        if (u == "MAP_KEY_OF") {
            auto given = rich(arg(1));
            if (!given) return given;
            auto fitted = fitTo(u, m.type->element, *given);
            if (!fitted) return core::Err<core::Error>(fitted.error());
            const core::Result<RV> v = valueOfItem(*fitted);
            for (const auto& [k, x] : entries)
                if (sameItem(x, *v)) return k;
            return rich(arg(2));
        }
        if (u == "MAP_REMOVE_IF") {
            auto op = comparisonArg(u, arg(1));
            if (!op) return core::Err<core::Error>(op.error());
            auto given = rich(arg(2));
            if (!given) return given;
            auto fitted = fitTo(u, m.type->element, *given);
            if (!fitted) return core::Err<core::Error>(fitted.error());
            const core::Result<RV> v = valueOfItem(*fitted);
            std::int64_t removed = 0;
            for (const auto& [k, x] : entries) {
                auto c = compareItem(u, x, *v);
                if (!c) return core::Err<core::Error>(c.error());
                if (!compares(*op, *c)) continue;
                if (textual) m.textKeys.erase(k.v.asString());
                else m.intKeys.erase(k.v.asInteger());
                ++removed;
            }
            return RV{Value::integer(Type::DInt, removed), nullptr};
        }
        if (u == "MAP_MERGE") {
            auto other = objArg(arg(1));
            if (!other) return core::Err<core::Error>(other.error());
            if (!other->obj || other->isScalar || !other->obj->type || other->obj->type->kind != TypeDesc::Kind::Map)
                return dialectError(u + " : une MAP est attendue, pas " + kindName(*other));
            ObjRef src = other->obj;
            if (src->type->literal) {                       // ['a' := 1] : dans le type de m
                src = makeObj(m.type);
                std::string why;
                if (!store(*src, RV{{}, other->obj}, &why)) return dialectError(u + " : " + why);
            } else if (!sameType(*src->type, *m.type)) {
                return dialectError(u + " : " + src->type->text() + " n'est pas un " + m.type->text());
            }
            bool replace = false;
            if (argc > 2) {
                auto r = boolArg(u, arg(2));
                if (!r) return core::Err<core::Error>(r.error());
                replace = *r;
            }
            std::int64_t added = 0;
            const auto mergeOne = [&](bool isText, const std::string& t, std::int64_t i, const ObjRef& v) -> core::Result<bool> {
                const ObjRef old = find(isText, t, i);
                if (!old) {
                    put(isText, t, i, deepCopy(*v));
                    ++added;
                } else if (replace) {
                    std::string why;
                    if (!assignObj(*old, *v, &why)) return dialectError(u + " : " + why);
                }
                return true;
            };
            const auto texts = src->textKeys;
            const auto ints = src->intKeys;
            for (const auto& [k, v] : texts) {
                auto r = mergeOne(true, k, 0, v);
                if (!r) return core::Err<core::Error>(r.error());
            }
            for (const auto& [k, v] : ints) {
                auto r = mergeOne(false, {}, k, v);
                if (!r) return core::Err<core::Error>(r.error());
            }
            return RV{Value::integer(Type::DInt, added), nullptr};
        }
        if (u == "MAP_VALUES") {
            auto out = makeObj(listType(m.type->element));
            for (const auto& [k, v] : entries) out->items.push_back(deepCopy(*v));
            return RV{{}, out};
        }
        if (u == "MAP_ITEMS") {
            const TypeRef pair = tupleType({scalarType(keyType), m.type->element});
            auto out = makeObj(listType(pair));
            for (const auto& [k, v] : entries) {
                auto t = makeObj(pair);
                if (t->items.size() == 2) {
                    t->items[0]->value.assignFrom(k.v);
                    t->items[1] = deepCopy(*v);
                }
                out->items.push_back(t);
            }
            return RV{{}, out};
        }
        return dialectError(u + " : pas encore prise en charge");
    }

    // ---- les TUPLE ------------------------------------------------------------------------
    core::Result<RV> tupleNative(const std::string& u, const Expr& e) {
        const auto arg = [&](std::size_t k) -> const Expr& { return *e.arguments[k].second; };
        auto T0 = tupleArg(u, arg(0));
        if (!T0) return core::Err<core::Error>(T0.error());
        ObjArg Ta = *T0;
        Obj& t = *Ta.obj;
        const auto fields = static_cast<std::int64_t>(t.items.size());
        if (u == "TUPLE_SIZE") return RV{Value::integer(Type::DInt, fields), nullptr};
        if (u == "TUPLE_TO_STRING") {
            std::string text;
            if (!literalText(t, text)) text = display(t);
            return RV{Value::text(text), nullptr};
        }
        if (u == "TUPLE_GET" || u == "TUPLE_SET") {
            auto k = intArg(u, arg(1), "le num\xC3\xA9ro");
            if (!k) return core::Err<core::Error>(k.error());
            if (*k < 1 || *k > fields)
                return dialectError(u + " : " + t.type->text() + " a " + std::to_string(fields) + " champs, pas " + std::to_string(*k));
            const ObjRef& field = t.items[static_cast<std::size_t>(*k - 1)];
            if (u == "TUPLE_GET") return valueOfItem(field);
            if (Ta.constant) return constantRefused(u);
            auto v = rich(arg(2));
            if (!v) return v;
            auto made = fitTo(u, t.type->members[static_cast<std::size_t>(*k - 1)].second, *v);
            if (!made) return core::Err<core::Error>(made.error());
            std::string why;
            if (!assignObj(*field, **made, &why)) return dialectError(u + " : " + why);
            auto c = commitArg(u, Ta);
            if (!c) return core::Err<core::Error>(c.error());
            return RV{Value::boolean(true), nullptr};
        }
        if (u == "TUPLE_UNPACK") {
            const auto given = static_cast<std::int64_t>(e.arguments.size()) - 1;
            if (given != fields)
                return dialectError(u + " : " + t.type->text() + " a " + std::to_string(fields) + " champs, " + std::to_string(given)
                                    + (given > 1 ? " variables donn\xC3\xA9" "es" : " variable donn\xC3\xA9" "e"));
            std::vector<Place> places;
            for (std::int64_t k = 0; k < fields; ++k) {
                const Expr& a = arg(static_cast<std::size_t>(k + 1));
                if (!isDesignator(a)) return dialectError(u + " : une variable est attendue en " + std::to_string(k + 2) + "e argument");
                auto p = locate(a, true);
                if (!p) return core::Err<core::Error>(p.error());
                if (p->temp) return dialectError(u + " : une variable est attendue en " + std::to_string(k + 2) + "e argument");
                if (p->constant) return constantRefused(u);
                // Le type de la variable et celui du champ : le meme.
                const TypeRef& ft = t.type->members[static_cast<std::size_t>(k)].second;
                std::string vt;
                if (p->obj && p->obj->type) vt = p->obj->type->kind == TypeDesc::Kind::Scalar ? scalarTypeName(*p->obj->type) : p->obj->type->text();
                else vt = upperOf(env_.declaredType(p->name));
                if (vt.empty()) {
                    Value probe;
                    if (env_.read(p->name, probe)) vt = valueTypeText(probe.type());
                }
                const std::string fname = ft ? (ft->kind == TypeDesc::Kind::Scalar ? scalarTypeName(*ft) : ft->text()) : std::string("?");
                const auto base = [](const std::string& s) {
                    const Type ty = typeFromName(s);
                    if (ty == Type::String) return std::string("STRING");
                    if (s == "LREAL") return std::string("REAL");
                    return s;
                };
                if (!vt.empty() && base(upperOf(vt)) != base(upperOf(fname)))
                    return dialectError(u + " : le champ " + std::to_string(k + 1) + " est un " + fname + ", " + exprText(a) + " un " + vt);
                places.push_back(*p);
            }
            for (std::int64_t k = 0; k < fields; ++k) {
                std::string why;
                if (!writePlace(places[static_cast<std::size_t>(k)], valueOfItem(t.items[static_cast<std::size_t>(k)]), &why))
                    return dialectError(u + " : " + why);
            }
            return RV{Value::boolean(true), nullptr};
        }
        return dialectError(u + " : pas encore prise en charge");
    }

    // ---- std::numeric ---------------------------------------------------------------------
    // OPERATION#... : son rang (0 Somme ... 5 Ou) ; TRANSFORMATION#... (0 Identite ... 5 Inverse).
    core::Result<int> enumArg(const std::string& u, const Expr& a, const char* enumName) {
        const std::string want = u + " : " + (std::string(enumName) == "OPERATION" ? "une OPERATION est attendue (OPERATION#Somme\xE2\x80\xA6)"
                                                                                   : "une TRANSFORMATION est attendue (TRANSFORMATION#Carre\xE2\x80\xA6)")
                               + ", pas ";
        if (a.kind == Expr::Kind::Reference && a.name.find('#') != std::string::npos && upperOf(a.name.substr(0, a.name.find('#'))) != enumName)
            return dialectError(want + a.name);
        auto v = rich(a);
        if (!v) return core::Err<core::Error>(v.error());
        if (v->o || !isInteger(v->v.type()) || v->v.type() == Type::Bool || v->v.asInteger() < 0 || v->v.asInteger() > 5)
            return dialectError(want + (v->o ? typeNameOf(*v) : v->v.display()));
        return static_cast<int>(v->v.asInteger());
    }
    core::Result<Value> applyOp(int op, const Value& a, const Value& b, std::uint32_t line) {
        Expr x;
        x.kind = Expr::Kind::Binary;
        x.line = line;
        switch (op) {
            case 1: x.op = "*"; break;
            case 2: return a.compare(b) <= 0 ? a : b;
            case 3: return a.compare(b) >= 0 ? a : b;
            case 4: x.op = "AND"; break;
            case 5: x.op = "OR"; break;
            default: x.op = "+"; break;
        }
        if ((op == 4 || op == 5) && (a.type() == Type::Real || b.type() == Type::Real))
            return dialectError("OPERATION#" + std::string(op == 4 ? "Et" : "Ou") + " : des entiers ou des BOOL sont attendus, pas des r\xC3\xA9" "els");
        return combine(x, a, b);
    }
    static Value transform(int tr, const Value& x) {
        switch (tr) {
            case 1: return x.type() == Type::Real ? Value::real(std::fabs(x.asReal())) : Value::integer(x.type(), x.asInteger() < 0 ? -x.asInteger() : x.asInteger());
            case 2: return x.type() == Type::Real ? Value::real(x.asReal() * x.asReal()) : Value::integer(x.type(), x.asInteger() * x.asInteger());
            case 3: return Value::real(std::sqrt(x.asReal()));
            case 4: return x.type() == Type::Real ? Value::real(-x.asReal()) : Value::integer(x.type(), -x.asInteger());
            case 5: return Value::real(1.0 / x.asReal());
            default: return x;
        }
    }
    // La valeur dans le type d'une autre (ACCUMULATE plie dans le type de init, comme le C++).
    static Value asTypeOf(const Value& like, const Value& v) {
        Value out = Value::defaultOf(like.type());
        out.assignFrom(v);
        return out;
    }
    core::Result<RV> numericNative(const std::string& u, const std::string& tmpl, const Expr& e) {
        const auto argc = e.arguments.size();
        const auto arg = [&](std::size_t k) -> const Expr& { return *e.arguments[k].second; };
        const auto value = [&](std::size_t k) -> core::Result<Value> {
            auto v = rich(arg(k));
            if (!v) return core::Err<core::Error>(v.error());
            if (v->o) return dialectError(u + " : un nombre est attendu, pas un " + typeNameOf(*v));
            if (!isNumeric(v->v.type()) || v->v.type() == Type::Time) return dialectError(u + " : un nombre est attendu, pas " + v->v.display());
            return v->v;
        };
        // Les nombres d'une suite (LIST, VECTOR, ARRAY).
        const auto numbers = [&](std::size_t k, ObjArg* keep) -> core::Result<std::vector<Value>> {
            auto s = seqArg(u, arg(k), true);
            if (!s) return core::Err<core::Error>(s.error());
            std::vector<Value> out;
            for (const auto& i : s->obj->items) {
                if (!i || !i->type || i->type->kind != TypeDesc::Kind::Scalar || !isNumeric(i->value.type()) || i->value.type() == Type::Time)
                    return dialectError(u + " : des nombres sont attendus, pas un " + s->obj->type->text());
                out.push_back(i->value);
            }
            if (keep) *keep = *s;
            return out;
        };
        const auto opAt = [&](std::size_t k) -> core::Result<int> { return k < argc ? enumArg(u, arg(k), "OPERATION") : core::Result<int>(0); };
        // Une suite rendue : du type de T (une LIST rend une LIST, un ARRAY un ARRAY aux memes bornes).
        const auto sequenceOf = [&](const ObjArg& like, const std::vector<Value>& vals) -> RV {
            // 1.12.7 : d'un SET, une LIST (les sommes cumulees ne sont pas rangees, ni uniques).
            auto out = makeObj(isSetType(*like.obj->type) ? listType(like.obj->type->element) : like.obj->type);
            for (std::size_t k = 0; k < out->items.size() && k < vals.size(); ++k) out->items[k]->value.assignFrom(vals[k]);
            if (like.obj->type->kind == TypeDesc::Kind::List) {
                out->items.clear();
                for (const auto& v : vals) {
                    auto item = makeObj(like.obj->type->element);
                    item->value.assignFrom(v);
                    out->items.push_back(item);
                }
            }
            return RV{{}, out};
        };
        if (u == "ACCUMULATE" || u == "REDUCE" || u == "TRANSFORM_REDUCE") {
            ObjArg seq;
            auto xs = numbers(0, &seq);
            if (!xs) return core::Err<core::Error>(xs.error());
            Value init;
            if (argc > 1) {
                auto i = value(1);
                if (!i) return core::Err<core::Error>(i.error());
                init = *i;
            } else {
                const TypeRef& el = seq.obj->type->element;
                init = el && el->kind == TypeDesc::Kind::Scalar && el->scalar == Type::Real ? Value::real(0.0) : Value::integer(Type::DInt, 0);
            }
            auto op = opAt(2);
            if (!op) return core::Err<core::Error>(op.error());
            int tr = 0;
            if (u == "TRANSFORM_REDUCE") {
                auto t = enumArg(u, arg(3), "TRANSFORMATION");
                if (!t) return core::Err<core::Error>(t.error());
                tr = *t;
            }
            Value acc = init;
            for (const auto& x : *xs) {
                auto r = applyOp(*op, acc, transform(tr, x), e.line);
                if (!r) return core::Err<core::Error>(r.error());
                acc = asTypeOf(init, *r);
            }
            return RV{acc, nullptr};
        }
        if (u == "INNER_PRODUCT") {
            auto a = numbers(0, nullptr);
            if (!a) return core::Err<core::Error>(a.error());
            auto b = numbers(1, nullptr);
            if (!b) return core::Err<core::Error>(b.error());
            auto init = value(2);
            if (!init) return core::Err<core::Error>(init.error());
            Value acc = *init;
            for (std::size_t k = 0; k < a->size(); ++k) {
                if (k >= b->size()) return dialectError(u + " : B est plus court que A : B[" + std::to_string(k) + "] manque");
                auto p = applyOp(1, (*a)[k], (*b)[k], e.line);
                if (!p) return core::Err<core::Error>(p.error());
                auto s = applyOp(0, acc, *p, e.line);
                if (!s) return core::Err<core::Error>(s.error());
                acc = asTypeOf(*init, *s);
            }
            return RV{acc, nullptr};
        }
        if (u == "PARTIAL_SUM" || u == "INCLUSIVE_SCAN" || u == "TRANSFORM_INCLUSIVE_SCAN") {
            ObjArg seq;
            auto xs = numbers(0, &seq);
            if (!xs) return core::Err<core::Error>(xs.error());
            auto op = opAt(1);
            if (!op) return core::Err<core::Error>(op.error());
            int tr = 0;
            if (u == "TRANSFORM_INCLUSIVE_SCAN") {
                auto t = enumArg(u, arg(2), "TRANSFORMATION");
                if (!t) return core::Err<core::Error>(t.error());
                tr = *t;
            }
            std::vector<Value> out;
            for (std::size_t k = 0; k < xs->size(); ++k) {
                const Value x = transform(tr, (*xs)[k]);
                if (k == 0) {
                    out.push_back(x);
                    continue;
                }
                auto r = applyOp(*op, out.back(), x, e.line);
                if (!r) return core::Err<core::Error>(r.error());
                out.push_back(*r);
            }
            return sequenceOf(seq, out);
        }
        if (u == "EXCLUSIVE_SCAN" || u == "TRANSFORM_EXCLUSIVE_SCAN") {
            ObjArg seq;
            auto xs = numbers(0, &seq);
            if (!xs) return core::Err<core::Error>(xs.error());
            auto init = value(1);
            if (!init) return core::Err<core::Error>(init.error());
            auto op = opAt(2);
            if (!op) return core::Err<core::Error>(op.error());
            int tr = 0;
            if (u == "TRANSFORM_EXCLUSIVE_SCAN") {
                auto t = enumArg(u, arg(3), "TRANSFORMATION");
                if (!t) return core::Err<core::Error>(t.error());
                tr = *t;
            }
            std::vector<Value> out;
            Value acc = *init;
            for (const auto& x : *xs) {
                out.push_back(acc);
                auto r = applyOp(*op, acc, transform(tr, x), e.line);
                if (!r) return core::Err<core::Error>(r.error());
                acc = *r;
            }
            return sequenceOf(seq, out);
        }
        if (u == "ADJACENT_DIFFERENCE") {
            ObjArg seq;
            auto xs = numbers(0, &seq);
            if (!xs) return core::Err<core::Error>(xs.error());
            std::vector<Value> out;
            Expr minus;
            minus.kind = Expr::Kind::Binary;
            minus.op = "-";
            minus.line = e.line;
            for (std::size_t k = 0; k < xs->size(); ++k) {
                if (k == 0) {
                    out.push_back((*xs)[0]);
                    continue;
                }
                auto d = combine(minus, (*xs)[k], (*xs)[k - 1]);
                if (!d) return core::Err<core::Error>(d.error());
                out.push_back(*d);
            }
            return sequenceOf(seq, out);
        }
        if (u == "IOTA") {
            auto s = seqArg(u, arg(0), true);
            if (!s) return core::Err<core::Error>(s.error());
            if (s->constant) return constantRefused(u);
            if (isSetType(*s->obj->type)) return setRefused(u);           // 1.12.7
            const TypeRef& el = s->obj->type->element;
            if (!el || el->kind != TypeDesc::Kind::Scalar || !isNumeric(el->scalar) || el->scalar == Type::Time)
                return dialectError(u + " : des nombres sont attendus, pas un " + s->obj->type->text());
            auto start = value(1);
            if (!start) return core::Err<core::Error>(start.error());
            Value step = Value::integer(Type::DInt, 1);
            if (argc > 2) {
                auto p = value(2);
                if (!p) return core::Err<core::Error>(p.error());
                step = *p;
            }
            Expr times, plus;
            times.kind = plus.kind = Expr::Kind::Binary;
            times.op = "*";
            plus.op = "+";
            times.line = plus.line = e.line;
            for (std::size_t k = 0; k < s->obj->items.size(); ++k) {
                auto off = combine(times, Value::integer(Type::DInt, static_cast<std::int64_t>(k)), step);
                if (!off) return core::Err<core::Error>(off.error());
                auto v = combine(plus, *start, *off);
                if (!v) return core::Err<core::Error>(v.error());
                s->obj->items[k]->value.assignFrom(*v);
            }
            auto c = commitArg(u, *s);
            if (!c) return core::Err<core::Error>(c.error());
            return RV{Value::integer(Type::DInt, static_cast<std::int64_t>(s->obj->items.size())), nullptr};
        }
        // ---- 1.12.16 : INTERPOLER_TABLE - une courbe donnee par des points, lue en ligne droite entre deux points ;
        // avant le premier, la premiere valeur, apres le dernier, la derniere.
        if (u == "INTERPOLER_TABLE") {
            ObjArg sx;
            auto xs = numbers(0, &sx);
            if (!xs) return core::Err<core::Error>(xs.error());
            auto ys = numbers(1, nullptr);
            if (!ys) return core::Err<core::Error>(ys.error());
            auto v = value(2);
            if (!v) return core::Err<core::Error>(v.error());
            const std::size_t n = xs->size();
            if (n == 0) return dialectError(u + " : la table est vide (X n'a aucun point)");
            if (ys->size() != n)
                return dialectError(u + " : X a " + std::to_string(n) + " valeur" + (n > 1 ? "s" : "") + ", Y en a " + std::to_string(ys->size())
                                    + " \xE2\x80\x94 il en faut autant");
            // Les indices dits : ceux d'un ARRAY (a ses bornes), ou a partir de 0.
            const TypeDesc& tx = *sx.obj->type;
            const std::int64_t low = tx.kind == TypeDesc::Kind::Array && tx.bounds.size() == 1 ? tx.bounds[0].first : 0;
            std::vector<double> x(n), y(n);
            for (std::size_t k = 0; k < n; ++k) {
                x[k] = (*xs)[k].asReal();
                y[k] = (*ys)[k].asReal();
                if (k > 0 && !(x[k] > x[k - 1]))
                    return dialectError(u + " : X doit cro\xC3\xAEtre : X[" + std::to_string(low + static_cast<std::int64_t>(k)) + "] = " + (*xs)[k].display()
                                        + " ne d\xC3\xA9passe pas X[" + std::to_string(low + static_cast<std::int64_t>(k) - 1) + "] = " + (*xs)[k - 1].display());
            }
            const double at = v->asReal();
            if (std::isnan(at)) return dialectError(u + " : V n'est pas un nombre (NaN)");
            if (at <= x.front()) return RV{Value::real(y.front()), nullptr};
            if (at >= x.back()) return RV{Value::real(y.back()), nullptr};
            const std::size_t i = static_cast<std::size_t>(std::upper_bound(x.begin(), x.end(), at) - x.begin());     // x[i - 1] <= at < x[i]
            const double t = (at - x[i - 1]) / (x[i] - x[i - 1]);
            return RV{Value::real(std::lerp(y[i - 1], y[i], t)), nullptr};
        }
        // ---- les entiers : GCD, LCM ; MIDPOINT et les saturations (un <T>, ou le type de a)
        const auto integerValue = [&](std::size_t k) -> core::Result<Value> {
            auto v = value(k);
            if (!v) return v;
            if (!isInteger(v->type()) || v->type() == Type::Bool) return dialectError(u + " attend deux entiers");
            return v;
        };
        if (u == "GCD" || u == "LCM") {
            auto a = integerValue(0);
            if (!a) return core::Err<core::Error>(a.error());
            auto b = integerValue(1);
            if (!b) return core::Err<core::Error>(b.error());
            const Type rt = bitWidth(a->type()) >= bitWidth(b->type()) ? a->type() : b->type();
            const lib::Wide wa = lib::wideOf(*a), wb = lib::wideOf(*b);
            std::uint64_t x = wa.mag, y = wb.mag;
            while (y != 0) {
                const std::uint64_t r = x % y;
                x = y;
                y = r;
            }
            lib::Wide g;
            g.mag = x;
            lib::Wide r = g;
            if (u == "LCM") {
                if (wa.mag == 0 || wb.mag == 0) return RV{Value::integer(rt, 0), nullptr};
                lib::Wide q = lib::wideDiv(lib::Wide{false, wa.mag, false}, g);
                r = lib::wideMul(q, lib::Wide{false, wb.mag, false});
            }
            lib::IntType it;
            if (!lib::intType(valueTypeText(rt), it)) lib::intType("DINT", it);
            if (!lib::fits(r, it)) return dialectError(u + " : le r\xC3\xA9sultat d\xC3\xA9passe un " + valueTypeText(rt));
            return RV{lib::saturate(r, it), nullptr};
        }
        // Le type T : donne entre < >, sinon celui de a (son type ecrit, sinon celui de sa valeur).
        auto first = value(0);
        if (!first) return core::Err<core::Error>(first.error());
        std::string type = tmpl;
        if (type.empty()) {
            lib::IntType probe;
            type = writtenType(arg(0));
            if (type.empty() || (!lib::intType(type, probe) && type != "REAL" && type != "LREAL")) type = valueTypeText(first->type());
        }
        lib::IntType it;
        const bool integral = lib::intType(type, it);
        if (u == "MIDPOINT") {
            auto b = value(1);
            if (!b) return core::Err<core::Error>(b.error());
            if (!integral) return RV{Value::real((first->asReal() + b->asReal()) / 2.0), nullptr};
            const auto whole = [](const Value& v) {
                return v.type() == Type::Real ? Value::integer(Type::LInt, static_cast<std::int64_t>(std::llround(v.asReal()))) : v;
            };
            const lib::Wide wa = lib::wideOf(whole(*first)), wb = lib::wideOf(whole(*b));
            const lib::Wide mid = lib::wideAdd(wa, lib::wideHalf(lib::wideSub(wb, wa)));
            return RV{lib::saturate(mid, it), nullptr};
        }
        if (!integral)
            return dialectError(u + " : un type entier est attendu (SINT \xE2\x80\xA6 ULINT, BYTE \xE2\x80\xA6 LWORD), pas " + type
                                + (tmpl.empty() ? " \xE2\x80\x94 " + u + "<INT>(a, b)" : std::string{}));
        if (u == "SATURATE_CAST") {
            Value x = *first;
            if (x.type() == Type::Real) {
                if (std::isnan(x.asReal())) return dialectError(u + " : NaN n'est pas un nombre");
                const double r = std::round(x.asReal());
                if (r >= 18446744073709551615.0) return RV{lib::saturate(lib::Wide{false, 0, true}, it), nullptr};
                if (r <= -9223372036854775808.0) return RV{lib::saturate(lib::Wide{true, 0, true}, it), nullptr};
                if (r >= 9223372036854775807.0) {
                    lib::Wide w;
                    w.mag = static_cast<std::uint64_t>(r);
                    return RV{lib::saturate(w, it), nullptr};
                }
                x = Value::integer(Type::LInt, static_cast<std::int64_t>(r));
            }
            return RV{lib::saturate(lib::wideOf(x), it), nullptr};
        }
        auto b = value(1);
        if (!b) return core::Err<core::Error>(b.error());
        if (first->type() == Type::Real || b->type() == Type::Real || first->type() == Type::Bool || b->type() == Type::Bool)
            return dialectError(u + " : deux entiers sont attendus");
        const lib::Wide wa = lib::wideOf(*first), wb = lib::wideOf(*b);
        lib::Wide r;
        if (u == "ADD_SAT") r = lib::wideAdd(wa, wb);
        else if (u == "SUB_SAT") r = lib::wideSub(wa, wb);
        else if (u == "MUL_SAT") r = lib::wideMul(wa, wb);
        else {
            if (wb.mag == 0 && !wb.huge) return dialectError(u + " : division par z\xC3\xA9ro");
            r = lib::wideDiv(wa, wb);
        }
        return RV{lib::saturate(r, it), nullptr};
    }

    // ---- 1.12.20 : STATIC_CAST<T>(x) et DYNAMIC_CAST<T>(x) ------------------------------------------
    //  STATIC_CAST : la conversion explicite, comme static_cast - un reel tronque vers zero, un entier
    //  replie modulo 2^n (40000 -> -25536 en INT), un nombre devenu BOOL (non nul : TRUE), un numero devenu
    //  une valeur d'enumeration (sans controle, comme en C++). DYNAMIC_CAST : la conversion verifiee en
    //  marche - x doit tenir exactement dans T (un entier dans ses bornes, un reel sans decimales, 0 ou 1
    //  pour un BOOL, un numero qui est une valeur de l'enumeration) ; sinon une erreur, que TRY...CATCH
    //  rattrape (comme std::bad_cast). T : un type numerique, BOOL, une enumeration, ou le T d'une fonction
    //  generique (TEMPLATE <T>, le type deduit a l'appel).
    core::Result<RV> castNative(const std::string& u, const std::string& tmpl, const Expr& e) {
        const bool checked = u == "DYNAMIC_CAST";
        const std::string head = u + "<" + tmpl + ">";
        const auto notAType = [&](const std::string& why) -> core::Result<RV> {
            const std::vector<std::string> pool(lib::limitsTypes().begin(), lib::limitsTypes().end());
            const std::string near = lib::closest(tmpl, pool);
            return dialectError(head + " : " + tmpl + " n'est pas un type" + (near.empty() ? " (" + why + ")" : near));
        };
        auto tt = typeFromText(tmpl);
        if (!tt) return notAType(tt.error().message());
        auto rt = resolve(*tt);
        if (!rt) return notAType(rt.error().message());
        const TypeDesc& target = **rt;
        std::string tname = upperOf(target.name);
        // 1.13.0 (lot J1) : DYNAMIC_CAST<T>(objet) vers un type IHM ou une interface - une MONTEE/DESCENTE
        // sur une reference : si le type REEL de l'objet va dans T (sous-type ou interface implementee),
        // rend une reference PERSISTANTE vers lui (comme REF, par son emplacement) ; sinon NULL. STATIC_CAST
        // lie toujours (comme un cast non verifie).
        if (target.kind == TypeDesc::Kind::Struct && !target.name.empty()) {
            const Expr& a = *e.arguments[0].second;
            if (isDesignator(a)) {
                auto where = locate(a, false);
                if (!where) return core::Result<RV>(core::Err<core::Error>(where.error()));
                std::string realType;
                if (where->obj && where->obj->type) realType = where->obj->type->text();
                else if (!where->name.empty()) realType = env_.declaredType(env_.canonicalName(where->name));
                if (!realType.empty()) {
                    const bool ok = !checked || env_.typeConvertsTo(realType, target.name) || upperOf(realType) == tname;
                    auto refT = std::make_shared<TypeDesc>();
                    refT->kind = TypeDesc::Kind::Ref;
                    refT->element = *rt;
                    auto o = std::make_shared<Obj>();
                    o->type = refT;
                    o->bound = ok;
                    if (ok) {
                        if (where->obj) o->target = where->obj;
                        else o->targetName = env_.canonicalName(where->name);
                    }
                    return RV{{}, o};
                }
            }
            // un objet litteral/temporaire ne se caste pas ainsi : on continue vers l'erreur ci-dessous.
        }
        if (target.kind != TypeDesc::Kind::Scalar || target.scalar == Type::String || target.scalar == Type::Time || isDateType(target.scalar))
            return dialectError(head + " : " + std::string(lib::templateInfo(u)->want));
        std::vector<std::pair<std::string, std::int64_t>> values;
        const bool toEnum = !target.name.empty() && enumValues(target.name, values);
        if (tname.empty() || toEnum) tname = toEnum ? tname : valueTypeText(target.scalar);
        auto w = rich(*e.arguments[0].second);
        if (!w) return w;
        if (w->o) return dialectError(head + " : " + typeNameOf(*w) + " ne se convertit pas ainsi (un nombre, un BOOL ou une \xC3\xA9num\xC3\xA9ration)");
        const Value x = w->v;
        const Type xt = x.type();
        if (xt == Type::String || xt == Type::Time || isDateType(xt) || !(xt == Type::Real || xt == Type::Bool || isInteger(xt)))
            return dialectError(head + " : " + (xt == Type::String ? std::string("un texte") : xt == Type::Time ? std::string("une dur\xC3\xA9" "e") : std::string(toString(xt)))
                                + " ne se convertit pas ainsi - " + u + " convertit des nombres, des BOOL et des \xC3\xA9num\xC3\xA9rations (un texte : STRING_TO_INT, STRING_TO_REAL)");
        const bool real = xt == Type::Real;
        const double r = real ? x.asReal() : 0.0;
        const std::string shownX = x.display();
        if (real && !std::isfinite(r)) return dialectError(head + " : " + shownX + " n'est pas un nombre fini");
        // ---- vers un reel : REAL (arrondi au float) ou LREAL ----
        if (target.scalar == Type::Real) {
            const double d = real ? r : xt == Type::Bool ? ((x.asInteger() != 0) ? 1.0 : 0.0)
                           : (xt == Type::ULInt || xt == Type::LWord) ? static_cast<double>(static_cast<std::uint64_t>(x.asInteger()))
                                                                      : static_cast<double>(x.asInteger());
            if (tname == "REAL") {
                if (checked && std::fabs(d) > static_cast<double>(std::numeric_limits<float>::max()))
                    return dialectError(head + " : " + shownX + " ne tient pas dans un REAL (au plus 3.4E38)");
                return RV{Value::real(static_cast<double>(static_cast<float>(d))), nullptr};
            }
            return RV{Value::real(d), nullptr};
        }
        // ---- vers un BOOL ----
        if (target.scalar == Type::Bool) {
            const bool zero = real ? r == 0.0 : x.asInteger() == 0;
            if (checked && !(xt == Type::Bool || zero || (real ? r == 1.0 : x.asInteger() == 1)))
                return dialectError(head + " : " + shownX + " n'est ni 0 ni 1");
            return RV{Value::boolean(!zero), nullptr};
        }
        // ---- vers un entier (ou une enumeration : un DINT qui garde le nom de son type) ----
        lib::IntType it;
        if (!lib::intType(toEnum ? "DINT" : tname, it) && !lib::intType(valueTypeText(target.scalar), it))
            return dialectError(head + " : " + std::string(lib::templateInfo(u)->want));
        // Les bits de x, en complement a deux sur 64 bits (un reel : tronque vers zero), et sa valeur exacte.
        std::uint64_t bits = 0;
        lib::Wide exact;
        if (real) {
            const double t = std::trunc(r);
            if (checked && t != r) return dialectError(head + " : " + shownX + " n'est pas un entier (il a des d\xC3\xA9" "cimales)");
            if (t >= 18446744073709551616.0 || t < -9223372036854775808.0)
                return dialectError(head + " : " + shownX + " est trop grand pour un entier (64 bits)");
            if (t < 0.0) {
                const auto n = static_cast<std::int64_t>(t);
                bits = static_cast<std::uint64_t>(n);
                exact.neg = true;
                exact.mag = static_cast<std::uint64_t>(-(n + 1)) + 1;
            } else {
                bits = static_cast<std::uint64_t>(t);
                exact.mag = bits;
            }
        } else if (xt == Type::Bool) {
            bits = (x.asInteger() != 0) ? 1 : 0;
            exact.mag = bits;
        } else {
            bits = static_cast<std::uint64_t>(x.asInteger());
            exact = lib::wideOf(x);
        }
        if (toEnum) {
            if (checked) {
                const bool found = !exact.huge && std::any_of(values.begin(), values.end(), [&](const auto& v) {
                    return v.second < 0 ? exact.neg && exact.mag == static_cast<std::uint64_t>(-(v.second + 1)) + 1
                                        : !exact.neg && exact.mag == static_cast<std::uint64_t>(v.second);
                });
                if (!found) {
                    std::string list;
                    for (std::size_t k = 0; k < values.size() && k < 6; ++k) list += (k ? ", " : "") + values[k].first + " = " + std::to_string(values[k].second);
                    if (values.size() > 6) list += ", \xE2\x80\xA6";
                    return dialectError(head + " : " + shownX + " n'est pas une valeur de " + target.name + " (" + list + ")");
                }
            }
        } else if (checked && !lib::fits(exact, it)) {
            const std::string hi = it.isSigned ? std::to_string(static_cast<std::int64_t>(it.hiBits)) : std::to_string(it.hiBits);
            return dialectError(head + " : " + shownX + " ne tient pas dans un " + tname + " (" + std::to_string(it.lo) + " \xE2\x80\xA6 " + hi + ")");
        }
        // Le repli modulo 2^n (static_cast d'un entier trop grand) : les n bits du bas, signe etendu.
        const std::uint64_t mask = it.bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << it.bits) - 1;
        std::uint64_t low = bits & mask;
        if (it.isSigned && it.bits < 64 && ((low >> (it.bits - 1)) & 1u)) low |= ~mask;
        return RV{Value::integer(it.type, static_cast<std::int64_t>(low)), nullptr};
    }

    // ---- <bit> : BIT_COUNT, BIT_FIRST, BIT_TEST, BIT_SET, BIT_RESET ----------------------
    core::Result<RV> bitNative(const std::string& u, const Expr& e) {
        if (u == "BYTESWAP" || u == "WORDSWAP") return swapNative(u, *e.arguments[0].second);       // 1.12.16
        const auto arg = [&](std::size_t k) -> const Expr& { return *e.arguments[k].second; };
        auto w = rich(arg(0));
        if (!w) return w;
        if (w->o || !isInteger(w->v.type()) || w->v.type() == Type::Bool || w->v.type() == Type::Time || isDateType(w->v.type()))
            return dialectError(u + " : un mot ou un entier est attendu (BYTE, WORD, DWORD, LWORD, INT\xE2\x80\xA6), pas "
                                + (w->o ? typeNameOf(*w) : w->v.type() == Type::String ? w->v.display() : std::string(toString(w->v.type()))));
        const std::string written = writtenType(arg(0));
        lib::IntType it;
        int width = bitWidth(w->v.type());
        std::string typeName = valueTypeText(w->v.type());
        bool isSigned = w->v.type() == Type::Int || w->v.type() == Type::DInt || w->v.type() == Type::LInt;
        if (!written.empty() && lib::intType(written, it)) {
            width = it.bits;
            typeName = written;
            isSigned = it.isSigned;
        }
        const std::uint64_t mask = width >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
        const std::uint64_t bits = static_cast<std::uint64_t>(w->v.asInteger()) & mask;
        if (u == "BIT_COUNT") {
            int c = 0;
            for (std::uint64_t x = bits; x != 0; x &= x - 1) ++c;
            return RV{Value::integer(Type::DInt, c), nullptr};
        }
        if (u == "BIT_FIRST") {
            if (bits == 0) return RV{Value::integer(Type::DInt, -1), nullptr};
            int k = 0;
            while (((bits >> k) & 1u) == 0) ++k;
            return RV{Value::integer(Type::DInt, k), nullptr};
        }
        auto n = intArg(u, arg(1), "le num\xC3\xA9ro du bit");
        if (!n) return core::Err<core::Error>(n.error());
        if (*n < 0 || *n >= width)
            return dialectError(u + " : le bit " + std::to_string(*n) + " n'existe pas dans un " + typeName + " (0.." + std::to_string(width - 1) + ")");
        const std::uint64_t one = std::uint64_t{1} << *n;
        if (u == "BIT_TEST") return RV{Value::boolean((bits & one) != 0), nullptr};
        const std::uint64_t r = u == "BIT_SET" ? (bits | one) : (bits & ~one);
        std::int64_t out = static_cast<std::int64_t>(r);
        if (isSigned && width < 64 && ((r >> (width - 1)) & 1u) != 0) out = static_cast<std::int64_t>(r | ~mask);     // le signe
        return RV{Value::integer(w->v.type(), truncateTo(w->v.type(), out)), nullptr};
    }

    // =====================================================================================
    // 1.12.16 : LES NATIVES std:: - <cmath>, <string>, <format>, <chrono> sur des valeurs simples ;
    // <bit> (BYTESWAP, WORDSWAP) ; <utility> (SWAP, EXCHANGE : des variables) ; LIST_TRANSFORM (une
    // TRANSFORMATION, ou une fonction du projet par son nom, comme une lambda).
    // =====================================================================================
    static constexpr std::int64_t kDayMs = 86400000;
    static std::int64_t floorDivision(std::int64_t a, std::int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

    core::Result<RV> valueNative(const std::string& u, const Expr& e) {
        std::vector<Value> a;
        for (const auto& given : e.arguments) {
            auto v = rich(*given.second);
            if (!v) return v;
            if (v->o) return dialectError(u + " : une valeur simple est attendue, pas un " + typeNameOf(*v));
            a.push_back(v->v);
        }
        const auto out = [](Value v) { return core::Result<RV>(RV{std::move(v), nullptr}); };
        // Un nombre (ni un BOOL, ni une duree, ni une date) ; un texte.
        const auto number = [&](std::size_t k) -> core::Result<double> {
            const Type t = a[k].type();
            if (!isNumeric(t) || t == Type::Time) return dialectError(u + " : un nombre est attendu, pas " + a[k].display());
            return a[k].asReal();
        };
        const auto text = [&](std::size_t k) -> core::Result<std::string> {
            if (a[k].type() != Type::String) return dialectError(u + " : un texte est attendu, pas " + a[k].display());
            return a[k].asString();
        };
        // ---- <cmath>
        if (u == "CEIL" || u == "FLOOR") {
            auto x = number(0);
            if (!x) return core::Err<core::Error>(x.error());
            if (a[0].type() != Type::Real) return out(a[0]);              // un entier l'est deja, dans son type
            return out(Value::real(u == "CEIL" ? std::ceil(*x) : std::floor(*x)));
        }
        if (u == "ISNAN" || u == "ISFINITE") {
            auto x = number(0);
            if (!x) return core::Err<core::Error>(x.error());
            return out(Value::boolean(u == "ISNAN" ? std::isnan(*x) : std::isfinite(*x)));
        }
        if (u == "FMOD" || u == "HYPOT" || u == "ATAN2" || u == "LERP") {
            double x[3] = {0.0, 0.0, 0.0};
            for (std::size_t k = 0; k < a.size() && k < 3; ++k) {
                auto v = number(k);
                if (!v) return core::Err<core::Error>(v.error());
                x[k] = *v;
            }
            if (u == "FMOD") {
                if (x[1] == 0.0) return dialectError(u + " : division par z\xC3\xA9ro");
                return out(Value::real(std::fmod(x[0], x[1])));
            }
            if (u == "HYPOT") return out(Value::real(std::hypot(x[0], x[1])));
            if (u == "ATAN2") return out(Value::real(std::atan2(x[0], x[1])));
            return out(Value::real(std::lerp(x[0], x[1], x[2])));
        }
        // ---- <string>, <format>
        if (u == "STARTS_WITH" || u == "ENDS_WITH" || u == "CONTAINS") {
            auto s = text(0);
            if (!s) return core::Err<core::Error>(s.error());
            auto p = text(1);
            if (!p) return core::Err<core::Error>(p.error());
            if (u == "CONTAINS") return out(Value::boolean(s->find(*p) != std::string::npos));
            if (p->size() > s->size()) return out(Value::boolean(false));
            const std::size_t from = u == "STARTS_WITH" ? 0 : s->size() - p->size();
            return out(Value::boolean(s->compare(from, p->size(), *p) == 0));
        }
        if (u == "TRIM" || u == "TRIM_LEFT" || u == "TRIM_RIGHT") {
            auto s = text(0);
            if (!s) return core::Err<core::Error>(s.error());
            static constexpr const char* kBlanks = " \t\r\n";
            std::string r = *s;
            if (u != "TRIM_LEFT") {
                const auto last = r.find_last_not_of(kBlanks);
                r.erase(last == std::string::npos ? 0 : last + 1);
            }
            if (u != "TRIM_RIGHT") r.erase(0, std::min(r.find_first_not_of(kBlanks), r.size()));
            return out(Value::text(std::move(r)));
        }
        if (u == "FORMAT") {
            auto model = text(0);
            if (!model) return core::Err<core::Error>(model.error());
            auto f = formatText(u, *model, std::vector<Value>(a.begin() + 1, a.end()));
            if (!f) return core::Err<core::Error>(f.error());
            return out(Value::text(std::move(*f)));
        }
        // ---- <chrono> : une date (DATE, DATE_AND_TIME), une heure (DATE_AND_TIME, TIME_OF_DAY) - ses jours
        // depuis le 1970-01-01, ses millisecondes depuis minuit (l'heure du mur, sans fuseau).
        struct When {
            std::int64_t days{0}, clock{0};
            bool         dated{true};
        };
        const auto when = [&](std::size_t k, bool date, bool clock) -> core::Result<When> {
            const Type t = a[k].type();
            if (!(t == Type::Dt || (t == Type::Date && !clock) || (t == Type::Tod && !date))) {
                const char* want = date && !clock ? "une date est attendue (DATE_AND_TIME, DATE)"
                                 : clock && !date ? "une heure est attendue (DATE_AND_TIME, TIME_OF_DAY)"
                                                  : "une date ou une heure est attendue (DATE_AND_TIME, DATE, TIME_OF_DAY)";
                return dialectError(u + " : " + want + ", pas " + a[k].display());
            }
            When w;
            const std::int64_t ms = a[k].asInteger(), day = floorDivision(ms, kDayMs);
            w.dated = t != Type::Tod;
            w.days = w.dated ? day : 0;
            w.clock = ms - day * kDayMs;
            return w;
        };
        const auto weekday = [](std::int64_t days) { return static_cast<int>(((days + 3) % 7 + 7) % 7); };   // 0 : lundi (le 1970-01-01, un jeudi)
        if (u == "YEAR" || u == "MONTH" || u == "DAY" || u == "WEEKDAY") {
            auto w = when(0, true, false);
            if (!w) return core::Err<core::Error>(w.error());
            if (u == "WEEKDAY") return out(Value::integer(Type::Int, weekday(w->days) + 1));     // ISO 8601 : 1 lundi ... 7 dimanche
            std::int64_t y = 0;
            unsigned m = 0, d = 0;
            civilFromDays(w->days, y, m, d);
            return out(Value::integer(Type::Int, u == "YEAR" ? y : u == "MONTH" ? static_cast<std::int64_t>(m) : static_cast<std::int64_t>(d)));
        }
        if (u == "HOUR" || u == "MINUTE" || u == "SECOND") {
            auto w = when(0, false, true);
            if (!w) return core::Err<core::Error>(w.error());
            const std::int64_t s = w->clock / 1000;
            return out(Value::integer(Type::Int, u == "HOUR" ? s / 3600 : u == "MINUTE" ? s / 60 % 60 : s % 60));
        }
        if (u == "ADD_DAYS") {
            auto w = when(0, true, false);
            if (!w) return core::Err<core::Error>(w.error());
            const Type nt = a[1].type();
            if (!isInteger(nt) || nt == Type::Time) return dialectError(u + " : N est un nombre entier de jours, pas " + a[1].display());
            const std::int64_t n = a[1].asInteger();
            std::int64_t y = n > 0 ? 10000 : 0;                    // au-dela de dix mille ans : hors du calendrier
            unsigned m = 0, d = 0;
            if (n >= -3660000 && n <= 3660000) civilFromDays(w->days + n, y, m, d);
            if (y < 1 || y > 9999) return dialectError(u + " : la date sort du calendrier (ann\xC3\xA9" "es 1 \xC3\xA0 9999)");
            return out(Value::integer(a[0].type(), a[0].asInteger() + n * kDayMs));         // a la meme heure, du meme type
        }
        if (u == "DAYS_BETWEEN") {
            auto from = when(0, true, false);
            if (!from) return core::Err<core::Error>(from.error());
            auto to = when(1, true, false);
            if (!to) return core::Err<core::Error>(to.error());
            // Des jours entiers, vers zero (18 jours et 6 h font 18 ; B avant A, le meme nombre, negatif) ; en
            // millisecondes sur 64 bits, pas en TIME (49 jours au plus).
            return out(Value::integer(Type::DInt, (a[1].asInteger() - a[0].asInteger()) / kDayMs));
        }
        if (u == "FORMAT_DT") {
            auto w = when(0, false, false);
            if (!w) return core::Err<core::Error>(w.error());
            auto model = text(1);
            if (!model) return core::Err<core::Error>(model.error());
            static const char* const kDays[7] = {"lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"};
            static const char* const kShortDays[7] = {"lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."};
            static const char* const kMonths[12] = {"janvier", "f\xC3\xA9vrier", "mars", "avril", "mai", "juin", "juillet", "ao\xC3\xBBt",
                                                    "septembre", "octobre", "novembre", "d\xC3\xA9" "cembre"};
            static const char* const kShortMonths[12] = {"janv.", "f\xC3\xA9vr.", "mars", "avr.", "mai", "juin", "juil.", "ao\xC3\xBBt",
                                                         "sept.", "oct.", "nov.", "d\xC3\xA9" "c."};
            std::int64_t y = 1970;
            unsigned m = 1, d = 1;
            civilFromDays(w->days, y, m, d);
            const std::int64_t s = w->clock / 1000;
            const auto digits = [](std::int64_t v, int n) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%0*lld", n, static_cast<long long>(v));
                return std::string(buf);
            };
            const std::string& f = *model;
            std::string r;
            for (std::size_t i = 0; i < f.size(); ++i) {
                if (f[i] != '%') {
                    r += f[i];
                    continue;
                }
                if (i + 1 >= f.size()) return dialectError(u + " : un % seul \xC3\xA0 la fin du mod\xC3\xA8le (%% pour un %)");
                const char c = f[++i];
                if (c == '%') {
                    r += '%';
                    continue;
                }
                const bool dated = c == 'd' || c == 'm' || c == 'Y' || c == 'y' || c == 'A' || c == 'a' || c == 'B' || c == 'b';
                if (dated && !w->dated) return dialectError(u + " : %" + std::string(1, c) + " : un TIME_OF_DAY n'a pas de date");
                switch (c) {
                    case 'd': r += digits(d, 2); break;
                    case 'm': r += digits(m, 2); break;
                    case 'Y': r += digits(y, 4); break;
                    case 'y': r += digits((y % 100 + 100) % 100, 2); break;
                    case 'H': r += digits(s / 3600, 2); break;
                    case 'M': r += digits(s / 60 % 60, 2); break;
                    case 'S': r += digits(s % 60, 2); break;
                    case 'A': r += kDays[weekday(w->days)]; break;
                    case 'a': r += kShortDays[weekday(w->days)]; break;
                    case 'B': r += kMonths[(m + 11) % 12]; break;
                    case 'b': r += kShortMonths[(m + 11) % 12]; break;
                    default:
                        return dialectError(u + " : %" + std::string(1, c) + " inconnu (%d %m %Y %y %H %M %S %A %a %B %b %%)");
                }
            }
            return out(Value::text(std::move(r)));
        }
        return dialectError(u + " : pas encore prise en charge");
    }

    // ---- FORMAT : std::format, en petit. {} : la valeur suivante ; {:[[remplissage]alignement][signe][#][0][largeur][.precision][type]}
    // (alignement < > ^ ; type d x X b B o pour un entier, f F e E g G pour un reel, s pour un texte) ; {{ et }} : une accolade.
    // Une largeur compte des caracteres (UTF-8), pas des octets.
    static std::size_t utf8Length(const std::string& s) {
        std::size_t n = 0;
        for (const char c : s) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0;
        return n;
    }
    static std::string utf8Prefix(const std::string& s, std::size_t count) {
        std::size_t n = 0, i = 0;
        for (; i < s.size(); ++i)
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
                if (n == count) break;
                ++n;
            }
        return s.substr(0, i);
    }
    core::Result<std::string> formatOne(const std::string& u, const std::string& spec, const Value& v) {
        const std::string hole = spec.empty() ? std::string("{}") : "{:" + spec + "}";
        const auto bad = [&](const std::string& why) { return dialectError(u + " : " + hole + " : " + why); };
        const auto isAlign = [](char c) { return c == '<' || c == '>' || c == '^'; };
        const auto isDigit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
        std::size_t i = 0, width = 0;
        char fill = ' ', align = 0, sign = '-', type = 0;
        bool alt = false, zero = false;
        int precision = -1;
        if (spec.size() >= 2 && isAlign(spec[1])) {
            fill = spec[0];
            align = spec[1];
            i = 2;
        } else if (!spec.empty() && isAlign(spec[0])) {
            align = spec[0];
            i = 1;
        }
        if (i < spec.size() && (spec[i] == '+' || spec[i] == '-' || spec[i] == ' ')) sign = spec[i++];
        if (i < spec.size() && spec[i] == '#') {
            alt = true;
            ++i;
        }
        if (i < spec.size() && spec[i] == '0') {
            zero = true;
            ++i;
        }
        while (i < spec.size() && isDigit(spec[i])) {
            width = width * 10 + static_cast<std::size_t>(spec[i++] - '0');
            if (width > 1000) return bad("largeur trop grande (1000 au plus)");
        }
        if (i < spec.size() && spec[i] == '.') {
            if (++i >= spec.size() || !isDigit(spec[i])) return bad("le nombre de d\xC3\xA9" "cimales manque apr\xC3\xA8s le point ({:.2f})");
            precision = 0;
            while (i < spec.size() && isDigit(spec[i])) {
                precision = precision * 10 + (spec[i++] - '0');
                if (precision > 50) return bad("50 d\xC3\xA9" "cimales au plus");
            }
        }
        if (i < spec.size()) type = spec[i++];
        if (i != spec.size() || (type != 0 && std::string_view("dxXbBofFeEgGs").find(type) == std::string_view::npos))
            return bad("format inconnu (d, x, X, b, o, f, e, g, s ; {:.2f}, {:5}, {:05}, {:<8})");
        const Type t = v.type();
        const bool floatType = type != 0 && std::string_view("fFeEgG").find(type) != std::string_view::npos;
        const bool intType = type != 0 && std::string_view("dxXbBo").find(type) != std::string_view::npos;
        const bool integral = isInteger(t) && t != Type::Time;
        std::string head, body;           // le signe (et 0x...), puis les chiffres ou le texte
        bool numeric = true;
        if (t == Type::Real || (integral && floatType)) {
            if (intType) return bad("va avec un entier, pas " + v.display());
            if (type == 's') return bad("va avec un texte, pas " + v.display());
            const double x = v.asReal();
            if (std::signbit(x) && !std::isnan(x)) head = "-";
            else if (sign == '+' || sign == ' ') head = std::string(1, sign);
            const double m = std::fabs(x);
            if (!type && precision < 0) {
                body = Value::real(m).display();                    // comme TO_STRING
            } else {
                const char fmt[] = {'%', '.', '*', type ? type : 'g', '\0'};
                char buf[400];
                std::snprintf(buf, sizeof buf, fmt, precision < 0 ? 6 : precision, m);
                body = buf;
            }
        } else if (integral) {
            if (type == 's') return bad("va avec un texte, pas " + v.display());
            if (precision >= 0) return bad("la pr\xC3\xA9" "cision va avec un r\xC3\xA9" "el ({:.2f}), pas " + v.display());
            std::uint64_t mag = static_cast<std::uint64_t>(v.asInteger());
            if (t != Type::ULInt && t != Type::LWord && v.asInteger() < 0) {
                head = "-";
                mag = std::uint64_t{0} - mag;
            } else if (sign == '+' || sign == ' ') {
                head = std::string(1, sign);
            }
            const unsigned base = type == 'x' || type == 'X' ? 16u : type == 'b' || type == 'B' ? 2u : type == 'o' ? 8u : 10u;
            const char* digit = type == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            do {
                body.insert(body.begin(), digit[mag % base]);
                mag /= base;
            } while (mag != 0);
            if (alt && base != 10) head += base == 16 ? (type == 'X' ? "0X" : "0x") : base == 2 ? (type == 'B' ? "0B" : "0b") : "0";
        } else {
            // Un texte, un BOOL, une duree, une date : leur texte (TRUE, T#1500ms, D#2026-10-19).
            if (type != 0 && type != 's') return bad("va avec un nombre, pas " + v.display());
            numeric = false;
            body = t == Type::String ? v.asString() : v.display();
            if (precision >= 0) body = utf8Prefix(body, static_cast<std::size_t>(precision));
        }
        const std::size_t length = utf8Length(head) + utf8Length(body);
        if (width <= length) return head + body;
        const std::size_t pad = width - length;
        if (numeric && zero && !align) return head + std::string(pad, '0') + body;          // {:05} : des zeros apres le signe
        const char side = align ? align : numeric ? '>' : '<';
        const std::size_t left = side == '<' ? 0 : side == '>' ? pad : pad / 2;
        return std::string(left, fill) + head + body + std::string(pad - left, fill);
    }
    core::Result<std::string> formatText(const std::string& u, const std::string& model, const std::vector<Value>& values) {
        std::string out;
        std::size_t holes = 0;
        for (std::size_t i = 0; i < model.size(); ++i) {
            const char c = model[i];
            if (c == '}') {
                if (i + 1 < model.size() && model[i + 1] == '}') {
                    out += '}';
                    ++i;
                    continue;
                }
                return dialectError(u + " : une } seule \xE2\x80\x94 }} pour une accolade");
            }
            if (c != '{') {
                out += c;
                continue;
            }
            if (i + 1 < model.size() && model[i + 1] == '{') {
                out += '{';
                ++i;
                continue;
            }
            const auto close = model.find('}', i + 1);
            if (close == std::string::npos) return dialectError(u + " : une { sans } \xE2\x80\x94 {{ pour une accolade");
            const std::string inside = model.substr(i + 1, close - i - 1);
            i = close;
            if (!inside.empty() && inside.front() != ':')
                return dialectError(u + " : {" + inside + "} \xE2\x80\x94 {} prend la valeur suivante, {:.1f} la met en forme");
            if (holes < values.size()) {
                auto piece = formatOne(u, inside.empty() ? std::string{} : inside.substr(1), values[holes]);
                if (!piece) return piece;
                out += *piece;
            }
            ++holes;
        }
        if (holes != values.size())
            return dialectError(u + " : " + std::to_string(holes) + " {} pour " + std::to_string(values.size()) + (values.size() > 1 ? " valeurs" : " valeur"));
        return out;
    }

    // ---- BYTESWAP, WORDSWAP (std::byteswap, les mots de 16 bits) : la largeur du type ecrit (une variable),
    // d'un litteral sans type (la plus petite qui le tient : 16 bits, 32 pour WORDSWAP, puis 64), ou de la valeur.
    core::Result<RV> swapNative(const std::string& u, const Expr& a) {
        auto w = rich(a);
        if (!w) return w;
        const bool words = u == "WORDSWAP";
        const std::string want = words ? " : un entier de 32 bits (ou 64) est attendu (DWORD, DINT, UDINT, LWORD), pas "
                                       : " : un mot ou un entier est attendu (WORD, DWORD, INT, DINT, UINT, UDINT, LWORD), pas ";
        if (w->o || !isInteger(w->v.type()) || w->v.type() == Type::Time)
            return dialectError(u + want + (w->o ? "un " + typeNameOf(*w) : w->v.type() == Type::String ? w->v.display() : std::string(toString(w->v.type()))));
        Type type = w->v.type();
        std::string name = valueTypeText(type);
        int width = bitWidth(type);
        bool isSigned = type == Type::Int || type == Type::DInt || type == Type::LInt;
        const Expr* lit = &a;
        if (lit->kind == Expr::Kind::Unary && (lit->op == "-" || lit->op == "+") && lit->lhs) lit = lit->lhs.get();
        const std::string written = writtenType(a);
        lib::IntType it;
        if (!written.empty() && lib::intType(written, it)) {
            width = it.bits;
            name = written;
            isSigned = it.isSigned;
            type = it.type;
        } else if (lit->kind == Expr::Kind::Literal && lit->literalType.empty()) {
            const std::int64_t v = w->v.asInteger();
            const bool negative = v < 0 && type != Type::ULInt && type != Type::LWord;
            const std::uint64_t mag = negative ? std::uint64_t{0} - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
            width = !words && mag <= (negative ? 0x8000u : 0xFFFFu) ? 16 : mag <= (negative ? 0x80000000ull : 0xFFFFFFFFull) ? 32 : 64;
            isSigned = negative;
            type = width == 16 ? (negative ? Type::Int : Type::Word) : width == 32 ? (negative ? Type::DInt : Type::DWord) : (negative ? Type::LInt : Type::LWord);
            name = valueTypeText(type);
        }
        if (width < 16) return dialectError(u + " : un " + name + " n'a qu'un octet (rien \xC3\xA0 inverser)");
        if (words && width < 32) return dialectError(u + want + "un " + name);
        const std::uint64_t mask = width >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
        const std::uint64_t bits = static_cast<std::uint64_t>(w->v.asInteger()) & mask;
        const int unit = words ? 16 : 8;
        const std::uint64_t part = words ? 0xFFFFu : 0xFFu;
        std::uint64_t r = 0;
        for (int k = 0; k < width / unit; ++k) r |= ((bits >> (unit * k)) & part) << (width - unit - unit * k);
        std::int64_t out = static_cast<std::int64_t>(r);
        if (isSigned && width < 64 && ((r >> (width - 1)) & 1u) != 0) out = static_cast<std::int64_t>(r | ~mask);     // le signe
        return RV{Value::integer(type, truncateTo(type, out)), nullptr};
    }

    // ---- <utility> : SWAP(A, B) echange deux variables du meme type ; EXCHANGE(X, NOUVELLE) ecrit X et rend son
    // ancienne valeur. Des variables (un nom, un membre, une case), comme un VAR_IN_OUT : ni une valeur calculee,
    // ni une constante.
    core::Result<RV> utilityNative(const std::string& u, const Expr& e) {
        const auto arg = [&](std::size_t k) -> const Expr& { return *e.arguments[k].second; };
        const auto variable = [&](std::size_t k) -> core::Result<Place> {
            const Expr& a = arg(k);
            const std::string nth = k == 0 ? "1er" : "2e";
            if (!isDesignator(a)) return dialectError(u + " : une variable est attendue en " + nth + " argument, pas " + exprText(a));
            auto p = locate(a, true);
            if (!p) return p;
            if (p->temp)
                return dialectError(u + " : une variable est attendue en " + nth + " argument, pas une valeur calcul\xC3\xA9" "e (" + exprText(a) + ")");
            if (p->constant) return constantRefused(u);
            if (p->setItem) return dialectError(u + " : " + std::string(kSetItemRefused));
            return p;
        };
        // Le type d'un emplacement : celui de son objet, ou celui que l'environnement declare (sa valeur, sinon).
        const auto typeOfPlace = [&](const Place& p) -> std::string {
            if (p.obj && p.obj->type) return p.obj->type->kind == TypeDesc::Kind::Scalar ? scalarTypeName(*p.obj->type) : p.obj->type->text();
            std::string t = upperOf(env_.declaredType(p.name));
            if (t.empty()) {
                Value probe;
                if (env_.read(p.name, probe)) t = valueTypeText(probe.type());
            }
            return t;
        };
        // Deux ecritures du meme type : STRING[20] et STRING, TOD et TIME_OF_DAY, LREAL et REAL (le moteur les calcule pareil).
        const auto same = [](const std::string& x, const std::string& y) {
            const auto base = [](const std::string& s) {
                const std::string t = upperOf(s);
                if (t == "LREAL") return std::string("REAL");
                const Type ty = typeFromName(t);
                return ty == Type::String || ty == Type::Bool || isDateType(ty) ? std::string(toString(ty)) : t;
            };
            return base(x) == base(y);
        };
        const auto copyOf = [](const RV& v) { return v.o ? RV{{}, deepCopy(*v.o)} : v; };
        if (u == "SWAP") {
            auto pa = variable(0);
            if (!pa) return core::Err<core::Error>(pa.error());
            auto pb = variable(1);
            if (!pb) return core::Err<core::Error>(pb.error());
            const std::string ta = typeOfPlace(*pa), tb = typeOfPlace(*pb);
            if (!ta.empty() && !tb.empty() && !same(ta, tb))
                return dialectError(u + " : " + exprText(arg(0)) + " est un " + ta + ", " + exprText(arg(1)) + " un " + tb
                                    + " \xE2\x80\x94 deux variables du m\xC3\xAAme type");
            auto va = readPlace(*pa);
            if (!va) return va;
            auto vb = readPlace(*pb);
            if (!vb) return vb;
            const RV first = copyOf(*va), second = copyOf(*vb);     // des copies : ecrire l'une ne change pas l'autre
            std::string why;
            if (!writePlace(*pa, second, &why) || !writePlace(*pb, first, &why)) return dialectError(u + " : " + why);
            return RV{Value::boolean(true), nullptr};
        }
        // EXCHANGE : NOUVELLE d'abord (comme l'argument d'un appel C++), puis l'ancienne valeur de X, puis X ecrit.
        auto px = variable(0);
        if (!px) return core::Err<core::Error>(px.error());
        auto fresh = rich(arg(1));
        if (!fresh) return fresh;
        auto old = readPlace(*px);
        if (!old) return old;
        const RV kept = copyOf(*old);
        RV next = *fresh;
        // Une valeur simple va dans le type de X, refusee comme LIST_SET la refuse ('abc' n'est pas un INT, 7.5 pas un entier).
        TypeRef target;
        if (px->obj && px->obj->type && px->obj->type->kind == TypeDesc::Kind::Scalar) {
            target = px->obj->type;
        } else if (!px->obj) {
            if (const std::string text = env_.declaredType(px->name); !text.empty())
                if (auto t = typeFromText(text); t && *t && (*t)->kind == TypeDesc::Kind::Scalar) target = *t;
        }
        if (target && !next.o) {
            auto made = fitTo(u, target, next);
            if (!made) return core::Err<core::Error>(made.error());
            next = valueOfItem(*made);
        }
        std::string why;
        if (!writePlace(*px, next, &why)) return dialectError(u + " : " + why);
        return kept;
    }

    // ---- 1.12.26 : LES FICHIERS ET LES RESSOURCES DU PROJET (FILE_, DIR_, RESOURCE_, PATH_) ----------------------
    // Les chemins se calculent ici (PATH_ : du texte, '/' et '\' tous deux separateurs) ; le reste, l'environnement
    // le fait (fileOp). Un echec ne coupe pas le script : FALSE, '', une liste vide, -1 - et FILE_ERROR() dit pourquoi
    // (la derniere fonction de fichier de cette execution).
    std::string fileError_;
    std::uint32_t sliced_{0};                // 1.12.26 : les instructions depuis le debut (la tranche d'un script detache)

    // ---- 1.12.26 : WAIT(duree) - l'environnement attend : un script detache rend la main jusqu'a son heure, un autre
    // bloque l'IHM (10 s au plus). Arrete pendant l'attente : le script s'arrete sans erreur (meme dans un TRY).
    core::Result<RV> waitNative(const std::string& u, const Expr& e) {
        auto v = rich(*e.arguments[0].second);
        if (!v) return v;
        const Type t = v->o ? Type::Unknown : v->v.type();
        std::int64_t ms = 0;
        if (t == Type::Time) ms = v->v.asInteger();
        else if (isNumeric(t) && t != Type::Bool) ms = static_cast<std::int64_t>(std::llround(v->v.asReal()));
        else return dialectError(u + " : une dur\xC3\xA9" "e est attendue (T#500ms, ou un nombre de millisecondes), pas "
                                 + (v->o ? "un " + typeNameOf(*v) : v->v.display()));
        if (ms < 0) ms = 0;
        std::string why;
        if (env_.wait(ms, why)) return RV{Value::boolean(true), nullptr};
        if (why.empty()) {
            aborted_ = true;
            return core::fail(core::ErrorCode::InvalidArgument, "arr\xC3\xAAt\xC3\xA9 pendant WAIT");
        }
        return dialectError(u + " : " + why);
    }

    // ---- 1.13.0 : WAIT_UNTIL(condition [, delai]) - attend que la condition (un BOOL, revue a chaque
    // reprise) devienne vraie. Employe (IF, affectation, argument) : rend TRUE devenue vraie, FALSE au
    // delai, sans erreur. Seul en instruction : un delai depasse LEVE E_ATTENTE (rattrapable par TRY).
    // Sans delai : attend indefiniment (IHM_SCRIPT_ARRETER l'arrete). Reutilise le point d'attente de
    // WAIT (env_.wait) : une tache detachee rend la main entre deux essais, l'IHM respire.
    core::Result<RV> waitUntilNative(const Expr& e) {
        const bool ignored = waitUntilIgnored_;         // sa valeur est-elle employee ?
        waitUntilIgnored_ = false;
        const Expr& condExpr = *e.arguments[0].second;
        // Le delai (facultatif) : un TIME ou un nombre de millisecondes >= 0 ; absent : -1 (sans fin).
        std::int64_t delayMs = -1;
        if (e.arguments.size() > 1) {
            auto d = rich(*e.arguments[1].second);
            if (!d) return d;
            const Type t = d->o ? Type::Unknown : d->v.type();
            if (t == Type::Time) delayMs = d->v.asInteger();
            else if (isNumeric(t) && t != Type::Bool) delayMs = static_cast<std::int64_t>(std::llround(d->v.asReal()));
            else return dialectError("WAIT_UNTIL : le d\xC3\xA9lai est une dur\xC3\xA9" "e (T#2m, ou des millisecondes), pas "
                                     + (d->o ? "un " + typeNameOf(*d) : d->v.display()));
            if (delayMs < 0) delayMs = 0;
        }
        // La condition, revue a chaque tour.
        const auto evalCond = [&]() -> core::Result<bool> {
            auto c = rich(condExpr);
            if (!c) return core::Err<core::Error>(c.error());
            if (c->o || c->v.type() != Type::Bool)
                return dialectError("WAIT_UNTIL : la condition doit \xC3\xAAtre un BOOL, pas "
                                    + (c->o ? "un " + typeNameOf(*c) : std::string(toString(c->v.type()))));
            return c->v.isTruthy();
        };
        const bool detached = env_.detachedTask();
        const double start = env_.scriptNow();
        constexpr std::int64_t kPollMs = 20;             // le pas entre deux essais d'une tache detachee
        constexpr std::int64_t kBlockingCapMs = 10'000;  // non detache : plafonne a 10 s, comme WAIT
        for (;;) {
            auto ok = evalCond();
            if (!ok) return core::Err<core::Error>(ok.error());
            if (*ok) return RV{Value::boolean(true), nullptr};
            if (!detached) {
                // Non detache : l'IHM est figee, la condition ne bougera pas - on attend une fois
                // (plafonnee a 10 s comme WAIT), on reessaie, puis c'est le delai.
                std::int64_t waitMs = delayMs < 0 ? kBlockingCapMs : std::min<std::int64_t>(delayMs, kBlockingCapMs);
                if (waitMs > 0) {
                    std::string why;
                    if (!env_.wait(waitMs, why)) {
                        if (why.empty()) { aborted_ = true; return core::fail(core::ErrorCode::InvalidArgument, "arr\xC3\xAAt\xC3\xA9 pendant WAIT_UNTIL"); }
                        return dialectError("WAIT_UNTIL : " + why);
                    }
                    auto again = evalCond();
                    if (!again) return core::Err<core::Error>(again.error());
                    if (*again) return RV{Value::boolean(true), nullptr};
                }
                return timedOut(ignored, delayMs, e.line);
            }
            // Detache : le delai se mesure sur l'heure du script (now_), qui avance entre deux reprises.
            if (delayMs >= 0 && (env_.scriptNow() - start) * 1000.0 >= static_cast<double>(delayMs))
                return timedOut(ignored, delayMs, e.line);
            std::string why;
            if (!env_.wait(kPollMs, why)) {
                if (why.empty()) { aborted_ = true; return core::fail(core::ErrorCode::InvalidArgument, "arr\xC3\xAAt\xC3\xA9 pendant WAIT_UNTIL"); }
                return dialectError("WAIT_UNTIL : " + why);
            }
        }
    }

    // 1.13.0 : un WAIT_UNTIL dont le delai est depasse : FALSE quand sa valeur est employee,
    // sinon (seul en instruction) l'erreur E_ATTENTE (rattrapable par TRY).
    core::Result<RV> timedOut(bool ignored, std::int64_t delayMs, std::uint32_t line) {
        if (!ignored) return RV{Value::boolean(false), nullptr};
        const std::string msg = "la condition n'est pas devenue vraie en " + delayText(delayMs);
        raise(Caught{true, "E_ATTENTE", msg, line});
        return core::fail(core::ErrorCode::InvalidArgument, "E_ATTENTE");
    }

    // 1.13.0 : un delai pour un message (T#2m -> "2 min", 5000 -> "5 s", 500 -> "500 ms").
    static std::string delayText(std::int64_t ms) {
        if (ms <= 0) return "0 ms";
        if (ms % 60000 == 0) return std::to_string(ms / 60000) + " min";
        if (ms % 1000 == 0) return std::to_string(ms / 1000) + " s";
        return std::to_string(ms) + " ms";
    }

    core::Result<RV> fileNative(const std::string& u, const Expr& e) {
        std::vector<std::string> a;
        for (std::size_t k = 0; k < e.arguments.size(); ++k) {
            const Expr& x = *e.arguments[k].second;
            if (u == "FILE_WRITE_LINES" && k == 1) {             // une LIST, un VECTOR, un ARRAY de valeurs simples
                auto held = objArg(x);
                if (!held) return core::Err<core::Error>(held.error());
                const Obj* list = held->isScalar ? nullptr : held->obj.get();
                if (!list || !list->type || (list->type->kind != TypeDesc::Kind::List && list->type->kind != TypeDesc::Kind::Array))
                    return dialectError(u + " : une liste de textes est attendue (LIST, VECTOR ou ARRAY), pas "
                                        + (list && list->type ? list->type->text() : std::string(toString(held->scalar.type()))));
                std::string all;
                for (const auto& item : list->items) {
                    if (!item || !item->type || item->type->kind != TypeDesc::Kind::Scalar)
                        return dialectError(u + " : une ligne est une valeur simple, pas un " + (item && item->type ? item->type->text() : std::string("?")));
                    all += (item->value.type() == Type::String ? item->value.asString() : item->value.display()) + "\n";
                }
                a.push_back(std::move(all));
                continue;
            }
            auto v = rich(x);
            if (!v) return v;
            if (v->o) return dialectError(u + " : un texte est attendu, pas un " + typeNameOf(*v));
            if (u == "DIR_DELETE" && k == 1) {
                a.push_back(v->v.isTruthy() ? "1" : "0");
                continue;
            }
            a.push_back(v->v.type() == Type::String ? v->v.asString() : v->v.display());
        }
        const auto text = [](std::string t) { return core::Result<RV>(RV{Value::text(std::move(t)), nullptr}); };
        const auto list = [](const std::vector<std::string>& lines) {
            auto out = makeObj(listType(scalarType(Type::String)));
            for (const auto& l : lines) {
                auto item = makeObj(scalarType(Type::String));
                item->value = Value::text(l);
                out->items.push_back(item);
            }
            return core::Result<RV>(RV{{}, out});
        };
        if (u == "FILE_ERROR") return text(fileError_);
        if (u.rfind("PATH_", 0) == 0) {
            const auto sep = [](char c) { return c == '/' || c == '\\'; };
            if (u == "PATH_JOIN") {
                const std::string& dir = a[0];
                const std::string& name = a[1];
                const bool whole = !name.empty() && (sep(name[0]) || (name.size() > 1 && name[1] == ':'));
                if (whole || dir.empty()) return text(name);
                if (name.empty()) return text(dir);
                return text(sep(dir.back()) ? dir + name : dir + "/" + name);
            }
            std::string p = a[0];
            while (p.size() > 1 && sep(p.back()) && !(p.size() == 3 && p[1] == ':')) p.pop_back();    // 'C:/Donnees/' : 'C:/Donnees'
            std::size_t cut = std::string::npos;
            for (std::size_t k = p.size(); k-- > 0;)
                if (sep(p[k])) { cut = k; break; }
            const std::string name = cut == std::string::npos ? p : p.substr(cut + 1);
            if (u == "PATH_NAME") return text(name);
            if (u == "PATH_PARENT") {
                if (cut == std::string::npos) return text({});
                if (cut == 0) return text(p.substr(0, 1));                                   // '/journal.csv' : '/'
                if (cut == 2 && p[1] == ':') return text(p.substr(0, 3));                    // 'C:/x.csv' : 'C:/'
                return text(p.substr(0, cut));
            }
            const auto dot = name.rfind('.');
            const bool ext = dot != std::string::npos && dot > 0 && name != "..";             // '.ini' seul : un nom, sans extension
            if (u == "PATH_EXTENSION") return text(ext ? name.substr(dot) : std::string{});
            return text(ext ? name.substr(0, dot) : name);                                    // PATH_STEM
        }
        Environment::FileResult r;
        std::string why;
        const bool ok = env_.fileOp(u, a, r, why);
        fileError_ = ok ? std::string{} : u + " : " + (why.empty() ? std::string("refus\xC3\xA9") : why);
        if (u == "FILE_READ" || u == "RESOURCE_READ" || u == "DIR_DATA") return text(ok ? r.text : std::string{});
        if (u == "FILE_READ_LINES" || u == "RESOURCE_LINES" || u == "DIR_LIST" || u == "RESOURCE_LIST")
            return list(ok ? r.lines : std::vector<std::string>{});
        if (u == "FILE_SIZE" || u == "RESOURCE_SIZE") return RV{Value::integer(Type::LInt, ok ? r.number : -1), nullptr};
        if (u == "FILE_TIME") return RV{Value::integer(Type::Dt, ok ? r.number : 0), nullptr};
        return RV{Value::boolean(ok && r.number != 0), nullptr};
    }

    // ---- LIST_TRANSFORM(SOURCE, DESTINATION, OP) : DESTINATION videe, puis un resultat par element ; rend leur nombre.
    // OP : une TRANSFORMATION (TRANSFORMATION#Carre...), ou le nom d'une fonction (du script, du projet) a un parametre.
    core::Result<RV> transformList(const std::string& u, const ObjArg& src, const Expr& destination, const Expr& opx, std::uint32_t line) {
        auto dst = seqArg(u, destination, false);          // une LIST ou un VECTOR : un ARRAY a une taille fixe
        if (!dst) return core::Err<core::Error>(dst.error());
        if (dst->constant) return constantRefused(u);
        Obj& D = *dst->obj;
        if (isSetType(*D.type)) return setRefused(u);
        const std::string want = u + " : OP est une TRANSFORMATION (TRANSFORMATION#Carre\xE2\x80\xA6) ou le nom d'une fonction du projet ('VersKelvin'), pas ";
        if (opx.kind == Expr::Kind::Reference && opx.name.find('#') != std::string::npos
            && upperOf(opx.name.substr(0, opx.name.find('#'))) != "TRANSFORMATION")
            return dialectError(want + opx.name);
        auto op = rich(opx);
        if (!op) return op;
        const std::vector<ObjRef> from = src.obj->items;      // une copie des pointeurs : SOURCE et DESTINATION peuvent etre la meme
        std::vector<ObjRef> made;
        made.reserve(from.size());
        if (!op->o && op->v.type() == Type::String) {
            for (const auto& x : from) {
                auto r = callNamed(u, op->v.asString(), valueOfItem(x), line);
                if (!r) return r;
                auto m = fitTo(u, D.type->element, *r);
                if (!m) return core::Err<core::Error>(m.error());
                made.push_back(*m);
            }
        } else if (!op->o && isInteger(op->v.type()) && op->v.type() != Type::Time && op->v.asInteger() >= 0 && op->v.asInteger() <= 5) {
            const int tr = static_cast<int>(op->v.asInteger());
            for (const auto& x : from) {
                if (!x || !x->type || x->type->kind != TypeDesc::Kind::Scalar || !isNumeric(x->value.type()) || x->value.type() == Type::Time)
                    return dialectError(u + " : une TRANSFORMATION calcule sur des nombres, pas sur un " + src.obj->type->text());
                auto m = fitTo(u, D.type->element, RV{transform(tr, x->value), nullptr});
                if (!m) return core::Err<core::Error>(m.error());
                made.push_back(*m);
            }
        } else {
            return dialectError(want + (op->o ? "un " + typeNameOf(*op) : op->v.display()));
        }
        D.items = std::move(made);
        normalizeList(D);                                     // un RING : les N derniers
        auto c = commitArg(u, *dst);
        if (!c) return core::Err<core::Error>(c.error());
        return RV{Value::integer(Type::DInt, static_cast<std::int64_t>(from.size())), nullptr};
    }
    // Une fonction par son nom, appelee avec une valeur : une fonction interne du script, ou une fonction IHM du
    // projet (ses surcharges : celle qui prend cette valeur). Ce qu'elle rend.
    core::Result<RV> callNamed(const std::string& u, std::string name, const RV& x, std::uint32_t line) {
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(0, 1);
        if (name.empty()) return dialectError(u + " : OP est vide \xE2\x80\x94 le nom d'une fonction du projet ('VersKelvin') ou TRANSFORMATION#Carre\xE2\x80\xA6");
        ArgShape shape;
        shape.type = x.o ? typeNameOf(x) : valueTypeText(x.v.type());
        shape.text = x.o ? typeNameOf(x) : x.v.display();
        if (const auto inner = findFunctions(name); !inner.empty()) {
            std::size_t k = 0;
            if (inner.size() > 1) {
                std::string why;
                const int chosen = env_.chooseInner(inner, {shape}, why);
                if (chosen < 0) return dialectError(u + " : " + why);
                k = static_cast<std::size_t>(chosen);
            }
            return runFunction(*inner[k], {Bound{x, {}, false}}, line, false);
        }
        std::string key = name;
        const int count = env_.overloads(name);
        if (count > 1) {
            std::string why;
            if (!env_.chooseOverload(name, {shape}, key, why)) return dialectError(u + " : " + why);
        }
        if (auto f = env_.dialectFunction(key)) return runFunction(*f, {Bound{x, {}, false}}, line, true);
        if (count == 0)
            return dialectError(u + " : " + name + " n'est pas une fonction du projet \xE2\x80\x94 OP : TRANSFORMATION#Carre\xE2\x80\xA6 ou le nom d'une fonction IHM du projet \xC3\xA0 un param\xC3\xA8tre");
        if (x.o) return dialectError(u + " : " + name + " prend une valeur simple, pas un " + typeNameOf(x));
        Value result;
        if (!env_.call(key, key, {{std::string{}, x.v}}, result)) return dialectError(u + " : l'appel de " + name + " a \xC3\xA9" "chou\xC3\xA9");
        return RV{result, nullptr};
    }

    // ---- les templates de classe : NUMERIC_LIMITS<T>.MEMBRE, NUMBERS[<T>].CONSTANTE ------
    // Vrai : le nom (avec son type) est celui d'un template de classe, sans variable du meme nom.
    bool templateClassName(const std::string& name, std::string& base, std::string& type) {
        const std::string u = upperOf(name);
        const auto lt = u.find('<');
        base = lt == std::string::npos ? u : u.substr(0, lt);
        type = lt == std::string::npos || u.back() != '>' ? std::string{} : u.substr(lt + 1, u.size() - lt - 2);
        if (base != "NUMERIC_LIMITS" && base != "NUMBERS") return false;
        if (lt != std::string::npos) return true;
        // NUMBERS seul : seulement s'il n'est pas une variable (ce qui tournait en 1.12.6 tourne pareil).
        return !findLocal(name) && !env_.exists(name) && env_.declaredType(name).empty() && !env_.richVariable(name);
    }
    std::optional<core::Result<RV>> templateMemberValue(const Expr& e, bool called) {
        if (e.kind != Expr::Kind::Member || !e.lhs || e.lhs->kind != Expr::Kind::Reference) return std::nullopt;
        std::string base, type;
        if (!templateClassName(e.lhs->name, base, type)) return std::nullopt;
        const auto* t = lib::templateInfo(base);
        if (!t) return std::nullopt;
        if (type.empty() && t->required) return core::Result<RV>(dialectError(std::string(t->hint)));
        if (type.empty()) type = std::string(t->dflt);
        const std::string head = base + (type.empty() ? std::string{} : "<" + type + ">");
        const auto* m = lib::templateMember(base, e.name);
        if (!m) {
            std::vector<std::string> pool;
            for (const auto& x : base == "NUMBERS" ? lib::numbersMembers() : lib::limitsMembers()) pool.emplace_back(x.name);
            return core::Result<RV>(dialectError((base == "NUMBERS" && e.lhs->name.find('<') == std::string::npos ? base : head) + " n'a pas de membre "
                                                 + e.name + lib::closest(e.name, pool)));
        }
        if (m->function && !called) return core::Result<RV>(dialectError(std::string(m->name) + " est une fonction : " + head + "." + std::string(m->name) + "()"));
        if (!m->function && called) return core::Result<RV>(dialectError(std::string(m->name) + " est une constante : sans parenth\xC3\xA8ses"));
        Value out;
        const bool ok = base == "NUMBERS" ? lib::numbersValue(type, m->name, out) : lib::limitsValue(type, m->name, out);
        if (!ok) return core::Result<RV>(dialectError(head + " : " + std::string(t->want)));
        return core::Result<RV>(RV{out, nullptr});
    }

    bool                                    dialect_{false};
    std::vector<Frame>                      frames_;
    Locals                                  ownLocals_;
    std::uint32_t                           depth_{0};
    Flow                                    lastFlow_{};
    std::map<std::string, TypeRef>          structs_;
    std::map<std::string, TypeRef>          typeTexts_;
    std::vector<std::map<std::string, TypeRef>> typeBindings_;  // 1.12.19 : les types generiques des appels en cours
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
    int                              quiet_{0};       // 1.12.1 : a ?? b, le cote a en cours
    // 1.12.1 : TRY ... CATCH - la profondeur, l'erreur gardee (la premiere).
    int                              catching_{0};
    Caught                           caught_{};
    // 1.13.0 : les erreurs des CATCH en cours (empilees) - RELEVER reemet celle du dessus.
    std::vector<Caught>              handling_;
    // 1.13.0 : WAIT_UNTIL seul en instruction (sa valeur est ignoree) : un delai depasse LEVE
    // E_ATTENTE ; employe (IF, affectation, argument) : il rend FALSE sans erreur.
    bool                             waitUntilIgnored_{false};
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

std::vector<ParamInfo> functionParams(const Function& f) {
    std::vector<ParamInfo> out;
    for (const auto& p : f.params) {
        ParamInfo i;
        i.mode = p.mode == VarDecl::Mode::InOut ? ParamInfo::Mode::InOut : p.mode == VarDecl::Mode::Output ? ParamInfo::Mode::Output : ParamInfo::Mode::Input;
        i.name = p.name;
        i.type = p.type ? p.type->text() : std::string{};
        i.hasDefault = p.initial != nullptr;
        out.push_back(std::move(i));
    }
    return out;
}

std::string functionResult(const Function& f) { return f.result ? f.result->text() : std::string{}; }

std::uint32_t functionLine(const Function& f) noexcept { return f.line; }

int Environment::chooseInner(const std::vector<const Function*>& candidates, const std::vector<ArgShape>& args, std::string& why) {
    std::size_t positional = 0;
    for (const auto& a : args) positional += a.name.empty() ? 1 : 0;
    for (std::size_t i = 0; i < candidates.size(); ++i)
        if (candidates[i] && positional <= candidates[i]->params.size()) return static_cast<int>(i);
    why = (candidates.empty() || !candidates.front() ? std::string("fonction") : candidates.front()->name) + " : aucune ne prend "
        + std::to_string(positional) + " argument(s)";
    return -1;
}

std::vector<std::pair<std::string, std::string>> functionTypeParams(const Function& f) { return f.typeParams; }

bool mentionsTypeParam(std::string_view type, const std::vector<std::pair<std::string, std::string>>& params) {
    if (params.empty()) return false;
    std::size_t i = 0;
    while (i < type.size()) {
        if (!(std::isalpha(static_cast<unsigned char>(type[i])) || type[i] == '_')) { ++i; continue; }
        std::size_t j = i;
        while (j < type.size() && (std::isalnum(static_cast<unsigned char>(type[j])) || type[j] == '_')) ++j;
        const std::string word = upperOf(type.substr(i, j - i));
        for (const auto& p : params)
            if (upperOf(p.first) == word) return true;
        i = j;
    }
    return false;
}

std::vector<CallSite> callSites(const Program& program, Environment& typing) {
    CallWalker w(typing, &program);
    w.walkProgram(program);
    return w.finish();
}

std::vector<CallSite> callSites(const Expression& expression, Environment& typing) {
    CallWalker w(typing, nullptr);
    w.walkExpression(expression.root);
    return w.finish();
}

std::vector<std::shared_ptr<const Function>> innerFunctions(const Program& program) {
    return {program.functions.begin(), program.functions.end()};
}

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

bool readCallerLocal(std::string_view name, Value& out) { return t_caller && t_caller->readLocal(name, out); }

ObjRef makeRichValue(std::string_view type, std::string_view initial, Environment& env, std::string* why) {
    Runner runner(env, RunLimits{}, "valeur");
    return runner.richValue(type, initial, why);
}

TypeRef typeOfText(std::string_view type, Environment& env, std::string* why) {
    Runner runner(env, RunLimits{}, "type");
    return runner.typeOnly(type, why);
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

// 1.12.19 : appeler un rappel depuis l'exterieur (un minuteur, l'operation Appeler).
core::Result<Value> callCallback(const Obj& callback, Environment& env, const std::vector<Value>& args, std::string where,
                                 const RunLimits& limits) {
    Runner runner(env, limits, std::move(where));
    return runner.callFromOutside(callback, args);
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
