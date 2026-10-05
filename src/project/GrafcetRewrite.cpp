#include "GrafcetRewrite.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

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

        bool endsWithCr(std::string_view line) { return !line.empty() && line.back() == '\r'; }

        std::uint32_t countLines(const std::string& body) {
            if (body.empty()) return 0;
            return static_cast<std::uint32_t>(std::count(body.begin(), body.end(), '\n') + 1);
        }

        void putBody(Project& p, Index section, std::string body) {
            p.sections[section].body = std::move(body);
            p.sections[section].lineCount = countLines(p.sections[section].body);
        }

        // parseChart does not fill actionSection - only findCharts does, because the
        // link is made by looking at every section in the project. Anything that is
        // going to RENUMBER must have it: a chart passed to shiftIds without it leaves
        // the companion section untouched, and the action bodies end up attached to the
        // wrong actions. That is not hypothetical; it is what happened, and the step
        // tests could not see it because renumbering steps does not touch that section.
        grafcet::Chart linkedChart(const Project& p, Index section) {
            auto chart = grafcet::parseChart(p, section);
            for (const auto& c : grafcet::findCharts(p))
                if (c.section == section) { chart.actionSection = c.actionSection; break; }
            return chart;
        }

        const char* arrayNameOf(ChartVector which) {
            switch (which) {
            case ChartVector::Step:       return "Steps_";
            case ChartVector::Transition: return "Trans_";
            case ChartVector::Action:     return "Acts_";
            }
            return "";
        }

        // The T:= of the Builder call this line belongs to, or 0 when it is not one.
        // Everything below is driven by this rather than by the field names on their
        // own, because `s=` means a source step on a transition and a bound step on an
        // action, and `id=` means a step id on one call and a transition id on another.
        int builderType(std::string_view line) {
            const auto call = line.find("Builder(");
            if (call == std::string_view::npos) return 0;
            const auto at = line.find("T:=", call);
            if (at == std::string_view::npos) return 0;
            std::size_t i = at + 3;
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            return (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])))
                ? line[i] - '0' : 0;
        }

        // Rewrites one comma-separated list of integers in place, shifting the ones at
        // or above `from`.
        std::string shiftList(std::string_view list, int from, int delta) {
            std::string out;
            std::size_t at = 0;
            bool first = true;
            while (at <= list.size()) {
                const auto comma = list.find(',', at);
                const auto end = (comma == std::string_view::npos) ? list.size() : comma;
                auto part = list.substr(at, end - at);

                std::size_t b = 0, e = part.size();
                while (b < e && std::isspace(static_cast<unsigned char>(part[b]))) ++b;
                while (e > b && std::isspace(static_cast<unsigned char>(part[e - 1]))) --e;
                const auto lead = std::string(part.substr(0, b));
                const auto trail = std::string(part.substr(e));
                const auto digits = std::string(part.substr(b, e - b));

                if (!first) out.push_back(',');
                first = false;
                int value = -1;
                if (!digits.empty()
                    && std::all_of(digits.begin(), digits.end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; })) {
                    value = std::stoi(digits);
                    if (value >= from) value += delta;
                    out += lead + std::to_string(value) + trail;
                }
                else {
                    out += std::string(part);   // not a number: left exactly as it was
                }
                if (comma == std::string_view::npos) break;
                at = comma + 1;
            }
            return out;
        }

        // "…|s=1,2|…" -> the same with the list shifted. Fields stop at the next '|',
        // which is what the engine's own tokeniser does.
        std::string shiftDescriptorField(const std::string& text, std::string_view key,
            int from, int delta) {
            std::string out;
            std::size_t at = 0;
            while (at <= text.size()) {
                const auto bar = text.find('|', at);
                const auto end = (bar == std::string::npos) ? text.size() : bar;
                auto part = text.substr(at, end - at);

                const auto eq = part.find('=');
                if (eq != std::string::npos) {
                    auto name = part.substr(0, eq);
                    name.erase(std::remove_if(name.begin(), name.end(),
                        [](unsigned char c) { return std::isspace(c) != 0; }),
                        name.end());
                    if (name == key) part = part.substr(0, eq + 1) + shiftList(part.substr(eq + 1),
                        from, delta);
                }
                out += part;
                if (bar == std::string::npos) break;
                out.push_back('|');
                at = bar + 1;
            }
            return out;
        }

        // Replaces one field's value, keeping every other field exactly as it was -
        // including any this version does not recognise. Rebuilding the descriptor from
        // what we understand would be shorter and would silently drop the rest.
        std::string setDescriptorField(const std::string& text, std::string_view key,
            const std::string& value) {
            std::string out;
            std::size_t at = 0;
            bool replaced = false;
            while (at <= text.size()) {
                const auto bar = text.find('|', at);
                const auto end = (bar == std::string::npos) ? text.size() : bar;
                auto part = text.substr(at, end - at);

                const auto eq = part.find('=');
                if (eq != std::string::npos) {
                    auto name = part.substr(0, eq);
                    name.erase(std::remove_if(name.begin(), name.end(),
                        [](unsigned char c) { return std::isspace(c) != 0; }),
                        name.end());
                    if (name == key) { part = part.substr(0, eq + 1) + value; replaced = true; }
                }
                out += part;
                if (bar == std::string::npos) break;
                out.push_back('|');
                at = bar + 1;
            }
            // A descriptor that never had the field gets it appended, which is what a
            // transition built without one needs.
            if (!replaced) out += "|" + std::string(key) + "=" + value;
            return out;
        }

        // Steps_ManuA[7] -> Steps_ManuA[8]. Only the named array, only subscripts at or
        // above `from`.
        std::string shiftSubscripts(const std::string& line, const std::string& array,
            int from, int delta) {
            std::string out;
            std::size_t at = 0;
            while (true) {
                const auto found = line.find(array, at);
                if (found == std::string::npos) { out += line.substr(at); break; }
                const auto open = found + array.size();
                if (open >= line.size() || line[open] != '[') {
                    out += line.substr(at, open - at);
                    at = open;
                    continue;
                }
                const auto close = line.find(']', open);
                if (close == std::string::npos) { out += line.substr(at); break; }

                const auto digits = line.substr(open + 1, close - open - 1);
                out += line.substr(at, open + 1 - at);
                if (!digits.empty()
                    && std::all_of(digits.begin(), digits.end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; })) {
                    int value = std::stoi(digits);
                    if (value >= from) value += delta;
                    out += std::to_string(value);
                }
                else {
                    out += digits;
                }
                out += ']';
                at = close + 1;
            }
            return out;
        }

        // The quoted Text:='…' of a Builder call, rewritten through `transform`.
        std::string mapDescriptor(const std::string& line,
            const std::function<std::string(const std::string&)>& transform) {
            const auto key = line.find("Text");
            if (key == std::string::npos) return line;
            const auto open = line.find('\'', key);
            if (open == std::string::npos) return line;
            const auto close = line.find('\'', open + 1);
            if (close == std::string::npos) return line;
            return line.substr(0, open + 1) + transform(line.substr(open + 1, close - open - 1))
                + line.substr(close);
        }

        void shiftSection(Project& p, Index section, const grafcet::Chart& chart,
            ChartVector which, int from, int delta) {
            if (section >= p.sections.size()) return;
            auto lines = grafcet::splitLines(p.sections[section].body);
            const std::string array = std::string(arrayNameOf(which)) + chart.arrayPrefix;

            for (auto& line : lines) {
                const bool cr = endsWithCr(line);
                if (cr) line.pop_back();

                line = shiftSubscripts(line, array, from, delta);

                // Descriptor fields, chosen by which kind of call this is. This switch
                // IS the trap described in the header; everything else is bookkeeping.
                switch (builderType(line)) {
                case 1:   // a step: its own id
                    if (which == ChartVector::Step)
                        line = mapDescriptor(line, [&](const std::string& d) {
                        return shiftDescriptorField(d, "id", from, delta);
                            });
                    break;
                case 2:   // a transition: id is its own, s and d are step ids
                    line = mapDescriptor(line, [&](const std::string& d) {
                        std::string out = d;
                        if (which == ChartVector::Transition)
                            out = shiftDescriptorField(out, "id", from, delta);
                        if (which == ChartVector::Step) {
                            out = shiftDescriptorField(out, "s", from, delta);
                            out = shiftDescriptorField(out, "d", from, delta);
                        }
                        return out;
                        });
                    break;
                case 3:   // an action: id is its own, s is the step it belongs to
                    line = mapDescriptor(line, [&](const std::string& d) {
                        std::string out = d;
                        if (which == ChartVector::Action)
                            out = shiftDescriptorField(out, "id", from, delta);
                        if (which == ChartVector::Step)
                            out = shiftDescriptorField(out, "s", from, delta);
                        return out;
                        });
                    break;
                default: break;
                }
                if (cr) line.push_back('\r');
            }
            putBody(p, section, joinLines(lines));
        }

    } // namespace

    // ---------------------------------------------------------------------------
    void shiftIds(Project& p, const grafcet::Chart& chart, ChartVector which,
        int from, int delta) {
        if (delta == 0) return;
        shiftSection(p, chart.section, chart, which, from, delta);
        // The companion section holds Acts_<prefix>[i] and nothing else of ours, but
        // it is shifted through the same code so the two cannot drift apart.
        if (chart.actionSection != kNoIndex && chart.actionSection != chart.section)
            shiftSection(p, chart.actionSection, chart, which, from, delta);

        // 1.10 (R2, decision 11 bis) : LES AUTRES SECTIONS DE L'UNITE. SFC_DEBUG de
        // MAST.XPG lit Steps_DetoxalA[1].Active, Acts_PompageA[i]... ; ne decaler que
        // les deux sections du grafcet lui faisait lire l'element d'a cote, sans que
        // rien ne le montre. Ici seulement les indices du tableau : les appels
        // Builder d'une autre section (un autre grafcet de l'unite) ne sont pas les
        // notres, leurs s= et d= ne bougent pas.
        const std::string array = std::string(arrayNameOf(which)) + chart.arrayPrefix;
        for (Index i = 0; i < p.sections.size(); ++i) {
            if (i == chart.section || i == chart.actionSection) continue;
            if (p.sections[i].owner != chart.owner) continue;
            if (p.sections[i].body.find(array + "[") == std::string::npos) continue;
            auto lines = grafcet::splitLines(p.sections[i].body);
            bool changed = false;
            for (auto& line : lines) {
                if (line.find(array + "[") == std::string::npos) continue;
                const bool cr = endsWithCr(line);
                if (cr) line.pop_back();
                auto shifted = shiftSubscripts(line, array, from, delta);
                if (shifted != line) { line = std::move(shifted); changed = true; }
                if (cr) line.push_back('\r');
            }
            if (changed) putBody(p, i, joinLines(lines));
        }
    }

    namespace {
        // 1.10 (R2) : le texte d'avant des autres sections de l'unite qui nomment les
        // tableaux du grafcet (celles que shiftIds peut renumeroter), pour l'annulation.
        std::vector<std::pair<Index, std::string>> othersOf(const Project& p, const grafcet::Chart& chart) {
            std::vector<std::pair<Index, std::string>> out;
            if (chart.arrayPrefix.empty()) return out;
            for (Index i = 0; i < p.sections.size(); ++i) {
                if (i == chart.section || i == chart.actionSection) continue;
                if (p.sections[i].owner != chart.owner) continue;
                const auto& body = p.sections[i].body;
                for (const char* a : { "Steps_", "Trans_", "Acts_" })
                    if (body.find(std::string(a) + chart.arrayPrefix + "[") != std::string::npos) {
                        out.emplace_back(i, body);
                        break;
                    }
            }
            return out;
        }

        void restoreOthers(Project& p, const std::vector<std::pair<Index, std::string>>& others) {
            for (const auto& [i, body] : others)
                if (i < p.sections.size() && p.sections[i].body != body) putBody(p, i, body);
        }
    } // namespace


    // ---------------------------------------------------------------------------
    namespace {

        // Where a Builder call of this type with this index sits, and what it looks
        // like. Used to place a new declaration among its own kind rather than at the
        // end of the section, and to copy the shape of its neighbours.
        struct Slot { std::size_t at{ 0 }; std::string model; bool found{ false }; };

        Slot slotFor(const std::vector<std::string>& lines, int type, const std::string& array,
            int newId) {
            Slot slot;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (builderType(lines[i]) != type) continue;
                const std::string mark = array + "[";
                const auto open = lines[i].find(mark);
                if (open == std::string::npos) continue;
                const auto close = lines[i].find(']', open);
                if (close == std::string::npos) continue;
                int index = -1;
                try { index = std::stoi(lines[i].substr(open + mark.size(), close - open - mark.size())); }
                catch (...) { continue; }

                if (index < newId) { slot.at = i + 1; slot.model = lines[i]; slot.found = true; }
                else if (!slot.found) { slot.at = i; slot.model = lines[i]; slot.found = true; break; }
            }
            return slot;
        }

        std::string indentPrefix(std::string_view line) {
            std::size_t at = 0;
            while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
            return std::string(line.substr(0, at));
        }

        std::string idList(const std::vector<int>& ids) {
            std::string out;
            for (std::size_t i = 0; i < ids.size(); ++i) {
                if (i) out.push_back(',');
                out += std::to_string(ids[i]);
            }
            return out;
        }

    } // namespace

    InsertTransitionCommand::InsertTransitionCommand(ProjectPtr project, Index section, int newId,
        std::vector<int> sources,
        std::vector<int> destinations,
        std::string label, std::string expression)
        : project_(std::move(project)), section_(section), newId_(newId),
        sources_(std::move(sources)), destinations_(std::move(destinations)),
        label_(std::move(label)), expression_(std::move(expression)) {}

    std::string InsertTransitionCommand::label() const {
        return "insert transition T" + std::to_string(newId_);
    }

    core::Status InsertTransitionCommand::execute() {
        if (!project_ || section_ >= project_->sections.size())
            return core::fail(core::ErrorCode::OutOfRange, "that section no longer exists");

        const auto chart = linkedChart(*project_, section_);
        if (chart.arrayPrefix.empty())
            return core::fail(core::ErrorCode::IncompleteProject, "this section builds no chart");

        if (sources_.empty() || destinations_.empty())
            return core::fail(core::ErrorCode::InvalidArgument,
                "a transition goes from at least one step to at least one step");
        for (int id : sources_)
            if (!chart.stepById(id))
                return core::fail(core::ErrorCode::OutOfRange,
                    "X" + std::to_string(id) + " is not in this chart");
        for (int id : destinations_)
            if (!chart.stepById(id))
                return core::fail(core::ErrorCode::OutOfRange,
                    "X" + std::to_string(id) + " is not in this chart");

        // The engine's arrays hold three of each and BUILDING does not check.
        if (sources_.size() > 3 || destinations_.size() > 3)
            return core::fail(core::ErrorCode::InvalidArgument,
                "the engine holds at most three sources and three destinations");
        // CondText is string[8] and BUILDING discards rather than truncates.
        if (label_.size() > 8)
            return core::fail(core::ErrorCode::InvalidArgument,
                "a condition label is at most 8 characters: the engine stores "
                "string[8] and discards anything longer.");
        if (label_.find('|') != std::string::npos)
            return core::fail(core::ErrorCode::InvalidArgument,
                "'|' separates the descriptor's fields, so a label cannot contain one");

        const int capacity = chartCapacity(*project_, chart);
        if (capacity > 0 && static_cast<int>(chart.transitions.size()) >= capacity)
            return core::fail(core::ErrorCode::IncompleteProject,
                "this chart already has all " + std::to_string(capacity)
                + " of its transitions");
        if (newId_ < 0 || newId_ > static_cast<int>(chart.transitions.size()))
            return core::fail(core::ErrorCode::OutOfRange, "that index is outside this chart");

        previousChart_ = project_->sections[section_].body;
        previousOthers_ = othersOf(*project_, chart);
        shiftIds(*project_, chart, ChartVector::Transition, newId_, +1);

        auto lines = grafcet::splitLines(project_->sections[section_].body);
        const std::string array = "Trans_" + chart.arrayPrefix;
        const auto slot = slotFor(lines, 2, array, newId_);
        if (!slot.found) {
            putBody(*project_, section_, previousChart_);
            restoreOthers(*project_, previousOthers_);
            return core::fail(core::ErrorCode::IncompleteProject,
                "no transition declaration to insert beside");
        }

        const std::string indent = indentPrefix(slot.model);
        const bool cr = endsWithCr(slot.model);
        const std::string target = array + "[" + std::to_string(newId_) + "]";

        std::string descriptor = "id=" + std::to_string(newId_) + "|s=" + idList(sources_)
            + "|d=" + idList(destinations_);
        if (!label_.empty()) descriptor += "|c=" + label_;

        std::vector<std::string> added;
        auto push = [&](std::string text) { added.push_back(cr ? text + "\r" : text); };
        push(indent + "Builder(T:=2,Text:='" + descriptor + "', _Trans:=" + target + ");");
        // The delay call every other transition has, so this one looks like them and
        // its TON is initialised rather than left holding whatever was in the array.
        push(indent + "Builder(T:=2,d:=t#0s, _Trans:=" + target + ");");

        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(std::min(slot.at, lines.size())),
            added.begin(), added.end());

        // The condition, among the other conditions rather than beside the Builder
        // call: that block is written in transition order and stays readable only if
        // it keeps it.
        std::size_t conditionAt = lines.size();
        std::string conditionModel;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].find(array + "[") == std::string::npos) continue;
            if (lines[i].find(".Condition") == std::string::npos) continue;
            const auto open = lines[i].find(array + "[") + array.size() + 1;
            const auto close = lines[i].find(']', open);
            int index = -1;
            try { index = std::stoi(lines[i].substr(open, close - open)); }
            catch (...) { continue; }
            conditionModel = lines[i];
            if (index < newId_) conditionAt = i + 1;
            else { conditionAt = i; break; }
        }
        const std::string cIndent = conditionModel.empty() ? indent : indentPrefix(conditionModel);
        const bool cCr = conditionModel.empty() ? cr : endsWithCr(conditionModel);
        std::string condition = cIndent + target + ".Condition := "
            + (expression_.empty() ? "FALSE" : expression_) + ";";
        if (cCr) condition.push_back('\r');
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(std::min(conditionAt, lines.size())),
            std::move(condition));

        putBody(*project_, section_, joinLines(lines));
        applied_ = true;
        return core::ok();
    }

    core::Status InsertTransitionCommand::undo() {
        if (!applied_ || !project_ || section_ >= project_->sections.size()) return core::ok();
        putBody(*project_, section_, previousChart_);
        restoreOthers(*project_, previousOthers_);
        applied_ = false;
        return core::ok();
    }

    // ---------------------------------------------------------------------------
    InsertActionCommand::InsertActionCommand(ProjectPtr project, Index section, int newId,
        std::string name, grafcet::ActionKind kind,
        int boundStep, std::string body, std::string delay)
        : project_(std::move(project)), section_(section), newId_(newId), name_(std::move(name)),
        kind_(kind), boundStep_(boundStep), body_(std::move(body)), delay_(std::move(delay)) {}

    std::string InsertActionCommand::label() const {
        return "insert action A" + std::to_string(newId_)
            + (name_.empty() ? std::string{} : " '" + name_ + "'");
    }

    core::Status InsertActionCommand::execute() {
        if (!project_ || section_ >= project_->sections.size())
            return core::fail(core::ErrorCode::OutOfRange, "that section no longer exists");

        const auto chart = linkedChart(*project_, section_);
        if (chart.arrayPrefix.empty())
            return core::fail(core::ErrorCode::IncompleteProject, "this section builds no chart");
        if (!chart.stepById(boundStep_))
            return core::fail(core::ErrorCode::OutOfRange,
                "X" + std::to_string(boundStep_) + " is not in this chart");
        if (name_.size() > 8)
            return core::fail(core::ErrorCode::InvalidArgument,
                "an action name is at most 8 characters: the engine stores "
                "string[8] and discards anything longer.");

        const int capacity = chartCapacity(*project_, chart);
        if (capacity > 0 && static_cast<int>(chart.actions.size()) >= capacity)
            return core::fail(core::ErrorCode::IncompleteProject,
                "this chart already has all " + std::to_string(capacity)
                + " of its actions");
        if (newId_ < 0 || newId_ > static_cast<int>(chart.actions.size()))
            return core::fail(core::ErrorCode::OutOfRange, "that index is outside this chart");

        // The body belongs in SFC_<name>_Actions, and nowhere else. Without one there
        // is no place for it, and writing it into the chart section would put
        // statements among the declarations.
        const Index actionSection = chart.actionSection;
        if (actionSection == kNoIndex || actionSection >= project_->sections.size())
            return core::fail(core::ErrorCode::IncompleteProject,
                "this chart has no companion actions section; create "
                "SFC_" + chart.name + "_Actions first");

        previousChart_ = project_->sections[section_].body;
        actionSection_ = actionSection;
        previousActions_ = project_->sections[actionSection_].body;
        previousOthers_ = othersOf(*project_, chart);

        shiftIds(*project_, chart, ChartVector::Action, newId_, +1);

        // ---- the declaration ---------------------------------------------------
        auto lines = grafcet::splitLines(project_->sections[section_].body);
        const std::string array = "Acts_" + chart.arrayPrefix;
        const auto slot = slotFor(lines, 3, array, newId_);
        if (!slot.found) {
            putBody(*project_, section_, previousChart_);
            putBody(*project_, actionSection_, previousActions_);
            restoreOthers(*project_, previousOthers_);
            return core::fail(core::ErrorCode::IncompleteProject,
                "no action declaration to insert beside");
        }
        const std::string indent = indentPrefix(slot.model);
        const bool cr = endsWithCr(slot.model);
        const std::string target = array + "[" + std::to_string(newId_) + "]";

        std::string descriptor = "id=" + std::to_string(newId_);
        if (!name_.empty()) descriptor += "|n=" + name_;
        descriptor += "|k=" + std::to_string(static_cast<int>(kind_))
            + "|s=" + std::to_string(boundStep_);

        std::vector<std::string> added;
        auto push = [&](std::string text) { added.push_back(cr ? text + "\r" : text); };
        push(indent + "Builder(T:=3,Text:='" + descriptor + "', _Act:=" + target + ");");
        if (grafcet::usesDelay(kind_))
            push(indent + "Builder(T:=3,d:=" + (delay_.empty() ? "t#0s" : delay_)
                + ", _Act:=" + target + ");");
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(std::min(slot.at, lines.size())),
            added.begin(), added.end());
        putBody(*project_, section_, joinLines(lines));

        // ---- the body, with the two comment lines ------------------------------
        auto actionLines = grafcet::splitLines(project_->sections[actionSection_].body);

        std::size_t at = actionLines.size();
        std::string model;
        for (std::size_t i = 0; i < actionLines.size(); ++i) {
            const auto open = actionLines[i].find(array + "[");
            if (open == std::string::npos) continue;
            if (actionLines[i].find(".Out") == std::string::npos) continue;
            const auto from = open + array.size() + 1;
            const auto close = actionLines[i].find(']', from);
            int index = -1;
            try { index = std::stoi(actionLines[i].substr(from, close - from)); }
            catch (...) { continue; }
            model = actionLines[i];
            if (index < newId_) {
                const auto end = grafcet::blockEnd(actionLines, i);
                at = (end == std::string::npos) ? i + 1 : end + 1;
            }
            else { at = i; break; }
        }
        const std::string bIndent = model.empty() ? std::string{} : indentPrefix(model);
        const bool bCr = model.empty() ? cr : endsWithCr(model);

        const auto* step = chart.stepById(boundStep_);
        const std::string stepName = (step && !step->name.empty()) ? step->name
            : "X" + std::to_string(boundStep_);

        std::vector<std::string> block;
        auto add = [&](std::string text) { block.push_back(bCr ? text + "\r" : text); };
        add({});
        // The two lines asked for, and the reason they earn their place: neither the
        // descriptor nor the IF says which step this belongs to or when it fires.
        add(bIndent + "(* Step X" + std::to_string(boundStep_) + " '" + stepName + "' *)");
        add(bIndent + "(* " + std::string(grafcet::codeName(kind_)) + " - "
            + std::string(grafcet::triggerText(kind_))
            + (grafcet::usesDelay(kind_) && !delay_.empty() ? ", " + delay_ : std::string{}) + " *)");
        add(bIndent + "IF " + target + ".Out THEN");
        if (body_.empty()) {
            add(bIndent + "    ;");
        }
        else {
            std::string current;
            for (char ch : body_) {
                if (ch == '\n') { add(bIndent + "    " + current); current.clear(); }
                else if (ch != '\r') current.push_back(ch);
            }
            if (!current.empty()) add(bIndent + "    " + current);
        }
        add(bIndent + "END_IF;");

        actionLines.insert(actionLines.begin() + static_cast<std::ptrdiff_t>(std::min(at, actionLines.size())),
            block.begin(), block.end());
        putBody(*project_, actionSection_, joinLines(actionLines));

        applied_ = true;
        return core::ok();
    }

    core::Status InsertActionCommand::undo() {
        if (!applied_ || !project_) return core::ok();
        if (section_ < project_->sections.size()) putBody(*project_, section_, previousChart_);
        if (actionSection_ != kNoIndex && actionSection_ < project_->sections.size())
            putBody(*project_, actionSection_, previousActions_);
        restoreOthers(*project_, previousOthers_);
        applied_ = false;
        return core::ok();
    }

    // ---------------------------------------------------------------------------

    // ---------------------------------------------------------------------------
    SetTransitionEndpointsCommand::SetTransitionEndpointsCommand(
        ProjectPtr project, Index section, int transitionId,
        std::vector<int> sources, std::vector<int> destinations)
        : project_(std::move(project)), section_(section), transitionId_(transitionId),
        sources_(std::move(sources)), destinations_(std::move(destinations)) {}

    std::string SetTransitionEndpointsCommand::label() const {
        return "re-point transition T" + std::to_string(transitionId_);
    }

    core::Status SetTransitionEndpointsCommand::execute() {
        if (!project_ || section_ >= project_->sections.size())
            return core::fail(core::ErrorCode::OutOfRange, "that section no longer exists");

        const auto chart = linkedChart(*project_, section_);
        if (sources_.empty() || destinations_.empty())
            return core::fail(core::ErrorCode::InvalidArgument,
                "a transition with no source or no destination never fires, and "
                "nothing about the chart says so");
        if (sources_.size() > 3 || destinations_.size() > 3)
            return core::fail(core::ErrorCode::InvalidArgument,
                "the engine holds at most three of each");
        for (int id : sources_)
            if (!chart.stepById(id))
                return core::fail(core::ErrorCode::OutOfRange,
                    "X" + std::to_string(id) + " is not in this chart");
        for (int id : destinations_)
            if (!chart.stepById(id))
                return core::fail(core::ErrorCode::OutOfRange,
                    "X" + std::to_string(id) + " is not in this chart");

        auto lines = grafcet::splitLines(project_->sections[section_].body);
        const std::string array = "Trans_" + chart.arrayPrefix;
        const std::string mark = array + "[" + std::to_string(transitionId_) + "]";

        bool found = false;
        for (auto& line : lines) {
            if (builderType(line) != 2) continue;
            if (line.find(mark) == std::string::npos) continue;
            if (line.find("Text") == std::string::npos) continue;   // the delay call has none

            const bool cr = endsWithCr(line);
            if (cr) line.pop_back();
            // Only the two fields are rewritten. Rebuilding the descriptor would be
            // shorter and would drop any field this version does not know about.
            line = mapDescriptor(line, [&](const std::string& descriptor) {
                std::string out = setDescriptorField(descriptor, "s", idList(sources_));
                return setDescriptorField(out, "d", idList(destinations_));
                });
            if (cr) line.push_back('\r');
            found = true;
            break;
        }
        if (!found)
            return core::fail(core::ErrorCode::IncompleteProject,
                "T" + std::to_string(transitionId_) + " has no descriptor here");

        previousChart_ = project_->sections[section_].body;
        putBody(*project_, section_, joinLines(lines));
        applied_ = true;
        return core::ok();
    }

    core::Status SetTransitionEndpointsCommand::undo() {
        if (!applied_ || !project_ || section_ >= project_->sections.size()) return core::ok();
        putBody(*project_, section_, previousChart_);
        applied_ = false;
        return core::ok();
    }

    // ---------------------------------------------------------------------------

    // ---------------------------------------------------------------------------
    RenameStepCommand::RenameStepCommand(ProjectPtr project, Index section, int stepId,
        std::string name, bool initial, bool isFinal)
        : project_(std::move(project)), section_(section), stepId_(stepId),
        name_(std::move(name)), initial_(initial), final_(isFinal) {}

    std::string RenameStepCommand::label() const {
        return "rename step X" + std::to_string(stepId_)
            + (name_.empty() ? std::string{} : " to " + name_);
    }

    core::Status RenameStepCommand::execute() {
        if (!project_ || section_ >= project_->sections.size())
            return core::fail(core::ErrorCode::OutOfRange, "that section no longer exists");

        const auto chart = linkedChart(*project_, section_);
        if (!chart.stepById(stepId_))
            return core::fail(core::ErrorCode::OutOfRange,
                "X" + std::to_string(stepId_) + " is not in this chart");
        if (name_.size() > 4)
            return core::fail(core::ErrorCode::InvalidArgument,
                "a step name is at most 4 characters: the engine stores string[4] "
                "and discards anything longer, so a longer name would leave the PLC "
                "with no name at all.");
        if (name_.find('|') != std::string::npos || name_.find('\'') != std::string::npos)
            return core::fail(core::ErrorCode::InvalidArgument,
                "'|' separates the descriptor's fields and '\\'' ends it");

        // Only one initial step. Two would leave the engine to pick, and which one it
        // picks is not something a drawing can show.
        if (initial_)
            for (const auto& other : chart.steps)
                if (other.initial && other.id != stepId_)
                    return core::fail(core::ErrorCode::IncompleteProject,
                        "X" + std::to_string(other.id)
                        + " is already the initial step; clear it first");

        auto lines = grafcet::splitLines(project_->sections[section_].body);
        const std::string mark = "Steps_" + chart.arrayPrefix + "["
            + std::to_string(stepId_) + "]";
        bool found = false;
        for (auto& line : lines) {
            if (builderType(line) != 1) continue;
            if (line.find(mark) == std::string::npos) continue;

            const bool cr = endsWithCr(line);
            if (cr) line.pop_back();
            line = mapDescriptor(line, [&](const std::string& descriptor) {
                // Field by field, so anything this version does not recognise stays.
                std::string out = setDescriptorField(descriptor, "n", name_);
                out = setDescriptorField(out, "i", initial_ ? "1" : "0");
                out = setDescriptorField(out, "f", final_ ? "1" : "0");
                return out;
                });
            if (cr) line.push_back('\r');
            found = true;
            break;
        }
        if (!found)
            return core::fail(core::ErrorCode::IncompleteProject,
                "X" + std::to_string(stepId_) + " has no descriptor here");

        previousChart_ = project_->sections[section_].body;
        putBody(*project_, section_, joinLines(lines));
        applied_ = true;
        return core::ok();
    }

    core::Status RenameStepCommand::undo() {
        if (!applied_ || !project_ || section_ >= project_->sections.size()) return core::ok();
        putBody(*project_, section_, previousChart_);
        applied_ = false;
        return core::ok();
    }

    // ---------------------------------------------------------------------------
    int chartCapacity(const Project& p, const grafcet::Chart& chart) {
        // Read from the declaration, not assumed. A chart whose arrays were widened
        // by hand must be allowed to use the room.
        const std::string wanted = "Steps_" + chart.arrayPrefix;
        for (const auto& v : p.variables) {
            if (p.strings.text(v.name) != wanted) continue;
            const std::string type(p.strings.text(v.type.name));
            const auto dots = type.find("..");
            const auto close = type.find(']', dots == std::string::npos ? 0 : dots);
            if (dots == std::string::npos || close == std::string::npos) return 0;
            try { return std::stoi(type.substr(dots + 2, close - dots - 2)) + 1; }
            catch (...) { return 0; }
        }
        return 0;
    }

    // ---------------------------------------------------------------------------
    InsertStepCommand::InsertStepCommand(ProjectPtr project, Index section, int newId,
        std::string name, bool initial, bool isFinal)
        : project_(std::move(project)), section_(section), newId_(newId),
        name_(std::move(name)), initial_(initial), final_(isFinal) {}

    std::string InsertStepCommand::label() const {
        return "insert step X" + std::to_string(newId_)
            + (name_.empty() ? std::string{} : " '" + name_ + "'");
    }

    core::Status InsertStepCommand::execute() {
        if (!project_ || section_ >= project_->sections.size())
            return core::fail(core::ErrorCode::OutOfRange, "that section no longer exists");

        const auto chart = linkedChart(*project_, section_);
        if (chart.arrayPrefix.empty())
            return core::fail(core::ErrorCode::IncompleteProject, "this section builds no chart");

        // ST_GC_Step.Name is string[4] and BUILDING discards anything longer rather
        // than truncating, so a long name would leave the PLC with none at all.
        if (name_.size() > 4)
            return core::fail(core::ErrorCode::InvalidArgument,
                "a step name is at most 4 characters: the engine stores string[4] "
                "and discards anything longer, leaving the PLC with no name.");

        const int capacity = chartCapacity(*project_, chart);
        if (capacity > 0 && static_cast<int>(chart.steps.size()) >= capacity)
            return core::fail(core::ErrorCode::IncompleteProject,
                "this chart already has all " + std::to_string(capacity)
                + " of its steps. Widening it means changing "
                "Steps_/Trans_/Acts_ AND the engine's InOut parameter types, "
                "which changes DFB_GRAFCETENGINE's signature for all of its "
                "instances - a change to the automation code, not to this tool.");

        if (newId_ < 0 || newId_ > static_cast<int>(chart.steps.size()))
            return core::fail(core::ErrorCode::OutOfRange,
                "X" + std::to_string(newId_) + " is outside this chart");

        previousChart_ = project_->sections[section_].body;
        actionSection_ = chart.actionSection;
        if (actionSection_ != kNoIndex && actionSection_ < project_->sections.size())
            previousActions_ = project_->sections[actionSection_].body;
        previousOthers_ = othersOf(*project_, chart);

        // Make room first, then write the declaration into it. Doing it the other
        // way round would shift the line just written.
        shiftIds(*project_, chart, ChartVector::Step, newId_, +1);

        auto lines = grafcet::splitLines(project_->sections[section_].body);

        // After the last step declaration below the new id, so the Builder calls
        // stay in the order a reader expects to find them.
        std::size_t at = 0;
        bool found = false;
        std::string model;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (builderType(lines[i]) != 1) continue;
            const std::string mark = "Steps_" + chart.arrayPrefix + "[";
            const auto open = lines[i].find(mark);
            if (open == std::string::npos) continue;
            const auto close = lines[i].find(']', open);
            if (close == std::string::npos) continue;
            int index = -1;
            try {
                index = std::stoi(lines[i].substr(open + mark.size(),
                    close - open - mark.size()));
            }
            catch (...) { continue; }
            if (index < newId_) { at = i + 1; found = true; model = lines[i]; }
            else if (!found) { at = i; model = lines[i]; found = true; break; }
        }
        if (!found) {
            // 1.10 (R2) : rien ne reste decale d'une insertion qui n'a pas eu lieu.
            putBody(*project_, section_, previousChart_);
            if (actionSection_ != kNoIndex && actionSection_ < project_->sections.size())
                putBody(*project_, actionSection_, previousActions_);
            restoreOthers(*project_, previousOthers_);
            return core::fail(core::ErrorCode::IncompleteProject,
                "no step declaration to insert beside");
        }

        // The new line copies the shape of its neighbours: same indentation, same
        // line ending. A tool-written line that looks different from the ones around
        // it is a tool-written line people distrust.
        std::size_t indentEnd = 0;
        while (indentEnd < model.size()
            && (model[indentEnd] == ' ' || model[indentEnd] == '\t')) ++indentEnd;
        std::string descriptor = "id=" + std::to_string(newId_);
        if (!name_.empty()) descriptor += "|n=" + name_;
        if (initial_) descriptor += "|i=1";
        if (final_)   descriptor += "|f=1";

        std::string line = model.substr(0, indentEnd) + "Builder(T:=1,Text:='" + descriptor
            + "', _Step:=Steps_" + chart.arrayPrefix + "["
            + std::to_string(newId_) + "]);";
        if (endsWithCr(model)) line.push_back('\r');
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(std::min(at, lines.size())),
            std::move(line));

        putBody(*project_, section_, joinLines(lines));
        applied_ = true;
        return core::ok();
    }

    core::Status InsertStepCommand::undo() {
        if (!applied_ || !project_) return core::ok();
        if (section_ < project_->sections.size()) putBody(*project_, section_, previousChart_);
        if (actionSection_ != kNoIndex && actionSection_ < project_->sections.size())
            putBody(*project_, actionSection_, previousActions_);
        restoreOthers(*project_, previousOthers_);
        applied_ = false;
        return core::ok();
    }

    // ---------------------------------------------------------------------------
    RemoveStepCommand::RemoveStepCommand(ProjectPtr project, Index section, int stepId,
        bool renumber)
        : project_(std::move(project)), section_(section), stepId_(stepId), renumber_(renumber) {}

    std::string RemoveStepCommand::label() const {
        return "delete step X" + std::to_string(stepId_)
            + (renumber_ ? " and renumber" : " and leave the gap");
    }

    std::vector<std::string> RemoveStepCommand::orphaned() const {
        std::vector<std::string> out;
        if (!project_ || section_ >= project_->sections.size()) return out;
        const auto chart = grafcet::parseChart(*project_, section_);

        for (const auto& t : chart.transitions) {
            const bool fromIt = std::find(t.sources.begin(), t.sources.end(), stepId_)
                != t.sources.end();
            const bool toIt = std::find(t.destinations.begin(), t.destinations.end(), stepId_)
                != t.destinations.end();
            if (fromIt || toIt)
                out.push_back("transition T" + std::to_string(t.id) + " ("
                    + (t.conditionText.empty() ? "no label" : t.conditionText)
                    + ") " + (fromIt ? "starts here" : "leads here"));
        }
        for (const auto& a : chart.actions)
            if (a.boundStep == stepId_)
                out.push_back("action A" + std::to_string(a.id) + " ('" + a.name
                    + "') belongs to this step");
        return out;
    }

    core::Status RemoveStepCommand::execute() {
        if (!project_ || section_ >= project_->sections.size())
            return core::fail(core::ErrorCode::OutOfRange, "that section no longer exists");

        const auto chart = linkedChart(*project_, section_);
        if (!chart.stepById(stepId_))
            return core::fail(core::ErrorCode::OutOfRange,
                "X" + std::to_string(stepId_) + " is not in this chart");

        previousChart_ = project_->sections[section_].body;
        actionSection_ = chart.actionSection;
        if (actionSection_ != kNoIndex && actionSection_ < project_->sections.size())
            previousActions_ = project_->sections[actionSection_].body;
        previousOthers_ = othersOf(*project_, chart);

        // The declaration goes; whether the ones above it move is the reader's call.
        auto lines = grafcet::splitLines(project_->sections[section_].body);
        const std::string mark = "Steps_" + chart.arrayPrefix + "["
            + std::to_string(stepId_) + "]";
        lines.erase(std::remove_if(lines.begin(), lines.end(),
            [&](const std::string& l) {
                return builderType(l) == 1
                    && l.find(mark) != std::string::npos;
            }),
            lines.end());
        putBody(*project_, section_, joinLines(lines));

        if (renumber_) {
            const auto after = linkedChart(*project_, section_);
            shiftIds(*project_, after, ChartVector::Step, stepId_ + 1, -1);
        }
        applied_ = true;
        return core::ok();
    }

    core::Status RemoveStepCommand::undo() {
        if (!applied_ || !project_) return core::ok();
        if (section_ < project_->sections.size()) putBody(*project_, section_, previousChart_);
        if (actionSection_ != kNoIndex && actionSection_ < project_->sections.size())
            putBody(*project_, actionSection_, previousActions_);
        restoreOthers(*project_, previousOthers_);
        applied_ = false;
        return core::ok();
    }

} // namespace project