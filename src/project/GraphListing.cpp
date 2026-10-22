#include "GraphListing.hpp"

#include "../import/XmlReader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <tuple>

namespace project {

    using namespace domain;

    namespace {

        // Une ligne de commentaire : ni lecture ni ecriture.
        GraphLine note(std::string text) {
            GraphLine l;
            l.text = std::move(text);
            return l;
        }

        // ------------------------------------------------------------- arbre XML
        // Un petit arbre : un corps de section fait quelques dizaines de ko.
        struct Node {
            std::string name;
            std::vector<std::pair<std::string, std::string>> attrs;
            std::string text;
            std::vector<Node> kids;

            [[nodiscard]] std::string attr(std::string_view k, std::string fallback = {}) const {
                for (const auto& [n, v] : attrs)
                    if (n == k) return v;
                return fallback;
            }
            [[nodiscard]] const Node* child(std::string_view n) const {
                for (const auto& k : kids)
                    if (k.name == n) return &k;
                return nullptr;
            }
            [[nodiscard]] int number(std::string_view k, int fallback) const {
                const auto v = attr(k);
                return v.empty() ? fallback : std::atoi(v.c_str());
            }
        };

        bool parseTree(std::string_view xml, Node& root) {
            importer::XmlReader r(xml);
            std::vector<Node*> stack;
            for (;;) {
                auto ev = r.next();
                if (!ev) return false;
                if (*ev == importer::XmlEvent::EndOfDocument) break;
                if (*ev == importer::XmlEvent::StartElement) {
                    Node n;
                    n.name = std::string(r.name());
                    for (const auto& a : r.attributes())
                        n.attrs.emplace_back(std::string(a.name), importer::XmlReader::decodeEntities(a.rawValue));
                    if (stack.empty()) {
                        root = std::move(n);
                        stack.push_back(&root);
                    }
                    else {
                        stack.back()->kids.push_back(std::move(n));
                        stack.push_back(&stack.back()->kids.back());
                    }
                    continue;
                }
                if (*ev == importer::XmlEvent::EndElement) {
                    if (!stack.empty()) stack.pop_back();
                    continue;
                }
                if (*ev == importer::XmlEvent::Text && !stack.empty())
                    stack.back()->text += importer::XmlReader::decodeEntities(r.text());
            }
            return !root.name.empty();
        }

        std::string trimmed(std::string_view s) {
            const auto a = s.find_first_not_of(" \t\r\n");
            if (a == std::string_view::npos) return {};
            const auto b = s.find_last_not_of(" \t\r\n");
            return std::string(s.substr(a, b - a + 1));
        }

        bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
        bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

        // Les noms d'une expression ST (X, X.Y, X[i], X1.T), sans mots-cles, litteraux
        // ni appels de fonction.
        std::vector<std::string> namesIn(std::string_view e) {
            static constexpr std::string_view kKeywords[] = {
                "AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE", "IF", "THEN", "ELSE", "END_IF" };
            std::vector<std::string> out;
            const auto n = e.size();
            std::size_t i = 0;
            while (i < n) {
                const char c = e[i];
                if (c == '\'') {                                    // chaine
                    const auto end = e.find('\'', i + 1);
                    i = end == std::string_view::npos ? n : end + 1;
                    continue;
                }
                if (c == '(' && i + 1 < n && e[i + 1] == '*') {     // commentaire
                    const auto end = e.find("*)", i + 2);
                    i = end == std::string_view::npos ? n : end + 2;
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(c))) {  // nombre, 16#.., 2#..
                    while (i < n && (isIdentChar(e[i]) || e[i] == '#' || e[i] == '.')) ++i;
                    continue;
                }
                if (!isIdentStart(c)) { ++i; continue; }
                std::size_t j = i;
                while (j < n && isIdentChar(e[j])) ++j;
                if (j < n && e[j] == '#') {                         // t#5s, INT#3...
                    ++j;
                    while (j < n && (isIdentChar(e[j]) || e[j] == '.' || e[j] == '-')) ++j;
                    i = j;
                    continue;
                }
                std::string word(e.substr(i, j - i));
                std::string upper(word);
                for (auto& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                bool keyword = false;
                for (auto k : kKeywords) keyword = keyword || upper == k;
                std::size_t k = j;
                for (;;) {
                    if (k < n && e[k] == '[') {
                        int depth = 0;
                        while (k < n) {
                            if (e[k] == '[') ++depth;
                            else if (e[k] == ']' && --depth == 0) { ++k; break; }
                            ++k;
                        }
                        continue;
                    }
                    if (k + 1 < n && e[k] == '.' && isIdentStart(e[k + 1])) {
                        ++k;
                        while (k < n && isIdentChar(e[k])) ++k;
                        continue;
                    }
                    break;
                }
                std::size_t m = k;
                while (m < n && (e[m] == ' ' || e[m] == '\t')) ++m;
                const bool call = m < n && e[m] == '(' && k == j;   // une fonction : MAX(...)
                if (!keyword && !call) out.emplace_back(e.substr(i, k - i));
                i = keyword ? j : k;
            }
            return out;
        }

