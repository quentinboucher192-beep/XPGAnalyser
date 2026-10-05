// =============================================================================
//  hmi/HmiExprCheck.cpp - Lot API 8 : les expressions impossibles
//  (voir HmiExprCheck.hpp). Une lecture a part, tolerante : elle ne refait pas
//  le travail de sim::parse (la syntaxe), elle suit les noms, les appels et
//  les types, et ne s'arrete jamais sur ce qu'elle ne comprend pas.
// =============================================================================
#include "HmiExprCheck.hpp"
#include "HmiApiVars.hpp"   // 1.11.1 (API-M) : API.<globale>, API.<Unite>.<variable>
#include "HmiPopupParams.hpp"

#include "HmiExpr.hpp"
#include "HmiMarkers.hpp"   // 1.11 (REP) : les reperes $...$, transparents pour la verification
#include "HmiOperators.hpp"
#include "HmiPublicVars.hpp"
#include "HmiScript.hpp"
#include "HmiSymbols.hpp"   // 1.11.10 : les fonctions des symboles
#include "HmiTypes.hpp"
#include "../project/MemberTree.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace hmi::exprcheck {

namespace {

std::string up(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

// Les types qu'on sait suivre ; Unknown ne declenche jamais d'erreur.
enum class T : std::uint8_t { Unknown, Bool, Num, Text, Time };

std::string typeWord(T t) {
    switch (t) {
        case T::Bool: return "un bool\xC3\xA9" "en";
        case T::Num:  return "un nombre";
        case T::Text: return "un texte";
        case T::Time: return "une dur\xC3\xA9" "e";
        case T::Unknown: break;
    }
    return "une valeur";
}

// Le type d'un type elementaire IHM ("REAL", "STRING"...).
T typeOfName(std::string_view type) {
    const std::string u = up(trimmed(type));
    if (u.empty()) return T::Unknown;
    if (u == "BOOL" || u == "EBOOL") return T::Bool;
    if (u == "STRING" || u.rfind("STRING[", 0) == 0 || u == "WSTRING") return T::Text;
    if (u == "TIME" || u == "LTIME") return T::Time;
    static const std::set<std::string> kNum = {"INT",  "UINT", "DINT", "UDINT", "SINT", "USINT", "LINT", "ULINT",
                                               "WORD", "DWORD", "BYTE", "LWORD", "REAL", "LREAL"};
    return kNum.count(u) ? T::Num : T::Unknown;
}

// ------------------------------------------------------------ les jetons ---
struct Tok {
    enum class K : std::uint8_t { End, Ident, Num, Str, Lit, Op } k{K::End};
    std::string text;
    double      num{0};
    bool        numKnown{false};
    T           litType{T::Unknown};    // un litteral type (T#5s, INT#3, BOOL#1)
};

std::vector<Tok> lex(std::string_view s) {
    std::vector<Tok> out;
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == '(' && i + 1 < n && s[i + 1] == '*') {
            const auto e = s.find("*)", i + 2);
            i = e == std::string_view::npos ? n : e + 2;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') ++i;
            continue;
        }
        if (c == '\'' || c == '"') {
            Tok t;
            t.k = Tok::K::Str;
            std::size_t j = i + 1;
            while (j < n && s[j] != c) {
                if (s[j] == '$' && j + 1 < n) { t.text += s[j + 1]; j += 2; continue; }
                t.text += s[j++];
            }
            i = j < n ? j + 1 : n;
            out.push_back(std::move(t));
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            bool based = false;
            while (j < n) {
                const char d = s[j];
                if (identChar(d) || d == '.') { ++j; continue; }
                if (d == '#') { based = true; ++j; continue; }
                if ((d == '+' || d == '-') && !based && j > i && (s[j - 1] == 'e' || s[j - 1] == 'E')) { ++j; continue; }
                break;
            }
            Tok t;
            t.k = Tok::K::Num;
            t.text = std::string(s.substr(i, j - i));
            std::string clean;
            for (char d : t.text) if (d != '_') clean += d;
            if (!based) {
                char* end = nullptr;
                const double v = std::strtod(clean.c_str(), &end);
                if (end && *end == '\0') { t.num = v; t.numKnown = true; }
            } else if (const auto h = clean.find('#'); h != std::string::npos) {
                const int base = std::atoi(clean.substr(0, h).c_str());
                if (base == 2 || base == 8 || base == 16) {
                    char* end = nullptr;
                    const std::string digits = clean.substr(h + 1);
                    const long long v = std::strtoll(digits.c_str(), &end, base);
                    if (!digits.empty() && end && *end == '\0') { t.num = static_cast<double>(v); t.numKnown = true; }
                }
            }
            out.push_back(std::move(t));
            i = j;
            continue;
        }
        if (identStart(c)) {
            std::size_t j = i;
            while (j < n && identChar(s[j])) ++j;
            std::string word(s.substr(i, j - i));
            if (j < n && s[j] == '#') {
                // T#5s, TIME#1h, INT#3, BOOL#1, D#2024-01-01, TOD#12:00:00
                const std::string w = up(word);
                const bool date = w == "D" || w == "DATE" || w == "DT" || w == "DATE_AND_TIME";
                std::size_t k = j + 1;
                while (k < n && (identChar(s[k]) || s[k] == '.' || s[k] == ':' || (date && s[k] == '-'))) ++k;
                Tok t;
                t.k = Tok::K::Lit;
                t.text = std::string(s.substr(i, k - i));
                t.litType = (w == "T" || w == "TIME" || w == "LTIME") ? T::Time
                          : w == "BOOL"                                ? T::Bool
                          : typeOfName(w) == T::Num                    ? T::Num
                          : w == "STRING"                              ? T::Text
                                                                       : T::Unknown;
                out.push_back(std::move(t));
                i = k;
                continue;
            }
            Tok t;
            t.k = Tok::K::Ident;
            t.text = std::move(word);
            out.push_back(std::move(t));
            i = j;
            continue;
        }
        Tok t;
        t.k = Tok::K::Op;
        static const char* const kTwo[] = {"<=", ">=", "<>", "**", ":="};
        for (const char* two : kTwo)
            if (s.substr(i, 2) == two) t.text = two;
        if (t.text.empty()) t.text = std::string(1, c);
        i += t.text.size();
        out.push_back(std::move(t));
    }
    out.push_back(Tok{});
    return out;
}

