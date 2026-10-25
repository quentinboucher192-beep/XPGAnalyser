#include "HmiScript.hpp"
#include "HmiNames.hpp"   // 1.13.0 (lot J8) : les directives USING
#include "../sim/DialectLib.hpp"   // 1.12.7 : les templates (NUMERIC_LIMITS<T>, NUMBERS) ne sont pas des variables

#include "../sim/Interpreter.hpp"
#include "HmiScriptCheck110.hpp"   // 1.10 : les noms du dialecte (fonctions internes, FOR EACH)
#include "HmiDecl.hpp"              // 1.11.18 (refonte, lot 3) : les declarations du modele, reconstruites
#include "HmiMarkers.hpp"          // 1.11.1 (REP) : les $ des reperes, transparents dans un script ST
#include "HmiOperators.hpp"        // 1.11.17 : la signature d'un operateur (le lieu d'un appel)
#include "HmiSymbols.hpp"          // 1.11.17 : la portee d'un symbole, rewriteNames (les textes d'un objet)
#include "HmiTypeRegistry.hpp"     // 1.11.19 (refonte, lot 6) : les types d'une locale
#include "HmiTypes.hpp"            // 1.12.20 : un rappel nomme (T_Filtre)
#include "HmiRoles.hpp"
#include "HmiTypeForms.hpp"         // 1.12.20 (lot C) : les formes d'un type, la signature d'un rappel (l'en-tete du projet)
#include "HmiNatives.hpp"           // 1.12.20 : une fonction IHM_ qui agit (pas une qui lit)            // 1.12.10 (lot F) : les bornes et les seuils tapes en nom de variable

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>
#include <type_traits>