        // ------------------------------------------------------------------ LD
        struct Cell {
            std::string text;
            char        fill{ ' ' };
            int         span{ 1 };
            bool        bar{ false };     // liaison verticale a droite de la case
        };

        void append(std::vector<std::string>& to, const std::vector<std::string>& from) {
            to.insert(to.end(), from.begin(), from.end());
        }

        // Une case du reseau : son texte, ses noms lus et ecrits.
        void cellsOf(const Node& e, std::vector<Cell>& row, GraphLine& g, std::vector<GraphLine>& blocks,
                     int rowNumber) {
            const auto& t = e.name;
            if (t == "contact") {
                const auto v = e.attr("contactVariableName", "?");
                const auto k = e.attr("typeContact");
                g.reads.push_back(v);
                std::string text = "[" + v + "]";
                if (k == "closedContact") text = "[/" + v + "]";
                else if (k == "PContact" || k == "positiveTransitionSensingContact") text = "[P " + v + "]";
                else if (k == "NContact" || k == "negativeTransitionSensingContact") text = "[N " + v + "]";
                row.push_back(Cell{ text, '-' });
            }
            else if (t == "coil") {
                const auto v = e.attr("coilVariableName", "?");
                const auto k = e.attr("typeCoil");
                g.writes.push_back(v);
                std::string mark;
                if (k == "notCoil") mark = "/";
                else if (k == "setCoil") mark = "S ";
                else if (k == "resetCoil") mark = "R ";
                else if (k == "PCoil" || k == "positiveTransitionSensingCoil") mark = "P ";
                else if (k == "NCoil" || k == "negativeTransitionSensingCoil") mark = "N ";
                else if (k == "callCoil") mark = "C ";
                else if (k == "haltCoil") mark = "H ";
                row.push_back(Cell{ "(" + mark + v + ")", '-' });
            }
            else if (t == "HLink") {
                row.push_back(Cell{ {}, '-', std::max(1, e.number("nbCells", 1)) });
            }
            else if (t == "emptyCell") {
                row.push_back(Cell{ {}, ' ', std::max(1, e.number("nbCells", 1)) });
            }
            else if (t == "shortCircuit") {
                const auto before = row.size();
                bool vlink = false;
                for (const auto& k : e.kids) {
                    if (k.name == "VLink") { vlink = true; continue; }
                    cellsOf(k, row, g, blocks, rowNumber);
                }
                if (vlink) {
                    if (row.size() == before) row.push_back(Cell{ {}, ' ' });
                    row.back().bar = true;
                }
            }
            else if (t == "compareBlock" || t == "operateBlock") {
                const auto* x = e.child(t == "compareBlock" ? "expression" : "statement");
                const auto text = x ? trimmed(x->text) : std::string{};
                if (t == "operateBlock") {
                    // "X := expression;" : X est ecrit, le reste lu.
                    const auto at = text.find(":=");
                    if (at != std::string::npos) {
                        append(g.writes, namesIn(std::string_view(text).substr(0, at)));
                        append(g.reads, namesIn(std::string_view(text).substr(at + 2)));
                    }
                    else append(g.reads, namesIn(text));
                }
                else append(g.reads, namesIn(text));
                row.push_back(Cell{ "[" + text + "]", ' ', std::max(1, e.number("width", 2)) });
            }
            else if (t == "FFBBlock") {
                const auto type = e.attr("typeName");
                const auto inst = e.attr("instanceName");
                const auto label = inst.empty() ? type : inst;
                row.push_back(Cell{ "{" + label + "}", ' ' });

                GraphLine b;
                std::string pins;
                if (!inst.empty()) b.reads.push_back(inst);
                if (const auto* d = e.child("descriptionFFB"))
                    for (const auto& pin : d->kids) {
                        const auto formal = pin.attr("formalParameter");
                        const auto actual = pin.attr("effectiveParameter");
                        if (formal.empty() || actual.empty()) continue;
                        const bool out = pin.name == "outputVariable";
                        const bool inv = pin.attr("invertedPin") == "true";
                        if (!pins.empty()) pins += ", ";
                        pins += formal + (out ? " => " : " := ") + (inv ? "NOT " : "") + actual;
                        append(out ? b.writes : b.reads, namesIn(actual));
                    }
                b.text = "   {" + label + "} : " + type + (inst.empty() ? "" : " " + inst)
                    + " (rang\xC3\xA9" "e " + std::to_string(rowNumber) + ")"
                    + (pins.empty() ? std::string{} : " : " + pins);
                blocks.push_back(std::move(b));
            }
            else if (t == "control") {
                row.push_back(Cell{ "->> " + e.attr("label"), '-' });
            }
            else if (t == "labelCell") {
                row.push_back(Cell{ e.attr("label") + ":", ' ' });
            }
            else if (t == "VLink") {
                if (!row.empty()) row.back().bar = true;
            }
            else if (t != "objPosition") {
                row.push_back(Cell{ "<" + t + ">", ' ' });
            }
        }

