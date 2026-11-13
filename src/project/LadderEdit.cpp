// =============================================================================
//  project/LadderEdit.cpp - 1.51.0 : les gestes de l'editeur de ladder
// =============================================================================
#include "LadderEdit.hpp"

#include "BlockLibrary.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace project::ladder {

    namespace {

        Network* netOf(Diagram& d, int network) {
            if (network < 0 || network >= static_cast<int>(d.networks.size())) return nullptr;
            return &d.networks[static_cast<std::size_t>(network)];
        }

        int colsOf(const Network& n, int row) {
            if (row < 0 || row >= n.rows()) return 0;
            return static_cast<int>(n.cells[static_cast<std::size_t>(row)].size());
        }

        Cell& at(Network& n, int row, int col) {
            return n.cells[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
        }

        bool occupied(const Cell& c) { return c.kind != Kind::Empty && c.kind != Kind::Covered; }

        // Un fil dans une case vide.
        void at_(Network& n, int row, int col) {
            auto& c = n.cells[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
            if (c.kind == Kind::Empty) c.kind = Kind::Wire;
        }

        // L'element qui commence a gauche de (row, col) et la couvre.
        int anchorCol(const Network& n, int row, int col) {
            while (col > 0 && n.cells[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)].kind == Kind::Covered) --col;
            return col;
        }

        void clearElement(Network& n, int row, int col) {
            col = anchorCol(n, row, col);
            const int w = std::max(1, at(n, row, col).width);
            for (int k = 0; k < w && col + k < colsOf(n, row); ++k) at(n, row, col + k) = Cell{};
        }

        // Pose un element de largeur w (efface ce qu'il recouvre). Faux si un bloc est la.
        bool put(Network& n, int row, int col, Cell c, int w) {
            w = std::max(1, w);
            if (row < 0 || row >= n.rows() || col < 0 || col + w > colsOf(n, row)) return false;
            for (int k = 0; k < w; ++k)
                if (n.blockAt(row, col + k) >= 0) return false;
            for (int k = 0; k < w; ++k) {
                auto& here = at(n, row, col + k);
                if (here.kind != Kind::Empty) clearElement(n, row, col + k);
            }
            c.width = w;
            at(n, row, col) = std::move(c);
            for (int k = 1; k < w; ++k) {
                Cell cov;
                cov.kind = Kind::Covered;
                at(n, row, col + k) = std::move(cov);
            }
            n.modified = true;
            return true;
        }

        bool placeCell(Diagram& d, Pos p, Cell c, int w) {
            auto* n = netOf(d, p.network);
            if (!n || !valid(d, p)) return false;
            if (!put(*n, p.row, p.col, std::move(c), w)) return false;
            autoWire(d, p.network, p.row);
            return true;
        }

        Cell* elementAt(Diagram& d, Pos p) {
            auto* n = netOf(d, p.network);
            if (!n || !valid(d, p)) return nullptr;
            const int a = anchorCol(*n, p.row, p.col);
            return &at(*n, p.row, a);
        }

        bool overlaps(const Network& n, const Block& b, int ignore) {
            for (int r = b.y; r < b.y + b.height; ++r)
                for (int c = b.x; c < b.x + b.cols; ++c) {
                    if (r < n.rows() && c < colsOf(n, r) && occupied(n.cells[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)])
                        && n.cells[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)].kind != Kind::Wire)
                        return true;
                    for (std::size_t i = 0; i < n.blocks.size(); ++i)
                        if (static_cast<int>(i) != ignore && n.blocks[i].covers(r, c)) return true;
                }
            return false;
        }

        // Une rangee qu'aucune liaison verticale ne relie a celle du dessus : elle
        // part du rail (le haut d'un reseau, une autre ligne independante).
        bool isFirstRow(const Network& n, int row) {
            if (row <= 0) return true;
            for (auto l : n.links[static_cast<std::size_t>(row - 1)])
                if (l) return false;
            return true;
        }

        // Les fils sous un bloc s'effacent (le bloc les remplace).
        void clearUnder(Network& n, const Block& b) {
            for (int r = b.y; r < b.y + b.height && r < n.rows(); ++r)
                for (int c = b.x; c < b.x + b.cols && c < colsOf(n, r); ++c)
                    if (at(n, r, c).kind == Kind::Wire) at(n, r, c) = Cell{};
        }

    } // namespace

    bool valid(const Diagram& d, Pos p) noexcept {
        if (p.network < 0 || p.network >= static_cast<int>(d.networks.size())) return false;
        const auto& n = d.networks[static_cast<std::size_t>(p.network)];
        return p.row >= 0 && p.row < n.rows() && p.col >= 0 && p.col < colsOf(n, p.row);
    }

    const Cell* cellAt(const Diagram& d, Pos p) noexcept {
        if (!valid(d, p)) return nullptr;
        return &d.networks[static_cast<std::size_t>(p.network)].cells[static_cast<std::size_t>(p.row)][static_cast<std::size_t>(p.col)];
    }

    Pos anchorOf(const Diagram& d, Pos p) noexcept {
        if (!valid(d, p)) return p;
        p.col = anchorCol(d.networks[static_cast<std::size_t>(p.network)], p.row, p.col);
        return p;
    }

    // ------------------------------------------------------------------ poser
    bool placeContact(Diagram& d, Pos p, ContactType type, std::string name) {
        Cell c;
        c.kind = Kind::Contact;
        c.contact = type;
        c.name = std::move(name);
        return placeCell(d, p, std::move(c), 1);
    }
    bool placeCoil(Diagram& d, Pos p, CoilType type, std::string name) {
        Cell c;
        c.kind = Kind::Coil;
        c.coil = type;
        c.name = std::move(name);
        return placeCell(d, p, std::move(c), 1);
    }
    bool placeCompare(Diagram& d, Pos p, std::string expression, int width) {
        Cell c;
        c.kind = Kind::Compare;
        c.text = std::move(expression);
        return placeCell(d, p, std::move(c), width);
    }
    bool placeOperate(Diagram& d, Pos p, std::string statement, int width) {
        Cell c;
        c.kind = Kind::Operate;
        c.text = std::move(statement);
        return placeCell(d, p, std::move(c), width);
    }
    bool placeWire(Diagram& d, Pos p) {
        Cell c;
        c.kind = Kind::Wire;
        auto* n = netOf(d, p.network);
        if (!n || !valid(d, p)) return false;
        return put(*n, p.row, p.col, std::move(c), 1);
    }
    bool placeJump(Diagram& d, Pos p, std::string label) {
        Cell c;
        c.kind = Kind::Jump;
        c.name = std::move(label);
        return placeCell(d, p, std::move(c), 1);
    }
    bool placeReturn(Diagram& d, Pos p) {
        Cell c;
        c.kind = Kind::Return;
        return placeCell(d, p, std::move(c), 1);
    }
    bool placeLabel(Diagram& d, Pos p, std::string label) {
        Cell c;
        c.kind = Kind::Label;
        c.name = std::move(label);
        return placeCell(d, p, std::move(c), 1);
    }

    void fitBlock(Block& b) {
        // EN / ENO : des broches ecrites, les premieres de chaque cote (comme Control Expert).
        auto has = [&](const char* formal) {
            for (const auto& p : b.pins)
                if (p.formal == formal) return true;
            return false;
        };
        if (b.enEno) {
            if (!has("EN")) b.pins.insert(b.pins.begin(), Pin{ "EN", "", false, PinDir::In, {}, {} });
            if (!has("ENO")) {
                auto at = b.pins.begin();
                while (at != b.pins.end() && at->dir != PinDir::Out) ++at;
                b.pins.insert(at, Pin{ "ENO", "", false, PinDir::Out, {}, {} });
            }
        }
        // L'en-tete (le type, l'instance), puis une rangee par broche.
        b.height = 1 + std::max(1, std::max(b.inputCount(), b.outputCount()));
        b.cols = std::max(2, b.cols);
        // La largeur dans les unites de Control Expert (il la refait a l'import).
        std::size_t in = 0, out = 0;
        for (const auto& p : b.pins) (p.dir == PinDir::Out ? out : in) = std::max(p.dir == PinDir::Out ? out : in, p.formal.size());
        b.width = std::max<int>(10, static_cast<int>(std::max(in + out + 5, b.type.size() + 4)));
    }

    int placeBlock(Diagram& d, Pos p, Block b) {
        auto* n = netOf(d, p.network);
        if (!n || p.row < 0 || p.col < 0) return -1;
        if (b.height < 2 || b.cols < 1) fitBlock(b);
        if (p.col + b.cols > d.columns) return -1;
        // Une fonction (EFB) porte un nom d'instance ".N", libre dans la section.
        if (b.isFunction()) {
            int next = 1;
            for (const auto& net : d.networks)
                for (const auto& o : net.blocks)
                    if (o.instance.size() > 1 && o.instance.front() == '.')
                        next = std::max(next, std::atoi(o.instance.c_str() + 1) + 1);
            b.instance = "." + std::to_string(next);
        }
        // `p` est la rangee de sa premiere broche (la ligne ou le courant arrive) :
        // le bloc commence une rangee plus haut (son en-tete), comme Control Expert.
        auto rowFree = [&](int r) {
            if (r < 0 || r >= n->rows()) return true;
            for (int c = p.col; c < p.col + b.cols; ++c) {
                const auto& here = at(*n, r, c);
                if ((occupied(here) && here.kind != Kind::Wire) || n->blockAt(r, c) >= 0) return false;
            }
            return true;
        };
        if (p.row == 0 || !rowFree(p.row - 1)) {
            if (!insertRow(d, p.network, p.row)) return -1;      // un en-tete libre
            n = netOf(d, p.network);
            ++p.row;
        }
        for (int k = 1; k < b.height - 1; ++k)
            if (!rowFree(p.row + k)) {
                if (!insertRow(d, p.network, p.row + k)) return -1;
                n = netOf(d, p.network);
            }
        b.x = p.col;
        b.y = p.row - 1;
        d.ensureRows(*n, b.y + b.height);
        if (overlaps(*n, b, -1)) return -1;
        clearUnder(*n, b);
        n->blocks.push_back(std::move(b));
        n->modified = true;
        // Les fils jusqu'a sa premiere broche, et de sa premiere sortie.
        autoWire(d, p.network, p.row);
        return static_cast<int>(n->blocks.size()) - 1;
    }

    // ------------------------------------------------------------------ changer
    bool setName(Diagram& d, Pos p, std::string name) {
        auto* n = netOf(d, p.network);
        if (!n || !valid(d, p)) return false;
        if (const int bi = n->blockAt(p.row, p.col); bi >= 0) {
            auto& b = n->blocks[static_cast<std::size_t>(bi)];
            if (b.instance.empty() && !name.empty()) return false;   // une fonction n'a pas d'instance
            b.instance = std::move(name);
            n->modified = true;
            return true;
        }
        auto* c = elementAt(d, p);
        if (!c) return false;
        if (c->kind != Kind::Contact && c->kind != Kind::Coil && c->kind != Kind::Jump && c->kind != Kind::Label) return false;
        c->name = std::move(name);
        n->modified = true;
        return true;
    }

    bool setText(Diagram& d, Pos p, std::string text) {
        auto* c = elementAt(d, p);
        if (!c || (c->kind != Kind::Compare && c->kind != Kind::Operate)) return false;
        c->text = std::move(text);
        d.networks[static_cast<std::size_t>(p.network)].modified = true;
        return true;
    }

    bool setPin(Diagram& d, int network, int block, int pin, std::string effective) {
        auto* n = netOf(d, network);
        if (!n || block < 0 || block >= static_cast<int>(n->blocks.size())) return false;
        auto& b = n->blocks[static_cast<std::size_t>(block)];
        if (pin < 0 || pin >= static_cast<int>(b.pins.size())) return false;
        b.pins[static_cast<std::size_t>(pin)].effective = std::move(effective);
        n->modified = true;
        return true;
    }

    bool setPinInverted(Diagram& d, int network, int block, int pin, bool inverted) {
        auto* n = netOf(d, network);
        if (!n || block < 0 || block >= static_cast<int>(n->blocks.size())) return false;
        auto& b = n->blocks[static_cast<std::size_t>(block)];
        if (pin < 0 || pin >= static_cast<int>(b.pins.size())) return false;
        b.pins[static_cast<std::size_t>(pin)].inverted = inverted;
        n->modified = true;
        return true;
    }

    bool setContactType(Diagram& d, Pos p, ContactType t) {
        auto* c = elementAt(d, p);
        if (!c || c->kind != Kind::Contact) return false;
        c->contact = t;
        d.networks[static_cast<std::size_t>(p.network)].modified = true;
        return true;
    }

    bool setCoilType(Diagram& d, Pos p, CoilType t) {
        auto* c = elementAt(d, p);
        if (!c || c->kind != Kind::Coil) return false;
        c->coil = t;
        d.networks[static_cast<std::size_t>(p.network)].modified = true;
        return true;
    }

    bool cycleType(Diagram& d, Pos p) {
        auto* c = elementAt(d, p);
        if (!c) return false;
        if (c->kind == Kind::Contact) {
            c->contact = static_cast<ContactType>((static_cast<int>(c->contact) + 1) % 4);
        }
        else if (c->kind == Kind::Coil) {
            c->coil = static_cast<CoilType>((static_cast<int>(c->coil) + 1) % 6);   // sans call / halt
        }
        else return false;
        d.networks[static_cast<std::size_t>(p.network)].modified = true;
        return true;
    }

    bool invert(Diagram& d, Pos p) {
        auto* c = elementAt(d, p);
        if (!c) return false;
        if (c->kind == Kind::Contact) {
            c->contact = c->contact == ContactType::Open ? ContactType::Closed
                       : c->contact == ContactType::Closed ? ContactType::Open
                       : c->contact == ContactType::Rising ? ContactType::Falling : ContactType::Rising;
        }
        else if (c->kind == Kind::Coil) {
            switch (c->coil) {
            case CoilType::Normal: c->coil = CoilType::Negated; break;
            case CoilType::Negated: c->coil = CoilType::Normal; break;
            case CoilType::Set: c->coil = CoilType::Reset; break;
            case CoilType::Reset: c->coil = CoilType::Set; break;
            case CoilType::Rising: c->coil = CoilType::Falling; break;
            case CoilType::Falling: c->coil = CoilType::Rising; break;
            default: return false;
            }
        }
        else return false;
        d.networks[static_cast<std::size_t>(p.network)].modified = true;
        return true;
    }

    // ------------------------------------------------------------------ effacer
    bool erase(Diagram& d, Pos p) {
        auto* n = netOf(d, p.network);
        if (!n || !valid(d, p)) return false;
        if (const int bi = n->blockAt(p.row, p.col); bi >= 0) {
            n->blocks.erase(n->blocks.begin() + bi);
            n->modified = true;
            return true;
        }
        const int a = anchorCol(*n, p.row, p.col);
        const auto& c = at(*n, p.row, a);
        if (c.kind == Kind::Empty) return false;
        const int w = std::max(1, c.width);
        const int cols = colsOf(*n, p.row);
        const bool wasWire = c.kind == Kind::Wire;
        const bool left = a == 0 || occupied(at(*n, p.row, a - 1)) || n->link(p.row, a) || n->link(p.row - 1, a)
            || n->blockAt(p.row, a - 1) >= 0;
        const bool right = a + w < cols && (occupied(at(*n, p.row, a + w)) || n->link(p.row, a + w)
                                            || n->link(p.row - 1, a + w) || n->blockAt(p.row, a + w) >= 0);
        clearElement(*n, p.row, a);
        if (!wasWire && left && right)
            for (int k = 0; k < w; ++k) {
                Cell wire;
                wire.kind = Kind::Wire;
                at(*n, p.row, a + k) = std::move(wire);
            }
        n->modified = true;
        return true;
    }

    // ------------------------------------------------------------------ liaisons
    bool setLink(Diagram& d, int network, int row, int boundary, bool on) {
        auto* n = netOf(d, network);
        if (!n || row < 0 || row + 1 >= n->rows() || boundary <= 0) return false;   // 0 : le rail
        auto& l = n->links[static_cast<std::size_t>(row)];
        if (boundary >= static_cast<int>(l.size())) return false;
        const std::uint8_t v = on ? 1 : 0;
        if (l[static_cast<std::size_t>(boundary)] == v) return false;
        l[static_cast<std::size_t>(boundary)] = v;
        n->modified = true;
        return true;
    }

    Pos addParallel(Diagram& d, Pos p, int width) {
        auto* n = netOf(d, p.network);
        if (!n || !valid(d, p)) return Pos{ -1, -1, -1 };
        p.col = anchorCol(*n, p.row, p.col);
        const auto& c = at(*n, p.row, p.col);
        if (occupied(c) && c.kind != Kind::Wire) width = std::max(width, c.width);
        width = std::max(1, width);
        const int cols = colsOf(*n, p.row);
        if (p.col + width > cols) return Pos{ -1, -1, -1 };
        // la rangee du dessous, libre sur ces colonnes ; sinon une rangee inseree
        const int t = p.row + 1;
        bool free = t < n->rows();
        if (free)
            for (int k = 0; k < width; ++k) {
                if (occupied(at(*n, t, p.col + k)) || n->blockAt(t, p.col + k) >= 0) free = false;
                if (k > 0 && n->link(p.row, p.col + k)) free = false;
            }
        if (!free) insertRow(d, p.network, t);
        n = netOf(d, p.network);
        Cell contact;
        contact.kind = Kind::Contact;
        put(*n, t, p.col, std::move(contact), 1);
        for (int k = 1; k < width; ++k) {
            Cell wire;
            wire.kind = Kind::Wire;
            put(*n, t, p.col + k, std::move(wire), 1);
        }
        // Les deux liaisons verticales ; a gauche, le rail les relie deja (frontiere 0).
        if (p.col > 0) n->links[static_cast<std::size_t>(p.row)][static_cast<std::size_t>(p.col)] = 1;
        n->links[static_cast<std::size_t>(p.row)][static_cast<std::size_t>(p.col + width)] = 1;
        n->modified = true;
        return Pos{ p.network, t, p.col };
    }

    // ------------------------------------------------------------------ rangees, reseaux
    bool insertRow(Diagram& d, int network, int row) {
        auto* n = netOf(d, network);
        if (!n || row < 0 || row > n->rows()) return false;
        for (const auto& b : n->blocks)
            if (b.y < row && row < b.y + b.height) return false;    // au milieu d'un bloc
        const int cols = n->rows() ? colsOf(*n, 0) : d.columns;
        n->cells.insert(n->cells.begin() + row, std::vector<Cell>(static_cast<std::size_t>(cols)));
        std::vector<std::uint8_t> links(static_cast<std::size_t>(cols) + 1, 0);
        // Une liaison verticale qui traversait l'endroit traverse la rangee neuve.
        if (row > 0 && row < n->rows()) links = n->links[static_cast<std::size_t>(row - 1)];
        n->links.insert(n->links.begin() + row, std::move(links));
        for (auto& b : n->blocks) if (b.y >= row) ++b.y;
        for (auto& c : n->comments) if (c.y >= row) { ++c.y; c.changed = true; }
        if (n->fileRows > row) ++n->fileRows;
        n->modified = true;
        return true;
    }

    bool deleteRow(Diagram& d, int network, int row) {
        auto* n = netOf(d, network);
        if (!n || row < 0 || row >= n->rows()) return false;
        for (const auto& b : n->blocks)
            if (b.y < row && row < b.y + b.height) return false;     // le milieu d'un bloc
        if (n->rows() == 1) {
            for (auto& c : n->cells[0]) c = Cell{};
            std::fill(n->links[0].begin(), n->links[0].end(), std::uint8_t{ 0 });
            n->blocks.clear();
            n->comments.clear();
            n->modified = true;
            return true;
        }
        if (row > 0) {
            auto& above = n->links[static_cast<std::size_t>(row - 1)];
            const auto& mine = n->links[static_cast<std::size_t>(row)];
            for (std::size_t b = 0; b < above.size() && b < mine.size(); ++b)
                above[b] = static_cast<std::uint8_t>(above[b] && mine[b]);
        }
        n->cells.erase(n->cells.begin() + row);
        n->links.erase(n->links.begin() + row);
        n->blocks.erase(std::remove_if(n->blocks.begin(), n->blocks.end(), [&](const Block& b) { return b.y == row; }),
                        n->blocks.end());
        n->comments.erase(std::remove_if(n->comments.begin(), n->comments.end(), [&](const Comment& c) { return c.y == row; }),
                          n->comments.end());
        for (auto& b : n->blocks) if (b.y > row) --b.y;
        for (auto& c : n->comments) if (c.y > row) { --c.y; c.changed = true; }
        if (n->fileRows > row) --n->fileRows;
        n->modified = true;
        return true;
    }

    int insertNetwork(Diagram& d, int at) {
        at = std::clamp(at, 0, static_cast<int>(d.networks.size()));
        auto n = d.makeNetwork(1);
        const int cols = d.columns;
        for (int c = 0; c + 1 < cols; ++c) n.cells[0][static_cast<std::size_t>(c)].kind = Kind::Wire;
        n.cells[0][static_cast<std::size_t>(cols - 1)].kind = Kind::Coil;
        // l'alignement des blancs : chaque reseau a le sien
        while (d.gaps.size() < d.networks.size()) d.gaps.push_back(d.newline + d.indent);
        d.networks.insert(d.networks.begin() + at, std::move(n));
        d.gaps.insert(d.gaps.begin() + at, d.newline + d.indent);
        return at;
    }

    bool deleteNetwork(Diagram& d, int at) {
        if (at < 0 || at >= static_cast<int>(d.networks.size())) return false;
        while (d.gaps.size() < d.networks.size()) d.gaps.push_back(d.newline + d.indent);
        d.networks.erase(d.networks.begin() + at);
        d.gaps.erase(d.gaps.begin() + at);
        return true;
    }

    bool moveNetwork(Diagram& d, int from, int to) {
        const int count = static_cast<int>(d.networks.size());
        if (from < 0 || from >= count || to < 0 || to >= count || from == to) return false;
        auto n = std::move(d.networks[static_cast<std::size_t>(from)]);
        d.networks.erase(d.networks.begin() + from);
        d.networks.insert(d.networks.begin() + to, std::move(n));
        return true;
    }

    bool moveElement(Diagram& d, Pos from, Pos to) {
        if (from.network != to.network || !valid(d, from) || !valid(d, to) || from == to) return false;
        auto* n = netOf(d, from.network);
        if (const int bi = n->blockAt(from.row, from.col); bi >= 0) {
            auto b = n->blocks[static_cast<std::size_t>(bi)];
            const int dx = to.col - from.col, dy = to.row - from.row;
            b.x += dx;
            b.y += dy;
            if (b.x < 0 || b.y < 0 || b.x + b.cols > d.columns) return false;
            d.ensureRows(*n, b.y + b.height);
            if (overlaps(*n, b, bi)) return false;
            clearUnder(*n, b);
            n->blocks[static_cast<std::size_t>(bi)] = std::move(b);
            n->modified = true;
            return true;
        }
        const int a = anchorCol(*n, from.row, from.col);
        Cell c = at(*n, from.row, a);
        if (!occupied(c) || c.kind == Kind::Wire) return false;
        const int w = std::max(1, c.width);
        if (to.col + w > colsOf(*n, to.row)) return false;
        for (int k = 0; k < w; ++k) {
            if (to.row == from.row && to.col + k >= a && to.col + k < a + w) continue;   // sur lui-meme
            const auto& t = at(*n, to.row, to.col + k);
            if ((occupied(t) && t.kind != Kind::Wire) || n->blockAt(to.row, to.col + k) >= 0) return false;
        }
        erase(d, Pos{ from.network, from.row, a });
        n = netOf(d, from.network);
        if (!put(*n, to.row, to.col, std::move(c), w)) return false;
        autoWire(d, to.network, to.row);
        return true;
    }

    // ------------------------------------------------------------------ lignes
    void rungRows(const Diagram& d, int network, int row, int& r0, int& r1) {
        r0 = r1 = row;
        if (network < 0 || network >= static_cast<int>(d.networks.size())) return;
        const auto& n = d.networks[static_cast<std::size_t>(network)];
        auto joined = [&](int r) {
            if (r < 0 || r + 1 >= n.rows()) return false;
            for (auto l : n.links[static_cast<std::size_t>(r)]) if (l) return true;
            for (const auto& b : n.blocks) if (b.y <= r && r + 1 < b.y + b.height) return true;
            return false;
        };
        while (r0 > 0 && joined(r0 - 1)) --r0;
        while (joined(r1)) ++r1;
    }

    int insertRung(Diagram& d, int network, int row) {
        auto* n = netOf(d, network);
        if (!n) return -1;
        bool empty = n->blocks.empty();
        for (const auto& r : n->cells)
            for (const auto& c : r) empty = empty && c.kind == Kind::Empty;
        int at = 0;
        if (!empty) {
            int r0 = 0, r1 = 0;
            rungRows(d, network, std::clamp(row, 0, std::max(0, n->rows() - 1)), r0, r1);
            // la ligne neuve apres la derniere rangee non vide de cette ligne, une rangee vide entre les deux
            if (!insertRow(d, network, r1 + 1)) return -1;
            if (!insertRow(d, network, r1 + 2)) return -1;
            n = netOf(d, network);
            at = r1 + 2;
        }
        d.ensureRows(*n, at + 1);
        const int cols = colsOf(*n, at);
        for (int c = 0; c + 1 < cols; ++c) at_(*n, at, c);
        Cell coil;
        coil.kind = Kind::Coil;
        put(*n, at, cols - 1, std::move(coil), 1);
        n->modified = true;
        return at;
    }

    bool deleteRung(Diagram& d, int network, int row) {
        auto* n = netOf(d, network);
        if (!n || row < 0 || row >= n->rows()) return false;
        int r0 = 0, r1 = 0;
        rungRows(d, network, row, r0, r1);
        const int count = r1 - r0 + 1;
        n->blocks.erase(std::remove_if(n->blocks.begin(), n->blocks.end(),
                                       [&](const Block& b) { return b.y >= r0 && b.y <= r1; }), n->blocks.end());
        n->comments.erase(std::remove_if(n->comments.begin(), n->comments.end(),
                                         [&](const Comment& c) { return c.y >= r0 && c.y <= r1; }), n->comments.end());
        if (n->rows() <= count) {
            // la derniere ligne du reseau : il reste une rangee vide
            n->cells.assign(1, std::vector<Cell>(static_cast<std::size_t>(d.columns)));
            n->links.assign(1, std::vector<std::uint8_t>(static_cast<std::size_t>(d.columns) + 1, 0));
            n->blocks.clear();
            n->comments.clear();
            n->fileRows = std::min(n->fileRows, 1);
            n->modified = true;
            return true;
        }
        n->cells.erase(n->cells.begin() + r0, n->cells.begin() + r1 + 1);
        n->links.erase(n->links.begin() + r0, n->links.begin() + r1 + 1);
        if (r0 > 0) std::fill(n->links[static_cast<std::size_t>(r0 - 1)].begin(), n->links[static_cast<std::size_t>(r0 - 1)].end(), std::uint8_t{ 0 });
        for (auto& b : n->blocks) if (b.y > r1) b.y -= count;
        for (auto& c : n->comments) if (c.y > r1) { c.y -= count; c.changed = true; }
        n->fileRows = std::max(0, n->fileRows - count);
        n->modified = true;
        // une rangee vide qui la separait de la precedente
        auto rowEmpty = [&](int r) {
            if (r < 0 || r >= n->rows()) return false;
            for (const auto& c : n->cells[static_cast<std::size_t>(r)]) if (c.kind != Kind::Empty) return false;
            for (const auto& b : n->blocks) if (b.y <= r && r < b.y + b.height) return false;
            for (const auto& c : n->comments) if (c.y <= r && r < c.y + c.height) return false;
            return true;
        };
        if (r0 > 0 && rowEmpty(r0 - 1) && n->rows() > 1) deleteRow(d, network, r0 - 1);
        else if (r0 == 0 && rowEmpty(0) && n->rows() > 1) deleteRow(d, network, 0);
        return true;
    }

    // ------------------------------------------------------------------ blocs
    bool makeBlock(const domain::Project* project, std::string_view type, std::string instance, bool enEno, Block& out) {
        Block b;
        b.type = std::string(type);
        b.enEno = enEno;
        auto pin = [](std::string name, PinDir dir) { return Pin{ std::move(name), "", false, dir, {}, {} }; };
        bool found = false;
        // un DFB du projet
        if (project)
            for (const auto& pou : project->pous) {
                if (pou.kind != domain::PouKind::FunctionBlockType) continue;
                const auto name = project->strings.text(pou.name);
                if (name.size() != type.size()) continue;
                bool same = true;
                for (std::size_t i = 0; i < name.size(); ++i)
                    same = same && std::toupper(static_cast<unsigned char>(name[i])) == std::toupper(static_cast<unsigned char>(type[i]));
                if (!same) continue;
                b.type = std::string(name);
                std::vector<Pin> ins, outs;
                for (auto vi : pou.parameters) {
                    if (vi >= project->variables.size()) continue;
                    const auto& v = project->variables[vi];
                    const auto pn = std::string(project->strings.text(v.name));
                    if (v.scope == domain::VariableScope::Output) outs.push_back(pin(pn, PinDir::Out));
                    else if (v.scope == domain::VariableScope::InOut) ins.push_back(pin(pn, PinDir::InOut));
                    else ins.push_back(pin(pn, PinDir::In));
                }
                b.pins = ins;
                b.pins.insert(b.pins.end(), outs.begin(), outs.end());
                found = true;
                break;
            }
        if (!found)
            if (const auto* def = BlockLibrary::shared().find(type)) {
                b.type = def->name;
                std::vector<Pin> ins, outs;
                for (const auto& p : def->parameters) {
                    if (p.direction == BlockParameter::Direction::Out) outs.push_back(pin(p.name, PinDir::Out));
                    else if (p.direction == BlockParameter::Direction::InOut) ins.push_back(pin(p.name, PinDir::InOut));
                    else ins.push_back(pin(p.name, PinDir::In));
                }
                if (def->kind == BlockKind::Function && outs.empty()) outs.push_back(pin("OUT", PinDir::Out));
                b.pins = ins;
                b.pins.insert(b.pins.end(), outs.begin(), outs.end());
                if (def->kind == BlockKind::Function) instance.clear();
                found = true;
            }
        if (!found) {
            // les EF operateurs, meme sans la bibliotheque : ADD, GT_INT, AND_BOOL, MOVE...
            std::string up(type);
            for (auto& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            const auto base = up.substr(0, up.find('_'));
            static constexpr std::string_view kTwo[] = { "ADD", "SUB", "MUL", "DIV", "MOD", "AND", "OR", "XOR",
                                                         "GT", "GE", "EQ", "NE", "LE", "LT" };
            bool two = false;
            for (auto k : kTwo) two = two || base == k;
            if (two) b.pins = { pin("IN1", PinDir::In), pin("IN2", PinDir::In), pin("OUT", PinDir::Out) };
            else if (base == "NOT" || base == "MOVE") b.pins = { pin("IN", PinDir::In), pin("OUT", PinDir::Out) };
            else return false;
            b.type = up;
            instance.clear();
        }
        b.instance = std::move(instance);
        fitBlock(b);
        out = std::move(b);
        return true;
    }

    std::string freeInstanceName(const domain::Project* project, const Diagram& d, std::string_view type) {
        auto taken = [&](const std::string& name) {
            auto same = [&](std::string_view a) {
                if (a.size() != name.size()) return false;
                for (std::size_t i = 0; i < a.size(); ++i)
                    if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(name[i]))) return false;
                return true;
            };
            if (project)
                for (const auto& v : project->variables)
                    if (same(project->strings.text(v.name))) return true;
            for (const auto& n : d.networks)
                for (const auto& b : n.blocks)
                    if (same(b.instance)) return true;
            return false;
        };
        std::string base(type);
        for (int k = 1; k < 10000; ++k) {
            auto name = base + "_" + std::to_string(k);
            if (!taken(name)) return name;
        }
        return base + "_X";
    }

    // ------------------------------------------------------------------ commentaire
    std::string networkComment(const Diagram& d, int network) {
        if (network < 0 || network >= static_cast<int>(d.networks.size())) return {};
        const auto& n = d.networks[static_cast<std::size_t>(network)];
        return n.comments.empty() ? std::string{} : n.comments.front().text;
    }

    bool setNetworkComment(Diagram& d, int network, std::string text) {
        auto* n = netOf(d, network);
        if (!n) return false;
        if (!n->comments.empty()) {
            if (n->comments.front().text == text) return false;
            if (text.empty()) n->comments.erase(n->comments.begin());
            else {
                n->comments.front().text = std::move(text);
                n->comments.front().changed = true;
            }
            n->modified = true;
            return true;
        }
        if (text.empty()) return false;
        insertRow(d, network, 0);
        n = netOf(d, network);
        Comment c;
        c.x = 0;
        c.y = 0;
        c.width = d.columns;
        c.height = 1;
        c.text = std::move(text);
        c.changed = true;
        n->comments.insert(n->comments.begin(), std::move(c));
        n->modified = true;
        return true;
    }

    // ------------------------------------------------------------------ fils
    void autoWire(Diagram& d, int network, int row) {
        auto* n = netOf(d, network);
        if (!n || row < 0 || row >= n->rows()) return;
        const int cols = colsOf(*n, row);
        int first = -1, last = -1;
        for (int c = 0; c < cols; ++c) {
            const auto& cell = at(*n, row, c);
            if (!occupied(cell) && cell.kind != Kind::Covered) continue;
            if (n->blockAt(row, c) >= 0) continue;
            if (first < 0) first = c;
            last = c;
        }
        // Un bloc sur cette rangee : ses broches cablees comptent comme des elements.
        for (const auto& b : n->blocks) {
            if (row <= b.y || row >= b.y + b.height) continue;   // ses rangees de broches, pas l'en-tete
            if (first < 0 || b.x < first) first = b.x;
            last = std::max(last, b.x + b.cols - 1);
        }
        if (last < 0) return;
        // La rangee du haut part du rail ; une branche, de sa premiere liaison verticale.
        if (row == 0 || isFirstRow(*n, row)) first = 0;
        else
            for (int b = 0; b <= cols; ++b)
                if (n->link(row - 1, b)) { first = std::min(first, b); break; }
        for (int c = std::max(0, first); c < last; ++c) {
            auto& cell = at(*n, row, c);
            if (cell.kind != Kind::Empty || n->blockAt(row, c) >= 0) continue;
            cell.kind = Kind::Wire;
            n->modified = true;
        }
    }

    // ------------------------------------------------------------------ annulation
    LadderEditCommand::LadderEditCommand(ProjectPtr project, domain::Index section, std::string body, std::string label)
        : project_(std::move(project)), section_(section), newBody_(std::move(body)), label_(std::move(label)) {
        if (label_.empty()) label_ = "Modifier le ladder";
    }

    core::Status LadderEditCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "no project");
        if (section_ >= project_->sections.size())
            return core::fail(core::ErrorCode::OutOfRange, "the section does not exist");
        auto& s = project_->sections[section_];
        if (!captured_) { oldBody_ = s.body; captured_ = true; }
        s.body = newBody_;
        std::uint32_t rows = 0;
        for (std::size_t at = s.body.find("<typeLine>"); at != std::string::npos; at = s.body.find("<typeLine>", at + 1)) ++rows;
        s.lineCount = rows;
        s.statementCount = 0;
        return core::ok();
    }

    core::Status LadderEditCommand::undo() {
        if (!project_ || section_ >= project_->sections.size() || !captured_) return core::ok();
        auto& s = project_->sections[section_];
        s.body = oldBody_;
        std::uint32_t rows = 0;
        for (std::size_t at = s.body.find("<typeLine>"); at != std::string::npos; at = s.body.find("<typeLine>", at + 1)) ++rows;
        s.lineCount = rows;
        return core::ok();
    }

} // namespace project::ladder
