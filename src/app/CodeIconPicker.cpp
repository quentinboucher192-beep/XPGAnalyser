// =============================================================================
//  app/CodeIconPicker.cpp - 1.8.0 : voir CodeIconPicker.hpp
// =============================================================================
#include "CodeIconPicker.hpp"

#include "../core/CodeIcons.hpp"
#include "../menu/MenuManager.hpp"
#include "../ui/Icons.hpp"
#include "../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>

namespace app {

namespace ci = core::codeicons;

namespace {
constexpr float kPanelW = 660.f;
constexpr float kCellH = 50.f;
constexpr float kTitleH = 34.f;
constexpr float kFootH = 50.f;
constexpr int   kCols = 2;
const gfx::FontId kSmall{13};

std::vector<std::string> wrap2(const std::string& s, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(trial, f) > w) {
            out.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        word.clear();
    };
    for (const char c : s) {
        if (c == ' ') flush();
        else word += c;
    }
    flush();
    if (!line.empty()) out.push_back(line);
    if (out.size() > 2) {
        out.resize(2);
        out[1] += "\xE2\x80\xA6";
    }
    return out;
}
} // namespace

// Le corps : le cadre, la grille, le pied (les boutons sont des enfants).
class PickerBody final : public ui::Widget {
public:
    explicit PickerBody(CodeIconPicker& owner) : ui::Widget("codeIconPicker.body"), owner_(owner) {}
    ui::Button* none{nullptr};
    ui::Button* close{nullptr};

