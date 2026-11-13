#include "Sfc.hpp"

#include "../import/XmlReader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>

namespace sfc {

    namespace {

        // ------------------------------------------------------------- arbre XML
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
            [[nodiscard]] bool has(std::string_view k) const {
                for (const auto& a : attrs)
                    if (a.first == k) return true;
                return false;
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
                    if (stack.empty()) break;
                    continue;
                }
                if (*ev == importer::XmlEvent::Text && !stack.empty())
                    stack.back()->text += importer::XmlReader::decodeEntities(r.text());
            }
            return !root.name.empty();
        }

        // Les enfants directs d'un element, chacun avec son XML tel quel.
        struct RawChild { std::string name, raw; };
        std::vector<RawChild> childRaws(std::string_view xml) {
            std::vector<RawChild> out;
            importer::XmlReader r(xml);
            auto ev = r.next();
            if (!ev || *ev != importer::XmlEvent::StartElement || r.selfClosing()) return out;
            for (;;) {
                ev = r.next();
                if (!ev || *ev == importer::XmlEvent::EndOfDocument || *ev == importer::XmlEvent::EndElement) break;
                if (*ev != importer::XmlEvent::StartElement) continue;
                std::string name(r.name());
                auto raw = r.readElementRaw();
                if (!raw) break;
                out.push_back({ std::move(name), std::move(*raw) });
            }
            return out;
        }

        std::string trimmed(std::string_view s) {
            const auto a = s.find_first_not_of(" \t\r\n");
            if (a == std::string_view::npos) return {};
            const auto b = s.find_last_not_of(" \t\r\n");
            return std::string(s.substr(a, b - a + 1));
        }

        std::string upper(std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }

        std::string escAttr(std::string_view in) {
            std::string out;
            out.reserve(in.size());
            for (char c : in) {
                switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                default: out.push_back(c);
                }
            }
            return out;
        }

        std::string escText(std::string_view in) {
            std::string out;
            out.reserve(in.size());
            for (char c : in) {
                switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                default: out.push_back(c);
                }
            }
            return out;
        }

        // L'indentation d'une ligne : ce qui suit le dernier saut de ligne de l'espace
        // qui precede un element.
        std::string indentOf(const std::string& gap) {
            const auto nl = gap.find_last_of('\n');
            std::string tail = nl == std::string::npos ? gap : gap.substr(nl + 1);
            if (tail.find_first_not_of(" \t") != std::string::npos) return {};
            return tail;
        }

        // L'indentation de la ligne ou commence la position `at`.
        std::string lineIndent(const std::string& text, std::size_t at) {
            const auto nl = at == 0 ? std::string::npos : text.rfind('\n', at - 1);
            const std::size_t from = nl == std::string::npos ? 0 : nl + 1;
            std::size_t end = from;
            while (end < at && (text[end] == '\t' || text[end] == ' ')) ++end;
            return text.substr(from, end - from);
        }

        ItemKind kindOf(std::string_view tag) {
            if (tag == "step") return ItemKind::Step;
            if (tag == "transition") return ItemKind::Transition;
            if (tag == "altBranch") return ItemKind::AltBranch;
            if (tag == "altJoint") return ItemKind::AltJoint;
            if (tag == "parBranch") return ItemKind::ParBranch;
            if (tag == "parJoint") return ItemKind::ParJoint;
            if (tag == "jumpSFC") return ItemKind::Jump;
            if (tag == "linkSFC") return ItemKind::Link;
            if (tag == "textBox") return ItemKind::TextBox;
            return ItemKind::Other;
        }

        std::string tagOf(ItemKind k) {
            switch (k) {
            case ItemKind::Step:       return "step";
            case ItemKind::Transition: return "transition";
            case ItemKind::AltBranch:  return "altBranch";
            case ItemKind::AltJoint:   return "altJoint";
            case ItemKind::ParBranch:  return "parBranch";
            case ItemKind::ParJoint:   return "parJoint";
            case ItemKind::Jump:       return "jumpSFC";
            case ItemKind::Link:       return "linkSFC";
            case ItemKind::TextBox:    return "textBox";
            case ItemKind::Other:      break;
            }
            return {};
        }

        StepKind stepKindOf(std::string_view t) {
            if (t == "initialStep") return StepKind::Initial;
            if (t == "macroStep") return StepKind::Macro;
            if (t == "inStep") return StepKind::In;
            if (t == "outStep") return StepKind::Out;
            return StepKind::Normal;
        }

        std::string stepTypeText(StepKind k) {
            switch (k) {
            case StepKind::Initial: return "initialStep";
            case StepKind::Macro:   return "macroStep";
            case StepKind::In:      return "inStep";
            case StepKind::Out:     return "outStep";
            case StepKind::Normal:  break;
            }
            return "step";
        }

        domain::PouLanguage languageOf(std::string_view tag) {
            if (tag == "STSource") return domain::PouLanguage::ST;
            if (tag == "ILSource") return domain::PouLanguage::IL;
            if (tag == "LDSource") return domain::PouLanguage::LD;
            if (tag == "FBDSource") return domain::PouLanguage::FBD;
            return domain::PouLanguage::Unknown;
        }

        void readItem(Item& it) {
            Node n;
            if (!parseTree(it.xml, n)) { it.kind = ItemKind::Other; return; }
            it.tag = n.name;
            it.kind = kindOf(n.name);
            it.attrs = n.attrs;
            if (const auto* p = n.child("objPosition")) {
                it.x = p->number("posX", 0);
                it.y = p->number("posY", 0);
            }
            std::set<std::string> known{ "objPosition" };
            switch (it.kind) {
            case ItemKind::Step: {
                known.insert({ "action", "literals", "comment" });
                it.name = n.attr("stepName");
                it.stepKind = stepKindOf(n.attr("stepType"));
                for (const auto& k : n.kids) {
                    if (k.name == "action") {
                        Action a;
                        a.qualifier = k.attr("qualifier", "N");
                        if (const auto* an = k.child("actionName")) {
                            for (const auto& c : an->kids) {
                                if (c.name == "sectionName") { a.isSection = true; a.name = trimmed(c.text); }
                                else if (c.name == "variableName") { a.isSection = false; a.name = trimmed(c.text); }
                            }
                            if (an->kids.empty()) a.name = trimmed(an->text);
                        }
                        if (const auto* tv = k.child("tValue")) {
                            for (const auto& c : tv->kids) {
                                const auto text = trimmed(c.text);
                                if (c.name == "tLiteral") { a.time = text; a.timeIsVariable = false; }
                                else if (!text.empty()) { a.time = text; a.timeIsVariable = true; }
                            }
                        }
                        it.actions.push_back(std::move(a));
                    }
                    else if (k.name == "literals") {
                        it.hasLiterals = true;
                        it.tmin = k.attr("min");
                        it.tmax = k.attr("max");
                        it.delay = k.attr("delay");
                    }
                    else if (k.name == "comment") {
                        it.hasComment = true;
                        it.comment = k.text;
                    }
                }
                break;
            }
            case ItemKind::Transition: {
                known.insert("transitionCondition");
                if (const auto* tc = n.child("transitionCondition")) {
                    it.invert = tc->attr("invertLogic") == "true";
                    for (const auto& c : tc->kids) {
                        const auto text = trimmed(c.text);
                        if (c.name == "variableName") { it.cond = CondKind::Variable; it.condText = text; }
                        else if (c.name == "sectionName") { it.cond = CondKind::Section; it.condText = text; }
                        else if (!text.empty()) { it.cond = CondKind::Expression; it.condText = text; }
                    }
                }
                // Une variable qui n'en est pas une (des espaces, des operateurs) : une
                // expression ecrite en ligne.
                if (it.cond == CondKind::Variable
                    && it.condText.find_first_of(" ()<>=+-*/:") != std::string::npos)
                    it.cond = CondKind::Expression;
                break;
            }
            case ItemKind::AltBranch:
            case ItemKind::AltJoint:
            case ItemKind::ParBranch:
            case ItemKind::ParJoint:
                it.width = std::max(1, n.number("width", 1));
                it.relativePos = n.number("relativePos", 0);
                break;
            case ItemKind::Jump:
                it.name = n.attr("stepName");
                break;
            case ItemKind::Link: {
                known = { "directedLinkSource", "directedLinkDestination", "gridObjPosition" };
                if (const auto* s = n.child("directedLinkSource")) {
                    it.srcType = s->attr("objectType");
                    if (const auto* p = s->child("objPosition")) { it.sx = p->number("posX", 0); it.sy = p->number("posY", 0); }
                }
                if (const auto* d = n.child("directedLinkDestination")) {
                    it.dstType = d->attr("objectType");
                    if (const auto* p = d->child("objPosition")) { it.dx = p->number("posX", 0); it.dy = p->number("posY", 0); }
                }
                for (const auto& k : n.kids)
                    if (k.name == "gridObjPosition") it.bends.emplace_back(k.attr("posX"), k.attr("posY"));
                if (const auto* p = n.child("directedLinkSource")) (void)p;
                it.x = it.sx;
                it.y = it.sy;
                break;
            }
            case ItemKind::TextBox:
                it.boxW = std::max(1, n.number("width", 1));
                it.boxH = std::max(1, n.number("height", 1));
                it.text = trimmed(n.text);
                break;
            case ItemKind::Other:
                break;
            }
            if (it.kind != ItemKind::Other)
                for (auto& c : childRaws(it.xml))
                    if (!known.count(c.name)) it.extraChildren.push_back(std::move(c.raw));
        }

        void setAttr(std::vector<std::pair<std::string, std::string>>& attrs, const std::string& key,
                     const std::string& value) {
            for (auto& [k, v] : attrs)
                if (k == key) { v = value; return; }
            attrs.emplace_back(key, value);
        }

        std::string attrsText(const std::vector<std::pair<std::string, std::string>>& attrs) {
            std::string out;
            for (const auto& [k, v] : attrs) out += " " + k + "=\"" + escAttr(v) + "\"";
            return out;
        }

        // Un element genere, ligne par ligne : la premiere sans indentation (l'espace
        // qui precede la porte), les suivantes indentees de `indent` + profondeur.
        struct Lines {
            std::string out;
            std::string indent, eol;
            bool first{ true };
            void line(int depth, const std::string& text) {
                if (!first) {
                    out += eol;
                    out += indent;
                }
                if (depth > 0) out.append(static_cast<std::size_t>(depth), '\t');
                out += text;
                first = false;
            }
            // Un fragment garde tel quel : sa premiere ligne a la profondeur donnee.
            void raw(int depth, const std::string& text) {
                std::size_t from = 0;
                bool head = true;
                for (;;) {
                    const auto nl = text.find('\n', from);
                    std::string piece = text.substr(from, (nl == std::string::npos ? text.size() : nl) - from);
                    if (!piece.empty() && piece.back() == '\r') piece.pop_back();
                    if (head) { line(depth, piece); head = false; }
                    else { out += eol; out += piece; }
                    if (nl == std::string::npos) break;
                    from = nl + 1;
                }
            }
        };

        std::string pos(int x, int y) {
            return "<objPosition posX=\"" + std::to_string(x) + "\" posY=\"" + std::to_string(y) + "\"></objPosition>";
        }

    } // namespace

    // ------------------------------------------------------------- les noms
    std::string normalQualifier(std::string_view q) {
        auto u = upper(trimmed(q));
        if (u.empty() || u == "NONE") return "N";
        return u;
    }

    bool qualifierTimed(std::string_view q) {
        const auto u = normalQualifier(q);
        return u == "L" || u == "D" || u == "DS" || u == "SD" || u == "SL";
    }

    const std::vector<std::string>& qualifiers() {
        static const std::vector<std::string> list{ "N", "S", "R", "L", "D", "P", "DS", "P1", "P0" };
        return list;
    }

    std::string qualifierMeaning(std::string_view q) {
        const auto u = normalQualifier(q);
        if (u == "N") return "tant que l'\xC3\xA9tape est active";
        if (u == "S") return "mise \xC3\xA0 1 m\xC3\xA9moris\xC3\xA9" "e (jusqu'\xC3\xA0 un R)";
        if (u == "R") return "remise \xC3\xA0 0 d'une action m\xC3\xA9moris\xC3\xA9" "e";
        if (u == "L") return "limit\xC3\xA9" "e dans le temps (pendant la dur\xC3\xA9" "e)";
        if (u == "D") return "retard\xC3\xA9" "e (apr\xC3\xA8s la dur\xC3\xA9" "e)";
        if (u == "P") return "impulsion \xC3\xA0 l'activation";
        if (u == "P1") return "impulsion sur front montant (activation)";
        if (u == "P0") return "impulsion sur front descendant (d\xC3\xA9sactivation)";
        if (u == "DS") return "retard\xC3\xA9" "e puis m\xC3\xA9moris\xC3\xA9" "e";
        if (u == "SD") return "m\xC3\xA9moris\xC3\xA9" "e puis retard\xC3\xA9" "e";
        if (u == "SL") return "m\xC3\xA9moris\xC3\xA9" "e et limit\xC3\xA9" "e dans le temps";
        return "qualificatif inconnu";
    }

    // ------------------------------------------------------------- Chart
    int Chart::findStep(std::string_view name) const {
        const auto want = upper(name);
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].kind == ItemKind::Step && upper(items[i].name) == want) return static_cast<int>(i);
        return -1;
    }

    int Chart::itemAt(int x, int y) const {
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].isCellObject() && items[i].x == x && items[i].y == y) return static_cast<int>(i);
        return -1;
    }

    const Source* Chart::source(std::string_view tag, std::string_view name) const {
        const auto want = upper(name);
        for (const auto& s : sources)
            if (s.tag == tag && upper(s.name) == want) return &s;
        return nullptr;
    }

    Source* Chart::source(std::string_view tag, std::string_view name) {
        return const_cast<Source*>(static_cast<const Chart*>(this)->source(tag, name));
    }

    std::vector<int> Chart::steps() const {
        std::vector<int> out;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].kind == ItemKind::Step) out.push_back(static_cast<int>(i));
        return out;
    }

    std::vector<int> Chart::transitions() const {
        std::vector<int> out;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].kind == ItemKind::Transition) out.push_back(static_cast<int>(i));
        return out;
    }

    bool Chart::modified() const noexcept {
        if (structureChanged) return true;
        for (const auto& i : items) if (i.dirty || i.xml.empty()) return true;
        for (const auto& s : sources) if (s.dirty || s.xml.empty()) return true;
        return false;
    }

    int Chart::lastRow() const {
        int row = -1;
        for (const auto& i : items) {
            if (i.kind == ItemKind::Link) {
                row = std::max({ row, i.sy, i.dy });
                continue;
            }
            row = std::max(row, i.y + (i.kind == ItemKind::TextBox ? i.boxH - 1 : 0));
        }
        return row;
    }

    int Chart::lastColumn() const {
        int col = -1;
        for (const auto& i : items) {
            if (i.kind == ItemKind::Link) continue;
            int w = 1;
            if (i.isBar()) w = i.width;
            else if (i.kind == ItemKind::TextBox) w = i.boxW;
            col = std::max(col, i.x + w - 1);
        }
        return col;
    }

    // ------------------------------------------------------------- parse
    Chart parse(const std::string& xml) {
        Chart c;
        c.original = xml;
        c.eol = xml.find("\r\n") != std::string::npos ? "\r\n" : (xml.find('\n') != std::string::npos ? "\n" : "\r\n");
        importer::XmlReader r(xml);
        const char* base = xml.data();
        auto off = [&](std::string_view sv) { return static_cast<std::size_t>(sv.data() - base); };

        auto ev = r.next();
        if (!ev || *ev != importer::XmlEvent::StartElement) {
            c.error = "le XML du GRAFCET ne commence pas par un \xC3\xA9l\xC3\xA9ment";
            return c;
        }
        const std::string rootName(r.name());
        if (rootName != "SFCProgram") {
            c.error = "pas un SFCProgram : " + rootName;
            return c;
        }
        if (r.selfClosing()) { c.ok = true; c.head = xml; return c; }

        std::size_t headEnd = std::string::npos;     // fin de <networkSFC>
        std::size_t midStart = std::string::npos, midEnd = std::string::npos;
        std::size_t prevEnd = 0;
        bool chartDone = false;
        std::string netIndent;
        for (;;) {
            ev = r.next();
            if (!ev) { c.error = ev.error().message(); return c; }
            if (*ev == importer::XmlEvent::EndOfDocument) break;
            if (*ev == importer::XmlEvent::EndElement) {
                // la fin de la racine
                const std::size_t closeAt = off(r.name()) - 2;
                if (chartDone) c.tail = xml.substr(prevEnd);
                (void)closeAt;
                break;
            }
            if (*ev != importer::XmlEvent::StartElement) continue;
            const std::string name(r.name());
            const std::size_t start = off(r.name()) - 1;

            if (name == "identProgram") {
                c.programName = r.attr("name");
                auto raw = r.readElementRaw();
                if (!raw) { c.error = raw.error().message(); return c; }
                continue;
            }
            if (name == "chartSource" && !chartDone && !r.selfClosing()) {
                // ---- le graphe
                for (;;) {
                    ev = r.next();
                    if (!ev) { c.error = ev.error().message(); return c; }
                    if (*ev == importer::XmlEvent::EndOfDocument) { c.error = "chartSource sans fin"; return c; }
                    if (*ev == importer::XmlEvent::EndElement) {          // </chartSource>
                        const std::size_t at = off(r.name());
                        const auto gt = xml.find('>', at);
                        midEnd = gt == std::string::npos ? xml.size() : gt + 1;
                        break;
                    }
                    if (*ev != importer::XmlEvent::StartElement) continue;
                    const std::string inner(r.name());
                    const std::size_t innerStart = off(r.name()) - 1;
                    if (inner != "networkSFC" || headEnd != std::string::npos) {
                        auto raw = r.readElementRaw();
                        if (!raw) { c.error = raw.error().message(); return c; }
                        continue;
                    }
                    netIndent = lineIndent(xml, innerStart);
                    if (r.selfClosing()) {
                        // <networkSFC/> : on l'ouvre et le ferme a l'ecriture
                        auto raw = r.readElementRaw();
                        if (!raw) { c.error = raw.error().message(); return c; }
                        c.head = xml.substr(0, innerStart) + "<networkSFC>";
                        headEnd = innerStart + raw->size();
                        midStart = headEnd;
                        c.mid = "</networkSFC>";
                        c.structureChanged = false;
                        prevEnd = headEnd;
                        continue;
                    }
                    const auto gt = xml.find('>', innerStart);
                    headEnd = gt == std::string::npos ? xml.size() : gt + 1;
                    c.head = xml.substr(0, headEnd);
                    prevEnd = headEnd;
                    for (;;) {
                        ev = r.next();
                        if (!ev) { c.error = ev.error().message(); return c; }
                        if (*ev == importer::XmlEvent::EndOfDocument) { c.error = "networkSFC sans fin"; return c; }
                        if (*ev == importer::XmlEvent::EndElement) {      // </networkSFC>
                            midStart = prevEnd;
                            break;
                        }
                        if (*ev != importer::XmlEvent::StartElement) continue;
                        const std::size_t itemStart = off(r.name()) - 1;
                        auto raw = r.readElementRaw();
                        if (!raw) { c.error = raw.error().message(); return c; }
                        Item it;
                        it.gap = xml.substr(prevEnd, itemStart - prevEnd);
                        it.xml = std::move(*raw);
                        prevEnd = itemStart + it.xml.size();
                        readItem(it);
                        c.items.push_back(std::move(it));
                    }
                }
                if (headEnd == std::string::npos) {
                    c.ok = true;          // un graphe sans reseau : on le lit, on ne l'ecrit pas
                    c.head = xml;
                    return c;
                }
                if (c.mid.empty()) c.mid = xml.substr(midStart, midEnd - midStart);
                else c.mid += xml.substr(prevEnd, midEnd - prevEnd);
                prevEnd = midEnd;
                chartDone = true;
                c.itemIndent = netIndent + "\t";
                c.sourceIndent = lineIndent(xml, xml.rfind("<chartSource", headEnd));
                for (const auto& it : c.items) {
                    const auto ind = indentOf(it.gap);
                    if (!ind.empty()) { c.itemIndent = ind; break; }
                }
                continue;
            }
            // ---- une section de transition, d'action, autre chose
            auto raw = r.readElementRaw();
            if (!raw) { c.error = raw.error().message(); return c; }
            if (!chartDone) continue;          // avant le graphe : reste dans head
            Source s;
            s.tag = name;
            s.gap = xml.substr(prevEnd, start - prevEnd);
            s.xml = std::move(*raw);
            prevEnd = start + s.xml.size();
            Node n;
            if (parseTree(s.xml, n)) {
                s.name = n.attr("name");
                for (const auto& k : n.kids) {
                    s.innerTag = k.name;
                    s.language = languageOf(k.name);
                    break;
                }
                if (s.language == domain::PouLanguage::ST || s.language == domain::PouLanguage::IL) {
                    if (const auto* k = n.child(s.innerTag)) s.body = k->text;
                }
                else {
                    for (auto& k : childRaws(s.xml)) { s.body = std::move(k.raw); break; }
                }
            }
            const auto ind = indentOf(s.gap);
            if (!ind.empty() && c.sources.empty()) c.sourceIndent = ind;
            c.sources.push_back(std::move(s));
        }
        if (!chartDone) {
            c.ok = true;
            c.head = xml;
            return c;
        }
        c.ok = true;
        c.editable = true;
        return c;
    }

    // ------------------------------------------------------------- ecrire
    std::string itemXml(const Item& it, const std::string& indent, const std::string& eol) {
        Lines o;
        o.indent = indent;
        o.eol = eol;
        auto attrs = it.attrs;
        const std::string tag = it.tag.empty() ? tagOf(it.kind) : it.tag;
        switch (it.kind) {
        case ItemKind::Step: {
            // l'ordre de Control Expert : stepType puis stepName
            if (attrs.empty()) {
                attrs.emplace_back("stepType", stepTypeText(it.stepKind));
                attrs.emplace_back("stepName", it.name);
            }
            setAttr(attrs, "stepType", stepTypeText(it.stepKind));
            setAttr(attrs, "stepName", it.name);
            o.line(0, "<" + tag + attrsText(attrs) + ">");
            o.line(1, pos(it.x, it.y));
            for (const auto& a : it.actions) {
                o.line(1, "<action qualifier=\"" + escAttr(a.qualifier.empty() ? "N" : a.qualifier) + "\">");
                o.line(2, "<actionName>");
                o.line(3, std::string(a.isSection ? "<sectionName>" : "<variableName>") + escText(a.name)
                              + (a.isSection ? "</sectionName>" : "</variableName>"));
                o.line(2, "</actionName>");
                o.line(2, "<tValue>");
                if (a.timeIsVariable && !a.time.empty())
                    o.line(3, "<variableName>" + escText(a.time) + "</variableName>");
                else
                    o.line(3, "<tLiteral>" + escText(a.time) + "</tLiteral>");
                o.line(2, "</tValue>");
                o.line(1, "</action>");
            }
            o.line(1, "<literals max=\"" + escAttr(it.tmax) + "\" min=\"" + escAttr(it.tmin) + "\" delay=\""
                          + escAttr(it.delay) + "\"></literals>");
            if (it.hasComment) o.line(1, "<comment>" + escText(it.comment) + "</comment>");
            for (const auto& x : it.extraChildren) o.raw(1, x);
            o.line(0, "</" + tag + ">");
            break;
        }
        case ItemKind::Transition: {
            o.line(0, "<" + tag + attrsText(attrs) + ">");
            o.line(1, pos(it.x, it.y));
            o.line(1, std::string("<transitionCondition invertLogic=\"") + (it.invert ? "true" : "false") + "\">");
            if (it.cond == CondKind::Section)
                o.line(2, "<sectionName>" + escText(it.condText) + "</sectionName>");
            else if (it.cond != CondKind::None || !it.condText.empty())
                o.line(2, "<variableName>" + escText(it.condText) + "</variableName>");
            o.line(1, "</transitionCondition>");
            for (const auto& x : it.extraChildren) o.raw(1, x);
            o.line(0, "</" + tag + ">");
            break;
        }
        case ItemKind::AltBranch:
        case ItemKind::AltJoint:
        case ItemKind::ParBranch:
        case ItemKind::ParJoint: {
            if (attrs.empty()) {
                attrs.emplace_back("width", std::to_string(it.width));
                attrs.emplace_back("relativePos", std::to_string(it.relativePos));
            }
            setAttr(attrs, "width", std::to_string(it.width));
            setAttr(attrs, "relativePos", std::to_string(it.relativePos));
            o.line(0, "<" + tag + attrsText(attrs) + ">");
            o.line(1, pos(it.x, it.y));
            for (const auto& x : it.extraChildren) o.raw(1, x);
            o.line(0, "</" + tag + ">");
            break;
        }
        case ItemKind::Jump: {
            setAttr(attrs, "stepName", it.name);
            o.line(0, "<" + tag + attrsText(attrs) + ">");
            o.line(1, pos(it.x, it.y));
            for (const auto& x : it.extraChildren) o.raw(1, x);
            o.line(0, "</" + tag + ">");
            break;
        }
        case ItemKind::Link: {
            o.line(0, "<" + tag + attrsText(attrs) + ">");
            o.line(1, "<directedLinkSource objectType=\"" + escAttr(it.srcType) + "\">");
            o.line(2, pos(it.sx, it.sy));
            o.line(1, "</directedLinkSource>");
            o.line(1, "<directedLinkDestination objectType=\"" + escAttr(it.dstType) + "\">");
            o.line(2, pos(it.dx, it.dy));
            o.line(1, "</directedLinkDestination>");
            for (const auto& [bx, by] : it.bends)
                o.line(1, "<gridObjPosition posX=\"" + escAttr(bx) + "\" posY=\"" + escAttr(by) + "\"></gridObjPosition>");
            for (const auto& x : it.extraChildren) o.raw(1, x);
            o.line(0, "</" + tag + ">");
            break;
        }
        case ItemKind::TextBox: {
            setAttr(attrs, "width", std::to_string(it.boxW));
            setAttr(attrs, "height", std::to_string(it.boxH));
            o.line(0, "<" + tag + attrsText(attrs) + ">" + escText(it.text) + pos(it.x, it.y));
            o.line(0, "</" + tag + ">");
            break;
        }
        case ItemKind::Other:
            return it.xml;
        }
        return o.out;
    }

    std::string sourceXml(const Source& s, const std::string& indent, const std::string& eol) {
        Lines o;
        o.indent = indent;
        o.eol = eol;
        o.line(0, "<" + s.tag + " name=\"" + escAttr(s.name) + "\">");
        if (s.language == domain::PouLanguage::ST || s.language == domain::PouLanguage::IL) {
            const std::string tag = s.innerTag.empty() ? std::string("STSource") : s.innerTag;
            std::string text = escText(s.body);
            // les fins de ligne du document
            std::string norm;
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '\r') continue;
                if (text[i] == '\n') { norm += eol; continue; }
                norm.push_back(text[i]);
            }
            o.line(1, "<" + tag + ">" + norm + "</" + tag + ">");
        }
        else if (!s.body.empty()) {
            o.raw(1, s.body);
        }
        o.line(0, "</" + s.tag + ">");
        return o.out;
    }

    std::string write(const Chart& c) {
        if (!c.ok) return c.original;
        if (!c.editable || !c.modified()) return c.original;
        std::string out = c.head;
        const std::string netIndent = c.itemIndent.empty() ? std::string{} : c.itemIndent.substr(0, c.itemIndent.size() - 1);
        for (const auto& it : c.items) {
            const std::string gap = it.gap.empty() ? c.eol + c.itemIndent : it.gap;
            out += gap;
            std::string ind = indentOf(gap);
            if (ind.empty()) ind = c.itemIndent;
            out += (it.dirty || it.xml.empty()) ? itemXml(it, ind, c.eol) : it.xml;
        }
        // un reseau vide qui recoit des elements : sa balise de fin sur sa ligne
        if (!c.items.empty() && !c.mid.empty() && c.mid.front() == '<') out += c.eol + netIndent;
        // un reseau vide de nouveau : la fin de ligne seule reste (comme Control Expert)
        if (c.items.empty() && !c.mid.empty() && c.mid.front() != '<') {
            const auto lt = c.mid.find('<');
            out += c.eol + netIndent + c.mid.substr(lt == std::string::npos ? 0 : lt);
        }
        else {
            out += c.mid;
        }
        for (const auto& s : c.sources) {
            const std::string gap = s.gap.empty() ? c.eol + c.sourceIndent : s.gap;
            out += gap;
            std::string ind = indentOf(gap);
            if (ind.empty()) ind = c.sourceIndent;
            out += (s.dirty || s.xml.empty()) ? sourceXml(s, ind, c.eol) : s.xml;
        }
        out += c.tail;
        return out;
    }

    std::string newProgram(const std::string& sectionName, const std::string& task,
                           const std::string& initialStep, int depth, const std::string& eol) {
        const std::string b(static_cast<std::size_t>(std::max(0, depth)), '\t');
        std::string x = "<SFCProgram areaNum=\"0\" operatorCtrl=\"0\">" + eol;
        x += b + "\t<identProgram name=\"" + escAttr(sectionName) + "\" type=\"section\""
           + (task.empty() ? std::string{} : " task=\"" + escAttr(task) + "\"") + "></identProgram>" + eol;
        x += b + "\t<chartSource name=\"Chart\">" + eol;
        x += b + "\t\t<networkSFC>" + eol;
        x += b + "\t\t\t<step stepType=\"initialStep\" stepName=\"" + escAttr(initialStep) + "\">" + eol;
        x += b + "\t\t\t\t" + pos(0, 0) + eol;
        x += b + "\t\t\t\t<literals max=\"\" min=\"\" delay=\"\"></literals>" + eol;
        x += b + "\t\t\t</step>" + eol;
        x += b + "\t\t</networkSFC>" + eol;
        x += b + "\t</chartSource>" + eol;
        x += b + "</SFCProgram>";
        return x;
    }

    // ------------------------------------------------------------- le graphe
    const TransitionLinks* Topology::of(int item) const {
        for (const auto& t : transitions) if (t.item == item) return &t;
        return nullptr;
    }

    std::vector<int> Topology::into(int step) const {
        std::vector<int> out;
        for (const auto& t : transitions)
            if (std::find(t.targets.begin(), t.targets.end(), step) != t.targets.end()) out.push_back(t.item);
        return out;
    }

    std::vector<int> Topology::outOf(int step) const {
        std::vector<int> out;
        for (const auto& t : transitions)
            if (std::find(t.sources.begin(), t.sources.end(), step) != t.sources.end()) out.push_back(t.item);
        return out;
    }

    Topology resolve(const Chart& c) {
        Topology t;
        const auto& items = c.items;
        const int n = static_cast<int>(items.size());
        std::map<std::pair<int, int>, int> cell;
        std::vector<int> bars;
        for (int i = 0; i < n; ++i) {
            const auto& it = items[static_cast<std::size_t>(i)];
            if (it.isCellObject()) {
                const auto key = std::make_pair(it.x, it.y);
                if (cell.count(key))
                    t.problems.emplace_back(i, "deux objets dans la m\xC3\xAAme case (" + std::to_string(it.x) + ", "
                                                   + std::to_string(it.y) + ")");
                else cell[key] = i;
            }
            else if (it.isBar()) bars.push_back(i);
        }
        auto objAt = [&](int x, int y) {
            const auto f = cell.find({ x, y });
            return f == cell.end() ? -1 : f->second;
        };
        auto barAt = [&](int x, int y, bool divergence) {
            for (const int b : bars) {
                const auto& it = items[static_cast<std::size_t>(b)];
                if (it.isDivergence() != divergence || it.y != y) continue;
                if (x >= it.x && x < it.x + it.width) return b;
            }
            return -1;
        };
        auto entry = [&](int b) { return items[static_cast<std::size_t>(b)].x + items[static_cast<std::size_t>(b)].relativePos; };
        auto add = [&](int from, int to, int link, int column) {
            for (const auto& e : t.edges)
                if (e.from == from && e.to == to && e.column == column) return;
            t.edges.push_back(Edge{ from, to, link, column });
        };

        for (int i = 0; i < n; ++i) {
            const auto& it = items[static_cast<std::size_t>(i)];
            if (it.kind != ItemKind::Step && it.kind != ItemKind::Transition) continue;
            if (const int j = barAt(it.x, it.y, false); j >= 0) { add(i, j, -1, it.x); continue; }
            if (const int b = barAt(it.x, it.y + 1, true); b >= 0) {
                if (entry(b) == it.x) add(i, b, -1, it.x);
                continue;
            }
            if (const int o = objAt(it.x, it.y + 1); o >= 0) add(i, o, -1, it.x);
        }
        for (const int b : bars) {
            const auto& it = items[static_cast<std::size_t>(b)];
            if (it.isDivergence()) {
                for (int col = it.x; col < it.x + it.width; ++col)
                    if (const int o = objAt(col, it.y); o >= 0) add(b, o, -1, col);
            }
            else {
                const int out = entry(b);
                if (const int d = barAt(out, it.y + 1, true); d >= 0 && entry(d) == out) add(b, d, -1, out);
                else if (const int o = objAt(out, it.y + 1); o >= 0) add(b, o, -1, out);
            }
        }
        for (int i = 0; i < n; ++i) {
            const auto& it = items[static_cast<std::size_t>(i)];
            if (it.kind != ItemKind::Link) continue;
            int from = -1, to = -1, column = -1;
            if (it.srcType == "altBranch" || it.srcType == "parBranch") { from = barAt(it.sx, it.sy, true); column = it.sx; }
            else if (it.srcType == "altJoint" || it.srcType == "parJoint") from = barAt(it.sx, it.sy, false);
            else from = objAt(it.sx, it.sy);
            if (from < 0) from = barAt(it.sx, it.sy, true);
            if (it.dstType == "altJoint" || it.dstType == "parJoint") { to = barAt(it.dx, it.dy, false); column = it.dx; }
            else if (it.dstType == "altBranch" || it.dstType == "parBranch") to = barAt(it.dx, it.dy, true);
            else to = objAt(it.dx, it.dy);
            if (to < 0) { to = barAt(it.dx, it.dy, false); if (to >= 0) column = it.dx; }
            if (from < 0 || to < 0) {
                t.problems.emplace_back(i, "un lien dont une extr\xC3\xA9mit\xC3\xA9 ne touche rien");
                continue;
            }
            add(from, to, i, column);
        }

        std::vector<std::vector<int>> out(static_cast<std::size_t>(n)), in(static_cast<std::size_t>(n));
        for (const auto& e : t.edges) {
            out[static_cast<std::size_t>(e.from)].push_back(e.to);
            in[static_cast<std::size_t>(e.to)].push_back(e.from);
        }

        // les sauts
        for (int i = 0; i < n; ++i) {
            const auto& it = items[static_cast<std::size_t>(i)];
            if (it.kind == ItemKind::Jump && c.findStep(it.name) < 0)
                t.problems.emplace_back(i, "saut vers une \xC3\xA9tape inconnue : " + it.name);
        }

        for (int i = 0; i < n; ++i) {
            const auto& it = items[static_cast<std::size_t>(i)];
            if (it.kind != ItemKind::Transition) continue;
            TransitionLinks tl;
            tl.item = i;
            // en amont
            {
                std::vector<int> todo = in[static_cast<std::size_t>(i)];
                std::set<int> seen;
                while (!todo.empty()) {
                    const int k = todo.back();
                    todo.pop_back();
                    if (!seen.insert(k).second) continue;
                    const auto& o = items[static_cast<std::size_t>(k)];
                    if (o.kind == ItemKind::Step) { tl.sources.push_back(k); continue; }
                    if (o.isBar()) {
                        if (o.kind == ItemKind::ParJoint) tl.andJoin = true;
                        for (const int p : in[static_cast<std::size_t>(k)]) todo.push_back(p);
                    }
                }
            }
            // en aval
            {
                std::vector<int> todo = out[static_cast<std::size_t>(i)];
                std::set<int> seen;
                while (!todo.empty()) {
                    const int k = todo.back();
                    todo.pop_back();
                    if (!seen.insert(k).second) continue;
                    const auto& o = items[static_cast<std::size_t>(k)];
                    if (o.kind == ItemKind::Step) { tl.targets.push_back(k); continue; }
                    if (o.kind == ItemKind::Jump) {
                        if (const int s = c.findStep(o.name); s >= 0) tl.targets.push_back(s);
                        continue;
                    }
                    if (o.isBar()) {
                        if (o.kind == ItemKind::ParBranch) tl.andSplit = true;
                        for (const int p : out[static_cast<std::size_t>(k)]) todo.push_back(p);
                    }
                }
            }
            std::sort(tl.sources.begin(), tl.sources.end());
            std::sort(tl.targets.begin(), tl.targets.end());
            tl.targets.erase(std::unique(tl.targets.begin(), tl.targets.end()), tl.targets.end());
            if (tl.sources.empty()) t.problems.emplace_back(i, "transition sans \xC3\xA9tape d'amont");
            if (tl.targets.empty()) t.problems.emplace_back(i, "transition sans \xC3\xA9tape d'aval");
            if (it.cond == CondKind::None || trimmed(it.condText).empty())
                t.problems.emplace_back(i, "transition sans r\xC3\xA9" "ceptivit\xC3\xA9");
            else if (it.cond == CondKind::Section && !c.source("transitionSource", it.condText))
                t.problems.emplace_back(i, "section de transition introuvable : " + it.condText);
            t.transitions.push_back(std::move(tl));
        }

        // deux etapes qui se suivent sans transition
        for (const auto& e : t.edges) {
            const auto& a = items[static_cast<std::size_t>(e.from)];
            const auto& b = items[static_cast<std::size_t>(e.to)];
            if (a.kind == ItemKind::Step && b.kind == ItemKind::Step)
                t.problems.emplace_back(e.to, "deux \xC3\xA9tapes se suivent sans transition");
            if (a.kind == ItemKind::Transition && b.kind == ItemKind::Transition)
                t.problems.emplace_back(e.to, "deux transitions se suivent sans \xC3\xA9tape");
        }
        // les noms
        std::map<std::string, int> names;
        bool initial = false;
        for (int i = 0; i < n; ++i) {
            const auto& it = items[static_cast<std::size_t>(i)];
            if (it.kind != ItemKind::Step) continue;
            initial = initial || it.stepKind == StepKind::Initial;
            const auto key = upper(it.name);
            if (it.name.empty()) t.problems.emplace_back(i, "\xC3\xA9tape sans nom");
            else if (names.count(key)) t.problems.emplace_back(i, "deux \xC3\xA9tapes s'appellent " + it.name);
            else names[key] = i;
        }
        if (!initial && !c.steps().empty()) t.problems.emplace_back(-1, "pas d'\xC3\xA9tape initiale");
        return t;
    }

    std::string conditionText(const Chart& c, const Item& t) {
        std::string text;
        if (t.cond == CondKind::Section) {
            if (const auto* s = c.source("transitionSource", t.condText)) {
                if (s->language == domain::PouLanguage::ST || s->language == domain::PouLanguage::IL) {
                    std::string flat;
                    bool space = false;
                    for (const char ch : s->body) {
                        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { space = true; continue; }
                        if (space && !flat.empty()) flat.push_back(' ');
                        space = false;
                        flat.push_back(ch);
                    }
                    while (!flat.empty() && flat.back() == ';') flat.pop_back();
                    text = flat;
                }
                else {
                    text = "[" + t.condText + " : " + std::string(domain::toString(s->language)) + "]";
                }
            }
            else text = "[" + t.condText + "]";
        }
        else text = t.condText;
        if (t.invert && !text.empty()) text = "NOT (" + text + ")";
        return text;
    }

} // namespace sfc
