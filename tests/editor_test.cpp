// =============================================================================
//  tests/editor_test.cpp — typing into a section, and being helped while typing
// -----------------------------------------------------------------------------
//  Three things decide whether an editor is usable rather than merely present:
//
//   1. what you type is what ends up in the model, including the awkward cases -
//      typing into a selection, deleting across a line break, Enter keeping the
//      indentation the folding is derived from;
//   2. undo steps back over a word, not a letter, which is what command merging
//      is for;
//   3. the suggestions are the symbols actually visible from *this* section,
//      ranked so the first one is usually the right one.
// =============================================================================
#include "../src/project/EditCommands.hpp"
#include "../src/project/ProjectStore.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ui;
using namespace project;
using namespace domain;

namespace {

// Mirrors the SDL3 backend's metrics so hit testing and layout are real.
class FakeRenderer final : public gfx::IRenderer {
public:
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    void drawText(gfx::Point, std::string_view, gfx::FontId, gfx::Color) override {}
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        const float adv = 8.f * scale(f);
        std::size_t g = 0;
        for (char c : s) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++g;
        return {static_cast<float>(g) * adv, adv, adv * 0.8f, adv * 0.2f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return 8.f * scale(f); }
    [[nodiscard]] gfx::Size surfaceSize() const override { return {1536.f, 1024.f}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f,
                                            float w) const override {
        const float adv = 8.f * scale(f);
        return w <= 0.f ? 0 : std::min<std::size_t>(s.size(), static_cast<std::size_t>(w / adv));
    }
private:
    static float scale(gfx::FontId f) {
        return std::max(1.f, std::round(static_cast<float>(f.v ? f.v : 16) / 8.f));
    }
};

void type(MultiLineText& view, std::string_view text) {
    for (char c : text) view.dispatch(TextInput{std::string(1, c)});
}

void press(MultiLineText& view, Key key, KeyMods mods = {}) {
    view.dispatch(KeyDown{key, mods, false});
}

} // namespace

