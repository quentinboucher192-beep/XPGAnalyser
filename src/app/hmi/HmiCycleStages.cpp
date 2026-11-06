// =============================================================================
//  app/hmi/HmiCycleStages.cpp - voir HmiCycleStages.hpp
// =============================================================================
#include "HmiCycleStages.hpp"

#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>

namespace app {

namespace vc = hmi::viewcycle;

namespace {

constexpr float kLeft = 36.f;       // le texte des etapes, les pastilles : apres le rail et ses ronds
constexpr float kRight = 10.f;

// Le texte qui tient dans `w` (des points de suite sinon).
std::string fitted(const gfx::IRenderer& r, const std::string& text, gfx::FontId font, float w) {
    if (w <= 0.f) return {};
    if (r.measure(text, font).width <= w) return text;
    const std::string dots = "\xE2\x80\xA6";
    std::size_t cut = std::min(text.size(), r.fitCharacters(text, font, std::max(0.f, w - r.measure(dots, font).width)));
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut) + dots;
}

// Les lignes d'un texte coupe aux espaces pour tenir dans `w` (au plus `maxLines`, la derniere avec des points).
std::vector<std::string> wrapped(const gfx::IRenderer& r, const std::string& text, gfx::FontId font, float w, std::size_t maxLines) {
    std::vector<std::string> lines;
    std::string cur;
    std::size_t at = 0;
    while (at <= text.size()) {
        const auto sp = text.find(' ', at);
        const std::string word = text.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
        const std::string tryLine = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && r.measure(tryLine, font).width > w) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = tryLine;
        }
        if (sp == std::string::npos) break;
        at = sp + 1;
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.size() > maxLines) {
        std::string rest;
        for (std::size_t i = maxLines - 1; i < lines.size(); ++i) rest += (rest.empty() ? "" : " ") + lines[i];
        lines.resize(maxLines - 1);
        lines.push_back(rest);
    }
    if (!lines.empty()) lines.back() = fitted(r, lines.back(), font, w);
    return lines;
}

void dashedRect(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color c) {
    constexpr float dash = 5.f, gap = 3.f;
    for (float x = b.x; x < b.right(); x += dash + gap) {
        const float e = std::min(b.right(), x + dash);
        r.line({x, b.y}, {e, b.y}, c, 1.5f);
        r.line({x, b.bottom()}, {e, b.bottom()}, c, 1.5f);
    }
    for (float y = b.y; y < b.bottom(); y += dash + gap) {
        const float e = std::min(b.bottom(), y + dash);
        r.line({b.x, y}, {b.x, e}, c, 1.5f);
        r.line({b.right(), y}, {b.right(), e}, c, 1.5f);
    }
}

void grip(gfx::IRenderer& r, float x, float cy, gfx::Color c) {
    for (int col = 0; col < 2; ++col)
        for (int row = -1; row <= 1; ++row) r.fillRect({x + static_cast<float>(col) * 4.f, cy + static_cast<float>(row) * 4.f - 1.f, 2.f, 2.f}, c);
}

} // namespace

HmiCycleStages::HmiCycleStages(std::string id) : ui::Widget(std::move(id)) {
    for (auto& ms : timing_.stepMs) ms = -1.0;
}

void HmiCycleStages::setChips(std::vector<Chip> chips) {
    chips_ = std::move(chips);
    if (std::none_of(chips_.begin(), chips_.end(), [&](const Chip& c) { return c.id == selected_; })) selected_ = hmi::kNoId;
    relayout();
}

void HmiCycleStages::setTiming(const vc::Timing& t) {
    timing_ = t;
    invalidate();
}

void HmiCycleStages::setSelected(hmi::Id id) {
    if (selected_ == id) return;
    selected_ = id;
    invalidate();
}

float HmiCycleStages::contentTop() const noexcept { return bounds().y + 4.f - scrollY_; }

void HmiCycleStages::onLayout() {
    relayout();
}

void HmiCycleStages::relayout() {
    geo_ = build();
    // Le contenu a raccourci : le defilement revient dans ses bornes.
    const float maxScroll = std::max(0.f, geo_.height - bounds().h);
    if (scrollY_ > maxScroll) {
        scrollY_ = maxScroll;
        geo_ = build();
    }
    invalidate();
}