        void listLd(const Node& ld, std::vector<GraphLine>& out, std::string_view indent) {
            int network = 0;
            for (const auto& net : ld.kids) {
                if (net.name != "networkLD") continue;
                ++network;
                out.push_back(note(std::string(indent) + "(* r\xC3\xA9seau " + std::to_string(network) + " *)"));

                for (const auto& k : net.kids)
                    if (k.name == "textBox") {
                        const auto text = trimmed(k.text);
                        if (!text.empty()) out.push_back(note(std::string(indent) + "(* " + text + " *)"));
                    }

                struct Row { int number; std::vector<Cell> cells; GraphLine line; };
                std::vector<Row> rows;
                std::vector<GraphLine> blocks;
                int number = 0;
                for (const auto& k : net.kids) {
                    if (k.name != "typeLine") continue;
                    if (const auto* empty = k.child("emptyLine")) {
                        number += std::max(1, empty->number("nbRows", 1));
                        continue;
                    }
                    Row r;
                    r.number = number++;
                    for (const auto& e : k.kids) cellsOf(e, r.cells, r.line, blocks, r.number);
                    rows.push_back(std::move(r));
                }

                // Largeur de chaque colonne : la plus longue case d'une colonne.
                std::vector<std::size_t> width;
                for (const auto& r : rows) {
                    std::size_t col = 0;
                    for (const auto& c : r.cells) {
                        if (width.size() < col + static_cast<std::size_t>(c.span)) width.resize(col + c.span, 3);
                        if (c.span == 1) width[col] = std::max(width[col], c.text.size() + (c.bar ? 2 : 1));
                        col += static_cast<std::size_t>(c.span);
                    }
                }
                for (auto& r : rows) {
                    std::string text;
                    std::size_t col = 0;
                    bool content = false;
                    for (const auto& c : r.cells) {
                        std::size_t avail = 0;
                        for (int s = 0; s < c.span && col + s < width.size(); ++s) avail += width[col + s];
                        std::string cell = c.text;
                        const std::size_t room = avail > (c.bar ? 1u : 0u) ? avail - (c.bar ? 1u : 0u) : 0u;
                        if (cell.size() < room) cell.append(room - cell.size(), c.fill);
                        if (c.bar) cell.push_back('|');
                        text += cell;
                        content = content || !c.text.empty() || c.bar || c.fill == '-';
                        col += static_cast<std::size_t>(c.span);
                    }
                    while (!text.empty() && text.back() == ' ') text.pop_back();
                    // Le fil apres la bobine de droite ne mene nulle part.
                    if (const auto paren = text.find_last_of(')'); paren != std::string::npos
                        && text.find_first_not_of('-', paren + 1) == std::string::npos)
                        text.erase(paren + 1);
                    if (!content || text.empty()) continue;
                    std::string num = std::to_string(r.number);
                    if (num.size() < 2) num.insert(0, 1, '0');
                    r.line.text = std::string(indent) + num + "| " + text;
                    out.push_back(std::move(r.line));
                }
                for (auto& b : blocks) {
                    b.text.insert(0, indent);
                    out.push_back(std::move(b));
                }
            }
        }

