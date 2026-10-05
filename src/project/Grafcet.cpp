#include "Grafcet.hpp"

#include <algorithm>
#include <cctype>

namespace grafcet {

    using namespace domain;

    std::string_view codeName(ActionKind k) noexcept {
        switch (k) {
        case ActionKind::Continuous:      return "CONTINU";
        case ActionKind::Rising:          return "RISING";
        case ActionKind::DelayOn:         return "DELAYON";
        case ActionKind::PulseOn:         return "PULSEON";
        case ActionKind::LimitedOn:       return "LIMITEDON";
        case ActionKind::Falling:         return "FALLING";
        case ActionKind::PulseOff:        return "PULSEOFF";
        case ActionKind::LimitedOff:      return "LIMITEDOFF";
        case ActionKind::CondDelayEnd:    return "CONDDELAYEND";
        case ActionKind::CondDelayPulse:  return "CONDDELAYPULSE";
        case ActionKind::CondLimitedOn:   return "CONDLIMITEDON";
        case ActionKind::CondLimitedOnTp: return "CONDLIMITEDONTP";
        case ActionKind::Unknown:         break;
        }
        return "?";
    }

    std::string_view triggerText(ActionKind k) noexcept {
        switch (k) {
        case ActionKind::Continuous:      return "while the step is active";
        case ActionKind::Rising:          return "once, when the step activates";
        case ActionKind::DelayOn:         return "after a delay, then while the step stays active";
        case ActionKind::PulseOn:         return "one pulse, when the step activates";
        case ActionKind::LimitedOn:       return "when the step activates, for a limited time";
        case ActionKind::Falling:         return "once, when the step deactivates";
        case ActionKind::PulseOff:        return "one pulse, when the step deactivates";
        case ActionKind::LimitedOff:      return "when the step deactivates, for a limited time";
        case ActionKind::CondDelayEnd:    return "on the condition, after a delay, until it clears";
        case ActionKind::CondDelayPulse:  return "on the condition, one pulse after a delay";
        case ActionKind::CondLimitedOn:   return "on the condition, for a limited time";
        case ActionKind::CondLimitedOnTp: return "on the condition, one limited pulse";
        case ActionKind::Unknown:         break;
        }
        return "unknown trigger";
    }

    bool usesDelay(ActionKind k) noexcept {
        switch (k) {
        case ActionKind::DelayOn:
        case ActionKind::LimitedOn:
        case ActionKind::LimitedOff:
        case ActionKind::CondDelayEnd:
        case ActionKind::CondDelayPulse:
        case ActionKind::CondLimitedOn:
        case ActionKind::CondLimitedOnTp: return true;
        default: return false;
        }
    }

    std::size_t Chart::countOf(Diagnostic::Level level) const {
        return static_cast<std::size_t>(
            std::count_if(warnings.begin(), warnings.end(),
                [&](const Diagnostic& d) { return d.level == level; }));
    }

    const Step* Chart::stepById(int id) const {
        for (const auto& s : steps) if (s.id == id) return &s;
        return nullptr;
    }

    std::vector<const Action*> Chart::actionsOfStep(int stepId) const {
        std::vector<const Action*> out;
        for (const auto& a : actions) if (a.boundStep == stepId) out.push_back(&a);
        return out;
    }

    // ---------------------------------------------------------------------------
    namespace {

        std::string trim(std::string_view s) {
            std::size_t b = 0, e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
            return std::string(s.substr(b, e - b));
        }

        bool startsWithNoCase(std::string_view hay, std::string_view needle) {
            if (hay.size() < needle.size()) return false;
            for (std::size_t i = 0; i < needle.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(hay[i]))
                    != std::tolower(static_cast<unsigned char>(needle[i]))) return false;
            return true;
        }