HmiCycleStages::Geometry HmiCycleStages::build() const {
    const auto b = bounds();
    Geometry g;
    float y = contentTop();
    const float cw = std::max(40.f, b.w - kLeft - kRight);
    for (int n = 1; n <= vc::kSteps; ++n) {
        g.steps[static_cast<std::size_t>(n)] = {b.x, y, b.w, kStepH};
        y += kStepH;
        if (n > vc::kLastMoment) continue;
        const float top = y;
        // Les pastilles de ce moment, sans celle qu'on tire ; la ligne pointillee a son rang.
        std::vector<const Chip*> here;
        for (const auto& c : chips_)
            if (c.moment == n && !(drag_.active && c.id == drag_.script && c.id != hmi::kNoId)) here.push_back(&c);
        const bool dropHere = drag_.active && drop_.moment == n;
        if (!here.empty() || dropHere) y += 2.f;
        for (std::size_t i = 0; i <= here.size(); ++i) {
            if (dropHere && drop_.rank == i) {
                g.drop = {b.x + kLeft, y, cw, kDropH};
                y += kDropH + 3.f;
            }
            if (i == here.size()) break;
            g.chips.emplace_back(here[i]->id, gfx::Rect{b.x + kLeft, y, cw, kChipH});
            y += kChipH + 3.f;
        }
        y += here.empty() && !dropHere ? 8.f : 2.f;
        g.slots[static_cast<std::size_t>(n)] = {b.x, top, b.w, y - top};
    }
    g.footer = {b.x + kLeft, y + 4.f, cw, 36.f};
    y += 48.f;
    g.height = y - contentTop();
    return g;
}

void HmiCycleStages::aim(gfx::Point at) {
    if (!bounds().contains(at)) {
        drop_ = {};
        relayout();
        return;
    }
    // Sur la ligne pointillee : la cible reste. Ailleurs, elle se vise sur la section telle qu'elle est dessinee (la
    // pastille tiree n'y est plus) : inserer la ligne au-dessus de la souris pousse le reste dessous, la cible tient.
    if (geo_.drop.w > 0.f && geo_.drop.contains(at)) return;
    int moment = vc::kLastMoment;
    for (int m = vc::kFirstMoment; m <= vc::kLastMoment; ++m) {
        const auto& next = geo_.steps[static_cast<std::size_t>(m + 1)];
        if (at.y < next.y + next.h * 0.5f) {
            moment = m;
            break;
        }
    }
    std::size_t rank = 0;
    for (const auto& [id, r] : geo_.chips)
        for (const auto& c : chips_)
            if (c.id == id && c.moment == moment && r.y + r.h * 0.5f < at.y) ++rank;
    drop_ = {moment, rank};
    relayout();
}

bool HmiCycleStages::finishDrag(gfx::Point at) {
    const bool inside = bounds().contains(at);
    if (inside) aim(at);
    const Drop d = drop_;
    const hmi::Id script = drag_.script;
    drag_ = {};
    drop_ = {};
    pressed_ = hmi::kNoId;
    if (holdsMouse()) releaseMouse();
    relayout();
    if (!inside || d.moment <= 0) return false;
    dropped->emit(script, d.moment, d.rank);
    return true;
}

void HmiCycleStages::beginExternalDrag(hmi::Id script, std::string label) {
    drag_ = {};
    drag_.active = true;
    drag_.script = script;
    drag_.label = std::move(label);
    drop_ = {};
    pressed_ = hmi::kNoId;
    captureMouse();
    relayout();
}

bool HmiCycleStages::externalDragAt(hmi::Id script, const std::string& label, gfx::Point at, bool released) {
    if (released) {
        const bool mine = drag_.active && drag_.external;
        if (mine && bounds().contains(at)) return finishDrag(at);
        if (mine) cancelDrag();
        return false;
    }
    if (bounds().contains(at)) {
        const bool entering = !drag_.active;
        drag_.active = true;
        drag_.external = true;
        drag_.script = script;
        drag_.label = label;
        drag_.at = at;
        if (entering) relayout();        // sa pastille quitte sa place avant qu'on vise
        aim(at);
    } else if (drag_.active && drag_.external) {
        cancelDrag();
    }
    return false;
}

void HmiCycleStages::cancelDrag() {
    drag_ = {};
    drop_ = {};
    pressed_ = hmi::kNoId;
    if (holdsMouse()) releaseMouse();
    relayout();
}

