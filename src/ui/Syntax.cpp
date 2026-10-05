#include "Syntax.hpp"

#include "Theme.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace ui {
namespace {

constexpr bool identStart(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
constexpr bool identChar(char c) noexcept {
    return identStart(c) || (c >= '0' && c <= '9');
}
constexpr bool digit(char c) noexcept { return c >= '0' && c <= '9'; }
constexpr char lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsCi(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(),
                      [](char x, char y) { return lower(x) == lower(y); });
}

// --- keyword tables ---------------------------------------------------------
constexpr std::string_view kCKeywords[] = {
    "auto", "break", "case", "const", "continue", "default", "do", "else", "enum",
    "extern", "for", "goto", "if", "inline", "register", "restrict", "return",
    "sizeof", "static", "struct", "switch", "typedef", "union", "volatile", "while",
};
constexpr std::string_view kCppKeywords[] = {
    "alignas", "alignof", "and", "asm", "auto", "break", "case", "catch", "class",
    "co_await", "co_return", "co_yield", "concept", "const", "consteval", "constexpr",
    "constinit", "const_cast", "continue", "decltype", "default", "delete", "do",
    "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "final",
    "for", "friend", "goto", "if", "inline", "mutable", "namespace", "new", "noexcept",
    "not", "nullptr", "operator", "or", "override", "private", "protected", "public",
    "register", "reinterpret_cast", "requires", "return", "sizeof", "static",
    "static_assert", "static_cast", "struct", "switch", "template", "this", "throw",
    "true", "try", "typedef", "typeid", "typename", "union", "using", "virtual",
    "volatile", "while", "xor",
};
constexpr std::string_view kCTypes[] = {
    "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float", "int",
    "int8_t", "int16_t", "int32_t", "int64_t", "long", "short", "signed", "size_t",
    "ssize_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "unsigned", "void",
    "wchar_t", "ptrdiff_t", "intptr_t", "uintptr_t",
};
// IEC 61131-3 Structured Text, plus the Control Expert extensions that appear in
// a Schneider export.
constexpr std::string_view kStKeywords[] = {
    "action", "and", "array", "at", "by", "case", "configuration", "constant", "do",
    "else", "elsif", "end_action", "end_case", "end_configuration", "end_for",
    "end_function", "end_function_block", "end_if", "end_program", "end_repeat",
    "end_resource", "end_step", "end_struct", "end_transition", "end_type",
    "end_var", "end_while", "exit", "false", "for", "function", "function_block",
    "if", "initial_step", "jmp", "mod", "not", "of", "on", "or", "program",
    "repeat", "resource", "retain", "return", "step", "struct", "then", "to",
    "transition", "true", "type", "until", "var", "var_global", "var_in_out",
    "var_input", "var_output", "var_temp", "while", "with", "xor",
};
constexpr std::string_view kStTypes[] = {
    "bool", "byte", "date", "date_and_time", "dint", "dt", "dword", "ebool", "int",
    "lint", "lreal", "lword", "real", "sint", "string", "time", "time_of_day",
    "tod", "udint", "uint", "ulint", "usint", "word",
};
// Standard function blocks and functions: highlighted as functions so a call to
// TON reads differently from a call to a project DFB.
constexpr std::string_view kStFunctions[] = {
    "abs", "acos", "add", "and_bit", "asin", "atan", "concat", "cos", "ctd", "ctu",
    "ctud", "delete", "div", "exp", "expt", "find", "f_trig", "gt", "insert", "int_to_real",
    "left", "len", "limit", "ln", "log", "lt", "max", "mid", "min", "mod", "move",
    "mul", "mux", "not_bit", "or_bit", "real_to_int", "replace", "right", "rol", "ror",
    "rs", "r_trig", "sel", "shl", "shr", "sin", "sqrt", "sr", "sub", "tan", "tof",
    "ton", "tp", "trunc", "xor_bit",
};

bool inTable(std::string_view word, const std::string_view* table, std::size_t n, bool caseSensitive) {
    for (std::size_t i = 0; i < n; ++i) {
        if (caseSensitive ? word == table[i] : equalsCi(word, table[i])) return true;
    }
    return false;
}

void push(std::vector<Token>& out, std::size_t b, std::size_t e, TokenClass c) {
    if (e <= b) return;
    // Merge with the previous token when both are plain text: fewer draw calls.
    if (!out.empty() && out.back().cls == c && out.back().end == b) {
        out.back().end = static_cast<std::uint32_t>(e);
        return;
    }
    out.push_back(Token{static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(e), c});
}

} // namespace

