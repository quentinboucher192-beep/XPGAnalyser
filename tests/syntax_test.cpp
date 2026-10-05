// =============================================================================
//  tests/syntax_test.cpp — the tokeniser, on the two languages that matter
// -----------------------------------------------------------------------------
//  The interesting cases are the ones where getting it wrong is invisible until
//  someone reads the code: a block comment that must survive a line break, an ST
//  keyword written in lower case, and C++ keywords that must NOT light up inside
//  a Structured Text body.
// =============================================================================
#include "../src/ui/Syntax.hpp"
#include "../src/ui/Theme.hpp"

#include <cassert>
#include <cstdio>
#include <map>
#include <string>

using namespace ui;

namespace {

std::map<TokenClass, std::string> classify(std::string_view src, Language lang) {
    std::vector<Token> toks;
    tokenize(src, lang, toks);
    std::map<TokenClass, std::string> out;
    for (const auto& t : toks)
        out[t.cls] += std::string(src.substr(t.begin, t.end - t.begin)) + "|";
    return out;
}

bool has(const std::map<TokenClass, std::string>& m, TokenClass c, std::string_view needle) {
    const auto it = m.find(c);
    return it != m.end() && it->second.find(needle) != std::string::npos;
}

} // namespace

int main() {
    // --- Structured Text, as a Control Expert section actually looks --------
    {
        const std::string st =
            "(* start the pump *)\n"
            "IF gStartBtn AND NOT gFault THEN\n"
            "  Timer(IN := TRUE, PT := T#500ms);\n"
            "  %MW10 := 16#FF00;\n"
            "  msg := 'ready';\n"
            "END_IF;\n";
        const auto m = classify(st, Language::StructuredText);
        std::printf("ST keywords : %s\n", m.at(TokenClass::Keyword).c_str());
        std::printf("ST comments : %s\n", m.at(TokenClass::Comment).c_str());
        std::printf("ST numbers  : %s\n", m.at(TokenClass::Number).c_str());
        std::printf("ST addresses: %s\n", m.at(TokenClass::Constant).c_str());

        assert(has(m, TokenClass::Keyword, "IF|"));
        assert(has(m, TokenClass::Keyword, "AND|"));
        assert(has(m, TokenClass::Keyword, "END_IF|"));
        assert(has(m, TokenClass::Comment, "(* start the pump *)"));
        assert(has(m, TokenClass::Number, "16#FF00"));   // typed literal, not "16"
        assert(has(m, TokenClass::Number, "T#500ms"));   // duration literal
        assert(has(m, TokenClass::Constant, "%MW10"));   // located address
        assert(has(m, TokenClass::String, "'ready'"));
    }

    // --- ST is case-insensitive: if, If and IF are one keyword --------------
    {
        const auto m = classify("if x then y := 1; end_if;", Language::StructuredText);
        assert(has(m, TokenClass::Keyword, "if|"));
        assert(has(m, TokenClass::Keyword, "then|"));
        assert(has(m, TokenClass::Keyword, "end_if|"));
    }

    // --- C++, including a comment that spans two lines ----------------------
    {
        const std::string cpp =
            "#include <vector>\n"
            "// count them\n"
            "int count(const std::vector<int>& v) {\n"
            "  /* multi\n     line */\n"
            "  return static_cast<int>(v.size()) + 0x2A;\n"
            "}\n";
        const auto m = classify(cpp, Language::Cpp);
        std::printf("C++ keywords: %s\n", m.at(TokenClass::Keyword).c_str());
        std::printf("C++ preproc : %s\n", m.at(TokenClass::Preprocessor).c_str());

        assert(has(m, TokenClass::Preprocessor, "#include <vector>"));
        assert(has(m, TokenClass::Keyword, "const|"));
        assert(has(m, TokenClass::Keyword, "return|"));
        assert(has(m, TokenClass::Type, "int|"));
        assert(has(m, TokenClass::Number, "0x2A"));
        // Both halves of the block comment must be coloured: the state has to
        // carry across the newline, which is what the per-line flag is for.
        assert(has(m, TokenClass::Comment, "/* multi"));
        assert(has(m, TokenClass::Comment, "line */"));
        assert(has(m, TokenClass::Function, "count|"));
    }

    // --- languages must not bleed into each other ---------------------------
    {
        const auto asSt = classify("return class int;", Language::StructuredText);
        assert(!has(asSt, TokenClass::Keyword, "class"));   // not an ST keyword
        const auto asCpp = classify("END_IF THEN", Language::Cpp);
        assert(asCpp.find(TokenClass::Keyword) == asCpp.end());
    }

    assert(languageFromExtension("Driver.hpp") == Language::Cpp);
    assert(languageFromExtension("io.c") == Language::C);
    assert(languageFromExtension("MAST.XPG") == Language::StructuredText);
    assert(languageFromExtension("readme.txt") == Language::PlainText);

    // Colours must resolve for every class, in both themes.
    for (const auto& theme : {Theme::dark(), Theme::light()})
        for (int i = 0; i <= static_cast<int>(TokenClass::Constant); ++i)
            (void)colorFor(static_cast<TokenClass>(i), theme.color);

    std::printf("\nsyntax_test: all assertions passed\n");
    return 0;
}