ui::EventResult HmiCycleStages::onEvent(const ui::InputEvent& ev) {
    using R = ui::EventResult;
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (d->button != ui::MouseButton::Left || !bounds().contains(d->pos)) return R::Ignored;
        for (const auto& [id, r] : geo_.chips)
            if (r.contains(d->pos)) {
                pressed_ = id;
                pressAt_ = d->pos;
                selected_ = id;
                captureMouse();          // un geste rapide sort de la pastille avant le premier deplacement : il se tire quand meme
                invalidate();
                chipClicked->emit(id);
                return R::Consumed;
            }
        return R::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        // Une pastille saisie : au-dela de quelques pixels, elle se tire (la section tient la souris).
        if (!drag_.active && pressed_ != hmi::kNoId && std::hypot(m->pos.x - pressAt_.x, m->pos.y - pressAt_.y) > 5.f) {
            const auto it = std::find_if(chips_.begin(), chips_.end(), [&](const Chip& c) { return c.id == pressed_; });
            drag_ = {};
            drag_.active = true;
            drag_.script = pressed_;
            drag_.label = it != chips_.end() ? it->name : std::string{};
            pressed_ = hmi::kNoId;
            captureMouse();
            relayout();                  // la pastille quitte sa place avant qu'on vise
        }
        if (drag_.active && !drag_.external) {
            drag_.at = m->pos;
            aim(m->pos);
            return R::Consumed;
        }
        return R::Ignored;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev)) {
        const bool was = pressed_ != hmi::kNoId;
        pressed_ = hmi::kNoId;
        if (drag_.active && !drag_.external) {
            (void)finishDrag(u->pos);
            return R::Consumed;
        }
        if (holdsMouse()) releaseMouse();
        return was ? R::Consumed : R::Ignored;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (!bounds().contains(w->pos)) return R::Ignored;
        const float maxScroll = std::max(0.f, geo_.height - bounds().h);
        scrollY_ = std::clamp(scrollY_ - w->dy * 40.f, 0.f, maxScroll);
        relayout();
        return R::Consumed;
    }
    return R::Ignored;
}

bool HmiCycleStages::stepRect(int number, gfx::Rect& out) const {
    if (number < 1 || number > vc::kSteps) return false;
    out = geo_.steps[static_cast<std::size_t>(number)];
    return out.h > 0.f;
}

bool HmiCycleStages::slotRect(int moment, gfx::Rect& out) const {
    if (moment < vc::kFirstMoment || moment > vc::kLastMoment) return false;
    out = geo_.slots[static_cast<std::size_t>(moment)];
    return out.h > 0.f;
}

bool HmiCycleStages::chipRect(hmi::Id id, gfx::Rect& out) const {
    for (const auto& [cid, r] : geo_.chips)
        if (cid == id) {
            out = r;
            return true;
        }
    return false;
}

std::string HmiCycleStages::dropText() const { return drag_.active && drop_.moment > 0 ? vc::dropText(drop_.moment) : std::string{}; }

std::string HmiCycleStages::stepTitle(int number) const { return std::string(vc::step(number).title); }

std::string HmiCycleStages::stepTime(int number) const {
    if (!timing_.measured()) return "\xE2\x80\x94";
    if (number == vc::kSteps) {
        const auto n = timing_.writes;
        return std::to_string(n) + (n > 1 ? " \xC3\xA9" "critures" : " \xC3\xA9" "criture");
    }
    return vc::msText(timing_.stepMs[static_cast<std::size_t>(std::clamp(number, 1, vc::kSteps))]);
}

std::string HmiCycleStages::totalText() const {
    if (!timing_.measured()) return "pas encore de simulation";
    double total = scriptsMs_;               // ses scripts de cycle, en plus des etapes
    for (int n = 1; n <= vc::kLastMoment; ++n)
        if (const double ms = timing_.stepMs[static_cast<std::size_t>(n)]; ms > 0) total += ms;
    char b[32];
    std::snprintf(b, sizeof b, total < 10 ? "%.1f" : "%.0f", total);
    std::string t = b;
    for (auto& c : t)
        if (c == '.') c = ',';
    char p[32];
    std::snprintf(p, sizeof p, "%.0f", timing_.periodMs);
    return "simulation : " + t + " ms sur " + p + " ms";
}

std::string HmiCycleStages::footerText() const {
    return "\xE2\x86\xBB le cycle suivant reprend en 1 \xC2\xB7 OnOpen avant le 1er cycle \xC2\xB7 OnClose apr\xC3\xA8s le dernier";
}