        // ----------------------------------------------------------------- FBD
        void listFbd(const Node& fbd, std::vector<GraphLine>& out, std::string_view indent) {
            int network = 0;
            for (const auto& net : fbd.kids) {
                if (net.name != "networkFBD") continue;
                out.push_back(note(std::string(indent) + "(* r\xC3\xA9seau " + std::to_string(++network) + " *)"));
                for (const auto& k : net.kids) {
                    if (k.name == "textBox") {
                        const auto text = trimmed(k.text);
                        if (!text.empty()) out.push_back(note(std::string(indent) + "(* " + text + " *)"));
                        continue;
                    }
                    if (k.name != "FFBBlock") continue;
                    std::vector<Cell> scratch;
                    GraphLine unused;
                    std::vector<GraphLine> blocks;
                    cellsOf(k, scratch, unused, blocks, 0);
                    for (auto& b : blocks) {
                        const auto cut = b.text.find(" (rang");
                        const auto colon = b.text.find(" : ", cut == std::string::npos ? 0 : cut);
                        if (cut != std::string::npos && colon != std::string::npos)
                            b.text.erase(cut, colon - cut);
                        else if (cut != std::string::npos)
                            b.text.erase(cut);
                        b.text.insert(0, indent);
                        out.push_back(std::move(b));
                    }
                }
            }
        }

