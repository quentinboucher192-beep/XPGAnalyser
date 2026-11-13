// =============================================================================
//  project/LadderSim.cpp - 1.51.0 : faire tourner un ladder (voir LadderSim.hpp)
// -----------------------------------------------------------------------------
//  Compiler une fois : chaque expression, instruction, broche et variable lue
//  une seule fois ; les sauts resolus ; une memoire de front par contact ou
//  bobine P/N. Puis, a chaque passage, le courant colonne par colonne.
// =============================================================================
#include "LadderSim.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace project::ladder {

    namespace {

        std::string upper(std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }

        std::string trimmed(std::string_view s) {
            const auto a = s.find_first_not_of(" \t\r\n");
            if (a == std::string_view::npos) return {};
            const auto b = s.find_last_not_of(" \t\r\n");
            return std::string(s.substr(a, b - a + 1));
        }

        // Un chemin qui se lit et s'ecrit tel quel : X, X.Y, X[3], %MW10, %I0.3.
        // Un indice calcule (X[i]) passe par l'interpreteur.
        bool simplePath(std::string_view s) {
            if (s.empty()) return false;
            bool inIndex = false;
            for (char c : s) {
                const auto u = static_cast<unsigned char>(c);
                if (inIndex) {
                    if (c == ']') { inIndex = false; continue; }
                    if (!std::isdigit(u)) return false;
                    continue;
                }
                if (c == '[') { inIndex = true; continue; }
                if (!(std::isalnum(u) || c == '_' || c == '.' || c == '%')) return false;
            }
            return !inIndex;
        }

        bool sameName(std::string_view a, std::string_view b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }

        constexpr std::string_view kPinPrefix = "__LDP";

        // Les broches d'une fonction, vues par l'interpreteur comme des variables
        // (__LDP0, __LDP1...) ; le reste passe a l'environnement du simulateur.
        class PinEnv final : public sim::Environment {
        public:
            PinEnv(sim::Environment& base, const std::vector<sim::Value>& pins) : base_(base), pins_(pins) {}
            bool read(std::string_view name, sim::Value& out) override {
                if (const int k = pinIndex(name); k >= 0) {
                    if (static_cast<std::size_t>(k) >= pins_.size()) return false;
                    out = pins_[static_cast<std::size_t>(k)];
                    return true;
                }
                return base_.read(name, out);
            }
            bool write(std::string_view name, const sim::Value& v) override {
                if (pinIndex(name) >= 0) return false;
                return base_.write(name, v);
            }
            bool exists(std::string_view name) override { return pinIndex(name) >= 0 || base_.exists(name); }
            bool call(std::string_view name, std::string_view instance,
                      const std::vector<std::pair<std::string, sim::Value>>& arguments, sim::Value& result) override {
                return base_.call(name, instance, arguments, result);
            }
            bool assignAggregate(std::string_view target, std::string_view source) override {
                return base_.assignAggregate(target, source);
            }
            void report(sim::Diagnostic d) override { base_.report(std::move(d)); }
            bool tolerateUnknownCall(std::string_view name, std::uint32_t line) override {
                return base_.tolerateUnknownCall(name, line);
            }
        private:
            static int pinIndex(std::string_view name) {
                if (name.size() <= kPinPrefix.size()) return -1;
                if (!sameName(name.substr(0, kPinPrefix.size()), kPinPrefix)) return -1;
                int k = 0;
                for (char c : name.substr(kPinPrefix.size())) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
                    k = k * 10 + (c - '0');
                }
                return k;
            }
            sim::Environment&              base_;
            const std::vector<sim::Value>& pins_;
        };

        // Les EFB qui sont des operateurs ST : ADD, GT_INT, AND_BOOL, MOVE...
        bool operatorText(std::string_view type, std::size_t inputs, std::string& out) {
            auto base = upper(type);
            static constexpr std::string_view kOps[] = {
                "ADD", "SUB", "MUL", "DIV", "MOD", "AND", "OR", "XOR", "NOT",
                "GT", "GE", "EQ", "NE", "LE", "LT", "MOVE" };
            auto isOp = [&](std::string_view b) {
                for (auto k : kOps) if (b == k) return true;
                return false;
            };
            if (!isOp(base)) {
                // ADD_INT, GT_REAL, AND_BOOL, MOVE_INT... : le type apres le souligne
                const auto us = base.find('_');
                if (us == std::string::npos || !isOp(std::string_view(base).substr(0, us))) return false;
                const auto suffix = std::string_view(base).substr(us + 1);
                if (sim::typeFromName(suffix) == sim::Type::Unknown && suffix != "ANY" && suffix != "TIME"
                    && suffix != "LREAL" && suffix != "EBOOL")
                    return false;
                base = base.substr(0, us);
            }
            auto p = [](std::size_t k) { return std::string(kPinPrefix) + std::to_string(k); };
            if (base == "MOVE") { if (inputs < 1) return false; out = p(0); return true; }
            if (base == "NOT") { if (inputs < 1) return false; out = "NOT " + p(0); return true; }
            if (inputs < 2) return false;
            const char* op = base == "ADD" ? " + " : base == "SUB" ? " - " : base == "MUL" ? " * " : base == "DIV" ? " / "
                           : base == "MOD" ? " MOD " : base == "AND" ? " AND " : base == "OR" ? " OR " : base == "XOR" ? " XOR "
                           : nullptr;
            if (op) {
                out = "(" + p(0);
                for (std::size_t k = 1; k < inputs; ++k) out += op + p(k);
                out += ")";
                return true;
            }
            const char* cmp = base == "GT" ? " > " : base == "GE" ? " >= " : base == "EQ" ? " = " : base == "NE" ? " <> "
                            : base == "LE" ? " <= " : " < ";
            out.clear();
            for (std::size_t k = 0; k + 1 < inputs; ++k) {
                if (k) out += " AND ";
                out += "(" + p(k) + cmp + p(k + 1) + ")";
            }
            return true;
        }

    } // namespace

    // ------------------------------------------------------------------ programme
    struct CCell {
        int edge{ -1 };                                     // contact / bobine P, N : sa memoire
        bool simple{ true };                                // la variable se lit / s'ecrit telle quelle
        std::shared_ptr<const sim::Expression> expr;        // compare ; contact a indice calcule
        std::shared_ptr<sim::Program> stmt;                 // operate ; bobine a indice calcule (X[i] := __LDP0)
        int jumpTarget{ -1 };
        bool broken{ false };                               // illisible : ne conduit pas
    };

    struct CPin {
        std::shared_ptr<const sim::Expression> expr;        // entree : son parametre effectif
        bool simple{ true };
        std::shared_ptr<sim::Program> assign;               // sortie a indice calcule
        bool connected{ false };                            // un fil arrive (entree) ou part (sortie)
    };

    struct CBlock {
        std::size_t index{ 0 };                             // dans Network::blocks
        bool instance{ false };
        std::shared_ptr<const sim::Expression> fn;          // une fonction : TYPE(__LDP0, ...) ou un operateur
        std::vector<CPin> pins;
        int resultPin{ -1 };                                // la sortie qui recoit le resultat d'une fonction
        bool broken{ false };
    };

    struct CNetwork {
        int rows{ 0 }, cols{ 0 };
        std::vector<CCell> cells;                           // rows * cols
        std::vector<CBlock> blocks;                         // tries par colonne puis rangee
        std::string label;
    };

    class Program {
    public:
        Diagram                  diagram;
        std::string              name;
        std::vector<std::string> warnings;
        std::vector<CNetwork>    networks;
        int                      edges{ 0 };
    };

    const std::vector<std::string>& compileWarnings(const Program& p) noexcept { return p.warnings; }
    const Diagram& diagramOf(const Program& p) noexcept { return p.diagram; }
    const std::string& sectionNameOf(const Program& p) noexcept { return p.name; }

    core::Result<std::shared_ptr<const Program>> compile(const Diagram& d, std::string sectionName) {
        auto prog = std::make_shared<Program>();
        prog->diagram = d;
        prog->name = std::move(sectionName);
        auto warn = [&](int net, std::string what) {
            prog->warnings.push_back("r\xC3\xA9seau " + std::to_string(net + 1) + " : " + std::move(what));
        };

        std::unordered_map<std::string, int> labels;   // etiquette (majuscules) -> reseau
        for (std::size_t ni = 0; ni < d.networks.size(); ++ni)
            for (const auto& row : d.networks[ni].cells)
                for (const auto& c : row)
                    if (c.kind == Kind::Label && !c.name.empty()) labels.emplace(upper(trimmed(c.name)), static_cast<int>(ni));

        for (std::size_t ni = 0; ni < d.networks.size(); ++ni) {
            const auto& n = d.networks[ni];
            const int net = static_cast<int>(ni);
            CNetwork cn;
            cn.rows = n.rows();
            cn.cols = 0;
            for (const auto& r : n.cells) cn.cols = std::max(cn.cols, static_cast<int>(r.size()));
            cn.cells.resize(static_cast<std::size_t>(cn.rows) * static_cast<std::size_t>(cn.cols));
            for (int r = 0; r < cn.rows; ++r)
                for (int c = 0; c < static_cast<int>(n.cells[static_cast<std::size_t>(r)].size()); ++c) {
                    const auto& cell = n.cells[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
                    auto& cc = cn.cells[static_cast<std::size_t>(r * cn.cols + c)];
                    switch (cell.kind) {
                    case Kind::Contact: {
                        const auto name = trimmed(cell.name);
                        if (name.empty()) { cc.broken = true; warn(net, "un contact sans variable"); break; }
                        if (cell.contact == ContactType::Rising || cell.contact == ContactType::Falling) cc.edge = prog->edges++;
                        cc.simple = simplePath(name);
                        if (!cc.simple) {
                            auto e = sim::parseExpression(name);
                            if (e) cc.expr = *e;
                            else { cc.broken = true; warn(net, "contact " + name + " : " + e.error().message()); }
                        }
                        break;
                    }
                    case Kind::Coil: {
                        if (cell.coil == CoilType::Rising || cell.coil == CoilType::Falling) cc.edge = prog->edges++;
                        if (cell.coil == CoilType::Halt) break;
                        const auto name = trimmed(cell.name);
                        if (name.empty()) { cc.broken = true; warn(net, "une bobine sans variable"); break; }
                        if (cell.coil == CoilType::Call) break;
                        cc.simple = simplePath(name);
                        if (!cc.simple) {
                            auto s = sim::parse(name + " := " + std::string(kPinPrefix) + "0;", prog->name);
                            if (s) cc.stmt = *s;
                            else { cc.broken = true; warn(net, "bobine " + name + " : " + s.error().message()); }
                        }
                        break;
                    }
                    case Kind::Compare: {
                        auto e = sim::parseExpression(trimmed(cell.text));
                        if (e) cc.expr = *e;
                        else { cc.broken = true; warn(net, "comparaison [" + cell.text + "] : " + e.error().message()); }
                        break;
                    }
                    case Kind::Operate: {
                        auto text = trimmed(cell.text);
                        if (!text.empty() && text.back() != ';') text += ';';
                        auto s = sim::parse(text, prog->name);
                        if (s) cc.stmt = *s;
                        else { cc.broken = true; warn(net, "op\xC3\xA9ration [" + cell.text + "] : " + s.error().message()); }
                        break;
                    }
                    case Kind::Jump: {
                        const auto it = labels.find(upper(trimmed(cell.name)));
                        if (it == labels.end()) { cc.broken = true; warn(net, "saut vers " + cell.name + " : \xC3\xA9tiquette introuvable"); }
                        else cc.jumpTarget = it->second;
                        break;
                    }
                    case Kind::Label:
                        if (cn.label.empty()) cn.label = cell.name;
                        break;
                    case Kind::Raw:
                        warn(net, "un \xC3\xA9l\xC3\xA9ment inconnu (" + cell.element + ") ne conduit pas");
                        break;
                    default: break;
                    }
                }

            // Les blocs, dans l'ordre ou le courant les atteint : colonne, puis rangee.
            std::vector<std::size_t> order(n.blocks.size());
            for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                return std::make_pair(n.blocks[a].x, n.blocks[a].y) < std::make_pair(n.blocks[b].x, n.blocks[b].y);
            });
            for (auto bi : order) {
                const auto& b = n.blocks[bi];
                CBlock cb;
                cb.index = bi;
                cb.instance = !trimmed(b.instance).empty();
                cb.pins.resize(b.pins.size());
                std::size_t inputs = 0;
                for (std::size_t pi = 0; pi < b.pins.size(); ++pi) {
                    const auto& pin = b.pins[pi];
                    auto& cp = cb.pins[pi];
                    const int row = b.pinRow(pi);
                    const auto eff = trimmed(pin.effective);
                    if (pin.dir != PinDir::Out) {
                        ++inputs;
                        // Un fil arrive : la case a gauche de la broche, ou une liaison verticale a sa frontiere.
                        if (row >= 0 && row < cn.rows) {
                            const int left = b.x - 1;
                            if (b.x == 0) cp.connected = true;
                            else if (left < cn.cols) {
                                const auto& lc = n.cells[static_cast<std::size_t>(row)][static_cast<std::size_t>(left)];
                                cp.connected = (lc.kind != Kind::Empty && lc.kind != Kind::Covered) || n.link(row, b.x)
                                    || (row > 0 && n.link(row - 1, b.x));
                                // la sortie d'un autre bloc
                                for (const auto& o : n.blocks)
                                    if (o.x + o.width == b.x && row >= o.y && row < o.y + o.height) cp.connected = true;
                            }
                        }
                        if (!eff.empty()) {
                            auto e = sim::parseExpression(eff);
                            if (e) cp.expr = *e;
                            else { cb.broken = true; warn(net, "bloc " + b.type + ", " + pin.formal + " : " + e.error().message()); }
                        }
                    }
                    if (pin.dir != PinDir::In) {
                        if (cb.resultPin < 0 && pin.dir == PinDir::Out) cb.resultPin = static_cast<int>(pi);
                        cp.connected = true;
                        if (!eff.empty()) {
                            cp.simple = simplePath(eff);
                            if (!cp.simple) {
                                auto s = sim::parse(eff + " := " + std::string(kPinPrefix) + "0;", prog->name);
                                if (s) cp.assign = *s;
                                else { cb.broken = true; warn(net, "bloc " + b.type + ", " + pin.formal + " : " + s.error().message()); }
                            }
                        }
                    }
                }
                if (!cb.instance) {
                    std::string text;
                    if (!operatorText(b.type, inputs, text)) {
                        text = trimmed(b.type) + "(";
                        for (std::size_t k = 0; k < inputs; ++k) text += (k ? ", " : "") + std::string(kPinPrefix) + std::to_string(k);
                        text += ")";
                    }
                    auto e = sim::parseExpression(text);
                    if (e) cb.fn = *e;
                    else { cb.broken = true; warn(net, "bloc " + b.type + " : " + e.error().message()); }
                }
                cn.blocks.push_back(std::move(cb));
            }
            prog->networks.push_back(std::move(cn));
        }
        return std::shared_ptr<const Program>(std::move(prog));
    }

    core::Result<std::shared_ptr<const Program>> compileXml(std::string_view xml, std::string sectionName) {
        auto d = parse(xml);
        if (!d) return core::Err<core::Error>(d.error());
        return compile(*d, std::move(sectionName));
    }

    // ------------------------------------------------------------------ execution
    namespace {

        struct Runner {
            const Program&    prog;
            sim::Environment& env;
            State&            state;
            const RunOptions& opt;
            RunResult         result;
            int               network{ 0 };

            void fail(std::string message) {
                sim::Diagnostic d;
                d.severity = sim::Diagnostic::Severity::Error;
                d.message = std::move(message);
                d.line = static_cast<std::uint32_t>(network + 1);
                d.section = prog.name;
                env.report(d);
                result.diagnostics.push_back(std::move(d));
                result.ok = false;
            }
            void warn(std::string message) {
                for (const auto& d : result.diagnostics)
                    if (d.message == message) return;
                sim::Diagnostic d;
                d.severity = sim::Diagnostic::Severity::Warning;
                d.message = std::move(message);
                d.line = static_cast<std::uint32_t>(network + 1);
                d.section = prog.name;
                env.report(d);
                result.diagnostics.push_back(std::move(d));
            }
            bool count() {
                ++result.elements;
                if (opt.scanStatements) {
                    ++*opt.scanStatements;
                    if (opt.maxStatements && *opt.scanStatements > opt.maxStatements) {
                        fail("the scan executed more than " + std::to_string(opt.maxStatements) + " statements");
                        return false;
                    }
                }
                return true;
            }

            bool readBool(const Cell& c, const CCell& cc, bool& out) {
                sim::Value v;
                if (cc.expr) {
                    auto r = sim::evaluate(*cc.expr, env);
                    if (!r) { fail(trimmed(c.name) + " : " + r.error().message()); return false; }
                    v = *r;
                }
                else if (!env.read(trimmed(c.name), v)) {
                    fail("'" + trimmed(c.name) + "' is not declared");
                    return false;
                }
                out = v.isTruthy();
                return true;
            }

            bool writeValue(const std::string& name, const std::shared_ptr<sim::Program>& assign, const sim::Value& v) {
                if (opt.captureCoil.size() && sameName(name, opt.captureCoil)) {
                    result.captured = true;
                    result.value = result.value || v.isTruthy();
                    return true;
                }
                if (assign) {
                    std::vector<sim::Value> pins{ v };
                    PinEnv pe(env, pins);
                    sim::RunLimits limits;
                    limits.scanStatements = opt.scanStatements;
                    limits.maxScanStatements = opt.maxStatements;
                    const auto r = sim::execute(*assign, pe, limits);
                    if (!r.completed) { result.ok = false; return false; }
                    return true;
                }
                if (!env.write(name, v)) {
                    fail("'" + name + "' cannot be written (not declared)");
                    return false;
                }
                return true;
            }

            std::uint8_t& edgeMemory(int k) {
                if (state.previous.size() < static_cast<std::size_t>(prog.edges))
                    state.previous.resize(static_cast<std::size_t>(prog.edges), 0);
                return state.previous[static_cast<std::size_t>(k)];
            }

            // Un element : le courant `in` a sa gauche ; rend le courant a sa droite.
            bool cell(const Cell& c, const CCell& cc, bool in, bool& out, std::uint8_t& live, int& jump, bool& stop) {
                out = false;
                live = 0;
                switch (c.kind) {
                case Kind::Wire:
                case Kind::Label:
                    out = in;
                    live = in ? 1 : 0;
                    return true;
                case Kind::Contact: {
                    if (!count()) return false;
                    if (cc.broken) return true;
                    bool v = false;
                    if (!readBool(c, cc, v)) return false;
                    bool pass = false;
                    switch (c.contact) {
                    case ContactType::Open: pass = v; break;
                    case ContactType::Closed: pass = !v; break;
                    case ContactType::Rising: {
                        auto& m = edgeMemory(cc.edge);
                        pass = v && !m;
                        m = v ? 1 : 0;
                        break;
                    }
                    case ContactType::Falling: {
                        auto& m = edgeMemory(cc.edge);
                        pass = !v && m;
                        m = v ? 1 : 0;
                        break;
                    }
                    }
                    live = pass ? 1 : 0;
                    out = in && pass;
                    return true;
                }
                case Kind::Coil: {
                    if (!count()) return false;
                    out = in;
                    if (cc.broken) return true;
                    const auto name = trimmed(c.name);
                    switch (c.coil) {
                    case CoilType::Normal:
                        live = in ? 1 : 0;
                        return writeValue(name, cc.stmt, sim::Value::boolean(in));
                    case CoilType::Negated:
                        live = in ? 0 : 1;
                        return writeValue(name, cc.stmt, sim::Value::boolean(!in));
                    case CoilType::Set:
                    case CoilType::Reset: {
                        if (in) {
                            if (!writeValue(name, cc.stmt, sim::Value::boolean(c.coil == CoilType::Set))) return false;
                        }
                        else if (!opt.captureCoil.empty() && sameName(name, opt.captureCoil)) result.captured = true;
                        sim::Value now;
                        live = (in && c.coil == CoilType::Set) ? 1 : 0;
                        if (cc.simple && env.read(name, now)) live = now.isTruthy() ? 1 : 0;
                        return true;
                    }
                    case CoilType::Rising:
                    case CoilType::Falling: {
                        auto& m = edgeMemory(cc.edge);
                        const bool pulse = c.coil == CoilType::Rising ? (in && !m) : (!in && m);
                        m = in ? 1 : 0;
                        live = pulse ? 1 : 0;
                        return writeValue(name, cc.stmt, sim::Value::boolean(pulse));
                    }
                    case CoilType::Call: {
                        live = in ? 1 : 0;
                        if (!in) return true;
                        sim::Value ignored;
                        if (!env.call(name, name, {}, ignored))
                            warn("callCoil " + name + " : subroutine calls are not simulated");
                        return true;
                    }
                    case CoilType::Halt:
                        live = in ? 1 : 0;
                        if (in) {
                            warn("haltCoil : the PLC program would stop here");
                            result.halted = true;
                            stop = true;
                        }
                        return true;
                    }
                    return true;
                }
                case Kind::Compare: {
                    if (!count()) return false;
                    if (cc.broken || !cc.expr) return true;
                    auto r = sim::evaluate(*cc.expr, env);
                    if (!r) { fail("[" + trimmed(c.text) + "] : " + r.error().message()); return false; }
                    live = r->isTruthy() ? 1 : 0;
                    out = in && live;
                    return true;
                }
                case Kind::Operate: {
                    if (!count()) return false;
                    out = in;
                    live = in ? 1 : 0;
                    if (!in || cc.broken || !cc.stmt) return true;
                    sim::RunLimits limits;
                    limits.scanStatements = opt.scanStatements;
                    limits.maxScanStatements = opt.maxStatements;
                    const auto r = sim::execute(*cc.stmt, env, limits);
                    if (!r.completed) { result.ok = false; return false; }
                    return true;
                }
                case Kind::Jump:
                    if (!count()) return false;
                    live = in ? 1 : 0;
                    if (in && cc.jumpTarget >= 0) jump = cc.jumpTarget;
                    return true;
                case Kind::Return:
                    live = in ? 1 : 0;
                    if (in) { result.returned = true; stop = true; }
                    return true;
                default:
                    return true;
                }
            }

            bool block(const Network& n, const CNetwork& cn, const CBlock& cb, const std::vector<std::uint8_t>& node,
                       std::vector<std::uint8_t>& pending, LiveBlock* live) {
                const auto& b = n.blocks[cb.index];
                const int stride = cn.cols + 1;
                auto nodeAt = [&](int row, int boundary) -> bool {
                    if (row < 0 || row >= cn.rows || boundary < 0 || boundary > cn.cols) return false;
                    return node[static_cast<std::size_t>(row * stride + boundary)] != 0;
                };
                if (live) live->pinValues.assign(b.pins.size(), std::string{});
                if (!count()) return false;
                const bool en = b.enEno ? nodeAt(b.y, b.x) : true;
                if (live) live->ran = en && !cb.broken;
                if (!en || cb.broken) return true;

                // Les entrees.
                std::vector<sim::Value> values;
                std::vector<std::pair<std::string, sim::Value>> named;
                for (std::size_t pi = 0; pi < b.pins.size(); ++pi) {
                    const auto& pin = b.pins[pi];
                    const auto& cp = cb.pins[pi];
                    if (pin.dir == PinDir::Out) continue;
                    sim::Value v;
                    bool given = true;
                    if (cp.expr) {
                        auto r = sim::evaluate(*cp.expr, env);
                        if (!r) { fail(b.type + "." + pin.formal + " : " + r.error().message()); return false; }
                        v = *r;
                    }
                    else if (cp.connected) v = sim::Value::boolean(nodeAt(b.pinRow(pi), b.x));
                    else { given = false; v = sim::Value::boolean(false); }
                    if (pin.inverted) v = sim::Value::boolean(!v.isTruthy());
                    values.push_back(v);
                    if (given) named.emplace_back(pin.formal, v);
                    if (live && given) live->pinValues[pi] = v.display();
                }

                auto output = [&](std::size_t pi, sim::Value v) -> bool {
                    const auto& pin = b.pins[pi];
                    const auto& cp = cb.pins[pi];
                    if (pin.inverted) v = sim::Value::boolean(!v.isTruthy());
                    if (live) live->pinValues[pi] = v.display();
                    const auto eff = trimmed(pin.effective);
                    if (!eff.empty() && !writeValue(eff, cp.assign, v)) return false;
                    const int row = b.pinRow(pi);
                    const int to = b.x + b.width;
                    if (row >= 0 && row < cn.rows && to <= cn.cols && v.isTruthy())
                        pending[static_cast<std::size_t>(row * stride + to)] = 1;
                    return true;
                };

                if (cb.instance) {
                    const auto inst = trimmed(b.instance);
                    sim::Value ignored;
                    if (!env.call(inst, inst, named, ignored)) {
                        fail("block " + inst + " (" + b.type + ") cannot be called: is it a declared instance?");
                        return false;
                    }
                    for (std::size_t pi = 0; pi < b.pins.size(); ++pi) {
                        if (b.pins[pi].dir == PinDir::In) continue;
                        sim::Value v;
                        if (!env.read(inst + "." + b.pins[pi].formal, v)) continue;
                        if (!output(pi, v)) return false;
                    }
                }
                else if (cb.fn) {
                    PinEnv pe(env, values);
                    auto r = sim::evaluate(*cb.fn, pe);
                    if (!r) { fail(b.type + " : " + r.error().message()); return false; }
                    if (cb.resultPin >= 0 && !output(static_cast<std::size_t>(cb.resultPin), *r)) return false;
                }
                // ENO : vrai quand le bloc s'est execute.
                if (b.enEno && b.y >= 0 && b.y < cn.rows && b.x + b.width <= cn.cols)
                    pending[static_cast<std::size_t>(b.y * stride + b.x + b.width)] = 1;
                return true;
            }

            // Un reseau. Rend le reseau suivant (un saut), -1 : le suivant dans l'ordre, -2 : arreter.
            int runNetwork(int ni) {
                network = ni;
                const auto& n = prog.diagram.networks[static_cast<std::size_t>(ni)];
                const auto& cn = prog.networks[static_cast<std::size_t>(ni)];
                const int stride = cn.cols + 1;
                std::vector<std::uint8_t> node(static_cast<std::size_t>(cn.rows * stride), 0);
                std::vector<std::uint8_t> pending(node.size(), 0);
                std::vector<std::uint8_t> cellLive(static_cast<std::size_t>(cn.rows * std::max(1, cn.cols)), 0);
                LiveNetwork* live = nullptr;
                if (opt.recordLive) {
                    auto& nets = state.live.networks;
                    if (nets.size() < prog.networks.size()) nets.resize(prog.networks.size());
                    live = &nets[static_cast<std::size_t>(ni)];
                    live->ran = true;
                    live->blocks.assign(n.blocks.size(), LiveBlock{});
                }
                for (int r = 0; r < cn.rows; ++r) pending[static_cast<std::size_t>(r * stride)] = 1;   // le rail de gauche

                int jump = -1;
                bool stop = false;
                std::size_t nextBlock = 0;
                for (int b = 0; b <= cn.cols && result.ok; ++b) {
                    for (int r = 0; r < cn.rows; ++r)
                        node[static_cast<std::size_t>(r * stride + b)] = pending[static_cast<std::size_t>(r * stride + b)];
                    // les liaisons verticales a cette frontiere : un groupe de rangees, un seul noeud
                    for (int r = 0; r < cn.rows;) {
                        int s = r;
                        std::uint8_t v = node[static_cast<std::size_t>(r * stride + b)];
                        while (r + 1 < cn.rows && n.link(r, b)) {
                            ++r;
                            v = static_cast<std::uint8_t>(v | node[static_cast<std::size_t>(r * stride + b)]);
                        }
                        for (int k = s; k <= r; ++k) node[static_cast<std::size_t>(k * stride + b)] = v;
                        ++r;
                    }
                    // les blocs de cette colonne
                    while (nextBlock < cn.blocks.size() && n.blocks[cn.blocks[nextBlock].index].x <= b) {
                        const auto& cb = cn.blocks[nextBlock];
                        if (n.blocks[cb.index].x == b) {
                            LiveBlock* lb = live ? &live->blocks[cb.index] : nullptr;
                            if (!block(n, cn, cb, node, pending, lb)) break;
                        }
                        ++nextBlock;
                    }
                    if (!result.ok || b >= cn.cols) continue;
                    // les elements de cette colonne, de haut en bas
                    for (int r = 0; r < cn.rows && result.ok; ++r) {
                        const auto& rowCells = n.cells[static_cast<std::size_t>(r)];
                        if (b >= static_cast<int>(rowCells.size())) continue;
                        const auto& c = rowCells[static_cast<std::size_t>(b)];
                        if (c.kind == Kind::Empty || c.kind == Kind::Covered || c.kind == Kind::Raw) continue;
                        if (n.blockAt(r, b) >= 0) continue;
                        const auto& cc = cn.cells[static_cast<std::size_t>(r * cn.cols + b)];
                        const bool in = node[static_cast<std::size_t>(r * stride + b)] != 0;
                        bool out = false;
                        std::uint8_t lv = 0;
                        if (!cell(c, cc, in, out, lv, jump, stop)) break;
                        cellLive[static_cast<std::size_t>(r * cn.cols + b)] = lv;
                        const int end = std::min(cn.cols, b + std::max(1, c.width));
                        if (out) pending[static_cast<std::size_t>(r * stride + end)] = 1;
                    }
                }
                ++result.networksRun;
                if (live) {
                    live->node.assign(static_cast<std::size_t>(cn.rows), std::vector<std::uint8_t>(static_cast<std::size_t>(stride), 0));
                    live->cell.assign(static_cast<std::size_t>(cn.rows), std::vector<std::uint8_t>(static_cast<std::size_t>(std::max(0, cn.cols)), 0));
                    for (int r = 0; r < cn.rows; ++r) {
                        for (int b = 0; b < stride; ++b)
                            live->node[static_cast<std::size_t>(r)][static_cast<std::size_t>(b)] = node[static_cast<std::size_t>(r * stride + b)];
                        for (int c = 0; c < cn.cols; ++c)
                            live->cell[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = cellLive[static_cast<std::size_t>(r * cn.cols + c)];
                    }
                }
                if (!result.ok || stop) return -2;
                return jump;
            }
        };

    } // namespace

    RunResult run(const Program& program, sim::Environment& env, State& state, const RunOptions& options) {
        Runner run{ program, env, state, options, {}, 0 };
        if (options.recordLive) {
            ++state.live.pass;
            auto& nets = state.live.networks;
            nets.resize(program.networks.size());
            for (auto& n : nets) n.ran = false;
        }
        const int count = static_cast<int>(program.networks.size());
        int ni = 0;
        int jumps = 0;
        while (ni >= 0 && ni < count) {
            const int next = run.runNetwork(ni);
            if (next == -2) break;
            if (next >= 0) {
                if (++jumps > 100000) {
                    run.network = ni;
                    run.fail("too many jumps in one pass (a loop between labels?)");
                    break;
                }
                ni = next;
                continue;
            }
            ++ni;
        }
        // Un reseau saute ou apres un RETURN : rien n'y est sous tension.
        if (options.recordLive)
            for (std::size_t i = 0; i < state.live.networks.size(); ++i) {
                auto& ln = state.live.networks[i];
                if (ln.ran) continue;
                for (auto& r : ln.node) std::fill(r.begin(), r.end(), 0);
                for (auto& r : ln.cell) std::fill(r.begin(), r.end(), 0);
                for (auto& b : ln.blocks) b.ran = false;
            }
        return std::move(run.result);
    }

    core::Result<bool> evaluateCondition(const Program& program, sim::Environment& env, State& state,
                                         std::string_view coilName) {
        RunOptions o;
        o.captureCoil = std::string(coilName);
        const auto r = run(program, env, state, o);
        if (!r.ok) {
            const auto msg = r.diagnostics.empty() ? std::string("LD : erreur") : r.diagnostics.back().message;
            return core::fail(core::ErrorCode::InvalidArgument, msg);
        }
        if (!r.captured)
            return core::fail(core::ErrorCode::InvalidArgument,
                              "pas de bobine " + std::string(coilName) + " dans la r\xC3\xA9" "ceptivit\xC3\xA9 LD");
        return r.value;
    }

} // namespace project::ladder