// ------------------------------------------------------------ les valeurs --
struct Val {
    T           type{T::Unknown};
    bool        numKnown{false};    // un nombre constant
    double      num{0};
    bool        strKnown{false};    // un texte constant
    std::string str;
    std::string show;               // ce qu'on montre dans un message
    std::vector<std::string> choices;   // les textes constants qu'elle peut valoir (un litteral, un SEL)
};

// Les fonctions standard : nombre d'arguments (min, max ; -1 : sans limite).
struct Arity { const char* name; int min; int max; };
constexpr Arity kArity[] = {
    {"ABS", 1, 1},  {"SQRT", 1, 1}, {"LN", 1, 1},    {"LOG", 1, 1},   {"EXP", 1, 1},   {"SIN", 1, 1},     {"COS", 1, 1},
    {"TAN", 1, 1},  {"ASIN", 1, 1}, {"ACOS", 1, 1},  {"ATAN", 1, 1},  {"TRUNC", 1, 1}, {"ROUND", 1, 2},   {"NEG", 1, 1},
    {"LEN", 1, 1},  {"EXPT", 2, 2}, {"LEFT", 2, 2},  {"RIGHT", 2, 2}, {"FIND", 2, 2},  {"SHL", 2, 2},     {"SHR", 2, 2},
    {"ROL", 2, 2},  {"ROR", 2, 2},  {"LIMIT", 3, 3}, {"SEL", 3, 3},   {"MID", 3, 3},   {"INSERT", 3, 3},  {"DELETE", 3, 3},
    {"REPLACE", 4, 4}, {"MIN", 2, -1}, {"MAX", 2, -1}, {"CONCAT", 2, -1}, {"MUX", 2, -1},
};

std::string plural(int n, const char* one) { return std::to_string(n) + " " + one + (n > 1 ? "s" : ""); }

std::string numberText(double v) {
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) return std::to_string(static_cast<long long>(std::llround(v)));
    char buf[40];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

// Une couleur ecrite : #RRGGBB, #RRGGBBAA, 16#RRGGBB, RRGGBB, transparent.
bool colorText(std::string_view t) {
    const std::string s = trimmed(t);
    if (s.empty() || s == "transparent" || s == "aucun") return true;
    std::string_view h = s;
    if (h.front() == '#') h.remove_prefix(1);
    else if (h.size() > 3 && h.substr(0, 3) == "16#") h.remove_prefix(3);
    if (h.size() != 6 && h.size() != 8) return false;
    return std::all_of(h.begin(), h.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

std::size_t distance(std::string_view a, std::string_view b) {
    const std::string x = up(a.substr(0, 64)), y = up(b.substr(0, 64));
    std::vector<std::size_t> row(y.size() + 1);
    for (std::size_t j = 0; j <= y.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= x.size(); ++i) {
        std::size_t diag = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= y.size(); ++j) {
            const std::size_t keep = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (x[i - 1] == y[j - 1] ? 0u : 1u)});
            diag = keep;
        }
    }
    return row[y.size()];
}

std::string closestAmong(std::string_view name, const std::vector<std::string>& pool) {
    const std::string u = up(name);
    std::string best;
    std::size_t bestScore = static_cast<std::size_t>(-1);
    const std::size_t limit = std::max<std::size_t>(2, u.size() / 3);
    for (const auto& c : pool) {
        if (c.empty() || up(c) == u) continue;
        const std::string cu = up(c);
        std::size_t d = distance(u, cu);
        // L'un commence par l'autre (Vitesse -> Vitesse_Moteur) : tres proche.
        if (u.size() >= 3 && (cu.rfind(u, 0) == 0 || u.rfind(cu, 0) == 0 || cu.find(u) != std::string::npos)) d = std::min<std::size_t>(d, 1);
        if (d <= limit && d < bestScore) {
            bestScore = d;
            best = c;
        }
    }
    return best;
}

class Checker {
public:
    Checker(const Context& ctx, std::vector<Tok> toks) : ctx_(ctx), toks_(std::move(toks)) {}