Language languageFromExtension(std::string_view name) noexcept {
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos) return Language::PlainText;
    const auto ext = name.substr(dot + 1);
    if (equalsCi(ext, "c") || equalsCi(ext, "h")) return Language::C;
    if (equalsCi(ext, "cpp") || equalsCi(ext, "cc") || equalsCi(ext, "cxx")
        || equalsCi(ext, "hpp") || equalsCi(ext, "hxx") || equalsCi(ext, "inl"))
        return Language::Cpp;
    if (equalsCi(ext, "st") || equalsCi(ext, "xpg") || equalsCi(ext, "scl"))
        return Language::StructuredText;
    if (equalsCi(ext, "il")) return Language::InstructionList;
    return Language::PlainText;
}

std::string_view toString(Language l) noexcept {
    switch (l) {
        case Language::C:               return "C";
        case Language::Cpp:             return "C++";
        case Language::StructuredText:  return "ST";
        case Language::InstructionList: return "IL";
        case Language::PlainText:       break;
    }
    return "Text";
}

void tokenizeLine(std::string_view line, Language lang, std::vector<Token>& out, bool& inBlockComment) {
    out.clear();
    const bool cLike = lang == Language::C || lang == Language::Cpp;
    const bool stLike = lang == Language::StructuredText || lang == Language::InstructionList;

    std::size_t i = 0;

    // Continuation of a block comment opened on an earlier line.
    if (inBlockComment) {
        const auto close = cLike ? line.find("*/") : line.find("*)");
        if (close == std::string_view::npos) {
            push(out, 0, line.size(), TokenClass::Comment);
            return;
        }
        push(out, 0, close + 2, TokenClass::Comment);
        inBlockComment = false;
        i = close + 2;
    }

    // A preprocessor line is coloured as a whole, but string and comment runs
    // inside it still win, so #include <foo> keeps its bracket text readable.
    if (cLike && i == 0) {
        std::size_t j = 0;
        while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) ++j;
        if (j < line.size() && line[j] == '#') {
            const auto comment = line.find("//", j);
            push(out, 0, comment == std::string_view::npos ? line.size() : comment,
                 TokenClass::Preprocessor);
            if (comment != std::string_view::npos)
                push(out, comment, line.size(), TokenClass::Comment);
            return;
        }
    }

    while (i < line.size()) {
        const char c = line[i];

        // ---- line comment -------------------------------------------------
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            push(out, i, line.size(), TokenClass::Comment);
            return;
        }
        // ---- block comment open -------------------------------------------
        if (cLike && c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            const auto close = line.find("*/", i + 2);
            if (close == std::string_view::npos) {
                push(out, i, line.size(), TokenClass::Comment);
                inBlockComment = true;
                return;
            }
            push(out, i, close + 2, TokenClass::Comment);
            i = close + 2;
            continue;
        }
        if (stLike && c == '(' && i + 1 < line.size() && line[i + 1] == '*') {
            const auto close = line.find("*)", i + 2);
            if (close == std::string_view::npos) {
                push(out, i, line.size(), TokenClass::Comment);
                inBlockComment = true;
                return;
            }
            push(out, i, close + 2, TokenClass::Comment);
            i = close + 2;
            continue;
        }

        // ---- strings and character literals --------------------------------
        if (c == '"' || (cLike && c == '\'') || (stLike && c == '\'')) {
            const char quote = c;
            std::size_t j = i + 1;
            while (j < line.size()) {
                if (cLike && line[j] == '\\') { j += 2; continue; }   // escape
                if (line[j] == quote) { ++j; break; }
                ++j;
            }
            push(out, i, std::min(j, line.size()), TokenClass::String);
            i = j;
            continue;
        }

        // ---- numbers, including ST's typed literals -------------------------
        // 16#FF00, 2#1010, T#500ms, 1.5e-3, 0xDEAD, 42u
        if (digit(c) || (c == '.' && i + 1 < line.size() && digit(line[i + 1]))) {
            std::size_t j = i;
            while (j < line.size()
                   && (identChar(line[j]) || line[j] == '.' || line[j] == '#'
                       || ((line[j] == '+' || line[j] == '-') && j > i
                           && (lower(line[j - 1]) == 'e'))))
                ++j;
            push(out, i, j, TokenClass::Number);
            i = j;
            continue;
        }

        // ---- ST located addresses: %MW1174, %I0.3 --------------------------
        if (stLike && c == '%') {
            std::size_t j = i + 1;
            while (j < line.size() && (identChar(line[j]) || line[j] == '.')) ++j;
            push(out, i, j, TokenClass::Constant);
            i = j;
            continue;
        }

        // ---- identifiers ----------------------------------------------------
        if (identStart(c)) {
            std::size_t j = i;
            while (j < line.size() && identChar(line[j])) ++j;

            // ST typed literals begin with a prefix, not a digit: T#500ms,
            // TIME#1s, DATE#2024-01-01, DT#..., BOOL#1. Scanning them here is
            // what keeps "T#500ms" one number instead of an identifier, a hash
            // and a number.
            if (stLike && j < line.size() && line[j] == '#') {
                std::size_t k = j + 1;
                while (k < line.size() && (identChar(line[k]) || line[k] == '-'
                                           || line[k] == ':' || line[k] == '.' || line[k] == '_'))
                    ++k;
                push(out, i, k, TokenClass::Number);
                i = k;
                continue;
            }

            const auto word = line.substr(i, j - i);

            TokenClass cls = TokenClass::Text;
            if (lang == Language::Cpp) {
                if (inTable(word, kCppKeywords, std::size(kCppKeywords), true)) cls = TokenClass::Keyword;
                else if (inTable(word, kCTypes, std::size(kCTypes), true))      cls = TokenClass::Type;
            } else if (lang == Language::C) {
                if (inTable(word, kCKeywords, std::size(kCKeywords), true))     cls = TokenClass::Keyword;
                else if (inTable(word, kCTypes, std::size(kCTypes), true))      cls = TokenClass::Type;
            } else if (stLike) {
                // ST is case-insensitive, so IF, If and if are one keyword.
                if (inTable(word, kStKeywords, std::size(kStKeywords), false))       cls = TokenClass::Keyword;
                else if (inTable(word, kStTypes, std::size(kStTypes), false))        cls = TokenClass::Type;
                else if (inTable(word, kStFunctions, std::size(kStFunctions), false)) cls = TokenClass::Function;
            }

            // Anything immediately followed by '(' reads as a call, which is how
            // a project DFB gets picked out without a symbol table.
            if (cls == TokenClass::Text) {
                std::size_t k = j;
                while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) ++k;
                if (k < line.size() && line[k] == '(') cls = TokenClass::Function;
            }
            push(out, i, j, cls);
            i = j;
            continue;
        }

        // ---- operators and punctuation ---------------------------------------
        if (std::string_view("+-*/%<>=!&|^~?:;,.()[]{}").find(c) != std::string_view::npos) {
            push(out, i, i + 1, TokenClass::Operator);
            ++i;
            continue;
        }

        push(out, i, i + 1, TokenClass::Text);
        ++i;
    }
}

