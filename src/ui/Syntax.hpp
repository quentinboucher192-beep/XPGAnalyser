// =============================================================================
//  ui/Syntax.hpp — tokeniser for the source preview
// -----------------------------------------------------------------------------
//  Deliberately a *lexer*, not a parser: highlighting must survive incomplete
//  and syntactically wrong code, because half of what an engineer opens in a
//  viewer is code that does not compile yet. It also has to run on every visible
//  line of a 20 000-line section at 60 fps, so it is a single left-to-right pass
//  with no backtracking and no allocation per token beyond the output vector.
//
//  Colours follow the convention every editor in this space shares (Visual
//  Studio dark, VS Code Dark+, Qt Creator dark): blue keywords, teal types,
//  green comments, orange strings, pale green numbers, mauve preprocessor.
//  Engineers read code faster in a palette their eyes already know.
// =============================================================================
#pragma once

#include "../platform/Geometry.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace ui {

struct Palette;   // ui/Theme.hpp

enum class Language : std::uint8_t {
    PlainText,
    C,
    Cpp,
    StructuredText,   // IEC 61131-3 ST, what a Control Expert section contains
    InstructionList,  // IEC 61131-3 IL
};

enum class TokenClass : std::uint8_t {
    Text, Keyword, Type, Preprocessor, Comment, String, Number, Operator, Function, Constant,
};

struct Token {
    std::uint32_t begin{0};
    std::uint32_t end{0};
    TokenClass    cls{TokenClass::Text};
};

// Language guessed from a file name or, for a POU section, from its body.
[[nodiscard]] Language languageFromExtension(std::string_view fileName) noexcept;
[[nodiscard]] std::string_view toString(Language) noexcept;

// Tokenises one line. `inBlockComment` carries the multi-line comment state from
// the previous line, so the highlighter can start at the first *visible* line
// instead of re-scanning the file from the top on every scroll.
void tokenizeLine(std::string_view line, Language, std::vector<Token>& out, bool& inBlockComment);

// Whole-buffer variant, used when a document is opened.
void tokenize(std::string_view source, Language, std::vector<Token>& out);

[[nodiscard]] gfx::Color colorFor(TokenClass, const Palette&);

} // namespace ui