namespace hmi {

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

// ---- 1.12.20 (lot C) : la verification plus large du C et du C++ ------------------------
// Les jetons d'un code C ou C++ : noms (std::function compris), nombres, ponctuation ; une chaine devient une valeur.
struct CTok {
    enum class K : std::uint8_t { Name, Num, Punct } k{K::Punct};
    std::string text;
    int line{1};
};
std::vector<CTok> cTokens(std::string_view code) {
    std::vector<CTok> out;
    int line = 1;
    for (std::size_t i = 0; i < code.size();) {
        const char c = code[i];
        const char n = i + 1 < code.size() ? code[i + 1] : '\0';
        if (c == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '/' && n == '/') {
            while (i < code.size() && code[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && n == '*') {
            i += 2;
            while (i + 1 < code.size() && !(code[i] == '*' && code[i + 1] == '/')) {
                if (code[i] == '\n') ++line;
                ++i;
            }
            i += 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            ++i;
            while (i < code.size() && code[i] != c && code[i] != '\n') i += code[i] == '\\' ? 2 : 1;
            ++i;
            out.push_back({CTok::K::Num, "\"\"", line});
            continue;
        }
        if (identStart(c)) {
            std::size_t j = i;
            while (j < code.size()) {
                if (identChar(code[j])) ++j;
                else if (code[j] == ':' && j + 2 < code.size() && code[j + 1] == ':' && identStart(code[j + 2])) j += 2;
                else break;
            }
            out.push_back({CTok::K::Name, std::string(code.substr(i, j - i)), line});
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            while (j < code.size() && (identChar(code[j]) || code[j] == '.')) ++j;
            out.push_back({CTok::K::Num, std::string(code.substr(i, j - i)), line});
            i = j;
            continue;
        }
        out.push_back({CTok::K::Punct, std::string(1, c), line});
        ++i;
    }
    return out;
}
// Les mots du C et du C++, et ce qu'un script emploie sans le declarer.
bool cWord(const std::string& n) {
    static const std::set<std::string> k = {
        "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue", "return", "goto", "sizeof", "typedef",
        "struct", "union", "enum", "const", "static", "extern", "volatile", "register", "inline", "void", "char", "short", "int", "long",
        "float", "double", "signed", "unsigned", "bool", "true", "false", "NULL", "auto", "class", "public", "private", "protected",
        "template", "typename", "using", "namespace", "new", "delete", "this", "nullptr", "mutable", "constexpr", "static_cast",
        "dynamic_cast", "const_cast", "reinterpret_cast", "operator", "virtual", "override", "final", "try", "catch", "throw",
        "size_t", "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "printf", "fabs", "sqrt",
        "abs", "min", "max"};
    return k.count(n) > 0;
}
// Les modeles de la bibliotheque standard qui prennent des chevrons (std::cout << x n'en est pas un).
bool cppTemplateName(const std::string& n) {
    static const std::set<std::string> k = {
        "template", "static_cast", "dynamic_cast", "const_cast", "reinterpret_cast", "std::function", "std::vector", "std::list",
        "std::map", "std::set", "std::array", "std::deque", "std::tuple", "std::pair", "std::optional", "std::unique_ptr",
        "std::shared_ptr", "std::weak_ptr", "std::unordered_map", "std::unordered_set", "std::multimap", "std::multiset",
        "std::variant", "std::span", "std::numeric_limits", "std::make_unique", "std::make_shared", "std::atomic", "std::queue",
        "std::stack", "std::priority_queue", "std::initializer_list", "std::basic_string"};
    return k.count(n) > 0;
}
// Le C++ qui ne se lit pas en C ; les chevrons d'un template ; la forme d'une lambda.
void cShapes(const std::vector<CTok>& t, ScriptLang lang, std::vector<ScriptDiagnostic>& out) {
    using S = ScriptDiagnostic::Severity;
    const auto is = [&](std::size_t k, const char* s) { return k < t.size() && t[k].text == s; };
    // [ en tete d'expression (apres =, (, ,, return, {, ;) : une lambda si ses parametres ou son corps suivent le ].
    const auto lambdaAt = [&](std::size_t k, bool& shaped) {
        shaped = false;
        if (!is(k, "[") || is(k + 1, "[") || (k > 0 && t[k - 1].text == "[")) return false;   // [[nodiscard]] : un attribut
        if (k > 0) {
            const std::string& p = t[k - 1].text;
            if (!(p == "=" || p == "(" || p == "," || p == "return" || p == "{" || p == ";")) return false;
        }
        std::size_t j = k + 1;
        int depth = 1;
        for (; j < t.size() && depth > 0; ++j) {
            if (t[j].text == "[") ++depth;
            else if (t[j].text == "]") --depth;
        }
        if (depth != 0) return false;
        shaped = j < t.size() && (t[j].text == "(" || t[j].text == "{" || t[j].text == "mutable" || t[j].text == "-" || t[j].text == "<");
        return true;
    };
    std::set<std::string> said;
    for (std::size_t k = 0; k < t.size(); ++k) {
        const CTok& x = t[k];
        bool shaped = false;
        const bool lambda = lambdaAt(k, shaped);
        if (lang == ScriptLang::C) {
            std::string what, instead;
            if (x.k == CTok::K::Name && x.text.rfind("std::", 0) == 0) {
                what = x.text;
                instead = x.text == "std::function" ? "un pointeur de fonction (bool (*f)(float)) ou un type rappel du projet (T_Filtre)"
                                                    : "les tableaux et les fonctions du C";
            } else if (x.text == "template") {
                what = "template";
                instead = "une fonction par type (et _Generic pour choisir)";
            } else if (x.text == "nullptr") {
                what = "nullptr";
                instead = "NULL";
            } else if (x.text == "class" || x.text == "namespace" || x.text == "using") {
                what = x.text;
                instead = x.text == "using" ? "typedef" : "struct et des fonctions";
            } else if (lambda && shaped) {                   // un [4] = 29 (initialiseur designe) n'en est pas une
                what = "une lambda";
                instead = "une fonction nomm\xC3\xA9" "e, pass\xC3\xA9" "e par son nom";
            }
            if (!what.empty() && said.insert(what).second) out.push_back({S::Error, x.line, what + " est du C++ : en C, " + instead});
            continue;
        }
        if (x.k == CTok::K::Name && cppTemplateName(x.text) && is(k + 1, "<")) {
            int depth = 0, paren = 0;
            bool closed = false;
            for (std::size_t j = k + 1; j < t.size(); ++j) {
                const std::string& s = t[j].text;
                if (s == "<") ++depth;
                else if (s == ">") {
                    if (--depth == 0) {
                        closed = true;
                        break;
                    }
                } else if (s == "(") ++paren;
                else if (s == ")") {
                    if (--paren < 0) break;
                } else if ((s == ";" || s == "{" || s == "}") && paren == 0) break;
            }
            if (!closed) out.push_back({S::Error, x.line, "il manque le > qui ferme " + x.text + "<\xE2\x80\xA6"});
            continue;
        }
        if (lambda && !shaped) out.push_back({S::Error, x.line, "une lambda : apr\xC3\xA8s [&] viennent ses param\xC3\xA8tres (\xE2\x80\xA6) ou son corps { \xE2\x80\xA6 }"});
    }
}

// ---- 1.12.20 (lot C) : L'EN-TETE DU PROJET, en C et en C++ ------------------------------
// Les types elementaires en C (le C++ garde les noms du ST : l'en-tete les definit).
std::string cElementary(const std::string& u) {
    if (u == "BOOL") return "bool";
    if (u == "REAL") return "float";
    if (u == "LREAL") return "double";
    if (u == "CHAR") return "char";
    if (u == "TIME" || u == "LTIME") return "int32_t";
    if (u == "DATE" || u == "TOD" || u == "DT" || u == "DATE_AND_TIME" || u == "TIME_OF_DAY" || u == "LDATE" || u == "LTOD" || u == "LDT")
        return "int64_t";
    const auto c = sim::lib::cTypeName(u);
    return c.empty() ? std::string{} : std::string(c);
}
bool stringName(const std::string& u) { return u == "STRING" || u == "WSTRING" || u.rfind("STRING[", 0) == 0; }
std::string trimmedCopy(std::string_view s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(a, b - a + 1));
}
std::vector<std::int64_t> dimensionsOf(const std::string& bounds) {
    std::vector<std::int64_t> out;
    std::size_t at = 0;
    while (at <= bounds.size()) {
        const auto comma = bounds.find(',', at);
        const std::string one = bounds.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (const auto dots = one.find(".."); dots != std::string::npos) {
            const long long lo = std::atoll(one.substr(0, dots).c_str()), hi = std::atoll(one.substr(dots + 2).c_str());
            out.push_back(hi >= lo ? hi - lo + 1 : 0);
        }
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return out;
}
std::vector<std::string> typeList(const std::string& text) {
    std::vector<std::string> out;
    int depth = 0;
    std::string cur;
    for (const char c : text) {
        if (c == '(' || c == '[') ++depth;
        if (c == ')' || c == ']') --depth;
        if (c == ',' && depth == 0) {
            out.push_back(trimmedCopy(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!trimmedCopy(cur).empty()) out.push_back(trimmedCopy(cur));
    return out;
}
// Le type en C++ : les formes du ST deviennent celles de la bibliotheque standard.
std::string cppTypeOf(const Project& p, std::string_view text) {
    const std::string t = trimmedCopy(text);
    const std::string u = upper(t);
    if (u.rfind("CALLBACK", 0) == 0) {
        typeform::CallbackParts parts;
        if (!typeform::callbackParts(p, t, parts)) return "std::function<void()>";
        std::string out = "std::function<" + (parts.result.empty() ? std::string("void") : cppTypeOf(p, parts.result)) + "(";
        for (std::size_t k = 0; k < parts.params.size(); ++k) out += (k ? ", " : "") + cppTypeOf(p, parts.params[k].second);
        return out + ")>";
    }
    // 1.12.20 (lot G) : l'instance d'une structure generique - T_Mesure<REAL>, ses types en C++.
    std::string base;
    std::vector<std::string> args;
    if (splitGenericName(t, base, args)) {
        std::string out = base + "<";
        for (std::size_t k = 0; k < args.size(); ++k) out += (k ? ", " : "") + cppTypeOf(p, args[k]);
        return out + ">";
    }
    const auto s = typeform::decompose(t);
    switch (s.form) {
        case typeform::Form::Simple:
            if (s.reference) return cppTypeOf(p, s.element) + "*";
            if (stringName(u)) return "STRING";
            return cElementary(u).empty() ? t : u;
        case typeform::Form::Array: case typeform::Form::Array2D: {
            std::string out = cppTypeOf(p, s.element);
            const auto dims = dimensionsOf(s.parameter);
            for (auto it = dims.rbegin(); it != dims.rend(); ++it) out = "std::array<" + out + ", " + std::to_string(*it) + ">";
            return out;
        }
        case typeform::Form::List: return "std::list<" + cppTypeOf(p, s.element) + ">";
        case typeform::Form::Vector: return "std::vector<" + cppTypeOf(p, s.element) + ">";
        case typeform::Form::Set: return "std::set<" + cppTypeOf(p, s.element) + ">";
        case typeform::Form::Ring: return "std::deque<" + cppTypeOf(p, s.element) + ">";
        case typeform::Form::Map: return "std::map<" + cppTypeOf(p, s.parameter.empty() ? std::string("STRING") : s.parameter) + ", " + cppTypeOf(p, s.element) + ">";
        case typeform::Form::Tuple: {
            std::string out = "std::tuple<" + cppTypeOf(p, s.element);
            for (const auto& more : typeList(s.parameter)) out += ", " + cppTypeOf(p, more);
            return out + ">";
        }
    }
    return t;
}
// Une declaration en C, le nom a sa place (un tableau : nom[N] ; un rappel : (*nom)(...)) ; vide : pas de C pour ce type.
// `value` : un parametre ou un retour (un texte : const char*) ; sinon une variable ou un membre (char nom[81]).
std::string cDeclOf(const Project& p, std::string_view text, const std::string& name, bool value) {
    const std::string t = trimmedCopy(text);
    const std::string u = upper(t);
    const auto join = [&](const std::string& type) { return name.empty() ? type : type + " " + name; };
    if (u.rfind("CALLBACK", 0) == 0) {
        typeform::CallbackParts parts;
        if (!typeform::callbackParts(p, t, parts)) return {};
        const std::string result = parts.result.empty() ? std::string("void") : cDeclOf(p, parts.result, {}, true);
        if (result.empty()) return {};
        std::string out = result + " (*" + name + ")(";
        if (parts.params.empty()) out += "void";
        for (std::size_t k = 0; k < parts.params.size(); ++k) {
            const std::string d = cDeclOf(p, parts.params[k].second, parts.params[k].first, true);
            if (d.empty()) return {};
            out += (k ? ", " : "") + d;
        }
        return out + ")";
    }
    // 1.12.20 (lot G) : l'instance d'une structure generique - sa macro, T_Mesure(float).
    std::string base;
    std::vector<std::string> args;
    if (splitGenericName(t, base, args)) {
        std::string out = base + "(";
        for (std::size_t k = 0; k < args.size(); ++k) {
            const std::string a = cDeclOf(p, args[k], {}, false);
            if (a.empty()) return {};
            out += (k ? ", " : "") + a;
        }
        return join(out + ")");
    }
    const auto s = typeform::decompose(t);
    if (s.form == typeform::Form::Simple) {
        if (s.reference) {
            const std::string inner = cDeclOf(p, s.element, {}, true);
            return inner.empty() ? std::string{} : join(inner + "*");
        }
        if (stringName(u)) return value || name.empty() ? join("const char*") : "char " + name + "[81]";
        if (const std::string c = cElementary(u); !c.empty()) return join(c);
        return join(t);                                             // un type du projet : son typedef
    }
    if (s.form == typeform::Form::Array || s.form == typeform::Form::Array2D) {
        std::string inner = cDeclOf(p, s.element, name, false);
        if (inner.empty() || name.empty()) return {};
        for (const auto n : dimensionsOf(s.parameter)) inner += "[" + std::to_string(n) + "]";
        return inner;
    }
    return {};                                                      // LIST, VECTOR, MAP, TUPLE, SET, RING : pas en C
}

// Les delimiteurs d'un code C / C++, avec la ligne de la faute ; 1.12.20 (lot C) : puis les chevrons d'un template,
// la forme d'une lambda, le C++ ecrit dans un script C (cShapes).
std::vector<ScriptDiagnostic> checkC(std::string_view code, ScriptLang lang) {
    std::vector<ScriptDiagnostic> out;
    struct Open { char c; int line; };
    std::vector<Open> stack;
    bool inString = false, inChar = false, lineComment = false, blockComment = false;
    int line = 1, stringLine = 0, commentLine = 0;
    for (std::size_t i = 0; i < code.size(); ++i) {
        const char c = code[i];
        const char n = i + 1 < code.size() ? code[i + 1] : '\0';
        if (c == '\n') {
            ++line;
            if (inString || inChar) {
                out.push_back({ScriptDiagnostic::Severity::Error, stringLine,
                               inString ? "cha\xC3\xAEne non ferm\xC3\xA9" "e en fin de ligne" : "caract\xC3\xA8re non ferm\xC3\xA9"});
                inString = inChar = false;
            }
            lineComment = false;
            continue;
        }
        if (lineComment) continue;
        if (blockComment) { if (c == '*' && n == '/') { blockComment = false; ++i; } continue; }
        if (inString) { if (c == '\\') ++i; else if (c == '"') inString = false; continue; }
        if (inChar) { if (c == '\\') ++i; else if (c == '\'') inChar = false; continue; }
        if (c == '/' && n == '/') { lineComment = true; ++i; continue; }
        if (c == '/' && n == '*') { blockComment = true; commentLine = line; ++i; continue; }
        if (c == '"') { inString = true; stringLine = line; continue; }
        if (c == '\'') { inChar = true; stringLine = line; continue; }
        if (c == '(' || c == '{' || c == '[') stack.push_back({c, line});
        if (c == ')' || c == '}' || c == ']') {
            const char want = c == ')' ? '(' : c == '}' ? '{' : '[';
            if (stack.empty() || stack.back().c != want) {
                std::string msg = std::string("'") + c + "' sans ouverture correspondante";
                if (!stack.empty())
                    msg += " (le '" + std::string(1, stack.back().c) + "' de la ligne " + std::to_string(stack.back().line)
                         + " est encore ouvert)";
                out.push_back({ScriptDiagnostic::Severity::Error, line, msg});
                return out;
            }
            stack.pop_back();
        }
    }
    if (inString) out.push_back({ScriptDiagnostic::Severity::Error, stringLine, "cha\xC3\xAEne non ferm\xC3\xA9" "e"});
    if (blockComment) out.push_back({ScriptDiagnostic::Severity::Error, commentLine, "commentaire /* non ferm\xC3\xA9"});
    if (!stack.empty())
        out.push_back({ScriptDiagnostic::Severity::Error, stack.back().line,
                       std::string("'") + stack.back().c + "' ouvert ici n'est jamais ferm\xC3\xA9"});
    if (out.empty()) cShapes(cTokens(code), lang, out);      // des delimiteurs justes d'abord
    return out;
}

const std::set<std::string>& stKeywords() {
    static const std::set<std::string> k = {
        "AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE", "IF", "THEN", "ELSIF", "ELSE", "END_IF", "CASE", "OF",
        "END_CASE", "FOR", "TO", "BY", "DO", "END_FOR", "WHILE", "END_WHILE", "REPEAT", "UNTIL", "END_REPEAT",
        "EXIT", "RETURN",
        "CONTINUE", "TRY", "CATCH", "END_TRY", "ENTRE",    // 1.12.1 : le dialecte de l'IHM
        "CALLBACK", "LAMBDA", "END_LAMBDA", "TEMPLATE", "NULL"};   // 1.12.19 : les rappels et les generiques
    return k;
}
// 1.12.1 : x ENTRE a ET b - ET n'est un mot du langage qu'apres ENTRE (une variable peut
// s'appeler ET) : les noms d'un script le sautent, il n'est pas reserve.
bool operatorWord(const std::string& upperWord) { return upperWord == "ET"; }

// Les arguments textes d'un appel : IHM_APPELER('X') -> X, pour chaque appel.
std::vector<std::string> quotedArgs(std::string_view body, std::string_view function) {
    std::vector<std::string> out;
    const std::string up = upper(body);
    const std::string f = upper(function);
    for (std::size_t at = up.find(f); at != std::string::npos; at = up.find(f, at + f.size())) {
        if (at > 0 && identChar(up[at - 1])) continue;
        std::size_t k = at + f.size();
        while (k < body.size() && std::isspace(static_cast<unsigned char>(body[k]))) ++k;
        if (k >= body.size() || body[k] != '(') continue;
        ++k;
        while (k < body.size() && std::isspace(static_cast<unsigned char>(body[k]))) ++k;
        if (k >= body.size() || (body[k] != '\'' && body[k] != '"')) continue;
        const char q = body[k];
        const auto end = body.find(q, k + 1);
        if (end == std::string_view::npos) continue;
        out.emplace_back(body.substr(k + 1, end - k - 1));
    }
    return out;
}

} // namespace

std::vector<NameUse> scriptNamesRaw(std::string_view s);

int splitLine(std::string_view m, std::string* rest) {
    // "line 3: expected THEN" (le parseur) ; "ligne 3 : ..." (deja traduit).
    for (std::string_view prefix : {std::string_view("line "), std::string_view("ligne ")}) {
        if (!startsWith(m, prefix)) continue;
        std::size_t k = prefix.size();
        int line = 0;
        while (k < m.size() && std::isdigit(static_cast<unsigned char>(m[k]))) line = line * 10 + (m[k++] - '0');
        while (k < m.size() && (m[k] == ' ' || m[k] == ':')) ++k;
        if (rest) *rest = std::string(m.substr(k));
        return line;
    }
    if (rest) *rest = std::string(m);
    return 0;
}

std::string frenchSimMessage(std::string_view english) {
    std::string rest;
    const int line = splitLine(english, &rest);
    // "expected END_IF (found '')" : ce que le parseur a trouve a la place.
    std::string found;
    bool hasFound = false;
    if (const auto at = rest.rfind(" (found '"); at != std::string::npos && !rest.empty() && rest.back() == ')') {
        found = rest.substr(at + 9, rest.size() - at - 9 - 2);
        rest.resize(at);
        hasFound = true;
    }
    std::string m = rest;
    const auto replaceAll = [&](std::string_view from, std::string_view to) {
        for (auto at = m.find(from); at != std::string::npos; at = m.find(from, at + to.size())) m.replace(at, from.size(), to);
    };
    std::string out;
    if (startsWith(m, "expected ")) {
        std::string what = m.substr(9);
        if (what == "a value") out = "une valeur est attendue";
        else if (what == "':=' or a call") out = "':=' ou un appel attendu";
        else if (startsWith(what, "the ")) out = what.substr(4) + " attendu";
        else if (startsWith(what, "a ")) out = what.substr(2) + " attendu";
        else out = what + " attendu";
        // Quelques mots du parseur, en francais.
        const std::pair<const char*, const char*> words[] = {
            {"loop variable", "la variable de boucle"}, {"case label", "une \xC3\xA9tiquette de CASE"},
            {"end of the range", "la fin de l'intervalle"}, {"after the case labels", "apr\xC3\xA8s les \xC3\xA9tiquettes"},
            {"after the loop variable", "apr\xC3\xA8s la variable de boucle"}};
        for (const auto& [en, fr] : words)
            for (auto at = out.find(en); at != std::string::npos; at = out.find(en, at + std::strlen(fr))) out.replace(at, std::strlen(en), fr);
    } else if (m.find("cannot be written") != std::string::npos) {
        replaceAll(" cannot be written", " ne peut pas \xC3\xAAtre \xC3\xA9" "crite (inconnue, ou en lecture seule)");
        out = m;
    } else if (m.find("is not a function or a block this simulator knows") != std::string::npos) {
        replaceAll(" is not a function or a block this simulator knows", " : fonction ou bloc inconnu du simulateur");
        out = m;
    } else if (startsWith(m, "WHILE loop ran more than") || startsWith(m, "REPEAT loop ran more than")
               || startsWith(m, "FOR loop ran more than")) {
        const std::string kind = m.substr(0, m.find(' '));
        const auto from = m.find("more than ") + 10;
        const std::string count = m.substr(from, m.find(' ', from) - from);
        out = "boucle " + kind + " : plus de " + count + " tours, arr\xC3\xAAt\xC3\xA9" "e (boucle sans fin ?)";
    } else if (startsWith(m, "the scan exceeded")) {
        const auto from = m.find("exceeded ") + 9;
        const std::string count = m.substr(from, m.find(' ', from) - from);
        out = "plus de " + count + " instructions : arr\xC3\xAAt\xC3\xA9 (une boucle ne se termine sans doute pas)";
    } else if (startsWith(m, "the scan ran for more than its time limit")) {     // 1.12.7 : un calcul au demarrage (2 s)
        out = "trop long : arr\xC3\xAAt\xC3\xA9 \xC3\xA0 son heure limite (une boucle ne se termine sans doute pas)";
    } else if (m == "division by zero") {
        out = "division par z\xC3\xA9ro";
    } else if (m == "modulo by zero") {
        out = "modulo par z\xC3\xA9ro";
    } else if (m == "FOR loop with a step of zero would never end") {
        out = "boucle FOR de pas nul : elle ne finirait jamais";
    } else if (m == "bad loop bounds") {
        out = "bornes de boucle illisibles";
    } else if (m.find("has no output called") != std::string::npos) {
        replaceAll(" has no output called ", " n'a pas de sortie ");
        out = m;
    } else if (startsWith(m, "bad date literal ")) {        // 1.12.1
        out = "date ou heure illisible : " + m.substr(17) + " (D#2026-10-09, TOD#14:30:00, DT#2026-10-09-14:30:00)";
    } else if (startsWith(m, "bad time literal ")) {
        out = "dur\xC3\xA9" "e illisible : " + m.substr(17) + " (T#5s, T#1m30s, T#250ms)";
    } else if (startsWith(m, "unexpected character ")) {
        out = "caract\xC3\xA8re inattendu : " + m.substr(21);
    } else if (startsWith(m, "unexpected ")) {
        out = "inattendu : " + m.substr(11);
    } else {
        out = m;
    }
    if (hasFound) out += found.empty() ? std::string(" (fin du script atteinte)") : " (trouv\xC3\xA9 : '" + found + "')";
    return line ? "ligne " + std::to_string(line) + " : " + out : out;
}

std::vector<ScriptDiagnostic> checkScript(ScriptLang lang, std::string_view code, std::string_view name, const TypeKnown& knownType) {
    std::vector<ScriptDiagnostic> out;
    // 1.11.1 (REP) : un script ST se lit sans les $ de ses reperes, comme une expression
    // (les chaines ST, (* *) et // gardent les leurs) ; les lignes ne bougent pas. Le C et
    // le C++ ne sont pas compiles : checkC ne lit pas les $ (rien n'est retire).
    // 1.13.0 (lot J8) : ses directives USING (blanchies : les lignes et les colonnes restent).
    const std::string st = lang == ScriptLang::ST ? names::blankUsings(markers::strip(code)) : std::string{};
    const std::string_view body = lang == ScriptLang::ST ? std::string_view(st) : code;
    if (lang != ScriptLang::ST) {
        out = checkC(body, lang);
        out.push_back({ScriptDiagnostic::Severity::Info, 0,
                       std::string(scriptLangKey(lang)) + " : \xC3\xA9" "dit\xC3\xA9 et v\xC3\xA9rifi\xC3\xA9, non ex\xC3\xA9" "cut\xC3\xA9 en simulation"});
        return out;
    }
    if (body.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        out.push_back({ScriptDiagnostic::Severity::Warning, 0, "script vide"});
        return out;
    }
    // Lot 7 : les blocs VAR / VAR_TEMP d'abord ; le simulateur lit le reste.
    const auto parts = splitDeclarations(body, false, knownType);
    out.insert(out.end(), parts.errors.begin(), parts.errors.end());
    auto program = sim::parse(parts.body, std::string(name), dialectOptions());   // 1.10 : le dialecte IHM
    if (!program) {
        const std::string raw = program.error().context.empty() ? program.error().message() : program.error().context;
        std::string rest;
        const int line = splitLine(raw, &rest);
        // 1.11.2 (REP) : un $ reste seul (le $ de fin d'un repere oublie ?) - comme dans une expression, la
        // faute dite a sa place avec la ligne juste, puis la regle ("il manque le $ de fin de $V[1] : ecris
        // y := $V[1]$; - un repere s'ecrit entre deux $..."), au lieu du seul "caractere inattendu : '$'".
        std::string fr = markers::explainScriptDollar(frenchSimMessage(rest), code, line);
        out.push_back({ScriptDiagnostic::Severity::Error, line, fr});
    }
    return out;
}

// 1.12.20 (lot C) : un nom de fonction du projet mal ecrit dans un script C ou C++ (voir HmiScript.hpp).
std::vector<ScriptDiagnostic> checkCNames(const Project& p, const View* view, std::string_view code) {
    std::vector<ScriptDiagnostic> out;
    std::vector<std::string> pool;
    std::set<std::string> known;
    for (const auto& f : p.programs.functions) {
        pool.push_back(f.name);
        known.insert(upper(f.name));
    }
    if (view)
        for (const auto& f : view->functions) {
            pool.push_back(f.name);
            known.insert(upper(f.name));
        }
    if (pool.empty()) return out;
    for (const auto& v : p.programs.variables) known.insert(upper(v.name));
    for (const auto& ty : p.programs.types) known.insert(upper(ty.name));
    const auto t = cTokens(code);
    // Les noms que le script declare : apres un type (un nom, *, &, >) - int x, T_Filtre garder, std::function<...> f.
    static const std::set<std::string> notTypes = {"return", "case", "goto", "else", "do", "sizeof", "new", "delete", "throw"};
    std::set<std::string> declared;
    for (std::size_t k = 1; k < t.size(); ++k) {
        if (t[k].k != CTok::K::Name) continue;
        const CTok& prev = t[k - 1];
        if ((prev.k == CTok::K::Name && !notTypes.count(prev.text)) || prev.text == "*" || prev.text == ">"
            || (prev.text == "&" && k >= 2 && t[k - 2].k == CTok::K::Name))
            declared.insert(upper(t[k].text));
    }
    std::set<std::string> said;
    for (std::size_t k = 0; k < t.size(); ++k) {
        const CTok& x = t[k];
        if (x.k != CTok::K::Name || x.text.find("::") != std::string::npos || cWord(x.text)) continue;
        if (k > 0 && (t[k - 1].text == "." || (t[k - 1].text == ">" && k > 1 && t[k - 2].text == "-"))) continue;   // un membre
        const std::string u = upper(x.text);
        if (known.count(u) || declared.count(u) || isHmiFunction(u) || natives::function(u) || sim::lib::native(u)) continue;
        const std::string hint = sim::lib::closest(x.text, pool);
        if (hint.empty() || !said.insert(u).second) continue;
        out.push_back({ScriptDiagnostic::Severity::Warning, x.line, x.text + " n'est pas une fonction du projet" + hint});
    }
    return out;
}

// 1.12.20 (lot C) : l'en-tete du projet (voir HmiScript.hpp).
std::string projectHeader(const Project& p, ScriptLang lang, const View* view) {
    const bool cpp = lang == ScriptLang::Cpp;
    std::string out;
    const auto line = [&](const std::string& s) { out += s + "\n"; };
    const auto comment = [&](const std::string& s) { line(cpp ? "// " + s : "/* " + s + " */"); };
    comment("En-t\xC3\xAAte du projet " + p.config.name + " (lecture seule) : ses types, ses rappels, ses fonctions, ses variables.");
    comment(cpp ? "C++ : \xC3\xA9" "dit\xC3\xA9 et v\xC3\xA9rifi\xC3\xA9, non ex\xC3\xA9" "cut\xC3\xA9 en simulation (seul le ST y tourne)."
                : "C : \xC3\xA9" "dit\xC3\xA9 et v\xC3\xA9rifi\xC3\xA9, non ex\xC3\xA9" "cut\xC3\xA9 en simulation (seul le ST y tourne).");
    if (cpp) {
        line("#include <array>\n#include <cstdint>\n#include <deque>\n#include <functional>\n#include <list>\n#include <map>\n#include <set>\n#include <string>\n#include <tuple>\n#include <vector>");
        line("");
        comment("Les types du ST, en C++.");
        line("using BOOL = bool;          using REAL = float;           using LREAL = double;\n"
             "using SINT = std::int8_t;   using INT = std::int16_t;     using DINT = std::int32_t;    using LINT = std::int64_t;\n"
             "using USINT = std::uint8_t; using UINT = std::uint16_t;   using UDINT = std::uint32_t;  using ULINT = std::uint64_t;\n"
             "using BYTE = std::uint8_t;  using WORD = std::uint16_t;   using DWORD = std::uint32_t;  using LWORD = std::uint64_t;\n"
             "using STRING = std::string; using TIME = std::int32_t;    using DATE_AND_TIME = std::int64_t;");
    } else {
        line("#include <stdbool.h>\n#include <stdint.h>");
    }
    // Les types IHM : enumerations, structures, rappels.
    bool first = true;
    for (const auto& ty : p.programs.types) {
        if (first) {
            line("");
            comment("Les types IHM");
            first = false;
        }
        if (ty.kind == HmiTypeKind::Enumeration) {
            std::string values;
            for (std::size_t k = 0; k < ty.values.size(); ++k)
                values += (k ? ", " : "") + ty.values[k].name + " = " + std::to_string(ty.values[k].value);
            line(cpp ? "enum class " + ty.name + " : DINT { " + values + " };" : "typedef enum { " + values + " } " + ty.name + ";");
            continue;
        }
        if (ty.kind == HmiTypeKind::Callback) {
            if (cpp) line("using " + ty.name + " = " + cppTypeOf(p, ty.signature) + ";");
            else if (const std::string d = cDeclOf(p, ty.signature, ty.name, true); !d.empty()) line("typedef " + d + ";");
            continue;
        }
        // 1.12.20 (lot G) : une structure generique - un template en C++, une macro en C (une structure par type).
        std::string names, tpl, constraints;
        for (const auto& g : ty.generics) {
            names += (names.empty() ? "" : ", ") + g.name;
            tpl += (tpl.empty() ? "" : ", ") + std::string("typename ") + g.name;
            if (!g.constraint.empty()) constraints += (constraints.empty() ? "" : ", ") + g.name + " : " + g.constraint;
        }
        const bool macro = !cpp && !ty.generics.empty();
        const std::string tail = macro ? " \\" : "";
        if (cpp && !ty.generics.empty()) line("template <" + tpl + ">" + (constraints.empty() ? std::string{} : "   // " + constraints));
        line(cpp ? "struct " + ty.name + " {" : macro ? "#define " + ty.name + "(" + names + ") struct {" + tail : "typedef struct {");
        for (const auto& m : ty.members) {
            if (cpp) {
                std::string init;
                const std::string iu = upper(trimmedCopy(m.initial));
                if (iu == "TRUE" || iu == "FALSE") init = iu == "TRUE" ? " = true" : " = false";
                else if (!iu.empty() && (std::isdigit(static_cast<unsigned char>(iu[0])) || iu[0] == '-')) init = " = " + trimmedCopy(m.initial);
                line("    " + cppTypeOf(p, m.type) + " " + m.name + init + ";");
            } else if (const std::string d = cDeclOf(p, m.type, m.name, false); !d.empty()) {
                line("    " + d + ";" + tail);
            } else {
                line("    /* " + m.name + " : " + m.type + " - pas de C pour ce type */" + tail);
            }
        }
        line(cpp ? std::string("};") : macro ? "}" + (constraints.empty() ? std::string{} : "   /* " + constraints + " */") : "} " + ty.name + ";");
    }
    // Les fonctions IHM (et celles de la vue du script) : leurs prototypes.
    std::vector<const HmiFunction*> fns;
    for (const auto& f : p.programs.functions) fns.push_back(&f);
    if (view)
        for (const auto& f : view->functions) fns.push_back(&f);
    if (!fns.empty()) {
        line("");
        comment(view ? "Les fonctions IHM, et celles de " + view->name : std::string("Les fonctions IHM"));
    }
    for (const HmiFunction* f : fns) {
        // 1.13.0 : une fonction generique (TEMPLATE <T>) - en C++, un template ; en C, un commentaire (pas de prototype
        //  unique, comme une structure generique), chacun appelant la version du type voulu.
        if (!f->generics.empty() && !cpp) {
            std::string names, constraints;
            for (const auto& g : f->generics) {
                names += (names.empty() ? "" : ", ") + g.name;
                if (!g.constraint.empty()) constraints += (constraints.empty() ? "" : ", ") + g.name + " : " + g.constraint;
            }
            comment(f->name + "<" + names + "> : fonction g\xC3\xA9n\xC3\xA9rique (template <" + names + ">)"
                    + (constraints.empty() ? std::string{} : " \xE2\x80\x94 " + constraints) + " - pas de prototype C unique");
            continue;
        }
        std::string params;
        bool ok = true;
        for (const auto& d : f->decls) {
            if (d.kind != DeclKind::Parameter) continue;
            std::string one;
            if (cpp) one = cppTypeOf(p, d.type) + (d.mode == PassMode::In ? " " : "& ") + d.name;
            else {
                one = d.mode == PassMode::In ? cDeclOf(p, d.type, d.name, true) : cDeclOf(p, d.type, "*" + d.name, true);
                ok = ok && !one.empty();
            }
            params += (params.empty() ? "" : ", ") + one;
        }
        const std::string ret = f->returnType.empty() ? std::string("void") : cpp ? cppTypeOf(p, f->returnType) : cDeclOf(p, f->returnType, {}, true);
        if (!ok || ret.empty()) {
            comment(f->name + " : un param\xC3\xA8tre ou un retour sans \xC3\xA9quivalent en C (une liste, une MAP)");
            continue;
        }
        // 1.13.0 : en C++, le template devant le prototype (template <typename T> T Max(T a, T b);).
        if (!f->generics.empty()) {
            std::string tpl, constraints;
            for (const auto& g : f->generics) {
                tpl += (tpl.empty() ? "" : ", ") + std::string("typename ") + g.name;
                if (!g.constraint.empty()) constraints += (constraints.empty() ? "" : ", ") + g.name + " : " + g.constraint;
            }
            line("template <" + tpl + ">" + (constraints.empty() ? std::string{} : "   // " + constraints));
        }
        line(ret + " " + f->name + "(" + (params.empty() && !cpp ? std::string("void") : params) + ");");
    }
    // Les variables IHM.
    if (!p.programs.variables.empty()) {
        line("");
        comment("Les variables IHM");
    }
    for (const auto& v : p.programs.variables) {
        if (cpp) {
            line("extern " + cppTypeOf(p, v.type) + " " + v.name + ";");
            continue;
        }
        const std::string d = cDeclOf(p, v.type, v.name, false);
        if (d.empty()) comment(v.name + " : " + v.type + " - pas d'\xC3\xA9quivalent en C");
        else line("extern " + d + ";");
    }
    return out;
}

std::vector<ScriptDiagnostic> checkScript(const Script& sc, const TypeKnown& knownType) {
    if (sc.decls.empty() || sc.lang != ScriptLang::ST) return checkScript(sc.lang, sc.body, sc.name, knownType);
    // 1.11.18 (refonte, lot 3) : ses declarations du modele d'abord ; le code se lit avec
    // celles qui sont justes, reconstruites sur sa ligne 1 (les lignes ne bougent pas).
    std::vector<Declaration> valid;
    auto out = decl::checkDeclarations(sc.decls, decl::Role::Script, sc.body, knownType, nullptr, &valid);
    if (sc.body.find_first_not_of(" \t\r\n") == std::string::npos) {
        out.push_back({ScriptDiagnostic::Severity::Warning, 0, "script vide"});
        return out;
    }
    const auto composed = decl::composeCode(sc.body, valid, decl::Role::Script);
    for (auto d : checkScript(sc.lang, composed.text, sc.name, knownType)) {
        d.column = composed.bodyColumn(d.line, d.column);
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<NameUse> scriptNames(std::string_view source) {
    // 1.11.1 (REP) : les $ des reperes, transparents ; 1.13.0 (lot J8) : les directives USING ne nomment rien.
    const std::string code = names::blankUsings(markers::strip(source));
    // Lot 7 : ce que les blocs de declaration nomment n'est pas lu ailleurs, et
    // une variable locale n'est ni IHM ni de l'automate. 1.10 : une locale de
    // type riche (MAP, ARRAY, REF_TO, un type IHM) reste une locale ; les
    // parametres et locales des fonctions internes, les noms de FOR EACH aussi.
    const auto parts = splitDeclarations(code, true, [](std::string_view) { return true; });
    const auto lang = lang110::analyze(code);
    const auto dialectName = [&](const std::string& name) {
        const std::string u = upper(name);
        for (const auto& f : lang.functions) {
            if (upper(f.name) == u) return true;
            for (const auto& p : f.params) if (upper(p.name) == u) return true;
            for (const auto& l : f.locals) if (upper(l.name) == u) return true;
        }
        for (const auto& n : lang.names) if (upper(n.name) == u) return true;
        return false;
    };
    // 1.12.20 : les parametres des LAMBDA (LAMBDA (x : REAL; y : INT) ...) ne sont ni IHM ni de l'automate.
    std::set<std::string> lambdaParams;
    {
        const std::string u = upper(parts.body);
        for (auto at = u.find("LAMBDA"); at != std::string::npos; at = u.find("LAMBDA", at + 6)) {
            if ((at > 0 && identChar(u[at - 1])) || (at + 6 < u.size() && identChar(u[at + 6]))) continue;
            std::size_t j = at + 6;
            while (j < u.size() && std::isspace(static_cast<unsigned char>(u[j]))) ++j;
            if (j >= u.size() || u[j] != '(') continue;
            int depth = 0;
            bool naming = true;
            for (; j < u.size(); ++j) {
                const char c = u[j];
                if (c == '(') { ++depth; if (depth == 1) naming = true; continue; }
                if (c == ')' && --depth == 0) break;
                if (depth != 1) continue;
                if (c == ';' || c == ',') naming = true;
                else if (c == ':') naming = false;
                else if (naming && identStart(c)) {
                    std::size_t e = j;
                    while (e < u.size() && identChar(u[e])) ++e;
                    lambdaParams.insert(u.substr(j, e - j));
                    j = e - 1;
                }
            }
        }
    }
    std::vector<NameUse> all = scriptNamesRaw(parts.body);
    std::vector<NameUse> out;
    for (auto& u : all)
        if (!parts.local(u.name) && !dialectName(u.name) && !lambdaParams.count(upper(u.name))) out.push_back(std::move(u));
    return out;
}

std::vector<NameUse> scriptNamesRaw(std::string_view s) {
    std::vector<NameUse> out;
    int line = 1;
    std::size_t i = 0;
    std::string open;   // 1.12.14 : les parentheses et crochets ouverts, le plus interieur a la fin
    const auto advanceTo = [&](std::size_t to) {
        for (; i < to && i < s.size(); ++i) if (s[i] == '\n') ++line;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '\'' || c == '"') {
            const auto end = s.find(c, i + 1);
            advanceTo(end == std::string_view::npos ? s.size() : end + 1);
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            advanceTo(end == std::string_view::npos ? s.size() : end + 2);
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            advanceTo(end == std::string_view::npos ? s.size() : end);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            while (i < s.size() && (identChar(s[i]) || s[i] == '#' || s[i] == '.')) ++i;
            continue;
        }
        if (identStart(c)) {
            const std::size_t start = i;
            while (i < s.size() && identChar(s[i])) ++i;
            const std::string word(s.substr(start, i - start));
            const bool member = start > 0 && s[start - 1] == '.';
            const bool typed = i < s.size() && s[i] == '#';
            std::size_t k = i;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            const bool call = k < s.size() && s[k] == '(';
            // 1.12.14 : (Nom := 'A', Niveau := 2.5) - le membre nomme d'une structure ecrite (ou l'argument
            // nomme d'un appel) n'est pas une variable : un nom suivi de :=, juste apres ( ou une virgule,
            // entre parentheses. Une cle de MAP ecrite ([Cle := 3]) reste lue.
            if (!open.empty() && open.back() == '(' && k + 1 < s.size() && s[k] == ':' && s[k + 1] == '=') {
                std::size_t b = start;
                while (b > 0 && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) --b;
                if (b > 0 && (s[b - 1] == '(' || s[b - 1] == ',')) continue;
            }
            if (typed) {
                ++i;
                while (i < s.size() && (identChar(s[i]) || s[i] == '.')) ++i;
                continue;
            }
            // 1.12.7 : un template (NUMERIC_LIMITS<UINT>.MAX(), NUMBERS.PI, ADD_SAT<INT>(a, b)) n'est pas
            // une variable, ni son type entre < >.
            if (!member && sim::lib::templateInfo(word)) {
                if (k < s.size() && s[k] == '<') {
                    std::size_t a = k + 1;
                    while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
                    std::size_t b = a;
                    while (b < s.size() && identChar(s[b])) ++b;
                    while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
                    if (b > a && b < s.size() && s[b] == '>') advanceTo(b + 1);
                }
                continue;
            }
            // 1.11.14 : IHM_LOG(INFO, ...) - le premier argument est un niveau (NIVEAU_LOG),
            // pas une variable : il n'est pas lu ici (Compiler verifie que c'est un niveau).
            if (call && !member && upper(word) == "IHM_LOG") {
                std::size_t a = k + 1;
                while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
                std::size_t b = a;
                while (b < s.size() && identChar(s[b])) ++b;
                std::size_t after = b;
                while (after < s.size() && (s[after] == ' ' || s[after] == '\t')) ++after;
                if (b > a && identStart(s[a]) && after < s.size() && (s[after] == ',' || s[after] == ')')) advanceTo(b);
                continue;
            }
            if (member || call || stKeywords().count(upper(word)) || operatorWord(upper(word))) continue;
            const bool seen = std::any_of(out.begin(), out.end(), [&](const NameUse& u) { return upper(u.name) == upper(word); });
            if (!seen) out.push_back({word, line});
            continue;
        }
        if (c == '(' || c == '[') open += c;
        else if ((c == ')' || c == ']') && !open.empty()) open.pop_back();
        ++i;
    }
    return out;
}

std::vector<PathUse> scriptPaths(std::string_view source) {
    const std::string s = markers::strip(source);   // 1.11.1 (REP) : $Vanne$.Ouv se lit Vanne.Ouv (les lignes restent)
    std::vector<PathUse> out;
    int line = 1;
    std::size_t i = 0;
    const auto advanceTo = [&](std::size_t to) {
        for (; i < to && i < s.size(); ++i) if (s[i] == '\n') ++line;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '\'' || c == '"') {
            const auto end = s.find(c, i + 1);
            advanceTo(end == std::string_view::npos ? s.size() : end + 1);
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            advanceTo(end == std::string_view::npos ? s.size() : end + 2);
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            advanceTo(end == std::string_view::npos ? s.size() : end);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            while (i < s.size() && (identChar(s[i]) || s[i] == '#' || s[i] == '.')) ++i;
            continue;
        }
        if (identStart(c)) {
            const std::size_t start = i;
            const bool member = start > 0 && s[start - 1] == '.';
            while (i < s.size() && identChar(s[i])) ++i;
            if (i < s.size() && s[i] == '#') {           // T#5s, INT#3
                ++i;
                while (i < s.size() && (identChar(s[i]) || s[i] == '.')) ++i;
                continue;
            }
            if (member) continue;
            // La suite pointee : .nom.nom (un index l'arrete).
            std::size_t end = i;
            while (end + 1 < s.size() && s[end] == '.' && identStart(s[end + 1])) {
                end += 1;
                while (end < s.size() && identChar(s[end])) ++end;
            }
            std::size_t k = end;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            const bool call = k < s.size() && s[k] == '(';
            const bool assigned = k + 1 < s.size() && s[k] == ':' && s[k + 1] == '=';
            const std::string word(s.substr(start, i - start));
            if (!call && !stKeywords().count(upper(word)) && !operatorWord(upper(word)) && end > i)
                out.push_back({std::string(s.substr(start, end - start)), line, assigned});
            i = end;
            continue;
        }
        ++i;
    }
    return out;
}

std::vector<std::string> scriptCalls(std::string_view body) { return quotedArgs(body, "IHM_APPELER"); }

bool isReservedWord(std::string_view name) {
    static const std::set<std::string> blocks = {"VAR", "VAR_TEMP", "VAR_INPUT", "VAR_IN_OUT", "VAR_OUTPUT", "END_VAR", "CONSTANT",
                                                 "RETAIN", "FUNCTION", "END_FUNCTION"};
    const std::string u = upper(name);
    return stKeywords().count(u) > 0 || blocks.count(u) > 0;
}

std::vector<std::string> scriptViews(std::string_view body) {
    auto v = quotedArgs(body, "IHM_NAVIGUER");
    for (const char* f : {"IHM_POPUP", "IHM_CHANGER_POPUP", "IHM_CENTRER_POPUP", "IHM_POPUP_OUVERTE"}) {
        const auto p = quotedArgs(body, f);
        v.insert(v.end(), p.begin(), p.end());
    }
    return v;
}

// ================================================================ lot 7 =====
const LocalVar* ScriptParts::local(std::string_view name) const noexcept {
    const std::string u = upper(name);
    for (const auto& l : locals) if (upper(l.name) == u) return &l;
    return nullptr;
}

std::vector<const LocalVar*> ScriptParts::inputs() const {
    std::vector<const LocalVar*> out;
    for (const auto& l : locals) if (l.section == LocalVar::Section::Input) out.push_back(&l);
    return out;
}

std::vector<const LocalVar*> ScriptParts::parameters() const {
    std::vector<const LocalVar*> out;
    for (const auto& l : locals) if (l.isParameter()) out.push_back(&l);
    return out;
}

// 1.11.19 (refonte, lot 6) : les types d'une locale viennent du registre des types (ceux qu'une
// declaration propose : BOOL, SINT ... STRING, TIME), sous leur nom exact.
bool localTypeSupported(std::string_view type) noexcept {
    const std::string u = upper(type);
    const auto* e = typereg::baseRegistry().byName(u);
    return e && (e->proposed & typereg::UseDeclaration) != 0 && typereg::comparable(u) == e->name;   // TOD, DT : 1.12.1
}

bool richLocalType(std::string_view type, const std::function<bool(std::string_view)>& knownType) {
    std::string u = upper(type);
    while (!u.empty() && std::isspace(static_cast<unsigned char>(u.back()))) u.pop_back();
    if (u.empty()) return false;
    // 1.12.2 : LIST OF T, VECTOR OF T, TUPLE(T1, T2...) ; 1.12.19 : CALLBACK (x : REAL) : BOOL.
    // 1.13.0 : ANY - une valeur JSON / dynamique (ANY_NUM, ANY_STRUCT... restent des contraintes de TEMPLATE).
    for (std::string_view p : {"ARRAY", "MAP", "REF_TO", "REFERENCE", "POINTER", "STRING", "LIST", "VECTOR", "TUPLE", "CALLBACK", "ANY"})
        if (u.rfind(p, 0) == 0 && (u.size() == p.size() || !identChar(u[p.size()]))) return true;
    // 1.12.7 : SET OF T, RING[N] OF T (un type IHM nomme SET ou RING reste un nom).
    for (std::string_view p : {"SET", "RING"})
        if (u.rfind(p, 0) == 0 && u.size() > p.size() && !identChar(u[p.size()]) && u.find(" OF ") != std::string::npos) return true;
    if (u == "MAP_ITERATOR" || u == "ITERATOR") return true;
    // un type de base permis a une declaration (LINT, ULINT : sans etre proposes)
    if (const auto* e = typereg::baseRegistry().byName(u); e && e->usable(typereg::UseDeclaration) && u == e->name) return true;
    // Un type IHM (structure) : un nom que le projet connait.
    if (!identStart(u[0])) return false;
    // 1.12.20 (lot G) : T_Mesure<REAL> - l'instance d'une structure generique du projet.
    if (std::string base; u.find('<') != std::string::npos) {
        std::vector<std::string> args;
        if (splitGenericName(type, base, args)) return knownType && knownType(type);
    }
    // 1.13.0 (lot J8) : Gaz.T_Vanne - le type d'un espace de noms (son nom complet).
    if (u.find('.') != std::string::npos) {
        std::string t(type);
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
        return isNamespaceName(t) && knownType && knownType(t);
    }
    for (const char c : u) if (!identChar(c)) return false;
    return knownType && knownType(type);
}

namespace {
std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
bool isIdent(std::string_view s) {
    if (s.empty() || !identStart(s[0])) return false;
    for (const char c : s) if (!identChar(c)) return false;
    return true;
}
} // namespace

ScriptParts splitDeclarations(std::string_view code, bool function, const TypeKnown& knownType) {
    ScriptParts out;
    out.body.assign(code.begin(), code.end());
    using E = ScriptDiagnostic::Severity;
    // On avance dans le code en sautant chaines et commentaires ; un mot VAR,
    // VAR_TEMP ou VAR_INPUT ouvre un bloc, END_VAR le ferme.
    int line = 1;
    std::size_t i = 0;
    const auto skipTo = [&](std::size_t to) {
        for (; i < to && i < code.size(); ++i) if (code[i] == '\n') ++line;
    };
    const auto blank = [&](std::size_t from, std::size_t to) {
        for (std::size_t k = from; k < to && k < out.body.size(); ++k)
            if (out.body[k] != '\n') out.body[k] = ' ';
    };
    while (i < code.size()) {
        const char c = code[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '\'' || c == '"') {
            const auto end = code.find(c, i + 1);
            skipTo(end == std::string_view::npos ? code.size() : end + 1);
            continue;
        }
        if (c == '(' && i + 1 < code.size() && code[i + 1] == '*') {
            const auto end = code.find("*)", i + 2);
            skipTo(end == std::string_view::npos ? code.size() : end + 2);
            continue;
        }
        if (c == '/' && i + 1 < code.size() && code[i + 1] == '/') {
            const auto end = code.find('\n', i);
            skipTo(end == std::string_view::npos ? code.size() : end);
            continue;
        }
        if (!identStart(c) || (i > 0 && (identChar(code[i - 1]) || code[i - 1] == '.'))) { ++i; continue; }
        const std::size_t wordStart = i;
        while (i < code.size() && identChar(code[i])) ++i;
        const std::string word = upper(code.substr(wordStart, i - wordStart));
        // 1.10 : une fonction interne (FUNCTION ... END_FUNCTION) garde ses
        // declarations : le simulateur (dialecte IHM) les lit avec elle.
        if (word == "FUNCTION" && !function) {
            while (i < code.size()) {
                const char d = code[i];
                if (d == '\n') { ++line; ++i; continue; }
                if (d == '\'' || d == '"') {
                    const auto end = code.find(d, i + 1);
                    skipTo(end == std::string_view::npos ? code.size() : end + 1);
                    continue;
                }
                if (d == '(' && i + 1 < code.size() && code[i + 1] == '*') {
                    const auto end = code.find("*)", i + 2);
                    skipTo(end == std::string_view::npos ? code.size() : end + 2);
                    continue;
                }
                if (d == '/' && i + 1 < code.size() && code[i + 1] == '/') {
                    const auto end = code.find('\n', i);
                    skipTo(end == std::string_view::npos ? code.size() : end);
                    continue;
                }
                if (identStart(d) && !identChar(code[i - 1])) {
                    std::size_t k = i;
                    while (k < code.size() && identChar(code[k])) ++k;
                    const bool end = upper(code.substr(i, k - i)) == "END_FUNCTION";
                    i = k;
                    if (end) break;
                    continue;
                }
                ++i;
            }
            continue;
        }
        // 1.11.20 : VAR_IN_OUT et VAR_OUTPUT aussi - des parametres comme VAR_INPUT (avant, ils
        // restaient dans le corps : une fonction qui en avait n'etait comptee que par ses entrees).
        if (word != "VAR" && word != "VAR_TEMP" && word != "VAR_INPUT" && word != "VAR_IN_OUT" && word != "VAR_OUTPUT") continue;
        const int blockLine = line;
        LocalVar::Section section = word == "VAR" ? LocalVar::Section::Var
                                  : word == "VAR_TEMP"   ? LocalVar::Section::Temp
                                  : word == "VAR_INPUT"  ? LocalVar::Section::Input
                                  : word == "VAR_IN_OUT" ? LocalVar::Section::InOut : LocalVar::Section::Output;
        if (section != LocalVar::Section::Var && section != LocalVar::Section::Temp && !function)
            out.errors.push_back({E::Error, blockLine, word + " : seulement dans une fonction IHM (les param\xC3\xA8tres)"});
        // Le contenu du bloc, commentaires ote, jusqu'a END_VAR.
        std::string decl;
        std::vector<int> declLines;   // la ligne de chaque caractere de `decl`
        bool closed = false;
        // RETAIN, CONSTANT apres VAR : acceptes (VAR est deja gardee).
        while (i < code.size()) {
            const char d = code[i];
            if (d == '\n') { ++line; decl += ' '; declLines.push_back(line); ++i; continue; }
            if (d == '(' && i + 1 < code.size() && code[i + 1] == '*') {
                const auto end = code.find("*)", i + 2);
                skipTo(end == std::string_view::npos ? code.size() : end + 2);
                continue;
            }
            if (d == '/' && i + 1 < code.size() && code[i + 1] == '/') {
                const auto end = code.find('\n', i);
                skipTo(end == std::string_view::npos ? code.size() : end);
                continue;
            }
            if (identStart(d) && !(i > 0 && identChar(code[i - 1]))) {
                std::size_t k = i;
                while (k < code.size() && identChar(code[k])) ++k;
                const std::string w = upper(code.substr(i, k - i));
                if (w == "END_VAR") {
                    i = k;
                    while (i < code.size() && (code[i] == ' ' || code[i] == '\t')) ++i;
                    if (i < code.size() && code[i] == ';') ++i;
                    closed = true;
                    break;
                }
                if ((w == "RETAIN" || w == "CONSTANT") && trimmed(decl).empty()) { i = k; continue; }
                for (std::size_t m = i; m < k; ++m) { decl += code[m]; declLines.push_back(line); }
                i = k;
                continue;
            }
            decl += d;
            declLines.push_back(line);
            ++i;
        }
        blank(wordStart, i);
        if (!closed) {
            out.errors.push_back({E::Error, blockLine, word + " sans END_VAR"});
            break;
        }
        // Les declarations : "a, b : INT := 0;". 1.12.19 : un ';' entre parentheses ou crochets ne les coupe
        // pas (f : CALLBACK (x : REAL; y : REAL) : BOOL;), ni dans une chaine.
        const auto nextSemi = [&decl](std::size_t from) {
            int depth = 0;
            bool quoted = false;
            for (std::size_t k = from; k < decl.size(); ++k) {
                const char c = decl[k];
                if (c == '\'') quoted = !quoted;
                if (quoted) continue;
                if (c == '(' || c == '[') ++depth;
                else if ((c == ')' || c == ']') && depth > 0) --depth;
                else if (c == ';' && depth == 0) return k;
            }
            return std::string::npos;
        };
        std::size_t start = 0;
        while (start < decl.size()) {
            auto semi = nextSemi(start);
            if (semi == std::string::npos) semi = decl.size();
            const std::string one = trimmed(std::string_view(decl).substr(start, semi - start));
            std::size_t lead = start;
            while (lead < semi && std::isspace(static_cast<unsigned char>(decl[lead]))) ++lead;
            const int at = lead < declLines.size() ? declLines[lead] : blockLine;
            start = semi + 1;
            if (one.empty()) continue;
            if (semi == decl.size()) out.errors.push_back({E::Error, at, "';' attendu apr\xC3\xA8s la d\xC3\xA9" "claration \xC2\xAB " + one + " \xC2\xBB"});
            const auto colon = one.find(':');
            if (colon == std::string::npos || (colon + 1 < one.size() && one[colon + 1] == '=')) {
                out.errors.push_back({E::Error, at, "d\xC3\xA9" "claration illisible : \xC2\xAB " + one + " \xC2\xBB (nom : TYPE ;)"});
                continue;
            }
            std::string type = trimmed(std::string_view(one).substr(colon + 1));
            std::string initial;
            if (const auto assign = type.find(":="); assign != std::string::npos) {
                initial = trimmed(std::string_view(type).substr(assign + 2));
                type = trimmed(std::string_view(type).substr(0, assign));
            }
            // 1.10 : un script (dialecte IHM) a aussi des locales riches : ARRAY[..]
            // (N dimensions), MAP[..] OF, REF_TO, POINTER TO, une structure IHM.
            // (1.10 : aussi dans une fonction IHM : une fonction aux types riches tourne dans le dialecte.)
            const bool rich = !localTypeSupported(type) && richLocalType(trimmed(type), knownType);
            if (!localTypeSupported(type) && !rich) {
                out.errors.push_back({E::Error, at, "type non pris en charge pour une variable locale : " + type
                                                        + " (BOOL, INT, DINT, REAL, TIME, STRING...)"});
                continue;
            }
            if (!rich) type = upper(type);
            std::string names = std::string(std::string_view(one).substr(0, colon));
            std::size_t ns = 0;
            while (ns <= names.size()) {
                auto comma = names.find(',', ns);
                if (comma == std::string::npos) comma = names.size();
                const std::string name = trimmed(std::string_view(names).substr(ns, comma - ns));
                ns = comma + 1;
                if (name.empty()) continue;
                if (!isIdent(name) || stKeywords().count(upper(name))) {
                    out.errors.push_back({E::Error, at, "nom de variable locale invalide : '" + name + "'"});
                    continue;
                }
                if (out.local(name)) {
                    out.errors.push_back({E::Error, at, "variable locale en double : " + name});
                    continue;
                }
                out.locals.push_back({name, type, initial, section, at});
            }
        }
    }
    return out;
}

std::vector<NameUse> scriptCallees(std::string_view code) {
    std::vector<NameUse> out;
    const auto parts = splitDeclarations(code, true);
    const std::string_view s = parts.body;
    int line = 1;
    std::size_t i = 0;
    std::set<std::string> seen;
    const auto skipTo = [&](std::size_t to) {
        for (; i < to && i < s.size(); ++i) if (s[i] == '\n') ++line;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '\'' || c == '"') { const auto end = s.find(c, i + 1); skipTo(end == std::string_view::npos ? s.size() : end + 1); continue; }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') { const auto end = s.find("*)", i + 2); skipTo(end == std::string_view::npos ? s.size() : end + 2); continue; }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') { const auto end = s.find('\n', i); skipTo(end == std::string_view::npos ? s.size() : end); continue; }
        if (!identStart(c) || (i > 0 && (identChar(s[i - 1]) || s[i - 1] == '.'))) { ++i; continue; }
        const std::size_t start = i;
        while (i < s.size() && identChar(s[i])) ++i;
        std::size_t k = i;
        while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
        if (k < s.size() && s[k] == '(') {
            const std::string name(s.substr(start, i - start));
            if (!stKeywords().count(upper(name)) && seen.insert(upper(name)).second) out.push_back({name, line});
        }
    }
    return out;
}

// 1.12.20 : les racines ecrites d'un code - Nom := ..., Nom.Membre := ..., Nom[i] := ... (hors chaines et commentaires).
namespace {
std::vector<std::string> assignedRoots(std::string_view s) {
    std::vector<std::string> out;
    const auto ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    for (std::size_t i = 0; i < s.size();) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            std::size_t j = i + 1;
            while (j < s.size() && s[j] != c) j += (s[j] == '$' && j + 1 < s.size()) ? 2 : 1;
            i = j + 1;
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto e = s.find("*)", i + 2);
            i = e == std::string_view::npos ? s.size() : e + 2;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') ++i;
            continue;
        }
        if (!ident(c) || std::isdigit(static_cast<unsigned char>(c))) {
            if (std::isdigit(static_cast<unsigned char>(c))) while (i < s.size() && (ident(s[i]) || s[i] == '#' || s[i] == '.')) ++i;
            else ++i;
            continue;
        }
        std::size_t j = i;
        while (j < s.size() && ident(s[j])) ++j;
        const bool member = i > 0 && s[i - 1] == '.';
        const std::string root(s.substr(i, j - i));
        std::size_t k = j;
        // Le chemin : .Membre, [index] (les crochets imbriques comptes).
        for (;;) {
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            if (k + 1 < s.size() && s[k] == '.' && ident(s[k + 1])) {
                ++k;
                while (k < s.size() && ident(s[k])) ++k;
                continue;
            }
            if (k < s.size() && s[k] == '[') {
                int depth = 0;
                for (; k < s.size(); ++k) {
                    if (s[k] == '[') ++depth;
                    else if (s[k] == ']' && --depth == 0) { ++k; break; }
                }
                continue;
            }
            break;
        }
        if (!member && k + 1 < s.size() && s[k] == ':' && s[k + 1] == '=') out.push_back(root);
        i = j;
    }
    return out;
}
} // namespace

// 1.12.20 : name := LAMBDA ... (voir HmiScript.hpp).
bool assignsLambda(std::string_view s, std::string_view name) {
    const std::string key = upper(name);
    const auto ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    const auto blanks = [&](std::size_t k) {
        while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) ++k;
        return k;
    };
    for (std::size_t i = 0; i < s.size();) {
        const char c = s[i];
        if (c == '\'' || c == '"') {                                   // une chaine
            std::size_t j = i + 1;
            while (j < s.size() && s[j] != c) j += (s[j] == '$' && j + 1 < s.size()) ? 2 : 1;
            i = j + 1;
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {          // (* ... *)
            const auto e = s.find("*)", i + 2);
            i = e == std::string_view::npos ? s.size() : e + 2;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {          // // ...
            while (i < s.size() && s[i] != '\n') ++i;
            continue;
        }
        if (!ident(c)) { ++i; continue; }
        std::size_t j = i;
        while (j < s.size() && ident(s[j])) ++j;
        const bool member = i > 0 && s[i - 1] == '.';
        if (!member && upper(s.substr(i, j - i)) == key) {
            std::size_t k = blanks(j);
            if (k + 1 < s.size() && s[k] == ':' && s[k + 1] == '=') {
                k = blanks(k + 2);
                std::size_t e = k;
                while (e < s.size() && ident(s[e])) ++e;
                if (upper(s.substr(k, e - k)) == "LAMBDA") return true;
            }
        }
        i = j;
    }
    return false;
}

// 1.12.20 : ce que fait une fonction (voir HmiScript.hpp).
bool hmiFunctionActs(std::string_view name) {
    if (!isHmiFunction(name)) return false;
    const auto* f = natives::function(upper(name));
    return !(f && f->readOnlyIhm);
}

Effects functionEffects(const Project& p, const HmiFunction& fn, const View* view) {
    Effects out;
    std::set<const HmiFunction*> seen;
    const auto note = [](std::vector<std::string>& v, const std::string& n) {
        for (const auto& x : v) if (upper(x) == upper(n)) return;
        v.push_back(n);
    };
    const std::function<void(const HmiFunction&)> walk = [&](const HmiFunction& f) {
        if (!seen.insert(&f).second) return;
        std::set<std::string> locals{upper(f.name)};
        for (const auto& d : f.decls) locals.insert(upper(d.name));
        for (const auto& root : assignedRoots(f.body))
            if (!locals.count(upper(root)) && p.variable(root)) note(out.writes, root);
        for (const auto& c : scriptCallees(f.body)) {
            if (locals.count(upper(c.name))) continue;
            if (hmiFunctionActs(c.name)) {
                note(out.acts, upper(c.name));
                continue;
            }
            bool found = false;
            if (view)
                for (const auto& vf : view->functions)
                    if (upper(vf.name) == upper(c.name)) {
                        found = true;
                        walk(vf);
                    }
            if (!found)
                for (const auto* g : p.functionsNamed(c.name)) walk(*g);
        }
    };
    walk(fn);
    return out;
}

std::string renameCalls(std::string_view s, std::string_view from, std::string_view to, bool bare, bool values) {
    std::string out;
    out.reserve(s.size() + 16);
    const std::string key = upper(from);
    std::size_t i = 0;
    const auto copyTo = [&](std::size_t end) {
        end = std::min(end, s.size());
        out.append(s.substr(i, end - i));
        i = end;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            std::size_t k = i + 1;
            while (k < s.size() && s[k] != c) k += s[k] == '$' ? 2 : 1;
            copyTo(k + 1);
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            copyTo(end == std::string_view::npos ? s.size() : end + 2);
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            copyTo(end == std::string_view::npos ? s.size() : end);
            continue;
        }
        if (!identStart(c) || (i > 0 && (identChar(s[i - 1]) || s[i - 1] == '.' || s[i - 1] == '#'))) {
            out += c;
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < s.size() && identChar(s[i])) ++i;
        const auto word = s.substr(start, i - start);
        bool rename = upper(word) == key;
        if (rename && !bare) {
            std::size_t k = i;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            const bool call = k < s.size() && s[k] == '(';
            // 1.12.19 (R5) : une fonction passee en rappel - le nom seul, la ou va une valeur.
            bool value = false;
            if (!call && values) {
                std::size_t b = start;
                const auto blanks = [&] {
                    while (b > 0 && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) --b;
                };
                blanks();
                // Le = d'une formule (la regle des cases) : Sur_Non := =Valider, la valeur initiale =Valider.
                bool formula = false;
                if (b > 0 && s[b - 1] == '=' && !(b > 1 && (s[b - 2] == ':' || s[b - 2] == '<' || s[b - 2] == '>' || s[b - 2] == '='))) {
                    --b;
                    blanks();
                    formula = true;
                }
                const bool after = (formula && b == 0) || (b > 0 && (s[b - 1] == '(' || s[b - 1] == ',' || (s[b - 1] == '=' && b > 1 && s[b - 2] == ':')));
                std::size_t e = k;
                while (e < s.size() && (s[e] == '\n' || s[e] == '\r' || s[e] == ' ' || s[e] == '\t')) ++e;
                const bool before = e >= s.size() || s[e] == ';' || s[e] == ')' || s[e] == ',';
                value = after && before;
            }
            rename = call || value;
        }
        out.append(rename ? to : word);
    }
    return out;
}

std::string rewriteInText(std::string_view text, const std::function<std::string(std::string_view)>& f) {
    if (!text.empty() && text.front() == '=') return "=" + f(text.substr(1));
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto open = text.find('{', i);
        if (open == std::string_view::npos) break;
        const auto close = text.find('}', open);
        if (close == std::string_view::npos) break;
        out.append(text.substr(i, open + 1 - i));
        out += f(text.substr(open + 1, close - open - 1));
        out += '}';
        i = close + 1;
    }
    out.append(text.substr(i));
    return out;
}