    Val run() { return orExpr(); }
    std::vector<Problem> problems;
    // 1.11.1 (REP-10) : les racines des reperes de l'expression (en majuscules) - un ancien
    // `$Vanne$` qui n'est pas une variable n'a pas de "veux-tu dire Vanne_Purge ?" : la
    // fausse piste ; c'est Dupliquer qui le remplit.
    std::set<std::string> markerRoots;

private:
    const Context&   ctx_;
    std::vector<Tok> toks_;
    std::size_t      at_{0};

    const Tok& peek() const { return toks_[std::min(at_, toks_.size() - 1)]; }
    const Tok& next() {
        const Tok& t = peek();
        if (at_ < toks_.size() - 1) ++at_;
        return t;
    }
    bool isOp(const char* op) const { return peek().k == Tok::K::Op && peek().text == op; }
    bool isWord(const char* w) const { return peek().k == Tok::K::Ident && up(peek().text) == w; }

    void problem(std::string msg) {
        for (const auto& p : problems) if (p.message == msg) return;
        problems.push_back(Problem{std::move(msg), {}, {}});
    }

    static bool textish(const Val& v) { return v.type == T::Text; }

    Val combine(const Val& a, const Val& b, const std::string& op) {
        if (textish(a) || textish(b)) {
            const Val& t = textish(a) ? a : b;
            problem("un texte ne se combine pas avec " + op + " (" + t.show + ") : compare-le d'abord (" + t.show
                    + " = 'Marche') ou utilise un bool\xC3\xA9" "en");
        }
        Val r;
        r.type = (a.type == T::Bool && b.type == T::Bool) ? T::Bool
               : (a.type == T::Num || b.type == T::Num)   ? T::Num
                                                          : T::Unknown;
        r.show = a.show + " " + op + " " + b.show;
        return r;
    }

    Val orExpr() {
        Val a = xorExpr();
        while (isWord("OR")) { next(); a = combine(a, xorExpr(), "OR"); }
        return a;
    }
    Val xorExpr() {
        Val a = andExpr();
        while (isWord("XOR")) { next(); a = combine(a, andExpr(), "XOR"); }
        return a;
    }
    Val andExpr() {
        Val a = cmpExpr();
        while (isWord("AND") || isOp("&")) { next(); a = combine(a, cmpExpr(), "AND"); }
        return a;
    }
    Val cmpExpr() {
        Val a = addExpr();
        while (isOp("=") || isOp("<>") || isOp("<") || isOp(">") || isOp("<=") || isOp(">=")) {
            const std::string op = next().text;
            const Val b = addExpr();
            // Le simulateur compare tout, mais un texte et un nombre ne sont jamais
            // egaux : la condition serait toujours fausse (ou toujours vraie).
            const bool clash = (a.type == T::Text && (b.type == T::Num || b.type == T::Bool))
                            || (b.type == T::Text && (a.type == T::Num || a.type == T::Bool));
            if (clash) {
                const std::string hint = (a.type == T::Num || b.type == T::Num)
                    ? " : \xC3\xA9" "cris le nombre sans apostrophes, ou convertis le texte (STRING_TO_REAL)"
                    : " : \xC3\xA9" "cris TRUE ou FALSE sans apostrophes";
                problem("tu compares " + typeWord(a.type) + " (" + a.show + ") \xC3\xA0 " + typeWord(b.type) + " (" + b.show
                        + "), ils ne se comparent pas" + hint);
            }
            Val r;
            r.type = T::Bool;
            r.show = a.show + " " + op + " " + b.show;
            a = r;
        }
        return a;
    }
    Val arith(const Val& a, const Val& b, const std::string& op) {
        Val r;
        r.show = a.show + " " + op + " " + b.show;
        // '+' met des textes bout a bout (le simulateur le fait) ; le reste non.
        if (op == "+" && (textish(a) || textish(b))) {
            r.type = T::Text;
            return r;
        }
        if (textish(a) || textish(b)) {
            const Val& t = textish(a) ? a : b;
            problem("un texte ne se calcule pas (" + t.show + " " + op + ") : pour calculer, convertis-le (STRING_TO_REAL)");
        }
        if (a.type == T::Time && (b.type == T::Time || b.type == T::Num)) r.type = T::Time;
        else if (a.type == T::Num && b.type == T::Num) r.type = T::Num;
        else if (a.type == T::Num && b.type == T::Time && op == "*") r.type = T::Time;
        if (a.numKnown && b.numKnown) {
            r.numKnown = true;
            if (op == "+") r.num = a.num + b.num;
            else if (op == "-") r.num = a.num - b.num;
            else if (op == "*") r.num = a.num * b.num;
            else if (op == "/" && b.num != 0) r.num = a.num / b.num;
            else if (op == "MOD" && b.num != 0) r.num = std::fmod(a.num, b.num);
            else if (op == "**") r.num = std::pow(a.num, b.num);
            else r.numKnown = false;
        }
        return r;
    }
    Val addExpr() {
        Val a = mulExpr();
        while (isOp("+") || isOp("-")) {
            const std::string op = next().text;
            a = arith(a, mulExpr(), op);
        }
        return a;
    }
    Val mulExpr() {
        Val a = powExpr();
        while (isOp("*") || isOp("/") || isWord("MOD")) {
            const std::string op = up(next().text);
            const Val b = powExpr();
            if ((op == "/" || op == "MOD") && b.numKnown && b.num == 0)
                problem("division par z\xC3\xA9ro (" + a.show + " " + op + " " + b.show
                        + ") : la valeur sera fausse \xC3\xA0 chaque cycle ; corrige le diviseur");
            a = arith(a, b, op);
        }
        return a;
    }
    Val powExpr() {
        Val a = unary();
        while (isOp("**")) { next(); a = arith(a, unary(), "**"); }
        return a;
    }
    Val unary() {
        if (isWord("NOT")) {
            next();
            Val a = unary();
            if (textish(a)) problem("NOT ne s'applique pas \xC3\xA0 un texte (" + a.show + ") : compare-le d'abord (" + a.show + " = 'Marche')");
            a.show = "NOT " + a.show;
            a.numKnown = false;
            if (a.type != T::Bool && a.type != T::Num) a.type = T::Unknown;
            return a;
        }
        if (isOp("-") || isOp("+")) {
            const std::string op = next().text;
            Val a = unary();
            if (textish(a)) problem("un texte n'a pas de signe (" + op + a.show + ") : convertis-le (STRING_TO_REAL)");
            if (a.numKnown && op == "-") a.num = -a.num;
            a.show = op + a.show;
            return a;
        }
        return primary();
    }