        // ----------------------------------------------------------------- SFC
        void listChart(const Node& chart, std::vector<GraphLine>& out, std::string_view indent) {
            for (const auto& net : chart.kids) {
                if (net.name != "networkSFC") continue;
                std::vector<std::tuple<int, int, GraphLine>> items;
                for (const auto& e : net.kids) {
                    int x = -1, y = -1;
                    if (const auto* pos = e.child("objPosition")) {
                        x = pos->number("posX", -1);
                        y = pos->number("posY", -1);
                    }
                    GraphLine g;
                    if (e.name == "step") {
                        const auto kind = e.attr("stepType");
                        g.text = "\xC3\xA9tape " + e.attr("stepName")
                            + (kind == "initialStep" ? " (initiale)" : kind == "macroStep" ? " (macro-\xC3\xA9tape)" : "");
                        if (const auto* lit = e.child("literals")) {
                            for (const char* k : { "min", "max", "delay" }) {
                                const auto v = lit->attr(k);
                                if (!v.empty()) g.text += std::string("  ") + k + " = " + v;
                            }
                        }
                    }
                    else if (e.name == "transition") {
                        std::string cond;
                        if (const auto* tc = e.child("transitionCondition")) {
                            const bool inv = tc->attr("invertLogic") == "true";
                            for (const auto& c : tc->kids) {
                                const auto text = trimmed(c.text);
                                if (c.name == "variableName") { cond = text; g.reads.push_back(text); }
                                else if (c.name == "sectionName") cond = "r\xC3\xA9" "ceptivit\xC3\xA9 " + text;
                                else if (!text.empty()) { cond = text; append(g.reads, namesIn(text)); }
                            }
                            if (inv) cond = "NOT " + cond;
                        }
                        g.text = "transition " + cond;
                    }
                    else if (e.name == "jumpSFC") g.text = "saut vers " + e.attr("stepName");
                    else if (e.name == "altBranch") g.text = "divergence en OU";
                    else if (e.name == "altJoint") g.text = "convergence en OU";
                    else if (e.name == "parBranch") g.text = "divergence en ET";
                    else if (e.name == "parJoint") g.text = "convergence en ET";
                    else continue;                                     // liens, commentaires
                    items.emplace_back(x, y, std::move(g));
                }
                std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
                    return std::tie(std::get<0>(a), std::get<1>(a)) < std::tie(std::get<0>(b), std::get<1>(b));
                });
                int column = -2;
                for (auto& [x, y, g] : items) {
                    if (x != column) {
                        column = x;
                        out.push_back(note(std::string(indent) + "(* colonne " + std::to_string(x) + " *)"));
                    }
                    g.text = std::string(indent) + "  " + (y < 10 ? "0" : "") + std::to_string(y) + "  " + g.text;
                    out.push_back(std::move(g));
                }
            }
        }

        void listSfc(const Node& sfc, std::vector<GraphLine>& out) {
            for (const auto& k : sfc.kids)
                if (k.name == "chartSource" || k.name == "SFCSource") listChart(k, out, "");
            for (const auto& k : sfc.kids) {
                if (k.name != "transitionSource") continue;
                out.push_back(note({}));
                out.push_back(note("(* r\xC3\xA9" "ceptivit\xC3\xA9 " + k.attr("name") + " *)"));
                for (const auto& body : k.kids) {
                    if (body.name == "LDSource") listLd(body, out, "  ");
                    else if (body.name == "FBDSource") listFbd(body, out, "  ");
                    else if (body.name == "STSource" || body.name == "ILSource") {
                        const auto text = trimmed(body.text);
                        std::size_t from = 0;
                        while (from <= text.size()) {
                            auto end = text.find('\n', from);
                            if (end == std::string::npos) end = text.size();
                            auto line = text.substr(from, end - from);
                            if (!line.empty() && line.back() == '\r') line.pop_back();
                            GraphLine g = note("  " + line);
                            g.reads = namesIn(line);
                            out.push_back(std::move(g));
                            from = end + 1;
                        }
                    }
                }
            }
        }

    } // namespace

    std::vector<GraphLine> graphListing(const Section& s) {
        std::vector<GraphLine> out;
        if (!keepsSourceXml(s)) return out;
        Node root;
        if (!parseTree(s.body, root)) {
            out.push_back(note("(* sch\xC3\xA9ma illisible : le XML de la section est incomplet *)"));
            return out;
        }
        if (root.name == "LDSource") listLd(root, out, "");
        else if (root.name == "FBDSource") listFbd(root, out, "");
        else if (root.name == "SFCProgram") listSfc(root, out);
        else if (root.name == "SFCSource") listChart(root, out, "");
        return out;
    }

    namespace {
        // Ce que l'on regarde, en tete du texte affiche.
        std::string headerOf(const Section& s) {
            std::string text;
            const auto lang = std::string(toString(s.language));
            text += "(* Section " + lang + " gard\xC3\xA9" "e telle que Control Expert l'a \xC3\xA9" "crite : elle se lit ici,\n"
                    "   elle se modifie dans Control Expert. API Studio la conserve et la r\xC3\xA9" "exporte \xC3\xA0 l'identique.\n";
            if (s.language == PouLanguage::LD)
                text += "   [X] contact, [/X] contact inverse, [P X] [N X] fronts ; (X) bobine, (/X) inverse,\n"
                        "   (S X) (R X) mise \xC3\xA0 1 / \xC3\xA0 0 ; | liaison verticale (OU) ; {BLOC} : d\xC3\xA9tail sous le r\xC3\xA9seau. *)\n";
            else if (s.language == PouLanguage::SFC)
                text += "   Le graphe, colonne par colonne (ligne, \xC3\xA9l\xC3\xA9ment), puis chaque r\xC3\xA9" "ceptivit\xC3\xA9. *)\n";
            else
                text += "   Les blocs du r\xC3\xA9seau et leurs broches. *)\n";
            text += "\n";
            return text;
        }
    } // namespace

    std::size_t graphListingHeaderLines(const Section& s) {
        const auto h = headerOf(s);
        return static_cast<std::size_t>(std::count(h.begin(), h.end(), '\n'));
    }

    std::string graphListingText(const Section& s) {
        std::string text = headerOf(s);
        for (const auto& line : graphListing(s)) {
            text += line.text;
            text += '\n';
        }
        return text;
    }

} // namespace project