std::string renameCallsInText(std::string_view text, std::string_view from, std::string_view to) {
    return rewriteInText(text, [&](std::string_view e) { return renameCalls(e, from, to); });
}

// ---- 1.11.17 (refonte, lot 0) : le parcours de chaque texte du projet --------------
namespace {

// La valeur d'une action : un script (Executer un script), un texte a trous (Journal),
// sinon une expression (une affectation, des arguments...).
CodeForm actionForm(const Action& a) {
    if (a.operation == Operation::RunScript) return CodeForm::Code;
    return a.operation == Operation::Log ? CodeForm::Template : CodeForm::Expression;
}

// 1.11.18 (refonte, lot 3) : les valeurs des declarations du modele d'un code (initiale,
// constante, par defaut) - des expressions, lues dans le meme lieu que lui.
template <class Decls, class Visit>
void visitDeclarations(Decls& decls, const CodeSite& site, Visit& visit) {
    for (auto& d : decls) {
        if (!d.value.empty()) visit(d.value, CodeForm::Expression, site);
        // 1.12.16 : le code d'une valeur calculee (un renommage, une recherche le suivent).
        if (d.computed && !d.calc.empty()) visit(d.calc, CodeForm::Code, site);
    }
}

// Les morceaux qu'en lit rewriteNames (une expression, une accolade d'un texte a trous,
// une plume, un etat, une cellule, un argument...) ; vrai : l'un d'eux a change.
template <class Visit>
bool visitPieces(Object& holder, const CodeSite& site, Visit& visit) {
    bool touched = false;
    rewriteNames(holder, [&](std::string_view t, bool code) {
        std::string text(t);
        visit(text, code ? CodeForm::Code : CodeForm::Expression, site);
        if (text != t) touched = true;
        return text;
    });
    return touched;
}

// Un objet : ses proprietes, ses actions, ses alarmes surchargees et (une instance)
// ses fonctions redefinies. `O` : Object ou const Object ; `view` : la vue qui le porte.
template <class O, class Visit>
void visitObject(const Project& p, O& o, const View* view, const View* scope, Visit& visit) {
    constexpr bool kWrite = !std::is_const_v<O>;
    const std::string at = view->name + "/" + o.name;
    const std::string props = at + " (propri\xC3\xA9t\xC3\xA9s)";
    for (auto& pr : o.props) {
        const CodeSite site{.scope = scope, .view = view, .where = at + " (" + pr.key + ")", .group = props};
        Object holder;
        holder.kind = o.kind;
        holder.props.push_back(pr);
        if (visitPieces(holder, site, visit)) {
            if constexpr (kWrite) pr = std::move(holder.props.front());
        }
        // 1.12.10 (lot F) : une borne, un seuil tapes en nom de variable (Consigne_Max) - plus seulement le champ de saisie.
        if (roles::rangeNameKey(o.kind, pr.key)) visit(pr.value, CodeForm::Expression, site);
        visit(pr.value, CodeForm::Template, site);       // un texte a trous ailleurs que dans "text" (un nom de fichier...)
    }
    if (!o.actions.empty()) {
        const CodeSite site{.scope = scope, .view = view, .where = at + " (action)"};
        Object holder;
        holder.actions = o.actions;
        if (visitPieces(holder, site, visit)) {
            if constexpr (kWrite) o.actions = std::move(holder.actions);
        }
        for (auto& a : o.actions) visit(a.value, actionForm(a), site);
    }
    for (auto& ov : o.alarmOverrides) {                   // 1.9 : une instance surcharge une alarme du symbole
        const CodeSite site{.where = at + " (alarme " + ov.alarm + ")"};
        if (ov.condition) visit(*ov.condition, CodeForm::Expression, site);
        if (ov.message) visit(*ov.message, CodeForm::Template, site);
        if (ov.instruction) visit(*ov.instruction, CodeForm::Template, site);
    }
    // 1.11.10 : le corps d'une redefinition est lu dans SON symbole (celui de l'instance).
    const View* own = o.functionOverrides.empty() ? nullptr : symbolOf(p, o);
    for (auto& fo : o.functionOverrides) {
        const CodeSite site{.scope = own, .view = own, .where = at + "." + fo.function + " (red\xC3\xA9" "finition)"};
        visit(fo.body, CodeForm::Code, site);
        visitDeclarations(fo.decls, site, visit);
    }
}

template <class P, class Visit>
void visitProject(P& p, Visit& visit) {
    const auto alarm = [&](auto& a, const std::string& where) {
        const CodeSite site{.where = where};              // une alarme de symbole : pas de qualification (HmiObjectAlarms)
        visit(a.condition, CodeForm::Expression, site);
        visit(a.message, CodeForm::Template, site);
        visit(a.instruction, CodeForm::Template, site);
    };
    const auto ops = [&](auto& list, const std::string& owner) {
        for (auto& o : list) {
            const CodeSite site{.where = "op\xC3\xA9rateur " + operatorSignature(o) + " (" + owner + ")"};
            visit(o.body, CodeForm::Code, site);
            visitDeclarations(o.decls, site, visit);
        }
    };
    for (auto& sc : p.programs.scripts) {
        if (sc.lang != ScriptLang::ST) continue;
        const CodeSite site{.where = "script " + sc.name};
        visit(sc.body, CodeForm::Code, site);
        visit(sc.watch, CodeForm::Expression, site);
        visitDeclarations(sc.decls, site, visit);
    }
    for (auto& f : p.programs.functions) {
        const CodeSite site{.own = &f, .where = "fonction " + f.name};
        visit(f.body, CodeForm::Code, site);
        visitDeclarations(f.decls, site, visit);
    }
    for (auto& t : p.programs.types) ops(t.operators, "type " + t.name);
    for (auto& v : p.views) {
        // Le code d'un symbole, et d'une popup qu'il porte : un appel court y vise d'abord ses fonctions.
        const View* scope = isSymbolView(v) ? &v : popupOwner(p, v);
        for (auto& sc : v.scripts)
            if (sc.lang == ScriptLang::ST) {
                const CodeSite site{.scope = scope, .view = &v, .where = v.name + "." + sc.event};
                visit(sc.body, CodeForm::Code, site);
                visitDeclarations(sc.decls, site, visit);
            }
        if (!v.actions.empty()) {
            const CodeSite site{.scope = scope, .view = &v, .where = v.name + " (action de vue)"};
            Object holder;
            holder.actions = v.actions;
            if (visitPieces(holder, site, visit)) {
                if constexpr (!std::is_const_v<P>) v.actions = std::move(holder.actions);
            }
            for (auto& a : v.actions) visit(a.value, actionForm(a), site);
        }
        for (auto& o : v.objects) visitObject(p, o, &v, scope, visit);
        // Le titre d'une popup et la valeur par defaut d'un parametre ne sont pas qualifies
        // (qualifiedOwnedPopup, symbolArguments) : un appel court y vise une fonction IHM.
        visit(v.popup.title, CodeForm::Template, CodeSite{.where = v.name + " (titre)"});
        for (auto& prm : v.params)
            visit(prm.defaultValue, CodeForm::Expression, CodeSite{.where = v.name + " (param\xC3\xA8tre " + prm.name + ")"});
        for (auto& fn : v.functions) {
            const CodeSite site{.scope = scope, .view = &v, .where = "fonction " + v.name + "." + fn.name};
            visit(fn.body, CodeForm::Code, site);
            visitDeclarations(fn.decls, site, visit);
        }
        // 1.12.16 : les constantes de la vue - leurs valeurs, leurs calculs.
        if (!v.constants.empty()) {
            const CodeSite site{.scope = scope, .view = &v, .where = v.name + " (constantes)"};
            visitDeclarations(v.constants, site, visit);
        }
        ops(v.operators, v.name);
        for (auto& a : v.alarms) alarm(a, "alarme " + v.name + "." + a.name);
    }
    for (auto& a : p.alarms) alarm(a, "alarme " + a.name);
    for (auto& r : p.recipes)
        for (auto& rec : r.records)
            for (auto& val : rec.values) visit(val, CodeForm::Expression, CodeSite{.where = "recette " + r.name});
    for (auto& u : p.security.users)
        if (u.protection == "expression")
            visit(u.expression, CodeForm::Expression, CodeSite{.where = "utilisateur " + u.login + " (autorisation)"});
    for (auto& archived : p.history.archived) visit(archived, CodeForm::Expression, CodeSite{.where = "historique"});
}

// Un appel court de `name` y vise la fonction du symbole, pas la fonction IHM.
bool shadowed(const CodeSite& s, std::string_view name) { return s.scope && symbolFunction(*s.scope, name); }

bool callsIn(const std::string& text, CodeForm form, std::string_view name) {
    if (text.empty()) return false;
    const auto next = form == CodeForm::Template ? renameCallsInText(text, name, "\x01") : renameCalls(text, name, "\x01", false, true);
    return next != text;
}

// 1.12.19 (R5) : une fonction passee en rappel hors du code - la valeur initiale d'une variable rappel (=Valider),
// l'argument d'une popup (Sur_Oui := Valider). `name` : le texte (sans = ni apostrophes) qui la nomme.
bool namesFunction(std::string_view value, std::string_view name) {
    std::string t = trimmed(value);
    if (t.size() > 1 && t.front() == '=' && t[1] != '=') t = trimmed(std::string_view(t).substr(1));
    return !t.empty() && upper(t) == upper(name);
}
template <class P, class Visit>
void forEachCallbackValue(P& p, std::string_view name, Visit&& visit) {
    // La valeur initiale d'une variable rappel (=Valider) : le code ne la lit pas.
    for (auto& v : p.programs.variables)
        if (!types::callbackForm(&p, v.type).empty() && namesFunction(v.initial, name))     // 1.12.20 : T_Filtre aussi
            visit(v.initial, "variable " + v.name + " (valeur initiale)");
    // La cible d'une action Appeler (sa valeur, les arguments, est lue comme une expression avec le reste).
    const auto call = [&](auto& view, auto* object, auto& a) {
        if (a.operation == Operation::Call && namesFunction(a.target, name))
            visit(a.target, view.name + (object ? "/" + object->name : std::string{}) + " (action)");
    };
    for (auto& view : p.views) {
        for (auto& a : view.actions) call(view, static_cast<decltype(&view.objects.front())>(nullptr), a);
        for (auto& o : view.objects)
            for (auto& a : o.actions) call(view, &o, a);
    }
}

} // namespace