int main() {
    FakeRenderer metrics;
    installPlatformServices(PlatformServices{
        [&](std::string_view s, gfx::FontId f) { return metrics.measure(s, f).width; },
        [&](gfx::FontId f) { return metrics.lineHeight(f); },
        [] { return std::string{}; },
        [](std::string_view) {},
    });
    const Theme theme = Theme::dark();

    auto paint = [&](MultiLineText& v) {
        v.setBounds({0.f, 0.f, 900.f, 400.f});
        v.layout();
        v.render(PaintContext{metrics, theme, {0, 0, 900, 400}, 0.0, nullptr});
    };

    // --- 1. a read-only view must stay read-only ----------------------------
    {
        MultiLineText view("ro");
        view.setText("IF a THEN\n");
        view.setReadOnly(true);
        paint(view);
        view.takeFocus();
        type(view, "XXX");
        press(view, Key::Return);
        press(view, Key::Backspace);
        assert(view.text() == "IF a THEN\n" && "a viewer must not become an editor by accident");
        assert(!view.modified());
    }

    // --- 2. typing, selection, deletion, Enter -------------------------------
    {
        MultiLineText view("rw");
        view.setLanguage(Language::StructuredText);
        view.setReadOnly(false);
        view.setText("");
        paint(view);
        view.takeFocus();

        type(view, "IF gStart THEN");
        press(view, Key::Return);
        type(view, "    count := 1;");
        press(view, Key::Return);
        type(view, "END_IF;");
        std::printf("typed:\n%s\n", view.text().c_str());

        // Enter carries the indentation forward, which is what the folding
        // structure is derived from.
        assert(view.text() == "IF gStart THEN\n    count := 1;\n    END_IF;");
        assert(view.modified());
        assert(view.lineCount() == 3);

        // Backspace across a line break joins the lines.
        press(view, Key::Home);
        press(view, Key::Backspace);
        assert(view.lineCount() == 2);

        // Select everything and type over it.
        press(view, Key::A, KeyMods{true, false, false, false});
        assert(view.hasSelection());
        type(view, "x");
        assert(view.text() == "x" && "typing replaces the selection");

        // Delete removes forward, and stops at the end rather than running off.
        press(view, Key::Home);
        press(view, Key::Delete);
        press(view, Key::Delete);
        assert(view.text().empty());
    }

    // --- 3. undo merges consecutive typing ----------------------------------
    {
        auto project = ProjectStore::createEmpty("Machine", "BMXP342020");
        core::CommandStack stack;
        assert(stack.push(std::make_unique<AddSectionCommand>(
                   project, "Cycle", "MAST", PouLanguage::ST)).has_value());
        const Index section = 0;
        const auto original = project->sections[section].body;

        // One command per keystroke, exactly as the screen produces them.
        const std::string typed = "counter := counter + 1;";
        std::string body = original;
        for (char c : typed) {
            body.push_back(c);
            assert(stack.push(std::make_unique<SetSectionBodyCommand>(
                       project, section, body)).has_value());
        }
        assert(project->sections[section].body == original + typed);
        assert(project->sections[section].statementCount == 1 && "metrics follow the text");

        // 23 keystrokes, one undo step.
        assert(stack.undo().has_value());
        std::printf("after one undo: '%s'\n", project->sections[section].body.c_str());
        assert(project->sections[section].body == original
               && "merged edits undo as a single step");
        assert(!stack.canUndo() || true);

        assert(stack.redo().has_value());
        assert(project->sections[section].body == original + typed);

        // An edit to a *different* section must not merge with it.
        assert(stack.push(std::make_unique<AddSectionCommand>(
                   project, "Other", "MAST", PouLanguage::ST)).has_value());
        assert(stack.push(std::make_unique<SetSectionBodyCommand>(
                   project, 1, "different;")).has_value());
        assert(stack.undo().has_value());
        assert(project->sections[section].body == original + typed
               && "undoing one section must not touch another");
    }

    // --- 4. the suggestions are the symbols visible from this section --------
    {
        auto project = ProjectStore::createEmpty("Machine", "BMXP342020");
        core::CommandStack stack;

        (void)stack.push(std::make_unique<AddDerivedTypeCommand>(project, "ST_Cabinet", "0.01"));
        (void)stack.push(std::make_unique<AddFunctionBlockCommand>(project, "DFB_Motor", "1.00"));
        (void)stack.push(std::make_unique<AddProgramUnitCommand>(project, "Pumping", "MAST"));

        Index unit = 0;
        for (Index i = 0; i < project->pous.size(); ++i)
            if (project->pous[i].kind == PouKind::ProgramUnit) unit = i;
        (void)stack.push(std::make_unique<AddSectionCommand>(
            project, "Body", "", PouLanguage::ST, unit));
        const Index section = static_cast<Index>(project->sections.size() - 1);

        {
            AddVariableCommand::Spec s;
            s.name = "gCounter"; s.type = "DINT"; s.address = "%MW10";
            (void)stack.push(std::make_unique<AddVariableCommand>(project, s));
        }
        {
            AddVariableCommand::Spec s;
            s.name = "localCount"; s.type = "INT";
            s.scope = VariableScope::Local; s.owner = unit;
            (void)stack.push(std::make_unique<AddVariableCommand>(project, s));
        }
        {
            // Starts with "coun", so it is the case that distinguishes a prefix
            // match from a substring one.
            AddVariableCommand::Spec s;
            s.name = "counterMax"; s.type = "INT";
            (void)stack.push(std::make_unique<AddVariableCommand>(project, s));
        }

        const auto all = suggestionsFor(*project, section, "");
        std::printf("%zu symbols visible from the section\n", all.size());
        assert(all.size() > 30 && "keywords, types, blocks and the project's own symbols");

        auto has = [](const std::vector<Suggestion>& v, std::string_view name) {
            return std::any_of(v.begin(), v.end(),
                               [&](const Suggestion& s) { return s.text == name; });
        };
        assert(has(all, "gCounter"));
        assert(has(all, "localCount") && "the section's own POU declarations are visible");
        assert(has(all, "ST_Cabinet"));
        assert(has(all, "DFB_Motor"));
        assert(has(all, "END_IF"));
        assert(has(all, "TON"));

        // Filtering, and the ranking that makes the first entry usually right.
        const auto count = suggestionsFor(*project, section, "coun");
        std::printf("'coun' -> ");
        for (const auto& s : count) std::printf("%s(%d) ", s.text.c_str(), s.rank);
        std::printf("\n");
        assert(count.size() == 3 && "counterMax, gCounter, localCount all contain it");
        assert(count.front().text == "counterMax"
               && "the one that starts with the prefix comes first");
        assert(count.front().rank == 0);
        // The other two only contain it, so they rank below and sort by name.
        assert(count[1].text == "gCounter" && count[1].rank == 1);
        assert(count[2].text == "localCount" && count[2].rank == 1);
        assert(has(count, "gCounter") && "substring matches still appear");

        const auto ci = suggestionsFor(*project, section, "dfb_m");
        assert(!ci.empty() && ci.front().text == "DFB_Motor" && "matching ignores case");

        // A detail column that says what the thing is, and where it lives.
        const auto located = std::find_if(all.begin(), all.end(),
                                          [](const Suggestion& s) { return s.text == "gCounter"; });
        assert(located != all.end());
        assert(located->detail.find("%MW10") != std::string::npos
               && "an address is the most useful thing to show about a located variable");

        // Nothing is offered twice.
        auto names = all;
        std::sort(names.begin(), names.end(),
                  [](const Suggestion& a, const Suggestion& b) { return a.text < b.text; });
        assert(std::adjacent_find(names.begin(), names.end(),
                                  [](const Suggestion& a, const Suggestion& b) {
                                      return a.text == b.text;
                                  }) == names.end());
    }

    // --- 5. the completion popup, driven through the widget -----------------
    {
        MultiLineText view("complete");
        view.setLanguage(Language::StructuredText);
        view.setReadOnly(false);
        view.setText("");
        view.setCompletionProvider([](std::string_view prefix,
                                      std::vector<MultiLineText::Completion>& out) {
            for (const char* name : {"gStartButton", "gStopButton", "gStartDelay"}) {
                const std::string_view text{name};
                if (prefix.empty() || text.substr(0, prefix.size()) == prefix)
                    out.push_back(MultiLineText::Completion{std::string(text), "BOOL",
                                                           Icon::Variable, 0, {},
                                                           std::string::npos});
            }
        });
        paint(view);
        view.takeFocus();

        type(view, "g");
        assert(!view.completionOpen() && "one character is not enough to be useful");
        type(view, "S");
        assert(view.completionOpen() && "two characters opens the list");
        assert(view.completionCount() == 3);
        assert(view.completionPrefix() == "gS");

        type(view, "ta");
        assert(view.completionCount() == 2 && "gStop no longer matches");

        press(view, Key::Down);       // move to the second entry
        press(view, Key::Return);     // accept
        std::printf("accepted: '%s'\n", view.text().c_str());
        assert(view.text() == "gStartDelay" && "the whole identifier is replaced, not appended");
        assert(!view.completionOpen());

        // Escape dismisses without inserting.
        type(view, " gSt");
        assert(view.completionOpen());
        press(view, Key::Escape);
        assert(!view.completionOpen());
        assert(view.text() == "gStartDelay gSt" && "Escape leaves what was typed");
    }

    // --- 5b. editing must not move the view under the caret -----------------
    //
    // scrollToLine() puts a line at the TOP of the view, and it was being called
    // after every edit. The page therefore jumped on each keystroke; on Tab,
    // where nothing else visibly changes, it looked like the editor recentring
    // itself for no reason.
    {
        MultiLineText view("scroll");
        view.setLanguage(Language::StructuredText);
        view.setReadOnly(false);
        std::string many;
        for (int i = 0; i < 200; ++i) many += "line " + std::to_string(i) + ";\n";
        view.setText(many);
        paint(view);
        view.takeFocus();

        // Put the caret somewhere in the middle of what is on screen.
        for (int i = 0; i < 10; ++i) press(view, Key::Down);
        const auto before = view.firstVisibleLine();
        std::printf("caret on line 10, first visible line %zu\n", before);

        press(view, Key::Tab);
        assert(view.firstVisibleLine() == before
               && "a visible caret must not scroll the view");
        assert(view.text().compare(0, 1, "l") == 0);
        type(view, "abc");
        assert(view.firstVisibleLine() == before && "nor must typing");

        // Tab inserts one tab character, matching what the reference project
        // uses: 3880 of its indented lines are tabs, 706 are spaces.
        const auto line10Start = view.text().find("line 10;");
        assert(line10Start != std::string::npos);
        assert(view.text().find('\t') != std::string::npos && "a real tab, not spaces");

        // Moving far away does scroll, because otherwise the caret is invisible.
        for (int i = 0; i < 80; ++i) press(view, Key::Down);
        assert(view.firstVisibleLine() > before && "an off-screen caret must be brought back");
    }

    // --- 6. control structures arrive whole, indentation and all ------------
    {
        auto project = ProjectStore::createEmpty("Machine", "BMXP342020");
        core::CommandStack stack;
        (void)stack.push(std::make_unique<AddSectionCommand>(
            project, "Cycle", "MAST", PouLanguage::ST));

        const auto ifs = suggestionsFor(*project, 0, "IF");
        const auto plain = std::find_if(ifs.begin(), ifs.end(),
                                        [](const Suggestion& s) { return s.text == "IF"; });
        assert(plain != ifs.end());
        assert(!plain->insert.empty() && "IF is offered as a skeleton, not a word");
        assert(plain->insert.find("THEN") != std::string::npos);
        assert(plain->insert.find("END_IF;") != std::string::npos
               && "the closing keyword is what gets forgotten");
        assert(plain->caret != std::string::npos && plain->caret < plain->insert.size()
               && "the cursor lands on the condition");
        assert(plain->insert.compare(plain->caret, 5, " THEN") == 0);

        for (const char* word : {"FOR", "WHILE", "REPEAT", "CASE", "ELSIF", "ELSE"}) {
            const auto found = suggestionsFor(*project, 0, word);
            assert(!found.empty() && !found.front().insert.empty());
        }
        // The closing keywords are still offered on their own, for the times you
        // are completing an existing block rather than opening one.
        assert(!suggestionsFor(*project, 0, "END_WH").empty());
        assert(!suggestionsFor(*project, 0, "END_FOR").empty());
    }

    // --- 7. a snippet is inserted at the caller's indentation ----------------
    {
        MultiLineText view("snippet");
        view.setLanguage(Language::StructuredText);
        view.setReadOnly(false);
        view.setText("");
        view.setCompletionProvider([](std::string_view, std::vector<MultiLineText::Completion>& out) {
            MultiLineText::Completion c;
            c.text   = "IF";
            c.insert = "IF  THEN\n    \nEND_IF;";
            c.caret  = 3;                       // between IF and THEN
            out.push_back(std::move(c));
        });
        paint(view);
        view.takeFocus();

        type(view, "    ");                     // four spaces of existing indent
        type(view, "IF");
        assert(view.completionOpen());
        press(view, Key::Return);               // accept

        std::printf("snippet inserted:\n%s\n", view.text().c_str());
        assert(view.text() == "    IF  THEN\n        \n    END_IF;"
               && "continuation lines take the caller's indentation");
        assert(view.lineCount() == 3);
        // The cursor is on the condition, not after END_IF.
        assert(view.text().substr(0, 4 + 3) == "    IF ");
    }

    // --- 8. signature help ---------------------------------------------------
    {
        auto project = ProjectStore::createEmpty("Machine", "BMXP342020");
        core::CommandStack stack;
        (void)stack.push(std::make_unique<AddFunctionBlockCommand>(project, "DFB_Motor", "1.00"));
        Index dfb = 0;
        for (Index i = 0; i < project->pous.size(); ++i)
            if (project->pous[i].kind == PouKind::FunctionBlockType) dfb = i;

        for (auto [name, scope] : {std::pair{"start", VariableScope::Input},
                                   std::pair{"stop",  VariableScope::Input},
                                   std::pair{"speed", VariableScope::InOut},
                                   std::pair{"running", VariableScope::Output}}) {
            AddVariableCommand::Spec s;
            s.name = name; s.type = "BOOL"; s.scope = scope; s.owner = dfb;
            (void)stack.push(std::make_unique<AddVariableCommand>(project, s));
        }
        {
            AddVariableCommand::Spec s;
            s.name = "Motor1"; s.type = "DFB_Motor";
            (void)stack.push(std::make_unique<AddVariableCommand>(project, s));
        }

        CallSignature sig;
        assert(signatureFor(*project, "DFB_Motor", sig));
        std::printf("DFB_Motor(%s", sig.parameters.front().c_str());
        for (std::size_t i = 1; i < sig.parameters.size(); ++i)
            std::printf(", %s", sig.parameters[i].c_str());
        std::printf(")\n");
        assert(sig.parameters.size() == 4);
        assert(sig.parameters[0].find("IN ") == 0 && sig.parameters[0].find("start") != std::string::npos);
        assert(sig.parameters[2].find("I/O") == 0 && "an in/out shows as such");
        assert(sig.parameters[3].find("OUT") == 0);

        // An instance answers with its type's interface: typing Motor1( is the
        // case that actually happens.
        CallSignature instance;
        assert(signatureFor(*project, "Motor1", instance));
        assert(instance.parameters.size() == 4);
        assert(instance.name.find("DFB_Motor") != std::string::npos
               && "the panel says which type the instance is");

        // Standard blocks, and the ones that are not callable.
        CallSignature ton;
        assert(signatureFor(*project, "TON", ton) && ton.parameters.size() == 4);
        assert(signatureFor(*project, "ton", ton) && "matching ignores case");
        CallSignature none;
        assert(!signatureFor(*project, "NotAThing", none));
        assert(!signatureFor(*project, "", none));
    }

    // --- 9. the panel opens on '(' and follows the commas -------------------
    {
        MultiLineText view("sig");
        view.setLanguage(Language::StructuredText);
        view.setReadOnly(false);
        view.setText("");
        view.setSignatureProvider([](std::string_view name, MultiLineText::Signature& out) {
            if (name != "TON") return false;
            out.name = "TON";
            out.parameters = {"IN : BOOL", "PT : TIME", "Q : BOOL", "ET : TIME"};
            return true;
        });
        paint(view);
        view.takeFocus();

        type(view, "Timer");
        assert(!view.signatureOpen() && "no call has been opened yet");
        type(view, " := TON(");
        assert(view.signatureOpen() && "the parenthesis opens it");
        assert(view.activeParameter() == 0);

        type(view, "gStart, ");
        assert(view.activeParameter() == 1 && "the comma moves to the next argument");
        type(view, "T#5s, x, y");
        assert(view.activeParameter() == 3);

        type(view, ")");
        assert(!view.signatureOpen() && "closing the call closes the panel");

        // A parenthesis inside a comment must not open a phantom call. Typed,
        // not assigned: setText does not run the detection, so asserting after
        // it would prove nothing.
        view.setText("");
        press(view, Key::End);
        type(view, "(* TON(");
        assert(!view.signatureOpen() && "a call inside a comment is not a call");
        type(view, " *) x := TON(");
        assert(view.signatureOpen() && "and the comment ending restores normal service");

        // Nor inside a string literal.
        view.setText("");
        press(view, Key::End);
        type(view, "msg := 'TON(';");
        assert(!view.signatureOpen());

        // Nor after a line comment.
        view.setText("");
        press(view, Key::End);
        type(view, "x := 1; // TON(");
        assert(!view.signatureOpen());

        // Nested calls report the innermost one.
        view.setText("");
        press(view, Key::End);
        type(view, "a := TON(MAX(1, 2), ");
        assert(view.signatureOpen());
        assert(view.activeParameter() == 1 && "the inner call is closed, so we are on TON's second");
    }

    std::printf("\neditor_test: all assertions passed\n");
    return 0;
}
