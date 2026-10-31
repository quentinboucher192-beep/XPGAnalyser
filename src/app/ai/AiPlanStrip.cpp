// =============================================================================
//  app/ai/AiPlanStrip.cpp - voir AiPlanStrip.hpp
// =============================================================================
#include "AiPlanStrip.hpp"

#include "AiPanel.hpp"
#include "../../ui/Icons.hpp"

#include <algorithm>
#include <cmath>

namespace app::ai {

namespace {

const gfx::FontId kBody{14};
const gfx::FontId kMeta{13};
constexpr float kHeadH = 28.f;    // la ligne du haut : le plan, son etat, la croix
constexpr float kBarH = 20.f;     // l'avancement
constexpr float kRowH = 18.f;     // une etape
constexpr float kPhasesH = 18.f;  // les phases (depliee)
constexpr std::size_t kMaxRows = 10;

gfx::Color alpha(gfx::Color c, int a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(a, 0, 255))); }

std::string elide(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float width) {
    if (width <= 8.f) return {};
    if (r.measure(text, f).width <= width) return text;
    const std::string dots = "\xE2\x80\xA6";
    const std::size_t n = r.fitCharacters(text, f, std::max(0.f, width - r.measure(dots, f).width));
    return text.substr(0, n) + dots;
}

// L'etat d'une etape, en mots (ceux de la 1.21 : En attente, En cours, Reussie, Echec, Ignoree, Annulee) - et sa couleur.
struct StepLook {
    std::string  label;
    std::uint8_t tone{0};   // 0 attente, 1 ok, 2 attention, 3 erreur, 4 info, 5 eteint
};
StepLook lookOf(const plan::Step& st, const AiChatView::LivePlan& lp) {
    if (st.excluded) return {"Retir\xC3\xA9" "e", 5};
    switch (st.state) {
        case plan::State::Failed: return {"\xC3\x89" "chec", 3};
        case plan::State::Skipped: return {"Ignor\xC3\xA9" "e", 2};
        case plan::State::Cancelled: return {"Annul\xC3\xA9" "e", 5};
        case plan::State::Applied: return {"R\xC3\xA9ussie", 1};
        default: break;
    }
    // Pendant l'application de fond, seul l'avancement global est connu : << En cours >>, jamais une reussite inventee.
    if (lp.applying) return {"En cours", 4};
    if (st.state == plan::State::Validated) return {"V\xC3\xA9rifi\xC3\xA9" "e", 4};
    return {"En attente", 0};
}

gfx::Color toneColor(const ui::Theme& th, std::uint8_t tone) {
    const auto& c = th.color;
    switch (tone) {
        case 1: return c.ok;
        case 2: return c.warning;
        case 3: return c.error;
        case 4: return c.info;
        case 5: return c.textDisabled;
        default: return c.textMuted;
    }
}

// L'avancement montre : le travail de fond, sinon les etapes faites (verifiees a decider, reussies appliquees).
float progressOf(const AiChatView::LivePlan& lp, std::string* text) {
    if (lp.progress >= 0.f) {
        if (text) *text = std::to_string(static_cast<int>(lp.progress * 100.f + 0.5f)) + " %";
        return lp.progress;
    }
    const std::size_t total = lp.plan ? lp.plan->steps.size() : 0;
    if (total == 0) {
        if (text) text->clear();
        return 0.f;
    }
    std::size_t done = 0;
    for (const auto& st : lp.plan->steps)
        if (lp.open ? st.state == plan::State::Validated : st.state == plan::State::Applied) ++done;
    if (text)
        *text = std::to_string(done) + "/" + std::to_string(total)
                + (lp.open ? " v\xC3\xA9rifi\xC3\xA9" "es" : " r\xC3\xA9ussies");
    return static_cast<float>(done) / static_cast<float>(total);
}

} // namespace

AiPlanStrip::AiPlanStrip(std::string id, AiPanel& panel) : ui::Widget(std::move(id)), panel_(panel) {}

float AiPlanStrip::heightFor(float /*width*/, float maxH) const {
    const auto lp = panel_.chat().livePlan();
    if (!lp || closed_.count(lp->id)) return 0.f;
    float h = kHeadH + kBarH + 4.f;
    if (open_) {
        const std::size_t n = lp->plan ? lp->plan->steps.size() : 0;
        if (!lp->phases.empty()) h += kPhasesH;
        h += static_cast<float>(std::min(n, kMaxRows)) * kRowH + 6.f;
    }
    return std::min(h, std::max(kHeadH + kBarH + 4.f, maxH));
}

void AiPlanStrip::setExpanded(bool open) {
    if (open_ == open) return;
    open_ = open;
    scroll_ = 0.f;
    if (parent()) parent()->invalidateLayout();
    invalidate();
}

void AiPlanStrip::closeCurrent() {
    if (const auto lp = panel_.chat().livePlan()) closed_.insert(lp->id);
    if (parent()) parent()->invalidateLayout();
    invalidate();
}

