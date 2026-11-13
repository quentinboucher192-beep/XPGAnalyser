// =============================================================================
//  app/LadderPane.cpp - 1.51.0 : l'editeur graphique d'une section LD (voir le .hpp)
// =============================================================================
#include "LadderPane.hpp"

#include "App.hpp"

#include "../project/BlockLibrary.hpp"
#include "../project/EditCommands.hpp"
#include "../project/GraphListing.hpp"
#include "../project/LadderSim.hpp"
#include "../sim/Runtime.hpp"
#include "../ui/widgets/Containers.hpp"
#include "../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace app {

    using namespace ui;
    namespace ld = project::ladder;

    namespace {

        // Les mesures du dessin, a 100 %.
        constexpr float kCellW = 96.f;     // une colonne
        constexpr float kRowH = 54.f;      // une rangee
        constexpr float kLeft = 30.f;      // la place du rail de gauche
        constexpr float kPad = 10.f;       // au-dessus du premier reseau
        constexpr float kHeader = 22.f;    // le titre d'un reseau (quand il y en a plusieurs)
        constexpr float kGap = 16.f;       // entre deux reseaux
        constexpr float kMidFrac = 0.62f;  // le fil, a cette hauteur de la rangee (le nom au-dessus)

        // Les modes de la saisie.
        enum Edit : int {
            None = 0, Name = 1, Text = 2, PinValue = 3, Instance = 4, BlockType = 5, NetComment = 6,
            // un element neuf, pose avec son nom (un seul pas de Ctrl+Z)
            NewContact = 10, NewClosed, NewRising, NewFalling, NewCoil, NewNotCoil, NewSet, NewReset,
            NewCompare = 20, NewOperate = 21,
        };

        gfx::FontId sized(float px, float zoom) {
            return gfx::FontId{ static_cast<std::uint16_t>(std::clamp(std::round(px * zoom), 7.f, 64.f)) };
        }

        std::string upper(std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }

        // Un texte qui tient dans `w` : coupe, avec ".." au bout.
        std::string fit(const gfx::IRenderer& r, std::string text, gfx::FontId font, float w) {
            if (w <= 4.f) return {};
            if (r.measure(text, font).width <= w) return text;
            const auto n = r.fitCharacters(text, font, std::max(0.f, w - r.measure("..", font).width));
            text.resize(std::min(text.size(), n));
            while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
            return text + "..";
        }

        const char* contactLetter(ld::ContactType t) {
            switch (t) {
            case ld::ContactType::Closed: return "/";
            case ld::ContactType::Rising: return "P";
            case ld::ContactType::Falling: return "N";
            default: return "";
            }
        }
        const char* coilLetter(ld::CoilType t) {
            switch (t) {
            case ld::CoilType::Negated: return "/";
            case ld::CoilType::Set: return "S";
            case ld::CoilType::Reset: return "R";
            case ld::CoilType::Rising: return "P";
            case ld::CoilType::Falling: return "N";
            case ld::CoilType::Call: return "C";
            case ld::CoilType::Halt: return "H";
            default: return "";
            }
        }

        // La premiere rangee utile d'un reseau et le nombre de rangees montrees.
        int shownRows(const ld::Network& n, int selRow) {
            int last = -1;
            for (int r = 0; r < n.rows(); ++r) {
                bool any = false;
                for (const auto& c : n.cells[static_cast<std::size_t>(r)]) any = any || c.kind != ld::Kind::Empty;
                for (auto l : n.links[static_cast<std::size_t>(r)]) any = any || l;
                if (any) last = r;
            }
            for (const auto& b : n.blocks) last = std::max(last, b.y + b.height - 1);
            for (const auto& c : n.comments) last = std::max(last, c.y + c.height - 1);
            last = std::max(last, selRow);
            return std::clamp(last + 2, 1, std::max(1, n.rows()));
        }

    } // namespace

    // =============================================================== la saisie
    // Un champ de saisie qui sait qu'Echap ANNULE (le champ ordinaire valide en
    // perdant le clavier).
    class LadderField final : public InputText {
    public:
        using InputText::InputText;
        bool cancelled{ false };
    protected:
        EventResult onEvent(const InputEvent& ev) override {
            if (const auto* k = std::get_if<KeyDown>(&ev); k && k->key == Key::Escape && focused() && !suggestionsOpen())
                cancelled = true;
            return InputText::onEvent(ev);
        }
    };

    // =============================================================== le dessin
    class LadderCanvas final : public Widget {
    public:
        LadderCanvas(LadderPane& pane, std::string id) : Widget(std::move(id)), pane_(pane) {
            setFocusPolicy(true);
            setZoomSupport(ZoomSupport::Native);
        }

        [[nodiscard]] float zoom() const override { return zoom_; }
        void setZoom(float z) override {
            zoom_ = std::clamp(z, minZoom(), maxZoom());
            clampScroll();
            invalidate();
        }
        [[nodiscard]] float minZoom() const override { return 0.4f; }
        [[nodiscard]] float maxZoom() const override { return 3.f; }
        [[nodiscard]] SizeHint sizeHint() const override { return { { 600.f, 300.f }, { 200.f, 120.f }, 1.f, 1.f }; }

        // ---- la geometrie ---------------------------------------------------------
        struct Net { float top{ 0.f }; float rowsTop{ 0.f }; int rows{ 1 }; int cols{ 11 }; };
        void relayout() {
            const auto& d = pane_.diagram();
            nets_.clear();
            float y = kPad;
            const bool headers = d.networks.size() > 1;
            for (std::size_t i = 0; i < d.networks.size(); ++i) {
                const auto& n = d.networks[i];
                Net g;
                g.top = y;
                g.rowsTop = y + (headers ? kHeader : 0.f);
                const auto sel = pane_.selection();
                g.rows = shownRows(n, sel.network == static_cast<int>(i) ? sel.row : -1);
                g.cols = n.rows() ? static_cast<int>(n.cells[0].size()) : d.columns;
                nets_.push_back(g);
                y = g.rowsTop + static_cast<float>(g.rows) * kRowH + kGap;
            }
            contentH_ = y;
            contentW_ = kLeft + static_cast<float>(d.columns) * kCellW + kLeft;
        }
        [[nodiscard]] float sx(float chartX) const { return bounds().x + chartX * zoom_ - scrollX_; }
        [[nodiscard]] float sy(float chartY) const { return bounds().y + chartY * zoom_ - scrollY_; }
        [[nodiscard]] float colX(int col) const { return sx(kLeft + static_cast<float>(col) * kCellW); }
        [[nodiscard]] float rowY(int net, int row) const {
            if (net < 0 || net >= static_cast<int>(nets_.size())) return 0.f;
            return sy(nets_[static_cast<std::size_t>(net)].rowsTop + static_cast<float>(row) * kRowH);
        }
        [[nodiscard]] float midY(int net, int row) const { return rowY(net, row) + kRowH * kMidFrac * zoom_; }
        [[nodiscard]] gfx::Rect cellRect(ld::Pos p, int width = 1) const {
            return { colX(p.col), rowY(p.network, p.row), kCellW * zoom_ * static_cast<float>(std::max(1, width)), kRowH * zoom_ };
        }
        [[nodiscard]] gfx::Rect blockRect(int net, const ld::Block& b) const {
            const float l = colX(b.x) + 10.f * zoom_, r = colX(b.x + b.cols) - 10.f * zoom_;
            const float t = rowY(net, b.y) + kRowH * 0.42f * zoom_, bo = rowY(net, b.y + b.height) - 8.f * zoom_;
            return { l, t, std::max(4.f, r - l), std::max(4.f, bo - t) };
        }

        // Le point -> la case (et, sur un bloc, la broche : -1 aucune).
        bool hit(gfx::Point p, ld::Pos& out, int* pin = nullptr) const {
            const auto& d = pane_.diagram();
            if (pin) *pin = -1;
            for (std::size_t i = 0; i < nets_.size() && i < d.networks.size(); ++i) {
                const auto& g = nets_[i];
                const float top = rowY(static_cast<int>(i), 0);
                const float bottom = rowY(static_cast<int>(i), g.rows);
                if (p.y < top || p.y >= bottom) continue;
                const int row = static_cast<int>((p.y - top) / (kRowH * zoom_));
                const int col = static_cast<int>(std::floor((p.x - colX(0)) / (kCellW * zoom_)));
                if (col < 0 || col >= g.cols) return false;
                out = { static_cast<int>(i), std::clamp(row, 0, g.rows - 1), col };
                const auto& n = d.networks[i];
                if (pin) {
                    const int bi = n.blockAt(out.row, out.col);
                    if (bi >= 0) {
                        const auto& b = n.blocks[static_cast<std::size_t>(bi)];
                        const auto box = blockRect(static_cast<int>(i), b);
                        const bool left = p.x < box.x + box.w * 0.5f;
                        for (std::size_t k = 0; k < b.pins.size(); ++k) {
                            const bool isOut = b.pins[k].dir == ld::PinDir::Out;
                            if (isOut == left || b.pinRow(k) != out.row) continue;
                            *pin = static_cast<int>(k);
                        }
                    }
                }
                return true;
            }
            return false;
        }

        void reveal(ld::Pos p) {
            relayout();
            if (p.network < 0 || p.network >= static_cast<int>(nets_.size())) return;
            const auto r = cellRect(p);
            const auto area = bounds();
            if (r.y < area.y + 4.f) scrollY_ -= (area.y + 4.f - r.y);
            else if (r.bottom() > area.bottom() - 4.f) scrollY_ += r.bottom() - (area.bottom() - 4.f);
            if (r.x < area.x + 4.f) scrollX_ -= (area.x + 4.f - r.x);
            else if (r.right() > area.right() - 4.f) scrollX_ += r.right() - (area.right() - 4.f);
            clampScroll();
            invalidate();
        }

        [[nodiscard]] ld::Pos hover() const noexcept { return hover_; }

        // Le survol : l'element, sa variable (type, commentaire), sa valeur en simulation.
        [[nodiscard]] bool hasTooltip() const override { return pane_.readable(); }
        [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;

    protected:
        void onPaint(const PaintContext& ctx) override;
        EventResult onEvent(const InputEvent& ev) override;

    private:
        void clampScroll() {
            const auto area = bounds();
            scrollX_ = std::clamp(scrollX_, 0.f, std::max(0.f, contentW_ * zoom_ - area.w));
            scrollY_ = std::clamp(scrollY_, 0.f, std::max(0.f, contentH_ * zoom_ - area.h));
        }
        void paintNetwork(const PaintContext& ctx, int ni, const ld::LiveNetwork* live, const sim::Runtime* rt);

        LadderPane&      pane_;
        float            zoom_{ 1.f };
        float            scrollX_{ 0.f }, scrollY_{ 0.f };
        float            contentW_{ 0.f }, contentH_{ 0.f };
        std::vector<Net> nets_;
        ld::Pos          hover_{ -1, -1, -1 };
        bool             pressed_{ false };
        ld::Pos          dragFrom_{ -1, -1, -1 };
        ld::Pos          dropAt_{ -1, -1, -1 };
    };

    // ---------------------------------------------------------------- le dessin
    void LadderCanvas::onPaint(const PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        const auto area = bounds();
        ctx.r.fillRect(area, c.windowBg);
        relayout();
        clampScroll();
        const auto& d = pane_.diagram();
        if (!pane_.readable()) {
            ctx.r.drawText({ area.x + 16.f, area.y + 16.f },
                           "Ce sch\xC3\xA9ma ne se lit pas (son XML est incomplet) : le volet Texte le montre.",
                           ctx.theme.font.ui, c.error);
            return;
        }
        ctx.r.pushClip(area);
        const sim::Runtime* rt = pane_.runtime();
        const ld::LiveState* live = rt ? rt->ladderLive(pane_.runtimeSection()) : nullptr;
        for (std::size_t i = 0; i < d.networks.size() && i < nets_.size(); ++i) {
            const ld::LiveNetwork* ln = nullptr;
            if (live && i < live->networks.size()) ln = &live->networks[i];
            paintNetwork(ctx, static_cast<int>(i), ln, rt);
        }
        if (d.networks.empty())
            ctx.r.drawText({ area.x + 16.f, area.y + 16.f }, "Aucun r\xC3\xA9seau : Inser (ou + Ligne) en ajoute un.",
                           ctx.theme.font.ui, c.textMuted);
        ctx.r.popClip();
    }

    void LadderCanvas::paintNetwork(const PaintContext& ctx, int ni, const ld::LiveNetwork* live, const sim::Runtime* rt) {
        const auto& c = ctx.theme.color;
        const auto& d = pane_.diagram();
        const auto& n = d.networks[static_cast<std::size_t>(ni)];
        const auto& g = nets_[static_cast<std::size_t>(ni)];
        const float z = zoom_;
        const auto fName = sized(12.f, z), fSym = sized(13.f, z), fSmall = sized(10.5f, z);
        const float lw = std::max(1.f, 1.6f * z), lwOn = std::max(2.f, 3.f * z);
        const gfx::Color wire = c.text.withAlpha(170), on = c.ok, muted = c.textMuted;
        const bool running = live && live->ran;
        const int rows = std::min(g.rows, n.rows());
        const int cols = g.cols;

        // un peu de liberte sur les tailles : l'etat en direct vient du runtime prepare
        auto node = [&](int r, int b) -> bool {
            if (!live || r < 0 || b < 0 || r >= static_cast<int>(live->node.size())) return false;
            const auto& row = live->node[static_cast<std::size_t>(r)];
            return b < static_cast<int>(row.size()) && row[static_cast<std::size_t>(b)] != 0;
        };
        auto cellOn = [&](int r, int col) -> bool {
            if (!live || r < 0 || col < 0 || r >= static_cast<int>(live->cell.size())) return false;
            const auto& row = live->cell[static_cast<std::size_t>(r)];
            return col < static_cast<int>(row.size()) && row[static_cast<std::size_t>(col)] != 0;
        };
        auto seg = [&](float x0, float x1, float y, bool energized) {
            ctx.r.line({ x0, y }, { x1, y }, energized ? on : wire, energized ? lwOn : lw);
        };
        auto valueOf = [&](const std::string& name, std::string& text, bool& forced) {
            if (!rt || name.empty()) return false;
            sim::Value v;
            if (!rt->get(name, v)) return false;
            text = v.display();
            if (v.type() == sim::Type::Bool) text = v.isTruthy() ? "1" : "0";
            forced = rt->isForced(name);
            return true;
        };

        // ---- le titre du reseau
        if (d.networks.size() > 1) {
            const std::string title = "R\xC3\xA9seau " + std::to_string(ni + 1);
            ctx.r.drawText({ sx(kLeft - 18.f), sy(g.top + 3.f) }, title, sized(11.f, z), muted);
        }
        const float top = rowY(ni, 0), bottom = rowY(ni, rows);
        // ---- les rangees (un fond leger une sur deux)
        for (int r = 0; r < rows; ++r)
            if (r % 2 == 1) ctx.r.fillRect({ colX(0), rowY(ni, r), colX(cols) - colX(0), kRowH * z }, c.rowAltBg.withAlpha(90));
        // ---- les commentaires
        for (const auto& cm : n.comments) {
            const gfx::Rect box{ colX(cm.x) + 2.f * z, rowY(ni, cm.y) + 3.f * z,
                                 kCellW * z * static_cast<float>(std::max(1, cm.width)) - 4.f * z,
                                 kRowH * z * static_cast<float>(std::max(1, cm.height)) - 6.f * z };
            ctx.r.fillRect(box, c.warning.withAlpha(36));
            ctx.r.strokeRect(box, c.warning.withAlpha(120), 1.f);
            ctx.r.drawText({ box.x + 6.f * z, box.y + 4.f * z }, fit(ctx.r, "(* " + cm.text + " *)", fName, box.w - 10.f * z),
                           fName, c.text);
        }
        // ---- les rails
        ctx.r.line({ colX(0), top }, { colX(0), bottom }, running ? on : c.text, std::max(2.f, 4.f * z));
        ctx.r.line({ colX(cols), top }, { colX(cols), bottom }, c.text.withAlpha(140), std::max(1.f, 2.f * z));

        // ---- les cases
        for (int r = 0; r < rows; ++r) {
            const float y = midY(ni, r);
            for (int col = 0; col < cols; ++col) {
                const auto& cell = n.cells[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)];
                if (cell.kind == ld::Kind::Empty || cell.kind == ld::Kind::Covered) continue;
                if (n.blockAt(r, col) >= 0) continue;
                const int w = std::max(1, cell.width);
                const float x0 = colX(col), x1 = colX(col + w), cx = (x0 + x1) / 2.f;
                const bool in = node(r, col);
                const bool lit = cellOn(r, col);
                switch (cell.kind) {
                case ld::Kind::Wire:
                case ld::Kind::Label:
                    if (cell.kind == ld::Kind::Wire) seg(x0, x1, y, in);
                    else {
                        ctx.r.drawText({ x0 + 4.f * z, y - 22.f * z }, fit(ctx.r, cell.name + " :", fSym, x1 - x0 - 6.f), fSym, c.accent);
                        seg(x0, x1, y, in);
                    }
                    break;
                case ld::Kind::Contact: {
                    const float gap = 7.f * z, h = 11.f * z;
                    const bool out = in && lit;
                    seg(x0, cx - gap, y, in);
                    seg(cx + gap, x1, y, out);
                    const gfx::Color ink = lit && running ? on : c.text;
                    ctx.r.line({ cx - gap, y - h }, { cx - gap, y + h }, ink, std::max(1.5f, 2.2f * z));
                    ctx.r.line({ cx + gap, y - h }, { cx + gap, y + h }, ink, std::max(1.5f, 2.2f * z));
                    if (cell.contact == ld::ContactType::Closed)
                        ctx.r.line({ cx - gap + 2.f * z, y + h - 2.f * z }, { cx + gap - 2.f * z, y - h + 2.f * z }, ink, std::max(1.f, 1.6f * z));
                    else if (*contactLetter(cell.contact)) {
                        const auto m = ctx.r.measure(contactLetter(cell.contact), fSmall);
                        ctx.r.drawText({ cx - m.width / 2.f, y - m.height / 2.f }, contactLetter(cell.contact), fSmall, ink);
                    }
                    const std::string name = cell.name.empty() ? std::string("???") : cell.name;
                    const auto t = fit(ctx.r, name, fName, x1 - x0 - 4.f);
                    ctx.r.drawText({ cx - ctx.r.measure(t, fName).width / 2.f, rowY(ni, r) + 4.f * z }, t, fName,
                                   cell.name.empty() ? c.error : c.text);
                    std::string value;
                    bool forced = false;
                    if (valueOf(cell.name, value, forced)) {
                        const std::string shown = value + (forced ? " F" : "");
                        ctx.r.drawText({ cx + gap + 3.f * z, y + 4.f * z }, shown, fSmall, forced ? c.warning : (lit ? on : muted));
                    }
                    break;
                }
                case ld::Kind::Coil: {
                    const float rad = 10.f * z;
                    seg(x0, cx - rad, y, in);
                    seg(cx + rad, x1, y, in);
                    const gfx::Color ink = lit && running ? on : c.text;
                    // deux arcs : ( )
                    for (int side = -1; side <= 1; side += 2) {
                        gfx::Point prev{};
                        for (int k = 0; k <= 8; ++k) {
                            const float a = (-55.f + 110.f * static_cast<float>(k) / 8.f) * 3.14159265f / 180.f;
                            const gfx::Point pt{ cx + static_cast<float>(side) * (rad * 0.35f + rad * 0.65f * std::cos(a)) , y + rad * std::sin(a) };
                            if (k) ctx.r.line(prev, pt, ink, std::max(1.5f, 2.f * z));
                            prev = pt;
                        }
                    }
                    if (*coilLetter(cell.coil)) {
                        const auto m = ctx.r.measure(coilLetter(cell.coil), fSmall);
                        ctx.r.drawText({ cx - m.width / 2.f, y - m.height / 2.f }, coilLetter(cell.coil), fSmall, ink);
                    }
                    const std::string name = cell.name.empty() && cell.coil != ld::CoilType::Halt ? std::string("???") : cell.name;
                    const auto t = fit(ctx.r, name, fName, x1 - x0 - 4.f);
                    ctx.r.drawText({ cx - ctx.r.measure(t, fName).width / 2.f, rowY(ni, r) + 4.f * z }, t, fName,
                                   name == "???" ? c.error : c.text);
                    std::string value;
                    bool forced = false;
                    if (cell.coil != ld::CoilType::Call && valueOf(cell.name, value, forced)) {
                        const std::string shown = value + (forced ? " F" : "");
                        ctx.r.drawText({ cx + rad + 3.f * z, y + 4.f * z }, shown, fSmall, forced ? c.warning : (lit ? on : muted));
                    }
                    break;
                }
                case ld::Kind::Compare:
                case ld::Kind::Operate: {
                    const gfx::Rect box{ x0 + 6.f * z, y - 13.f * z, x1 - x0 - 12.f * z, 26.f * z };
                    seg(x0, box.x, y, in);
                    seg(box.right(), x1, y, cell.kind == ld::Kind::Compare ? (in && lit) : in);
                    ctx.r.fillRect(box, c.panelBg);
                    ctx.r.strokeRect(box, lit && running ? on : c.text, std::max(1.f, 1.5f * z));
                    const std::string text = cell.kind == ld::Kind::Compare ? cell.text : cell.text;
                    const auto t = fit(ctx.r, text.empty() ? std::string("?") : text, fName, box.w - 8.f * z);
                    const auto m = ctx.r.measure(t, fName);
                    ctx.r.drawText({ box.x + (box.w - m.width) / 2.f, box.y + (box.h - m.height) / 2.f }, t, fName, c.text);
                    if (cell.kind == ld::Kind::Operate)
                        ctx.r.drawText({ box.x, rowY(ni, r) + 2.f * z }, "op\xC3\xA9ration", fSmall, muted);
                    break;
                }
                case ld::Kind::Jump: {
                    seg(x0, cx - 12.f * z, y, in);
                    const std::string t = ">> " + cell.name;
                    ctx.r.drawText({ cx - 12.f * z, y - 9.f * z }, fit(ctx.r, t, fSym, x1 - cx + 10.f * z), fSym, lit && running ? on : c.accent);
                    break;
                }
                case ld::Kind::Return: {
                    seg(x0, cx - 16.f * z, y, in);
                    ctx.r.drawText({ cx - 16.f * z, y - 9.f * z }, "<RETURN>", fSmall, lit && running ? on : c.accent);
                    break;
                }
                case ld::Kind::Raw: {
                    const gfx::Rect box{ x0 + 4.f * z, y - 12.f * z, x1 - x0 - 8.f * z, 24.f * z };
                    ctx.r.strokeRect(box, muted, 1.f);
                    ctx.r.drawText({ box.x + 4.f * z, box.y + 4.f * z }, fit(ctx.r, "<" + cell.element + ">", fSmall, box.w - 6.f), fSmall, muted);
                    break;
                }
                default: break;
                }
            }
        }
        // ---- les liaisons verticales
        for (int r = 0; r + 1 < rows; ++r)
            for (int b = 1; b <= cols; ++b)
                if (n.link(r, b)) {
                    const bool lit = node(r, b);
                    ctx.r.line({ colX(b), midY(ni, r) }, { colX(b), midY(ni, r + 1) }, lit ? on : wire, lit ? lwOn : lw);
                }
        // ---- les blocs
        for (std::size_t bi = 0; bi < n.blocks.size(); ++bi) {
            const auto& b = n.blocks[bi];
            if (b.y >= rows) continue;
            const auto box = blockRect(ni, b);
            const ld::LiveBlock* lb = live && bi < live->blocks.size() ? &live->blocks[bi] : nullptr;
            const bool ran = lb && lb->ran && running;
            ctx.r.fillRect(box, c.panelBg);
            ctx.r.strokeRect(box, ran ? on : c.text, std::max(1.f, 1.8f * z));
            const auto fType = sized(12.5f, z);
            const auto type = fit(ctx.r, b.type, fType, box.w - 6.f * z);
            ctx.r.drawText({ box.x + (box.w - ctx.r.measure(type, fType).width) / 2.f, box.y + 3.f * z }, type, fType, c.accent);
            if (!b.isFunction()) {
                const auto inst = fit(ctx.r, b.instance, fName, (box.w + 40.f * z));
                ctx.r.drawText({ box.x + (box.w - ctx.r.measure(inst, fName).width) / 2.f, rowY(ni, b.y) + 3.f * z }, inst, fName, c.text);
            }
            for (std::size_t k = 0; k < b.pins.size(); ++k) {
                const auto& pin = b.pins[k];
                const int pr = b.pinRow(k);
                const float py = midY(ni, pr);
                const bool isOut = pin.dir == ld::PinDir::Out;
                const float edge = isOut ? box.right() : box.x;
                const float outer = isOut ? colX(b.x + b.cols) : colX(b.x);
                const bool lit = isOut ? node(pr, b.x + b.cols) : node(pr, b.x);
                ctx.r.line({ outer, py }, { edge, py }, lit ? on : wire, lit ? lwOn : lw);
                if (pin.inverted) ctx.r.strokeRect({ edge - (isOut ? 0.f : 6.f * z), py - 3.f * z, 6.f * z, 6.f * z }, c.text, 1.f);
                const auto pn = fit(ctx.r, pin.formal, fSmall, box.w * 0.5f - 4.f * z);
                const float pw = ctx.r.measure(pn, fSmall).width;
                ctx.r.drawText({ isOut ? box.right() - pw - 3.f * z : box.x + 3.f * z, py - 7.f * z }, pn, fSmall, c.text);
                // le parametre effectif, dehors ; et sa valeur en simulation
                std::string label = pin.effective;
                if (lb && k < lb->pinValues.size() && !lb->pinValues[k].empty() && running)
                    label = label.empty() ? lb->pinValues[k] : label + " = " + lb->pinValues[k];
                if (!label.empty()) {
                    const float room = kCellW * z * 1.6f;
                    const auto t = fit(ctx.r, label, fSmall, room);
                    const float tw = ctx.r.measure(t, fSmall).width;
                    const float tx = isOut ? edge + 4.f * z : edge - tw - 4.f * z;
                    ctx.r.drawText({ tx, py - 15.f * z }, t, fSmall, pin.effective.empty() ? (lit ? on : muted) : c.syntaxString);
                }
            }
        }
        // ---- le choix, le survol, le depot
        const auto sel = pane_.selection();
        auto frame = [&](ld::Pos p, gfx::Color col, float thick) {
            if (p.network != ni || p.row < 0 || p.row >= rows || p.col < 0 || p.col >= cols) return;
            if (const int bi = n.blockAt(p.row, p.col); bi >= 0) {
                const auto& b = n.blocks[static_cast<std::size_t>(bi)];
                auto r = blockRect(ni, b);
                ctx.r.strokeRect({ r.x - 4.f, r.y - 4.f, r.w + 8.f, r.h + 8.f }, col, thick);
                return;
            }
            const auto a = ld::anchorOf(d, p);
            const auto* cc = ld::cellAt(d, a);
            const auto r = cellRect(a, cc ? cc->width : 1);
            ctx.r.strokeRect({ r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f }, col, thick);
        };
        if (hover_.network == ni && !(hover_ == sel)) frame(hover_, c.accent.withAlpha(90), 1.f);
        if (pressed_ && dropAt_.network == ni && !(dropAt_ == dragFrom_)) frame(dropAt_, c.warning, 2.f);
        frame(sel, focused() ? c.accent : c.accent.withAlpha(150), 2.f);
        // un element en cours de pose : son fantome
        if (pane_.editMode_ >= NewContact && sel.network == ni) {
            const auto r = cellRect(sel, pane_.editMode_ >= NewCompare ? 2 : 1);
            ctx.r.fillRect(r, c.accent.withAlpha(30));
        }
    }

    std::string LadderCanvas::liveTooltip(gfx::Point mouse) const {
        ld::Pos p;
        int pin = -1;
        if (!hit(mouse, p, &pin)) return {};
        const auto& d = pane_.diagram();
        const auto& n = d.networks[static_cast<std::size_t>(p.network)];
        std::string name, what;
        if (const int bi = n.blockAt(p.row, p.col); bi >= 0) {
            const auto& b = n.blocks[static_cast<std::size_t>(bi)];
            what = "bloc " + b.type + (b.isFunction() ? std::string{} : " " + b.instance);
            if (pin >= 0) {
                const auto& pn = b.pins[static_cast<std::size_t>(pin)];
                what += ", broche " + pn.formal + (pn.effective.empty() ? std::string(" (c\xC3\xA2" "bl\xC3\xA9" "e)") : " := " + pn.effective);
                name = pn.effective;
            }
            else name = b.isFunction() ? std::string{} : b.instance;
        }
        else {
            const auto* c = ld::cellAt(d, ld::anchorOf(d, p));
            if (!c) return {};
            switch (c->kind) {
            case ld::Kind::Contact: what = "contact " + std::string(ld::contactTypeName(c->contact)); name = c->name; break;
            case ld::Kind::Coil: what = "bobine " + std::string(ld::coilTypeName(c->coil)); name = c->name; break;
            case ld::Kind::Compare: what = "comparaison [" + c->text + "]"; break;
            case ld::Kind::Operate: what = "op\xC3\xA9ration [" + c->text + "]"; break;
            case ld::Kind::Jump: what = "saut vers " + c->name; break;
            case ld::Kind::Return: what = "RETURN"; break;
            case ld::Kind::Label: what = "\xC3\xA9tiquette " + c->name; break;
            case ld::Kind::Raw: what = "<" + c->element + "> (gard\xC3\xA9 tel quel)"; break;
            default: return {};
            }
        }
        std::string out = what;
        auto doc = pane_.app_.document();
        if (doc && !name.empty()) {
            const auto info = project::describeSymbol(*doc, pane_.section(), name);
            if (info.found) out += "\n" + project::symbolStatusLine(info);
        }
        if (const auto* rt = pane_.runtime(); rt && !name.empty()) {
            sim::Value v;
            if (rt->get(name, v)) out += "\n" + name + " = " + v.display() + (rt->isForced(name) ? "  (forc\xC3\xA9" "e)" : "");
        }
        return out;
    }

    // ---------------------------------------------------------------- les evenements
    EventResult LadderCanvas::onEvent(const InputEvent& ev) {
        if (const auto* w = std::get_if<MouseWheel>(&ev)) {
            if (!bounds().contains(w->pos)) return EventResult::Ignored;
            if (w->mods.ctrl) {
                const float chartX = (w->pos.x - bounds().x + scrollX_) / zoom_;
                const float chartY = (w->pos.y - bounds().y + scrollY_) / zoom_;
                zoom_ = std::clamp(zoom_ * (w->dy > 0.f ? 1.1f : 1.f / 1.1f), minZoom(), maxZoom());
                scrollX_ = chartX * zoom_ - (w->pos.x - bounds().x);
                scrollY_ = chartY * zoom_ - (w->pos.y - bounds().y);
                clampScroll();
                invalidate();
                return EventResult::Consumed;
            }
            if (w->mods.shift) scrollX_ -= w->dy * 40.f;
            else { scrollY_ -= w->dy * 40.f; scrollX_ -= w->dx * 40.f; }
            clampScroll();
            invalidate();
            return EventResult::Consumed;
        }
        if (const auto* m = std::get_if<MouseDown>(&ev)) {
            if (!bounds().contains(m->pos)) return EventResult::Ignored;
            takeFocus();
            ld::Pos p;
            int pin = -1;
            if (!hit(m->pos, p, &pin)) return EventResult::Consumed;
            pane_.select(p);
            pane_.editPin_ = pin;
            if (m->button == MouseButton::Left && m->clickCount >= 2) {
                // en simulation, sur un contact ou une bobine : basculer son forcage
                const auto* cell = ld::cellAt(pane_.diagram(), ld::anchorOf(pane_.diagram(), p));
                if (pane_.runtime() && cell && (cell->kind == ld::Kind::Contact || cell->kind == ld::Kind::Coil)) {
                    sim::Value v;
                    if (pane_.runtime()->get(cell->name, v)) pane_.forceSelected(v.isTruthy() ? 0 : 1);
                    return EventResult::Consumed;
                }
                pane_.openEditor();
                return EventResult::Consumed;
            }
            if (m->button == MouseButton::Left) {
                pressed_ = true;
                dragFrom_ = p;
                dropAt_ = p;
                captureMouse();
            }
            invalidate();
            return EventResult::Consumed;
        }
        if (const auto* m = std::get_if<MouseMove>(&ev)) {
            ld::Pos p;
            const bool over = bounds().contains(m->pos) && hit(m->pos, p);
            const ld::Pos next = over ? p : ld::Pos{ -1, -1, -1 };
            if (pressed_ && over && !(next == dropAt_)) { dropAt_ = next; invalidate(); }
            if (!(next == hover_)) { hover_ = next; invalidate(); }
            if (pressed_ && !(dropAt_ == dragFrom_)) cursor::hold(cursor::Shape::Move);
            return over ? EventResult::Consumed : EventResult::Ignored;
        }
        if (const auto* m = std::get_if<MouseUp>(&ev)) {
            if (!pressed_) return EventResult::Ignored;
            pressed_ = false;
            releaseMouse();
            ld::Pos p;
            if (m->button == MouseButton::Left && hit(m->pos, p) && !(p == dragFrom_) && p.network == dragFrom_.network) {
                const auto from = dragFrom_;
                if (pane_.change([from, p](ld::Diagram& d) { return ld::moveElement(d, from, p); }, "D\xC3\xA9placer"))
                    pane_.select(p);
            }
            dragFrom_ = dropAt_ = { -1, -1, -1 };
            invalidate();
            return EventResult::Consumed;
        }
        if (const auto* k = std::get_if<KeyDown>(&ev)) {
            if (!focused()) return EventResult::Ignored;
            const auto& m = k->mods;
            auto sel = pane_.selection();
            const auto& d = pane_.diagram();
            auto move = [&](int dr, int dc) {
                if (m.alt) {
                    // deplacer l'element
                    ld::Pos to{ sel.network, sel.row + dr, sel.col + dc };
                    const auto from = ld::anchorOf(d, sel);
                    if (pane_.change([from, to](ld::Diagram& dd) { return ld::moveElement(dd, from, to); }, "D\xC3\xA9placer"))
                        pane_.select(to);
                    return EventResult::Consumed;
                }
                ld::Pos to{ sel.network, sel.row + dr, sel.col + dc };
                if (sel.network >= 0 && sel.network < static_cast<int>(nets_.size())) {
                    const auto& g = nets_[static_cast<std::size_t>(sel.network)];
                    if (to.row < 0 && sel.network > 0) {
                        to.network = sel.network - 1;
                        to.row = nets_[static_cast<std::size_t>(to.network)].rows - 1;
                    }
                    else if (to.row >= g.rows && sel.network + 1 < static_cast<int>(nets_.size())) {
                        to.network = sel.network + 1;
                        to.row = 0;
                    }
                    to.row = std::max(0, to.row);
                    to.col = std::clamp(to.col, 0, std::max(0, g.cols - 1));
                    // une case couverte : sauter l'element large
                    if (dc > 0) {
                        const auto a = ld::anchorOf(d, sel);
                        if (const auto* cc = ld::cellAt(d, a)) to.col = std::min(g.cols - 1, std::max(to.col, a.col + cc->width));
                    }
                }
                pane_.select(ld::anchorOf(d, to));
                reveal(pane_.selection());
                return EventResult::Consumed;
            };
            switch (k->key) {
            case Key::Left: return move(0, -1);
            case Key::Right: return move(0, 1);
            case Key::Up: return move(-1, 0);
            case Key::Down: return move(1, 0);
            case Key::Return: pane_.openEditor(); return EventResult::Consumed;
            case Key::Delete:
            case Key::Backspace:
                pane_.keyCommand(m.shift ? "ld.delRung" : "ld.erase");
                return EventResult::Consumed;
            case Key::Insert: pane_.keyCommand(m.shift ? "ld.row" : "ld.rung"); return EventResult::Consumed;
            default: break;
            }
            if (m.ctrl || m.alt || m.super) return EventResult::Ignored;
            const char* id = nullptr;
            switch (k->key) {
            case Key::C: id = m.shift ? "ld.closed" : "ld.contact"; break;
            case Key::P: id = "ld.rising"; break;
            case Key::N: id = "ld.falling"; break;
            case Key::B: id = m.shift ? "ld.notCoil" : "ld.coil"; break;
            case Key::S: id = "ld.set"; break;
            case Key::R: id = "ld.reset"; break;
            case Key::F: id = "ld.block"; break;
            case Key::K: id = "ld.compare"; break;
            case Key::O: id = "ld.operate"; break;
            case Key::H: id = "ld.wire"; break;
            case Key::V: id = "ld.vlink"; break;
            case Key::U: id = "ld.parallel"; break;
            case Key::I: id = "ld.invert"; break;
            case Key::T: id = "ld.type"; break;
            case Key::Num1: id = "ld.force1"; break;
            case Key::Num0: id = "ld.force0"; break;
            case Key::L: id = "ld.unforce"; break;
            default: break;
            }
            if (!id) return EventResult::Ignored;
            pane_.keyCommand(id);
            return EventResult::Consumed;
        }
        if (std::get_if<TextInput>(&ev) && focused()) return EventResult::Consumed;   // les lettres sont des commandes
        return EventResult::Ignored;
    }

    // =============================================================== le volet
    LadderPane::LadderPane(App& app, domain::Index section, std::string paneId)
        : Widget(std::move(paneId)), app_(app), section_(section), alive_(std::make_shared<bool>(true)) {
        auto bar = std::make_unique<ToolBar>(this->id() + ".bar");
        auto tool = [&](const char* text, const char* action, const char* why) {
            bar->addButton(text, action, Icon::None).setTooltip(why);
        };
        tool("-| |-", "ld.contact", "Contact ouvert (C) : passe quand la variable est vraie");
        tool("-|/|-", "ld.closed", "Contact ferm\xC3\xA9 (Maj+C) : passe quand la variable est fausse");
        tool("-|P|-", "ld.rising", "Front montant (P) : passe un cycle quand la variable devient vraie");
        tool("-|N|-", "ld.falling", "Front descendant (N) : passe un cycle quand la variable devient fausse");
        bar->addSeparator();
        tool("-( )-", "ld.coil", "Bobine (B), dans la derni\xC3\xA8re colonne : la variable prend le courant");
        tool("-(/)-", "ld.notCoil", "Bobine inverse (Maj+B) : la variable prend le contraire du courant");
        tool("-(S)-", "ld.set", "Bobine S (S) : met la variable \xC3\xA0 1 quand le courant passe");
        tool("-(R)-", "ld.reset", "Bobine R (R) : met la variable \xC3\xA0 0 quand le courant passe");
        bar->addSeparator();
        tool("Bloc", "ld.block", "Un bloc (F) : TON, CTU, R_TRIG, ADD, GT, MOVE, un DFB du projet... ; son instance est d\xC3\xA9" "clar\xC3\xA9" "e");
        tool("[ = ]", "ld.compare", "Comparaison (K) : une expression ST, passe quand elle est vraie (Niveau > 80)");
        tool("[ := ]", "ld.operate", "Op\xC3\xA9ration (O) : une instruction ST ex\xC3\xA9" "cut\xC3\xA9" "e quand le courant passe (N := N + 1)");
        bar->addSeparator();
        tool("Fil", "ld.wire", "Un fil horizontal dans la case (H)");
        tool("|", "ld.vlink", "Liaison verticale au bord droit de la case, vers la rang\xC3\xA9" "e du dessous (V)");
        tool("OU", "ld.parallel", "Une branche en parall\xC3\xA8le sous l'\xC3\xA9l\xC3\xA9ment choisi (U) : un contact de plus, en OU");
        tool("Inverser", "ld.invert", "Ouvert / ferm\xC3\xA9, bobine / inverse, S / R (I) ; T : le type suivant");
        tool("Supprimer", "ld.erase", "Efface l'\xC3\xA9l\xC3\xA9ment choisi (Suppr) ; entre deux \xC3\xA9l\xC3\xA9ments, un fil reste");
        bar->addSeparator();
        tool("+ Ligne", "ld.rung", "Une ligne neuve sous celle choisie : un fil et une bobine (Inser)");
        tool("- Ligne", "ld.delRung", "Supprime la ligne choisie, ses branches et ses blocs (Maj+Suppr)");
        tool("+ Rang\xC3\xA9" "e", "ld.row", "Une rang\xC3\xA9" "e vide sous la case choisie (Maj+Inser)");
        tool("Commentaire", "ld.comment", "Le commentaire du r\xC3\xA9seau (au-dessus)");
        bar->addSeparator();
        tool("\xE2\x88\x92", "ld.zoomOut", "Zoom arri\xC3\xA8re (Ctrl+molette)");
        tool("+", "ld.zoomIn", "Zoom avant (Ctrl+molette)");
        bar->setActionSink([this](core::ActionId a) { command(std::string(a)); });
        bar_ = &static_cast<ToolBar&>(addChild(std::move(bar)));

        canvas_ = &static_cast<LadderCanvas&>(addChild(std::make_unique<LadderCanvas>(*this, this->id() + ".canvas")));

        auto strip = std::make_unique<StatusBar>(this->id() + ".status");
        status_ = &static_cast<StatusBar&>(addChild(std::move(strip)));

        auto field = std::make_unique<LadderField>(this->id() + ".field");
        field->setMonospace(true);
        field_ = &static_cast<LadderField&>(addChild(std::move(field)));
        field_->setVisibility(Visibility::Collapsed);
        const std::weak_ptr<bool> alive = alive_;
        field_->setAssist([this, alive](std::string_view before, std::size_t& from, std::vector<InputText::Suggestion>& out) {
            const auto a = alive.lock();
            if (!a || !*a) return;
            // le mot en cours : depuis le dernier separateur
            std::size_t start = before.size();
            while (start > 0) {
                const char ch = before[start - 1];
                if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.' || ch == '%' || ch == '[' || ch == ']') --start;
                else break;
            }
            from = start;
            const auto prefix = before.substr(start);
            if (editMode_ == BlockType) {
                const auto want = upper(prefix);
                std::vector<std::pair<std::string, std::string>> names;
                if (auto doc = app_.document())
                    for (const auto& pou : doc->pous)
                        if (pou.kind == domain::PouKind::FunctionBlockType)
                            names.emplace_back(std::string(doc->strings.text(pou.name)), "DFB du projet");
                for (const auto& b : project::BlockLibrary::shared().all())
                    names.emplace_back(b.name, b.family.empty() ? b.library : b.family);
                for (const auto& [name, detail] : names) {
                    if (upper(name).find(want) == std::string::npos) continue;
                    InputText::Suggestion s;
                    s.text = name;
                    s.detail = detail;
                    s.icon = Icon::FunctionBlock;
                    out.push_back(std::move(s));
                    if (out.size() >= 60) break;
                }
                return;
            }
            auto doc = app_.document();
            if (!doc) return;
            for (const auto& sg : project::suggestionsFor(*doc, section_, prefix)) {
                if (sg.kind == project::Suggestion::Kind::Keyword) continue;
                InputText::Suggestion s;
                s.text = sg.text;
                s.detail = sg.detail;
                s.icon = sg.kind == project::Suggestion::Kind::LocatedVariable ? Icon::LocatedVariable : Icon::Variable;
                out.push_back(std::move(s));
                if (out.size() >= 60) break;
            }
        });
        links_ += field_->editingDone->connect([this, alive](const std::string& text) {
            const auto a = alive.lock();
            if (!a || !*a || editMode_ == None) return;
            const bool cancel = field_->cancelled;
            field_->cancelled = false;
            if (cancel) { closeEditor(); say("saisie annul\xC3\xA9" "e"); return; }
            commitEditor(text);
        });
        reload();
    }

    LadderPane::~LadderPane() { *alive_ = false; }

    sim::Runtime* LadderPane::runtime() const { return app_.simulationRuntime(); }

    std::string LadderPane::runtimeSection() const {
        auto p = app_.project();
        if (!p || section_ >= p->sections.size()) return {};
        const auto& s = p->sections[section_];
        std::string name(p->strings.text(s.name));
        if (s.owner != domain::kNoIndex && s.owner < p->pous.size()) {
            const auto& pou = p->pous[s.owner];
            if (pou.kind == domain::PouKind::ProgramUnit) return std::string(p->strings.text(pou.name)) + "." + name;
        }
        return name;
    }

    void LadderPane::reload() {
        auto p = app_.project();
        if (!p || section_ >= p->sections.size()) { readable_ = false; return; }
        const auto& body = p->sections[section_].body;
        body_ = body;
        auto d = ld::parse(body);
        readable_ = static_cast<bool>(d);
        diagram_ = d ? std::move(*d) : ld::Diagram{};
        if (readable_ && diagram_.networks.empty()) {
            // un ladder sans reseau : on montre un reseau vide (rien n'est ecrit tant qu'on n'y touche pas)
            diagram_.networks.push_back(diagram_.makeNetwork(1));
            diagram_.gaps.push_back(diagram_.newline + diagram_.indent);
        }
        if (!ld::valid(diagram_, sel_)) sel_ = { 0, 0, 0 };
        if (canvas_) canvas_->invalidate();
        showSelection();
    }

    void LadderPane::onLayout() {
        const auto area = contentRect();
        const float barH = 32.f, stripH = 22.f;
        if (bar_) bar_->setBounds({ area.x, area.y, area.w, barH });
        if (canvas_) canvas_->setBounds({ area.x, area.y + barH + 2.f, area.w, std::max(0.f, area.h - barH - stripH - 4.f) });
        if (status_) status_->setBounds({ area.x, area.bottom() - stripH, area.w, stripH });
        placeField();
    }

    void LadderPane::onPaint(const PaintContext& ctx) {
        // Ctrl+Z, un renommage, le dossier relu : le corps a change sous nos yeux.
        if (auto p = app_.project(); p && section_ < p->sections.size() && p->sections[section_].body != body_ && !editing())
            reload();
        // en simulation : la valeur de ce qui est choisi (le bas se met a jour)
        if (runtime()) showSelection();
        (void)ctx;
    }

    void LadderPane::placeField() {
        if (!field_ || !canvas_ || editMode_ == None) return;
        canvas_->relayout();
        auto r = canvas_->cellRect(sel_, 2);
        if (editMode_ == PinValue || editMode_ == Instance || editMode_ == BlockType || editMode_ == NetComment) r.w = std::max(r.w, 260.f);
        r.w = std::max(r.w, 200.f);
        r.h = 26.f;
        r.y = canvas_->rowY(sel_.network, sel_.row) + 2.f;
        const auto area = canvas_->bounds();
        r.x = std::clamp(r.x, area.x, std::max(area.x, area.right() - r.w));
        r.y = std::clamp(r.y, area.y, std::max(area.y, area.bottom() - r.h));
        field_->setBounds(r);
    }

    void LadderPane::say(const std::string& text, bool error) {
        lastMessage_ = text;
        if (status_) status_->setMessage(text, error ? StatusBar::Severity::Error : StatusBar::Severity::Info);
    }

    std::string LadderPane::selectedVariable() const {
        const auto* c = ld::cellAt(diagram_, ld::anchorOf(diagram_, sel_));
        if (!c || (c->kind != ld::Kind::Contact && c->kind != ld::Kind::Coil)) return {};
        return c->name;
    }

    void LadderPane::showSelection() {
        if (!status_) return;
        std::string line;
        if (!readable_) line = "sch\xC3\xA9ma illisible";
        else if (ld::valid(diagram_, sel_)) {
            const auto& n = diagram_.networks[static_cast<std::size_t>(sel_.network)];
            line = (diagram_.networks.size() > 1 ? "r\xC3\xA9seau " + std::to_string(sel_.network + 1) + ", " : std::string{})
                + "rang\xC3\xA9" "e " + std::to_string(sel_.row + 1) + ", colonne " + std::to_string(sel_.col + 1) + " : ";
            if (const int bi = n.blockAt(sel_.row, sel_.col); bi >= 0) {
                const auto& b = n.blocks[static_cast<std::size_t>(bi)];
                line += "bloc " + b.type + (b.isFunction() ? std::string{} : " " + b.instance) + " (double-clic sur une broche : son param\xC3\xA8tre)";
            }
            else {
                const auto* c = ld::cellAt(diagram_, ld::anchorOf(diagram_, sel_));
                switch (c ? c->kind : ld::Kind::Empty) {
                case ld::Kind::Contact: line += "contact " + std::string(ld::contactTypeName(c->contact)) + " " + c->name; break;
                case ld::Kind::Coil: line += "bobine " + std::string(ld::coilTypeName(c->coil)) + " " + c->name; break;
                case ld::Kind::Compare: line += "comparaison [" + c->text + "]"; break;
                case ld::Kind::Operate: line += "op\xC3\xA9ration [" + c->text + "]"; break;
                case ld::Kind::Wire: line += "fil"; break;
                case ld::Kind::Jump: line += "saut vers " + c->name; break;
                case ld::Kind::Return: line += "RETURN"; break;
                case ld::Kind::Label: line += "\xC3\xA9tiquette " + c->name; break;
                case ld::Kind::Raw: line += "\xC3\xA9l\xC3\xA9ment inconnu <" + c->element + "> (gard\xC3\xA9 tel quel)"; break;
                default: line += "vide (C contact, B bobine, F bloc, Inser ligne neuve)"; break;
                }
            }
            if (auto* rt = runtime()) {
                const auto var = selectedVariable();
                sim::Value v;
                if (!var.empty() && rt->get(var, v))
                    line += "   = " + v.display() + (rt->isForced(var) ? "  (forc\xC3\xA9" "e : L la lib\xC3\xA8re)" : "  (1 / 0 : forcer)");
            }
        }
        if (line != statusLine_) {
            statusLine_ = line;
            status_->setMessage(line);
        }
    }

    bool LadderPane::select(ld::Pos p) {
        if (!ld::valid(diagram_, p)) return false;
        if (editing()) closeEditor();
        sel_ = p;
        editPin_ = -1;
        if (canvas_) { canvas_->reveal(p); canvas_->invalidate(); }
        showSelection();
        return true;
    }

    bool LadderPane::revealListingLine(std::size_t line) {
        auto p = app_.project();
        if (!p || section_ >= p->sections.size()) return false;
        const auto& s = p->sections[section_];
        const auto head = project::graphListingHeaderLines(s);
        if (line <= head) return false;
        const auto lines = project::graphListing(s);
        const auto i = line - head - 1;
        if (i >= lines.size() || lines[i].network < 0) return false;
        return select({ lines[i].network, std::max(0, lines[i].row), 0 });
    }

    bool LadderPane::change(const std::function<bool(ld::Diagram&)>& op, const std::string& label) {
        if (!readable_) { say("sch\xC3\xA9ma illisible : rien n'est modifi\xC3\xA9", true); return false; }
        auto next = diagram_;
        if (!op(next)) { say(label + " : impossible ici", true); return false; }
        const auto body = ld::write(next);
        if (body == body_) return false;
        auto doc = app_.document();
        if (!doc) return false;
        app_.apply(std::make_unique<ld::LadderEditCommand>(doc, section_, body, "Ladder : " + label), /*refreshViews=*/false);
        const auto keep = sel_;
        reload();
        if (ld::valid(diagram_, keep)) sel_ = keep;
        if (body_ != body) { say(label + " : refus\xC3\xA9", true); return false; }
        say(label + " (Ctrl+Z le d\xC3\xA9" "fait)");
        edited->emit();
        return true;
    }

    // ---------------------------------------------------------------- la saisie
    bool LadderPane::openEditor() {
        if (!readable_ || !ld::valid(diagram_, sel_)) return false;
        const auto& n = diagram_.networks[static_cast<std::size_t>(sel_.network)];
        std::string text;
        if (editMode_ == NetComment) text = ld::networkComment(diagram_, sel_.network);
        else if (editMode_ == Name) {
            if (const auto* c = ld::cellAt(diagram_, ld::anchorOf(diagram_, sel_))) text = c->name;
        }
        else if (editMode_ == None) {
            if (const int bi = n.blockAt(sel_.row, sel_.col); bi >= 0) {
                const auto& b = n.blocks[static_cast<std::size_t>(bi)];
                if (editPin_ >= 0 && editPin_ < static_cast<int>(b.pins.size())) {
                    editMode_ = PinValue;
                    text = b.pins[static_cast<std::size_t>(editPin_)].effective;
                }
                else if (!b.isFunction()) { editMode_ = Instance; text = b.instance; }
                else {
                    // une fonction : sa premiere entree sans fil
                    for (std::size_t k = 0; k < b.pins.size(); ++k)
                        if (b.pins[k].dir == ld::PinDir::In && b.pins[k].formal != "EN") { editPin_ = static_cast<int>(k); break; }
                    if (editPin_ < 0) return false;
                    editMode_ = PinValue;
                    text = b.pins[static_cast<std::size_t>(editPin_)].effective;
                }
            }
            else {
                const auto a = ld::anchorOf(diagram_, sel_);
                const auto* c = ld::cellAt(diagram_, a);
                if (!c) return false;
                sel_ = a;
                switch (c->kind) {
                case ld::Kind::Contact: case ld::Kind::Coil: case ld::Kind::Jump: case ld::Kind::Label:
                    editMode_ = Name; text = c->name; break;
                case ld::Kind::Compare: case ld::Kind::Operate:
                    editMode_ = Text; text = c->text; break;
                default:
                    say("rien \xC3\xA0 nommer ici : C pose un contact, B une bobine, F un bloc");
                    return false;
                }
            }
        }
        field_->setText(text);
        field_->setPlaceholder(editMode_ == BlockType ? "type du bloc : TON, CTU, ADD, un DFB..."
                               : editMode_ == NetComment ? "commentaire du r\xC3\xA9seau"
                               : editMode_ == Text || editMode_ == NewCompare ? "expression ST : Niveau > 80"
                               : editMode_ == NewOperate ? "instruction ST : N := N + 1;"
                               : "variable (Ctrl+Espace : la liste)");
        field_->cancelled = false;
        field_->setVisibility(Visibility::Visible);
        placeField();
        field_->takeFocus();
        if (editMode_ != NetComment) field_->openSuggestions();
        invalidateLayout();
        if (canvas_) canvas_->invalidate();
        return true;
    }

    void LadderPane::closeEditor() {
        editMode_ = None;
        editPin_ = -1;
        if (field_) {
            field_->closeSuggestions();
            field_->setVisibility(Visibility::Collapsed);
        }
        if (canvas_) { canvas_->takeFocus(); canvas_->invalidate(); }
    }

    void LadderPane::commitEditor(const std::string& raw) {
        const int mode = editMode_;
        const int pin = editPin_;
        const auto at = sel_;
        editMode_ = None;     // avant de rendre le clavier : la fin de saisie qui suit ne compte plus
        closeEditor();
        std::string text = raw;
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
        switch (mode) {
        case Name: change([&](ld::Diagram& d) { return ld::setName(d, at, text); }, "Variable " + text); break;
        case Text: change([&](ld::Diagram& d) { return ld::setText(d, at, text); }, "Texte"); break;
        case PinValue:
            if (const int bi = diagram_.networks[static_cast<std::size_t>(at.network)].blockAt(at.row, at.col); bi >= 0)
                change([&](ld::Diagram& d) { return ld::setPin(d, at.network, bi, pin, text); }, "Param\xC3\xA8tre " + text);
            break;
        case Instance: change([&](ld::Diagram& d) { return ld::setName(d, at, text); }, "Instance " + text); break;
        case BlockType: if (!text.empty()) placeBlockOfType(text); break;
        case NetComment: change([&](ld::Diagram& d) { return ld::setNetworkComment(d, at.network, text); }, "Commentaire"); break;
        case NewContact: case NewClosed: case NewRising: case NewFalling: {
            if (text.empty()) { say("un contact a besoin d'une variable", true); break; }
            const auto t = mode == NewClosed ? ld::ContactType::Closed : mode == NewRising ? ld::ContactType::Rising
                         : mode == NewFalling ? ld::ContactType::Falling : ld::ContactType::Open;
            change([&](ld::Diagram& d) { return ld::placeContact(d, at, t, text); }, "Contact " + text);
            break;
        }
        case NewCoil: case NewNotCoil: case NewSet: case NewReset: {
            if (text.empty()) { say("une bobine a besoin d'une variable", true); break; }
            const auto t = mode == NewNotCoil ? ld::CoilType::Negated : mode == NewSet ? ld::CoilType::Set
                         : mode == NewReset ? ld::CoilType::Reset : ld::CoilType::Normal;
            change([&](ld::Diagram& d) { return ld::placeCoil(d, at, t, text); }, "Bobine " + text);
            break;
        }
        case NewCompare:
            if (!text.empty()) change([&](ld::Diagram& d) { return ld::placeCompare(d, at, text, 2); }, "Comparaison");
            break;
        case NewOperate:
            if (!text.empty()) change([&](ld::Diagram& d) { return ld::placeOperate(d, at, text, 3); }, "Op\xC3\xA9ration");
            break;
        default: break;
        }
        showSelection();
    }

    bool LadderPane::typeInEditor(const std::string& text, bool accept) {
        if (!editing() || !field_) return false;
        field_->setText(text);
        field_->openSuggestions();
        if (accept) {
            const auto t = field_->text();
            commitEditor(t);
        }
        return true;
    }

    std::vector<std::string> LadderPane::editorSuggestions() const {
        std::vector<std::string> out;
        if (field_) for (const auto& s : field_->suggestions()) out.push_back(s.text);
        return out;
    }

    bool LadderPane::setSelectionText(const std::string& text) {
        if (!ld::valid(diagram_, sel_)) return false;
        const auto at = sel_;
        const auto& n = diagram_.networks[static_cast<std::size_t>(at.network)];
        if (const int bi = n.blockAt(at.row, at.col); bi >= 0) {
            if (editPin_ >= 0) {
                const int pin = editPin_;
                return change([&](ld::Diagram& d) { return ld::setPin(d, at.network, bi, pin, text); }, "Param\xC3\xA8tre " + text);
            }
            return change([&](ld::Diagram& d) { return ld::setName(d, at, text); }, "Instance " + text);
        }
        const auto* c = ld::cellAt(diagram_, ld::anchorOf(diagram_, at));
        if (c && (c->kind == ld::Kind::Compare || c->kind == ld::Kind::Operate))
            return change([&](ld::Diagram& d) { return ld::setText(d, ld::anchorOf(d, at), text); }, "Texte");
        return change([&](ld::Diagram& d) { return ld::setName(d, ld::anchorOf(d, at), text); }, "Variable " + text);
    }

    bool LadderPane::placeBlockOfType(const std::string& type) {
        if (!ld::valid(diagram_, sel_)) return false;
        auto doc = app_.document();
        ld::Block b;
        if (!ld::makeBlock(doc.get(), type, ld::freeInstanceName(doc.get(), diagram_, type), false, b)) {
            say("bloc inconnu : " + type + " (la biblioth\xC3\xA8que et les DFB du projet)", true);
            return false;
        }
        const auto at = sel_;
        const bool needsInstance = !b.isFunction();
        bool declared = false;
        if (needsInstance && doc) {
            for (const auto& v : doc->variables)
                if (upper(doc->strings.text(v.name)) == upper(b.instance)) declared = true;
        }
        // UN pas de Ctrl+Z : l'instance declaree et le bloc pose
        const std::string label = "Bloc " + b.type + (needsInstance ? " " + b.instance : std::string{});
        const bool group = needsInstance && !declared && doc;
        if (group) {
            app_.commands().beginGroup("Ladder : " + label);
            project::AddVariableCommand::Spec spec;
            spec.name = b.instance;
            spec.type = b.type;
            const auto& s = doc->sections[section_];
            if (s.owner != domain::kNoIndex && s.owner < doc->pous.size() && doc->pous[s.owner].kind != domain::PouKind::Section) {
                spec.scope = domain::VariableScope::Local;
                spec.owner = s.owner;
            }
            app_.apply(std::make_unique<project::AddVariableCommand>(doc, spec), false);
        }
        const bool ok = change([&](ld::Diagram& d) { return ld::placeBlock(d, at, b) >= 0; }, label);
        if (group) app_.commands().endGroup();
        if (ok) {
            // le choix sur la ligne du bloc (sa premiere broche)
            const auto& n = diagram_.networks[static_cast<std::size_t>(at.network)];
            for (const auto& bb : n.blocks)
                if (bb.instance == b.instance && bb.type == b.type) sel_ = { at.network, bb.y + 1, bb.x };
            if (canvas_) canvas_->reveal(sel_);
            say(label + (group ? " (instance d\xC3\xA9" "clar\xC3\xA9" "e) " : " ") + "- double-clic sur une broche pour son param\xC3\xA8tre");
        }
        return ok;
    }

    bool LadderPane::forceSelected(int value) {
        auto* rt = runtime();
        const auto var = selectedVariable();
        if (!rt || var.empty()) { say("forcer : choisis un contact ou une bobine, la simulation en marche", true); return false; }
        bool ok = false;
        if (value < 0) ok = rt->unforce(var);
        else ok = rt->force(var, sim::Value::boolean(value != 0));
        say(ok ? (value < 0 ? var + " lib\xC3\xA9r\xC3\xA9" "e" : var + " forc\xC3\xA9" "e \xC3\xA0 " + std::to_string(value))
               : "forcer " + var + " : refus\xC3\xA9", !ok);
        if (canvas_) canvas_->invalidate();
        return ok;
    }

    // ---------------------------------------------------------------- les gestes
    void LadderPane::command(const std::string& id) {
        if (id == "ld.zoomIn") { canvas_->setZoom(canvas_->zoom() * 1.2f); return; }
        if (id == "ld.zoomOut") { canvas_->setZoom(canvas_->zoom() / 1.2f); return; }
        if (id == "ld.fit") { canvas_->setZoom(1.f); return; }
        if (id == "ld.force1") { forceSelected(1); return; }
        if (id == "ld.force0") { forceSelected(0); return; }
        if (id == "ld.unforce") { forceSelected(-1); return; }
        if (!readable_) { say("sch\xC3\xA9ma illisible : rien n'est modifi\xC3\xA9", true); return; }
        if (!ld::valid(diagram_, sel_)) sel_ = { 0, 0, 0 };
        if (editing()) closeEditor();
        const auto at = ld::anchorOf(diagram_, sel_);
        const int cols = diagram_.networks.empty() ? diagram_.columns
                         : static_cast<int>(diagram_.networks[static_cast<std::size_t>(at.network)].cells[static_cast<std::size_t>(at.row)].size());
        auto startNew = [&](int mode, ld::Pos where) {
            sel_ = where;
            editMode_ = mode;
            editPin_ = -1;
            openEditor();
        };
        if (id == "ld.contact") { startNew(NewContact, at); return; }
        if (id == "ld.closed") { startNew(NewClosed, at); return; }
        if (id == "ld.rising") { startNew(NewRising, at); return; }
        if (id == "ld.falling") { startNew(NewFalling, at); return; }
        if (id == "ld.coil" || id == "ld.notCoil" || id == "ld.set" || id == "ld.reset") {
            // une bobine : dans la derniere colonne de la rangee
            const int mode = id == "ld.coil" ? NewCoil : id == "ld.notCoil" ? NewNotCoil : id == "ld.set" ? NewSet : NewReset;
            startNew(mode, { at.network, at.row, cols - 1 });
            return;
        }
        if (id == "ld.compare") { startNew(NewCompare, { at.network, at.row, std::min(at.col, std::max(0, cols - 2)) }); return; }
        if (id == "ld.operate") { startNew(NewOperate, { at.network, at.row, std::max(0, cols - 3) }); return; }
        if (id == "ld.block") { editMode_ = BlockType; editPin_ = -1; sel_ = at; openEditor(); return; }
        if (id == "ld.comment") { sel_ = at; editMode_ = NetComment; openEditor(); return; }
        if (id == "ld.edit") { openEditor(); return; }
        if (id == "ld.wire") { change([&](ld::Diagram& d) { return ld::placeWire(d, at); }, "Fil"); return; }
        if (id == "ld.vlink") {
            const auto* c = ld::cellAt(diagram_, at);
            const int b = at.col + (c ? std::max(1, c->width) : 1);
            const bool on = !diagram_.networks[static_cast<std::size_t>(at.network)].link(at.row, b);
            change([&](ld::Diagram& d) { return ld::setLink(d, at.network, at.row, b, on); },
                   on ? "Liaison verticale" : "Liaison verticale retir\xC3\xA9" "e");
            return;
        }
        if (id == "ld.parallel") {
            ld::Pos made{ -1, -1, -1 };
            if (change([&](ld::Diagram& d) { made = ld::addParallel(d, at); return made.row >= 0; }, "Branche en parall\xC3\xA8le")) {
                sel_ = made;
                editMode_ = Name;
                openEditor();
            }
            return;
        }
        if (id == "ld.invert") { change([&](ld::Diagram& d) { return ld::invert(d, at); }, "Inverser"); return; }
        if (id == "ld.type") { change([&](ld::Diagram& d) { return ld::cycleType(d, at); }, "Type"); return; }
        if (id == "ld.erase") { change([&](ld::Diagram& d) { return ld::erase(d, at); }, "Supprimer"); return; }
        if (id == "ld.rung") {
            int row = -1;
            if (change([&](ld::Diagram& d) { row = ld::insertRung(d, at.network, at.row); return row >= 0; }, "Ligne neuve")) {
                sel_ = { at.network, row, 0 };
                if (canvas_) canvas_->reveal(sel_);
                showSelection();
            }
            return;
        }
        if (id == "ld.delRung") { change([&](ld::Diagram& d) { return ld::deleteRung(d, at.network, at.row); }, "Ligne supprim\xC3\xA9" "e"); return; }
        if (id == "ld.row") { change([&](ld::Diagram& d) { return ld::insertRow(d, at.network, at.row + 1); }, "Rang\xC3\xA9" "e"); return; }
        if (id == "ld.delRow") { change([&](ld::Diagram& d) { return ld::deleteRow(d, at.network, at.row); }, "Rang\xC3\xA9" "e supprim\xC3\xA9" "e"); return; }
        say("commande inconnue : " + id, true);
    }

    bool runLadderScript(App& app, LadderPane& pane, const std::vector<std::string>& w, std::string& said, std::string& error) {
        auto arg = [&](std::size_t i) { return i < w.size() ? w[i] : std::string{}; };
        const std::string cmd = arg(0);
        auto num = [&](std::size_t i, int fallback) { return arg(i).empty() ? fallback : std::atoi(arg(i).c_str()); };
        auto result = [&](bool ok) {
            if (!ok) error = pane.lastMessage().empty() ? std::string("refus\xC3\xA9") : pane.lastMessage();
            else said = pane.lastMessage();
            return ok;
        };
        if (cmd == "ladder-choisir") {
            const ld::Pos p{ num(3, 1) - 1, num(1, 1) - 1, num(2, 1) - 1 };
            if (!pane.select(p)) { error = "pas de case " + arg(1) + " " + arg(2); return false; }
            said = pane.describe();
            return true;
        }
        if (cmd == "ladder-outil") { pane.command(arg(1)); said = pane.lastMessage(); return true; }
        if (cmd == "ladder-taper") {
            if (!pane.typeInEditor(arg(1), arg(2) == "entree")) { error = "pas de saisie ouverte"; return false; }
            std::string list;
            for (const auto& s : pane.editorSuggestions()) list += (list.empty() ? "" : ", ") + s;
            said = arg(2) == "entree" ? pane.lastMessage() : "propose : " + list;
            return true;
        }
        if (cmd == "ladder-nommer") return result(pane.setSelectionText(arg(1)));
        if (cmd == "ladder-bloc") return result(pane.placeBlockOfType(arg(1)));
        if (cmd == "ladder-broche") {
            const auto sel = pane.selection();
            const auto& d = pane.diagram();
            if (!ld::valid(d, sel)) { error = "rien de choisi"; return false; }
            const auto& n = d.networks[static_cast<std::size_t>(sel.network)];
            const int bi = n.blockAt(sel.row, sel.col);
            if (bi < 0) { error = "pas de bloc choisi"; return false; }
            const int k = num(1, 1) - 1;
            pane.select(sel);
            const auto text = arg(2);
            const auto label = "Param\xC3\xA8tre " + text;
            // la broche, puis son parametre (un pas de Ctrl+Z)
            auto next = d;
            if (!ld::setPin(next, sel.network, bi, k, text)) { error = "pas de broche " + arg(1); return false; }
            auto doc = app.document();
            if (!doc) return false;
            app.apply(std::make_unique<ld::LadderEditCommand>(doc, pane.section(), ld::write(next), "Ladder : " + label), false);
            pane.reload();
            said = label;
            return true;
        }
        if (cmd == "ladder-forcer") {
            auto* rt = pane.runtime();
            if (!rt) { error = "pas de simulation en marche"; return false; }
            const auto v = arg(2);
            const bool ok = v == "non" ? rt->unforce(arg(1)) : rt->force(arg(1), sim::Value::boolean(v == "1"));
            if (!ok) error = "forcer " + arg(1) + " : refus\xC3\xA9";
            said = arg(1) + (v == "non" ? " lib\xC3\xA9r\xC3\xA9" "e" : " forc\xC3\xA9" "e \xC3\xA0 " + v);
            return ok;
        }
        if (cmd == "ladder-annuler") {
            const auto r = app.actions().trigger("edit.undo", app.commands());
            pane.reload();
            if (!r) error = r.error().message();
            said = "Ctrl+Z";
            return static_cast<bool>(r);
        }
        if (cmd == "ladder-etat") {
            said = pane.describe();
            if (!arg(1).empty() && said.find(arg(1)) == std::string::npos) {
                error = "attendu \"" + arg(1) + "\" : " + said;
                return false;
            }
            return true;
        }
        error = "commande inconnue : " + cmd;
        return false;
    }

    void revealLadderLine(ui::Widget* root, domain::Index section, std::size_t line) {
        if (!root || line == 0) return;
        auto* w = root->findById("analysis.doc." + std::to_string(section) + ".ld");
        if (auto* pane = dynamic_cast<LadderPane*>(w)) (void)pane->revealListingLine(line);
    }

    std::string LadderPane::describe() const {
        std::string out = "ladder : " + std::to_string(diagram_.networks.size()) + " r\xC3\xA9seau(x)";
        int rows = 0, elements = 0;
        for (const auto& n : diagram_.networks) {
            rows += n.rows();
            for (const auto& r : n.cells)
                for (const auto& c : r)
                    elements += (c.kind != ld::Kind::Empty && c.kind != ld::Kind::Covered && c.kind != ld::Kind::Wire) ? 1 : 0;
            elements += static_cast<int>(n.blocks.size());
        }
        out += ", " + std::to_string(rows) + " rang\xC3\xA9" "es, " + std::to_string(elements) + " \xC3\xA9l\xC3\xA9ments ; choix ("
             + std::to_string(sel_.network) + "," + std::to_string(sel_.row) + "," + std::to_string(sel_.col) + ")";
        if (!statusLine_.empty()) out += " " + statusLine_;
        if (const auto* rt = runtime()) {
            if (const auto* live = rt->ladderLive(runtimeSection())) {
                int lit = 0;
                for (const auto& n : live->networks)
                    for (const auto& r : n.cell)
                        for (auto v : r) lit += v ? 1 : 0;
                out += " ; simulation : passage " + std::to_string(live->pass) + ", " + std::to_string(lit) + " \xC3\xA9l\xC3\xA9ment(s) sous tension";
            }
        }
        return out;
    }

} // namespace app
