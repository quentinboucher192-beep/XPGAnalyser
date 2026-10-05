#include "GrafcetRewrite2.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>

// Textes de l'ecran en francais : echappements UTF-8 (sources en ASCII).
//   \xC3\xA9 e accent aigu, \xC3\xA8 e grave, \xC3\xA0 a grave, \xC3\xAA e circonflexe,
//   \xE2\x80\xA6 points de suspension.

namespace project {

    using namespace domain;

    namespace {

        using Bodies = std::vector<std::pair<Index, std::string>>;

        // ---- le texte, ligne a ligne (comme GrafcetRewrite.cpp) -----------------
        std::string joinLines(const std::vector<std::string>& lines) {
            std::string out;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (i) out.push_back('\n');
                out += lines[i];
            }
            return out;
        }

        bool endsWithCr(std::string_view line) { return !line.empty() && line.back() == '\r'; }

        std::string noCr(std::string line) {
            if (endsWithCr(line)) line.pop_back();
            return line;
        }

        std::uint32_t countLines(const std::string& body) {
            if (body.empty()) return 0;
            return static_cast<std::uint32_t>(std::count(body.begin(), body.end(), '\n') + 1);
        }

        void putBody(Project& p, Index section, std::string body) {
            p.sections[section].body = std::move(body);
            p.sections[section].lineCount = countLines(p.sections[section].body);
        }

        std::string trimmed(std::string_view s) {
            std::size_t b = 0, e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
            return std::string(s.substr(b, e - b));
        }

        std::string indentPrefix(std::string_view line) {
            std::size_t at = 0;
            while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
            return std::string(line.substr(0, at));
        }

        bool isIdentChar(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
        }