gfx::Rect AiPlanStrip::headRect() const {
    const auto b = bounds();
    return {b.x, b.y, b.w, kHeadH};
}

gfx::Rect AiPlanStrip::closeRect() const {
    const auto b = bounds();
    return {b.right() - 26.f, b.y + 5.f, 18.f, 18.f};
}

float AiPlanStrip::stepsTop() const {
    const auto lp = panel_.chat().livePlan();
    return bounds().y + kHeadH + kBarH + 4.f + (lp && !lp->phases.empty() ? kPhasesH : 0.f);
}

std::size_t AiPlanStrip::visibleSteps() const {
    const float room = bounds().bottom() - stepsTop() - 4.f;
    return room <= 0.f ? 0 : static_cast<std::size_t>(room / kRowH);
}

gfx::Rect AiPlanStrip::stepRect(std::size_t index) const {
    if (!open_) return {};
    const std::size_t first = static_cast<std::size_t>(scroll_);
    if (index < first || index >= first + visibleSteps()) return {};
    const auto b = bounds();
    return {b.x + 8.f, stepsTop() + static_cast<float>(index - first) * kRowH, b.w - 16.f, kRowH};
}

std::vector<std::string> AiPlanStrip::lines() const {
    std::vector<std::string> out;
    const auto lp = panel_.chat().livePlan();
    if (!lp || closed_.count(lp->id)) return out;
    std::string prog;
    (void)progressOf(*lp, &prog);
    out.push_back("Plan en cours : " + lp->head);
    out.push_back(lp->status + (prog.empty() ? std::string() : " \xC2\xB7 " + prog));
    if (open_) {
        if (!lp->phases.empty()) out.push_back(lp->phases);
        if (lp->plan)
            for (const auto& st : lp->plan->steps) out.push_back(st.id + "  " + st.title + "  \xE2\x80\x94 " + lookOf(st, *lp).label);
    }
    return out;
}

std::string AiPlanStrip::liveTooltip(gfx::Point mouse) const {
    for (auto it = tips_.rbegin(); it != tips_.rend(); ++it)
        if (it->first.contains(mouse)) return it->second;
    return {};
}