void forEachCode(Project& p, const CodeVisit& visit) { visitProject(p, visit); }
void forEachCode(const Project& p, const ConstCodeVisit& visit) { visitProject(p, visit); }

std::size_t renameFunctionEverywhere(Project& p, std::string_view from, std::string_view to) {
    std::size_t changed = 0;
    const std::string key = upper(from), next = upper(to);
    forEachCode(p, [&](std::string& text, CodeForm form, const CodeSite& s) {
        if (text.empty() || shadowed(s, from)) return;
        const bool bare = s.own && (upper(s.own->name) == key || upper(s.own->name) == next);
        auto out = form == CodeForm::Template ? renameCallsInText(text, from, to) : renameCalls(text, from, to, bare, true);   // 1.12.19 : passee en rappel aussi
        if (out != text) { text = std::move(out); ++changed; }
    });
    // 1.12.19 (R5) : la valeur initiale d'une variable rappel (=Valider), la cible d'une action Appeler - le texte
    // entier la nomme : il prend le nouveau nom (et garde son =).
    forEachCallbackValue(p, from, [&](std::string& text, const std::string&) {
        const std::string t = trimmed(text);
        text = (t.size() > 1 && t.front() == '=' ? "=" : "") + std::string(to);
        ++changed;
    });
    return changed;
}