        // Des chiffres, sans exception (std::stoi en leve une).
        bool parseIndex(std::string_view digits, int& out) {
            if (digits.empty() || digits.size() > 6) return false;
            int v = 0;
            for (char c : digits) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
                v = v * 10 + (c - '0');
            }
            out = v;
            return true;
        }

        // Le T:= de l'appel Builder de la ligne, 0 si ce n'en est pas un. Tout est
        // decide par lui, jamais par le nom d'un champ seul (voir GrafcetRewrite.hpp).
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

        // Chaque Acts_DetoxalA[<k>] de la ligne (le nom entier : pas MyActs_DetoxalA[).
        template <class Fn>
        void forEachSubscript(std::string_view line, const std::string& array, Fn&& fn) {
            std::size_t at = 0;
            while (true) {
                const auto found = line.find(array, at);
                if (found == std::string_view::npos) return;
                const auto open = found + array.size();
                at = open;
                if (found > 0 && isIdentChar(line[found - 1])) continue;
                if (open >= line.size() || line[open] != '[') continue;
                const auto close = line.find(']', open);
                if (close == std::string_view::npos) return;
                int index = -1;
                if (parseIndex(trimmed(line.substr(open + 1, close - open - 1)), index))
                    fn(found, open, close, index);
                at = close + 1;
            }
        }

        bool mentions(std::string_view line, const std::string& array, int index) {
            bool hit = false;
            forEachSubscript(line, array, [&](std::size_t, std::size_t, std::size_t, int k) {
                if (k == index) hit = true;
                });
            return hit;
        }

        bool mentionsAny(std::string_view line, const std::string& array) {
            bool hit = false;
            forEachSubscript(line, array, [&](std::size_t, std::size_t, std::size_t, int) { hit = true; });
            return hit;
        }

        // Acts_X[7] -> Acts_X[6] : seulement ce tableau, seulement les indices >= from.
        std::string shiftSubscripts(const std::string& line, const std::string& array,
            int from, int delta) {
            std::string out;
            std::size_t copied = 0;
            forEachSubscript(line, array, [&](std::size_t, std::size_t open, std::size_t close, int k) {
                if (k < from) return;
                out += line.substr(copied, open + 1 - copied);
                out += std::to_string(k + delta);
                copied = close;
                });
            out += line.substr(copied);
            return out;
        }

        // Le Text:='...' d'un appel Builder, reecrit par `transform`.
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

        std::string fieldName(std::string part, std::size_t eq) {
            auto name = part.substr(0, eq);
            name.erase(std::remove_if(name.begin(), name.end(),
                [](unsigned char c) { return std::isspace(c) != 0; }), name.end());
            return name;
        }

        // Un champ du descripteur, avec sa nouvelle valeur ; les autres champs restent
        // tels quels, y compris ceux que cette version ne connait pas.
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
                if (eq != std::string::npos && fieldName(part, eq) == key) {
                    part = part.substr(0, eq + 1) + value;
                    replaced = true;
                }
                out += part;
                if (bar == std::string::npos) break;
                out.push_back('|');
                at = bar + 1;
            }
            if (!replaced) out += "|" + std::string(key) + "=" + value;
            return out;
        }

        // id=<k> du descripteur, decale si k >= from.
        std::string shiftIdField(const std::string& text, int from, int delta) {
            std::string out;
            std::size_t at = 0;
            while (at <= text.size()) {
                const auto bar = text.find('|', at);
                const auto end = (bar == std::string::npos) ? text.size() : bar;
                auto part = text.substr(at, end - at);
                const auto eq = part.find('=');
                int k = -1;
                if (eq != std::string::npos && fieldName(part, eq) == "id"
                    && parseIndex(trimmed(std::string_view(part).substr(eq + 1)), k) && k >= from) {
                    const auto value = part.substr(eq + 1);
                    const auto lead = value.substr(0, value.find_first_not_of(" \t"));
                    part = part.substr(0, eq + 1) + lead + std::to_string(k + delta);
                }
                out += part;
                if (bar == std::string::npos) break;
                out.push_back('|');
                at = bar + 1;
            }
            return out;
        }

        // La fin de l'instruction qui commence a la ligne `from` : la ligne qui porte
        // le ';'. Les receptivites du projet tiennent sur une ligne ; une qui en
        // prendrait plusieurs doit partir entiere, pas a moitie.
        std::size_t statementEnd(const std::vector<std::string>& lines, std::size_t from) {
            for (std::size_t i = from; i < lines.size(); ++i) {
                std::string_view l = lines[i];
                const auto comment = l.find("(*");
                if (l.substr(0, comment).find(';') != std::string_view::npos) return i;
            }
            return from;
        }

        // "Trans_X[3].Condition :=" ou "Acts_X[3].EnableCond :=" : l'indice, ou -1.
        int assignedIndex(std::string_view line, const std::string& array, std::string_view member) {
            int found = -1;
            forEachSubscript(line, array, [&](std::size_t, std::size_t, std::size_t close, int k) {
                if (found >= 0) return;
                std::size_t i = close + 1;
                while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
                if (line.substr(i, member.size()) != member) return;
                const auto rest = line.substr(i + member.size());
                const auto assign = rest.find(":=");
                if (assign == std::string_view::npos) return;
                if (!trimmed(rest.substr(0, assign)).empty()) return;
                found = k;
                });
            return found;
        }

        bool startsIf(std::string_view line) {
            const auto t = trimmed(line);
            return t.size() >= 3 && (t[0] == 'I' || t[0] == 'i') && (t[1] == 'F' || t[1] == 'f')
                && std::isspace(static_cast<unsigned char>(t[2]));
        }

        std::string sectionNameOf(const Project& p, Index s) {
            return s < p.sections.size() ? std::string(p.strings.text(p.sections[s].name)) : std::string{};
        }

        // Les sections ou les tableaux du grafcet sont visibles : celles de son unite.
        // Une section d'une AUTRE unite qui les nomme est signalee a part (refus) :
        // ce n'est pas a cet outil de deviner si c'est la meme variable.
        std::vector<Index> unitSections(const Project& p, const grafcet::Chart& chart) {
            std::vector<Index> out;
            for (Index i = 0; i < p.sections.size(); ++i)
                if (i == chart.section || i == chart.actionSection
                    || p.sections[i].owner == chart.owner)
                    out.push_back(i);
            return out;
        }

        std::vector<std::string> foreignUses(const Project& p, const grafcet::Chart& chart,
            const std::string& array) {
            std::vector<std::string> out;
            for (Index i = 0; i < p.sections.size(); ++i) {
                if (i == chart.section || i == chart.actionSection
                    || p.sections[i].owner == chart.owner) continue;
                const auto& body = p.sections[i].body;
                if (body.find(array) == std::string::npos) continue;
                const auto lines = grafcet::splitLines(body);
                for (std::size_t l = 0; l < lines.size(); ++l)
                    if (mentionsAny(lines[l], array)) {
                        out.push_back(sectionNameOf(p, i) + " (une autre unit\xC3\xA9), ligne "
                            + std::to_string(l + 1));
                        break;
                    }
            }
            return out;
        }

        std::string joinUses(const std::vector<std::string>& uses) {
            std::string out;
            const std::size_t shown = std::min<std::size_t>(uses.size(), 6);
            for (std::size_t i = 0; i < shown; ++i) {
                if (i) out += ", ";
                out += uses[i];
            }
            if (uses.size() > shown)
                out += " et " + std::to_string(uses.size() - shown) + " autre(s)";
            return out;
        }

        GrafcetPlan refuse(core::ErrorCode code, std::string message) {
            GrafcetPlan plan;
            plan.code = code;
            plan.error = std::move(message);
            return plan;
        }

        // Ce que le plan doit ecrire : seulement les sections dont le texte change.
        void keepChanged(const Project& p, GrafcetPlan& plan, Index s, std::string body) {
            if (s >= p.sections.size() || p.sections[s].body == body) return;
            plan.bodies.emplace_back(s, std::move(body));
        }

        // Le grafcet de la section, complet (corps des actions et section compagne).
        bool chartOf(const Project& p, Index section, grafcet::Chart& chart, GrafcetPlan& refusal) {
            if (section >= p.sections.size()) {
                refusal = refuse(core::ErrorCode::OutOfRange, "Cette section n'existe plus.");
                return false;
            }
            chart = grafcet::findChart(p, section);
            if (chart.arrayPrefix.empty() || chart.steps.empty()) {
                refusal = refuse(core::ErrorCode::IncompleteProject,
                    "Cette section ne construit pas de grafcet.");
                return false;
            }
            return true;
        }

        // Applique un plan en gardant le texte d'avant de chaque section touchee.
        core::Status applyPlan(Project& p, const GrafcetPlan& plan, Bodies& previous) {
            if (!plan.ok()) return core::fail(plan.code, plan.error);
            previous.clear();
            for (const auto& [s, body] : plan.bodies) {
                if (s >= p.sections.size()) continue;
                previous.emplace_back(s, p.sections[s].body);
                putBody(p, s, body);
            }
            return core::ok();
        }

        void restore(Project& p, Bodies& previous) {
            // A l'envers : si une section figurait deux fois, la plus ancienne gagne.
            for (auto it = previous.rbegin(); it != previous.rend(); ++it)
                if (it->first < p.sections.size()) putBody(p, it->first, it->second);
            previous.clear();
        }

        // Renumerote Trans_/Acts_ au-dessus de `removed` dans les sections de l'unite,
        // et le id= des descripteurs du type donne dans la section du grafcet.
        void renumberAbove(const Project& p, const grafcet::Chart& chart, const std::string& array,
            int builderKind, int removed, std::vector<std::pair<Index, std::vector<std::string>>>& work) {
            for (auto& [s, lines] : work) {
                (void)p;
                for (auto& line : lines) {
                    const bool cr = endsWithCr(line);
                    if (cr) line.pop_back();
                    line = shiftSubscripts(line, array, removed + 1, -1);
                    if (s == chart.section && builderType(line) == builderKind)
                        line = mapDescriptor(line, [&](const std::string& d) {
                        return shiftIdField(d, removed + 1, -1);
                            });
                    if (cr) line.push_back('\r');
                }
            }
        }

        std::vector<std::pair<Index, std::vector<std::string>>> loadUnit(const Project& p,
            const grafcet::Chart& chart, const std::string& array) {
            std::vector<std::pair<Index, std::vector<std::string>>> work;
            for (Index s : unitSections(p, chart))
                if (s == chart.section || s == chart.actionSection
                    || p.sections[s].body.find(array) != std::string::npos)
                    work.emplace_back(s, grafcet::splitLines(p.sections[s].body));
            return work;
        }

        std::vector<std::string>* linesOf(std::vector<std::pair<Index, std::vector<std::string>>>& work,
            Index s) {
            for (auto& [i, lines] : work)
                if (i == s) return &lines;
            return nullptr;
        }

        // Qui emploie array[index] hors des lignes `own` (par section) : "T2" pour une
        // receptivite du grafcet, "A4" pour une condition d'action, sinon section et ligne.
        std::vector<std::string> usesOf(const Project& p, const grafcet::Chart& chart,
            const std::vector<std::pair<Index, std::vector<std::string>>>& work,
            const std::string& array, int index,
            const std::function<bool(Index, std::size_t)>& isOwn) {
            std::vector<std::string> uses;
            std::set<std::string> seen;
            const std::string trans = "Trans_" + chart.arrayPrefix;
            const std::string acts = "Acts_" + chart.arrayPrefix;
            for (const auto& [s, lines] : work) {
                for (std::size_t l = 0; l < lines.size(); ++l) {
                    if (isOwn(s, l) || !mentions(lines[l], array, index)) continue;
                    std::string where;
                    if (s == chart.section) {
                        const int t = assignedIndex(lines[l], trans, ".Condition");
                        const int a = assignedIndex(lines[l], acts, ".EnableCond");
                        if (t >= 0) where = "la r\xC3\xA9" "ceptivit\xC3\xA9 de T" + std::to_string(t);
                        else if (a >= 0) where = "la condition de A" + std::to_string(a);
                    }
                    if (where.empty())
                        where = sectionNameOf(p, s) + ", ligne " + std::to_string(l + 1);
                    if (seen.insert(where).second) uses.push_back(where);
                }
            }
            return uses;
        }

        // Les deux commentaires qu'InsertActionCommand ecrit au-dessus d'un bloc :
        //   (* Step X1 'X1' *)
        //   (* LIMITEDON - <sens>[, <retard>] *)
        // Reconnus a leur forme exacte, pour ne jamais toucher un commentaire du projet.
        bool generatedComments(const std::vector<std::string>& lines, std::size_t ifLine,
            grafcet::ActionKind kind) {
            if (ifLine < 2) return false;
            const auto kindLine = trimmed(lines[ifLine - 1]);
            const auto stepLine = trimmed(lines[ifLine - 2]);
            const std::string kindHead = "(* " + std::string(grafcet::codeName(kind)) + " - ";
            return stepLine.rfind("(* Step X", 0) == 0 && kindLine.rfind(kindHead, 0) == 0
                && kindLine.size() >= 2 && kindLine.substr(kindLine.size() - 2) == "*)";
        }

        // La ligne du bloc IF Acts_X[i].Out THEN dans la section des actions.
        std::size_t findOutBlock(const std::vector<std::string>& lines, const std::string& array, int id) {
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (!startsIf(lines[i]) || !mentions(lines[i], array, id)) continue;
                const auto mark = array + "[" + std::to_string(id) + "]";
                const auto at = lines[i].find(mark);
                if (at == std::string::npos) continue;
                const auto rest = std::string_view(lines[i]).substr(at + mark.size());
                if (trimmed(rest).rfind(".Out", 0) == 0) return i;
            }
            return std::string::npos;
        }

        // ---- le retard : la valeur de l'argument d:= d'un appel Builder -----------
        // [debut, fin) de la valeur, sans les blancs autour ; false s'il n'y en a pas.
        bool delayArgument(std::string_view line, std::size_t& begin, std::size_t& end) {
            const auto call = line.find("Builder(");
            if (call == std::string_view::npos) return false;
            int depth = 0;
            bool inQuote = false;
            std::size_t argStart = call + 8;
            for (std::size_t i = call + 8; i <= line.size(); ++i) {
                const char c = i < line.size() ? line[i] : ',';
                if (c == '\'') { inQuote = !inQuote; continue; }
                if (inQuote) continue;
                if (c == '(' || c == '[') { ++depth; continue; }
                if ((c == ')' || c == ']') && depth > 0) { --depth; continue; }
                if ((c == ',' && depth == 0) || (c == ')' && depth == 0)) {
                    const auto arg = line.substr(argStart, i - argStart);
                    const auto assign = arg.find(":=");
                    if (assign != std::string_view::npos && trimmed(arg.substr(0, assign)) == "d") {
                        std::size_t b = argStart + assign + 2, e = i;
                        while (b < e && std::isspace(static_cast<unsigned char>(line[b]))) ++b;
                        while (e > b && std::isspace(static_cast<unsigned char>(line[e - 1]))) --e;
                        begin = b;
                        end = e;
                        return true;
                    }
                    if (c == ')') return false;
                    argStart = i + 1;
                }
            }
            return false;
        }

        // Un retard que l'appel Builder peut porter : pas de ';', pas de quote, des
        // parentheses equilibrees, pas de virgule hors parentheses.
        bool delayFits(std::string_view d) {
            int depth = 0;
            for (char c : d) {
                if (c == ';' || c == '\'' || c == '\n' || c == '\r') return false;
                if (c == '(') ++depth;
                else if (c == ')') { if (--depth < 0) return false; }
                else if (c == ',' && depth == 0) return false;
            }
            return depth == 0;
        }

        std::string actionTitle(const grafcet::Action& a) {
            return "A" + std::to_string(a.id) + (a.name.empty() ? std::string{} : " (" + a.name + ")");
        }

    } // namespace

    // =========================================================================
    //  Supprimer une transition
    // =========================================================================
    GrafcetPlan planRemoveTransition(const Project& p, Index section, int transitionId) {
        grafcet::Chart chart;
        GrafcetPlan refusal;
        if (!chartOf(p, section, chart, refusal)) return refusal;
        const auto t = std::find_if(chart.transitions.begin(), chart.transitions.end(),
            [&](const grafcet::Transition& x) { return x.id == transitionId; });
        if (t == chart.transitions.end())
            return refuse(core::ErrorCode::OutOfRange,
                "T" + std::to_string(transitionId) + " n'est pas dans ce grafcet.");

        const std::string array = "Trans_" + chart.arrayPrefix;
        const auto foreign = foreignUses(p, chart, array);
        if (!foreign.empty())
            return refuse(core::ErrorCode::IncompleteProject,
                "Tu ne peux pas supprimer T" + std::to_string(transitionId)
                + " ici : les transitions de ce grafcet sont nomm\xC3\xA9" "es aussi dans "
                + joinUses(foreign) + ", que la renum\xC3\xA9rotation ne suivrait pas.");

        auto work = loadUnit(p, chart, array);
        auto* chartLines = linesOf(work, chart.section);
        if (!chartLines)
            return refuse(core::ErrorCode::IncompleteProject, "Cette section ne construit pas de grafcet.");

        // Ses lignes : les appels Builder(T:=2) et l'instruction .Condition.
        std::set<std::size_t> own;
        for (std::size_t l = 0; l < chartLines->size(); ++l) {
            const auto& line = (*chartLines)[l];
            if (builderType(line) == 2 && mentions(line, array, transitionId)) own.insert(l);
            else if (assignedIndex(line, array, ".Condition") == transitionId) {
                const auto end = statementEnd(*chartLines, l);
                for (std::size_t k = l; k <= end; ++k) own.insert(k);
            }
        }
        if (own.empty())
            return refuse(core::ErrorCode::IncompleteProject,
                "T" + std::to_string(transitionId) + " n'a pas de ligne dans cette section.");

        const auto uses = usesOf(p, chart, work, array, transitionId,
            [&](Index s, std::size_t l) { return s == chart.section && own.count(l) != 0; });
        if (!uses.empty())
            return refuse(core::ErrorCode::InvalidArgument,
                "Tu ne peux pas supprimer T" + std::to_string(transitionId)
                + " : elle est employ\xC3\xA9" "e dans " + joinUses(uses)
                + ". Change d'abord ces lignes.");

        for (auto it = own.rbegin(); it != own.rend(); ++it)
            chartLines->erase(chartLines->begin() + static_cast<std::ptrdiff_t>(*it));
        renumberAbove(p, chart, array, 2, transitionId, work);

        GrafcetPlan plan;
        for (auto& [s, lines] : work) keepChanged(p, plan, s, joinLines(lines));
        return plan;
    }

    // =========================================================================
    //  Supprimer une action
    // =========================================================================
    namespace {
        struct ActionLines {
            std::set<std::size_t> chartOwn;     // Builder(T:=3) et .EnableCond
            std::set<std::size_t> bodyOwn;      // le bloc, ses commentaires, une ligne vide
            std::size_t           ifLine{ std::string::npos }, endLine{ std::string::npos };
        };

        bool collectActionLines(const grafcet::Chart& chart, const grafcet::Action& a,
            const std::string& array, const std::vector<std::string>& chartLines,
            const std::vector<std::string>* actionLines, ActionLines& out, GrafcetPlan& refusal) {
            for (std::size_t l = 0; l < chartLines.size(); ++l) {
                const auto& line = chartLines[l];
                if (builderType(line) == 3 && mentions(line, array, a.id)) out.chartOwn.insert(l);
                else if (assignedIndex(line, array, ".EnableCond") == a.id) {
                    const auto end = statementEnd(chartLines, l);
                    for (std::size_t k = l; k <= end; ++k) out.chartOwn.insert(k);
                }
            }
            if (out.chartOwn.empty()) {
                refusal = refuse(core::ErrorCode::IncompleteProject,
                    "A" + std::to_string(a.id) + " n'a pas de ligne dans cette section.");
                return false;
            }
            if (!actionLines) return true;   // pas de section des actions : pas de corps
            const auto& lines = *actionLines;
            out.ifLine = findOutBlock(lines, array, a.id);
            if (out.ifLine == std::string::npos) return true;
            out.endLine = grafcet::blockEnd(lines, out.ifLine);
            if (out.endLine == std::string::npos) {
                refusal = refuse(core::ErrorCode::IncompleteProject,
                    "Le bloc IF de A" + std::to_string(a.id) + " n'est jamais ferm\xC3\xA9 (END_IF) : "
                    "je ne peux pas le retirer sans risquer la suite de la section.");
                return false;
            }
            for (std::size_t k = out.ifLine; k <= out.endLine; ++k) out.bodyOwn.insert(k);
            // Les deux commentaires d'InsertActionCommand, et la ligne vide qu'il met
            // avant eux : inserer puis supprimer rend le texte d'avant.
            if (generatedComments(lines, out.ifLine, a.kind)) {
                out.bodyOwn.insert(out.ifLine - 1);
                out.bodyOwn.insert(out.ifLine - 2);
                if (out.ifLine >= 3 && trimmed(lines[out.ifLine - 3]).empty())
                    out.bodyOwn.insert(out.ifLine - 3);
            }
            else {
                // Sinon, pas deux lignes vides de suite : une ligne vide avant le bloc
                // (ou le debut) et une apres -> celle d'apres part avec lui.
                const std::size_t first = *out.bodyOwn.begin();
                const bool blankBefore = first == 0 || trimmed(lines[first - 1]).empty();
                const bool blankAfter = out.endLine + 1 < lines.size()
                    && trimmed(lines[out.endLine + 1]).empty();
                if (blankBefore && blankAfter) out.bodyOwn.insert(out.endLine + 1);
            }
            (void)chart;
            return true;
        }
    } // namespace

    GrafcetPlan planRemoveAction(const Project& p, Index section, int actionId) {
        grafcet::Chart chart;
        GrafcetPlan refusal;
        if (!chartOf(p, section, chart, refusal)) return refusal;
        const auto a = std::find_if(chart.actions.begin(), chart.actions.end(),
            [&](const grafcet::Action& x) { return x.id == actionId; });
        if (a == chart.actions.end())
            return refuse(core::ErrorCode::OutOfRange,
                "A" + std::to_string(actionId) + " n'est pas dans ce grafcet.");

        const std::string array = "Acts_" + chart.arrayPrefix;
        const auto foreign = foreignUses(p, chart, array);
        if (!foreign.empty())
            return refuse(core::ErrorCode::IncompleteProject,
                "Tu ne peux pas supprimer " + actionTitle(*a) + " ici : les actions de ce grafcet "
                "sont nomm\xC3\xA9" "es aussi dans " + joinUses(foreign)
                + ", que la renum\xC3\xA9rotation ne suivrait pas.");

        auto work = loadUnit(p, chart, array);
        auto* chartLines = linesOf(work, chart.section);
        auto* actionLines = chart.actionSection != kNoIndex ? linesOf(work, chart.actionSection) : nullptr;
        if (!chartLines)
            return refuse(core::ErrorCode::IncompleteProject, "Cette section ne construit pas de grafcet.");

        ActionLines own;
        if (!collectActionLines(chart, *a, array, *chartLines, actionLines, own, refusal)) return refusal;

        const auto uses = usesOf(p, chart, work, array, actionId, [&](Index s, std::size_t l) {
            if (s == chart.section) return own.chartOwn.count(l) != 0;
            if (s == chart.actionSection) return own.bodyOwn.count(l) != 0;
            return false;
            });
        if (!uses.empty())
            return refuse(core::ErrorCode::InvalidArgument,
                "Tu ne peux pas supprimer " + actionTitle(*a) + " : elle est employ\xC3\xA9" "e dans "
                + joinUses(uses) + " (FIN(A" + std::to_string(actionId) + "), Acts_"
                + chart.arrayPrefix + "[" + std::to_string(actionId) + "]\xE2\x80\xA6). "
                "Change d'abord ces lignes, puis supprime-la.");

        for (auto it = own.chartOwn.rbegin(); it != own.chartOwn.rend(); ++it)
            chartLines->erase(chartLines->begin() + static_cast<std::ptrdiff_t>(*it));
        if (actionLines)
            for (auto it = own.bodyOwn.rbegin(); it != own.bodyOwn.rend(); ++it)
                actionLines->erase(actionLines->begin() + static_cast<std::ptrdiff_t>(*it));
        renumberAbove(p, chart, array, 3, actionId, work);

        GrafcetPlan plan;
        for (auto& [s, lines] : work) keepChanged(p, plan, s, joinLines(lines));
        return plan;
    }

    // =========================================================================
    //  Modifier une action
    // =========================================================================
    GrafcetPlan planModifyAction(const Project& p, Index section, int actionId,
        const ActionChange& change) {
        grafcet::Chart chart;
        GrafcetPlan refusal;
        if (!chartOf(p, section, chart, refusal)) return refusal;
        const auto a = std::find_if(chart.actions.begin(), chart.actions.end(),
            [&](const grafcet::Action& x) { return x.id == actionId; });
        if (a == chart.actions.end())
            return refuse(core::ErrorCode::OutOfRange,
                "A" + std::to_string(actionId) + " n'est pas dans ce grafcet.");

        const bool nameChanged = change.name != a->name;
        const bool kindChanged = change.kind != a->kind;
        const bool stepChanged = change.boundStep != a->boundStep;
        const std::string delay = trimmed(change.delay);
        // Un retard vide (genre sans duree) : on garde la ligne qui est la.
        const bool delayChanged = !delay.empty() && delay != a->delay;

        // ---- ce que le moteur peut stocker ---------------------------------------
        // Seulement ce qui change : 17 actions de MAST.XPG ont deja un nom de plus de
        // 8 caracteres (avertissement du modele) ; refuser d'en changer le genre a
        // cause de ce nom-la serait refuser une modification qui ne le touche pas.
        if (nameChanged && change.name.size() > 8)
            return refuse(core::ErrorCode::InvalidArgument,
                "Un nom d'action a 8 caract\xC3\xA8res au plus : le moteur garde un string[8] "
                "et jette un nom plus long (l'automate n'aurait plus de nom du tout).");
        if (nameChanged && change.name.find_first_of("|'$;") != std::string::npos)
            return refuse(core::ErrorCode::InvalidArgument,
                "Le nom ne peut pas contenir | ' $ ou ; (ils coupent le descripteur).");
        const int k = static_cast<int>(change.kind);
        if (k < 0 || k > 11)
            return refuse(core::ErrorCode::InvalidArgument,
                "Ce genre d'action n'existe pas dans le moteur (K0 \xC3\xA0 K11).");
        if (!chart.stepById(change.boundStep))
            return refuse(core::ErrorCode::OutOfRange,
                "X" + std::to_string(change.boundStep) + " n'est pas une \xC3\xA9tape de ce grafcet.");
        // Un genre retarde / limite sans duree : on la demande (ActionChange::from porte
        // la duree actuelle, donc ne rien toucher ne declenche pas ce refus).
        if (grafcet::usesDelay(change.kind) && delay.empty())
            return refuse(core::ErrorCode::InvalidArgument,
                "Ce genre d'action a besoin d'une dur\xC3\xA9" "e : donne-la (par exemple t#5s).");
        if (delayChanged && !delayFits(delay))
            return refuse(core::ErrorCode::InvalidArgument,
                "Cette dur\xC3\xA9" "e ne tient pas dans l'appel Builder : pas de ; ni de ', "
                "des parenth\xC3\xA8ses \xC3\xA9quilibr\xC3\xA9" "es, pas de virgule hors parenth\xC3\xA8ses.");

        const std::string array = "Acts_" + chart.arrayPrefix;
        auto lines = grafcet::splitLines(p.sections[section].body);

        // ---- le descripteur --------------------------------------------------------
        std::size_t descriptor = std::string::npos;
        for (std::size_t l = 0; l < lines.size(); ++l)
            if (builderType(lines[l]) == 3 && mentions(lines[l], array, actionId)
                && lines[l].find("Text") != std::string::npos) { descriptor = l; break; }
        if (descriptor == std::string::npos)
            return refuse(core::ErrorCode::IncompleteProject,
                "A" + std::to_string(actionId) + " n'a pas de descripteur dans cette section.");

        if (nameChanged || kindChanged || stepChanged) {
            auto& line = lines[descriptor];
            const bool cr = endsWithCr(line);
            if (cr) line.pop_back();
            line = mapDescriptor(line, [&](const std::string& d) {
                std::string out = d;
                if (nameChanged) out = setDescriptorField(out, "n", change.name);
                if (kindChanged) out = setDescriptorField(out, "k", std::to_string(k));
                if (stepChanged) out = setDescriptorField(out, "s", std::to_string(change.boundStep));
                return out;
                });
            if (cr) line.push_back('\r');
        }

        // ---- le retard -------------------------------------------------------------
        if (delayChanged) {
            bool replaced = false;
            for (std::size_t l = 0; l < lines.size() && !replaced; ++l) {
                if (builderType(lines[l]) != 3 || !mentions(lines[l], array, actionId)) continue;
                std::size_t b = 0, e = 0;
                if (!delayArgument(lines[l], b, e)) continue;
                lines[l] = lines[l].substr(0, b) + delay + lines[l].substr(e);
                replaced = true;
            }
            if (!replaced) {
                // La ligne qu'InsertActionCommand ecrit, juste sous le descripteur.
                const auto& model = lines[descriptor];
                std::string added = indentPrefix(model) + "Builder(T:=3,d:=" + delay + ", _Act:="
                    + array + "[" + std::to_string(actionId) + "]);";
                if (endsWithCr(model)) added.push_back('\r');
                lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(descriptor + 1), std::move(added));
            }
        }

        GrafcetPlan plan;
        keepChanged(p, plan, section, joinLines(lines));

        // ---- les commentaires ecrits par InsertActionCommand, s'il y en a ----------
        if ((kindChanged || stepChanged || delayChanged || nameChanged)
            && chart.actionSection != kNoIndex && chart.actionSection < p.sections.size()) {
            auto body = grafcet::splitLines(p.sections[chart.actionSection].body);
            const auto ifLine = findOutBlock(body, array, actionId);
            if (ifLine != std::string::npos && generatedComments(body, ifLine, a->kind)) {
                const auto* step = chart.stepById(change.boundStep);
                const std::string stepName = (step && !step->name.empty()) ? step->name
                    : "X" + std::to_string(change.boundStep);
                const std::string effectiveDelay = delay.empty() ? a->delay : delay;
                const std::string stepText = "(* Step X" + std::to_string(change.boundStep) + " '"
                    + stepName + "' *)";
                const std::string kindText = "(* " + std::string(grafcet::codeName(change.kind)) + " - "
                    + std::string(grafcet::triggerText(change.kind))
                    + (grafcet::usesDelay(change.kind) && !effectiveDelay.empty()
                        ? ", " + effectiveDelay : std::string{}) + " *)";
                auto rewrite = [&](std::size_t l, const std::string& text) {
                    const bool cr = endsWithCr(body[l]);
                    body[l] = indentPrefix(body[l]) + text + (cr ? "\r" : "");
                    };
                rewrite(ifLine - 2, stepText);
                rewrite(ifLine - 1, kindText);
                keepChanged(p, plan, chart.actionSection, joinLines(body));
            }
        }
        return plan;
    }

    // =========================================================================
    //  L'apercu
    // =========================================================================
    std::vector<StLineChange> diffSectionText(Index section, const std::string& sectionName,
        const std::string& before, const std::string& after) {
        std::vector<StLineChange> out;
        if (before == after) return out;
        const auto a = grafcet::splitLines(before);
        const auto b = grafcet::splitLines(after);

        // Le debut et la fin communs ; la plus longue sous-suite commune au milieu.
        std::size_t head = 0;
        while (head < a.size() && head < b.size() && a[head] == b[head]) ++head;
        std::size_t tail = 0;
        while (tail < a.size() - head && tail < b.size() - head
            && a[a.size() - 1 - tail] == b[b.size() - 1 - tail]) ++tail;
        const std::size_t n = a.size() - head - tail, m = b.size() - head - tail;

        std::vector<std::uint32_t> lcs((n + 1) * (m + 1), 0);
        auto at = [&](std::size_t i, std::size_t j) -> std::uint32_t& { return lcs[i * (m + 1) + j]; };
        for (std::size_t i = n; i-- > 0;)
            for (std::size_t j = m; j-- > 0;)
                at(i, j) = (a[head + i] == b[head + j]) ? at(i + 1, j + 1) + 1
                : std::max(at(i + 1, j), at(i, j + 1));

        // Les retraits et ajouts entre deux lignes communes forment un morceau : ses
        // premieres lignes retirees et ajoutees vont par paires (lignes modifiees).
        std::vector<std::size_t> removed, added;
        auto flush = [&]() {
            const std::size_t pairs = std::min(removed.size(), added.size());
            for (std::size_t k = 0; k < std::max(removed.size(), added.size()); ++k) {
                StLineChange c;
                c.section = section;
                c.sectionName = sectionName;
                if (k < removed.size()) { c.lineBefore = removed[k] + 1; c.before = noCr(a[removed[k]]); }
                if (k < added.size()) { c.lineAfter = added[k] + 1; c.after = noCr(b[added[k]]); }
                (void)pairs;
                out.push_back(std::move(c));
            }
            removed.clear();
            added.clear();
            };
        std::size_t i = 0, j = 0;
        while (i < n || j < m) {
            if (i < n && j < m && a[head + i] == b[head + j]) { flush(); ++i; ++j; }
            else if (j < m && (i == n || at(i, j + 1) >= at(i + 1, j))) { added.push_back(head + j); ++j; }
            else { removed.push_back(head + i); ++i; }
        }
        flush();
        return out;
    }

    std::vector<StLineChange> previewPlan(const Project& p, const GrafcetPlan& plan) {
        std::vector<StLineChange> out;
        if (!plan.ok()) return out;
        for (const auto& [s, body] : plan.bodies) {
            if (s >= p.sections.size()) continue;
            auto part = diffSectionText(s, sectionNameOf(p, s), p.sections[s].body, body);
            out.insert(out.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
        }
        return out;
    }

    bool previewCommand(Project& p, core::ICommand& command, std::vector<StLineChange>& out,
        std::string& error) {
        out.clear();
        error.clear();
        std::vector<std::string> before;
        before.reserve(p.sections.size());
        for (const auto& s : p.sections) before.push_back(s.body);

        auto done = command.execute();
        if (!done) {
            error = done.error().context;
            for (Index s = 0; s < p.sections.size() && s < before.size(); ++s)
                if (p.sections[s].body != before[s]) putBody(p, s, before[s]);
            return false;
        }
        for (Index s = 0; s < p.sections.size() && s < before.size(); ++s)
            if (p.sections[s].body != before[s]) {
                auto part = diffSectionText(s, sectionNameOf(p, s), before[s], p.sections[s].body);
                out.insert(out.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
            }
        (void)command.undo();
        // Octet pour octet, quoi que la commande ait fait de son annulation.
        for (Index s = 0; s < p.sections.size() && s < before.size(); ++s)
            if (p.sections[s].body != before[s]) putBody(p, s, before[s]);
        return true;
    }

    // =========================================================================
    //  Les commandes
    // =========================================================================
    namespace {
        std::vector<StLineChange> previewOf(const ProjectPtr& project, const GrafcetPlan& plan,
            std::string* error) {
            if (error) error->clear();
            if (!project) {
                if (error) *error = "Le projet n'est plus l\xC3\xA0.";
                return {};
            }
            if (!plan.ok()) {
                if (error) *error = plan.error;
                return {};
            }
            return previewPlan(*project, plan);
        }
    } // namespace

    RemoveTransitionCommand::RemoveTransitionCommand(ProjectPtr project, Index section, int transitionId)
        : project_(std::move(project)), section_(section), transitionId_(transitionId) {}

    std::string RemoveTransitionCommand::label() const {
        return "supprimer la transition T" + std::to_string(transitionId_);
    }

    core::Status RemoveTransitionCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::OutOfRange, "Le projet n'est plus l\xC3\xA0.");
        const auto plan = planRemoveTransition(*project_, section_, transitionId_);
        auto status = applyPlan(*project_, plan, previous_);
        applied_ = status.has_value();
        return status;
    }

    core::Status RemoveTransitionCommand::undo() {
        if (!applied_ || !project_) return core::ok();
        restore(*project_, previous_);
        applied_ = false;
        return core::ok();
    }

    std::vector<StLineChange> RemoveTransitionCommand::preview(std::string* error) const {
        if (!project_) return previewOf(project_, GrafcetPlan{}, error);
        return previewOf(project_, planRemoveTransition(*project_, section_, transitionId_), error);
    }

    std::vector<std::string> RemoveTransitionCommand::consequences() const {
        std::vector<std::string> out;
        if (!project_ || section_ >= project_->sections.size()) return out;
        const auto chart = grafcet::parseChart(*project_, section_);
        const auto t = std::find_if(chart.transitions.begin(), chart.transitions.end(),
            [&](const grafcet::Transition& x) { return x.id == transitionId_; });
        if (t == chart.transitions.end()) return out;
        auto others = [&](int step, bool asSource) {
            int count = 0;
            for (const auto& o : chart.transitions) {
                if (o.id == transitionId_) continue;
                const auto& ends = asSource ? o.sources : o.destinations;
                if (std::find(ends.begin(), ends.end(), step) != ends.end()) ++count;
            }
            return count;
            };
        std::set<int> done;
        for (int s : t->sources) {
            const auto* step = chart.stepById(s);
            if (!step || step->isFinal || !done.insert(s).second) continue;
            if (others(s, true) == 0)
                out.push_back("X" + std::to_string(s) + " n'aura plus de transition de sortie.");
        }
        done.clear();
        for (int d : t->destinations) {
            const auto* step = chart.stepById(d);
            if (!step || step->initial || !done.insert(d).second) continue;
            if (others(d, false) == 0)
                out.push_back("X" + std::to_string(d) + " ne sera plus atteinte (plus aucune transition n'y m\xC3\xA8ne).");
        }
        return out;
    }

    // ---------------------------------------------------------------------------
    RemoveActionCommand::RemoveActionCommand(ProjectPtr project, Index section, int actionId)
        : project_(std::move(project)), section_(section), actionId_(actionId) {
        if (project_ && section_ < project_->sections.size()) {
            const auto chart = grafcet::parseChart(*project_, section_);
            for (const auto& a : chart.actions)
                if (a.id == actionId_) name_ = a.name;
        }
    }

    std::string RemoveActionCommand::label() const {
        return "supprimer l'action A" + std::to_string(actionId_)
            + (name_.empty() ? std::string{} : " (" + name_ + ")");
    }

    core::Status RemoveActionCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::OutOfRange, "Le projet n'est plus l\xC3\xA0.");
        const auto plan = planRemoveAction(*project_, section_, actionId_);
        auto status = applyPlan(*project_, plan, previous_);
        applied_ = status.has_value();
        return status;
    }

    core::Status RemoveActionCommand::undo() {
        if (!applied_ || !project_) return core::ok();
        restore(*project_, previous_);
        applied_ = false;
        return core::ok();
    }

    std::vector<StLineChange> RemoveActionCommand::preview(std::string* error) const {
        if (!project_) return previewOf(project_, GrafcetPlan{}, error);
        return previewOf(project_, planRemoveAction(*project_, section_, actionId_), error);
    }

    std::vector<std::string> RemoveActionCommand::usedBy() const {
        if (!project_ || section_ >= project_->sections.size()) return {};
        const auto chart = grafcet::findChart(*project_, section_);
        const auto a = std::find_if(chart.actions.begin(), chart.actions.end(),
            [&](const grafcet::Action& x) { return x.id == actionId_; });
        if (a == chart.actions.end()) return {};
        const std::string array = "Acts_" + chart.arrayPrefix;
        auto work = loadUnit(*project_, chart, array);
        auto* chartLines = linesOf(work, chart.section);
        if (!chartLines) return {};
        auto* actionLines = chart.actionSection != kNoIndex ? linesOf(work, chart.actionSection) : nullptr;
        ActionLines own;
        GrafcetPlan refusal;
        if (!collectActionLines(chart, *a, array, *chartLines, actionLines, own, refusal)) return {};
        auto uses = usesOf(*project_, chart, work, array, actionId_, [&](Index s, std::size_t l) {
            if (s == chart.section) return own.chartOwn.count(l) != 0;
            if (s == chart.actionSection) return own.bodyOwn.count(l) != 0;
            return false;
            });
        for (auto& f : foreignUses(*project_, chart, array)) uses.push_back(std::move(f));
        return uses;
    }

    // ---------------------------------------------------------------------------
    ModifyActionCommand::ModifyActionCommand(ProjectPtr project, Index section, int actionId,
        ActionChange change)
        : project_(std::move(project)), section_(section), actionId_(actionId), change_(std::move(change)) {}

    std::string ModifyActionCommand::label() const {
        return "modifier l'action A" + std::to_string(actionId_)
            + (change_.name.empty() ? std::string{} : " (" + change_.name + ")");
    }

    core::Status ModifyActionCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::OutOfRange, "Le projet n'est plus l\xC3\xA0.");
        const auto plan = planModifyAction(*project_, section_, actionId_, change_);
        auto status = applyPlan(*project_, plan, previous_);
        applied_ = status.has_value();
        return status;
    }

    core::Status ModifyActionCommand::undo() {
        if (!applied_ || !project_) return core::ok();
        restore(*project_, previous_);
        applied_ = false;
        return core::ok();
    }

    std::vector<StLineChange> ModifyActionCommand::preview(std::string* error) const {
        if (!project_) return previewOf(project_, GrafcetPlan{}, error);
        return previewOf(project_, planModifyAction(*project_, section_, actionId_, change_), error);
    }

} // namespace project