void AiPlanStrip::onPaint(const ui::PaintContext& ctx) {
    tips_.clear();
    const auto lp = panel_.chat().livePlan();
    const auto b = bounds();
    if (!lp || closed_.count(lp->id) || b.h <= 0.f) return;
    const auto& c = ctx.theme.color;
    const float meta = ctx.r.lineHeight(kMeta);
    ctx.r.pushClip(b);
    ctx.r.fillRect(b, ctx.theme.brand.card.a ? ctx.theme.brand.card : c.inputBg);
    ctx.r.fillRect({b.x, b.y, b.w, kHeadH + kBarH}, alpha(c.info, 22));
    ctx.r.fillRect({b.x, b.bottom() - 1.f, b.w, 1.f}, c.border);
    // La ligne du haut : replier/deplier, << Plan en cours >>, le plan, son etat, la croix.
    const gfx::Rect head = headRect();
    ui::drawIcon(ctx.r, open_ ? ui::Icon::Collapse : ui::Icon::Expand, {head.x + 8.f, head.y + 7.f, 14.f, 14.f}, c.textMuted);
    const std::string title = "Plan en cours";
    const float tw = ctx.r.measure(title, kBody).width;
    const float ty = head.y + (kHeadH - ctx.r.lineHeight(kBody)) / 2.f;
    ctx.r.drawText({head.x + 28.f, ty}, title, kBody, c.text);
    ctx.r.drawText({head.x + 28.8f, ty}, title, kBody, c.text);   // le gras du logiciel
    const gfx::Color tone = lp->tone == 1 ? c.ok : lp->tone == 2 ? c.warning : lp->tone == 3 ? c.error : c.info;
    const float sw = ctx.r.measure(lp->status, kMeta).width + 14.f;
    const gfx::Rect close = closeRect();
    const gfx::Rect chip{close.x - 8.f - sw, head.y + 5.f, sw, 18.f};
    ctx.r.fillRoundedRect(chip, alpha(tone, 46), 9.f);
    ctx.r.drawText({chip.x + 7.f, chip.y + (chip.h - meta) / 2.f}, lp->status, kMeta, tone);
    const float hx = head.x + 28.f + tw + 12.f;
    const std::string shown = elide(ctx.r, lp->head, kMeta, chip.x - 8.f - hx);
    ctx.r.drawText({hx, head.y + (kHeadH - meta) / 2.f}, shown, kMeta, c.textMuted);
    if (shown != lp->head) tips_.emplace_back(gfx::Rect{hx, head.y, chip.x - hx, kHeadH}, lp->head);
    ui::drawIcon(ctx.r, ui::Icon::Close, {close.x + 2.f, close.y + 2.f, 14.f, 14.f}, c.textMuted);
    tips_.emplace_back(close, "Cacher la bande pour ce plan (la proposition suivante la remontre).");
    tips_.emplace_back(gfx::Rect{head.x, head.y, chip.x - head.x, kHeadH},
                       open_ ? std::string("Replier le plan") : std::string("D\xC3\xA9plier le plan : chaque \xC3\xA9tape et son \xC3\xA9tat"));
    // L'avancement.
    std::string prog;
    const float frac = std::clamp(progressOf(*lp, &prog), 0.f, 1.f);
    const float pw = prog.empty() ? 0.f : ctx.r.measure(prog, kMeta).width + 10.f;
    const gfx::Rect track{b.x + 10.f, b.y + kHeadH + 6.f, std::max(20.f, b.w - 20.f - pw), 8.f};
    ctx.r.fillRoundedRect(track, alpha(c.info, 40), 4.f);
    if (frac > 0.f) ctx.r.fillRoundedRect({track.x, track.y, std::max(8.f, track.w * frac), track.h}, lp->tone == 3 ? c.error : lp->tone == 1 ? c.ok : c.info, 4.f);
    if (!prog.empty()) ctx.r.drawText({track.right() + 8.f, track.y + (track.h - meta) / 2.f}, prog, kMeta, c.textMuted);
    // Depliee : les phases, puis les etapes (celles qui tiennent ; la molette fait defiler).
    if (open_) {
        float y = b.y + kHeadH + kBarH + 4.f;
        if (!lp->phases.empty()) {
            const std::string ph = elide(ctx.r, lp->phases, kMeta, b.w - 20.f);
            ctx.r.drawText({b.x + 10.f, y}, ph, kMeta, c.textMuted);
            if (ph != lp->phases) tips_.emplace_back(gfx::Rect{b.x, y, b.w, kPhasesH}, lp->phases);
            y += kPhasesH;
        }
        if (lp->plan) {
            const auto& steps = lp->plan->steps;
            const std::size_t first = std::min(static_cast<std::size_t>(scroll_), steps.size());
            const std::size_t n = std::min(visibleSteps(), steps.size() - first);
            for (std::size_t i = first; i < first + n; ++i) {
                const auto& st = steps[i];
                const StepLook look = lookOf(st, *lp);
                const gfx::Color dot = toneColor(ctx.theme, look.tone);
                ctx.r.fillRoundedRect({b.x + 12.f, y + 5.5f, 7.f, 7.f}, dot, 3.5f);
                const float lw = ctx.r.measure(look.label, kMeta).width;
                ctx.r.drawText({b.right() - 12.f - lw, y}, look.label, kMeta, dot);
                const std::string text = st.id + "  " + st.title;
                const float room = b.right() - 20.f - lw - (b.x + 26.f);
                const std::string cut = elide(ctx.r, text, kMeta, room);
                ctx.r.drawText({b.x + 26.f, y}, cut, kMeta, st.excluded ? c.textMuted : c.text);
                std::string tip = text + " \xE2\x80\x94 " + look.label;
                if (!st.errors.empty()) tip += "\n" + st.errors.front();
                else if (!st.note.empty()) tip += "\n" + st.note;
                tips_.emplace_back(gfx::Rect{b.x, y, b.w, kRowH}, tip + "\nClic : la carte de la proposition, son plan d\xC3\xA9pli\xC3\xA9.");
                y += kRowH;
            }
            if (steps.size() > visibleSteps()) {
                const std::string more = std::to_string(first + 1) + "\xE2\x80\x93" + std::to_string(first + n) + " sur " + std::to_string(steps.size());
                tips_.emplace_back(gfx::Rect{b.x, b.y + kHeadH + kBarH, b.w, b.h - kHeadH - kBarH},
                                   "La molette fait d\xC3\xA9" "filer les \xC3\xA9tapes (" + more + ").");
            }
        }
    }
    ctx.r.popClip();
}

ui::EventResult AiPlanStrip::onEvent(const ui::InputEvent& ev) {
    const auto lp = panel_.chat().livePlan();
    if (!lp) return ui::EventResult::Ignored;
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && bounds().contains(w->pos) && open_ && lp->plan) {
        const float maxFirst = static_cast<float>(lp->plan->steps.size() > visibleSteps() ? lp->plan->steps.size() - visibleSteps() : 0);
        scroll_ = std::clamp(scroll_ - w->dy * 3.f, 0.f, maxFirst);
        scroll_ = std::floor(scroll_);
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && bounds().contains(d->pos)) {
        if (closeRect().contains(d->pos)) {
            closeCurrent();
            return ui::EventResult::Consumed;
        }
        if (headRect().contains(d->pos) || d->pos.y < bounds().y + kHeadH + kBarH) {
            setExpanded(!open_);
            return ui::EventResult::Consumed;
        }
        if (open_ && lp->plan)
            for (std::size_t i = 0; i < lp->plan->steps.size(); ++i)
                if (stepRect(i).contains(d->pos)) {
                    panel_.chat().revealMessage(lp->message, true);
                    return ui::EventResult::Consumed;
                }
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace app::ai