std::string HmiCycleStages::liveTooltip(gfx::Point mouse) const {
    if (geo_.drop.w > 0.f && geo_.drop.contains(mouse)) return dropText();
    for (const auto& [id, r] : geo_.chips)
        if (r.contains(mouse))
            for (const auto& c : chips_)
                if (c.id == id) return c.name + "\n" + c.tip + "\nGlisse-le entre deux \xC3\xA9tapes pour changer son moment.";
    for (int n = 1; n <= vc::kSteps; ++n)
        if (geo_.steps[static_cast<std::size_t>(n)].contains(mouse)) {
            const auto& st = vc::step(n);
            return "\xC3\x89tape " + std::to_string(n) + " : " + std::string(st.title) + " (" + stepTime(n) + ")\n" + std::string(st.exact);
        }
    return "Les \xC3\xA9tapes du cycle de la vue, dans l'ordre o\xC3\xB9 le moteur les fait ; sous chacune, les scripts de cycle qui tournent "
           "apr\xC3\xA8s elle. Glisse un script entre deux \xC3\xA9tapes.";
}

void HmiCycleStages::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    auto& r = ctx.r;
    r.fillRect(b, c.panelBg);
    const auto ui = ctx.theme.font.ui;
    const auto small = ctx.theme.font.caption;
    const float lh = r.lineHeight(ui), sh = r.lineHeight(small);
    // Le rail : du rond de 1 a celui de 10.
    const auto& s1 = geo_.steps[1];
    const auto& s10 = geo_.steps[static_cast<std::size_t>(vc::kSteps)];
    r.fillRect({b.x + 17.f, s1.y + 20.f, 2.f, std::max(0.f, s10.y - s1.y)}, c.border);
    for (int n = 1; n <= vc::kSteps; ++n) {
        const auto& s = geo_.steps[static_cast<std::size_t>(n)];
        if (s.bottom() < b.y || s.y > b.bottom()) continue;
        const auto& st = vc::step(n);
        // Le rond et son numero (9, le dessin : teinte de l'accent).
        const gfx::Rect dot{b.x + 8.f, s.y + 10.f, 20.f, 20.f};
        r.fillRoundedRect(dot, n == 9 ? c.accent.withAlpha(60) : c.headerBg, 10.f);
        const std::string num = std::to_string(n);
        const float nw = r.measure(num, small).width;
        r.drawText({dot.x + (dot.w - nw) * 0.5f, dot.y + (dot.h - sh) * 0.5f}, num, small, n == 9 ? c.accent : c.text);
        // La duree mesuree, a droite ; le titre, la ligne de ce qu'elle fait.
        const std::string ms = stepTime(n);
        const float mw = r.measure(ms, small).width;
        r.drawText({s.right() - kRight - mw, s.y + 4.f}, ms, small, c.textMuted);
        const float room = std::max(0.f, s.w - kLeft - kRight - mw - 8.f);
        r.drawText({s.x + kLeft, s.y + 3.f}, fitted(r, std::string(st.title), ui, room), ui, c.text);
        r.drawText({s.x + kLeft, s.y + 3.f + lh}, fitted(r, std::string(st.detail), small, s.w - kLeft - kRight), small, c.textMuted);
    }
    // Les pastilles des scripts de cycle.
    for (const auto& [id, rc] : geo_.chips) {
        const auto it = std::find_if(chips_.begin(), chips_.end(), [&](const Chip& ch) { return ch.id == id; });
        if (it == chips_.end() || rc.bottom() < b.y || rc.y > b.bottom()) continue;
        const bool sel = id == selected_;
        r.fillRoundedRect(rc, c.accent.withAlpha(sel ? 56 : 26), 5.f);
        r.strokeRect(rc, sel ? c.accent : c.accent.withAlpha(110), sel ? 1.5f : 1.f);
        grip(r, rc.x + 7.f, rc.y + rc.h * 0.5f, c.textMuted);
        float x = rc.x + 20.f;
        const std::string lang = it->lang.empty() ? std::string("ST") : it->lang;
        const float tw = r.measure(lang, small).width + 8.f;
        r.fillRoundedRect({x, rc.y + (rc.h - sh - 2.f) * 0.5f, tw, sh + 2.f}, c.accent, 3.f);
        r.drawText({x + 4.f, rc.y + (rc.h - sh) * 0.5f}, lang, small, c.textInverted);
        x += tw + 6.f;
        const float nw = it->note.empty() ? 0.f : r.measure(it->note, small).width;
        if (nw > 0.f) r.drawText({rc.right() - 8.f - nw, rc.y + (rc.h - sh) * 0.5f}, it->note, small, c.textMuted);
        r.drawText({x, rc.y + (rc.h - lh) * 0.5f}, fitted(r, it->name, ui, rc.right() - x - nw - 14.f), ui, c.text);
    }
    // La ligne pointillee : ou le script va tourner, ce que ca change.
    if (geo_.drop.w > 0.f) {
        const auto& d = geo_.drop;
        r.fillRoundedRect(d, c.accent.withAlpha(18), 6.f);
        dashedRect(r, d, c.accent);
        const auto lines = wrapped(r, "\xE2\x86\xB3 " + dropText(), small, d.w - 16.f, 2);
        float y = d.y + (d.h - sh * static_cast<float>(lines.size())) * 0.5f;
        for (const auto& l : lines) {
            r.drawText({d.x + 8.f, y}, l, small, c.accent);
            y += sh;
        }
    }
    // Le pied : le cycle suivant.
    {
        const auto lines = wrapped(r, footerText(), small, geo_.footer.w, 2);
        float y = geo_.footer.y;
        for (const auto& l : lines) {
            r.drawText({geo_.footer.x, y}, l, small, c.textMuted);
            y += sh;
        }
    }
    // Le fantome du script tire, sous la souris.
    if (drag_.active && b.contains(drag_.at)) {
        const std::string label = drag_.label.empty() ? std::string("Nouveau script de cycle") : drag_.label;
        const float w = std::min(b.w - 8.f, r.measure(label, ui).width + 44.f);
        const gfx::Rect gh{std::min(drag_.at.x + 10.f, b.right() - w - 2.f), drag_.at.y - 12.f, w, kChipH};
        r.fillRoundedRect(gh, c.panelBg, 5.f);
        r.strokeRect(gh, c.accent, 1.5f);
        grip(r, gh.x + 7.f, gh.y + gh.h * 0.5f, c.textMuted);
        r.drawText({gh.x + 20.f, gh.y + (gh.h - lh) * 0.5f}, fitted(r, label, ui, gh.w - 26.f), ui, c.text);
    }
}