std::vector<std::string> functionCallers(const Project& p, std::string_view name) {
    std::vector<std::string> out;
    std::set<std::string> said;
    const std::string key = upper(name);
    forEachCode(p, [&](const std::string& text, CodeForm form, const CodeSite& s) {
        if ((s.own && upper(s.own->name) == key) || shadowed(s, name)) return;   // pas elle-meme
        const std::string& group = s.group.empty() ? s.where : s.group;
        if (said.count(group) || !callsIn(text, form, name)) return;
        said.insert(group);
        out.push_back(s.where);
    });
    // 1.12.19 (R5) : passee en rappel hors du code (la valeur initiale d'une variable rappel, une action Appeler).
    forEachCallbackValue(p, name, [&](const std::string&, const std::string& where) {
        if (said.insert(where).second) out.push_back(where);
    });
    return out;
}

std::vector<std::string> renameCaptures(const Project& p, const View* symbol, std::string_view from, std::string_view to) {
    std::vector<std::string> out;
    std::set<std::string> said;
    forEachCode(p, [&](const std::string& text, CodeForm form, const CodeSite& s) {
        if (!s.scope) return;
        std::string_view call;
        if (!symbol) {
            if (symbolFunction(*s.scope, from) || !symbolFunction(*s.scope, to)) return;
            call = from;
        } else {
            if (s.scope != symbol || symbolFunction(*symbol, to)) return;
            call = to;
        }
        if (callsIn(text, form, call) && said.insert(s.where).second) out.push_back(s.where);
    });
    return out;
}