    Val primary() {
        const Tok t = peek();
        Val v;
        switch (t.k) {
            case Tok::K::Num:
                next();
                v.type = T::Num;
                v.numKnown = t.numKnown;
                v.num = t.num;
                v.show = t.text;
                return v;
            case Tok::K::Str:
                next();
                v.type = T::Text;
                v.strKnown = true;
                v.str = t.text;
                v.show = "'" + t.text + "'";
                v.choices.push_back(t.text);
                return v;
            case Tok::K::Lit:
                next();
                v.type = t.litType;
                v.show = t.text;
                return v;
            case Tok::K::Op:
                if (t.text == "(") {
                    next();
                    v = orExpr();
                    if (isOp(")")) next();
                    v.show = "(" + v.show + ")";
                    return v;
                }
                return v;     // la syntaxe est l'affaire de sim::parse
            case Tok::K::Ident: {
                next();
                const std::string u = up(t.text);
                if (u == "TRUE" || u == "FALSE") {
                    v.type = T::Bool;
                    v.show = t.text;
                    return v;
                }
                if (isOp("(")) return call(t.text);
                // 1.11.10 : Vanne_3.Etat(), Vue_Vannes.Vanne_3.Etat() - la fonction d'une instance.
                if (isOp(".")) {
                    std::vector<std::string> segs{t.text};
                    std::size_t k = at_;
                    while (k + 1 < toks_.size() && toks_[k].k == Tok::K::Op && toks_[k].text == "." && toks_[k + 1].k == Tok::K::Ident) {
                        segs.push_back(toks_[k + 1].text);
                        k += 2;
                    }
                    if (segs.size() >= 2 && k < toks_.size() && toks_[k].k == Tok::K::Op && toks_[k].text == "(")
                        if (const HmiFunction* fn = instanceFunction(segs)) {
                            at_ = k;
                            std::string dotted;
                            for (const auto& sg : segs) dotted += (dotted.empty() ? "" : ".") + sg;
                            pendingFn_ = fn;
                            return call(dotted);
                        }
                }
                return path(t.text);
            }
            case Tok::K::End:
                break;
        }
        return v;
    }

    std::vector<std::string> pool() const {
        std::vector<std::string> out;
        if (ctx_.project) {
            for (const auto& var : ctx_.project->programs.variables) out.push_back(var.name);
            for (const auto& f : ctx_.project->programs.functions) out.push_back(f.name);
        }
        if (ctx_.view)
            for (const auto& prm : ctx_.view->params) out.push_back(prm.name);
        for (const auto& c : ctx_.candidates) out.push_back(c);
        return out;
    }

    // Un morceau de chemin : un membre, ou des indices (constants ou non).
    struct Seg {
        bool                                    index{false};
        std::string                             member;
        std::vector<std::pair<bool, long long>> idx;    // (constant, valeur)
    };

    // Les membres et les indices constants d'un chemin de l'automate ; le type
    // du bout ("" : il ne se lit pas).
    std::string plcPath(const std::string& root, const std::vector<Seg>& segs) {
        std::string type = ctx_.plc.rootType(root);
        std::string where = root;
        for (const auto& sg : segs) {
            if (type.empty()) return {};
            if (sg.index) {
                const auto shape = project::members::parseArray(type);
                if (!shape.valid()) return {};
                std::string shown;
                for (std::size_t d = 0; d < sg.idx.size(); ++d) {
                    shown += (d ? "," : "") + (sg.idx[d].first ? std::to_string(sg.idx[d].second) : std::string("i"));
                    if (sg.idx.size() != shape.dims.size() || !sg.idx[d].first) continue;
                    const auto [lo, hi] = shape.dims[d];
                    if (sg.idx[d].second < lo || sg.idx[d].second > hi) {
                        problem("indice " + std::to_string(sg.idx[d].second) + " hors des bornes de " + where + " (" + std::to_string(lo) + ".."
                                + std::to_string(hi) + ", un tableau de l'automate) : corrige l'indice");
                        return {};
                    }
                }
                type = shape.element;
                where += "[" + shown + "]";
                continue;
            }
            if (!sg.member.empty() && std::isdigit(static_cast<unsigned char>(sg.member[0]))) return {};   // un bit : Mot.3
            const std::string mt = ctx_.plc.memberType ? ctx_.plc.memberType(type, sg.member) : std::string{};
            if (mt.empty()) {
                const bool elementary = typeOfName(type) != T::Unknown;
                if (elementary)
                    problem(where + "." + sg.member + " : " + where + " est un " + type + " de l'automate, il n'a pas de membre ; corrige le chemin");
                else if (ctx_.plc.isStruct && ctx_.plc.isStruct(type))
                    problem(where + "." + sg.member + " : le type " + type + " de l'automate n'a pas de membre " + sg.member + " ; corrige le chemin");
                return {};
            }
            type = mt;
            where += "." + sg.member;
        }
        return type;
    }

