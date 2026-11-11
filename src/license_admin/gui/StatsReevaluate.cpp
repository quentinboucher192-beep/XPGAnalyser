// =============================================================================
//  license_admin/gui/StatsReevaluate.cpp - 1.48.1 : << REEVALUER >> LES
//  STATISTIQUES (docs/STATISTIQUES.md, par. 15)
// -----------------------------------------------------------------------------
//  Le bouton de la page Statistiques (tous ses onglets) : le service remet ses
//  statistiques d'accord avec ce qui existe (ce qui est supprime sur ce poste lui
//  est dit d'abord : Workspace::statsReevaluate), puis une fenetre dit ce qu'il a
//  corrige, groupe par groupe (<< Licences actives, releve du 11/11 : 17 -> 15 >>,
//  << Globe : Lyon retiree >>...), ou << Rien a corriger >>. Ensuite toutes les
//  vues se relisent (changes::bump).
// =============================================================================
#include "AdminsKit.hpp"
#include "StatsKit.hpp"

#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace license_admin::gui::statskit {

namespace {

using namespace kit;

constexpr float kLine = 24.f;

class ReevaluateMenu;

class ReevaluateBody final : public ui::Widget {
public:
    ReevaluateBody(Dialog& owner, stats::ReevaluateReply reply) : ui::Widget("dialog.reevaluer.corps"), owner_(owner), reply_(std::move(reply)) {
        close_ = &button(*this, "Fermer", "reevaluer.fermer", ui::Button::Style::Primary);
        links_ += close_->clicked->connect([this] { owner_.close(); });
        lines_ = reevaluateLines(reply_);
    }

protected:
    void onLayout() override {
        frame_.place(*this, 720.f, 560.f);
        const auto& p = frame_.panel;
        const float cw = buttonWidth(*close_, 110.f);
        close_->setBounds({p.right() - 24.f - cw, p.bottom() - 52.f, cw, 34.f});
        list_ = {p.x + 24.f, p.y + 92.f, p.w - 48.f, p.bottom() - 70.f - (p.y + 92.f)};
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const float top = frame_.paint(ctx.r, ctx.theme, "R\xC3\xA9\xC3\xA9valuer les statistiques", stats::reevaluateSummary(reply_), ui::Icon::Refresh);
        (void)top;
        ctx.r.fillRect({frame_.panel.x + 1.f, frame_.panel.bottom() - 66.f, frame_.panel.w - 2.f, 1.f}, c.border);
        // la liste (defilante)
        const int first = static_cast<int>(scroll_);
        const int shown = visibleLines();
        for (int i = 0; i < shown && first + i < static_cast<int>(lines_.size()); ++i) {
            const auto& l = lines_[static_cast<std::size_t>(first + i)];
            const float y = list_.y + static_cast<float>(i) * kLine;
            const bool title = l.rfind("# ", 0) == 0;
            const std::string text = title ? l.substr(2) : "\xE2\x80\xA2 " + l;
            const FontId f = title ? gfx::semibold(FontId{15}) : FontId{14};
            ctx.r.drawText({list_.x + (title ? 0.f : 14.f), y + 3.f}, fit(ctx.r, text, f, list_.w - (title ? 0.f : 14.f)), f, title ? c.text : c.textMuted);
        }
        if (maxScroll() > 0.f) {
            const float h = list_.h * static_cast<float>(shown) / static_cast<float>(lines_.size());
            const float y = list_.y + (list_.h - h) * (scroll_ / maxScroll());
            ctx.r.fillRoundedRect({list_.right() + 8.f, y, 4.f, h}, c.borderStrong, 2.f);
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (frame_.hitClose(ev)) {
            owner_.close();
            return ui::EventResult::Consumed;
        }
        if (const auto* wh = std::get_if<ui::MouseWheel>(&ev); wh && list_.contains(wh->pos)) {
            scroll_ = std::clamp(scroll_ - wh->dy * 3.f, 0.f, maxScroll());
            invalidate();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    [[nodiscard]] int visibleLines() const { return std::max(1, static_cast<int>(list_.h / kLine)); }
    [[nodiscard]] float maxScroll() const { return static_cast<float>(std::max(0, static_cast<int>(lines_.size()) - visibleLines())); }

    Dialog&                  owner_;
    stats::ReevaluateReply   reply_;
    std::vector<std::string> lines_;
    Frame                    frame_;
    Rect                     list_{};
    float                    scroll_{0.f};
    ui::Button*              close_{nullptr};
    core::ConnectionScope    links_;
};

class ReevaluateMenu final : public Dialog {
public:
    explicit ReevaluateMenu(stats::ReevaluateReply reply) : Dialog("dialog.reevaluer"), reply_(std::move(reply)) {}
    [[nodiscard]] std::string title() const override { return "R\xC3\xA9\xC3\xA9valuer les statistiques"; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<ReevaluateBody>(*this, reply_));
        return core::ok();
    }

private:
    stats::ReevaluateReply reply_;
};

} // namespace

std::vector<std::string> reevaluateLines(const stats::ReevaluateReply& reply) {
    std::vector<std::string> out;
    if (reply.corrections.empty()) {
        out.push_back("# " + stats::reevaluateSummary(reply));
        out.push_back("Ce qui est montr\xC3\xA9 compte ce qui existe : rien n'a \xC3\xA9t\xC3\xA9 chang\xC3\xA9.");
        return out;
    }
    std::vector<std::string> groups = stats::correctionGroups();
    for (const auto& c : reply.corrections)
        if (std::find(groups.begin(), groups.end(), c.group) == groups.end()) groups.push_back(c.group);
    for (const auto& g : groups) {
        std::size_t n = 0;
        for (const auto& c : reply.corrections) n += c.group == g ? 1 : 0;
        if (!n) continue;
        out.push_back("# " + stats::correctionGroupTitle(g) + " (" + std::to_string(n) + ")");
        for (const auto& c : reply.corrections)
            if (c.group == g) out.push_back(c.text);
    }
    return out;
}

menu::MenuPtr makeReevaluateDialog(stats::ReevaluateReply reply) { return std::make_unique<ReevaluateMenu>(std::move(reply)); }

std::string reevaluateBlocked(const Workspace& w) {
    bool old = false;
    return blocked(w, &old);
}

bool startReevaluate(const ShellHost& host, std::function<void(const OpResult&, const stats::ReevaluateReply&)> done) {
    if (!host.runSlow) return false;
    const Workspace* w = &host.workspace();
    auto out = std::make_shared<stats::ReevaluateReply>();
    return host.runSlow(
        "Statistiques : r\xC3\xA9\xC3\xA9valuer", [w, out] { return fetchReevaluate(*w, *out); },
        [host, out, done = std::move(done)](const OpResult& r) {
            changes::bump();   // les vues se relisent (le service a pu corriger)
            if (r.ok && host.openDialog) host.openDialog(makeReevaluateDialog(*out));
            if (done) done(r, *out);
        });
}

} // namespace license_admin::gui::statskit