        // "id=2|s=1|d=2|c=PT1<=SP" -> the value for a key, or empty.
        //
        // EVERY field stops at the next '|', including the condition.
        //
        // The first version took the condition to the end of the descriptor, reasoning
        // that 'c=A|B' ought to be a boolean or. That was reasoning where reading was
        // called for: DFB BUILDING splits the string on '|' BEFORE it looks at any key,
        // so the PLC stores "A" and silently discards "B AND !C". A viewer that showed
        // the whole thing would be showing a condition the machine does not have - the
        // exact failure this program exists to avoid. A '|' is simply not expressible
        // in this format, and the warning below says so where one appears.
        std::string field(std::string_view descriptor, std::string_view key) {
            std::size_t at = 0;
            while (at < descriptor.size()) {
                const auto end = descriptor.find('|', at);
                const auto part = descriptor.substr(at, (end == std::string_view::npos ? descriptor.size() : end) - at);
                const auto eq = part.find('=');
                if (eq != std::string_view::npos && trim(part.substr(0, eq)) == key)
                    return trim(part.substr(eq + 1));
                if (end == std::string_view::npos) break;
                at = end + 1;
            }
            return {};
        }

        int toInt(const std::string& s, int fallback = -1) {
            if (s.empty()) return fallback;
            try { return std::stoi(s); }
            catch (...) { return fallback; }
        }

        // "0,1,2" -> {0,1,2}. A single value is the common case and a comma list is what
        // a join or a split looks like; the engine's Sources/Destinations arrays hold up
        // to three either way.
        std::vector<int> toIntList(const std::string& s) {
            std::vector<int> out;
            std::size_t at = 0;
            while (at <= s.size()) {
                const auto comma = s.find(',', at);
                const auto part = trim(std::string_view(s).substr(
                    at, (comma == std::string::npos ? s.size() : comma) - at));
                if (!part.empty()) out.push_back(toInt(part));
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
            return out;
        }

        // The index in Steps_X[7] / Trans_X[7] / Acts_X[7], and the prefix X.
        struct Target { std::string prefix; int index{ -1 }; };

        Target parseTarget(std::string_view text) {
            Target t;
            const auto open = text.find('[');
            if (open == std::string_view::npos) return t;
            const auto close = text.find(']', open);
            if (close == std::string_view::npos) return t;
            t.index = toInt(trim(text.substr(open + 1, close - open - 1)));

            auto head = trim(text.substr(0, open));
            const auto underscore = head.find('_');
            if (underscore != std::string::npos) t.prefix = head.substr(underscore + 1);
            return t;
        }

        // The argument list of one Builder(...) call, split on top-level commas. The
        // split has to respect quotes and brackets: a delay may be
        // REAL_TO_TIME(INT_TO_REAL(config.DLDETOX)*1000.0), which has commas nowhere but
        // parentheses everywhere, and a Text may contain anything at all.
        std::vector<std::string> splitArguments(std::string_view args) {
            std::vector<std::string> out;
            int depth = 0;
            bool inQuote = false;
            std::size_t from = 0;
            for (std::size_t i = 0; i < args.size(); ++i) {
                const char c = args[i];
                if (c == '\'') inQuote = !inQuote;
                else if (!inQuote && (c == '(' || c == '[')) ++depth;
                else if (!inQuote && (c == ')' || c == ']')) --depth;
                else if (!inQuote && depth == 0 && c == ',') {
                    out.push_back(trim(args.substr(from, i - from)));
                    from = i + 1;
                }
            }
            out.push_back(trim(args.substr(from)));
            return out;
        }

        std::string argumentValue(const std::vector<std::string>& args, std::string_view name) {
            for (const auto& a : args) {
                const auto at = a.find(":=");
                if (at == std::string::npos) continue;
                if (trim(std::string_view(a).substr(0, at)) != name) continue;
                return trim(std::string_view(a).substr(at + 2));
            }
            return {};
        }

        std::string unquote(std::string_view s) {
            if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'')
                return std::string(s.substr(1, s.size() - 2));
            return std::string(s);
        }

        // Statements, one per line, with the line number kept: a warning that cannot say
        // where it applies makes the reader search 217 lines by hand.
        struct Line { std::string text; std::size_t number; };

        std::vector<Line> linesOf(const std::string& body) {
            std::vector<Line> out;
            std::size_t from = 0, number = 1;
            while (from <= body.size()) {
                auto end = body.find('\n', from);
                if (end == std::string::npos) end = body.size();
                auto text = body.substr(from, end - from);
                if (!text.empty() && text.back() == '\r') text.pop_back();
                out.push_back({ std::move(text), number++ });
                if (end == body.size()) break;
                from = end + 1;
            }
            return out;
        }