    Val path(const std::string& root) {
        std::string text = root;
        std::vector<Seg> segs;
        for (;;) {
            if (isOp(".")) {
                next();
                if (peek().k == Tok::K::Ident || peek().k == Tok::K::Num) {
                    Seg sg;
                    sg.member = next().text;
                    text += "." + sg.member;
                    segs.push_back(std::move(sg));
                } else {
                    break;
                }
            } else if (isOp("[")) {
                next();
                text += "[";
                Seg sg;
                sg.index = true;
                bool first = true;
                for (;;) {
                    const Val idx = orExpr();
                    if (!first) text += ",";
                    first = false;
                    text += idx.numKnown ? numberText(idx.num) : std::string("0");
                    const bool whole = idx.numKnown && std::fabs(idx.num - std::round(idx.num)) < 1e-9 && std::fabs(idx.num) < 1e15;
                    sg.idx.emplace_back(whole, whole ? static_cast<long long>(std::llround(idx.num)) : 0LL);
                    if (isOp(",")) { next(); continue; }
                    break;
                }
                if (isOp("]")) next();
                text += "]";
                segs.push_back(std::move(sg));
            } else {
                break;
            }
        }
        Val v;
        v.show = text;
        const Project* p = ctx_.project;
        if (const Variable* var = p ? p->variable(root) : nullptr) {
            if (text == root) v.type = typeOfName(var->type);
            else if (const auto ty = types::typeOfPath(*p, text); !ty.empty()) v.type = typeOfName(ty);
            return v;
        }
        // 1.9 : un parametre type de la vue - Moteur.Vitesse est REAL d'apres T_Moteur.
        if (p && ctx_.view)
            if (const auto* prm = ctx_.view->param(root); prm && !prm->type.empty()) {
                if (const auto ty = params::expressionType(*p, ctx_.view, text); !ty.empty()) v.type = typeOfName(ty);
                return v;
            }
        // 1.11.1 (API-M) : API.<globale>, API.<Unite>.<variable>, jusqu'au bout des
        // membres (une variable IHM, une vue nommee API sont vues avant ; un
        // parametre de la vue aussi). Sans le modele : rien a dire.
        if (up(root) == "API" && !segs.empty() && !segs.front().index && !(p && pub::viewNamed(*p, root))) {
            if (!ctx_.plc.api) return v;
            std::string apiText = root;
            for (const auto& sg : segs) {
                if (!sg.index) {
                    apiText += "." + sg.member;
                    continue;
                }
                apiText += "[";
                for (std::size_t d = 0; d < sg.idx.size(); ++d)
                    apiText += (d ? "," : "") + (sg.idx[d].first ? std::to_string(sg.idx[d].second) : std::string("i"));
                apiText += "]";
            }
            const auto r = ctx_.plc.api->resolve(apiText);
            if (!r.ok) {
                Problem pb;
                pb.unknownName = r.missing;
                pb.suggestion = r.suggestion;
                pb.message = r.error;
                for (const auto& q : problems) if (q.message == pb.message) return v;
                problems.push_back(std::move(pb));
                return v;
            }
            v.type = typeOfName(r.type);
            return v;
        }
        // 1.11.1 (remarque d'API-V) : API seul, API[...], ou API. en cours de frappe,
        // n'est pas un nom inconnu (« API n'existe pas ») : le chemin est incomplet.
        if (up(root) == "API" && !(p && pub::viewNamed(*p, root))) {
            if (!ctx_.plc.api) return v;
            Problem pb;
            pb.message = "API : chemin incomplet \xE2\x80\x94 \xC3\xA9" "cris API.<globale> ou API.<Unit\xC3\xA9>.<variable>";
            for (const auto& q : problems) if (q.message == pb.message) return v;
            problems.push_back(std::move(pb));
            return v;
        }
        if (ctx_.known && !ctx_.known(root)) {
            Problem pb;
            pb.unknownName = root;
            const bool marker = markerRoots.count(up(root)) != 0;
            if (!marker) pb.suggestion = closestAmong(root, pool());
            pb.message = root + " n'existe pas";
            if (!pb.suggestion.empty())
                pb.message += " : veux-tu dire " + pb.suggestion + " ?";
            else if (marker)
                pb.message += " (ni variable IHM, ni variable syst\xC3\xA8me, ni variable de l'automate)";
            else
                pb.message += " (ni variable IHM, ni variable syst\xC3\xA8me, ni variable de l'automate) : corrige le nom ou d\xC3\xA9" "clare la variable";
            for (const auto& q : problems) if (q.message == pb.message) return v;
            problems.push_back(std::move(pb));
            return v;
        }
        // Tranche 2 : un chemin de l'automate (pas un parametre de la vue).
        if (ctx_.plc.rootType && !(ctx_.view && ctx_.view->param(root))) {
            const std::string end = plcPath(root, segs);
            if (!end.empty()) v.type = typeOfName(end);
        }
        return v;
    }

