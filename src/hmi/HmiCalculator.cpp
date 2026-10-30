// =============================================================================
//  hmi/HmiCalculator.cpp - 1.14.1 (lot N4) : la Calculatrice. Voir HmiCalculator.hpp.
// -----------------------------------------------------------------------------
//  L'EXPRESSION : une liste de nombres, d'operateurs et de parentheses ouvertes,
//  plus le nombre en cours (tape, ou calcule : une fonction, pi, MR, un resultat).
//  Une parenthese fermee se calcule tout de suite et devient un nombre "(4 - 1)" ;
//  = calcule le reste dans l'ordre des operations (les parentheses ouvertes se
//  ferment seules). Les nombres sont des doubles (standard, scientifique) ou des
//  mots de 8 a 64 bits (programmeur : les operations debordent dans la taille du
//  mot, en complement a deux).
// =============================================================================
#include "HmiCalculator.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace hmi::calculator {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kE = 2.71828182845904523536;
constexpr std::string_view kMinus = "\xE2\x88\x92";        // le signe moins de l'afficheur
constexpr std::string_view kTimes = "\xC3\x97";
constexpr std::string_view kDivide = "\xC3\xB7";
constexpr std::string_view kRoot = "\xE2\x88\x9A";
constexpr std::string_view kThin = "\xE2\x80\xAF";         // l'espace fine insecable des milliers