// ------------------------------------------------------------ + Script de cycle ----
HmiCycleAdd::HmiCycleAdd(std::string id) : ui::Widget(std::move(id)) {
    setTooltip("Un nouveau script de cycle (ST), apr\xC3\xA8s l'\xC3\xA9tape du script choisi (sinon apr\xC3\xA8s 4, comme l'OnCycle d'avant). "
               "Glisse-le sur la section Ex\xC3\xA9" "cution pour le poser exactement \xC3\xA0 l'\xC3\xA9tape voulue.");
}

gfx::Rect HmiCycleAdd::buttonRect() const {
    const auto b = bounds();
    return {b.x + 8.f, b.y + 5.f, 150.f, std::max(0.f, b.h - 10.f)};
}

void HmiCycleAdd::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    auto& r = ctx.r;
    r.fillRect(b, c.panelBg);
    r.fillRect({b.x, b.y, b.w, 1.f}, c.border);
    const auto bt = buttonRect();
    r.fillRoundedRect(bt, pressed_ ? c.accent.withAlpha(40) : c.inputBg, 4.f);
    r.strokeRect(bt, c.borderStrong, 1.f);
    const auto ui = ctx.theme.font.ui;
    const auto small = ctx.theme.font.caption;
    const std::string label = "+ Script de cycle";
    const float lw = r.measure(label, ui).width;
    r.drawText({bt.x + (bt.w - lw) * 0.5f, bt.y + (bt.h - r.lineHeight(ui)) * 0.5f}, label, ui, c.text);
    const std::string hint = "ou glisse-le \xC3\xA0 droite, \xC3\xA0 l'\xC3\xA9tape voulue";
    r.drawText({bt.right() + 8.f, b.y + (b.h - r.lineHeight(small)) * 0.5f}, fitted(r, hint, small, b.right() - bt.right() - 12.f), small,
               c.textMuted);
}

ui::EventResult HmiCycleAdd::onEvent(const ui::InputEvent& ev) {
    using R = ui::EventResult;
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (d->button != ui::MouseButton::Left || !buttonRect().contains(d->pos)) return R::Ignored;
        pressed_ = true;
        pressAt_ = d->pos;
        captureMouse();                  // le glisser commence meme si la souris sort vite du bouton
        invalidate();
        return R::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        if (!pressed_ || std::hypot(m->pos.x - pressAt_.x, m->pos.y - pressAt_.y) <= 5.f) return R::Ignored;
        pressed_ = false;
        invalidate();
        if (holdsMouse()) releaseMouse();
        if (startDrag) startDrag();      // la section Execution tient alors la souris
        return R::Consumed;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev)) {
        if (!pressed_) return R::Ignored;
        pressed_ = false;
        if (holdsMouse()) releaseMouse();
        invalidate();
        if (buttonRect().contains(u->pos)) clicked->emit();     // lache hors du bouton : rien
        return R::Consumed;
    }
    return R::Ignored;
}

} // namespace app