    // ---- 1.11.10 : les fonctions des symboles ----
    // Le symbole dont l'expression est controlee : le symbole lui-meme, ou celui qui porte la popup.
    const View* symbolHere() const {
        if (!ctx_.view || !ctx_.project) return nullptr;
        if (isSymbolView(*ctx_.view)) return ctx_.view;
        return popupOwner(*ctx_.project, *ctx_.view);
    }
    // ["Vanne_3", "Etat"] (une instance de la vue ou du symbole) ou ["Vue", "Vanne_3", "Etat"] : la fonction.
    const HmiFunction* instanceFunction(const std::vector<std::string>& segs) const {
        const Project* p = ctx_.project;
        if (!p || segs.size() < 2) return nullptr;
        const View* here = symbolHere() ? symbolHere() : ctx_.view;
        if (here) {
            const View* sym = nullptr;
            for (const auto& o : here->objects)
                if (o.kind == Kind::SymbolInstance && up(o.name) == up(segs[0])) sym = symbolOf(*p, o);
            for (std::size_t k = 1; sym && k < segs.size(); ++k) {
                if (k + 1 == segs.size()) return symbolFunction(*sym, segs[k]);
                const View* inner = nullptr;
                for (const auto& o : sym->objects)
                    if (o.kind == Kind::SymbolInstance && up(o.name) == up(segs[k])) inner = symbolOf(*p, o);
                sym = inner;
            }
        }
        std::string dotted;
        for (const auto& sg : segs) dotted += (dotted.empty() ? "" : ".") + sg;
        if (BoundFunction b; boundSymbolFunction(*p, dotted, b))
            if (const View* sv = p->viewByName(b.symbol)) return symbolFunction(*sv, b.function.name);
        return nullptr;
    }
    const HmiFunction* pendingFn_{nullptr};

