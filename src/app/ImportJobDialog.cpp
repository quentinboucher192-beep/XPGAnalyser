// =============================================================================
//  app/ImportJobDialog.cpp - 1.8.0 : voir ImportJobDialog.hpp
// =============================================================================
#include "ImportJobDialog.hpp"

#include "../menu/MenuManager.hpp"
#include "../ui/Icons.hpp"
#include "../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace app {

namespace {
const gfx::FontId kSmall{13};
constexpr float kW = 600.f;

std::string seconds(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, s < 10.0 ? "%.2f s" : "%.1f s", s);
    std::string t(buf);
    for (auto& c : t)
        if (c == '.') c = ',';
    return t;
}
} // namespace

class ProgressBody final : public ui::Widget {
public:
    explicit ProgressBody(ImportJobDialog& d) : ui::Widget("importProgress.body"), d_(d) {}

protected:
    void onLayout() override {
        const auto r = bounds();
        const bool mast = d_.job_->kind() == ImportJob::Kind::Mast;
        const float h = 44.f + 22.f + 40.f + 18.f + static_cast<float>(d_.job_->steps().size()) * 26.f + 20.f + (mast ? 56.f : 56.f) + 40.f + 52.f;
        panel_ = {std::floor((r.w - kW) * 0.5f), std::floor(std::max(10.f, (r.h - h) * 0.5f)), kW, h};
        float bx = panel_.x + panel_.w - 16.f;
        for (auto* b : {d_.cancel_, d_.bg_}) {
            if (!b || !b->visible()) continue;
            const float bw = std::max(100.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 32.f);
            bx -= bw;
            b->setBounds({bx, panel_.y + panel_.h - 42.f, bw, 30.f});
            bx -= 10.f;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const auto& s = d_.state_;
        const auto& job = *d_.job_;
        const bool mast = job.kind() == ImportJob::Kind::Mast;
        ctx.r.fillRect(panel_, c.panelBg);
        ctx.r.strokeRect(panel_, c.borderStrong, 1.f);
        const float titleH = 36.f;
        ctx.r.fillRect({panel_.x, panel_.y, panel_.w, titleH}, c.headerBg);
        ui::drawIcon(ctx.r, mast ? ui::Icon::Program : ui::Icon::Rack, {panel_.x + 12.f, panel_.y + 10.f, 16.f, 16.f}, c.text);
        ctx.r.drawText({panel_.x + 36.f, panel_.y + (titleH - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, d_.title(), ctx.theme.font.uiBold, c.text);
        const std::string el = seconds(s.elapsed);
        ctx.r.drawText({panel_.x + panel_.w - 14.f - ctx.r.measure(el, kSmall).width, panel_.y + (titleH - ctx.r.lineHeight(kSmall)) * 0.5f}, el, kSmall, c.textMuted);
        const float x = panel_.x + 16.f, w = panel_.w - 32.f;
        float y = panel_.y + titleH + 10.f;
        std::string where = job.source() + (d_.where_.empty() ? std::string{} : "  \xE2\x86\x92  " + d_.where_);
        while (where.size() > 8 && ctx.r.measure(where, kSmall).width > w) where = where.substr(0, where.size() - 4) + "\xE2\x80\xA6";
        ctx.r.drawText({x, y}, where, kSmall, c.textMuted);
        y += 24.f;
        // Le pourcentage, l'etape.
        const int pct = static_cast<int>(std::lround(std::clamp(s.fraction, 0.f, 1.f) * 100.f));
        const std::string pc = std::to_string(pct) + " %";
        ctx.r.drawText({x, y}, pc, ctx.theme.font.uiBold, s.failed ? c.error : c.text);
        std::string detail = s.failed ? s.error : s.cancelled ? std::string("Annul\xC3\xA9 : le projet n'a pas chang\xC3\xA9.")
                           : job.done() ? "Termin\xC3\xA9 en " + seconds(s.elapsed) + (mast ? std::string(" : le r\xC3\xA9" "capitulatif s'ouvre\xE2\x80\xA6") : std::string{})
                                        : s.detail;
        const float dx = x + 70.f;
        while (detail.size() > 8 && ctx.r.measure(detail, ctx.theme.font.ui).width > w - 70.f) detail = detail.substr(0, detail.size() - 4) + "\xE2\x80\xA6";
        ctx.r.drawText({dx, y}, detail, ctx.theme.font.ui, s.failed ? c.error : c.text);
        y += 28.f;
        const gfx::Rect bar{x, y, w, 10.f};
        ctx.r.fillRoundedRect(bar, c.border, 5.f);
        const gfx::Color fill = s.failed ? c.error : job.done() ? c.ok : c.accent;
        ctx.r.fillRoundedRect({bar.x, bar.y, std::max(10.f, bar.w * std::clamp(s.fraction, 0.f, 1.f)), bar.h}, fill, 5.f);
        y += 22.f;
        // Les etapes.
        const auto& steps = job.steps();
        const gfx::Rect box{x, y, w, static_cast<float>(steps.size()) * 26.f + 8.f};
        ctx.r.fillRect(box, c.windowBg);
        ctx.r.strokeRect(box, c.border, 1.f);
        float sy = y + 4.f;
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const bool doneStep = i < s.seconds.size() && s.seconds[i] >= 0.0;
            const bool current = !doneStep && static_cast<int>(i) == s.step && !job.done();
            const gfx::Color col = doneStep ? c.text : current ? c.text : c.textMuted;
            const float cy = sy + 13.f;
            if (doneStep) {
                ctx.r.line({x + 12.f, cy}, {x + 16.f, cy + 4.f}, c.ok, 2.f);
                ctx.r.line({x + 16.f, cy + 4.f}, {x + 23.f, cy - 4.f}, c.ok, 2.f);
            } else if (current) {
                // Un arc qui tourne (8 traits, un plus clair qui avance).
                const int head = static_cast<int>(d_.now_ * 12.0) % 8;
                for (int k = 0; k < 8; ++k) {
                    const float a = 6.2831853f * static_cast<float>(k) / 8.f;
                    const gfx::Color t = k == head ? c.accent : c.border;
                    ctx.r.line({x + 17.f + 3.f * std::cos(a), cy + 3.f * std::sin(a)}, {x + 17.f + 6.f * std::cos(a), cy + 6.f * std::sin(a)}, t, 2.f);
                }
            } else {
                ctx.r.strokeRect({x + 13.f, cy - 4.f, 8.f, 8.f}, c.textMuted, 1.f);
            }
            const auto& st = steps[i];
            ctx.r.drawText({x + 34.f, sy + 5.f}, st.title, current ? ctx.theme.font.uiBold : ctx.theme.font.ui, col);
            const float tw = ctx.r.measure(st.title, current ? ctx.theme.font.uiBold : ctx.theme.font.ui).width;
            std::string d = "  \xC2\xB7  " + st.detail;
            while (d.size() > 8 && ctx.r.measure(d, kSmall).width > w - 120.f - tw) d = d.substr(0, d.size() - 4) + "\xE2\x80\xA6";
            ctx.r.drawText({x + 34.f + tw, sy + 7.f}, d, kSmall, c.textMuted);
            if (doneStep) {
                const std::string t = seconds(s.seconds[i]);
                ctx.r.drawText({x + w - 10.f - ctx.r.measure(t, kSmall).width, sy + 7.f}, t, kSmall, c.textMuted);
            }
            sy += 26.f;
        }
        y += box.h + 12.f;
        // Les compteurs.
        std::vector<std::pair<std::size_t, std::string>> counts;
        if (mast)
            counts = {{s.sections, "sections"}, {s.units, "unit\xC3\xA9s"}, {s.dfbs, "blocs DFB"}, {s.ddts, "types DDT"}, {s.variables, "variables"}};
        else
            counts = {{s.racks, "racks"}, {s.modules, "modules"}, {s.channels, "voies"}};
        const float cw = (w - 8.f * static_cast<float>(counts.size() - 1)) / static_cast<float>(counts.size());
        for (std::size_t i = 0; i < counts.size(); ++i) {
            const gfx::Rect cr{x + static_cast<float>(i) * (cw + 8.f), y, cw, 44.f};
            ctx.r.fillRoundedRect(cr, c.inputBg, 6.f);
            ctx.r.drawText({cr.x + 10.f, cr.y + 4.f}, std::to_string(counts[i].first), ctx.theme.font.uiBold, c.text);
            ctx.r.drawText({cr.x + 10.f, cr.y + 24.f}, counts[i].second, kSmall, c.textMuted);
        }
        y += 52.f;
        const std::string note = mast ? "L'application reste utilisable : cette fen\xC3\xAAtre suit un travail fait \xC3\xA0 c\xC3\xB4t\xC3\xA9. Rien ne change dans le projet avant la fin et ton accord."
                                      : "L'application reste utilisable : cette fen\xC3\xAAtre suit un travail fait \xC3\xA0 c\xC3\xB4t\xC3\xA9. Rien ne change avant la fin (Ctrl+Z le retirera).";
        std::string n1 = note;
        while (n1.size() > 8 && ctx.r.measure(n1, kSmall).width > w) {
            const auto sp = n1.rfind(' ');
            if (sp == std::string::npos) break;
            n1 = n1.substr(0, sp);
        }
        ctx.r.drawText({x, y}, n1, kSmall, c.textMuted);
        if (n1.size() < note.size()) ctx.r.drawText({x, y + 17.f}, note.substr(n1.size() + 1), kSmall, c.textMuted);
    }

private:
    ImportJobDialog& d_;
    gfx::Rect panel_{};
};

ImportJobDialog::ImportJobDialog(std::shared_ptr<ImportJob> job, std::string where)
    : menu::WidgetMenu("dialog.importProgress"), job_(std::move(job)), where_(std::move(where)) {
    if (job_) state_ = job_->state();
}

std::string ImportJobDialog::title() const {
    if (!job_) return "Importer";
    return job_->kind() == ImportJob::Kind::Mast ? "Importer " + job_->source() + " (nouveau MAST)"
                                                 : "Importer " + job_->source() + " (configuration mat\xC3\xA9rielle)";
}

menu::MenuTraits ImportJobDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;       // l'application continue : c'est le but
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status ImportJobDialog::buildUi() {
    auto body = std::make_unique<ProgressBody>(*this);
    bg_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Continuer en arri\xC3\xA8re-plan", "importProgress.arrierePlan")));
    bg_->setTooltip("La fen\xC3\xAAtre se ferme ; la barre du haut suit l'import, la cloche pr\xC3\xA9vient \xC3\xA0 la fin");
    cancel_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", "importProgress.annuler")));
    links_ += bg_->clicked->connect([this] { background(); });
    links_ += cancel_->clicked->connect([this] { cancelImport(); });
    setRoot(std::move(body));
    return core::ok();
}

void ImportJobDialog::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    now_ = f.totalSeconds;
    if (!job_ || closed_) return;
    state_ = job_->state();
    root().invalidate();
    if (!job_->done()) return;
    if (state_.failed) {
        // L'echec reste affiche : un seul bouton, Fermer.
        if (bg_->visible()) {
            bg_->setVisibility(ui::Visibility::Collapsed);
            cancel_->setText("Fermer");
            root().invalidateLayout();
        }
        return;
    }
    if (state_.cancelled) {
        finish("cancelled");
        return;
    }
    if (doneAt_ < 0.0) {
        doneAt_ = now_;
        bg_->setEnabled(false);
        cancel_->setEnabled(false);
    }
    // Termine : une seconde pour le lire, puis la suite.
    if (now_ - doneAt_ > 1.0) finish("done");
}

ui::EventResult ImportJobDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
        if (state_.failed) finish("failed");
        else background();          // Echap : on ne perd rien, l'import continue
        return ui::EventResult::Consumed;
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

void ImportJobDialog::background() {
    if (job_ && job_->done() && !state_.failed) {
        finish("done");
        return;
    }
    finish("background");
}

void ImportJobDialog::cancelImport() {
    if (state_.failed) {
        finish("failed");
        return;
    }
    if (job_) job_->cancel();
    finish("cancelled");
}

void ImportJobDialog::finish(const std::string& payload) {
    if (closed_) return;
    closed_ = true;
    manager().CloseDialog(menu::DialogResult{payload == "done" ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, payload});
}

} // namespace app
