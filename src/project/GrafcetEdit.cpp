#include "GrafcetEdit.hpp"

#include <algorithm>
#include <cctype>

namespace project {

    using namespace domain;

    namespace {

        std::string joinLines(const std::vector<std::string>& lines) {
            std::string out;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (i) out.push_back('\n');
                out += lines[i];
            }
            return out;
        }

        // The whitespace a line starts with, so a replacement sits where its neighbours
        // do instead of announcing which lines a tool wrote.
        std::string indentOf(std::string_view line) {
            std::size_t at = 0;
            while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
            return std::string(line.substr(0, at));
        }

        // Where the statement's terminating semicolon is, scanning from the assignment.
        // Quotes are respected because 'a;b' is a legal string literal, and a comment
        // because (* ; *) is legal too - reading either as the end of the statement
        // would cut the line in the wrong place.
        std::size_t statementEnd(std::string_view line, std::size_t from) {
            bool inString = false, inComment = false;
            for (std::size_t i = from; i < line.size(); ++i) {
                if (inComment) {
                    if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == ')') { inComment = false; ++i; }
                    continue;
                }
                if (inString) { if (line[i] == '\'') inString = false; continue; }
                if (i + 1 < line.size() && line[i] == '(' && line[i + 1] == '*') { inComment = true; ++i; continue; }
                if (line[i] == '\'') { inString = true; continue; }
                if (line[i] == ';') return i;
            }
            return std::string_view::npos;
        }

        // The carriage return, if this file has them. Kept per line rather than assumed
        // for the file: a project that mixes them must come back out mixed the same way.
        bool endsWithCr(std::string_view line) { return !line.empty() && line.back() == '\r'; }

        std::string withEnding(std::string text, bool cr) {
            if (cr) text.push_back('\r');
            return text;
        }

        // "Trans_ManuA[2]" at the start of a statement. Whole-token, so Trans_ManuAB
        // cannot match Trans_ManuA.
        bool mentionsElement(std::string_view line, std::string_view array,
            std::string_view prefix, int index) {
            const std::string needle = std::string(array) + "_" + std::string(prefix) + "["
                + std::to_string(index) + "]";
            const auto at = line.find(needle);
            if (at == std::string_view::npos) return false;
            const auto after = at + needle.size();
            return after >= line.size() || line[after] == '.' || line[after] == ' ';
        }

        std::uint32_t countLines(const std::string& body) {
            if (body.empty()) return 0;
            return static_cast<std::uint32_t>(std::count(body.begin(), body.end(), '\n') + 1);
        }

        void replaceBody(Project& p, Index section, std::string body) {
            p.sections[section].body = std::move(body);
            p.sections[section].lineCount = countLines(p.sections[section].body);
        }

    } // namespace

    // ---------------------------------------------------------------------------
    bool findConditionLine(const Project& p, Index section, std::string_view arrayPrefix,
        int transitionId, std::size_t& line) {
        if (section >= p.sections.size()) return false;
        const auto lines = grafcet::splitLines(p.sections[section].body);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (!mentionsElement(lines[i], "Trans", arrayPrefix, transitionId)) continue;
            if (lines[i].find(".Condition") == std::string::npos) continue;
            if (lines[i].find(":=") == std::string::npos) continue;
            line = i;
            return true;
        }
        return false;
    }

    bool findActionBlock(const Project& p, Index section, std::string_view arrayPrefix,
        int actionId, std::size_t& openLine, std::size_t& closeLine) {
        if (section >= p.sections.size()) return false;
        const auto lines = grafcet::splitLines(p.sections[section].body);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (!mentionsElement(lines[i], "Acts", arrayPrefix, actionId)) continue;
            if (lines[i].find(".Out") == std::string::npos) continue;

            const auto close = grafcet::blockEnd(lines, i);
            if (close == std::string::npos) return false;   // unterminated: refuse
            openLine = i;
            closeLine = close;
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------------------
    SetTransitionConditionCommand::SetTransitionConditionCommand(
        ProjectPtr project, Index section, int transitionId, std::string expression)
        : project_(std::move(project)), section_(section), transitionId_(transitionId),
        expression_(std::move(expression)) {}

    std::string SetTransitionConditionCommand::label() const {
        return "edit condition of T" + std::to_string(transitionId_);
    }

    core::Status SetTransitionConditionCommand::execute() {
        if (!project_ || section_ >= project_->sections.size())
            return core::fail(core::ErrorCode::OutOfRange, "that section no longer exists");

        const auto chart = grafcet::parseChart(*project_, section_);
        std::size_t at = 0;
        if (!findConditionLine(*project_, section_, chart.arrayPrefix, transitionId_, at))
            return core::fail(core::ErrorCode::IncompleteProject,
                "T" + std::to_string(transitionId_)
                + " has no '.Condition :=' line in this section. Add one in the "
                "editor first: where a new statement belongs is the author's "
                "choice, not this tool's.");

        auto lines = grafcet::splitLines(project_->sections[section_].body);
        previousBody_ = project_->sections[section_].body;

        std::string expression = expression_;
        while (!expression.empty()
            && std::isspace(static_cast<unsigned char>(expression.back()))) expression.pop_back();
        if (!expression.empty() && expression.back() == ';') expression.pop_back();

        // Rebuilt from the parts of the old line that are NOT the expression: the
        // indentation before it, and everything after the semicolon.
        //
        // That tail matters more than it looks. The sweep over all 193 transitions
        // caught this: one line ends with four tabs after its semicolon, and dropping
        // them made an identity rewrite change bytes. Trailing whitespace is the
        // harmless version - the same tail can hold an inline comment, and deleting
        // an author's words while "setting a value to what it already is" would be
        // indefensible.
        const auto& old = lines[at];
        const auto assign = old.find(":=");
        const auto terminator = statementEnd(old, assign + 2);

        std::string tail;
        if (terminator != std::string::npos) {
            tail = old.substr(terminator + 1);
            if (endsWithCr(tail)) tail.pop_back();   // re-added below, once
        }
        // And the spacing after ':=' is preserved too, for the same reason: these
        // sections align their assignments in a column, and normalising two spaces
        // to one would reflow a block the author lined up by hand.
        std::size_t exprStart = assign + 2;
        while (exprStart < old.size()
            && (old[exprStart] == ' ' || old[exprStart] == '\t')) ++exprStart;
        const std::string head = old.substr(0, exprStart);

        lines[at] = withEnding(head + expression + ";" + tail, endsWithCr(old));

        replaceBody(*project_, section_, joinLines(lines));
        applied_ = true;
        return core::ok();
    }

    core::Status SetTransitionConditionCommand::undo() {
        if (!applied_ || !project_ || section_ >= project_->sections.size()) return core::ok();
        replaceBody(*project_, section_, previousBody_);
        applied_ = false;
        return core::ok();
    }

    // ---------------------------------------------------------------------------
    SetActionBodyCommand::SetActionBodyCommand(ProjectPtr project, Index actionSection,
        std::string arrayPrefix, int actionId,
        std::string body)
        : project_(std::move(project)), section_(actionSection),
        arrayPrefix_(std::move(arrayPrefix)), actionId_(actionId), body_(std::move(body)) {}

    std::string SetActionBodyCommand::label() const {
        return "edit body of A" + std::to_string(actionId_);
    }

    core::Status SetActionBodyCommand::execute() {
        if (!project_ || section_ >= project_->sections.size())
            return core::fail(core::ErrorCode::OutOfRange, "that section no longer exists");

        std::size_t open = 0, close = 0;
        if (!findActionBlock(*project_, section_, arrayPrefix_, actionId_, open, close))
            return core::fail(core::ErrorCode::IncompleteProject,
                "A" + std::to_string(actionId_)
                + " has no 'IF ... .Out THEN ... END_IF' block here, or the block "
                "is not closed. Nothing was changed.");

        auto lines = grafcet::splitLines(project_->sections[section_].body);
        previousBody_ = project_->sections[section_].body;

        // Match what is already there. Falling back to the opening line's indent plus
        // a step is only for a body that was empty and has nothing to copy.
        std::string indent = (close > open + 1) ? indentOf(lines[open + 1])
            : indentOf(lines[open]) + "    ";
        const bool cr = endsWithCr(lines[open]);

        std::vector<std::string> replacement;
        std::string current;
        for (char ch : body_) {
            if (ch == '\n') { replacement.push_back(current); current.clear(); }
            else if (ch != '\r') current.push_back(ch);
        }
        if (!current.empty()) replacement.push_back(current);

        std::vector<std::string> rebuilt(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(open) + 1);
        for (auto& line : replacement) {
            // An empty line keeps no indentation: trailing whitespace on a blank line
            // is invisible and shows up as a diff nobody made.
            rebuilt.push_back(line.empty() ? withEnding({}, cr)
                : withEnding(indent + line, cr));
        }
        rebuilt.insert(rebuilt.end(), lines.begin() + static_cast<std::ptrdiff_t>(close), lines.end());

        replaceBody(*project_, section_, joinLines(rebuilt));
        applied_ = true;
        return core::ok();
    }

    core::Status SetActionBodyCommand::undo() {
        if (!applied_ || !project_ || section_ >= project_->sections.size()) return core::ok();
        replaceBody(*project_, section_, previousBody_);
        applied_ = false;
        return core::ok();
    }

} // namespace project