    Val call(const std::string& name) {
        // 1.11.10 : une fonction de symbole (par son nom dans le symbole, ou Instance.Fonction).
        const HmiFunction* symFn = pendingFn_;
        pendingFn_ = nullptr;
        if (!symFn)
            if (const View* sv = symbolHere()) symFn = symbolFunction(*sv, name);
        next();    // (
        std::vector<Val> args;
        if (!isOp(")"))
            for (;;) {
                args.push_back(orExpr());
                if (isOp(",")) { next(); continue; }
                break;
            }
        if (isOp(")")) next();
        const std::string u = up(name);
        const int n = static_cast<int>(args.size());
        Val r;
        r.show = name + "(...)";
        if (symFn) {
            r.type = typeOfName(symFn->returnType);
            if (trimmed(symFn->returnType).empty())
                problem(name + " ne rend pas de valeur : une expression ne peut appeler qu'une fonction qui rend quelque chose");
            else {
                // Le nombre d'arguments : ses VAR_INPUT (une entree avec une valeur initiale est facultative).
                const auto parts = splitDeclarations(symFn->body, true);
                const auto inputs = parts.inputs();
                int required = 0;
                for (const auto* in : inputs) required += in->initial.empty() ? 1 : 0;
                if (n < required || n > static_cast<int>(inputs.size()))
                    problem(name + " prend " + (required == static_cast<int>(inputs.size()) ? plural(required, "argument")
                                                                                            : std::to_string(required) + " \xC3\xA0 " + plural(static_cast<int>(inputs.size()), "argument"))
                            + ", pas " + std::to_string(n));
            }
            return r;
        }
        if (isStandardFunction(u)) {
            const auto to = u.find("_TO_");
            if (to != std::string::npos && to > 0) {
                if (n != 1) problem(u + " prend 1 argument, pas " + std::to_string(n) + " : " + u + "(valeur)");
                const T from = typeOfName(u.substr(0, to));
                if (n >= 1 && args[0].type != T::Unknown && from != T::Unknown && args[0].type != from
                    && (from == T::Text || args[0].type == T::Text))
                    problem(u + " attend " + typeWord(from) + ", pas " + typeWord(args[0].type) + " (" + args[0].show + ")");
                r.type = typeOfName(u.substr(to + 4));
                return r;
            }
            for (const auto& a : kArity) {
                if (u != a.name) continue;
                if (n < a.min || (a.max >= 0 && n > a.max)) {
                    const std::string want = a.max < 0             ? "au moins " + plural(a.min, "argument")
                                           : a.min == a.max        ? plural(a.min, "argument")
                                                                   : std::to_string(a.min) + " ou " + plural(a.max, "argument");
                    problem(u + " prend " + want + ", pas " + std::to_string(n));
                }
                break;
            }
            static const std::set<std::string> kText = {"LEFT", "RIGHT", "MID", "CONCAT", "INSERT", "DELETE", "REPLACE"};
            static const std::set<std::string> kTextIn = {"LEN", "LEFT", "RIGHT", "MID", "FIND", "CONCAT", "INSERT", "DELETE", "REPLACE"};
            if (kTextIn.count(u)) {
                if (n >= 1 && (args[0].type == T::Num || args[0].type == T::Bool))
                    problem(u + " travaille sur un texte, pas sur " + typeWord(args[0].type) + " (" + args[0].show + ") : convertis-le (INT_TO_STRING)");
            } else if (u != "SEL" && u != "MUX") {
                for (const auto& a : args)
                    if (a.type == T::Text) {
                        problem(u + " calcule sur des nombres, pas sur un texte (" + a.show + ")");
                        break;
                    }
            }
            if (u == "SEL") {
                if (n >= 1 && args[0].type == T::Text) problem("SEL choisit avec un bool\xC3\xA9" "en, pas un texte (" + args[0].show + ")");
                if (n == 3) r.type = args[1].type == args[2].type ? args[1].type : T::Unknown;
            }
            if (u == "SEL" || u == "MUX") {
                for (std::size_t k = 1; k < args.size(); ++k) r.choices.insert(r.choices.end(), args[k].choices.begin(), args[k].choices.end());
            }
            if (u == "SEL") {
            } else if (kText.count(u)) {
                r.type = T::Text;
            } else if (u == "LEN" || u == "FIND") {
                r.type = T::Num;
            } else if (u == "MIN" || u == "MAX" || u == "LIMIT") {
                T common = n ? args[0].type : T::Unknown;
                for (const auto& a : args) if (a.type != common) common = T::Unknown;
                r.type = common == T::Time ? T::Time : common == T::Num ? T::Num : T::Unknown;
            } else if (u != "MUX") {
                r.type = T::Num;
            }
            return r;
        }
        if (const Project* p = ctx_.project) {
            for (const auto& f : p->programs.functions)
                if (up(f.name) == u) {
                    r.type = typeOfName(f.returnType);
                    if (trimmed(f.returnType).empty())
                        problem(name + " ne rend pas de valeur : une expression ne peut appeler qu'une fonction qui rend quelque chose");
                    return r;
                }
        }
        if (isHmiFunction(u)) return r;
        // 1.10.2 (chantier T3) : TO_STRING(Mode), TO_INT(x), TO_T_MODE(2) - les conversions
        // du dialecte et celles des types du projet, que le moteur sert (HmiExpr.cpp).
        if (ctx_.project && isConversion(*ctx_.project, u)) {
            if (n != 1) problem(u + " prend 1 argument, pas " + std::to_string(n) + " : " + u + "(valeur)");
            r.type = typeOfName(u.substr(3));
            return r;
        }
        std::vector<std::string> names;
        for (const auto& a : kArity) names.emplace_back(a.name);
        for (const char* conv : {"INT_TO_REAL", "REAL_TO_INT", "INT_TO_STRING", "REAL_TO_STRING", "BOOL_TO_INT"}) names.emplace_back(conv);
        if (ctx_.project) for (const auto& f : ctx_.project->programs.functions) names.push_back(f.name);
        if (const View* sv = symbolHere()) for (const auto& f : sv->functions) names.push_back(f.name);   // 1.11.10
        const std::string near = closestAmong(name, names);
        problem("fonction inconnue : " + name + (near.empty() ? std::string() : " (veux-tu dire " + near + " ?)")
                + " ; une expression n'appelle que les fonctions standard (ABS, MIN, LIMIT, INT_TO_REAL...), les fonctions IHM du projet"
                  " et celles des symboles (dans le symbole : Nom(), dans la vue : Instance.Nom())");
        return r;
    }
};

} // namespace

std::string closest(const Context& ctx, std::string_view name) {
    std::vector<std::string> pool;
    if (ctx.project) {
        for (const auto& var : ctx.project->programs.variables) pool.push_back(var.name);
        for (const auto& f : ctx.project->programs.functions) pool.push_back(f.name);
    }
    if (ctx.view)
        for (const auto& prm : ctx.view->params) pool.push_back(prm.name);
    for (const auto& c : ctx.candidates) pool.push_back(c);
    return closestAmong(name, pool);
}

Want wantOf(std::string_view key) {
    // Les cles dont le type est connu d'abord (visible : BOOL, x : REAL...).
    for (const auto& p : pub::kPropTypes)
        if (p.key == key) return p.type == "BOOL" ? Want::Bool : Want::Number;
    const std::string k(key);
    if (k == "fill" || k == "stroke" || k == "color"
        || (k.size() > 5 && (k.compare(k.size() - 5, 5, "Color") == 0 || k.compare(k.size() - 5, 5, "color") == 0)))
        return Want::Color;
    return Want::Any;
}