bool declarationValueReadable(std::string_view type, std::string_view value, std::string* why) {
    // Lue comme le simulateur la lira : une declaration du dialecte, seule.
    const std::string code = "VAR\n    Valeur__ : " + std::string(type) + " := " + std::string(value) + ";\nEND_VAR\n";
    const auto program = sim::parse(code, "valeur", dialectOptions());
    if (program) return true;
    if (why) {
        const std::string raw = program.error().context.empty() ? program.error().message() : program.error().context;
        std::string rest;
        (void)splitLine(raw, &rest);
        *why = frenchSimMessage(rest);
    }
    return false;
}

std::string functionTemplate(std::string_view name, std::string_view returnType, std::string_view description) {
    std::string head = "(* " + std::string(name) + (description.empty() ? std::string{} : " : " + std::string(description)) + " *)\n";
    // 1.11.18 (lot 5) : ses declarations dans le modele (functionTemplateDecls), plus dans le code.
    if (returnType.empty()) return head + "IHM_JOURNAL(Message);\n";
    return head + "Resultat := Entree;\n" + std::string(name) + " := Resultat;\n";
}

std::vector<Declaration> functionTemplateDecls(std::string_view returnType) {
    const auto make = [](DeclKind kind, std::string name, std::string type, std::string doc) {
        Declaration d;
        d.kind = kind;
        d.name = std::move(name);
        d.type = std::move(type);
        d.description = std::move(doc);
        d.visibility = kind == DeclKind::Parameter ? Visibility::Public : Visibility::Private;
        return d;
    };
    if (returnType.empty())
        return {make(DeclKind::Parameter, "Message", "STRING", "un exemple : le texte \xC3\xA9" "crit au journal")};
    const std::string t(returnType);
    return {make(DeclKind::Parameter, "Entree", t, "un exemple : renommez-le, changez son type, ajoutez-en (onglet Param\xC3\xA8tres)"),
            make(DeclKind::Variable, "Resultat", t, "le r\xC3\xA9sultat, avant de le rendre")};
}

