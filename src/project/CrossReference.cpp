#include "CrossReference.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace project {

    using namespace domain;

    namespace {

        char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

        bool isWordChar(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
        }

        std::string trim(std::string_view s) {
            std::size_t b = 0, e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
            return std::string(s.substr(b, e - b));
        }

        // The line with comments and string literals blanked out, so a name inside
        // either cannot be mistaken for a use of it. (* ... *) spans lines, so the state
        // is carried by the caller.
        std::string codeOnly(std::string_view line, bool& inComment) {
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
                    inComment = true;
                    ++i;
                    out.append(2, ' ');
                    continue;
                }
                if (line[i] == '\'') { inString = true; out.push_back(' '); continue; }
                out.push_back(line[i]);
            }
            return out;
        }

        // Every whole-token occurrence of `name`, with the dotted path it starts.
        // "gStatus.temp.value" found while looking for gStatus yields that whole path:
        // what a reader wants to see is which MEMBER was touched, not that the root was.
        struct Hit {
            std::size_t at{ 0 };
            std::string path;
        };

        std::vector<Hit> findTokens(const std::string& code, const std::string& lowerName) {
            std::vector<Hit> hits;
            std::string folded;
            folded.reserve(code.size());
            for (char c : code) folded.push_back(lower(c));

            std::size_t from = 0;
            while ((from = folded.find(lowerName, from)) != std::string::npos) {
                const bool leftOk = from == 0 || !isWordChar(code[from - 1]);
                const auto after = from + lowerName.size();
                const bool rightOk = after >= code.size() || !isWordChar(code[after]);
                if (leftOk && rightOk) {
                    // Take the dotted path, and any [index] along it.
                    std::size_t end = after;
                    while (end < code.size()) {
                        if (code[end] == '.' && end + 1 < code.size() && isWordChar(code[end + 1])) {
                            ++end;
                            while (end < code.size() && isWordChar(code[end])) ++end;
                        }
                        else if (code[end] == '[') {
                            const auto close = code.find(']', end);
                            if (close == std::string::npos) break;
                            end = close + 1;
                        }
                        else {
                            break;
                        }
                    }
                    hits.push_back(Hit{ from, code.substr(from, end - from) });
                    from = end;
                }
                else {
                    from = after;
                }
            }
            return hits;
        }

        // Le nom occupe-t-il toute l'instruction ? "Init_ES;" oui, "x := Init_ES;" non,
        // "IF Init_ES THEN" non plus. C'est ce qui distingue un appel de sous-routine
        // d'un usage du meme identifiant.
        bool isBareCall(const std::string& code, const Hit& hit) {
            std::size_t b = 0;
            while (b < hit.at && std::isspace(static_cast<unsigned char>(code[b]))) ++b;
            if (b != hit.at) return false;

            std::size_t e = hit.at + hit.path.size();
            while (e < code.size() && std::isspace(static_cast<unsigned char>(code[e]))) ++e;
            // Un point-virgule, et plus rien d'autre que des blancs apres.
            if (e >= code.size() || code[e] != ';') return false;
            for (std::size_t k = e + 1; k < code.size(); ++k)
                if (!std::isspace(static_cast<unsigned char>(code[k]))) return false;
            // Un chemin pointe ou indice n'est pas un appel de SR.
            return hit.path.find('.') == std::string::npos
                && hit.path.find('[') == std::string::npos;
        }

        // Where the statement's ':=' is, ignoring the ones inside parentheses - those
        // belong to a call's named arguments, not to this statement.
        std::size_t assignmentAt(const std::string& code) {
            int depth = 0;
            for (std::size_t i = 0; i + 1 < code.size(); ++i) {
                if (code[i] == '(') ++depth;
                else if (code[i] == ')') --depth;
                else if (depth == 0 && code[i] == ':' && code[i + 1] == '=') return i;
            }
            return std::string::npos;
        }

        bool startsWithKeyword(const std::string& code, const char* word) {
            const auto text = trim(code);
            const auto n = std::char_traits<char>::length(word);
            if (text.size() < n) return false;
            for (std::size_t i = 0; i < n; ++i)
                if (lower(text[i]) != lower(word[i])) return false;
            return text.size() == n || !isWordChar(text[n]);
        }

    } // namespace

    std::size_t CrossReference::sectionCount() const {
        std::set<domain::Index> seen;
        for (const auto& r : writes) seen.insert(r.section);
        for (const auto& r : reads) seen.insert(r.section);
        for (const auto& r : calls) seen.insert(r.section);
        return seen.size();
    }

    // ---------------------------------------------------------------------------
    CrossReference crossReference(const Project& p, std::string_view name) {
        CrossReference out;
        out.name = std::string(name);
        if (name.empty()) return out;

        std::string lowerName;
        for (char c : name) lowerName.push_back(lower(c));

        for (Index s = 0; s < p.sections.size(); ++s) {
            const auto& section = p.sections[s];
            if (section.body.find_first_not_of(" \t\r\n") == std::string::npos) continue;

            std::string ownerName;
            if (section.owner != kNoIndex && section.owner < p.pous.size())
                ownerName = p.strings.text(p.pous[section.owner].name);
            const std::string sectionName(p.strings.text(section.name));

            bool inComment = false;
            // The innermost IF still open. Not a full parse: the condition a
            // reference sits under is what a reviewer scans for, and "the IF above
            // it" is that in every case that matters.
            std::vector<std::string> conditions;

            std::size_t lineNumber = 0, from = 0;
            while (from <= section.body.size()) {
                auto end = section.body.find('\n', from);
                if (end == std::string::npos) end = section.body.size();
                const auto raw = section.body.substr(from, end - from);
                ++lineNumber;
                from = end + 1;

                const auto code = codeOnly(raw, inComment);
                if (trim(code).empty()) continue;

                if (startsWithKeyword(code, "END_IF")) {
                    if (!conditions.empty()) conditions.pop_back();
                }

                const auto hits = findTokens(code, lowerName);
                if (!hits.empty()) {
                    const auto assign = assignmentAt(code);
                    for (const auto& hit : hits) {
                        Reference ref;
                        ref.section = s;
                        ref.sectionName = sectionName;
                        ref.ownerName = ownerName;
                        ref.line = lineNumber;
                        ref.path = hit.path;
                        ref.text = trim(code);
                        ref.condition = conditions.empty() ? std::string{} : conditions.back();

                        // Le nom SEUL, en instruction: c'est un appel de sous-routine.
                        // Teste avant tout le reste, parce qu'un appel n'est ni une
                        // lecture ni une ecriture et qu'il ne doit pas etre range
                        // dans l'un des deux faute de mieux.
                        if (assign == std::string::npos && isBareCall(code, hit)) {
                            ref.kind = Reference::Kind::Call;
                            out.calls.push_back(std::move(ref));
                        }
                        else if (assign != std::string::npos && hit.at < assign) {
                            ref.kind = Reference::Kind::Write;
                            out.writes.push_back(std::move(ref));
                        }
                        else {
                            // Inside a call's parentheses: passed to a block. Read,
                            // unless the parameter is InOut - which this cannot know
                            // without resolving the call, so it is labelled rather
                            // than guessed at.
                            const auto open = code.find('(');
                            ref.kind = (open != std::string::npos && hit.at > open)
                                ? Reference::Kind::CallArgument
                                : Reference::Kind::Read;
                            out.reads.push_back(std::move(ref));
                        }
                    }
                }

                if (startsWithKeyword(code, "IF") || startsWithKeyword(code, "ELSIF")) {
                    if (startsWithKeyword(code, "ELSIF") && !conditions.empty()) conditions.pop_back();
                    auto text = trim(code);
                    const auto then = text.size();
                    (void)then;
                    // Keep the condition itself, without the IF and the THEN.
                    const auto start = text.find_first_of(" \t");
                    if (start != std::string::npos) text = trim(text.substr(start));
                    const auto lowerText = [&] {
                        std::string t;
                        for (char c : text) t.push_back(lower(c));
                        return t;
                        }();
                    const auto thenAt = lowerText.rfind("then");
                    if (thenAt != std::string::npos) text = trim(text.substr(0, thenAt));
                    conditions.push_back(text);
                }
            }
        }
        return out;
    }

    // ---------------------------------------------------------------------------
    namespace {

        void expandInto(const Project& p, StructureNode& node, Index derived,
            std::size_t depth, std::size_t maxDepth, std::set<Index>& onPath) {
            node.isStruct = true;
            if (depth >= maxDepth || derived >= p.derivedTypes.size()) {
                node.truncated = true;
                return;
            }
            // A type that contains itself would be followed until the stack ran out.
            // Control Expert forbids it; a file on disk is not obliged to be valid.
            if (!onPath.insert(derived).second) {
                node.truncated = true;
                return;
            }

            for (Index field : p.derivedTypes[derived].fields) {
                if (field >= p.variables.size()) continue;
                const auto& v = p.variables[field];

                StructureNode child;
                child.name = p.strings.text(v.name);
                child.path = node.path + "." + child.name;
                child.type = p.strings.text(v.type.name);
                child.comment = v.comment != kNoIndex ? std::string(p.strings.text(v.comment))
                    : std::string{};
                child.depth = depth + 1;

                if (v.type.derivedIndex != kNoIndex)
                    expandInto(p, child, v.type.derivedIndex, depth + 1, maxDepth, onPath);

                node.children.push_back(std::move(child));
            }
            onPath.erase(derived);
        }

    } // namespace

    StructureNode expandStructure(const Project& p, Index variable, std::size_t maxDepth) {
        StructureNode root;
        if (variable >= p.variables.size()) return root;

        const auto& v = p.variables[variable];
        root.name = p.strings.text(v.name);
        root.path = root.name;
        root.type = p.strings.text(v.type.name);
        root.comment = v.comment != kNoIndex ? std::string(p.strings.text(v.comment))
            : std::string{};

        if (v.type.derivedIndex != kNoIndex) {
            std::set<Index> onPath;
            expandInto(p, root, v.type.derivedIndex, 0, maxDepth, onPath);
        }
        return root;
    }

} // namespace project