void tokenize(std::string_view source, Language lang, std::vector<Token>& out) {
    out.clear();
    std::vector<Token> lineTokens;
    bool inBlock = false;
    std::size_t offset = 0;

    while (offset <= source.size()) {
        const auto nl = source.find('\n', offset);
        const auto end = (nl == std::string_view::npos) ? source.size() : nl;
        tokenizeLine(source.substr(offset, end - offset), lang, lineTokens, inBlock);
        for (auto t : lineTokens) {
            t.begin += static_cast<std::uint32_t>(offset);
            t.end   += static_cast<std::uint32_t>(offset);
            out.push_back(t);
        }
        if (nl == std::string_view::npos) break;
        offset = nl + 1;
    }
}

gfx::Color colorFor(TokenClass c, const Palette& p) {
    switch (c) {
        case TokenClass::Keyword:      return p.syntaxKeyword;
        case TokenClass::Type:         return p.syntaxType;
        case TokenClass::Preprocessor: return p.syntaxPreprocessor;
        case TokenClass::Comment:      return p.syntaxComment;
        case TokenClass::String:       return p.syntaxString;
        case TokenClass::Number:       return p.syntaxNumber;
        case TokenClass::Function:     return p.syntaxFunction;
        case TokenClass::Constant:     return p.syntaxConstant;
        case TokenClass::Operator:     return p.syntaxOperator;
        case TokenClass::Text:         break;
    }
    return p.text;
}

} // namespace ui