        // "Trans_DetoxalA[2].Condition := <expr>;" -> index 2, expr. Returns false when
        // the line is not one of these.
        bool assignment(const std::string& line, std::string_view arrayPrefixHint,
            std::string_view suffix, int& index, std::string& expr) {
            const auto dot = line.find(suffix);
            if (dot == std::string::npos) return false;
            const auto assign = line.find(":=", dot);
            if (assign == std::string::npos) return false;

            const auto target = parseTarget(std::string_view(line).substr(0, dot));
            if (target.index < 0) return false;
            if (!arrayPrefixHint.empty() && target.prefix != arrayPrefixHint) return false;

            index = target.index;
            expr = trim(std::string_view(line).substr(assign + 2));
            if (!expr.empty() && expr.back() == ';') expr.pop_back();
            expr = trim(expr);
            return true;
        }

    } // namespace

    // ---------------------------------------------------------------------------
    std::vector<std::string> splitLines(const std::string& body) {
        std::vector<std::string> out;
        std::size_t from = 0;
        while (from <= body.size()) {
            auto end = body.find('\n', from);
            if (end == std::string::npos) end = body.size();
            out.push_back(body.substr(from, end - from));   // \r kept on purpose
            if (end == body.size()) break;
            from = end + 1;
        }
        return out;
    }

    namespace {

        // One line, with comments and string literals blanked out so a keyword inside
        // either cannot be mistaken for code. (* ... *) can span lines, so the caller
        // carries the state.
        std::string strip(std::string_view line, bool& inComment) {
            std::string out;
            out.reserve(line.size());
            bool inString = false;
            for (std::size_t i = 0; i < line.size(); ++i) {
                if (inComment) {
                    if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == ')') { inComment = false; ++i; }
                    out.push_back(' ');
                    continue;
                }
                if (inString) {
                    if (line[i] == '\'') inString = false;
                    out.push_back(' ');
                    continue;
                }
                if (i + 1 < line.size() && line[i] == '(' && line[i + 1] == '*') {
                    inComment = true; ++i; out.append(2, ' ');
                    continue;
                }
                if (line[i] == '\'') { inString = true; out.push_back(' '); continue; }
                out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(line[i]))));
            }
            return out;
        }

        // A whole word, not a substring. This is what tells IF from ELSIF.
        bool hasWord(std::string_view text, std::string_view word) {
            std::size_t at = 0;
            while ((at = text.find(word, at)) != std::string_view::npos) {
                const bool leftOk = at == 0 || !(std::isalnum(static_cast<unsigned char>(text[at - 1]))
                    || text[at - 1] == '_');
                const std::size_t after = at + word.size();
                const bool rightOk = after >= text.size()
                    || !(std::isalnum(static_cast<unsigned char>(text[after]))
                        || text[after] == '_');
                if (leftOk && rightOk) return true;
                at += word.size();
            }
            return false;
        }

    } // namespace

    std::size_t blockEnd(const std::vector<std::string>& lines, std::size_t openLine) {
        if (openLine >= lines.size()) return std::string::npos;

        bool inComment = false;
        int depth = 0;
        for (std::size_t i = openLine; i < lines.size(); ++i) {
            const auto code = strip(lines[i], inComment);

            // Closers first: a line may hold both, as "IF a THEN b; END_IF;" does.
            if (hasWord(code, "END_IF") && depth > 0) {
                // An IF opened and closed on the same line nets out to nothing.
                if (!(hasWord(code, "IF") && i != openLine)) {
                    if (--depth == 0) return i;
                }
                continue;
            }
            // ELSIF is not a new block. hasWord is what makes that true.
            if (hasWord(code, "IF") && !hasWord(code, "ELSIF")) ++depth;
        }
        return std::string::npos;
    }

    // ---------------------------------------------------------------------------
    Chart parseChart(const Project& p, Index section) {
        Chart chart;
        if (section >= p.sections.size()) return chart;
        const auto& s = p.sections[section];
        chart.section = section;
        chart.owner = s.owner;
        // 1.10 : comme Runtime::prepare nomme les variables d'une unite
        if (s.owner != kNoIndex && s.owner < p.pous.size()
            && p.pous[s.owner].kind == domain::PouKind::ProgramUnit)
            chart.scope = std::string(p.strings.text(p.pous[s.owner].name)) + ".";

        const std::string sectionName(p.strings.text(s.name));
        chart.name = startsWithNoCase(sectionName, "SFC_") ? sectionName.substr(4) : sectionName;

        const auto lines = linesOf(s.body);

        // ---- pass 1: the Builder calls, which carry the structure --------------
        for (const auto& line : lines) {
            const auto call = line.text.find("Builder(");
            if (call == std::string::npos) continue;
            const auto open = call + 8;
            const auto close = line.text.rfind(')');
            if (close == std::string::npos || close <= open) continue;

            const auto args = splitArguments(std::string_view(line.text).substr(open, close - open));
            const int  type = toInt(argumentValue(args, "T"));
            const auto text = unquote(argumentValue(args, "Text"));
            const auto delay = argumentValue(args, "d");     // NOT the destination

            auto target = parseTarget(argumentValue(args, type == 1 ? "_Step"
                : type == 2 ? "_Trans" : "_Act"));
            if (chart.arrayPrefix.empty() && !target.prefix.empty())
                chart.arrayPrefix = target.prefix;

            if (type == 1 && !text.empty()) {
                Step st;
                st.id = toInt(field(text, "id"));
                st.name = field(text, "n");
                st.initial = field(text, "i") == "1";
                st.isFinal = field(text, "f") == "1";
                st.line = line.number;
                // ST_GC_Step.Name is string[4], and BUILDING only assigns when the
                // value fits: a longer name is DISCARDED, not truncated, so the PLC
                // ends up with an empty name where the descriptor has a long one.
                if (st.name.size() > 4)
                    chart.warnings.push_back(Diagnostic{ Diagnostic::Level::EngineLimit, "line " + std::to_string(line.number) + ": step name '"
                                             + st.name + "' is longer than the 4 characters the "
                                             "engine stores; the PLC will have no name for it" });
                chart.steps.push_back(std::move(st));
            }
            else if (type == 2) {
                const int id = text.empty() ? target.index : toInt(field(text, "id"));
                auto it = std::find_if(chart.transitions.begin(), chart.transitions.end(),
                    [&](const Transition& t) { return t.id == id; });
                if (it == chart.transitions.end()) {
                    chart.transitions.push_back(Transition{});
                    it = chart.transitions.end() - 1;
                    it->id = id;
                    it->line = line.number;
                }
                if (!text.empty()) {
                    it->sources = toIntList(field(text, "s"));
                    it->destinations = toIntList(field(text, "d"));
                    it->conditionText = field(text, "c");
                }
                if (!delay.empty()) it->delay = delay;

                // Same discard-rather-than-truncate rule: CondText is string[8].
                // Only when this call carried a Text: every transition gets a second
                // Builder call for its delay, and checking there reported each label
                // twice.
                if (!text.empty() && it->conditionText.size() > 8)
                    chart.warnings.push_back(Diagnostic{ Diagnostic::Level::EngineLimit, "line " + std::to_string(line.number)
                                             + ": condition label '" + it->conditionText
                                             + "' is longer than the 8 characters the engine stores" });
                // A '|' cannot survive the descriptor format at all.
                if (!text.empty() && field(text, "c").empty()
                    && text.find("|c=") != std::string::npos)
                    chart.warnings.push_back(Diagnostic{ Diagnostic::Level::EngineLimit, "line " + std::to_string(line.number)
                                             + ": the condition label is empty or was cut at a '|'" });
                // Sources and Destinations are ARRAY[0..2]. BUILDING writes past the
                // end without checking, so a fourth entry corrupts whatever follows.
                if (it->sources.size() > 3 || it->destinations.size() > 3)
                    chart.warnings.push_back(Diagnostic{ Diagnostic::Level::Structure, "transition " + std::to_string(it->id)
                                             + " has more than three sources or destinations; "
                                             "the engine's arrays hold three and it does not check" });
                // ONLY the simultaneous kind is affected, and the distinction had
                // to be corrected: an earlier version of this note said "divergence"
                // and was wrong about half the cases.
                //
                // An ALTERNATIVE (OR) divergence is several transitions leaving the
                // same step, each with ONE destination - the engine runs those
                // perfectly, and this project is full of them. A SIMULTANEOUS (AND)
                // divergence is one transition with several destinations, and that
                // is the one BUILDING cannot express: it hardcodes SplitKind := 0,
                // so the engine activates Destinations[0] and ignores the rest.
                if (it->destinations.size() > 1 && it->split == SplitKind::Single)
                    chart.warnings.push_back(Diagnostic{ Diagnostic::Level::Structure,
                        "transition " + std::to_string(it->id) + " is a simultaneous (AND) "
                        "divergence with " + std::to_string(it->destinations.size())
                        + " destinations, but Builder always writes SplitKind=0, so the engine "
                        "will activate only the first. An alternative (OR) divergence - one "
                        "transition per branch - runs correctly." });
            }
            else if (type == 3) {
                const int id = text.empty() ? target.index : toInt(field(text, "id"));
                auto it = std::find_if(chart.actions.begin(), chart.actions.end(),
                    [&](const Action& a) { return a.id == id; });
                if (it == chart.actions.end()) {
                    chart.actions.push_back(Action{});
                    it = chart.actions.end() - 1;
                    it->id = id;
                    it->line = line.number;
                }
                if (!text.empty()) {
                    it->name = field(text, "n");
                    const int k = toInt(field(text, "k"), 0);
                    it->kind = (k >= 0 && k <= 11) ? static_cast<ActionKind>(k)
                        : ActionKind::Unknown;
                    if (it->kind == ActionKind::Unknown)
                        chart.warnings.push_back(Diagnostic{ Diagnostic::Level::Structure, "line " + std::to_string(line.number)
                                                 + ": action kind " + field(text, "k")
                                                 + " is not one the engine implements" });
                    it->boundStep = toInt(field(text, "s"));
                    if (it->name.size() > 8)
                        chart.warnings.push_back(Diagnostic{ Diagnostic::Level::EngineLimit, "line " + std::to_string(line.number)
                                                 + ": action name '" + it->name
                                                 + "' is longer than the 8 characters the engine "
                                                 "stores" });
                }
                if (!delay.empty()) it->delay = delay;
            }
        }

        // ---- pass 2: the real expressions --------------------------------------
        for (const auto& line : lines) {
            int index = -1;
            std::string expr;
            if (assignment(line.text, chart.arrayPrefix, ".Condition", index, expr)) {
                auto it = std::find_if(chart.transitions.begin(), chart.transitions.end(),
                    [&](const Transition& t) { return t.id == index; });
                if (it != chart.transitions.end()) it->conditionExpr = std::move(expr);
            }
            else if (assignment(line.text, chart.arrayPrefix, ".EnableCond", index, expr)) {
                auto it = std::find_if(chart.actions.begin(), chart.actions.end(),
                    [&](const Action& a) { return a.id == index; });
                if (it != chart.actions.end()) it->enableExpr = std::move(expr);
            }
        }

        std::sort(chart.steps.begin(), chart.steps.end(),
            [](const Step& a, const Step& b) { return a.id < b.id; });
        std::sort(chart.transitions.begin(), chart.transitions.end(),
            [](const Transition& a, const Transition& b) { return a.id < b.id; });
        std::sort(chart.actions.begin(), chart.actions.end(),
            [](const Action& a, const Action& b) { return a.id < b.id; });

        // ---- what does not add up ----------------------------------------------
        // Recorded, never fixed and never hidden. A chart drawn without its dangling
        // transition looks correct, which is the one thing it must not do.
        for (std::size_t i = 1; i < chart.steps.size(); ++i)
            if (chart.steps[i].id == chart.steps[i - 1].id)
                chart.warnings.push_back(Diagnostic{ Diagnostic::Level::Structure, "two steps share id " + std::to_string(chart.steps[i].id) });

        if (std::none_of(chart.steps.begin(), chart.steps.end(),
            [](const Step& s2) { return s2.initial; }) && !chart.steps.empty())
            chart.warnings.push_back(Diagnostic{ Diagnostic::Level::Structure, "no initial step: this chart cannot start" });

        for (const auto& t : chart.transitions) {
            if (t.sources.empty() || t.destinations.empty()) {
                chart.warnings.push_back(Diagnostic{ Diagnostic::Level::Structure, "transition " + std::to_string(t.id)
                                         + " has no source or no destination" });
                continue;
            }
            for (int id : t.sources)
                if (!chart.stepById(id))
                    chart.warnings.push_back(Diagnostic{ Diagnostic::Level::Structure, "transition " + std::to_string(t.id)
                                             + " starts from step " + std::to_string(id)
                                             + ", which does not exist" });
            for (int id : t.destinations)
                if (!chart.stepById(id))
                    chart.warnings.push_back(Diagnostic{ Diagnostic::Level::Structure, "transition " + std::to_string(t.id)
                                             + " leads to step " + std::to_string(id)
                                             + ", which does not exist" });
            if (t.conditionExpr.empty())
                chart.warnings.push_back(Diagnostic{ Diagnostic::Level::Structure, "transition " + std::to_string(t.id)
                                         + " has no condition: it can never fire" });
        }
        for (const auto& a : chart.actions)
            if (a.boundStep >= 0 && !chart.stepById(a.boundStep))
                chart.warnings.push_back(Diagnostic{ Diagnostic::Level::Structure, "action " + std::to_string(a.id) + " ('" + a.name
                                         + "') belongs to step " + std::to_string(a.boundStep)
                                         + ", which does not exist" });
        return chart;
    }

    // ---------------------------------------------------------------------------
    std::vector<Chart> findCharts(const Project& p) {
        std::vector<Chart> out;

        for (Index i = 0; i < p.sections.size(); ++i) {
            const std::string name(p.strings.text(p.sections[i].name));
            if (!startsWithNoCase(name, "SFC_")) continue;
            // A cheap pre-filter, not a correctness guard: the `steps.empty()` test
            // below already rejects everything this rejects. It is here so a 674-line
            // SFC_DEBUG is not tokenised for nothing, and it is labelled as an
            // optimisation so nobody later writes a test for behaviour it does not
            // own. What actually tells SFC_DetoxalA from SFC_DetoxalA_Actions is
            // that only one of them declares steps - the "_Actions" suffix is not
            // reliable, since SFC_Helium_APA pairs with SFC_HeliumAPA_Actions.
            if (p.sections[i].body.find("Builder(") == std::string::npos) continue;

            auto chart = parseChart(p, i);
            if (chart.steps.empty()) continue;

            // The engine instance. The convention is Gc_<name>, and the convention
            // is NOT kept: section SFC_Helium_APA fills Steps_HeliumAPA and is run
            // by Gc_HeliumAPA, with the underscore in one name and not the others.
            // So the section name is tried, then the array prefix, which is the one
            // spelling the code itself has to agree on - the arrays and the engine
            // call sit in the same section and would not compile otherwise.
            for (const auto& candidate : { "Gc_" + chart.name, "Gc_" + chart.arrayPrefix })
                if (chart.instance.empty())
                    for (const auto& v : p.variables)
                        if (p.strings.text(v.name) == candidate) { chart.instance = candidate; break; }

            if (chart.instance.empty())
                chart.warnings.push_back(Diagnostic{ Diagnostic::Level::Structure, "no engine instance called Gc_" + chart.name
                                         + " or Gc_" + chart.arrayPrefix
                                         + "; live state will not be available" });

            out.push_back(std::move(chart));
        }

        // The action bodies live in a second section. WHICH one is not a guess.
        //
        // The first version took whichever section mentioned Acts_<prefix>[ most
        // often. That picked the right one here, and it is the wrong rule: two
        // program units that were copied from one another both mention the same
        // arrays, and "most often" would then silently attach one chart's bodies to
        // the other's actions. Nothing would look wrong.
        //
        // The rule, in order:
        //   1. named SFC_<chart>_Actions, in the SAME program unit as the chart;
        //   2. any SFC_..._Actions in the same unit that mentions these arrays;
        //   3. any section in the same unit that mentions them;
        //   4. nothing - and the chart says so rather than borrowing from elsewhere.
        //
        // Scope is what makes this safe: Steps_ManuA is declared inside
        // Logigrammes_A, so only sections of that unit can legitimately name it. A
        // chart declared globally accepts a global section for the same reason.
        for (auto& chart : out) {
            const std::string needle = "Acts_" + chart.arrayPrefix + "[";
            const std::string wanted = "SFC_" + chart.name + "_Actions";
            const Index owner = chart.section < p.sections.size() ? p.sections[chart.section].owner
                : kNoIndex;
            Index best = kNoIndex;
            int   bestRank = 0;
            std::size_t bestHits = 0;

            for (Index i = 0; i < p.sections.size(); ++i) {
                if (i == chart.section) continue;
                const auto& candidate = p.sections[i];
                if (candidate.owner != owner) continue;          // a different unit: not ours
                if (candidate.body.find("Builder(") != std::string::npos) continue;

                std::size_t hits = 0, at = 0;
                while ((at = candidate.body.find(needle, at)) != std::string::npos) {
                    ++hits;
                    at += needle.size();
                }
                const std::string name(p.strings.text(candidate.name));
                int rank = 0;
                if (name == wanted)                                     rank = 3;
                else if (hits > 0 && startsWithNoCase(name, "SFC_")
                    && name.find("_Actions") != std::string::npos) rank = 2;
                else if (hits > 0)                                      rank = 1;
                if (rank == 0) continue;

                if (rank > bestRank || (rank == bestRank && hits > bestHits)) {
                    best = i;
                    bestRank = rank;
                    bestHits = hits;
                }
            }
            chart.actionSection = best;

            if (best == kNoIndex) {
                chart.warnings.push_back(Diagnostic{ Diagnostic::Level::Structure,
                    "no " + wanted + " in the same program unit: the action bodies cannot be "
                    "found, and nothing else will be used in their place" });
                continue;
            }
            if (bestRank < 3)
                chart.warnings.push_back(Diagnostic{ Diagnostic::Level::EngineLimit,
                    "the action bodies were found in '"
                    + std::string(p.strings.text(p.sections[best].name))
                    + "' rather than in " + wanted });

            // Each body is the IF ... THEN ... END_IF guarded by the action's Out.
            const auto lines = linesOf(p.sections[chart.actionSection].body);
            for (std::size_t i = 0; i < lines.size(); ++i) {
                const auto at = lines[i].text.find(needle);
                if (at == std::string::npos) continue;
                if (lines[i].text.find(".Out") == std::string::npos) continue;

                const auto target = parseTarget(std::string_view(lines[i].text).substr(at));
                auto it = std::find_if(chart.actions.begin(), chart.actions.end(),
                    [&](const Action& a) { return a.id == target.index; });
                if (it == chart.actions.end()) continue;

                // The real scanner, not a substring count: see blockEnd().
                std::vector<std::string> raw;
                raw.reserve(lines.size());
                for (const auto& l : lines) raw.push_back(l.text);

                const auto close = blockEnd(raw, i);
                if (close == std::string::npos) {
                    chart.warnings.push_back(Diagnostic{ Diagnostic::Level::Structure,
                        "the IF block for action " + std::to_string(target.index)
                        + " is never closed; its body cannot be read" });
                    continue;
                }
                // The block's own indentation is removed; everything inside it is
                // kept. trim() on every line was wrong: it flattened a nested IF onto
                // the same column as the statement that guards it, and an editor
                // that shows code with its structure removed is worse than one that
                // shows none - it is confidently wrong about what runs when.
                //
                // What comes off is the SHALLOWEST indentation any line has, so the
                // block loses its own level and keeps the ones inside it.
                std::size_t common = std::string::npos;
                for (std::size_t j = i + 1; j < close; ++j) {
                    const auto& line = lines[j].text;
                    const auto firstReal = line.find_first_not_of(" \t\r");
                    if (firstReal == std::string::npos) continue;   // a blank line says nothing
                    common = std::min(common, firstReal);
                }
                if (common == std::string::npos) common = 0;

                std::string body;
                for (std::size_t j = i + 1; j < close; ++j) {
                    auto line = lines[j].text;
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    body += line.size() > common ? line.substr(common) : std::string{};
                    body.push_back('\n');
                }
                it->body = std::move(body);
            }
        }
        return out;
    }

    Chart findChart(const Project& p, Index section) {
        for (auto& chart : findCharts(p))
            if (chart.section == section) return chart;
        return Chart{};
    }


} // namespace grafcet