std::vector<Problem> check(const Context& ctx, std::string_view written, Want want) {
    std::vector<Problem> out;
    // 1.11 (REP) : les `$` des reperes sont transparents ($V[1].Ouv$ se verifie comme V[1].Ouv).
    const std::string plain = markers::strip(written, markers::Mode::Expression);
    const std::string_view source = plain;
    if (trimmed(source).empty()) return out;
    Checker c(ctx, lex(source));
    for (const auto& m : markers::find(written, markers::Mode::Expression))   // 1.11.1 (REP-10)
        if (const auto roots = scanRoots(m.content(written)); !roots.empty()) c.markerRoots.insert(up(roots.front()));
    const Val v = c.run();
    out = std::move(c.problems);
    const auto add = [&out](std::string m) {
        for (const auto& p : out) if (p.message == m) return;
        out.push_back(Problem{std::move(m), {}, {}});
    };
    switch (want) {
        case Want::Bool:
            if (v.type == T::Text)
                add("un bool\xC3\xA9" "en est attendu (vrai ou faux) et l'expression donne un texte (" + v.show + ") : compare-le, par exemple "
                    + v.show + " = 'Marche'");
            else if (v.type == T::Time)
                add("un bool\xC3\xA9" "en est attendu (vrai ou faux) et l'expression donne une dur\xC3\xA9" "e (" + v.show + ") : compare-la (" + v.show + " > T#5s)");
            break;
        case Want::Number:
            if (v.type == T::Text)
                add("un nombre est attendu et l'expression donne un texte (" + v.show + ") : convertis-le (STRING_TO_REAL) ou choisis une variable num\xC3\xA9rique");
            break;
        case Want::Color:
            if (v.type == T::Bool || v.type == T::Num || v.type == T::Time)
                add("une couleur est attendue (un texte '#RRGGBB') et l'expression donne " + typeWord(v.type) + " (" + v.show
                    + ") : choisis la couleur avec SEL(" + (v.type == T::Bool ? v.show : v.show + " > 0") + ", '#808080', '#00A000')");
            else
                for (const auto& s : v.choices)
                    if (!colorText(s)) {
                        add("'" + s + "' n'est pas une couleur : \xC3\xA9" "cris '#RRGGBB' (par exemple '#FF0000')");
                        break;
                    }
            break;
        case Want::Text:
        case Want::Any:
            break;
    }
    // Les membres et les indices constants des variables IHM composees.
    if (ctx.project)
        for (const auto& pp : types::pathProblems(*ctx.project, source)) add(pp.message);
    return out;
}

std::vector<Problem> checkTarget(const Context& ctx, std::string_view target) {
    std::vector<Problem> out;
    const std::string t = trimmed(markers::strip(target, markers::Mode::Expression));   // 1.11 (REP)
    if (t.empty()) return out;
    const auto toks = lex(t);
    const Tok& first = toks.front();
    const bool constant = first.k == Tok::K::Num || first.k == Tok::K::Str || first.k == Tok::K::Lit
                       || (first.k == Tok::K::Ident && (up(first.text) == "TRUE" || up(first.text) == "FALSE"));
    if (constant) {
        out.push_back(Problem{"tu \xC3\xA9" "cris dans une constante (" + t + ") : la cible d'une \xC3\xA9" "criture doit \xC3\xAA" "tre une variable", {}, {}});
        return out;
    }
    if (first.k != Tok::K::Ident) return out;
    if (const Variable* var = ctx.project ? ctx.project->variable(first.text) : nullptr) {
        if (var->readOnly)
            out.push_back(Problem{"la variable IHM " + var->name + " est en lecture seule : rien ne peut l'\xC3\xA9" "crire ; d\xC3\xA9" "coche "
                                  "\xC2\xAB Lecture seule \xC2\xBB dans Variables IHM, ou vise une autre variable", {}, {}});
    } else if (ctx.plc.api && up(first.text) == "API" && apivars::isApiPath(t) && !(ctx.project && pub::viewNamed(*ctx.project, first.text))) {
        // 1.11.1 (API-M) : une ecriture vers API.<...> - une constante ou une
        // variable en lecture seule de l'automate est refusee, avec la raison
        // (un chemin inconnu : check() le dit plus bas).
        const auto r = ctx.plc.api->resolve(t);
        if (r.ok && r.constant)
            out.push_back(Problem{r.path + " est une constante de l'automate : rien ne peut l'\xC3\xA9" "crire", {}, {}});
        else if (r.ok && r.access == apivars::Access::ReadOnly)
            out.push_back(Problem{r.path + " est en lecture seule (" + r.accessWhy + ") : l'IHM ne peut pas l'\xC3\xA9" "crire", {}, {}});
    }
    for (auto& p : check(ctx, t, Want::Any)) out.push_back(std::move(p));
    return out;
}

std::vector<std::string> templateExpressions(std::string_view text) {
    // Le meme decoupage que TextTemplate::compile : {{ et }} sont des accolades
    // litterales, un trou va jusqu'a la premiere '}', son format est apres le
    // dernier ':' (pas celui d'un ":=") s'il en a l'air, ou ":u".
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if ((c == '{' || c == '}') && i + 1 < text.size() && text[i + 1] == c) { i += 2; continue; }
        if (c != '{') { ++i; continue; }
        const auto close = text.find('}', i + 1);
        if (close == std::string_view::npos) break;
        std::string inside(text.substr(i + 1, close - i - 1));
        const auto colon = inside.rfind(':');
        if (colon != std::string::npos && colon + 1 < inside.size() && inside[colon + 1] != '=') {
            const std::string fmt = inside.substr(colon + 1);
            if (looksLikeFormat(fmt) || fmt == "u" || fmt == "U") inside.resize(colon);
        }
        if (!trimmed(inside).empty()) out.push_back(trimmed(inside));
        i = close + 1;
    }
    return out;
}

} // namespace hmi::exprcheck
