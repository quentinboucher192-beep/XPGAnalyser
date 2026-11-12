// hmi/HmiScriptC.cpp - 1.50.0 : les scripts C et C++ traduits dans le ST de l'IHM, ligne pour ligne, puis
// executes par le moteur des scripts (voir HmiScriptC.hpp).
#include "HmiScriptC.hpp"

#include "../sim/DialectLib.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
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
};

bool nameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool namePart(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

class Lexer {
public:
    Lexer(std::string_view src, std::map<std::string, std::vector<Tok>>& macros) : s_(src), macros_(macros) {}

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
            if (c == '#' && lineStart) {
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
        Tok end;
        end.k = Tok::K::End;
        end.line = line_;
        out.push_back(end);
        return out;
    }

private:
    char at(std::size_t ahead) const { return i_ + ahead < s_.size() ? s_[i_ + ahead] : '\0'; }

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
        static const char* const kLong[] = {"<<=", ">>=", "...", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
                                            "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "::"};
        Tok t;
        t.k = Tok::K::Punct;
        t.line = line_;
        for (const char* p : kLong) {
            const std::string_view w(p);
            if (s_.substr(i_, w.size()) == w) {
                t.text = std::string(w);
                i_ += w.size();
                return t;
            }
        }
        const char c = s_[i_];
        if (std::string_view("{}[]();,.?:~!%^&*-+=<>|/").find(c) == std::string_view::npos)
            throw Fault{line_, "caract\xC3\xA8re inattendu : '" + std::string(1, c) + "'"};
        t.text = std::string(1, c);
        ++i_;
        return t;
    }

    // #include, #pragma : ignores ; #define NOM valeur : une constante ; le reste : refuse.
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
            body += s_[i_++];
        }
        std::size_t k = 0;
        while (k < body.size() && (body[k] == ' ' || body[k] == '\t')) ++k;
        std::size_t e = k;
        while (e < body.size() && namePart(body[e])) ++e;
        const std::string word = body.substr(k, e - k);
        if (word == "include" || word == "pragma" || word.empty()) return;
        if (word == "define") {
            std::size_t a = e;
            while (a < body.size() && (body[a] == ' ' || body[a] == '\t')) ++a;
            std::size_t b = a;
            while (b < body.size() && namePart(body[b])) ++b;
            const std::string nm = body.substr(a, b - a);
            if (nm.empty() || !nameStart(nm[0])) throw Fault{line, "#define : un nom est attendu (#define N 10)"};
            if (b < body.size() && body[b] == '(')
                throw Fault{line, "#define " + nm + "(...) : une macro \xC3\xA0 param\xC3\xA8tres n'est pas prise en charge (une fonction l'est)"};
            std::map<std::string, std::vector<Tok>> none;
            Lexer sub(std::string_view(body).substr(b), none);
            sub.line_ = line;
            auto toks = sub.run();
            toks.pop_back();                             // la fin
            macros_[nm] = std::move(toks);
            return;
        }
        throw Fault{line, "#" + word + " : directive non prise en charge (#include et #define N valeur le sont)"};
    }

    std::string_view                         s_;
    std::map<std::string, std::vector<Tok>>& macros_;
    std::size_t                              i_{0};
    int                                      line_{1};
};

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
        {"std::string", "STRING"}, {"string", "STRING"}, {"_Bool", "BOOL"}};
    return k;
}
bool typeKeyword(const std::string& w) {
    static const std::set<std::string> k = {"void", "char", "short", "int", "long", "float", "double", "bool", "_Bool", "signed",
                                            "unsigned", "auto", "const", "constexpr", "static", "volatile", "register", "extern",
                                            "inline", "struct", "enum", "union", "wchar_t"};
    return k.count(w) > 0;
}
// Ce qu'un C ou un C++ ecrit et que le moteur ne prend pas : la raison, dite a sa ligne.
std::string refusedWord(const std::string& w) {
    static const std::map<std::string, std::string> k = {
        {"class", "une classe n'est pas prise en charge (les types IHM, Types IHM \xE2\x80\xBA Nouveau, le sont)"},
        {"union", "union n'est pas pris en charge"},
        {"typedef", "typedef n'est pas pris en charge (les types IHM le remplacent)"},
        {"namespace", "namespace n'est pas pris en charge"},
        {"template", "template n'est pas pris en charge (une fonction par type)"},
        {"typename", "typename n'est pas pris en charge"},
        {"try", "try / catch n'est pas pris en charge (un script ST a TRY ... CATCH)"},
        {"catch", "try / catch n'est pas pris en charge (un script ST a TRY ... CATCH)"},
        {"throw", "throw n'est pas pris en charge"},
        {"new", "new n'est pas pris en charge (pas de m\xC3\xA9moire allou\xC3\xA9" "e : un tableau T t[N])"},
        {"delete", "delete n'est pas pris en charge"},
        {"goto", "goto n'est pas pris en charge (break, continue, return)"},
        {"sizeof", "sizeof n'est pas pris en charge (\xC3\xA9" "cris la taille)"},
        {"operator", "operator n'est pas pris en charge"},
        {"this", "this n'est pas pris en charge"},
        {"virtual", "virtual n'est pas pris en charge"},
        {"asm", "asm n'est pas pris en charge"},
        {"malloc", "malloc n'est pas pris en charge (un tableau T t[N])"},
        {"free", "free n'est pas pris en charge"},
        {"using", "using n'est pas pris en charge (sauf using namespace std;)"},
        {"dynamic_cast", "dynamic_cast n'est pas pris en charge"},
        {"reinterpret_cast", "reinterpret_cast n'est pas pris en charge"},
        {"const_cast", "const_cast n'est pas pris en charge"}};
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
        {"to_string", "TO_STRING"}, {"atoi", "STRING_TO_DINT"}, {"atol", "STRING_TO_DINT"}, {"atof", "STRING_TO_REAL"}};
    return k;
}
std::string mappedCall(const std::string& name) {
    std::string n = name;
    if (n.rfind("std::", 0) == 0) n = n.substr(5);
    const auto& k = libraryCalls();
    const auto it = k.find(n);
    return it == k.end() ? std::string{} : it->second;
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

// ------------------------------------------------------------- la traduction --
struct X {                       // une expression traduite
    std::string st;
    bool        compound{false}; // un operateur au sommet : des parentheses quand elle devient un operande
    bool        boolish{false};  // un BOOL sans conversion (une comparaison, &&, !...)
    enum class Lit : std::uint8_t { None, Int, Real, Bool, Text } lit{Lit::None};
    bool        lvalue{false};
    bool        call{false};     // un appel f(...) au sommet : une instruction a lui seul
    std::int64_t intValue{0};    // Lit::Int
    bool        pointee{false};  // une variable POINTER TO du script (T* p local) : *p -> p^
    std::string address;         // &x : x (passe tel quel a un parametre pointeur) ; st vaut ADR(x)
};

std::string wrap(const X& x) { return x.compound ? "(" + x.st + ")" : x.st; }
std::string boolOf(const X& x) { return x.boolish ? x.st : "TO_BOOL(" + x.st + ")"; }
std::string boolWrap(const X& x) { return x.boolish ? wrap(x) : "TO_BOOL(" + x.st + ")"; }

class Translator {
public:
    Translator(std::string_view code, ScriptLang lang, const std::vector<std::string>& entryNames)
        : code_(code), lang_(lang), entryNames_(entryNames) {}

    Translation run() {
        Translation out;
        int lines = 1;
        for (const char c : code_)
            if (c == '\n') ++lines;
        try {
            Lexer lex(code_, macros_);
            auto raw = lex.run();
            // Les #define : leurs jetons a la place de leur nom (un niveau).
            for (auto& tk : raw) {
                if (tk.k == Tok::K::Name)
                    if (const auto it = macros_.find(tk.text); it != macros_.end()) {
                        for (Tok m : it->second) {
                            m.line = tk.line;
                            toks_.push_back(std::move(m));
                        }
                        continue;
                    }
                toks_.push_back(std::move(tk));
            }
            for (const auto& tk : toks_)
                if (tk.k == Tok::K::Name) allNames_.insert(upperOf(tk.text));
            for (const auto& [nm, toks] : macros_) {
                // une constante entiere (#define N 10) : la taille d'un tableau
                if (toks.size() == 1 && toks[0].k == Tok::K::Number && !toks[0].real) constInts_[nm] = toks[0].value;
                if (toks.size() == 2 && toks[0].text == "-" && toks[1].k == Tok::K::Number && !toks[1].real) constInts_[nm] = -toks[1].value;
            }
            scanFunctions();
            cur_ = &script_;
            script_.scopes.emplace_back();
            frags_.push_back({1, ""});                   // les declarations du script, patchees a la fin
            while (!at(Tok::K::End)) topLevel();
            // Rien que des fonctions (et des declarations) : un fichier C - ses variables globales sont gardees d'une
            // execution a l'autre, initialisees une fois (comme en C). Avec des instructions au niveau du script, ce niveau
            // est le corps d'une fonction appelee a chaque execution : ses variables repartent (static : gardees).
            bool anyFunction = false;
            for (const auto& [cname, info] : functions_) anyFunction = anyFunction || info.defined;
            if (!topStatements_ && anyFunction)
                for (const auto& g : globals_) {
                    auto& h = script_.hoisted[g.hoisted];
                    if (g.hasInit && !g.constant)
                        fail("une variable globale (hors des fonctions) s'initialise par une constante, comme en C ; ou donne-lui sa valeur "
                             "dans une fonction",
                             g.line);
                    h.keep = true;
                    h.init = g.literal;
                    if (g.assign < frags_.size()) frags_[g.assign].text.clear();
                }
            // Les declarations du script, en tete de la ligne 1.
            frags_[0].text = declarationBlock(script_, true);
            note(script_, 1);
            // Le point d'entree : aucune instruction au niveau du script -> main(), le nom du script, son evenement.
            if (!topStatements_) {
                std::vector<std::string> wanted = {"main"};
                for (const auto& n : entryNames_) wanted.push_back(n);
                for (const auto& w : wanted) {
                    for (const auto& [cname, info] : functions_)
                        if (upperOf(cname) == upperOf(w) && info.params == 0 && info.defined) {
                            out.entry = cname;
                            break;
                        }
                    if (!out.entry.empty()) break;
                }
                if (!out.entry.empty()) emit(last_, functions_[out.entry].st + "();");
            }
            std::vector<std::string> text(static_cast<std::size_t>(lines));
            for (const auto& f : frags_) {
                if (f.text.empty()) continue;
                auto& l = text[static_cast<std::size_t>(std::clamp(f.line, 1, lines) - 1)];
                l += (l.empty() ? "" : " ") + f.text;
            }
            for (std::size_t k = 0; k < text.size(); ++k) out.st += (k ? "\n" : "") + text[k];
            out.declared = std::move(declared_);
            out.declarationLines = std::move(declarationLines_);
        } catch (const Fault& f) {
            ScriptDiagnostic d;
            d.severity = ScriptDiagnostic::Severity::Error;
            d.line = std::max(1, f.line);
            d.message = f.message;
            out.errors.push_back(std::move(d));
            out.st.clear();
            out.entry.clear();
        }
        return out;
    }

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

    // ---- la sortie : des morceaux, chacun sur la ligne de son texte C (jamais avant le precedent)
    struct Frag {
        int         line;
        std::string text;
    };
    std::size_t emit(int line, std::string text) {
        last_ = std::max(last_, line);
        frags_.push_back({last_, std::move(text)});
        return frags_.size() - 1;
    }

    // ---- les noms
    struct Var {
        std::string st;
        std::string type;
        bool        pointer{false};          // un parametre T* p : *p, p->m designent la variable passee
        bool        target{false};           // 1.50.0 : un pointeur local T* p (POINTER TO T) : *p -> p^, p->m -> p^.m
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
        std::string                             result;    // le type rendu ("" : void)
        bool                                    function{false};
    };
    struct FnInfo {
        std::string st;
        std::size_t params{0};
        bool        defined{false};
    };

    static bool reservedSt(const std::string& name) {
        const std::string u = upperOf(name);
        if (isReservedWord(u) || stElementary().count(u)) return true;
        static const std::set<std::string> more = {"VAR", "VAR_TEMP", "VAR_INPUT", "VAR_OUTPUT", "VAR_IN_OUT", "END_VAR", "FUNCTION",
                                                   "END_FUNCTION", "CONSTANT", "ARRAY", "OF", "MAP", "LIST", "VECTOR", "SET", "RING",
                                                   "TUPLE", "REF_TO", "POINTER", "REFERENCE", "TO", "BY", "DO", "THEN", "EACH", "IN",
                                                   "AND", "OR", "XOR", "NOT", "MOD", "ENTRE", "ET", "LDATE", "LTIME", "LTOD", "LDT",
                                                   "TIME_OF_DAY", "DATE_AND_TIME", "ANY", "OBJECT", "FONT", "TEXTURE", "SOUND", "STYLE"};
        return more.count(u) > 0;
    }
    std::string fresh(const std::string& base, const std::set<std::string>& taken) const {
        for (int n = 2;; ++n) {
            const std::string c = base + "_" + std::to_string(n);
            if (!taken.count(upperOf(c)) && !allNames_.count(upperOf(c))) return c;
        }
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
            const Tok& prev = toks_[k - 1];
            if (!(toks_[k + 1].k == Tok::K::Punct && toks_[k + 1].text == "(")) continue;
            if (!(prev.k == Tok::K::Name || (prev.k == Tok::K::Punct && (prev.text == "*" || prev.text == "&" || prev.text == ">")))) continue;
            if (prev.k == Tok::K::Name
                && (prev.text == "return" || prev.text == "else" || prev.text == "case" || prev.text == "do" || prev.text == "sizeof"
                    || prev.text == "new" || prev.text == "delete" || prev.text == "throw" || prev.text == "goto"))
                continue;
            // apres la parenthese fermante : { (une definition) ou ; (un prototype)
            int paren = 0;
            std::size_t e = k + 1;
            std::size_t params = 0;
            bool any = false;
            for (; e < toks_.size(); ++e) {
                const Tok& x = toks_[e];
                if (x.k == Tok::K::Punct && x.text == "(") ++paren;
                else if (x.k == Tok::K::Punct && x.text == ")") {
                    if (--paren == 0) break;
                } else if (paren == 1) {
                    if (x.k == Tok::K::Punct && x.text == ",") ++params;
                    else if (!(x.k == Tok::K::Name && x.text == "void")) any = true;
                }
            }
            if (e + 1 >= toks_.size()) continue;
            std::size_t after = e + 1;
            if (toks_[after].k == Tok::K::Name && toks_[after].text == "const") ++after;
            const bool body = toks_[after].k == Tok::K::Punct && toks_[after].text == "{";
            const bool proto = toks_[after].k == Tok::K::Punct && toks_[after].text == ";";
            if (!body && !proto) continue;
            auto& info = functions_[t.text];
            info.params = any ? params + 1 : 0;
            info.defined = info.defined || body;
            if (info.st.empty()) {
                // un nom que le ST reserve, ou celui d'une fonction de la bibliotheque : un autre nom ST
                const std::string u = upperOf(t.text);
                const bool clash = reservedSt(t.text) || !mappedCall(t.text).empty() || sim::lib::native(u) || isHmiFunction(u);
                info.st = clash ? fresh(t.text + "_c", {}) : t.text;
            }
        }
    }

    const Var* findVar(const std::string& name) const {
        for (auto it = cur_->scopes.rbegin(); it != cur_->scopes.rend(); ++it)
            if (const auto f = it->find(name); f != it->end()) return &f->second;
        if (cur_ != &script_ && !script_.scopes.empty())   // une fonction voit les declarations du script
            if (const auto f = script_.scopes.front().find(name); f != script_.scopes.front().end()) return &f->second;
        return nullptr;
    }

    // Une variable declaree ici : son nom ST (le nom C, ou un autre s'il est pris ou reserve en ST).
    std::string declare(const std::string& name, const std::string& type, int line, bool pointer = false, bool forEach = false) {
        auto& scope = cur_->scopes.back();
        if (scope.count(name)) fail(name + " est d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "clar\xC3\xA9" "e dans ce bloc", line);
        if (functions_.count(name)) fail(name + " est d\xC3\xA9j\xC3\xA0 une fonction du script", line);
        std::string st = name;
        const bool visible = findVar(name) != nullptr;
        // Un frere d'un autre bloc, du meme type : le meme nom ST (for (int i...) deux fois).
        const Hoisted* same = nullptr;
        for (const auto& h : cur_->hoisted)
            if (upperOf(h.st) == upperOf(name)) same = &h;
        const bool reuse = same && !visible && same->type == type && !same->keep && !forEach;
        if (reservedSt(name) || visible || (cur_->taken.count(upperOf(name)) && !reuse)) st = fresh(name, cur_->taken);
        scope[name] = Var{st, type, pointer};
        if (!(reuse && st == name)) cur_->taken.insert(upperOf(st));
        return st;
    }

    // ---- les types
    struct CType {
        std::string st;          // le type ST ("" : void)
        bool        isConst{false}, isStatic{false}, isAuto{false}, isVoid{false}, isChar{false};
        int         line{0};
    };

    bool startsType(std::size_t k = 0) const {
        const Tok& t = peek(k);
        if (t.k != Tok::K::Name) return false;
        if (typeKeyword(t.text) && t.text != "struct" && t.text != "enum" && t.text != "union") return true;
        if (t.text == "struct" || t.text == "enum") return peek(k + 1).k == Tok::K::Name;
        if (cNamedTypes().count(t.text) || stElementary().count(t.text)) return true;
        if (t.text.rfind("std::", 0) == 0 && punct("<", k + 1)) return true;        // std::vector<int> : refuse plus loin
        // T x ; T *x ; T &x : un type du projet
        if (functions_.count(t.text) || findVar(t.text)) return false;
        const Tok& n = peek(k + 1);
        if (n.k == Tok::K::Name && !refusedWord(n.text).size() && n.text != "const") return true;
        if (n.k == Tok::K::Name && n.text == "const") return true;
        if (n.k == Tok::K::Punct && (n.text == "*" || n.text == "&") && peek(k + 2).k == Tok::K::Name
            && (punct("=", k + 3) || punct(";", k + 3) || punct(",", k + 3) || punct("[", k + 3) || punct(")", k + 3)))
            return true;
        return false;
    }

    CType parseType() {
        CType ty;
        ty.line = peek().line;
        int longs = 0;
        bool isUnsigned = false, isSigned = false, isShort = false, isInt = false, isChar = false, isFloat = false, isDouble = false, isBool = false;
        std::string named;
        while (peek().k == Tok::K::Name) {
            const std::string w = peek().text;
            if (w == "const" || w == "constexpr") ty.isConst = true;
            else if (w == "static") ty.isStatic = true;
            else if (w == "volatile" || w == "register" || w == "inline") {}
            else if (w == "extern") fail("extern : les variables IHM sont d\xC3\xA9j\xC3\xA0 connues par leur nom (retire extern)");
            else if (w == "unsigned") isUnsigned = true;
            else if (w == "signed") isSigned = true;
            else if (w == "short") isShort = true;
            else if (w == "long") ++longs;
            else if (w == "int") isInt = true;
            else if (w == "char") isChar = true;
            else if (w == "float") isFloat = true;
            else if (w == "double") isDouble = true;
            else if (w == "bool" || w == "_Bool") isBool = true;
            else if (w == "void") ty.isVoid = true;
            else if (w == "auto") {
                if (lang_ == ScriptLang::C) fail("auto : \xC3\xA9" "cris le type (int, float...)");
                ty.isAuto = true;
            } else if (w == "struct" || w == "enum") {
                next();
                if (punct("{", 0) || (peek().k == Tok::K::Name && punct("{", 1)))
                    fail(w + " { ... } : d\xC3\xA9" "finir un type n'est pas pris en charge ici (Types IHM \xE2\x80\xBA Nouveau)");
                if (peek().k != Tok::K::Name) fail(w + " : un nom de type est attendu");
                named = next().text;
                continue;
            } else if (w == "union" || w == "class") {
                fail(refusedWord(w));
            } else if (named.empty() && !isUnsigned && !isSigned && !isShort && !longs && !isInt && !isChar && !isFloat && !isDouble
                       && !isBool && !ty.isVoid && !ty.isAuto) {
                if (w.rfind("std::", 0) == 0 && !cNamedTypes().count(w))
                    fail(w + " : non pris en charge (un tableau T t[N], un texte std::string ; une liste : une variable IHM LIST OF T)");
                if (const std::string why = refusedWord(w); !why.empty()) fail(why);
                named = w;
            } else {
                break;
            }
            next();
        }
        if (punct("<")) fail((named.empty() ? std::string("<") : named + "<...>") + " : les mod\xC3\xA8les (template) ne sont pas pris en charge");
        ty.isChar = isChar && !isUnsigned;
        if (!named.empty()) {
            if (const auto it = cNamedTypes().find(named); it != cNamedTypes().end()) ty.st = it->second;
            else if (const auto e = stElementary().find(named); e != stElementary().end()) ty.st = e->second;
            else ty.st = named;                          // un type IHM (T_Four, T_MODE)
        } else if (ty.isVoid) {
            ty.st.clear();
        } else if (ty.isAuto) {
            ty.st.clear();
        } else if (isBool) {
            ty.st = "BOOL";
        } else if (isFloat) {
            ty.st = "REAL";
        } else if (isDouble) {
            ty.st = "LREAL";
        } else if (isChar) {
            ty.st = isUnsigned ? "USINT" : "SINT";
        } else if (isShort) {
            ty.st = isUnsigned ? "UINT" : "INT";
        } else if (longs >= 2) {
            ty.st = isUnsigned ? "ULINT" : "LINT";
        } else if (isInt || longs == 1 || isUnsigned || isSigned) {
            ty.st = isUnsigned ? "UDINT" : "DINT";
        } else {
            fail("un type est attendu (int, float, bool...)" + found());
        }
        return ty;
    }

    std::int64_t constantSize(int line) {
        X e = ternary();
        if (e.lit == X::Lit::Int) return e.intValue;
        if (const auto it = constInts_.find(e.st); it != constInts_.end()) return it->second;
        for (const auto& [c, v] : constInts_)
            if (findVar(c) && findVar(c)->st == e.st) return v;
        fail("la taille d'un tableau est une constante enti\xC3\xA8re (un nombre, #define N 10, const int N = 10)", line);
    }

    // Un declarateur : [*|&] nom [N][M]... ; le type ST complet.
    struct Declarator {
        std::string name;
        std::string st;
        bool        pointer{false}, reference{false};
        std::vector<std::int64_t> dims;
        int         line{0};
    };
    Declarator declarator(const CType& base, bool parameter) {
        Declarator d;
        d.line = peek().line;
        while (punct("*") || punct("&") || punct("&&")) {
            if (punct("*")) d.pointer = true;
            else {
                if (lang_ == ScriptLang::C) fail("& dans une d\xC3\xA9" "claration est du C++ (en C : un pointeur T* p)");
                d.reference = true;
            }
            next();
            while (word("const")) next();
        }
        if (peek().k != Tok::K::Name) fail("un nom est attendu" + found());
        d.name = next().text;
        if (const std::string why = refusedWord(d.name); !why.empty()) fail(why, d.line);
        if (d.name.find("::") != std::string::npos) fail(d.name + " : un nom simple est attendu", d.line);
        while (accept("[")) {
            if (punct("]")) {
                if (!parameter) fail("[] : la taille du tableau est attendue (T t[10])", d.line);
                fail("un param\xC3\xA8tre tableau T t[] : \xC3\xA9" "cris sa taille (T t[10]) ou passe un pointeur", d.line);
            }
            const std::int64_t n = constantSize(d.line);
            if (n <= 0 || n > 100000) fail("taille de tableau " + std::to_string(n) + " : de 1 \xC3\xA0 100000", d.line);
            d.dims.push_back(n);
            expect("]", "apr\xC3\xA8s la taille du tableau");
        }
        // Le type ST : un texte (char*, char s[N]) ; un tableau de tableaux ; sinon le type de base.
        std::string st = base.st;
        if (base.isChar && (d.pointer || (d.dims.size() == 1 && !parameter))) {
            st = "STRING";
            if (!d.dims.empty()) d.dims.clear();
            d.pointer = false;
        }
        for (auto it = d.dims.rbegin(); it != d.dims.rend(); ++it) st = "ARRAY[0.." + std::to_string(*it - 1) + "] OF " + st;
        d.st = st;
        return d;
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

    // ---- le niveau du script : des fonctions, des declarations, des instructions
    void topLevel() {
        attributes();
        if (accept(";")) return;
        if (word("using") && word("namespace", 1)) {
            while (!at(Tok::K::End) && !accept(";")) next();
            return;
        }
        if (word("typedef") || word("namespace") || word("template") || word("class")) fail(refusedWord(peek().text));
        if ((word("struct") || word("enum") || word("union")) && (punct("{", 1) || (peek(1).k == Tok::K::Name && punct("{", 2))))
            fail(peek().text + " { ... } : d\xC3\xA9" "finir un type n'est pas pris en charge (les types IHM le sont : Types IHM \xE2\x80\xBA Nouveau)");
        if (startsType()) {
            declaration(true);
            return;
        }
        topStatements_ = true;
        statement();
    }

    // int a = 1, t[3] = {1, 2, 3}; static int n = 0; et, au niveau du script, une fonction.
    void declaration(bool topLevel) {
        const CType base = parseType();
        bool first = true;
        while (true) {
            if (base.isVoid && !(peek().k == Tok::K::Name && punct("(", 1)) && !punct("*"))
                fail("void : une variable a un type (int, float...)");
            const Declarator d = declarator(base, false);
            if (punct("(")) {
                if (!first || !topLevel || cur_ != &script_ || script_.scopes.size() != 1)
                    fail("une fonction se d\xC3\xA9" "finit au niveau du script, pas dans un bloc", d.line);
                if (!d.dims.empty() || d.reference) fail("une fonction ne rend pas un tableau ni une r\xC3\xA9" "f\xC3\xA9rence", d.line);
                if (d.pointer && !(base.isChar)) fail("une fonction ne rend pas un pointeur", d.line);
                function(base, d);
                return;
            }
            variable(base, d, topLevel);
            first = false;
            if (accept(",")) continue;
            expect(";", "apr\xC3\xA8s la d\xC3\xA9" "claration");
            return;
        }
    }

    void variable(const CType& base, const Declarator& d, bool topLevel) {
        // un pointeur local : POINTER TO T du ST (un vrai pointeur : ADR(x), p^, NULL) ; pas de tableau de pointeurs
        if (d.pointer && !d.dims.empty()) fail("un tableau de pointeurs (" + d.name + ") n'est pas pris en charge", d.line);
        if (d.reference) fail("une r\xC3\xA9" "f\xC3\xA9rence locale (" + d.name + ") n'est pas prise en charge ; un param\xC3\xA8tre T& r l'est", d.line);
        const bool keep = base.isStatic;
        if (keep && cur_->function)
            fail("static dans une fonction n'est pas pris en charge (une fonction n'a pas de m\xC3\xA9moire) : d\xC3\xA9" "clare " + d.name
                     + " static au niveau du script",
                 d.line);
        std::string type = d.pointer ? "POINTER TO " + d.st : d.st;
        // L'initialisation : lue avant de declarer (int x = x; lit l'autre x, comme en C apres sa declaration... ici la valeur d'avant).
        std::vector<std::pair<std::string, X>> cells;   // (le chemin relatif "[0][1]", la valeur)
        X init;
        bool hasInit = false, braces = false;
        std::string braceLiteral;
        if (accept("=")) {
            hasInit = true;
            if (punct("{")) {
                braces = true;
                if (d.dims.empty()) fail("{ ... } : une liste de valeurs initialise un tableau (une structure IHM : membre par membre)", d.line);
                braceLiteral = initList(d.dims, 0, "", cells);
            } else {
                init = ternary();
                if (base.isAuto) {
                    switch (init.lit) {
                        case X::Lit::Int: type = "DINT"; break;
                        case X::Lit::Real: type = "LREAL"; break;
                        case X::Lit::Bool: type = "BOOL"; break;
                        case X::Lit::Text: type = "STRING"; break;
                        default: fail("auto " + d.name + " : \xC3\xA9" "cris son type (le moteur ne le d\xC3\xA9" "duit que d'un nombre, d'un texte ou de true/false)", d.line);
                    }
                }
                if (!d.dims.empty() && type != "STRING") fail("un tableau s'initialise par { ... }", d.line);
            }
        } else if (base.isAuto) {
            fail("auto " + d.name + " : une valeur initiale est attendue", d.line);
        }
        const std::string st = declare(d.name, type, d.line);
        if (d.pointer) cur_->scopes.back()[d.name].target = true;
        if (base.isConst && hasInit && !braces && init.lit == X::Lit::Int) constInts_[d.name] = init.intValue;
        Hoisted h{st, type, {}, keep, d.line};
        // la valeur initiale ecrite en constante (une variable gardee la prend une fois) ; vide : pas une constante
        const std::string literal = !hasInit ? std::string{} : braces ? braceLiteral : init.lit != X::Lit::None ? init.st : std::string{};
        const bool constant = !hasInit || !literal.empty() || (braces && cells.empty());
        if (keep) {
            if (!constant)
                fail("static " + d.name + " : sa valeur initiale est une constante (un nombre, un texte, true/false, { 1, 2 })", d.line);
            h.init = literal;
        }
        // une variable deja hissee de meme nom ST (un frere) n'est pas redeclaree
        std::size_t index = cur_->hoisted.size();
        for (std::size_t k = 0; k < cur_->hoisted.size(); ++k)
            if (upperOf(cur_->hoisted[k].st) == upperOf(st)) index = k;
        if (index == cur_->hoisted.size()) cur_->hoisted.push_back(h);
        std::size_t assign = static_cast<std::size_t>(-1);
        if (!keep && hasInit) {
            if (braces) {
                std::string text;
                for (const auto& [path, v] : cells) text += (text.empty() ? "" : " ") + st + path + " := " + v.st + ";";
                if (!text.empty()) assign = emit(d.line, text);
            } else {
                assign = emit(d.line, st + " := " + init.st + ";");
            }
        }
        // au niveau du script : une variable globale si le script n'a que des fonctions (decide a la fin)
        if (topLevel && cur_ == &script_ && !keep) globals_.push_back({index, assign, literal, d.line, hasInit, constant});
    }

    // {1, 2, {3, 4}} : chaque case, son chemin et sa valeur ; {0} et {} : rien (la variable part de zero). Rend le
    // litteral ST ([1, 2], [[1, 2], [3, 4]]) si toutes les valeurs sont des constantes, sinon vide.
    std::string initList(const std::vector<std::int64_t>& dims, std::size_t level, const std::string& path,
                         std::vector<std::pair<std::string, X>>& cells) {
        expect("{", "(une liste de valeurs)");
        std::int64_t k = 0;
        std::vector<std::pair<std::string, X>> mine;
        std::string literal = "[";
        bool constant = true;
        while (!punct("}")) {
            if (k >= dims[level]) fail("trop de valeurs pour un tableau de " + std::to_string(dims[level]));
            const std::string here = path + "[" + std::to_string(k) + "]";
            if (punct("[") || punct("."))
                fail("un initialiseur d\xC3\xA9sign\xC3\xA9 ([4] = 29, .x = 1) n'est pas pris en charge : \xC3\xA9" "cris la case apr\xC3\xA8s la d\xC3\xA9" "claration (t[4] = 29;)");
            if (punct("{")) {
                if (level + 1 >= dims.size()) fail("une valeur est attendue, pas une liste");
                const std::string sub = initList(dims, level + 1, here, mine);
                constant = constant && !sub.empty();
                literal += (k ? ", " : "") + (sub.empty() ? std::string("[]") : sub);
            } else {
                if (level + 1 < dims.size()) fail("une liste { ... } est attendue pour chaque ligne du tableau");
                const X v = ternary();
                constant = constant && v.lit != X::Lit::None;
                literal += (k ? ", " : "") + v.st;
                mine.emplace_back(here, v);
            }
            ++k;
            if (!accept(",")) break;
        }
        expect("}", "\xC3\xA0 la fin de la liste");
        literal += "]";
        // {0} seul : tout a zero (le depart de la variable) - rien a ecrire
        if (level == 0 && mine.size() == 1 && mine[0].second.lit == X::Lit::Int && mine[0].second.intValue == 0 && dims.size() == 1) return {};
        for (auto& c : mine) cells.push_back(std::move(c));
        return constant && k > 0 ? literal : std::string{};
    }

    // type nom(params) { corps } : une FUNCTION interne ; un prototype (;) : rien.
    void function(const CType& ret, const Declarator& d) {
        auto& info = functions_[d.name];
        const std::string stName = info.st.empty() ? d.name : info.st;
        const std::string result = ret.isChar && d.pointer ? std::string("STRING") : ret.st;
        if (ret.isAuto) fail("auto : \xC3\xA9" "cris le type rendu par " + d.name, d.line);
        expect("(", "apr\xC3\xA8s le nom de la fonction");
        Code fn;
        fn.function = true;
        fn.result = result;
        fn.scopes.emplace_back();
        fn.taken.insert(upperOf(stName));
        for (const auto& [cname, fi] : functions_) fn.taken.insert(upperOf(fi.st));
        Code* saved = cur_;
        cur_ = &fn;
        std::string params;
        if (word("void") && punct(")", 1)) next();
        while (!punct(")")) {
            if (punct("...")) fail("les param\xC3\xA8tres variables (...) ne sont pas pris en charge");
            const CType pt = parseType();
            if (pt.isVoid) fail("void : un param\xC3\xA8tre a un type");
            if (pt.isAuto) fail("auto : \xC3\xA9" "cris le type du param\xC3\xA8tre");
            const Declarator pd = declarator(pt, true);
            if (punct("=")) fail("une valeur par d\xC3\xA9" "faut n'est pas prise en charge (passe-la \xC3\xA0 l'appel)");
            // T* p, T& r, T t[N] : la variable de l'appelant (VAR_IN_OUT) ; const T& r, const char* s : une valeur.
            const bool byRef = (pd.pointer && !pt.isChar) || (pd.reference && !pt.isConst) || !pd.dims.empty();
            const std::string pst = declare(pd.name, pd.st, pd.line, pd.pointer && !pt.isChar);
            params += (params.empty() ? "" : "; ") + std::string(byRef ? "VAR_IN_OUT " : "") + pst + " : " + pd.st;
            if (!accept(",")) break;
        }
        expect(")", "apr\xC3\xA8s les param\xC3\xA8tres");
        if (word("const")) next();
        if (accept(";")) {                               // un prototype
            cur_ = saved;
            return;
        }
        if (!punct("{")) fail("'{' attendu : le corps de la fonction" + found());
        const std::size_t header = emit(d.line, "FUNCTION " + stName + "(" + params + ")" + (result.empty() ? std::string{} : " : " + result));
        next();                                          // {
        fn.scopes.emplace_back();
        while (!punct("}")) {
            if (at(Tok::K::End)) fail("'}' attendu : la fin de la fonction " + d.name, d.line);
            statement();
        }
        const int close = next().line;
        frags_[header].text += declarationBlock(fn, false).empty() ? std::string{} : " " + declarationBlock(fn, false);
        note(fn, frags_[header].line);
        emit(close, "END_FUNCTION");
        cur_ = saved;
    }

    void note(const Code& c, int line) {
        if (c.hoisted.empty()) return;
        declarationLines_.push_back(line);
        for (const auto& h : c.hoisted) declared_.emplace_back(upperOf(h.st), h.line);
        for (const auto& h : c.hoisted) {                    // le type nomme (un type inconnu se dit par son nom)
            std::string t = h.type;
            while (t.rfind("ARRAY[", 0) == 0 && t.find(" OF ") != std::string::npos) t = t.substr(t.find(" OF ") + 4);
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
        enum class K : std::uint8_t { Loop, Switch } k{K::Loop};
        std::string step;            // un for : son pas (ecrit avant CONTINUE)
        int         stepLine{0};
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
                if (loops_.back().k == Loop::K::Switch) fail("break au milieu d'un case n'est pas pris en charge : un seul break, \xC3\xA0 la fin du case", line);
                emit(line, "EXIT;");
                return;
            }
            if (w == "continue") {
                next();
                expect(";", "apr\xC3\xA8s continue");
                const Loop* loop = nullptr;
                for (auto it = loops_.rbegin(); it != loops_.rend(); ++it)
                    if (it->k == Loop::K::Loop) {
                        loop = &*it;
                        break;
                    }
                if (!loop) fail("continue hors d'une boucle", line);
                emit(line, loop->step + (loop->step.empty() ? "" : " ") + "CONTINUE;");
                return;
            }
            if (w == "return") {
                next();
                if (accept(";")) {
                    emit(line, "RETURN;");
                    return;
                }
                const X v = ternary();
                expect(";", "apr\xC3\xA8s return");
                if (cur_->function && !cur_->result.empty()) emit(line, "RETURN " + v.st + ";");
                else if (cur_->function) fail("return avec une valeur dans une fonction void", line);
                else emit(line, "RETURN;");             // le script : sa valeur ne va nulle part
                return;
            }
            if (w == "using" && word("namespace", 1)) {
                while (!at(Tok::K::End) && !accept(";")) next();
                return;
            }
            if (w == "else") fail("else sans if", line);
            if (w == "case" || w == "default") fail(w + " hors d'un switch", line);
            if (const std::string why = refusedWord(w); !why.empty() && w != "using") fail(why, line);
            if ((w == "struct" || w == "enum" || w == "union") && (punct("{", 1) || (peek(1).k == Tok::K::Name && punct("{", 2))))
                fail(w + " { ... } : d\xC3\xA9" "finir un type n'est pas pris en charge (les types IHM le sont)", line);
            if (startsType()) {
                declaration(false);
                return;
            }
        }
        expressionStatement(line, true);
    }

    void ifStatement() {
        const int line = next().line;
        expect("(", "apr\xC3\xA8s if");
        const X c = expression();
        expect(")", "apr\xC3\xA8s la condition du if");
        emit(line, "IF " + boolOf(c) + " THEN");
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
        const X c = expression();
        expect(")", "apr\xC3\xA8s la condition du while");
        emit(line, "WHILE " + boolOf(c) + " DO");
        loops_.push_back({});
        body();
        loops_.pop_back();
        emit(peekPrevLine(), "END_WHILE;");
    }

    void doStatement() {
        const int line = next().line;
        emit(line, "REPEAT");
        loops_.push_back({});
        body();
        loops_.pop_back();
        if (!word("while")) fail("while (...) ; attendu apr\xC3\xA8s le corps du do" + found());
        const int wl = next().line;
        expect("(", "apr\xC3\xA8s while");
        const X c = expression();
        expect(")", "apr\xC3\xA8s la condition");
        expect(";", "apr\xC3\xA8s do ... while (...)");
        emit(wl, "UNTIL NOT " + boolWrap(c) + " END_REPEAT;");
    }

    void forStatement() {
        const int line = next().line;
        expect("(", "apr\xC3\xA8s for");
        cur_->scopes.emplace_back();
        // C++ : for (T x : tableau) -> FOR EACH
        if (lang_ == ScriptLang::Cpp && startsType()) {
            const std::size_t save = i_;
            const CType ty = parseType();
            if (punct("&") || punct("*")) next();
            if (peek().k == Tok::K::Name && punct(":", 1)) {
                const std::string nm = next().text;
                next();                                  // :
                const X coll = expression();
                expect(")", "apr\xC3\xA8s la collection du for");
                const std::string type = ty.isAuto ? std::string("ANY") : ty.st;
                const std::string st = declare(nm, type, line, false, true);
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
                expressionStatement(line, false);
                while (accept(",")) expressionStatement(line, false);
                expect(";", "apr\xC3\xA8s l'initialisation du for");
            }
        }
        // la condition
        std::string cond = "TRUE";
        if (!punct(";")) cond = boolOf(expression());
        expect(";", "apr\xC3\xA8s la condition du for");
        // le pas : traduit maintenant, ecrit a la fin du corps (et avant chaque continue)
        std::string step;
        if (!punct(")")) {
            const std::size_t mark = frags_.size();
            const int keepLast = last_;
            expressionStatement(line, false);
            while (accept(",")) expressionStatement(line, false);
            for (std::size_t k = mark; k < frags_.size(); ++k) step += (step.empty() ? "" : " ") + frags_[k].text;
            frags_.resize(mark);
            last_ = keepLast;
        }
        expect(")", "apr\xC3\xA8s le pas du for");
        emit(line, "WHILE " + cond + " DO");
        loops_.push_back({Loop::K::Loop, step, line});
        body();
        loops_.pop_back();
        const int end = peekPrevLine();
        emit(end, step + (step.empty() ? "" : " ") + "END_WHILE;");
        cur_->scopes.pop_back();
    }

    // switch (x) { case 1: case 2: ...; break; default: ...; } -> CASE x OF 1, 2: ... ELSE ... END_CASE;
    void switchStatement() {
        const int line = next().line;
        expect("(", "apr\xC3\xA8s switch");
        const X v = expression();
        expect(")", "apr\xC3\xA8s la valeur du switch");
        expect("{", "apr\xC3\xA8s switch (...)");
        emit(line, "CASE " + v.st + " OF");
        cur_->scopes.emplace_back();
        loops_.push_back({Loop::K::Switch, {}, 0});
        std::string labels;                  // les etiquettes en attente (1, 2)
        int labelLine = line;
        bool open = false, ended = false, elseSeen = false;
        while (!punct("}")) {
            if (at(Tok::K::End)) fail("'}' attendu : la fin du switch", line);
            if (word("case") || word("default")) {
                const bool dflt = word("default");
                const int ll = next().line;
                if (open && !ended)
                    fail("le case de la ligne " + std::to_string(labelLine) + " continue dans le suivant (pas de break) : ce n'est pas pris en charge", ll);
                if (elseSeen) fail("un case apr\xC3\xA8s default : mets default en dernier", ll);
                open = false;
                if (!dflt) {
                    const X lab = ternary();
                    const bool name = !lab.st.empty() && (std::isalpha(static_cast<unsigned char>(lab.st[0])) || lab.st[0] == '_');
                    if (lab.lit != X::Lit::Int && !name) fail("une \xC3\xA9tiquette de case est une constante (un nombre, 'a', une valeur d'\xC3\xA9num\xC3\xA9ration)", ll);
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
            if (ended) fail("une instruction apr\xC3\xA8s le break du case (jamais ex\xC3\xA9" "cut\xC3\xA9" "e)");
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

    // x = e;  x += e;  x++;  f(a);  (`alone` : une instruction ; sinon un morceau d'un for)
    void expressionStatement(int line, bool alone) {
        // ++x ; --x
        if (punct("++") || punct("--")) {
            const bool inc = next().text == "++";
            const X target = unary();
            if (!target.lvalue) fail(std::string(inc ? "++" : "--") + " : une variable est attendue", line);
            emit(line, target.st + (inc ? " += 1;" : " -= 1;"));
            if (alone) expect(";", "apr\xC3\xA8s l'instruction");
            return;
        }
        allowPost_ = true;
        X lhs = ternary();
        allowPost_ = false;
        if (post_ != 0 && lhs.pointee) fail("l'arithm\xC3\xA9tique des pointeurs (" + lhs.st + "++) n'est pas prise en charge", line);
        if (post_ != 0) {
            const int p = post_;
            post_ = 0;
            emit(line, lhs.st + (p > 0 ? " += 1;" : " -= 1;"));
            if (alone) expect(";", "apr\xC3\xA8s l'instruction");
            return;
        }
        static const std::map<std::string, std::string> compound = {
            {"+=", "+"}, {"-=", "-"}, {"*=", "*"}, {"/=", "/"}, {"%=", "MOD"}, {"&=", "AND"}, {"|=", "OR"}, {"^=", "XOR"}, {"<<=", "SHL"}, {">>=", "SHR"}};
        if (punct("=")) {
            std::vector<X> targets{lhs};
            X value;
            while (accept("=")) {
                X e = ternary();
                if (punct("=")) {
                    if (!e.lvalue) fail("une affectation vise une variable", line);
                    targets.push_back(e);
                    continue;
                }
                value = e;
                break;
            }
            for (const auto& tg : targets)
                if (!tg.lvalue) fail("une affectation vise une variable (" + tg.st + " n'en est pas une)", line);
            std::string text = targets.back().st + " := " + value.st + ";";
            for (std::size_t k = targets.size() - 1; k-- > 0;) text += " " + targets[k].st + " := " + targets.back().st + ";";
            emit(line, text);
        } else if (peek().k == Tok::K::Punct && compound.count(peek().text)) {
            const std::string op = next().text;
            if (!lhs.lvalue) fail(op + " vise une variable", line);
            if (lhs.pointee) fail("l'arithm\xC3\xA9tique des pointeurs (" + lhs.st + " " + op + " ...) n'est pas prise en charge", line);
            const X r = ternary();
            const std::string& stOp = compound.at(op);
            if (op == "+=" || op == "-=" || op == "*=" || op == "/=") emit(line, lhs.st + " " + op + " " + r.st + ";");
            else if (op == "<<=" || op == ">>=") emit(line, lhs.st + " := " + stOp + "(" + lhs.st + ", " + r.st + ");");
            else emit(line, lhs.st + " := " + lhs.st + " " + stOp + " " + wrap(r) + ";");
        } else {
            if (!lhs.call) fail("une instruction sans effet : une affectation (x = ...) ou un appel f(...) est attendu" + found(), line);
            emit(line, lhs.st + ";");
        }
        if (alone) expect(";", "apr\xC3\xA8s l'instruction");
    }

    // ---- les expressions (du plus faible au plus fort)
    X expression() {
        X e = ternary();
        if (punct(",")) fail("l'op\xC3\xA9rateur virgule n'est pas pris en charge");
        if (punct("=") || punct("+=") || punct("-=")) fail("une affectation dans une expression n'est pas prise en charge : \xC3\xA9" "cris-la sur sa propre ligne");
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
        const bool post = allowPost_;
        allowPost_ = false;
        X a = ternary();
        expect(":", "dans c ? a : b");
        X b = ternary();
        allowPost_ = post;
        X out;
        out.st = boolWrap(c) + " ? " + wrap(a) + " : " + wrap(b);
        out.compound = true;
        out.boolish = a.boolish && b.boolish;
        return out;
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
            const bool post = allowPost_;
            allowPost_ = false;
            next();
            X rhs = logical(level + 1);
            allowPost_ = post;
            X out;
            out.compound = true;
            if (op == "||" || op == "&&") {
                out.st = boolWrap(lhs) + (op == "||" ? " OR " : " AND ") + boolWrap(rhs);
                out.boolish = true;
            } else if (op == "<<" || op == ">>") {
                out.st = std::string(op == "<<" ? "SHL(" : "SHR(") + lhs.st + ", " + rhs.st + ")";
                out.compound = false;
            } else {
                static const std::map<std::string, std::string> st = {{"|", "OR"}, {"^", "XOR"}, {"&", "AND"}, {"==", "="}, {"!=", "<>"},
                                                                      {"<", "<"}, {">", ">"}, {"<=", "<="}, {">=", ">="}, {"+", "+"},
                                                                      {"-", "-"}, {"*", "*"}, {"/", "/"}, {"%", "MOD"}};
                out.st = wrap(lhs) + " " + st.at(op) + " " + wrap(rhs);
                out.boolish = op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">="
                              || ((op == "|" || op == "&" || op == "^") && lhs.boolish && rhs.boolish);
                // un calcul de constantes entieres (t[N - 1], case 2 + 1) garde sa valeur
                if (lhs.lit == X::Lit::Int && rhs.lit == X::Lit::Int && (op == "+" || op == "-" || op == "*")) {
                    out.lit = X::Lit::Int;
                    out.intValue = op == "+" ? lhs.intValue + rhs.intValue : op == "-" ? lhs.intValue - rhs.intValue : lhs.intValue * rhs.intValue;
                }
            }
            lhs = out;
        }
        return lhs;
    }

    X unary() {
        Depth guard(exprDepth_, peek().line);
        const int line = peek().line;
        if (punct("-") || punct("+")) {
            const bool minus = next().text == "-";
            X v = unary();
            if (!minus) return v;
            X out;
            if (v.lit == X::Lit::Int || v.lit == X::Lit::Real) {
                out = v;
                out.st = "-" + v.st;
                out.intValue = -v.intValue;
                out.compound = false;
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
            return out;
        }
        if (accept("~")) {
            X v = unary();
            X out;
            out.st = "NOT " + wrap(v);
            out.compound = true;
            return out;
        }
        if (punct("*")) {                                // *p : la variable passee par pointeur
            next();
            X v = unary();
            if (v.pointee) {                             // un pointeur local : p^
                X out;
                out.st = v.st + "^";
                out.lvalue = true;
                pointerName_ = false;
                return out;
            }
            if (!pointerName_) fail("*" + v.st + " : seul un pointeur (T* p) se d\xC3\xA9r\xC3\xA9" "f\xC3\xA9rence", line);
            pointerName_ = false;
            return v;
        }
        if (punct("&")) {                                // &x : passer x a un parametre pointeur
            next();
            X v = unary();
            if (!v.lvalue) fail("&" + v.st + " : une variable est attendue", line);
            X out;                                       // ADR(x) ; un argument d'appel : x (un parametre pointeur)
            out.st = "ADR(" + v.st + ")";
            out.address = v.st;
            return out;
        }
        if (punct("++") || punct("--")) fail(peek().text + " dans une expression n'est pas pris en charge : \xC3\xA9" "cris-le sur sa propre ligne", line);
        if (word("sizeof")) fail(refusedWord("sizeof"), line);
        // (type) x : une conversion
        if (punct("(") && startsCast()) {
            next();
            const CType ty = parseType();
            bool ptr = false;
            while (accept("*")) ptr = true;
            expect(")", "apr\xC3\xA8s le type de la conversion");
            X v = unary();
            if (ptr) fail("une conversion vers un pointeur n'est pas prise en charge", line);
            return cast(ty, v, line);
        }
        return postfix();
    }

    bool startsCast() const {
        const Tok& t = peek(1);
        if (t.k != Tok::K::Name) return false;
        if (typeKeyword(t.text) && t.text != "auto" && t.text != "static") return true;
        if (cNamedTypes().count(t.text) || stElementary().count(t.text)) return punct(")", 2) || punct("*", 2);
        return false;
    }

    X cast(const CType& ty, const X& v, int line) {
        X out;
        const std::string& t = ty.st;
        if (t.empty()) fail("(void) : une conversion vers void n'est pas prise en charge", line);
        if (t == "BOOL") {
            out.st = "TO_BOOL(" + v.st + ")";
            out.boolish = true;
        } else if (t == "REAL" || t == "LREAL" || t == "STRING") {
            out.st = "TO_" + t + "(" + v.st + ")";
        } else if (t == "LINT" || t == "ULINT") {
            out.st = "TO_" + t + "(" + v.st + ")";
        } else if (stElementary().count(t) || cNamedTypes().count(t)) {
            // vers un entier : tronque vers zero, comme le C ((int)2.7 = 2)
            out.st = "TO_" + t + "(TRUNC(" + v.st + "))";
        } else {
            out.st = "TO_" + t + "(" + v.st + ")";
        }
        return out;
    }

    X postfix() {
        X e = primary();
        while (true) {
            const int line = peek().line;
            if (e.pointee && punct("[")) fail(e.st + "[...] : l'arithm\xC3\xA9tique des pointeurs n'est pas prise en charge (un tableau T t[N] l'est)", line);
            if (accept("[")) {
                const X i = expression();
                expect("]", "apr\xC3\xA8s l'indice");
                e.st = wrap(e) + "[" + i.st + "]";
                e.compound = false;
                e.lvalue = true;
                e.lit = X::Lit::None;
                e.boolish = false;
                pointerName_ = false;
                continue;
            }
            if (punct(".") || punct("->")) {
                const bool arrow = next().text == "->";
                if (peek().k != Tok::K::Name) fail("un membre est attendu apr\xC3\xA8s " + std::string(arrow ? "->" : "."), line);
                const std::string m = next().text;
                if (arrow && !pointerName_ && !e.pointee) fail(e.st + "->" + m + " : seul un pointeur (T* p) s'emploie avec ->", line);
                pointerName_ = false;
                e.st = wrap(e) + (arrow && e.pointee ? "^." : ".") + m;
                e.pointee = false;
                e.compound = false;
                e.lvalue = true;
                e.boolish = false;
                e.lit = X::Lit::None;
                if (punct("(")) e = callArgs(e.st, line, true);
                continue;
            }
            if (punct("++") || punct("--")) {
                if (!allowPost_ || !e.lvalue || !(punct(";", 1) || punct(")", 1) || punct(",", 1)))
                    fail(peek().text + " dans une expression n'est pas pris en charge : \xC3\xA9" "cris-le sur sa propre ligne", line);
                post_ = next().text == "++" ? 1 : -1;
                return e;
            }
            break;
        }
        return e;
    }

    // Les arguments d'un appel ; `st` : le nom ST appele.
    X callArgs(const std::string& st, int line, bool method) {
        expect("(", "");
        std::vector<X> args;
        while (!punct(")")) {
            const bool post = allowPost_;
            allowPost_ = false;
            args.push_back(ternary());
            allowPost_ = post;
            if (!accept(",")) break;
        }
        expect(")", "apr\xC3\xA8s les arguments" );
        (void)method;
        (void)line;
        // &x passe a un parametre pointeur : x (la variable elle-meme, VAR_IN_OUT) ; un pointeur local passe a une
        // fonction du script : ce qu'il vise (p^).
        bool userFunction = false;
        for (const auto& [cname, info] : functions_) userFunction = userFunction || info.st == st;
        X out;
        out.st = st + "(";
        for (std::size_t k = 0; k < args.size(); ++k) {
            const std::string a = !args[k].address.empty() ? args[k].address : args[k].pointee && userFunction ? args[k].st + "^" : args[k].st;
            out.st += (k ? ", " : "") + a;
        }
        out.st += ")";
        out.call = true;
        return out;
    }

    // printf("Niveau %d : %.1f\n", n, x) -> IHM_JOURNAL('Niveau ' + TO_STRING(n) + ' : ' + TO_STRING(x))
    X printfCall(int line, bool newline) {
        expect("(", "apr\xC3\xA8s printf");
        if (!at(Tok::K::String)) fail("printf : le format est un texte entre guillemets", line);
        std::string fmt;
        while (at(Tok::K::String)) fmt += next().text;
        std::vector<X> args;
        while (accept(",")) args.push_back(ternary());
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
            if (e >= fmt.size() || std::string_view("diufFgGeEsScxXo").find(fmt[e]) == std::string_view::npos)
                fail("printf : format %" + fmt.substr(k + 1, e - k) + " non pris en charge", line);
            if (used >= args.size()) fail("printf : il manque une valeur pour %" + fmt.substr(k + 1, e - k), line);
            if (!lit.empty()) parts.push_back(stString(lit));
            lit.clear();
            const X& a = args[used++];
            parts.push_back(fmt[e] == 's' ? a.st : "TO_STRING(" + a.st + ")");
            k = e;
        }
        if (used < args.size()) fail("printf : une valeur de trop (" + std::to_string(args.size()) + " pour " + std::to_string(used) + " %)", line);
        while (!lit.empty() && (lit.back() == '\n' || lit.back() == '\r')) lit.pop_back();
        if (!lit.empty() || parts.empty()) parts.push_back(stString(lit));
        (void)newline;
        X out;
        out.st = "IHM_JOURNAL(";
        for (std::size_t k = 0; k < parts.size(); ++k) out.st += (k ? " + " : "") + parts[k];
        out.st += ")";
        out.call = true;
        return out;
    }

    X primary() {
        Depth guard(exprDepth_, peek().line);
        pointerName_ = false;
        const Tok& t = peek();
        const int line = t.line;
        X out;
        switch (t.k) {
            case Tok::K::Number:
                next();
                out.st = t.text;
                out.lit = t.real ? X::Lit::Real : X::Lit::Int;
                out.intValue = t.value;
                return out;
            case Tok::K::Char:
                next();
                out.st = t.text;
                out.lit = X::Lit::Int;
                out.intValue = t.value;
                return out;
            case Tok::K::String: {
                std::string s;
                while (at(Tok::K::String)) s += next().text;
                out.st = stString(s);
                out.lit = X::Lit::Text;
                return out;
            }
            case Tok::K::End: fail("une valeur est attendue (fin du script atteinte)", line);
            case Tok::K::Punct:
                if (accept("(")) {
                    const bool post = allowPost_;
                    allowPost_ = false;
                    X v = expression();
                    allowPost_ = post;
                    expect(")", "(une parenth\xC3\xA8se ouverte n'est pas ferm\xC3\xA9" "e)");
                    if (v.lvalue || !v.compound) return v;          // (x), (*p) : la variable elle-meme
                    v.st = "(" + v.st + ")";
                    v.compound = false;
                    v.call = false;
                    return v;
                }
                if (punct("[")) fail("une lambda n'est pas prise en charge (une fonction nomm\xC3\xA9" "e l'est)", line);
                if (punct("{")) fail("{ ... } : une liste de valeurs n'initialise qu'un tableau, dans sa d\xC3\xA9" "claration", line);
                fail("une valeur est attendue" + found(), line);
            case Tok::K::Name: break;
        }
        const std::string name = next().text;
        if (name == "true" || name == "false" || name == "TRUE" || name == "FALSE") {
            out.st = upperOf(name);
            out.lit = X::Lit::Bool;
            out.boolish = true;
            return out;
        }
        if (name == "NULL" || name == "nullptr") {
            out.st = "NULL";
            return out;
        }
        if (const std::string why = refusedWord(name); !why.empty()) fail(why, line);
        if (name == "static_cast") {
            expect("<", "apr\xC3\xA8s static_cast");
            const CType ty = parseType();
            expect(">", "apr\xC3\xA8s le type de static_cast");
            expect("(", "apr\xC3\xA8s static_cast<...>");
            const X v = expression();
            expect(")", "apr\xC3\xA8s la valeur de static_cast");
            return cast(ty, v, line);
        }
        if (name == "printf" || name == "puts" || name == "std::printf" || name == "std::puts") {
            if (name.find("puts") != std::string::npos) {
                X a = callArgs("IHM_JOURNAL", line, false);
                return a;
            }
            return printfCall(line, false);
        }
        if (punct("(")) {
            // une fonction du script, de la bibliotheque, ou de l'IHM
            if (const auto it = functions_.find(name); it != functions_.end()) return callArgs(it->second.st, line, false);
            if (name == "std::clamp" || name == "clamp") {      // std::clamp(x, lo, hi) -> LIMIT(lo, x, hi)
                next();
                const X x = ternary();
                expect(",", "dans std::clamp(x, bas, haut)");
                const X lo = ternary();
                expect(",", "dans std::clamp(x, bas, haut)");
                const X hi = ternary();
                expect(")", "apr\xC3\xA8s std::clamp(x, bas, haut)");
                out.st = "LIMIT(" + lo.st + ", " + x.st + ", " + hi.st + ")";
                out.call = true;
                return out;
            }
            if (const std::string m = mappedCall(name); !m.empty()) return callArgs(m, line, false);
            if (name.rfind("std::", 0) == 0) fail(name + " : non pris en charge (les fonctions math\xC3\xA9matiques std::sqrt, std::min... le sont)", line);
            if (name.find("::") != std::string::npos) fail(name + " : un nom simple est attendu", line);
            return callArgs(name, line, false);
        }
        if (name.rfind("std::", 0) == 0) fail(name + " : non pris en charge (IHM_JOURNAL ou printf pour \xC3\xA9" "crire au journal)", line);
        if (const auto sep = name.find("::"); sep != std::string::npos) {
            // T_MODE::Auto (une enumeration C++) -> T_MODE#Auto
            if (name.find("::", sep + 2) != std::string::npos) fail(name + " : un nom simple est attendu", line);
            out.st = name.substr(0, sep) + "#" + name.substr(sep + 2);
            return out;
        }
        if (const Var* v = findVar(name)) {
            out.st = v->st;
            out.lvalue = true;
            out.boolish = v->type == "BOOL";
            out.pointee = v->target;
            pointerName_ = v->pointer;
            return out;
        }
        if (const auto it = functions_.find(name); it != functions_.end()) {   // une fonction donnee comme valeur (un rappel)
            out.st = it->second.st;
            return out;
        }
        // une variable IHM, une constante de l'IHM, une valeur d'enumeration : tels quels
        out.st = name;
        out.lvalue = true;
        return out;
    }

    std::string_view                       code_;
    ScriptLang                             lang_;
    std::vector<std::string>               entryNames_;
    std::map<std::string, std::vector<Tok>> macros_;
    std::vector<Tok>                       toks_;
    std::size_t                            i_{0};
    std::vector<Frag>                      frags_;
    int                                    last_{1};
    std::set<std::string>                  allNames_;
    std::map<std::string, FnInfo>          functions_;
    std::map<std::string, std::int64_t>    constInts_;
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
    bool                                   allowPost_{false};
    int                                    post_{0};
    bool                                   pointerName_{false};
    int                                    depth_{0};
    int                                    exprDepth_{0};
};

} // namespace

Translation translate(std::string_view code, ScriptLang lang, const std::vector<std::string>& entryNames) {
    Translator t(code, lang == ScriptLang::ST ? ScriptLang::C : lang, entryNames);
    return t.run();
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
        if (const auto it = cache.find(key); it != cache.end()) return it->second;
    }
    Translation t = translate(code, lang, entryNames);
    std::lock_guard<std::mutex> lock(m);
    if (cache.size() > 256) cache.clear();
    cache.emplace(std::move(key), t);
    return t;
}

void placeFault(const Translation& t, ScriptDiagnostic& d) {
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

std::string supportedSubset(ScriptLang lang) {
    const bool cpp = lang == ScriptLang::Cpp;
    return std::string(cpp ? "C++" : "C")
         + " : compil\xC3\xA9 et ex\xC3\xA9" "cut\xC3\xA9 par le moteur des scripts de l'IHM (le m\xC3\xAAme que le ST), un sous-ensemble : "
           "int, float, double, bool, char, les intN_t, un texte (const char*"
         + std::string(cpp ? ", std::string" : "") + "), les types IHM et du ST (REAL, DINT...), les tableaux t[N][M] ; "
           "if, switch/case (un break par case), while, do, for"
         + std::string(cpp ? " (et for (T x : tableau))" : "") + ", break, continue, return ; "
           "les fonctions du script (par valeur, pointeur T* p"
         + std::string(cpp ? ", r\xC3\xA9" "f\xC3\xA9rence T& r" : "") + ") ; les op\xC3\xA9rateurs du C ; sqrt, pow, sin, min, max, printf (au journal) ; "
           "les pointeurs (T* p = &x ; *p ; p->m ; NULL : un POINTER TO du ST) ; "
           "les variables et les fonctions IHM par leur nom. Pas d'arithm\xC3\xA9tique des pointeurs, ni malloc, structures, "
         + std::string(cpp ? "classes, templates, lambdas, exceptions, std::vector, std::cout" : "goto, sizeof") + ".";
}

std::string compiledNote(ScriptLang lang) {
    return std::string(lang == ScriptLang::Cpp ? "C++" : "C")
         + " : compil\xC3\xA9 pour le moteur des scripts de l'IHM (un sous-ensemble du " + (lang == ScriptLang::Cpp ? "C++" : "C") + "), ex\xC3\xA9" "cut\xC3\xA9 en simulation";
}

} // namespace hmi::cscript
