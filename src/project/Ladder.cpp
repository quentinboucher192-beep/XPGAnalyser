// =============================================================================
//  project/Ladder.cpp - 1.51.0 : lire et ecrire un ladder de Control Expert
// -----------------------------------------------------------------------------
//  Voir Ladder.hpp. Deux regles tiennent tout le fichier :
//    - ce qui n'a pas change se rend tel que lu (le reseau, ses blancs, ses
//      attributs encodes, un element inconnu) ;
//    - ce qui a change s'ecrit comme Control Expert l'ecrit dans CE fichier
//      (fins de ligne, tabulations, VLink seule ou dans un shortCircuit).
// =============================================================================
#include "Ladder.hpp"

#include "../import/XmlReader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace project::ladder {

    namespace {

        // ------------------------------------------------------------- un arbre
        // Un petit arbre ou chaque noeud garde sa source : un element inconnu est
        // rendu tel quel. Les valeurs d'attribut restent ENCODEES (fidelite).
        struct Node {
            std::string name;
            Attrs       attrs;      // valeurs encodees
            std::string text;       // texte decode (concatene)
            std::vector<Node> kids;
            std::string raw;

            [[nodiscard]] const std::string* rawAttr(std::string_view k) const {
                for (const auto& [n, v] : attrs)
                    if (n == k) return &v;
                return nullptr;
            }
            [[nodiscard]] std::string attr(std::string_view k, std::string fallback = {}) const {
                const auto* v = rawAttr(k);
                return v ? importer::XmlReader::decodeEntities(*v) : fallback;
            }
            [[nodiscard]] int number(std::string_view k, int fallback) const {
                const auto v = attr(k);
                if (v.empty()) return fallback;
                return std::atoi(v.c_str());
            }
            [[nodiscard]] const Node* child(std::string_view n) const {
                for (const auto& k : kids)
                    if (k.name == n) return &k;
                return nullptr;
            }
        };

        // `raw` : un element entier, de sa balise ouvrante a sa balise fermante.
        bool parseNode(std::string_view raw, Node& out) {
            importer::XmlReader r(raw);
            auto ev = r.next();
            if (!ev || *ev != importer::XmlEvent::StartElement) return false;
            out.name = std::string(r.name());
            out.raw = std::string(raw);
            for (const auto& a : r.attributes()) out.attrs.emplace_back(std::string(a.name), std::string(a.rawValue));
            if (r.selfClosing()) return true;
            for (;;) {
                auto e = r.next();
                if (!e) return false;
                if (*e == importer::XmlEvent::EndOfDocument) return false;
                if (*e == importer::XmlEvent::EndElement) return true;
                if (*e == importer::XmlEvent::Text) {
                    out.text += importer::XmlReader::decodeEntities(r.text());
                    continue;
                }
                if (*e == importer::XmlEvent::StartElement) {
                    auto kidRaw = r.readElementRaw();
                    if (!kidRaw) return false;
                    Node kid;
                    if (!parseNode(*kidRaw, kid)) return false;
                    out.kids.push_back(std::move(kid));
                }
            }
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

        bool sameName(std::string_view a, std::string_view b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }

        // ---- l'encodage : attributs (& < > ") et texte (& < >)
        std::string encodeAttr(std::string_view in) {
            std::string out;
            out.reserve(in.size());
            for (char c : in) {
                switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                default:  out.push_back(c);
                }
            }
            return out;
        }
        std::string encodeText(std::string_view in) {
            std::string out;
            out.reserve(in.size());
            for (char c : in) {
                switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                default:  out.push_back(c);
                }
            }
            return out;
        }

        // Les attributs d'un element reecrit : ceux lus, dans leur ordre (valeurs
        // encodees telles quelles), ceux que le modele porte remplaces s'ils ont
        // change, et ceux qui manquent ajoutes a la fin.
        std::string attrsText(const Attrs& read, const std::vector<std::pair<std::string, std::string>>& model) {
            std::string out;
            std::vector<bool> used(model.size(), false);
            for (const auto& [n, raw] : read) {
                std::string value = raw;
                for (std::size_t i = 0; i < model.size(); ++i)
                    if (model[i].first == n) {
                        used[i] = true;
                        if (importer::XmlReader::decodeEntities(raw) != model[i].second) value = encodeAttr(model[i].second);
                    }
                out += ' ' + n + "=\"" + value + '"';
            }
            for (std::size_t i = 0; i < model.size(); ++i)
                if (!used[i]) out += ' ' + model[i].first + "=\"" + encodeAttr(model[i].second) + '"';
            return out;
        }

        const Attrs kNoAttrs{};

    } // namespace

    // ------------------------------------------------------------------ noms des types
    std::string_view contactTypeName(ContactType t) noexcept {
        switch (t) {
        case ContactType::Open:    return "openContact";
        case ContactType::Closed:  return "closedContact";
        case ContactType::Rising:  return "PContact";
        case ContactType::Falling: return "NContact";
        }
        return "openContact";
    }
    std::string_view coilTypeName(CoilType t) noexcept {
        switch (t) {
        case CoilType::Normal:  return "coil";
        case CoilType::Negated: return "notCoil";
        case CoilType::Set:     return "setCoil";
        case CoilType::Reset:   return "resetCoil";
        case CoilType::Rising:  return "PCoil";
        case CoilType::Falling: return "NCoil";
        case CoilType::Call:    return "callCoil";
        case CoilType::Halt:    return "haltCoil";
        }
        return "coil";
    }
    bool contactTypeFrom(std::string_view s, ContactType& out) noexcept {
        if (s == "openContact" || s.empty()) { out = ContactType::Open; return true; }
        if (s == "closedContact" || s == "negatedContact") { out = ContactType::Closed; return true; }
        if (s == "PContact" || s == "positiveTransitionSensingContact") { out = ContactType::Rising; return true; }
        if (s == "NContact" || s == "negativeTransitionSensingContact") { out = ContactType::Falling; return true; }
        return false;
    }
    bool coilTypeFrom(std::string_view s, CoilType& out) noexcept {
        if (s == "coil" || s.empty()) { out = CoilType::Normal; return true; }
        if (s == "notCoil" || s == "negatedCoil") { out = CoilType::Negated; return true; }
        if (s == "setCoil") { out = CoilType::Set; return true; }
        if (s == "resetCoil") { out = CoilType::Reset; return true; }
        if (s == "PCoil" || s == "positiveTransitionSensingCoil") { out = CoilType::Rising; return true; }
        if (s == "NCoil" || s == "negativeTransitionSensingCoil") { out = CoilType::Falling; return true; }
        if (s == "callCoil") { out = CoilType::Call; return true; }
        if (s == "haltCoil") { out = CoilType::Halt; return true; }
        return false;
    }

    // ------------------------------------------------------------------ blocs
    int Block::inputCount() const noexcept {
        int n = 0;
        for (const auto& p : pins) n += p.dir != PinDir::Out ? 1 : 0;
        return n;
    }
    int Block::outputCount() const noexcept {
        int n = 0;
        for (const auto& p : pins) n += p.dir == PinDir::Out ? 1 : 0;
        return n;
    }
    int Block::pinRow(std::size_t pin) const noexcept {
        if (pin >= pins.size()) return y;
        const bool out = pins[pin].dir == PinDir::Out;
        int k = 0;
        for (std::size_t i = 0; i < pin; ++i) k += ((pins[i].dir == PinDir::Out) == out) ? 1 : 0;
        return y + (enEno ? 1 : 0) + k;
    }

    int Network::blockAt(int row, int col) const noexcept {
        for (std::size_t i = 0; i < blocks.size(); ++i)
            if (blocks[i].covers(row, col)) return static_cast<int>(i);
        return -1;
    }

    void Diagram::ensureRows(Network& n, int rows) const {
        while (n.rows() < rows) {
            n.cells.emplace_back(static_cast<std::size_t>(columns));
            n.links.emplace_back(static_cast<std::size_t>(columns) + 1, std::uint8_t{ 0 });
        }
        for (auto& r : n.cells)
            if (static_cast<int>(r.size()) < columns) r.resize(static_cast<std::size_t>(columns));
        for (auto& l : n.links)
            if (static_cast<int>(l.size()) < columns + 1) l.resize(static_cast<std::size_t>(columns) + 1, 0);
    }

    Network Diagram::makeNetwork(int rows) const {
        Network n;
        n.modified = true;
        ensureRows(n, std::max(1, rows));
        return n;
    }

    // ------------------------------------------------------------------ lecture
    namespace {

        struct RowReader {
            const Diagram& d;
            Network&       n;
            int            row;
            int            col{ 0 };

            void ensureCols(int upto) {
                // Une rangee plus large que nbColumns (un fichier incoherent) : la
                // grille de ce reseau s'elargit, rien ne se perd.
                if (upto <= static_cast<int>(n.cells[static_cast<std::size_t>(row)].size())) return;
                for (auto& r : n.cells)
                    if (static_cast<int>(r.size()) < upto) r.resize(static_cast<std::size_t>(upto));
                for (auto& l : n.links)
                    if (static_cast<int>(l.size()) < upto + 1) l.resize(static_cast<std::size_t>(upto) + 1, 0);
            }

            Cell& put(Cell c, int width) {
                width = std::max(1, width);
                ensureCols(col + width);
                auto& r = n.cells[static_cast<std::size_t>(row)];
                c.width = width;
                r[static_cast<std::size_t>(col)] = std::move(c);
                for (int k = 1; k < width; ++k) {
                    Cell cov;
                    cov.kind = Kind::Covered;
                    r[static_cast<std::size_t>(col + k)] = std::move(cov);
                }
                auto& placed = r[static_cast<std::size_t>(col)];
                col += width;
                return placed;
            }

            void link() {
                ensureCols(col);
                n.links[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] = 1;
            }

            void element(const Node& e, Diagram& diagram, std::vector<Block>& blocks);
        };

        Block readBlock(const Node& e) {
            Block b;
            b.attrs = e.attrs;
            b.type = e.attr("typeName");
            b.instance = e.attr("instanceName");
            b.width = std::max(1, e.number("width", 3));
            b.height = std::max(1, e.number("height", 3));
            b.enEno = e.attr("enEnO") == "true";
            b.additionalPins = e.number("additionnalPinNumber", 0);
            for (const auto& k : e.kids) {
                if (k.name == "objPosition") {
                    b.x = std::max(0, k.number("posX", 0));
                    b.y = std::max(0, k.number("posY", 0));
                    continue;
                }
                if (k.name == "descriptionFFB") {
                    b.execAfter = k.attr("execAfter");
                    for (const auto& [an, av] : k.attrs)
                        if (an != "execAfter") b.descAttrsRaw += ' ' + an + "=\"" + av + '"';
                    for (const auto& pin : k.kids) {
                        Pin p;
                        p.element = pin.name;
                        p.attrs = pin.attrs;
                        p.formal = pin.attr("formalParameter");
                        p.effective = pin.attr("effectiveParameter");
                        p.inverted = pin.attr("invertedPin") == "true";
                        p.dir = pin.name == "outputVariable" ? PinDir::Out
                              : pin.name == "inOutVariable"  ? PinDir::InOut
                                                             : PinDir::In;
                        b.pins.push_back(std::move(p));
                    }
                    continue;
                }
                b.extras.push_back(k.raw);
            }
            return b;
        }

        void RowReader::element(const Node& e, Diagram& diagram, std::vector<Block>& blocks) {
            const auto& t = e.name;
            Cell c;
            c.element = t;
            c.attrs = e.attrs;
            if (t == "contact") {
                c.kind = Kind::Contact;
                c.name = e.attr("contactVariableName");
                c.typeText = e.attr("typeContact");
                if (!contactTypeFrom(c.typeText, c.contact)) c.contact = ContactType::Open;
                put(std::move(c), 1);
            }
            else if (t == "coil") {
                c.kind = Kind::Coil;
                c.name = e.attr("coilVariableName");
                c.typeText = e.attr("typeCoil");
                if (!coilTypeFrom(c.typeText, c.coil)) c.coil = CoilType::Normal;
                if (c.name.empty()) c.name = e.attr("label");
                put(std::move(c), 1);
            }
            else if (t == "HLink" || t == "emptyCell") {
                const int nb = std::max(1, e.number("nbCells", 1));
                for (int k = 0; k < nb; ++k) {
                    Cell one;
                    one.kind = t == "HLink" ? Kind::Wire : Kind::Empty;
                    // Fidelite : le premier garde les attributs lus.
                    if (k == 0) { one.element = t; one.attrs = e.attrs; }
                    put(std::move(one), 1);
                }
            }
            else if (t == "VLink") {
                diagram.bareVLink = true;
                link();
            }
            else if (t == "shortCircuit") {
                for (const auto& k : e.kids) {
                    if (k.name == "VLink") { link(); continue; }
                    element(k, diagram, blocks);
                }
            }
            else if (t == "compareBlock" || t == "operateBlock") {
                c.kind = t == "compareBlock" ? Kind::Compare : Kind::Operate;
                const auto* x = e.child(t == "compareBlock" ? "expression" : "statement");
                if (!x && !e.kids.empty()) x = &e.kids.front();
                c.text = x ? trimmed(x->text) : trimmed(e.text);
                put(std::move(c), std::max(1, e.number("width", 2)));
            }
            else if (t == "control") {
                const auto kind = e.attr("typeControl");
                const auto low = upper(kind);
                c.kind = low.find("RET") != std::string::npos ? Kind::Return : Kind::Jump;
                c.typeText = kind;
                c.name = e.attr("label");
                put(std::move(c), 1);
            }
            else if (t == "jumpCoil" || t == "jump") {
                c.kind = Kind::Jump;
                c.name = e.attr("label");
                put(std::move(c), 1);
            }
            else if (t == "returnCoil" || t == "retCoil" || t == "return") {
                c.kind = Kind::Return;
                put(std::move(c), 1);
            }
            else if (t == "labelCell" || t == "label") {
                c.kind = Kind::Label;
                c.name = e.attr("label");
                put(std::move(c), 1);
            }
            else if (t == "FFBBlock") {
                auto b = readBlock(e);
                // Place par sa position ; s'il est ecrit a sa colonne dans la suite de
                // la rangee, il en occupe la largeur.
                const bool inFlow = b.x == col && b.y == row;
                const int w = b.width;
                blocks.push_back(std::move(b));
                if (inFlow) {
                    ensureCols(col + w);
                    col += w;
                }
            }
            else if (t == "objPosition") {
                // une position sur une rangee : rien a placer
            }
            else {
                c.kind = Kind::Raw;
                c.raw = e.raw;
                put(std::move(c), std::max(1, e.number("nbCells", 1)));
            }
        }

        bool readNetwork(const Node& net, Diagram& d, Network& n) {
            int row = 0;
            for (const auto& k : net.kids) {
                if (k.name == "typeLine") {
                    const Node* empty = k.child("emptyLine");
                    if (empty) {
                        const int nb = std::max(1, empty->number("nbRows", 1));
                        d.ensureRows(n, row + nb);
                        row += nb;
                        continue;
                    }
                    d.ensureRows(n, row + 1);
                    RowReader rr{ d, n, row };
                    for (const auto& e : k.kids) rr.element(e, d, n.blocks);
                    ++row;
                    continue;
                }
                if (k.name == "FFBBlock") {
                    n.blocks.push_back(readBlock(k));
                    continue;
                }
                if (k.name == "textBox") {
                    Comment c;
                    c.attrs = k.attrs;
                    c.raw = k.raw;
                    c.changed = false;
                    c.width = std::max(1, k.number("width", 4));
                    c.height = std::max(1, k.number("height", 1));
                    if (const auto* pos = k.child("objPosition")) {
                        c.x = std::max(0, pos->number("posX", 0));
                        c.y = std::max(0, pos->number("posY", 0));
                    }
                    c.text = trimmed(k.text);
                    n.comments.push_back(std::move(c));
                    continue;
                }
                n.extras.push_back(k.raw);
            }
            // Un bloc sous la derniere rangee ecrite : la grille va jusqu'a lui.
            int bottom = std::max(1, n.rows());
            for (const auto& b : n.blocks) bottom = std::max(bottom, b.y + b.height);
            for (const auto& c : n.comments) bottom = std::max(bottom, c.y + c.height);
            d.ensureRows(n, bottom);
            return true;
        }

        std::string lineIndentBefore(std::string_view text) {
            // L'indentation de la derniere ligne de `text` (ce qui suit le dernier \n).
            const auto nl = text.rfind('\n');
            if (nl == std::string_view::npos) return {};
            const auto rest = text.substr(nl + 1);
            std::string out;
            for (char c : rest) {
                if (c != ' ' && c != '\t') break;
                out.push_back(c);
            }
            return out;
        }

    } // namespace

    core::Result<Diagram> parse(std::string_view xml) {
        Diagram d;
        const auto lt = xml.find('<');
        if (lt == std::string_view::npos) return core::fail(core::ErrorCode::XmlMalformed, "pas de <LDSource>");
        importer::XmlReader r(xml);
        auto ev = r.next();
        while (ev && *ev == importer::XmlEvent::Text) ev = r.next();
        if (!ev) return core::fail(core::ErrorCode::XmlMalformed, ev.error().message());
        if (*ev != importer::XmlEvent::StartElement || r.name() != "LDSource")
            return core::fail(core::ErrorCode::XmlUnexpectedRoot, "pas de <LDSource>");
        {
            const auto cols = r.attr("nbColumns");
            if (!cols.empty()) d.columns = std::max(1, std::min(256, std::atoi(cols.c_str())));
        }
        const auto gt = xml.find('>', lt);
        if (gt == std::string_view::npos) return core::fail(core::ErrorCode::XmlMalformed, "<LDSource> incomplet");
        d.startTag = std::string(xml.substr(0, gt + 1));
        d.newline = xml.find("\r\n") != std::string_view::npos ? "\r\n" : (xml.find('\n') != std::string_view::npos ? "\n" : "\r\n");
        if (r.selfClosing()) {
            d.selfClosed = true;
            d.endTag.clear();
            return d;
        }
        std::size_t cursor = gt + 1;
        for (;;) {
            auto e = r.next();
            if (!e) return core::fail(core::ErrorCode::XmlMalformed, e.error().message());
            if (*e == importer::XmlEvent::EndOfDocument)
                return core::fail(core::ErrorCode::XmlMalformed, "</LDSource> manquant");
            if (*e == importer::XmlEvent::EndElement) break;
            if (*e != importer::XmlEvent::StartElement) continue;
            const bool network = r.name() == "networkLD";
            auto raw = r.readElementRaw();
            if (!raw) return core::fail(core::ErrorCode::XmlMalformed, raw.error().message());
            if (!network) continue;        // un enfant inconnu reste dans le blanc qui le contient
            const auto at = xml.find(*raw, cursor);
            if (at == std::string_view::npos) return core::fail(core::ErrorCode::XmlMalformed, "reseau introuvable");
            Node net;
            if (!parseNode(*raw, net)) return core::fail(core::ErrorCode::XmlMalformed, "reseau illisible");
            Network n;
            n.raw = *raw;
            n.modified = false;
            readNetwork(net, d, n);
            d.gaps.emplace_back(xml.substr(cursor, at - cursor));
            d.networks.push_back(std::move(n));
            cursor = at + raw->size();
        }
        const auto close = xml.rfind("</");
        if (close == std::string_view::npos || close < cursor) return core::fail(core::ErrorCode::XmlMalformed, "</LDSource> manquant");
        d.tail = std::string(xml.substr(cursor, close - cursor));
        d.endTag = std::string(xml.substr(close));

        // Les conventions : l'indentation des reseaux et d'un niveau.
        if (!d.gaps.empty()) d.indent = lineIndentBefore(d.gaps.front());
        else d.indent = lineIndentBefore(d.tail) + "\t";
        d.step = "\t";
        if (!d.networks.empty()) {
            const auto& raw = d.networks.front().raw;
            const auto nl = raw.find('\n');
            if (nl != std::string::npos) {
                std::string inner;
                for (std::size_t i = nl + 1; i < raw.size() && (raw[i] == ' ' || raw[i] == '\t'); ++i) inner.push_back(raw[i]);
                if (inner.size() > d.indent.size() && inner.compare(0, d.indent.size(), d.indent) == 0)
                    d.step = inner.substr(d.indent.size());
            }
        }
        return d;
    }

    // ------------------------------------------------------------------ ecriture
    namespace {

        struct Writer {
            const Diagram& d;
            std::string    out;

            void line(int depth, std::string_view s) {
                out += d.newline;
                out += d.indent;
                for (int i = 0; i < depth; ++i) out += d.step;
                out += s;
            }

            // Un element d'une case (pas un fil ni du vide).
            std::string cellText(const Cell& c, int depth, bool& multi) {
                multi = false;
                switch (c.kind) {
                case Kind::Contact: {
                    const auto& base = c.element == "contact" ? c.attrs : kNoAttrs;
                    // La graphie lue du type, si elle dit la meme chose.
                    std::string typeText(contactTypeName(c.contact));
                    ContactType read{};
                    if (!c.typeText.empty() && contactTypeFrom(c.typeText, read) && read == c.contact) typeText = c.typeText;
                    return "<contact" + attrsText(base, { { "typeContact", typeText }, { "contactVariableName", c.name } })
                        + "></contact>";
                }
                case Kind::Coil: {
                    const auto& base = c.element == "coil" ? c.attrs : kNoAttrs;
                    std::string typeText(coilTypeName(c.coil));
                    CoilType read{};
                    if (!c.typeText.empty() && coilTypeFrom(c.typeText, read) && read == c.coil) typeText = c.typeText;
                    std::vector<std::pair<std::string, std::string>> model{ { "typeCoil", typeText } };
                    if (c.coil != CoilType::Halt || !c.name.empty()) model.emplace_back("coilVariableName", c.name);
                    return "<coil" + attrsText(base, model) + "></coil>";
                }
                case Kind::Compare:
                case Kind::Operate: {
                    const bool cmp = c.kind == Kind::Compare;
                    const char* el = cmp ? "compareBlock" : "operateBlock";
                    const auto& base = c.element == el ? c.attrs : kNoAttrs;
                    multi = true;
                    std::string s = std::string("<") + el + attrsText(base, { { "width", std::to_string(c.width) } }) + ">";
                    std::string inner = std::string(cmp ? "<expression>" : "<statement>") + encodeText(c.text)
                        + (cmp ? "</expression>" : "</statement>");
                    std::string text = s;
                    // les lignes suivantes sont ajoutees par l'appelant via line()
                    text += '\x01';
                    text += inner;
                    text += '\x01';
                    text += std::string("</") + el + ">";
                    (void)depth;
                    return text;
                }
                case Kind::Jump: {
                    if (c.element == "control" || c.element.empty()) {
                        std::string typeText = c.typeText.empty() || upper(c.typeText).find("RET") != std::string::npos
                            ? std::string("jump") : c.typeText;
                        return "<control" + attrsText(c.element == "control" ? c.attrs : kNoAttrs,
                                                      { { "typeControl", typeText }, { "label", c.name } }) + "></control>";
                    }
                    return "<" + c.element + attrsText(c.attrs, { { "label", c.name } }) + "></" + c.element + ">";
                }
                case Kind::Return: {
                    if (c.element == "control" || c.element.empty()) {
                        std::string typeText = !c.typeText.empty() && upper(c.typeText).find("RET") != std::string::npos
                            ? c.typeText : std::string("return");
                        return "<control" + attrsText(c.element == "control" ? c.attrs : kNoAttrs,
                                                      { { "typeControl", typeText } }) + "></control>";
                    }
                    return "<" + c.element + attrsText(c.attrs, {}) + "></" + c.element + ">";
                }
                case Kind::Label: {
                    const std::string el = c.element.empty() ? std::string("labelCell") : c.element;
                    return "<" + el + attrsText(c.element.empty() ? kNoAttrs : c.attrs, { { "label", c.name } }) + "></" + el + ">";
                }
                case Kind::Raw:
                    return c.raw;
                default:
                    return {};
                }
            }

            // Une chaine qui peut porter des \x01 (les lignes d'un element sur plusieurs) : ecrite a `depth`.
            void emit(int depth, const std::string& s) {
                if (s.find('\x01') == std::string::npos) { line(depth, s); return; }
                std::size_t from = 0;
                int k = 0;
                for (;;) {
                    const auto at = s.find('\x01', from);
                    const auto piece = s.substr(from, at == std::string::npos ? std::string::npos : at - from);
                    // le premier et le dernier morceau a `depth`, ceux du milieu un cran plus loin
                    const bool last = at == std::string::npos;
                    line(depth + ((k > 0 && !last) ? 1 : 0), piece);
                    if (last) break;
                    from = at + 1;
                    ++k;
                }
            }

            void writeBlock(const Block& b, int depth) {
                std::vector<std::pair<std::string, std::string>> model{
                    { "instanceName", b.instance }, { "typeName", b.type },
                    { "additionnalPinNumber", std::to_string(b.additionalPins) },
                    { "enEnO", b.enEno ? "true" : "false" },
                    { "width", std::to_string(b.width) }, { "height", std::to_string(b.height) } };
                line(depth, "<FFBBlock" + attrsText(b.attrs, model) + ">");
                line(depth + 1, "<objPosition posX=\"" + std::to_string(b.x) + "\" posY=\"" + std::to_string(b.y)
                                    + "\"></objPosition>");
                line(depth + 1, "<descriptionFFB execAfter=\"" + encodeAttr(b.execAfter) + "\"" + b.descAttrsRaw + ">");
                for (const auto& p : b.pins) {
                    const char* el = p.dir == PinDir::Out ? "outputVariable" : p.dir == PinDir::InOut ? "inOutVariable" : "inputVariable";
                    const auto& base = p.element == el ? p.attrs : kNoAttrs;
                    std::vector<std::pair<std::string, std::string>> pm{
                        { "invertedPin", p.inverted ? "true" : "false" }, { "formalParameter", p.formal } };
                    // Un parametre effectif vide n'est pas ecrit (sauf s'il l'etait).
                    bool hadEffective = false;
                    for (const auto& [n, v] : base) hadEffective = hadEffective || n == "effectiveParameter";
                    if (!p.effective.empty() || hadEffective) pm.emplace_back("effectiveParameter", p.effective);
                    line(depth + 2, std::string("<") + el + attrsText(base, pm) + "></" + el + ">");
                }
                line(depth + 1, "</descriptionFFB>");
                for (const auto& x : b.extras) line(depth + 1, x);
                line(depth, "</FFBBlock>");
            }

            // Une rangee : les elements de gauche a droite, les fils et le vide
            // regroupes, une liaison verticale avant l'element de sa frontiere.
            void writeRow(const Network& n, int row) {
                const auto& cells = n.cells[static_cast<std::size_t>(row)];
                const int cols = static_cast<int>(cells.size());
                line(1, "<typeLine>");
                int col = 0;
                std::vector<bool> written(n.blocks.size(), false);
                auto linkAt = [&](int b) { return n.link(row, b); };
                auto openLink = [&](int b, bool& wrapped) {
                    wrapped = false;
                    if (!linkAt(b)) return;
                    if (d.bareVLink) { line(2, "<VLink></VLink>"); return; }
                    line(2, "<shortCircuit>");
                    line(3, "<VLink></VLink>");
                    wrapped = true;
                };
                auto closeLink = [&](bool wrapped) { if (wrapped) line(2, "</shortCircuit>"); };
                while (col < cols) {
                    // un bloc qui commence ici
                    int blockHere = -1;
                    for (std::size_t i = 0; i < n.blocks.size(); ++i)
                        if (n.blocks[i].y == row && n.blocks[i].x == col) blockHere = static_cast<int>(i);
                    if (blockHere >= 0) {
                        bool wrapped = false;
                        openLink(col, wrapped);
                        written[static_cast<std::size_t>(blockHere)] = true;
                        const auto& b = n.blocks[static_cast<std::size_t>(blockHere)];
                        writeBlock(b, wrapped ? 3 : 2);
                        closeLink(wrapped);
                        col += b.width;
                        continue;
                    }
                    const auto& c = cells[static_cast<std::size_t>(col)];
                    const int under = n.blockAt(row, col);
                    const bool covered = under >= 0 || c.kind == Kind::Covered;
                    const bool emptyish = covered || c.kind == Kind::Empty;
                    if (c.kind == Kind::Wire || emptyish) {
                        // une suite de fils (ou de vide), coupee par une liaison verticale ou un bloc
                        const bool wire = c.kind == Kind::Wire && !covered;
                        bool wrapped = false;
                        openLink(col, wrapped);
                        int len = 1;
                        if (!wrapped) {
                            while (col + len < cols && !linkAt(col + len)) {
                                const auto& nx = cells[static_cast<std::size_t>(col + len)];
                                bool blockStarts = false;
                                for (const auto& b : n.blocks) blockStarts = blockStarts || (b.y == row && b.x == col + len);
                                if (blockStarts) break;
                                const bool nxCovered = n.blockAt(row, col + len) >= 0 || nx.kind == Kind::Covered;
                                const bool nxWire = nx.kind == Kind::Wire && !nxCovered;
                                const bool nxEmpty = nxCovered || nx.kind == Kind::Empty;
                                if (wire ? !nxWire : !nxEmpty) break;
                                ++len;
                            }
                        }
                        const char* el = wire ? "HLink" : "emptyCell";
                        const auto& base = (c.element == el && !covered) ? c.attrs : kNoAttrs;
                        line(wrapped ? 3 : 2, std::string("<") + el + attrsText(base, { { "nbCells", std::to_string(len) } })
                                                  + "></" + el + ">");
                        closeLink(wrapped);
                        col += len;
                        continue;
                    }
                    bool wrapped = false;
                    openLink(col, wrapped);
                    bool multi = false;
                    emit(wrapped ? 3 : 2, cellText(c, wrapped ? 3 : 2, multi));
                    closeLink(wrapped);
                    col += std::max(1, c.width);
                }
                // Un bloc hors de la grille (au-dela de nbColumns) : a la fin de sa rangee.
                for (std::size_t i = 0; i < n.blocks.size(); ++i)
                    if (!written[i] && n.blocks[i].y == row) writeBlock(n.blocks[i], 2);
                if (linkAt(cols)) {
                    if (d.bareVLink) line(2, "<VLink></VLink>");
                    else { line(2, "<shortCircuit>"); line(3, "<VLink></VLink>"); line(2, "</shortCircuit>"); }
                }
                line(1, "</typeLine>");
            }

            [[nodiscard]] static bool rowIsEmpty(const Network& n, int row) {
                const auto& cells = n.cells[static_cast<std::size_t>(row)];
                for (const auto& c : cells)
                    if (c.kind != Kind::Empty && c.kind != Kind::Covered) return false;
                for (auto l : n.links[static_cast<std::size_t>(row)])
                    if (l) return false;
                for (const auto& b : n.blocks)
                    if (b.y == row) return false;
                return true;
            }

            void writeNetwork(const Network& n) {
                out += "<networkLD>";
                // la derniere rangee utile
                int last = -1;
                for (int r = 0; r < n.rows(); ++r)
                    if (!rowIsEmpty(n, r)) last = r;
                int r = 0;
                if (last < 0) {
                    line(1, "<typeLine>");
                    line(2, "<emptyLine nbRows=\"1\"></emptyLine>");
                    line(1, "</typeLine>");
                }
                while (r <= last) {
                    if (rowIsEmpty(n, r)) {
                        int k = 1;
                        while (r + k <= last && rowIsEmpty(n, r + k)) ++k;
                        line(1, "<typeLine>");
                        line(2, "<emptyLine nbRows=\"" + std::to_string(k) + "\"></emptyLine>");
                        line(1, "</typeLine>");
                        r += k;
                        continue;
                    }
                    writeRow(n, r);
                    ++r;
                }
                for (const auto& c : n.comments) {
                    if (!c.changed && !c.raw.empty()) { line(1, c.raw); continue; }
                    std::vector<std::pair<std::string, std::string>> model{
                        { "width", std::to_string(c.width) }, { "height", std::to_string(c.height) } };
                    line(1, "<textBox" + attrsText(c.attrs, model) + ">" + encodeText(c.text) + "<objPosition posX=\""
                                + std::to_string(c.x) + "\" posY=\"" + std::to_string(c.y) + "\"></objPosition></textBox>");
                }
                for (const auto& x : n.extras) line(1, x);
                line(0, "</networkLD>");
            }
        };

    } // namespace

    std::string write(const Diagram& d) {
        bool anyChange = d.headerChanged || d.networks.size() != d.gaps.size();
        for (const auto& n : d.networks) anyChange = anyChange || n.modified || n.raw.empty();
        if (!anyChange) {
            if (d.selfClosed) return d.startTag;
            std::string out = d.startTag;
            for (std::size_t i = 0; i < d.networks.size(); ++i) {
                out += d.gaps[i];
                out += d.networks[i].raw;
            }
            out += d.tail;
            out += d.endTag;
            return out;
        }

        Writer w{ d, {} };
        std::string head = d.startTag;
        if (head.empty()) head = "<LDSource nbColumns=\"" + std::to_string(d.columns) + "\">";
        if (d.selfClosed) {
            // <LDSource .../> : rouvert
            const auto slash = head.rfind("/>");
            if (slash != std::string::npos) head = head.substr(0, slash) + ">";
            while (head.size() > 2 && head[head.size() - 2] == ' ') head.erase(head.size() - 2, 1);
        }
        if (d.headerChanged) {
            const auto at = head.find("nbColumns=\"");
            if (at != std::string::npos) {
                const auto end = head.find('"', at + 11);
                if (end != std::string::npos) head.replace(at + 11, end - at - 11, std::to_string(d.columns));
            }
        }
        w.out = head;
        for (std::size_t i = 0; i < d.networks.size(); ++i) {
            const auto& n = d.networks[i];
            if (i < d.gaps.size() && !d.selfClosed) w.out += d.gaps[i];
            else { w.out += d.newline; w.out += d.indent; }
            if (!n.modified && !n.raw.empty()) w.out += n.raw;
            else w.writeNetwork(n);
        }
        if (d.selfClosed || d.endTag.empty()) {
            w.out += d.newline;
            const auto& ind = d.indent;
            w.out += ind.size() >= d.step.size() ? ind.substr(0, ind.size() - d.step.size()) : std::string{};
            w.out += "</LDSource>";
        }
        else {
            w.out += d.tail;
            w.out += d.endTag;
        }
        return w.out;
    }

    std::string emptySourceXml(int columns, int depth) {
        Diagram d;
        d.columns = std::max(1, columns);
        d.startTag = "<LDSource nbColumns=\"" + std::to_string(d.columns) + "\">";
        d.newline = "\r\n";
        d.indent = std::string(static_cast<std::size_t>(std::max(0, depth) + 1), '\t');
        d.networks.push_back(d.makeNetwork(1));
        d.gaps.push_back(d.newline + d.indent);
        d.tail = d.newline + std::string(static_cast<std::size_t>(std::max(0, depth)), '\t');
        d.endTag = "</LDSource>";
        return write(d);
    }

    // ------------------------------------------------------------------ les noms
    namespace {
        bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
        bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
    } // namespace

    std::vector<std::string> namesInExpression(std::string_view e) {
        static constexpr std::string_view kKeywords[] = {
            "AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE", "IF", "THEN", "ELSE", "ELSIF", "END_IF" };
        std::vector<std::string> out;
        const auto n = e.size();
        std::size_t i = 0;
        while (i < n) {
            const char c = e[i];
            if (c == '\'' || c == '"') {
                const auto end = e.find(c, i + 1);
                i = end == std::string_view::npos ? n : end + 1;
                continue;
            }
            if (c == '(' && i + 1 < n && e[i + 1] == '*') {
                const auto end = e.find("*)", i + 2);
                i = end == std::string_view::npos ? n : end + 2;
                continue;
            }
            if (c == '%') {                                        // une adresse : %MW10, %I0.3, %M5
                std::size_t j = i + 1;
                while (j < n && (isIdentChar(e[j]) || e[j] == '.' || e[j] == '[' || e[j] == ']')) ++j;
                out.emplace_back(e.substr(i, j - i));
                i = j;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                while (i < n && (isIdentChar(e[i]) || e[i] == '#' || e[i] == '.')) ++i;
                continue;
            }
            if (!isIdentStart(c)) { ++i; continue; }
            std::size_t j = i;
            while (j < n && isIdentChar(e[j])) ++j;
            if (j < n && e[j] == '#') {                            // t#5s, INT#3
                ++j;
                while (j < n && (isIdentChar(e[j]) || e[j] == '.' || e[j] == '-' || e[j] == ':')) ++j;
                i = j;
                continue;
            }
            const auto word = upper(e.substr(i, j - i));
            bool keyword = false;
            for (auto k : kKeywords) keyword = keyword || word == k;
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
            const bool call = m < n && e[m] == '(' && k == j;
            if (!keyword && !call) out.emplace_back(e.substr(i, k - i));
            i = keyword ? j : k;
        }
        return out;
    }

    std::vector<NameUse> namesUsed(const Diagram& d) {
        std::vector<NameUse> out;
        for (std::size_t ni = 0; ni < d.networks.size(); ++ni) {
            const auto& n = d.networks[ni];
            const int net = static_cast<int>(ni);
            for (int r = 0; r < n.rows(); ++r)
                for (int c = 0; c < static_cast<int>(n.cells[static_cast<std::size_t>(r)].size()); ++c) {
                    const auto& cell = n.cells[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
                    auto add = [&](const std::string& name, bool write) {
                        if (!name.empty()) out.push_back(NameUse{ name, write, net, r, c });
                    };
                    switch (cell.kind) {
                    case Kind::Contact: add(cell.name, false); break;
                    case Kind::Coil:
                        if (cell.writesName()) add(cell.name, true);
                        break;
                    case Kind::Compare:
                        for (const auto& x : namesInExpression(cell.text)) add(x, false);
                        break;
                    case Kind::Operate: {
                        const auto at = cell.text.find(":=");
                        if (at != std::string::npos) {
                            for (const auto& x : namesInExpression(std::string_view(cell.text).substr(0, at))) add(x, true);
                            for (const auto& x : namesInExpression(std::string_view(cell.text).substr(at + 2))) add(x, false);
                        }
                        else for (const auto& x : namesInExpression(cell.text)) add(x, false);
                        break;
                    }
                    default: break;
                    }
                }
            for (const auto& b : n.blocks) {
                if (!b.instance.empty()) out.push_back(NameUse{ b.instance, false, net, b.y, b.x });
                for (std::size_t p = 0; p < b.pins.size(); ++p) {
                    const auto& pin = b.pins[p];
                    if (pin.effective.empty()) continue;
                    const int row = b.pinRow(p);
                    for (const auto& x : namesInExpression(pin.effective)) {
                        out.push_back(NameUse{ x, pin.dir == PinDir::Out, net, row, b.x });
                        if (pin.dir == PinDir::InOut) out.push_back(NameUse{ x, true, net, row, b.x });
                    }
                }
            }
        }
        return out;
    }

    std::string renameInText(std::string_view code, std::string_view from, std::string_view to, std::size_t* count) {
        std::string out;
        out.reserve(code.size());
        std::size_t n = 0, i = 0;
        const auto len = code.size();
        char prev = 0;
        while (i < len) {
            const char c = code[i];
            if (c == '(' && i + 1 < len && code[i + 1] == '*') {
                const auto end = code.find("*)", i + 2);
                const auto stop = end == std::string_view::npos ? len : end + 2;
                out.append(code.substr(i, stop - i));
                i = stop;
                continue;
            }
            if (c == '\'' || c == '"') {
                const auto end = code.find(c, i + 1);
                const auto stop = end == std::string_view::npos ? len : end + 1;
                out.append(code.substr(i, stop - i));
                i = stop;
                prev = c;
                continue;
            }
            if (isIdentStart(c) && prev != '#') {
                std::size_t j = i + 1;
                while (j < len && isIdentChar(code[j])) ++j;
                const auto word = code.substr(i, j - i);
                if (prev != '.' && (j >= len || code[j] != '#') && sameName(word, from)) {
                    out += to;
                    ++n;
                }
                else out += word;
                prev = 'a';
                i = j;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                // un nombre ou un litteral type (16#FF, t#1s) : tel quel
                std::size_t j = i;
                while (j < len && (isIdentChar(code[j]) || code[j] == '#' || code[j] == '.')) ++j;
                out.append(code.substr(i, j - i));
                prev = '0';
                i = j;
                continue;
            }
            if (!std::isspace(static_cast<unsigned char>(c))) prev = c;
            out += c;
            ++i;
        }
        if (count) *count = n;
        return out;
    }

    std::size_t renameVariable(Diagram& d, std::string_view from, std::string_view to) {
        std::size_t total = 0;
        auto renameName = [&](std::string& s) -> bool {
            std::size_t k = 0;
            auto next = renameInText(s, from, to, &k);
            if (k == 0) return false;
            s = std::move(next);
            total += k;
            return true;
        };
        for (auto& n : d.networks) {
            bool touched = false;
            for (auto& row : n.cells)
                for (auto& c : row) {
                    switch (c.kind) {
                    case Kind::Contact:
                    case Kind::Coil:
                        if (c.kind == Kind::Coil && c.coil == CoilType::Call) break;
                        touched = renameName(c.name) || touched;
                        break;
                    case Kind::Compare:
                    case Kind::Operate:
                        touched = renameName(c.text) || touched;
                        break;
                    default: break;
                    }
                }
            for (auto& b : n.blocks) {
                if (sameName(b.instance, from)) {
                    b.instance = std::string(to);
                    ++total;
                    touched = true;
                }
                for (auto& p : b.pins) touched = renameName(p.effective) || touched;
            }
            if (touched) n.modified = true;
        }
        return total;
    }

} // namespace project::ladder