std::string lowerAscii(std::string_view s) {
    std::string out;
    for (const char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string upperAscii(std::string_view s) {
    std::string out;
    for (const char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool isProgrammer(const State& s) { return s.settings.mode == Mode::Programmer; }
bool isScientific(const State& s) { return s.settings.mode == Mode::Scientific; }

std::uint64_t norm(const State& s, std::uint64_t w) { return w & maskOf(s.settings.bits); }

// La valeur d'un mot lue comme un nombre (selon le signe).
double wordAsNumber(std::uint64_t w, int bits, bool isSigned) {
    return isSigned ? static_cast<double>(signExtend(w, bits)) : static_cast<double>(w & maskOf(bits));
}

int digitOf(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Le nombre en cours de frappe, en programmeur : ses chiffres dans la base active (un "-" en tete en DEC).
std::uint64_t parseWord(std::string_view text, Base base, int bits) {
    bool negative = false;
    if (!text.empty() && text.front() == '-') {
        negative = true;
        text.remove_prefix(1);
    }
    std::uint64_t v = 0;
    const auto radix = static_cast<std::uint64_t>(baseRadix(base));
    for (const char c : text) {
        const int d = digitOf(c);
        if (d < 0 || static_cast<std::uint64_t>(d) >= radix) continue;
        v = v * radix + static_cast<std::uint64_t>(d);
    }
    if (negative) v = 0 - v;
    return v & maskOf(bits);
}

// Le chiffre `d` ajoute a `text` tient-il dans le mot ? (HEX, OCT, BIN : les bits ; DEC : la valeur, signee ou non.)
bool wordAccepts(std::string_view text, int d, Base base, int bits, bool isSigned) {
    bool negative = false;
    if (!text.empty() && text.front() == '-') {
        negative = true;
        text.remove_prefix(1);
    }
    const auto radix = static_cast<std::uint64_t>(baseRadix(base));
    // DEC signe : 32767 au plus (WORD), -32768 au plus bas.
    const std::uint64_t half = std::uint64_t{1} << (bits - 1);
    const std::uint64_t limit = base == Base::Dec && isSigned ? (negative ? half : half - 1) : maskOf(bits);
    std::uint64_t v = 0;
    for (const char c : text) {
        const int x = digitOf(c);
        if (x < 0) continue;
        v = v * radix + static_cast<std::uint64_t>(x);
    }
    const auto ud = static_cast<std::uint64_t>(d);
    if (v > (limit - ud) / radix) return false;
    return v * radix + ud <= limit;
}

std::string group3(std::string_view digits, std::string_view sep) {
    std::string out;
    const std::size_t n = digits.size();
    for (std::size_t i = 0; i < n; ++i) {
        out += digits[i];
        const std::size_t left = n - 1 - i;
        if (left > 0 && left % 3 == 0) out += sep;
    }
    return out;
}

// "-12345,60E+3" -> "-12 345,60E+3" (les milliers de la partie entiere).
std::string groupEntry(std::string_view raw) {
    std::string sign;
    if (!raw.empty() && raw.front() == '-') {
        sign = std::string(kMinus);
        raw.remove_prefix(1);
    }
    std::size_t end = raw.find_first_of(",E");
    if (end == std::string_view::npos) end = raw.size();
    std::string out = sign + group3(raw.substr(0, end), kThin) + std::string(raw.substr(end));
    // L'exposant : "E-5" avec le vrai moins.
    if (const auto e = out.find("E-"); e != std::string::npos) out.replace(e + 1, 1, kMinus);
    return out;
}

std::string opText(std::string_view op) {
    if (op == "+") return "+";
    if (op == "-") return std::string(kMinus);
    if (op == "*") return std::string(kTimes);
    if (op == "/") return std::string(kDivide);
    if (op == "pow") return "^";
    if (op == "root") return "yroot";
    if (op == "mod") return "mod";
    if (op == "shl") return "<<";
    if (op == "shr") return ">>";
    return upperAscii(op);   // AND, OR, XOR, NAND, NOR, ROL, ROR
}

bool isBinary(std::string_view k) {
    for (const std::string_view b : {"+", "-", "*", "/", "pow", "root", "mod", "and", "or", "xor", "nand", "nor", "shl", "shr", "rol", "ror"})
        if (k == b) return true;
    return false;
}
bool binaryForMode(const State& s, std::string_view k) {
    if (k == "+" || k == "-" || k == "*" || k == "/") return true;
    if (k == "pow" || k == "root") return isScientific(s);
    if (k == "mod") return s.settings.mode != Mode::Standard;
    return isProgrammer(s) && isBinary(k);
}

bool isUnary(std::string_view k) {
    for (const std::string_view u : {"neg", "inv", "sqr", "sqrt", "%", "sin", "cos", "tan", "asin", "acos", "atan", "log", "ln",
                                     "pow10", "powe", "fact", "abs", "not", "swap"})
        if (k == u) return true;
    return false;
}
bool unaryForMode(const State& s, std::string_view k) {
    if (k == "neg") return true;
    if (isProgrammer(s)) return k == "not" || k == "swap";
    if (k == "inv" || k == "sqr" || k == "sqrt" || k == "%") return true;
    return isScientific(s) && isUnary(k) && k != "not" && k != "swap";
}

int precedence(std::string_view op, bool programmer) {
    if (programmer) {
        if (op == "or" || op == "nor") return 1;
        if (op == "xor") return 2;
        if (op == "and" || op == "nand") return 3;
        if (op == "shl" || op == "shr" || op == "rol" || op == "ror") return 4;
        if (op == "+" || op == "-") return 5;
        return 6;   // * / mod
    }
    if (op == "+" || op == "-") return 1;
    if (op == "*" || op == "/" || op == "mod") return 2;
    return 3;       // pow, root (a droite d'abord : 2^3^2 = 2^9)
}
bool rightAssoc(std::string_view op) { return op == "pow" || op == "root"; }

const std::string kDivZero = "division par z\xC3\xA9ro";
const std::string kDomain = "entr\xC3\xA9" "e hors du domaine";
const std::string kOverflow = "d\xC3\xA9passement de capacit\xC3\xA9";

struct Num {
    double        v{0};
    std::uint64_t w{0};
};

bool finiteOr(double v, std::string& err) {
    if (std::isnan(v)) { err = kDomain; return false; }
    if (std::isinf(v)) { err = kOverflow; return false; }
    return true;
}

bool applyBinary(const Settings& s, std::string_view op, const Num& a, const Num& b, Num& out, std::string& err) {
    if (s.mode == Mode::Programmer) {
        const int bits = s.bits;
        const std::uint64_t m = maskOf(bits);
        const std::uint64_t x = a.w & m, y = b.w & m;
        const std::int64_t sx = signExtend(x, bits), sy = signExtend(y, bits);
        std::uint64_t r = 0;
        if (op == "+") r = x + y;
        else if (op == "-") r = x - y;
        else if (op == "*") r = x * y;
        else if (op == "/" || op == "mod") {
            if (y == 0) { err = kDivZero; return false; }
            if (s.isSigned) {
                if (sx == std::numeric_limits<std::int64_t>::min() && sy == -1) r = op == "/" ? x : 0;
                else r = static_cast<std::uint64_t>(op == "/" ? sx / sy : sx % sy);
            } else {
                r = op == "/" ? x / y : x % y;
            }
        } else if (op == "and") r = x & y;
        else if (op == "or") r = x | y;
        else if (op == "xor") r = x ^ y;
        else if (op == "nand") r = ~(x & y);
        else if (op == "nor") r = ~(x | y);
        else if (op == "shl" || op == "shr" || op == "rol" || op == "ror") {
            const std::int64_t n = s.isSigned ? sy : static_cast<std::int64_t>(std::min<std::uint64_t>(y, 1u << 20));
            if (n < 0) { err = "d\xC3\xA9" "calage n\xC3\xA9gatif"; return false; }
            const auto u = static_cast<std::uint64_t>(n);
            if (op == "shl") r = u >= static_cast<std::uint64_t>(bits) ? 0 : x << u;
            else if (op == "shr") {
                if (s.isSigned) r = static_cast<std::uint64_t>(u >= static_cast<std::uint64_t>(bits) ? (sx < 0 ? -1 : 0) : sx >> u);
                else r = u >= static_cast<std::uint64_t>(bits) ? 0 : x >> u;
            } else {
                const auto k = static_cast<int>(u % static_cast<std::uint64_t>(bits));
                if (k == 0) r = x;
                else if (op == "rol") r = (x << k) | (x >> (bits - k));
                else r = (x >> k) | (x << (bits - k));
            }
        } else {
            err = "op\xC3\xA9rateur inconnu";
            return false;
        }
        out.w = r & m;
        out.v = wordAsNumber(out.w, bits, s.isSigned);
        return true;
    }
    const double x = a.v, y = b.v;
    double r = 0;
    if (op == "+") r = x + y;
    else if (op == "-") r = x - y;
    else if (op == "*") r = x * y;
    else if (op == "/") {
        if (y == 0) { err = kDivZero; return false; }
        r = x / y;
    } else if (op == "mod") {
        if (y == 0) { err = kDivZero; return false; }
        r = std::fmod(x, y);
    } else if (op == "pow") {
        if (x == 0 && y < 0) { err = kDivZero; return false; }
        if (x < 0 && y != std::floor(y)) { err = kDomain; return false; }
        r = std::pow(x, y);
    } else if (op == "root") {
        // x yroot y : la racine y-ieme de x.
        if (y == 0) { err = kDomain; return false; }
        if (x < 0) {
            const bool oddInteger = y == std::floor(y) && std::fmod(std::fabs(y), 2.0) == 1.0;
            if (!oddInteger) { err = kDomain; return false; }
            r = -std::pow(-x, 1.0 / y);
        } else {
            if (x == 0 && y < 0) { err = kDivZero; return false; }
            r = std::pow(x, 1.0 / y);
        }
    } else {
        err = "op\xC3\xA9rateur inconnu";
        return false;
    }
    if (!finiteOr(r, err)) return false;
    out.v = r;
    return true;
}

double toRadians(const Settings& s, double x) { return s.degrees ? x * kPi / 180.0 : x; }
double fromRadians(const Settings& s, double x) { return s.degrees ? x * 180.0 / kPi : x; }

bool applyUnary(const Settings& s, std::string_view f, const Num& a, Num& out, std::string& err) {
    if (s.mode == Mode::Programmer) {
        const std::uint64_t m = maskOf(s.bits);
        const std::uint64_t x = a.w & m;
        std::uint64_t r = x;
        if (f == "neg") r = 0 - x;
        else if (f == "not") r = ~x;
        else if (f == "swap") {
            const int half = s.bits / 2;
            r = (x << half) | (x >> half);
        } else {
            err = "fonction inconnue";
            return false;
        }
        out.w = r & m;
        out.v = wordAsNumber(out.w, s.bits, s.isSigned);
        return true;
    }
    const double x = a.v;
    double r = 0;
    if (f == "neg") r = -x;
    else if (f == "inv") {
        if (x == 0) { err = kDivZero; return false; }
        r = 1.0 / x;
    } else if (f == "sqr") r = x * x;
    else if (f == "sqrt") {
        if (x < 0) { err = kDomain; return false; }
        r = std::sqrt(x);
    } else if (f == "sin" || f == "cos" || f == "tan") {
        if (f == "tan" && s.degrees) {
            const double k = std::fmod(std::fabs(x), 180.0);
            if (k == 90.0) { err = kDomain; return false; }
        }
        const double rad = toRadians(s, x);
        r = f == "sin" ? std::sin(rad) : f == "cos" ? std::cos(rad) : std::tan(rad);
        if (std::fabs(r) < 1e-15) r = 0;   // sin(180) : 0, pas 1,2E-16
    } else if (f == "asin" || f == "acos") {
        if (x < -1 || x > 1) { err = kDomain; return false; }
        r = fromRadians(s, f == "asin" ? std::asin(x) : std::acos(x));
    } else if (f == "atan") r = fromRadians(s, std::atan(x));
    else if (f == "log" || f == "ln") {
        if (x <= 0) { err = kDomain; return false; }
        r = f == "log" ? std::log10(x) : std::log(x);
    } else if (f == "pow10") r = std::pow(10.0, x);
    else if (f == "powe") r = std::exp(x);
    else if (f == "fact") {
        if (x < 0 || std::fabs(x - std::round(x)) > 1e-9) { err = kDomain; return false; }
        const double n = std::round(x);
        if (n > 170) { err = kOverflow; return false; }
        r = 1;
        for (int k = 2; k <= static_cast<int>(n); ++k) r *= k;
    } else if (f == "abs") r = std::fabs(x);
    else {
        err = "fonction inconnue";
        return false;
    }
    if (!finiteOr(r, err)) return false;
    out.v = r;
    return true;
}

// L'expression (sans parenthese fermee : celles qui restent ouvertes se ferment a la fin), dans l'ordre des
// operations. Un operateur en fin d'expression (sans son second nombre) est ignore.
bool evaluate(const Settings& s, const std::vector<Token>& tokens, Num& out, std::string& err) {
    const bool prog = s.mode == Mode::Programmer;
    std::vector<Num> vals;
    std::vector<std::string> ops;     // "(" : une parenthese ouverte
    const auto reduce = [&]() -> bool {
        const std::string op = ops.back();
        ops.pop_back();
        if (op == "(") return true;
        if (vals.size() < 2) { err = "expression incompl\xC3\xA8te"; return false; }
        const Num b = vals.back();
        vals.pop_back();
        const Num a = vals.back();
        vals.pop_back();
        Num r;
        if (!applyBinary(s, op, a, b, r, err)) return false;
        vals.push_back(r);
        return true;
    };
    // Un operateur pendant (en fin d'expression, ou juste avant une parenthese jamais remplie) : ignore.
    std::vector<Token> list = tokens;
    while (!list.empty() && list.back().type != Token::Type::Number) list.pop_back();
    for (const auto& t : list) {
        switch (t.type) {
            case Token::Type::Number: vals.push_back({t.value, t.word}); break;
            case Token::Type::Open: ops.emplace_back("("); break;
            case Token::Type::Op:
                while (!ops.empty() && ops.back() != "(") {
                    const int top = precedence(ops.back(), prog), me = precedence(t.op, prog);
                    if (top > me || (top == me && !rightAssoc(t.op))) {
                        if (!reduce()) return false;
                    } else {
                        break;
                    }
                }
                ops.push_back(t.op);
                break;
        }
    }
    while (!ops.empty())
        if (!reduce()) return false;
    if (vals.size() != 1) {
        if (vals.empty()) {
            out = {};
            return true;
        }
        err = "expression incompl\xC3\xA8te";
        return false;
    }
    out = vals.back();
    return true;
}

std::string tokensText(const std::vector<Token>& tokens) {
    std::string s;
    for (const auto& t : tokens) {
        if (t.type == Token::Type::Number) s += t.text;
        else if (t.type == Token::Type::Open) s += "(";
        else s += " " + opText(t.op) + " ";
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

// Le texte d'un nombre dans l'expression : a la francaise (standard), dans la base active (programmeur).
std::string numberText(const State& s, double v, std::uint64_t w) {
    if (isProgrammer(s)) {
        const std::string t = baseText(w, s.settings.base, s.settings.bits, s.settings.isSigned, false);
        return !t.empty() && t.front() == '-' ? std::string(kMinus) + t.substr(1) : t;
    }
    return formatNumber(v, s.settings.decimals);
}

Token numberToken(const State& s, double v, std::uint64_t w, bool plain) {
    Token t;
    t.type = Token::Type::Number;
    t.value = v;
    t.word = w;
    t.plain = plain;
    t.text = numberText(s, v, w);
    return t;
}

Token entryToken(const State& s) {
    Token t;
    t.type = Token::Type::Number;
    t.plain = true;
    if (isProgrammer(s)) {
        t.word = parseWord(s.entry, s.settings.base, s.settings.bits);
        t.value = wordAsNumber(t.word, s.settings.bits, s.settings.isSigned);
        t.text = numberText(s, t.value, t.word);
    } else {
        double v = 0;
        (void)parseEntry(s.entry, v);
        t.value = v;
        t.text = groupEntry(s.entry);
        // "12," tape : "12" dans l'expression.
        while (!t.text.empty() && t.text.back() == ',') t.text.pop_back();
        if (t.text.empty() || t.text == std::string(kMinus)) t.text = "0";
    }
    return t;
}

// Le nombre en cours (tape ou calcule) passe dans l'expression ; faux : il n'y en a pas.
bool commitCurrent(State& s) {
    if (!s.entry.empty()) {
        s.tokens.push_back(entryToken(s));
        s.entry.clear();
        return true;
    }
    if (s.hasOperand) {
        s.tokens.push_back(s.operand);
        s.hasOperand = false;
        return true;
    }
    return false;
}

// Le dernier nombre de l'expression (ce que montre l'afficheur apres un operateur).
const Token* lastNumber(const State& s) {
    for (auto it = s.tokens.rbegin(); it != s.tokens.rend(); ++it)
        if (it->type == Token::Type::Number) return &*it;
    return nullptr;
}

// Le nombre sur lequel agit une fonction (et ce qu'il devient : il quitte la frappe).
Token takeCurrent(State& s) {
    if (!s.entry.empty()) {
        Token t = entryToken(s);
        s.entry.clear();
        return t;
    }
    if (s.hasOperand) {
        s.hasOperand = false;
        return s.operand;
    }
    if (const Token* t = lastNumber(s)) {
        Token copy = *t;
        copy.plain = true;
        copy.text = numberText(s, copy.value, copy.word);
        return copy;
    }
    return numberToken(s, 0, 0, true);
}

void leaveEquals(State& s) {
    if (!s.afterEquals) return;
    s.afterEquals = false;
    s.equalsLine.clear();
}

void clearAll(State& s) {
    s.tokens.clear();
    s.entry.clear();
    s.hasOperand = false;
    s.operand = {};
    s.afterEquals = false;
    s.equalsLine.clear();
    s.repeatOp.clear();
    s.error.clear();
    s.second = false;
}

std::string functionText(std::string_view f, const std::string& inner) {
    if (f == "neg") return std::string(kMinus) + "(" + inner + ")";
    if (f == "inv") return "1/(" + inner + ")";
    if (f == "sqr") return "sqr(" + inner + ")";
    if (f == "sqrt") return std::string(kRoot) + "(" + inner + ")";
    if (f == "pow10") return "10^(" + inner + ")";
    if (f == "powe") return "e^(" + inner + ")";
    if (f == "not") return "NOT(" + inner + ")";
    if (f == "swap") return "SWAP(" + inner + ")";
    return std::string(f) + "(" + inner + ")";   // sin, asin, log, ln, fact, abs
}

bool fail(State& s, const std::string& why, const std::string& line) {
    s.error = why;
    s.equalsLine = line;
    s.tokens.clear();
    s.entry.clear();
    s.hasOperand = false;
    s.afterEquals = false;
    return true;
}

bool pressUnary(State& s, std::string f) {
    if (!unaryForMode(s, f)) return false;
    if (s.settings.mode == Mode::Scientific && s.second) {
        if (f == "sin") f = "asin";
        else if (f == "cos") f = "acos";
        else if (f == "tan") f = "atan";
    }
    // +/- sur le nombre qu'on tape : son signe (ou celui de son exposant), sans le passer en fonction.
    if (f == "neg" && !s.entry.empty()) {
        if (isProgrammer(s)) {
            const std::uint64_t w = parseWord(s.entry, s.settings.base, s.settings.bits);
            s.entry = baseText(norm(s, 0 - w), s.settings.base, s.settings.bits, s.settings.isSigned, false);
            return true;
        }
        if (const auto e = s.entry.find('E'); e != std::string::npos && e + 1 < s.entry.size()) {
            s.entry[e + 1] = s.entry[e + 1] == '-' ? '+' : '-';
            return true;
        }
        if (s.entry.front() == '-') s.entry.erase(0, 1);
        else s.entry.insert(0, "-");
        return true;
    }
    const bool hadEquals = s.afterEquals;
    leaveEquals(s);
    Token target = takeCurrent(s);
    // Le pourcentage : a + b % -> b % de a ; a x b % -> b / 100.
    if (f == "%") {
        double base = 1.0, b = target.value;
        if (!s.tokens.empty() && s.tokens.back().type == Token::Type::Op) {
            const std::string op = s.tokens.back().op;
            if (op == "+" || op == "-") {
                std::vector<Token> left(s.tokens.begin(), s.tokens.end() - 1);
                Num l;
                std::string err;
                if (!evaluate(s.settings, left, l, err)) return fail(s, err, tokensText(s.tokens));
                base = l.v;
            }
        }
        const double r = base * b / 100.0;
        s.operand = numberToken(s, r, 0, false);
        s.hasOperand = true;
        return true;
    }
    Num out;
    std::string err;
    if (!applyUnary(s.settings, f, {target.value, target.word}, out, err)) {
        const std::string line = tokensText(s.tokens);
        return fail(s, err, (line.empty() ? std::string{} : line + " ") + functionText(f, target.text));
    }
    if (hadEquals) s.tokens.clear();
    Token t;
    t.type = Token::Type::Number;
    t.value = out.v;
    t.word = out.w;
    t.text = functionText(f, target.text);
    t.plain = false;
    s.operand = t;
    s.hasOperand = true;
    if (f == "asin" || f == "acos" || f == "atan") s.second = false;
    return true;
}

bool pressDigit(State& s, char d) {
    const int v = digitOf(d);
    if (v < 0) return false;
    if (isProgrammer(s)) {
        if (v >= baseRadix(s.settings.base)) return false;
    } else if (v >= 10) {
        return false;
    }
    if (!s.error.empty()) clearAll(s);
    leaveEquals(s);
    s.hasOperand = false;
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(d)));
    if (isProgrammer(s)) {
        std::string next = s.entry;
        const bool negative = !next.empty() && next.front() == '-';
        std::string digits = negative ? next.substr(1) : next;
        if (digits == "0") digits.clear();
        if (!wordAccepts(digits, v, s.settings.base, s.settings.bits, s.settings.isSigned)) return false;
        digits += c;
        s.entry = (negative ? "-" : "") + digits;
        return true;
    }
    std::string& e = s.entry;
    if (const auto at = e.find('E'); at != std::string::npos) {
        // L'exposant : trois chiffres au plus.
        if (e.size() - (at + 2) >= 3) return false;
        e += c;
        return true;
    }
    std::size_t count = 0;
    for (const char x : e) count += std::isdigit(static_cast<unsigned char>(x)) ? 1 : 0;
    if (count >= 16) return false;
    if (e == "0") e.clear();
    else if (e == "-0") e = "-";
    e += c;
    return true;
}

bool pressComma(State& s) {
    if (isProgrammer(s)) return false;
    if (!s.error.empty()) clearAll(s);
    leaveEquals(s);
    s.hasOperand = false;
    if (s.entry.find(',') != std::string::npos || s.entry.find('E') != std::string::npos) return false;
    if (s.entry.empty() || s.entry == "-") s.entry += "0";
    s.entry += ',';
    return true;
}

bool pressExp(State& s) {
    if (!isScientific(s) || !s.error.empty()) return false;
    if (s.entry.find('E') != std::string::npos) return false;
    if (s.entry.empty()) {
        // Le nombre montre devient la mantisse (un resultat, pi...).
        const double v = value(s);
        leaveEquals(s);
        if (s.hasOperand) s.hasOperand = false;
        std::string m = formatNumber(v, s.settings.decimals, false);
        if (m.find('E') != std::string::npos) m = "1";
        // le moins de l'afficheur redevient "-"
        if (m.rfind(kMinus, 0) == 0) m = "-" + m.substr(kMinus.size());
        s.entry = m;
    }
    if (!s.entry.empty() && s.entry.back() == ',') s.entry.pop_back();
    s.entry += "E+";
    return true;
}

bool pressBack(State& s) {
    if (!s.error.empty()) {
        clearAll(s);
        return true;
    }
    if (s.entry.empty()) return false;
    s.entry.pop_back();
    if (s.entry.size() >= 1 && (s.entry.back() == '+' || s.entry.back() == '-') && s.entry.size() >= 2 && s.entry[s.entry.size() - 2] == 'E')
        s.entry.resize(s.entry.size() - 2);
    if (s.entry == "-") s.entry.clear();
    if (s.entry.empty()) s.entry = "0";
    return true;
}

bool pressBinary(State& s, const std::string& op) {
    if (!binaryForMode(s, op) || !s.error.empty()) return false;
    leaveEquals(s);
    if (!commitCurrent(s)) {
        if (s.tokens.empty()) s.tokens.push_back(numberToken(s, 0, 0, true));
        else if (s.tokens.back().type == Token::Type::Op) {
            s.tokens.back().op = op;   // un autre operateur remplace le precedent
            return true;
        } else if (s.tokens.back().type == Token::Type::Open) {
            s.tokens.push_back(numberToken(s, 0, 0, true));
        }
    }
    Token t;
    t.type = Token::Type::Op;
    t.op = op;
    s.tokens.push_back(t);
    return true;
}

bool pressOpen(State& s) {
    if (s.settings.mode == Mode::Standard || !s.error.empty()) return false;
    if (s.afterEquals) {
        leaveEquals(s);
        s.hasOperand = false;      // ( apres = : une nouvelle expression
    }
    if (commitCurrent(s)) {
        Token times;
        times.type = Token::Type::Op;
        times.op = "*";
        s.tokens.push_back(times);   // 2(3 + 1) : 2 x (3 + 1)
    }
    Token t;
    t.type = Token::Type::Open;
    s.tokens.push_back(t);
    return true;
}

int openCount(const State& s) {
    int n = 0;
    for (const auto& t : s.tokens) n += t.type == Token::Type::Open ? 1 : 0;
    return n;
}

bool pressClose(State& s) {
    if (s.settings.mode == Mode::Standard || !s.error.empty() || openCount(s) == 0) return false;
    if (!commitCurrent(s)) {
        if (s.tokens.back().type == Token::Type::Open) s.tokens.push_back(numberToken(s, 0, 0, true));
        else if (s.tokens.back().type == Token::Type::Op) {
            const Token* last = lastNumber(s);
            s.tokens.push_back(last ? *last : numberToken(s, 0, 0, true));
        }
    }
    std::size_t open = s.tokens.size();
    while (open > 0 && s.tokens[open - 1].type != Token::Type::Open) --open;
    if (open == 0) return false;
    const std::vector<Token> inner(s.tokens.begin() + static_cast<std::ptrdiff_t>(open), s.tokens.end());
    Num r;
    std::string err;
    const std::string innerText = tokensText(inner);
    if (!evaluate(s.settings, inner, r, err)) return fail(s, err, tokensText(s.tokens) + ")");
    s.tokens.resize(open - 1);
    Token t;
    t.type = Token::Type::Number;
    t.value = r.v;
    t.word = r.w;
    t.text = "(" + innerText + ")";
    s.operand = t;
    s.hasOperand = true;
    return true;
}

bool pressEquals(State& s) {
    if (!s.error.empty()) return false;
    if (s.afterEquals && s.hasOperand && !s.repeatOp.empty()) {
        // = encore : le resultat, la derniere operation, son second nombre.
        Token result = s.operand;
        result.text = numberText(s, result.value, result.word);
        Token op;
        op.type = Token::Type::Op;
        op.op = s.repeatOp;
        s.tokens = {result, op, s.repeatOperand};
    } else {
        leaveEquals(s);
        if (!commitCurrent(s)) {
            if (s.tokens.empty()) s.tokens.push_back(numberToken(s, 0, 0, true));
            else if (s.tokens.back().type == Token::Type::Op) {
                // 5 + = : 5 + 5 (le dernier nombre).
                const Token* last = lastNumber(s);
                s.tokens.push_back(last ? *last : numberToken(s, 0, 0, true));
            } else if (s.tokens.back().type == Token::Type::Open) {
                s.tokens.push_back(numberToken(s, 0, 0, true));
            }
        }
        s.repeatOp.clear();
        const std::size_t n = s.tokens.size();
        if (openCount(s) == 0 && n >= 3 && s.tokens[n - 2].type == Token::Type::Op && s.tokens[n - 1].type == Token::Type::Number) {
            s.repeatOp = s.tokens[n - 2].op;
            s.repeatOperand = s.tokens[n - 1];
        }
    }
    std::string line = tokensText(s.tokens);
    for (int k = openCount(s); k > 0; --k) line += ")";
    Num r;
    std::string err;
    if (!evaluate(s.settings, s.tokens, r, err)) return fail(s, err, line);
    s.tokens.clear();
    s.entry.clear();
    s.operand = numberToken(s, r.v, r.w, true);
    s.hasOperand = true;
    s.afterEquals = true;
    s.equalsLine = line + " =";
    return true;
}

bool pressMemory(State& s, std::string_view k) {
    if (k == "MC") {
        if (!s.hasMemory) return false;
        s.memory = 0;
        s.memoryWord = 0;
        s.hasMemory = false;
        return true;
    }
    if (k == "MR") {
        if (!s.hasMemory) return false;
        if (!s.error.empty()) clearAll(s);
        leaveEquals(s);
        s.entry.clear();
        s.operand = numberToken(s, isProgrammer(s) ? wordAsNumber(s.memoryWord, s.settings.bits, s.settings.isSigned) : s.memory,
                                norm(s, s.memoryWord), true);
        s.hasOperand = true;
        return true;
    }
    if (!s.error.empty()) return false;
    const double v = value(s);
    const std::uint64_t w = wordValue(s);
    const bool plus = k == "M+";
    if (isProgrammer(s)) {
        s.memoryWord = norm(s, plus ? s.memoryWord + w : s.memoryWord - w);
        s.memory = wordAsNumber(s.memoryWord, s.settings.bits, s.settings.isSigned);
    } else {
        s.memory = plus ? s.memory + v : s.memory - v;
    }
    s.hasMemory = true;
    // Le nombre tape est garde ; le suivant le remplace (comme un resultat).
    if (!s.entry.empty()) {
        s.operand = entryToken(s);
        s.hasOperand = true;
        s.entry.clear();
    }
    return true;
}

bool pressConstant(State& s, std::string_view k) {
    if (!isScientific(s)) return false;
    if (!s.error.empty()) clearAll(s);
    const bool hadEquals = s.afterEquals;
    leaveEquals(s);
    if (hadEquals) s.tokens.clear();
    s.entry.clear();
    Token t;
    t.type = Token::Type::Number;
    t.value = k == "pi" ? kPi : kE;
    t.text = k == "pi" ? "\xCF\x80" : "e";
    s.operand = t;
    s.hasOperand = true;
    return true;
}

// Les touches de chaque mode (le pave et la rangee du haut), avec leurs libelles.
struct KeyDef {
    const char* id;
    const char* label;
    const char* sup;
    const char* pre;
    Key::Role   role;
};
using R = Key::Role;

// Standard : 4 x 6.
constexpr KeyDef kStandard[] = {
    {"%", "%", "", "", R::Function},        {"CE", "CE", "", "", R::Function},       {"C", "C", "", "", R::Function},
    {"back", "\xE2\x8C\xAB", "", "", R::Function},
    {"inv", "1/x", "", "", R::Function},    {"sqr", "x\xC2\xB2", "", "", R::Function}, {"sqrt", "\xE2\x88\x9Ax", "", "", R::Function},
    {"/", "\xC3\xB7", "", "", R::Operator},
    {"7", "7", "", "", R::Digit},           {"8", "8", "", "", R::Digit},            {"9", "9", "", "", R::Digit},
    {"*", "\xC3\x97", "", "", R::Operator},
    {"4", "4", "", "", R::Digit},           {"5", "5", "", "", R::Digit},            {"6", "6", "", "", R::Digit},
    {"-", "\xE2\x88\x92", "", "", R::Operator},
    {"1", "1", "", "", R::Digit},           {"2", "2", "", "", R::Digit},            {"3", "3", "", "", R::Digit},
    {"+", "+", "", "", R::Operator},
    {"neg", "\xC2\xB1", "", "", R::Digit},  {"0", "0", "", "", R::Digit},            {",", ",", "", "", R::Digit},
    {"=", "=", "", "", R::Equals},
};

// Scientifique : 6 x 7 (la 2nde change sin cos tan en leurs inverses).
constexpr KeyDef kScientific[] = {
    {"2nd", "2nde", "", "", R::Toggle},     {"sin", "sin", "", "", R::Function},     {"cos", "cos", "", "", R::Function},
    {"tan", "tan", "", "", R::Function},    {"pi", "\xCF\x80", "", "", R::Function}, {"e", "e", "", "", R::Function},
    {"pow", "x", "y", "", R::Function},     {"root", "\xE2\x88\x9Ax", "", "y", R::Function}, {"%", "%", "", "", R::Function},
    {"CE", "CE", "", "", R::Function},      {"C", "C", "", "", R::Function},         {"back", "\xE2\x8C\xAB", "", "", R::Function},
    {"pow10", "10", "x", "", R::Function},  {"powe", "e", "x", "", R::Function},     {"inv", "1/x", "", "", R::Function},
    {"sqr", "x\xC2\xB2", "", "", R::Function}, {"sqrt", "\xE2\x88\x9Ax", "", "", R::Function}, {"/", "\xC3\xB7", "", "", R::Operator},
    {"log", "log", "", "", R::Function},    {"ln", "ln", "", "", R::Function},       {"7", "7", "", "", R::Digit},
    {"8", "8", "", "", R::Digit},           {"9", "9", "", "", R::Digit},            {"*", "\xC3\x97", "", "", R::Operator},
    {"(", "(", "", "", R::Function},        {")", ")", "", "", R::Function},         {"4", "4", "", "", R::Digit},
    {"5", "5", "", "", R::Digit},           {"6", "6", "", "", R::Digit},            {"-", "\xE2\x88\x92", "", "", R::Operator},
    {"fact", "n!", "", "", R::Function},    {"abs", "|x|", "", "", R::Function},     {"1", "1", "", "", R::Digit},
    {"2", "2", "", "", R::Digit},           {"3", "3", "", "", R::Digit},            {"+", "+", "", "", R::Operator},
    {"mod", "mod", "", "", R::Function},    {"exp", "Exp", "", "", R::Function},     {"neg", "\xC2\xB1", "", "", R::Digit},
    {"0", "0", "", "", R::Digit},           {",", ",", "", "", R::Digit},            {"=", "=", "", "", R::Equals},
};

// Programmeur : 5 x 8 (la virgule grisee : des entiers).
constexpr KeyDef kProgrammer[] = {
    {"and", "AND", "", "", R::Function},    {"or", "OR", "", "", R::Function},       {"xor", "XOR", "", "", R::Function},
    {"not", "NOT", "", "", R::Function},    {"swap", "SWAP", "", "", R::Function},
    {"nand", "NAND", "", "", R::Function},  {"nor", "NOR", "", "", R::Function},     {"rol", "ROL", "", "", R::Function},
    {"ror", "ROR", "", "", R::Function},    {"CE", "CE", "", "", R::Function},
    {"A", "A", "", "", R::Digit},           {"shl", "<<", "", "", R::Function},      {"shr", ">>", "", "", R::Function},
    {"C", "C", "", "", R::Function},        {"back", "\xE2\x8C\xAB", "", "", R::Function},
    {"B", "B", "", "", R::Digit},           {"(", "(", "", "", R::Function},         {")", ")", "", "", R::Function},
    {"mod", "mod", "", "", R::Function},    {"/", "\xC3\xB7", "", "", R::Operator},
    {"C#", "C", "", "", R::Digit},          {"7", "7", "", "", R::Digit},            {"8", "8", "", "", R::Digit},
    {"9", "9", "", "", R::Digit},           {"*", "\xC3\x97", "", "", R::Operator},
    {"D", "D", "", "", R::Digit},           {"4", "4", "", "", R::Digit},            {"5", "5", "", "", R::Digit},
    {"6", "6", "", "", R::Digit},           {"-", "\xE2\x88\x92", "", "", R::Operator},
    {"E", "E", "", "", R::Digit},           {"1", "1", "", "", R::Digit},            {"2", "2", "", "", R::Digit},
    {"3", "3", "", "", R::Digit},           {"+", "+", "", "", R::Operator},
    {"F", "F", "", "", R::Digit},           {"neg", "\xC2\xB1", "", "", R::Digit},   {"0", "0", "", "", R::Digit},
    {",", ",", "", "", R::Digit},           {"=", "=", "", "", R::Equals},
};

} // namespace

// ================================================================== les noms ===
Mode modeFrom(std::string_view text) {
    const std::string t = lowerAscii(text);
    if (t.rfind("sci", 0) == 0) return Mode::Scientific;
    if (t.rfind("prog", 0) == 0) return Mode::Programmer;
    return Mode::Standard;
}

bool knownMode(std::string_view text) {
    const std::string t = lowerAscii(text);
    return t == "standard" || t == "scientifique" || t == "programmeur" || t == "scientific" || t == "programmer";
}

std::string_view modeKey(Mode m) noexcept {
    switch (m) {
        case Mode::Scientific: return "scientifique";
        case Mode::Programmer: return "programmeur";
        default: return "standard";
    }
}

std::string_view modeLabel(Mode m) noexcept {
    switch (m) {
        case Mode::Scientific: return "Calculatrice scientifique";
        case Mode::Programmer: return "Calculatrice programmeur";
        default: return "Calculatrice";
    }
}

std::string_view baseKey(Base b) noexcept {
    switch (b) {
        case Base::Hex: return "HEX";
        case Base::Oct: return "OCT";
        case Base::Bin: return "BIN";
        default: return "DEC";
    }
}

bool baseFrom(std::string_view text, Base& out) {
    const std::string t = upperAscii(text);
    for (const Base b : {Base::Hex, Base::Dec, Base::Oct, Base::Bin})
        if (t == baseKey(b)) {
            out = b;
            return true;
        }
    return false;
}

int baseRadix(Base b) noexcept {
    switch (b) {
        case Base::Hex: return 16;
        case Base::Oct: return 8;
        case Base::Bin: return 2;
        default: return 10;
    }
}

int wordBits(std::string_view text) {
    const std::string t = upperAscii(text);
    if (t == "BYTE" || t == "8") return 8;
    if (t == "WORD" || t == "16") return 16;
    if (t == "DWORD" || t == "32") return 32;
    if (t == "LWORD" || t == "QWORD" || t == "64") return 64;
    return 0;
}

std::string_view wordName(int bits) noexcept {
    switch (bits) {
        case 8: return "BYTE";
        case 32: return "DWORD";
        case 64: return "LWORD";
        default: return "WORD";
    }
}

std::string_view wordType(int bits, bool isSigned) noexcept {
    switch (bits) {
        case 8: return isSigned ? "SINT" : "USINT";
        case 32: return isSigned ? "DINT" : "UDINT";
        case 64: return isSigned ? "LINT" : "ULINT";
        default: return isSigned ? "INT" : "UINT";
    }
}

std::uint64_t maskOf(int bits) noexcept {
    return bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1;
}

std::int64_t signExtend(std::uint64_t word, int bits) noexcept {
    if (bits >= 64) return static_cast<std::int64_t>(word);
    word &= maskOf(bits);
    if (word & (std::uint64_t{1} << (bits - 1))) return static_cast<std::int64_t>(word | ~maskOf(bits));
    return static_cast<std::int64_t>(word);
}

Settings settingsOf(const Object& o) {
    Settings s;
    s.mode = modeFrom(o.text("calcMode", "standard"));
    s.degrees = upperAscii(o.text("angleUnit", "DEG")) != "RAD";
    const int bits = wordBits(o.text("wordSize", "WORD"));
    s.bits = bits > 0 ? bits : 16;
    s.isSigned = o.flag("signedWord", true);
    const double d = o.number("decimals", 10);
    s.decimals = std::isfinite(d) ? std::clamp(static_cast<int>(std::lround(d)), 0, 15) : 10;
    Base b = Base::Dec;
    if (baseFrom(o.text("calcBase", "DEC"), b)) s.base = b;
    return s;
}

State initialState(const Settings& settings) {
    State s;
    s.settings = settings;
    s.fromObject = settings;
    return s;
}

void applySettings(State& s, const Settings& next) {
    const Settings was = s.fromObject;
    if (next == was) return;
    s.fromObject = next;
    if (next.mode != was.mode) {
        // Un autre mode : une autre calculatrice (la memoire reste).
        clearAll(s);
        s.settings = next;
        return;
    }
    if (next.degrees != was.degrees) s.settings.degrees = next.degrees;
    if (next.decimals != was.decimals) s.settings.decimals = next.decimals;
    if (next.bits != was.bits) setWord(s, next.bits);
    if (next.isSigned != was.isSigned) setSigned(s, next.isSigned);
    if (next.base != was.base) setBase(s, next.base);
}

// ================================================================== les touches ===
bool keyEnabled(const State& s, std::string_view key) {
    const Mode m = s.settings.mode;
    // Un chiffre : 0-9, A-F en majuscules ("C#" : le chiffre C du pave programmeur ; "C" seul efface, "e" est la
    // constante).
    const bool digit = key == "C#" || (key.size() == 1 && key != "C"
                                       && (std::isdigit(static_cast<unsigned char>(key[0])) || (key[0] >= 'A' && key[0] <= 'F')));
    if (digit) {
        const int d = digitOf(key == "C#" ? 'C' : key[0]);
        return d < (m == Mode::Programmer ? baseRadix(s.settings.base) : 10);
    }
    if (key == "C" || key == "CE") return true;
    if (key == "back") return !s.entry.empty() || !s.error.empty();
    if (key == "MC" || key == "MR") return s.hasMemory;
    if (key == "angle" || key == "2nd") return m == Mode::Scientific;
    if (key == "word" || key == "sign") return m == Mode::Programmer;
    if (key == "load" || key == "send") return true;     // le moteur decide (Charger, Envoyer)
    if (key == ",") return m != Mode::Programmer;
    if (!s.error.empty()) return false;
    if (key == "M+" || key == "M-") return true;
    if (key == "=") return true;
    if (key == "(") return m != Mode::Standard;
    if (key == ")") return m != Mode::Standard && openCount(s) > 0;
    if (key == "exp") return m == Mode::Scientific && s.entry.find('E') == std::string::npos;
    if (key == "pi" || key == "e") return m == Mode::Scientific;
    if (isBinary(key)) return binaryForMode(s, key);
    if (isUnary(key)) return unaryForMode(s, key);
    return false;
}

bool press(State& s, std::string_view rawKey) {
    std::string key(rawKey);
    if (key == "C#") key = "C";           // la touche C (chiffre hexa) du pave programmeur
    else if (key == "C" && rawKey == "C") key = "clear";
    if (key == "x") key = "*";
    // Les chiffres (le "C" chiffre arrive en "C#" ; "C" seul efface).
    if (key.size() == 1 && (std::isdigit(static_cast<unsigned char>(key[0])) || (key[0] >= 'A' && key[0] <= 'F')))
        return pressDigit(s, key[0]);
    if (key == "clear") {
        clearAll(s);
        return true;
    }
    if (key == "CE") {
        if (!s.error.empty()) {
            clearAll(s);
            return true;
        }
        if (s.afterEquals) clearAll(s);
        s.hasOperand = false;
        s.entry = "0";
        return true;
    }
    if (key == ",") return pressComma(s);
    if (key == "back") return pressBack(s);
    if (key == "MC" || key == "MR" || key == "M+" || key == "M-") return pressMemory(s, key);
    if (key == "angle") {
        if (!isScientific(s)) return false;
        s.settings.degrees = !s.settings.degrees;
        return true;
    }
    if (key == "2nd") {
        if (!isScientific(s)) return false;
        s.second = !s.second;
        return true;
    }
    if (key == "word") {
        if (!isProgrammer(s)) return false;
        const int next = s.settings.bits == 8 ? 16 : s.settings.bits == 16 ? 32 : s.settings.bits == 32 ? 64 : 8;
        setWord(s, next);
        return true;
    }
    if (key == "sign") {
        if (!isProgrammer(s)) return false;
        setSigned(s, !s.settings.isSigned);
        return true;
    }
    if (key == "=") return pressEquals(s);
    if (key == "(") return pressOpen(s);
    if (key == ")") return pressClose(s);
    if (key == "exp") return pressExp(s);
    if (key == "pi" || key == "e") return pressConstant(s, key);
    if (!s.error.empty()) return false;
    if (isBinary(key)) return pressBinary(s, key);
    if (isUnary(key)) return pressUnary(s, key);
    return false;
}

std::string keyForChar(const State& s, char c) {
    const Mode m = s.settings.mode;
    if (c >= '0' && c <= '9') return std::string(1, c);
    if (m == Mode::Programmer) {
        const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (u >= 'A' && u <= 'F') return u == 'C' ? "C#" : std::string(1, u);
        switch (c) {
            case '&': return "and";
            case '|': return "or";
            case '^': return "xor";
            case '~': return "not";
            case '<': return "shl";
            case '>': return "shr";
            case '%': return "mod";
            default: break;
        }
    }
    switch (c) {
        case '.': case ',': return m == Mode::Programmer ? std::string{} : ",";
        case '+': return "+";
        case '-': return "-";
        case '*': return "*";
        case '/': return "/";
        case '=': return "=";
        case '(': return m == Mode::Standard ? std::string{} : "(";
        case ')': return m == Mode::Standard ? std::string{} : ")";
        case '^': return m == Mode::Scientific ? "pow" : std::string{};
        case '%': return m == Mode::Standard ? "%" : "mod";
        case '!': return m == Mode::Scientific ? "fact" : std::string{};
        default: return {};
    }
}

void setBase(State& s, Base b) {
    if (s.settings.base == b) return;
    const std::uint64_t w = s.entry.empty() ? 0 : parseWord(s.entry, s.settings.base, s.settings.bits);
    s.settings.base = b;
    if (!s.entry.empty()) s.entry = baseText(w, b, s.settings.bits, s.settings.isSigned, false);
    for (auto& t : s.tokens)
        if (t.type == Token::Type::Number && t.plain) t.text = numberText(s, t.value, t.word);
    if (s.hasOperand && s.operand.plain) s.operand.text = numberText(s, s.operand.value, s.operand.word);
}

void setWord(State& s, int bits) {
    if (bits != 8 && bits != 16 && bits != 32 && bits != 64) return;
    const int was = s.settings.bits;
    if (was == bits) return;
    const bool sg = s.settings.isSigned;
    // La valeur garde son signe en grandissant ; en retrecissant, ses bits du haut tombent.
    const auto resize = [&](std::uint64_t w) {
        const std::uint64_t wide = sg ? static_cast<std::uint64_t>(signExtend(w, was)) : (w & maskOf(was));
        return wide & maskOf(bits);
    };
    const std::uint64_t entryWord = s.entry.empty() ? 0 : parseWord(s.entry, s.settings.base, was);
    s.settings.bits = bits;
    if (!s.entry.empty()) s.entry = baseText(resize(entryWord), s.settings.base, bits, sg, false);
    for (auto& t : s.tokens)
        if (t.type == Token::Type::Number) {
            t.word = resize(t.word);
            t.value = wordAsNumber(t.word, bits, sg);
            if (t.plain) t.text = numberText(s, t.value, t.word);
        }
    if (s.hasOperand) {
        s.operand.word = resize(s.operand.word);
        s.operand.value = wordAsNumber(s.operand.word, bits, sg);
        if (s.operand.plain || s.afterEquals) s.operand.text = numberText(s, s.operand.value, s.operand.word);
    }
    s.memoryWord = resize(s.memoryWord);
    s.memory = wordAsNumber(s.memoryWord, bits, sg);
    if (!s.repeatOp.empty()) s.repeatOperand.word = resize(s.repeatOperand.word);
}

void setSigned(State& s, bool isSigned) {
    if (s.settings.isSigned == isSigned) return;
    const std::uint64_t entryWord = s.entry.empty() ? 0 : parseWord(s.entry, s.settings.base, s.settings.bits);
    s.settings.isSigned = isSigned;
    const int bits = s.settings.bits;
    if (!s.entry.empty()) s.entry = baseText(entryWord, s.settings.base, bits, isSigned, false);
    for (auto& t : s.tokens)
        if (t.type == Token::Type::Number) {
            t.value = wordAsNumber(t.word, bits, isSigned);
            if (t.plain) t.text = numberText(s, t.value, t.word);
        }
    if (s.hasOperand) {
        s.operand.value = wordAsNumber(s.operand.word, bits, isSigned);
        if (s.operand.plain) s.operand.text = numberText(s, s.operand.value, s.operand.word);
    }
    s.memory = wordAsNumber(s.memoryWord, bits, isSigned);
}

bool toggleBit(State& s, int bit) {
    if (!isProgrammer(s) || bit < 0 || bit >= s.settings.bits) return false;
    if (!s.error.empty()) clearAll(s);
    const std::uint64_t w = wordValue(s) ^ (std::uint64_t{1} << bit);
    leaveEquals(s);
    s.hasOperand = false;
    s.entry = baseText(norm(s, w), s.settings.base, s.settings.bits, s.settings.isSigned, false);
    return true;
}

void load(State& s, double v) {
    if (!std::isfinite(v)) return;
    clearAll(s);
    if (isProgrammer(s)) {
        const double r = std::round(v);
        const std::uint64_t w = r < 0 ? 0 - static_cast<std::uint64_t>(-static_cast<std::int64_t>(std::max(r, -9.2e18)))
                                      : static_cast<std::uint64_t>(std::min(r, 1.8e19));
        s.operand = numberToken(s, wordAsNumber(norm(s, w), s.settings.bits, s.settings.isSigned), norm(s, w), true);
    } else {
        s.operand = numberToken(s, v, 0, true);
    }
    s.hasOperand = true;
}

void loadWord(State& s, std::int64_t v) {
    if (!isProgrammer(s)) {
        load(s, static_cast<double>(v));
        return;
    }
    clearAll(s);
    const std::uint64_t w = norm(s, static_cast<std::uint64_t>(v));
    s.operand = numberToken(s, wordAsNumber(w, s.settings.bits, s.settings.isSigned), w, true);
    s.hasOperand = true;
}

// ================================================================== l'affichage ===
double value(const State& s) {
    if (!s.entry.empty()) {
        if (isProgrammer(s)) return wordAsNumber(parseWord(s.entry, s.settings.base, s.settings.bits), s.settings.bits, s.settings.isSigned);
        double v = 0;
        (void)parseEntry(s.entry, v);
        return v;
    }
    if (s.hasOperand) return s.operand.value;
    if (const Token* t = lastNumber(s)) return t->value;
    return 0;
}

std::uint64_t wordValue(const State& s) {
    if (!s.entry.empty()) return parseWord(s.entry, s.settings.base, s.settings.bits);
    if (s.hasOperand) return s.operand.word & maskOf(s.settings.bits);
    if (const Token* t = lastNumber(s)) return t->word & maskOf(s.settings.bits);
    return 0;
}

std::int64_t signedValue(const State& s) {
    const std::uint64_t w = wordValue(s);
    return s.settings.isSigned ? signExtend(w, s.settings.bits) : static_cast<std::int64_t>(w);
}

std::string displayText(const State& s) {
    if (!s.error.empty()) return "Erreur";
    if (isProgrammer(s)) {
        // Le mot tel qu'il est lu ; le binaire en cours de frappe n'est pas complete a 4 (101, pas 0101).
        if (!s.entry.empty() && s.settings.base == Base::Bin) {
            const std::string digits = baseText(wordValue(s), Base::Bin, s.settings.bits, s.settings.isSigned, false);
            std::string out;
            for (std::size_t i = 0; i < digits.size(); ++i) {
                out += digits[i];
                const std::size_t left = digits.size() - 1 - i;
                if (left > 0 && left % 4 == 0) out += ' ';
            }
            return out;
        }
        return baseText(wordValue(s), s.settings.base, s.settings.bits, s.settings.isSigned, true);
    }
    if (!s.entry.empty()) return groupEntry(s.entry);
    return formatNumber(value(s), s.settings.decimals);
}

std::string expressionText(const State& s) {
    // En erreur : ce qui a echoue, et pourquoi ("1 / 0 - division par zero") ; la fin se lit si c'est trop long.
    if (!s.error.empty()) return s.equalsLine.empty() ? s.error : s.equalsLine + " \xE2\x80\x94 " + s.error;
    if (s.afterEquals) return s.equalsLine;
    std::string line = tokensText(s.tokens);
    // Une fonction, une parenthese, pi : sur la petite ligne des qu'elle est calculee (pas un nombre tape, MR).
    if (s.hasOperand && !s.operand.plain) {
        if (!line.empty() && s.tokens.back().type != Token::Type::Open) line += " ";
        line += s.operand.text;
    }
    return line;
}

std::string baseText(std::uint64_t word, Base base, int bits, bool isSigned, bool grouped) {
    const std::uint64_t w = word & maskOf(bits);
    if (base == Base::Dec) {
        if (isSigned) {
            const std::int64_t v = signExtend(w, bits);
            const std::uint64_t mag = v < 0 ? 0 - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
            const std::string digits = std::to_string(mag);
            if (!grouped) return (v < 0 ? "-" : "") + digits;
            return (v < 0 ? std::string(kMinus) : std::string{}) + group3(digits, kThin);
        }
        const std::string digits = std::to_string(w);
        return grouped ? group3(digits, kThin) : digits;
    }
    const int radix = baseRadix(base);
    std::string digits;
    std::uint64_t x = w;
    do {
        const int d = static_cast<int>(x % static_cast<std::uint64_t>(radix));
        digits.insert(digits.begin(), static_cast<char>(d < 10 ? '0' + d : 'A' + d - 10));
        x /= static_cast<std::uint64_t>(radix);
    } while (x != 0);
    if (!grouped) return digits;
    if (base == Base::Bin) {
        // Le binaire : complete au groupe de 4 (0001 1111), comme le pave des bits.
        while (digits.size() % 4 != 0) digits.insert(digits.begin(), '0');
    }
    const std::size_t g = base == Base::Oct ? 3 : 4;
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        out += digits[i];
        const std::size_t left = digits.size() - 1 - i;
        if (left > 0 && left % g == 0) out += ' ';
    }
    return out;
}

std::string formatNumber(double v, int decimals, bool group) {
    if (!std::isfinite(v)) return "Erreur";
    if (v == 0) return "0";
    decimals = std::clamp(decimals, 0, 15);
    const double a = std::fabs(v);
    const auto scientific = [&]() {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.*e", std::min(decimals, 14), v);
        std::string s(buf);
        const auto e = s.find('e');
        std::string mant = s.substr(0, e);
        int exp = std::atoi(s.c_str() + e + 1);
        if (mant.find('.') != std::string::npos) {
            while (!mant.empty() && mant.back() == '0') mant.pop_back();
            if (!mant.empty() && mant.back() == '.') mant.pop_back();
        }
        for (auto& c : mant) if (c == '.') c = ',';
        std::string out = mant;
        if (out.rfind('-', 0) == 0) out = std::string(kMinus) + out.substr(1);
        out += "E";
        out += exp < 0 ? std::string(kMinus) : std::string("+");
        out += std::to_string(std::abs(exp));
        return out;
    };
    if (a >= 1e16) return scientific();
    const int intDigits = a < 1 ? 1 : static_cast<int>(std::floor(std::log10(a))) + 1;
    const int d = std::clamp(std::min(decimals, 15 - intDigits), 0, 15);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", d, v);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "0" || s == "-0") return a > 0 ? scientific() : "0";
    std::string sign;
    if (s.front() == '-') {
        sign = std::string(kMinus);
        s.erase(0, 1);
    }
    const auto dot = s.find('.');
    std::string ip = s.substr(0, dot), fp = dot == std::string::npos ? std::string{} : s.substr(dot + 1);
    return sign + (group ? group3(ip, kThin) : ip) + (fp.empty() ? std::string{} : "," + fp);
}

bool parseEntry(std::string_view text, double& out) {
    std::string t;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (text.substr(i, kMinus.size()) == kMinus) {
            t += '-';
            i += kMinus.size() - 1;
            continue;
        }
        if (text.substr(i, kThin.size()) == kThin) {
            i += kThin.size() - 1;
            continue;
        }
        if (c == ',') t += '.';
        else if (c == ' ') continue;
        else t += c;
    }
    // "1,2E+" (l'exposant pas encore tape) : la mantisse.
    if (!t.empty() && (t.back() == '+' || t.back() == '-') && t.size() >= 2 && (t[t.size() - 2] == 'E' || t[t.size() - 2] == 'e'))
        t.resize(t.size() - 2);
    if (!t.empty() && (t.back() == 'E' || t.back() == 'e')) t.pop_back();
    if (t.empty() || t == "-" || t == ".") {
        out = 0;
        return !t.empty();
    }
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

// ================================================================== la geometrie ===
Box defaultSize(Mode m) noexcept {
    switch (m) {
        case Mode::Scientific: return {0, 0, 460, 480};
        case Mode::Programmer: return {0, 0, 440, 660};
        default: return {0, 0, 320, 470};
    }
}

Box minimumSize(Mode m) noexcept {
    switch (m) {
        case Mode::Scientific: return {0, 0, 260, 300};
        case Mode::Programmer: return {0, 0, 280, 470};
        default: return {0, 0, 170, 270};
    }
}

Layout layoutOf(const Object& o, const State& s, double w, double h) {
    const auto set = [&](std::string_view key) {
        const Prop* p = o.find(key);
        return p && (!p->expr.empty() || p->value.find_first_not_of(" \t") != std::string::npos);
    };
    return layoutOf(s, w, h, set("startValue"), set("resultTo"));
}

Layout layoutOf(const State& s, double w, double h, bool startValue, bool resultTo) {
    Layout l;
    const Mode m = s.settings.mode;
    l.mode = m;
    w = std::max(w, 40.0);
    h = std::max(h, 60.0);
    const double pad = std::clamp(std::min(w, h) * 0.02, 4.0, 8.0);
    const double gap = std::clamp(std::min(w, h) * 0.01, 2.0, 5.0);
    const double innerW = w - 2 * pad;
    double y = pad;
    // ---- l'afficheur : la petite ligne (l'expression, les temoins a gauche), la grande (le resultat)
    const double dispH = m == Mode::Programmer ? std::clamp(h * 0.12, 40.0, 96.0) : std::clamp(h * 0.21, 50.0, 150.0);
    l.display = {pad, y, innerW, dispH};
    const double exprH = std::max(12.0, dispH * 0.32);
    const double inset = std::clamp(dispH * 0.08, 3.0, 8.0);
    l.indicators = {pad + inset, y + 2, innerW * 0.3, exprH};
    l.expression = {pad + inset, y + 2, innerW - 2 * inset, exprH};
    l.result = {pad + inset, y + 2 + exprH, innerW - 2 * inset, dispH - exprH - 4};
    l.expressionFont = std::clamp(exprH * 0.72, 8.0, 20.0);
    l.resultFont = std::clamp(l.result.h * 0.78, 12.0, 64.0);
    y += dispH + gap;
    // ---- programmeur : les quatre bases
    if (m == Mode::Programmer) {
        const double rowH = std::clamp(h * 0.036, 14.0, 26.0);
        for (int i = 0; i < 4; ++i) l.baseRows.push_back({pad, y + rowH * i, innerW, rowH});
        l.baseFont = std::clamp(rowH * 0.62, 8.0, 15.0);
        y += rowH * 4 + gap;
    }
    // ---- la rangee du haut : les bascules, la memoire, Charger, Envoyer
    const double stripH = std::clamp(h * 0.07, 22.0, 40.0);   // 32 px des 460 de haut : au doigt
    l.strip = {pad, y, innerW, stripH};
    l.stripFont = std::clamp(stripH * 0.46, 8.0, 14.0);
    {
        struct Item { std::string id, label; double weight; Key::Role role; bool on; };
        std::vector<Item> items;
        if (m == Mode::Scientific) items.push_back({"angle", s.settings.degrees ? "DEG" : "RAD", 1.3, Key::Role::Toggle, false});
        if (m == Mode::Programmer) {
            items.push_back({"word", std::string(wordName(s.settings.bits)), 1.5, Key::Role::Toggle, false});
            items.push_back({"sign", s.settings.isSigned ? "Sign\xC3\xA9" : "Non sign\xC3\xA9", 1.9, Key::Role::Toggle, false});
        }
        for (const char* k : {"MC", "MR", "M+", "M-"})
            items.push_back({k, std::string(k) == "M-" ? "M\xE2\x88\x92" : std::string(k), 1.0, Key::Role::Memory, false});
        if (startValue) items.push_back({"load", "\xE2\x87\xA9", 0.9, Key::Role::Memory, false});
        if (resultTo) items.push_back({"send", "Envoyer", 2.2, Key::Role::Send, false});
        double total = 0;
        for (const auto& it : items) total += it.weight;
        const double room = innerW - gap * static_cast<double>(items.size() - 1);
        double x = pad;
        for (const auto& it : items) {
            const double kw = room * it.weight / total;
            Key k;
            k.id = it.id;
            k.label = it.label;
            k.role = it.role;
            k.box = {x, y, kw, stripH};
            l.stripKeys.push_back(std::move(k));
            x += kw + gap;
        }
    }
    y += stripH + gap;
    // ---- programmeur : la grille des bits (16 par ligne, par groupes de 4, le bit 0 en bas a droite)
    if (m == Mode::Programmer) {
        const int bits = s.settings.bits;
        const int rows = std::max(1, bits / 16);
        const double cellH = std::clamp(h * 0.03, 12.0, 22.0);
        const double labelH = std::clamp(cellH * 0.62, 8.0, 13.0);
        const double groupGap = std::clamp(innerW * 0.025, 4.0, 14.0);
        const double cellW = (innerW - groupGap * 3) / 16.0;
        l.bitFont = std::clamp(std::min(cellH * 0.7, cellW * 0.8), 7.0, 14.0);
        l.bits.assign(static_cast<std::size_t>(bits), Box{});
        for (int r = 0; r < rows; ++r) {
            const int high = (rows - 1 - r) * 16;          // le bit le plus bas de cette ligne
            const double ry = y + r * (cellH + labelH);
            for (int c = 0; c < 16; ++c) {
                const int bit = high + 15 - c;
                const int group = c / 4;
                const double x = pad + c * cellW + group * groupGap;
                if (bit < bits) l.bits[static_cast<std::size_t>(bit)] = {x, ry, cellW, cellH};
                if (c % 4 == 3 && bit < bits) {
                    l.bitGroups.push_back({x - cellW * 3, ry + cellH, cellW * 4, labelH});
                    l.bitGroupFirst.push_back(bit);
                }
            }
        }
        y += rows * (cellH + labelH) + gap;
    }
    // ---- le pave
    const KeyDef* defs = kStandard;
    std::size_t count = std::size(kStandard);
    int cols = 4;
    if (m == Mode::Scientific) {
        defs = kScientific;
        count = std::size(kScientific);
        cols = 6;
    } else if (m == Mode::Programmer) {
        defs = kProgrammer;
        count = std::size(kProgrammer);
        cols = 5;
    }
    const int rows = static_cast<int>((count + static_cast<std::size_t>(cols) - 1) / static_cast<std::size_t>(cols));
    const double keyW = (innerW - gap * (cols - 1)) / cols;
    const double keyH = std::max(6.0, (h - pad - y - gap * (rows - 1)) / rows);
    l.keyFont = std::clamp(std::min(keyH * 0.42, keyW * 0.3), 8.0, 26.0);
    for (std::size_t i = 0; i < count; ++i) {
        const KeyDef& d = defs[i];
        const int r = static_cast<int>(i) / cols, c = static_cast<int>(i) % cols;
        Key k;
        k.id = d.id;
        k.label = d.label;
        k.sup = d.sup;
        k.pre = d.pre;
        k.role = d.role;
        k.box = {pad + c * (keyW + gap), y + r * (keyH + gap), keyW, keyH};
        if (k.id == "2nd") k.on = s.second;
        if (s.second && (k.id == "sin" || k.id == "cos" || k.id == "tan")) k.sup = "\xE2\x88\x92" "1";
        l.keys.push_back(std::move(k));
    }
    return l;
}

std::string hitPart(const Layout& l, double x, double y) {
    for (const auto& k : l.keys)
        if (k.box.contains(x, y)) return "touche:" + k.id;
    for (const auto& k : l.stripKeys)
        if (k.box.contains(x, y)) return "touche:" + k.id;
    for (std::size_t i = 0; i < l.baseRows.size(); ++i)
        if (l.baseRows[i].contains(x, y)) return "base:" + std::string(baseKey(static_cast<Base>(i)));
    for (std::size_t i = 0; i < l.bits.size(); ++i)
        if (l.bits[i].w > 0 && l.bits[i].contains(x, y)) return "bit:" + std::to_string(i);
    if (l.display.contains(x, y)) return "afficheur";
    return {};
}

bool partBox(const Layout& l, std::string_view part, Box& out) {
    if (part.rfind("touche:", 0) == 0) {
        const std::string_view id = part.substr(7);
        for (const auto* list : {&l.keys, &l.stripKeys})
            for (const auto& k : *list)
                if (k.id == id) {
                    out = k.box;
                    return true;
                }
        return false;
    }
    if (part.rfind("base:", 0) == 0) {
        Base b = Base::Dec;
        if (!baseFrom(part.substr(5), b) || l.baseRows.size() != 4) return false;
        out = l.baseRows[static_cast<std::size_t>(b)];
        return true;
    }
    if (part.rfind("bit:", 0) == 0) {
        const int n = std::atoi(std::string(part.substr(4)).c_str());
        if (n < 0 || static_cast<std::size_t>(n) >= l.bits.size()) return false;
        out = l.bits[static_cast<std::size_t>(n)];
        return out.w > 0;
    }
    if (part == "afficheur") {
        out = l.display;
        return true;
    }
    return false;
}

} // namespace hmi::calculator