    [[nodiscard]] gfx::Rect cell(int i) const {
        const float cw = (panel_.w - 24.f) / static_cast<float>(kCols);
        const int row = i / kCols, col = i % kCols;
        return {panel_.x + 12.f + static_cast<float>(col) * cw, panel_.y + kTitleH + 8.f + static_cast<float>(row) * kCellH, cw - 4.f, kCellH - 4.f};
    }
    [[nodiscard]] int hit(gfx::Point p) const {
        for (int i = 0; i < static_cast<int>(ci::kCount); ++i)
            if (cell(i).contains(p)) return i;
        return -1;
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const int rows = (static_cast<int>(ci::kCount) + kCols - 1) / kCols;
        const float h = kTitleH + 16.f + static_cast<float>(rows) * kCellH + kFootH;
        panel_ = {std::floor((r.w - kPanelW) * 0.5f), std::floor(std::max(20.f, (r.h - h) * 0.5f)), kPanelW, h};
        const float by = panel_.y + panel_.h - kFootH + 10.f;
        float bx = panel_.x + panel_.w - 14.f;
        for (auto* b : {close, none}) {
            if (!b) continue;
            const float bw = std::max(96.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 32.f);
            bx -= bw;
            b->setBounds({bx, by, bw, 30.f});
            bx -= 10.f;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel_, c.panelBg);
        ctx.r.strokeRect(panel_, c.borderStrong, 1.f);
        ctx.r.fillRect({panel_.x, panel_.y, panel_.w, kTitleH}, c.headerBg);
        ui::drawIcon(ctx.r, ui::Icon::Image, {panel_.x + 12.f, panel_.y + 9.f, 16.f, 16.f}, c.textMuted);
        ctx.r.drawText({panel_.x + 36.f, panel_.y + (kTitleH - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, owner_.spec_.title,
                       ctx.theme.font.uiBold, c.text);
        const std::string esc = "\xC3\x89" "chap : fermer";
        ctx.r.drawText({panel_.x + panel_.w - 14.f - ctx.r.measure(esc, kSmall).width, panel_.y + (kTitleH - ctx.r.lineHeight(kSmall)) * 0.5f}, esc, kSmall,
                       c.textMuted);
        const int current = ci::indexOf(owner_.spec_.current), suggested = ci::indexOf(owner_.spec_.suggestion);
        for (int i = 0; i < static_cast<int>(ci::kCount); ++i) {
            const auto r = cell(i);
            const auto& info = ci::info(static_cast<std::size_t>(i));
            const auto color = ui::codeIconColor(i);
            if (i == current) ctx.r.fillRoundedRect(r, c.selectionBg, 6.f);
            else if (i == owner_.hot_) ctx.r.fillRoundedRect(r, ctx.theme.brand.hover, 6.f);
            if (i == suggested && i != current) ctx.r.strokeRect(r, c.warning, 1.f);
            if (i == current) ctx.r.strokeRect(r, c.accent, 1.f);
            const gfx::Rect sw{r.x + 6.f, r.y + (r.h - 30.f) * 0.5f, 30.f, 30.f};
            ctx.r.fillRoundedRect(sw, color.withAlpha(46), 6.f);
            ui::drawIcon(ctx.r, ui::codeIcon(i), {sw.x + 6.f, sw.y + 6.f, 18.f, 18.f}, color);
            const float tx = sw.x + sw.w + 9.f;
            float tw = r.x + r.w - tx - 6.f;
            if (i == suggested && i != current) {
                const std::string tag = "sugg\xC3\xA9r\xC3\xA9" "e";
                const float w = ctx.r.measure(tag, kSmall).width;
                ctx.r.drawText({r.x + r.w - w - 8.f, r.y + 5.f}, tag, kSmall, c.warning);
                tw -= w + 8.f;
            }
            ctx.r.drawText({tx, r.y + 4.f}, std::string(info.name), ctx.theme.font.uiBold, c.text);
            float y = r.y + 4.f + ctx.r.lineHeight(ctx.theme.font.uiBold);
            for (const auto& l : wrap2(std::string(info.description), kSmall, r.x + r.w - tx - 6.f)) {
                ctx.r.drawText({tx, y}, l, kSmall, c.textMuted);
                y += ctx.r.lineHeight(kSmall);
            }
            (void)tw;
        }
        // Le pied : ce que dit la suggestion.
        const float fy = panel_.y + panel_.h - kFootH;
        ctx.r.fillRect({panel_.x, fy, panel_.w, 1.f}, c.border);
        std::string note = suggested >= 0 ? "Suggestion d'apr\xC3\xA8s le nom : " + std::string(ci::info(static_cast<std::size_t>(suggested)).name) + ". C'est toi qui choisis."
                                          : "Pas de suggestion pour ce nom : c'est toi qui choisis.";
        ctx.r.drawText({panel_.x + 14.f, fy + (kFootH - ctx.r.lineHeight(kSmall)) * 0.5f}, note, kSmall, c.textMuted);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            const int h = hit(m->pos);
            if (h != owner_.hot_) {
                owner_.hot_ = h;
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
            const int h = hit(d->pos);
            if (h >= 0) {
                owner_.choose(h);
                return ui::EventResult::Consumed;
            }
            if (!panel_.contains(d->pos)) {
                owner_.cancel();          // un clic hors du mini-menu : il se ferme
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

private:
    CodeIconPicker& owner_;
    gfx::Rect       panel_{};
};

CodeIconPicker::CodeIconPicker(Spec spec) : menu::WidgetMenu("dialog.codeIcon"), spec_(std::move(spec)) {
    hot_ = ci::indexOf(spec_.current);
    if (hot_ < 0) hot_ = ci::indexOf(spec_.suggestion);
}

menu::MenuTraits CodeIconPicker::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status CodeIconPicker::buildUi() {
    auto body = std::make_unique<PickerBody>(*this);
    auto* raw = body.get();
    raw->none = &static_cast<ui::Button&>(raw->addChild(std::make_unique<ui::Button>("Aucune ic\xC3\xB4ne", "codeIconPicker.none")));
    raw->close = &static_cast<ui::Button&>(raw->addChild(std::make_unique<ui::Button>("Annuler", "codeIconPicker.cancel")));
    raw->none->setTooltip("L'\xC3\xA9l\xC3\xA9ment reprend son ic\xC3\xB4ne de toujours");
    links_ += raw->none->clicked->connect([this] { choose(-1); });
    links_ += raw->close->clicked->connect([this] { cancel(); });
    setRoot(std::move(body));
    return core::ok();
}

void CodeIconPicker::choose(int index) {
    if (done_) return;
    done_ = true;
    const std::string key = index >= 0 && index < static_cast<int>(ci::kCount) ? std::string(ci::info(static_cast<std::size_t>(index)).key) : std::string{};
    manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, "icon=" + key});
}

void CodeIconPicker::cancel() {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
}

std::string CodeIconPicker::parse(const std::string& payload) { return payload.rfind("icon=", 0) == 0 ? payload.substr(5) : std::string{}; }

ui::EventResult CodeIconPicker::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        const int n = static_cast<int>(ci::kCount);
        const auto move = [&](int delta) {
            hot_ = hot_ < 0 ? 0 : std::clamp(hot_ + delta, 0, n - 1);
            root().invalidate();
        };
        switch (k->key) {
            case ui::Key::Escape: cancel(); return ui::EventResult::Consumed;
            case ui::Key::Return:
                if (hot_ >= 0) choose(hot_);
                return ui::EventResult::Consumed;
            case ui::Key::Left: move(-1); return ui::EventResult::Consumed;
            case ui::Key::Right: move(1); return ui::EventResult::Consumed;
            case ui::Key::Up: move(-kCols); return ui::EventResult::Consumed;
            case ui::Key::Down: move(kCols); return ui::EventResult::Consumed;
            default: break;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

} // namespace app