std::string functionText(const HmiFunction& f) {
    // 1.11.1 (REP) : le corps sans les $ de ses reperes (ce texte ne sert qu'a l'analyse ; les lignes restent).
    // 1.11.18 (lot 3) : ses declarations du modele reconstruites sur la ligne 1 (decl::codeOf).
    // 1.13.0 : une fonction generique porte son TEMPLATE <T : ANY_NUM, U> devant FUNCTION - le moteur la lit alors
    //  comme generique (il deduit T a l'appel, le verifie contre sa contrainte) ; genericsText reprend sa case.
    const std::string tmpl = f.generics.empty() ? std::string{} : "TEMPLATE <" + genericsText(f.generics) + "> ";
    // 1.13.0 (lot J8) : ses directives USING ne vont pas au moteur (blanchies : les lignes restent ; le moteur
    //  recoit les noms deja qualifies - names::qualify, au lancement).
    if (names::mentionsUsing(f.body)) {
        HmiFunction g = f;
        g.body = names::blankUsings(f.body);
        return tmpl + "FUNCTION " + f.name + (f.returnType.empty() ? std::string{} : " : " + f.returnType) + " "
             + markers::strip(decl::codeOf(g)) + "\nEND_FUNCTION\n";
    }
    return tmpl + "FUNCTION " + f.name + (f.returnType.empty() ? std::string{} : " : " + f.returnType) + " "
         + markers::strip(decl::codeOf(f)) + "\nEND_FUNCTION\n";
}

std::string functionSignature(const HmiFunction& f) {
    // 1.13.0 : une fonction generique montre ses types entre chevrons, Max<T>(a : T; b : T) : T (comme un type T_Mesure<T>).
    std::string angles;
    if (!f.generics.empty()) {
        std::string names;
        for (const auto& g : f.generics) names += (names.empty() ? "" : ", ") + g.name;
        angles = "<" + names + ">";
    }
    // 1.10 : une fonction aux types riches (VAR_IN_OUT, REF_TO, ARRAY...) : la signature du dialecte.
    if (auto fn = sim::parseFunction(functionText(f), f.name); fn && !sim::functionIsSimple(**fn)) {
        std::string sig = sim::functionSignature(**fn);
        if (!angles.empty() && sig.rfind(f.name, 0) == 0) sig.insert(f.name.size(), angles);
        return sig;
    }
    std::string s = f.name + angles + "(";
    const auto parts = splitDeclarations(decl::codeOf(f), true);       // 1.11.18 (lot 3) : ses parametres du modele aussi
    bool first = true;
    for (const auto* in : parts.parameters()) {                        // 1.11.20 : E/S et sorties comprises
        s += (first ? "" : ", ");
        if (in->section == LocalVar::Section::InOut) s += "VAR_IN_OUT ";
        else if (in->section == LocalVar::Section::Output) s += "VAR_OUTPUT ";
        s += in->name + " : " + in->type;
        first = false;
    }
    s += ")";
    if (!f.returnType.empty()) s += " : " + f.returnType;
    return s;
}

std::vector<ScriptDiagnostic> checkFunction(const HmiFunction& source, const TypeKnown& knownType) {
    using E = ScriptDiagnostic::Severity;
    // 1.13.0 : les generiques de la fonction (TEMPLATE <T>) elargissent les types permis a ses parametres, ses locales
    //  et son retour - un type T (ou LIST OF T, T_Mesure<T>...) vaut son exemple dans la contrainte (T -> REAL), puis le
    //  type permis d'avant decide. Sans generiques : knownType tel quel (comportement d'avant).
    TypeKnown known = knownType;
    if (!source.generics.empty())
        known = [knownType, gens = source.generics](std::string_view t) -> bool {
            if (knownType && knownType(t)) return true;
            std::vector<std::string> samples;
            for (const auto& g : gens) samples.push_back(genericSample(g.constraint));
            const std::string concrete = substituteGenerics(t, gens, samples);
            return concrete != std::string(t) && (localTypeSupported(concrete) || richLocalType(concrete, knownType));
        };
    // 1.11.18 (refonte, lot 3) : ses declarations du modele d'abord ; le code se lit avec
    // celles qui sont justes, reconstruites sur sa ligne 1 (les lignes ne bougent pas).
    if (!source.decls.empty()) {
        std::vector<Declaration> valid;
        auto out = decl::checkDeclarations(source.decls, decl::Role::Function, source.body, known, nullptr, &valid);
        valid.erase(std::remove_if(valid.begin(), valid.end(), [&](const Declaration& d) {
                        if (upper(d.name) != upper(source.name)) return false;
                        out.push_back({E::Error, 0, "d\xC3\xA9" "claration \xC2\xAB " + d.name
                                                        + " \xC2\xBB : elle ne peut pas porter le nom de la fonction"});
                        return true;
                    }),
                    valid.end());
        HmiFunction flat = source;
        flat.decls = std::move(valid);
        flat.body = decl::codeOf(flat);
        flat.decls.clear();
        for (auto& d : checkFunction(flat, knownType)) out.push_back(std::move(d));
        return out;
    }
    HmiFunction f = source;                // 1.11.1 (REP) : le corps sans les $ de ses reperes (les lignes restent)
    f.body = markers::strip(source.body);
    std::vector<ScriptDiagnostic> out;
    // 1.13.0 : la case Generiques relue (un nom illisible, deux fois le meme, une contrainte qui n'est pas un ANY_*).
    if (!f.generics.empty()) {
        std::string why;
        std::vector<GenericParam> round;
        if (!parseGenerics(genericsText(f.generics), round, &why))
            out.push_back({E::Error, 0, "G\xC3\xA9n\xC3\xA9riques : " + why});
    }
    // 1.10 : un type de retour riche (ARRAY, MAP, REF_TO, POINTER TO, un type IHM
    // connu) est permis ; un nom inconnu reste refuse, comme en 1.9. 1.13.0 : ou un type T de la fonction.
    if (!f.returnType.empty() && !localTypeSupported(f.returnType) && !richLocalType(f.returnType, known)) {
        out.push_back({E::Error, 0, "type de retour non pris en charge : " + f.returnType});
        return out;
    }
    // 1.10 : une fonction aux types riches se lit d'un bloc dans le dialecte (ses
    // VAR_IN_OUT, ses REF_TO, ses tableaux) ; une faute : sa ligne.
    {
        auto fn = sim::parseFunction(functionText(f), f.name);
        if (fn && !sim::functionIsSimple(**fn)) return out;
        if (!fn) {
            const std::string raw = fn.error().context.empty() ? fn.error().message() : fn.error().context;
            const bool rich = f.body.find("VAR_IN_OUT") != std::string::npos || f.body.find("VAR_OUTPUT") != std::string::npos
                           || (!f.returnType.empty() && !localTypeSupported(f.returnType));
            if (rich) {
                std::string rest;
                const int line = splitLine(raw, &rest);
                // 1.11.2 (REP) : la ligne N de functionText est celle du corps (il commence sur la ligne de FUNCTION).
                out.push_back({E::Error, line, markers::explainScriptDollar(frenchSimMessage(rest), source.body, line)});
                return out;
            }
        }
    }
    const auto parts = splitDeclarations(f.body, true);
    out.insert(out.end(), parts.errors.begin(), parts.errors.end());
    if (parts.local(f.name))
        out.push_back({E::Error, parts.local(f.name)->line, "une variable locale ne peut pas porter le nom de la fonction : " + f.name});
    if (parts.body.find_first_not_of(" \t\r\n") == std::string::npos) {
        out.push_back({E::Warning, 0, "fonction sans instruction"});
        return out;
    }
    auto program = sim::parse(parts.body, f.name, dialectOptions());              // 1.10 : le dialecte IHM
    if (!program) {
        const std::string raw = program.error().context.empty() ? program.error().message() : program.error().context;
        std::string rest;
        const int line = splitLine(raw, &rest);
        out.push_back({E::Error, line, markers::explainScriptDollar(frenchSimMessage(rest), source.body, line)});   // 1.11.2 (REP)
        return out;
    }
    // Une fonction qui rend une valeur l'affecte a son nom : Moyenne := ... ;
    if (!f.returnType.empty()) {
        bool assigned = false;
        const std::string u = upper(f.name);
        const std::string code = upper(parts.body);
        for (std::size_t at = code.find(u); at != std::string::npos && !assigned; at = code.find(u, at + u.size())) {
            if (at > 0 && (identChar(code[at - 1]) || code[at - 1] == '.')) continue;
            std::size_t k = at + u.size();
            if (k < code.size() && identChar(code[k])) continue;
            while (k < code.size() && (code[k] == ' ' || code[k] == '\t')) ++k;
            assigned = code.compare(k, 2, ":=") == 0;
        }
        if (!assigned)
            out.push_back({E::Warning, 0, "la fonction ne donne jamais sa valeur (" + f.name + " := ... ;) : elle rendra "
                                              + (f.returnType == "STRING" ? std::string("''") : std::string("0"))});
    }
    return out;
}

bool isHmiFunction(std::string_view name) noexcept {
    static const char* kNames[] = {"IHM_NAVIGUER", "IHM_POPUP", "IHM_FERMER_POPUP", "IHM_JOURNAL", "IHM_APPELER",
                                   "IHM_SON", "IHM_VUE", "IHM_TEMPS",
                                   // lot 8 : les popups intelligentes, l'utilisateur connecte
                                   "IHM_CHANGER_POPUP", "IHM_POPUP_PRECEDENTE", "IHM_CENTRER_POPUP", "IHM_FERMER_POPUPS",
                                   "IHM_POPUP_OUVERTE", "IHM_UTILISATEUR", "IHM_NOM_UTILISATEUR", "IHM_GROUPE", "IHM_NIVEAU",
                                   "IHM_DECONNECTER",
                                   // lot 10 : le menu natif Parametres systeme
                                   "IHM_PARAMETRES_SYSTEME",
                                   // lot 11 : les alarmes mises de cote, le silence, l'export
                                   "IHM_METTRE_DE_COTE", "IHM_REMETTRE", "IHM_FAIRE_TAIRE", "IHM_EXPORTER",
                                   // lot 12 : l'historique de navigation, la vue d'accueil
                                   "IHM_PRECEDENTE", "IHM_SUIVANTE", "IHM_ACCUEIL",
                                   // lot 12 : le menu natif de connexion
                                   "IHM_MENU_CONNEXION",
                                   // lot 13 : la langue, le theme
                                   "IHM_LANGUE", "IHM_THEME",
                                   // lot 15 : les equipements ; lot 16 : les GIF animes
                                   "IHM_EQUIPEMENT_OK", "IHM_EQUIPEMENT_PING",
                                   "IHM_GIF_JOUER", "IHM_GIF_PAUSE", "IHM_GIF_ARRETER", "IHM_GIF_REJOUER",
                                   // 1.9 : l'IHM lit-elle cet equipement sur son esclave simule ?
                                   "IHM_ESCLAVE_SIMULE",
                                   // 1.11.14 : une ligne de la Console, d'un niveau
                                   "IHM_LOG",
                                   // 1.12.19 : un rappel plus tard (les minuteurs)
                                   "IHM_APRES", "IHM_CHAQUE", "IHM_ANNULER",
                                   // 1.12.20 : surveiller une variable
                                   "IHM_SUR_CHANGEMENT",
                                   // 1.12.26 : les scripts detaches
                                   "IHM_SCRIPT_ACTIF", "IHM_SCRIPT_ARRETER"};
    const std::string u = upper(name);
    for (const char* n : kNames) if (u == n) return true;
    return false;
}

} // namespace hmi
