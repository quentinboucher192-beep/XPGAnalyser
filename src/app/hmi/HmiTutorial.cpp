#include "HmiTutorial.hpp"

#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>

namespace app {

namespace {

// Des lignes qui tiennent dans `width`, coupees aux espaces ; "\n" force une ligne.
std::vector<std::string> wrap(const ui::PaintContext& ctx, gfx::FontId font, const std::string& text, float width) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        auto nl = text.find('\n', start);
        if (nl == std::string::npos) nl = text.size();
        const std::string para = text.substr(start, nl - start);
        std::string line;
        std::size_t i = 0;
        while (i <= para.size()) {
            auto sp = para.find(' ', i);
            if (sp == std::string::npos) sp = para.size();
            const std::string word = para.substr(i, sp - i);
            const std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && ctx.r.measure(trial, font).width > width) {
                out.push_back(line);
                line = word;
            } else {
                line = trial;
            }
            i = sp + 1;
        }
        out.push_back(line);
        start = nl + 1;
    }
    return out;
}

gfx::Color withAlpha(gfx::Color c, std::uint8_t a) {
    c.a = a;
    return c;
}

// Une coche, dessinee (pas un caractere : toutes les polices ne l'ont pas).
void drawCheck(const ui::PaintContext& ctx, float x, float y, gfx::Color c) {
    ctx.r.line({x, y + 5.f}, {x + 3.5f, y + 8.5f}, c, 2.f);
    ctx.r.line({x + 3.5f, y + 8.5f}, {x + 10.f, y + 1.f}, c, 2.f);
}

// Un sablier, dessine.
void drawHourglass(const ui::PaintContext& ctx, float x, float y, gfx::Color c) {
    ctx.r.line({x, y}, {x + 9.f, y}, c, 1.5f);
    ctx.r.line({x, y + 12.f}, {x + 9.f, y + 12.f}, c, 1.5f);
    ctx.r.line({x + 1.f, y}, {x + 8.f, y + 12.f}, c, 1.2f);
    ctx.r.line({x + 8.f, y}, {x + 1.f, y + 12.f}, c, 1.2f);
}

// Un petit triangle plein (lecture), pointe a droite ou a gauche.
void drawTriangle(const ui::PaintContext& ctx, float x, float y, float h, bool right, gfx::Color c) {
    const float half = h / 2.f;
    for (int k = 0; k < static_cast<int>(half); ++k) {
        const float len = h - 2.f * static_cast<float>(k);
        const float px = right ? x + static_cast<float>(k) : x + half - static_cast<float>(k);
        ctx.r.fillRect({px, y + static_cast<float>(k), 1.f, len}, c);
    }
}

// Un cadre en pointilles.
void dashedRect(const ui::PaintContext& ctx, gfx::Rect r, gfx::Color c) {
    for (float x = r.x; x < r.right(); x += 7.f) {
        const float w = std::min(4.f, r.right() - x);
        ctx.r.fillRect({x, r.y, w, 1.f}, c);
        ctx.r.fillRect({x, r.bottom() - 1.f, w, 1.f}, c);
    }
    for (float y = r.y; y < r.bottom(); y += 7.f) {
        const float h = std::min(4.f, r.bottom() - y);
        ctx.r.fillRect({r.x, y, 1.f, h}, c);
        ctx.r.fillRect({r.right() - 1.f, y, 1.f, h}, c);
    }
}

} // namespace

HmiTutorial::HmiTutorial(std::string id) : ui::Widget(std::move(id)) {
    setVisibility(ui::Visibility::Collapsed);
}

void HmiTutorial::start(std::vector<Step> steps, std::string title, std::size_t from) {
    steps_ = std::move(steps);
    title_ = std::move(title);
    index_ = steps_.empty() ? 0 : std::min(from, steps_.size() - 1);
    active_ = !steps_.empty();
    completed_ = false;
    success_.clear();
    undo_ = {};
    pending_ = {};
    hover_ = -1;
    pressedOutside_ = false;
    setVisibility(active_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (active_) enterStep();
    invalidate();
}

void HmiTutorial::setUndo(std::function<void()> undoAll, std::function<bool()> pending) {
    undo_ = std::move(undoAll);
    pending_ = std::move(pending);
    invalidate();
}

void HmiTutorial::stop(bool finished) {
    if (!active_) return;
    active_ = false;
    completed_ = false;
    undo_ = {};
    pending_ = {};
    setVisibility(ui::Visibility::Collapsed);
    invalidate();
    closed->emit(finished);
}

void HmiTutorial::next() {
    if (!active_) return;
    if (index_ + 1 >= steps_.size()) { stop(true); return; }
    ++index_;
    enterStep();
    invalidate();
}

void HmiTutorial::previous() {
    if (!active_ || index_ == 0) return;
    --index_;
    success_.clear();
    enterStep();
    invalidate();
}

bool HmiTutorial::interactive() const noexcept {
    const auto* st = current();
    return st && static_cast<bool>(st->done);
}

bool HmiTutorial::undoOffered() const {
    return static_cast<bool>(undo_) && (!pending_ || pending_());
}

void HmiTutorial::enterStep() {
    already_ = false;
    alreadyWhat_.clear();
    completed_ = false;
    lastPoll_ = -1.0;
    if (index_ < steps_.size()) {
        auto& st = steps_[index_];
        if (st.enter) st.enter();
        // Deja fait en y arrivant (Precedent, un parcours repris) : on le dit, et
        // Suivant continue - sans quoi l'etape passerait aussitot a la suivante.
        if (st.done) {
            std::string what;
            if (st.done(what)) {
                already_ = true;
                alreadyWhat_ = what;
                if (index_ + 1 == steps_.size()) completed_ = true;
            }
        }
    }
    stepped->emit(index_);
}

void HmiTutorial::poll(double now) {
    if (!active_ || completed_) return;
    const Step* st = current();
    if (!st || !st->done) return;
    if (lastPoll_ >= 0.0 && now - lastPoll_ < 0.2 && now >= lastPoll_) return;
    lastPoll_ = now;
    std::string what;
    const bool ok = st->done(what);
    if (already_) {
        // Defait depuis (Ctrl+Z) : l'etape se rearme et attend le geste.
        if (!ok) {
            already_ = false;
            alreadyWhat_.clear();
            invalidate();
        }
        return;
    }
    if (!ok) return;
    success_ = "\xC3\x89tape " + std::to_string(index_ + 1) + " r\xC3\xA9ussie" + (what.empty() ? std::string{} : " : " + what);
    // Le geste fait peut avoir fait aussi les etapes suivantes (un dialogue
    // valide a la fois ouvert et rempli) : elles passent, la derniere reussite dite.
    for (;;) {
        if (index_ + 1 >= steps_.size()) {
            completed_ = true;
            invalidate();
            stepped->emit(index_);
            return;
        }
        next();
        if (!active_ || !already_) return;
        success_ = "\xC3\x89tape " + std::to_string(index_ + 1) + " r\xC3\xA9ussie"
                 + (alreadyWhat_.empty() ? std::string{} : " : " + alreadyWhat_);
        already_ = false;
        alreadyWhat_.clear();
        if (completed_) {           // la derniere, deja faite : le parcours est au bout
            invalidate();
            return;
        }
    }
}

void HmiTutorial::showMe() {
    const Step* st = current();
    if (!st || !st->showMe || completed_) return;
    const auto show = st->showMe;     // l'etape peut changer pendant l'appel
    show();
    lastPoll_ = -1.0;                  // verifier des la prochaine image
    invalidate();
}

void HmiTutorial::keep() {
    if (!active_) return;
    stop(true);
}

void HmiTutorial::undoAll() {
    if (!active_) return;
    const auto undo = undo_;
    if (undo) undo();
    stop(true);
}

gfx::Rect HmiTutorial::partRect(Part p) const noexcept {
    switch (p) {
        case Part::Next:     return next_;
        case Part::Previous: return previous_;
        case Part::Skip:     return skip_;
        case Part::ShowMe:   return showMe_;
        case Part::Keep:     return keep_;
        case Part::UndoAll:  return undoAll_;
    }
    return {};
}

void HmiTutorial::onPaintOverlay(const ui::PaintContext& ctx) {
    const auto* st = current();
    if (!st) return;
    const auto& c = ctx.theme.color;
    const gfx::Rect all = ctx.clip;
    const bool live = static_cast<bool>(st->done);        // une etape interactive

    // ---- le voile (une visite), troue autour de la zone decrite ; une etape
    // interactive n'assombrit rien : c'est a l'utilisateur de faire.
    gfx::Rect hole{};
    bool lit = false;
    if (st->target && st->target(hole) && !hole.empty()) {
        lit = true;
        hole = {hole.x - 6.f, hole.y - 4.f, hole.w + 12.f, hole.h + 8.f};
    }
    const gfx::Color veil{0, 0, 0, 150};
    if (!live) {
        if (lit) {
            ctx.r.fillRect({all.x, all.y, all.w, std::max(0.f, hole.y - all.y)}, veil);
            ctx.r.fillRect({all.x, hole.bottom(), all.w, std::max(0.f, all.bottom() - hole.bottom())}, veil);
            ctx.r.fillRect({all.x, hole.y, std::max(0.f, hole.x - all.x), hole.h}, veil);
            ctx.r.fillRect({hole.right(), hole.y, std::max(0.f, all.right() - hole.right()), hole.h}, veil);
        } else {
            ctx.r.fillRect(all, veil);
        }
    }
    if (lit) {
        ctx.r.strokeRect(hole, c.accent, 2.f);
        ctx.r.strokeRect({hole.x - 3.f, hole.y - 3.f, hole.w + 6.f, hole.h + 6.f}, withAlpha(c.accent, live ? 120 : 90), 2.f);
    }

    // ---- la bulle
    constexpr float kW = 470.f, kPad = 18.f;
    const auto titleFont = ctx.theme.font.lead;
    const auto bodyFont = ctx.theme.font.ui;
    const auto smallFont = ctx.theme.font.caption;
    const auto lines = wrap(ctx, bodyFont, st->text, kW - 2 * kPad);
    const float lh = ctx.r.lineHeight(bodyFont);
    const float th = ctx.r.lineHeight(titleFont);
    const float sh = ctx.r.lineHeight(smallFont);
    const float buttonsH = 32.f;
    // Les encadres : la reussite de l'etape d'avant (ou "deja fait"), l'attente.
    const float boxW = kW - 2 * kPad - 34.f;
    std::string okText;
    if (completed_) okText = success_.empty() ? "Parcours r\xC3\xA9ussi" + (alreadyWhat_.empty() ? std::string{} : " : " + alreadyWhat_) : success_;
    else if (already_) okText = "D\xC3\xA9j\xC3\xA0 fait" + (alreadyWhat_.empty() ? std::string{} : " : " + alreadyWhat_);
    else okText = success_;
    const auto okLines = okText.empty() ? std::vector<std::string>{} : wrap(ctx, smallFont, okText, boxW);
    const bool waiting = live && !already_ && !completed_ && !st->waiting.empty();
    const auto waitLines = waiting ? wrap(ctx, smallFont, st->waiting, boxW) : std::vector<std::string>{};
    std::string endText;
    if (completed_) endText = undoOffered() ? "Garder ce que le parcours a cr\xC3\xA9\xC3\xA9, ou tout d\xC3\xA9" "faire : l'historique revient \xC3\xA0 l'\xC3\xA9tat d'avant."
                                            : "Le parcours est termin\xC3\xA9.";
    const auto endLines = endText.empty() ? std::vector<std::string>{} : wrap(ctx, bodyFont, endText, kW - 2 * kPad);
    const auto boxH = [&](std::size_t n) { return n == 0 ? 0.f : static_cast<float>(n) * (sh + 2.f) + 14.f + 8.f; };
    const float h = kPad + sh + 6.f + th + 10.f + static_cast<float>(lines.size()) * (lh + 3.f) + 8.f + boxH(okLines.size())
                  + boxH(waitLines.size()) + static_cast<float>(endLines.size()) * (lh + 3.f) + 12.f + buttonsH + kPad;
    gfx::Rect b{0, 0, kW, h};
    if (st->aside) {
        b.x = all.right() - kW - 24.f;
        b.y = all.y + 70.f;
    } else if (lit) {
        // A droite de la zone si la place le permet, sinon a gauche, dessous, dessus.
        if (hole.right() + 24.f + kW <= all.right() - 12.f) { b.x = hole.right() + 24.f; b.y = hole.y; }
        else if (hole.x - 24.f - kW >= all.x + 12.f) { b.x = hole.x - 24.f - kW; b.y = hole.y; }
        else if (hole.bottom() + 24.f + h <= all.bottom() - 12.f) { b.x = hole.x; b.y = hole.bottom() + 24.f; }
        else { b.x = hole.x; b.y = hole.y - 24.f - h; }
    } else {
        b.x = all.x + (all.w - kW) / 2.f;
        b.y = all.y + (all.h - h) / 2.f;
    }
    b.x = std::clamp(b.x, all.x + 12.f, std::max(all.x + 12.f, all.right() - kW - 12.f));
    b.y = std::clamp(b.y, all.y + 12.f, std::max(all.y + 12.f, all.bottom() - h - 12.f));
    bubble_ = b;
    // L'ombre (deux voiles decales), la carte, un lisere d'accent en haut.
    ctx.r.fillRoundedRect({b.x + 3.f, b.y + 5.f, b.w, b.h}, gfx::Color{0, 0, 0, 90}, 8.f);
    ctx.r.fillRoundedRect(b, c.panelBg, 8.f);
    ctx.r.strokeRect(b, live ? c.accent : c.borderStrong, 1.f);
    ctx.r.fillRect({b.x, b.y, b.w, 4.f}, c.accent);
    // Une fleche vers la zone, du cote de la bulle qui la regarde.
    if (lit && !st->aside) {
        const float ay = std::clamp(hole.y + std::min(hole.h, 40.f) / 2.f, b.y + 16.f, b.bottom() - 16.f);
        if (b.x >= hole.right()) {
            for (int k = 0; k < 10; ++k)
                ctx.r.fillRect({b.x - 10.f + static_cast<float>(k), ay - (10.f - static_cast<float>(k)), 1.f, 2.f * (10.f - static_cast<float>(k))}, c.panelBg);
        } else if (b.right() <= hole.x) {
            for (int k = 0; k < 10; ++k)
                ctx.r.fillRect({b.right() + 9.f - static_cast<float>(k), ay - (10.f - static_cast<float>(k)), 1.f, 2.f * (10.f - static_cast<float>(k))}, c.panelBg);
        }
    }

    float y = b.y + kPad;
    const std::string counter = title_.empty()
        ? "\xC3\x89tape " + std::to_string(index_ + 1) + " / " + std::to_string(steps_.size()) + "  \xC2\xB7  didacticiel de l'IHM"
        : title_ + "  \xC2\xB7  \xC3\xA9tape " + std::to_string(index_ + 1) + " / " + std::to_string(steps_.size());
    ctx.r.drawText({b.x + kPad, y}, counter, smallFont, c.textMuted);
    // Les points de progression, a droite du compteur : faits et en cours pleins.
    const float dotsRoom = std::min(static_cast<float>(steps_.size()) * 11.f, 150.f);
    const float step = steps_.empty() ? 11.f : dotsRoom / static_cast<float>(steps_.size());
    for (std::size_t i = 0; i < steps_.size(); ++i) {
        const float dx = b.right() - kPad - dotsRoom + static_cast<float>(i) * step;
        const bool doneDot = i < index_ || (i == index_ && completed_);
        const bool here = i == index_;
        if (doneDot || here) ctx.r.fillRoundedRect({dx, y + sh / 2.f - 3.5f, 7.f, 7.f}, here && !completed_ ? c.accent : withAlpha(c.accent, 170), 3.5f);
        else ctx.r.fillRoundedRect({dx, y + sh / 2.f - 3.5f, 7.f, 7.f}, c.border, 3.5f);
    }
    y += sh + 6.f;
    ctx.r.drawText({b.x + kPad, y}, st->title, titleFont, c.text);
    ctx.r.drawText({b.x + kPad + 1.f, y}, st->title, titleFont, c.text);     // un faux gras
    y += th + 10.f;
    for (const auto& l : lines) {
        ctx.r.drawText({b.x + kPad, y}, l, bodyFont, c.text);
        y += lh + 3.f;
    }
    y += 8.f;
    // La reussite (vert) : l'etape d'avant, ou "deja fait".
    if (!okLines.empty()) {
        const float bh = boxH(okLines.size()) - 8.f;
        const gfx::Rect box{b.x + kPad, y, kW - 2 * kPad, bh};
        ctx.r.fillRoundedRect(box, withAlpha(c.ok, 38), 4.f);
        drawCheck(ctx, box.x + 9.f, box.y + 9.f, c.ok);
        float ty = box.y + 7.f;
        for (const auto& l : okLines) {
            ctx.r.drawText({box.x + 28.f, ty}, l, smallFont, c.ok);
            ty += sh + 2.f;
        }
        y += bh + 8.f;
    }
    // L'attente (pointilles) : le geste attendu.
    if (!waitLines.empty()) {
        const float bh = boxH(waitLines.size()) - 8.f;
        const gfx::Rect box{b.x + kPad, y, kW - 2 * kPad, bh};
        ctx.r.fillRoundedRect(box, withAlpha(c.accent, 18), 4.f);
        dashedRect(ctx, box, withAlpha(c.textMuted, 160));
        drawHourglass(ctx, box.x + 10.f, box.y + 8.f, c.textMuted);
        float ty = box.y + 7.f;
        for (const auto& l : waitLines) {
            ctx.r.drawText({box.x + 28.f, ty}, l, smallFont, c.textMuted);
            ty += sh + 2.f;
        }
        y += bh + 8.f;
    }
    for (const auto& l : endLines) {
        ctx.r.drawText({b.x + kPad, y}, l, bodyFont, c.text);
        y += lh + 3.f;
    }
    y += 12.f;

    // ---- les boutons
    const auto button = [&](gfx::Rect r, const std::string& label, bool primary, bool hovered, int arrow) {
        if (primary) ctx.r.fillRoundedRect(r, hovered ? c.accentHover : c.accent, 5.f);
        else {
            ctx.r.fillRoundedRect(r, hovered ? c.selectionBg : c.inputBg, 5.f);
            ctx.r.strokeRect(r, c.border, 1.f);
        }
        const float w = ctx.r.measure(label, bodyFont).width + (arrow != 0 ? 14.f : 0.f);
        float x = r.x + (r.w - w) / 2.f;
        const gfx::Color fg = primary ? c.textInverted : c.text;
        if (arrow < 0) { drawTriangle(ctx, x, r.y + r.h / 2.f - 5.f, 10.f, false, fg); x += 14.f; }
        if (arrow > 0) { drawTriangle(ctx, x + 2.f, r.y + r.h / 2.f - 5.f, 10.f, true, fg); x += 14.f; }
        ctx.r.drawText({x, r.y + (r.h - lh) / 2.f}, label, bodyFont, fg);
    };
    const auto widthOf = [&](const std::string& label, bool arrow) { return ctx.r.measure(label, bodyFont).width + (arrow ? 42.f : 28.f); };
    next_ = previous_ = skip_ = showMe_ = keep_ = undoAll_ = gfx::Rect{};
    const std::string prevLabel = "Pr\xC3\xA9" "c\xC3\xA9" "dent";
    const std::string skipLabel = "Passer";
    if (completed_) {
        if (undoOffered()) {
            const std::string keepLabel = "Garder", undoLabel = "Tout d\xC3\xA9" "faire";
            keep_ = {b.right() - kPad - widthOf(keepLabel, false), y, widthOf(keepLabel, false), buttonsH};
            undoAll_ = {b.x + kPad, y, widthOf(undoLabel, false), buttonsH};
            button(keep_, keepLabel, true, hover_ == 4, 0);
            button(undoAll_, undoLabel, false, hover_ == 5, 0);
        } else {
            const std::string endLabel = "Terminer";
            next_ = {b.right() - kPad - widthOf(endLabel, false), y, widthOf(endLabel, false), buttonsH};
            button(next_, endLabel, true, hover_ == 0, 0);
        }
    } else if (live && fullNav_) {
        // Lot API 7 : Passer et Montre-moi a gauche ; Precedent et Suivant a
        // droite. Suivant reste en place, grise (sans rectangle : un clic n'y
        // fait rien) tant que le geste n'est pas fait - on voit ou l'on va.
        float x = b.x + kPad;
        skip_ = {x, y, widthOf(skipLabel, false), buttonsH};
        button(skip_, skipLabel, false, hover_ == 2, 0);
        x = skip_.right() + 8.f;
        if (!already_ && st->showMe) {
            const std::string showLabel = "Montre-moi";
            showMe_ = {x, y, widthOf(showLabel, false), buttonsH};
            button(showMe_, showLabel, false, hover_ == 3, 0);
        }
        const std::string nextLabel = index_ + 1 == steps_.size() ? "Terminer" : "Suivant";
        const gfx::Rect nextRect{b.right() - kPad - widthOf(nextLabel, false), y, widthOf(nextLabel, false), buttonsH};
        if (already_) {
            next_ = nextRect;
            button(next_, nextLabel, true, hover_ == 0, 0);
        } else {
            ctx.r.fillRoundedRect(nextRect, c.inputBg, 5.f);
            ctx.r.strokeRect(nextRect, c.border, 1.f);
            const float w = ctx.r.measure(nextLabel, bodyFont).width;
            ctx.r.drawText({nextRect.x + (nextRect.w - w) / 2.f, nextRect.y + (nextRect.h - lh) / 2.f}, nextLabel, bodyFont, c.textDisabled);
        }
        if (index_ > 0) {
            const float pw = widthOf(prevLabel, false);
            previous_ = {nextRect.x - 8.f - pw, y, pw, buttonsH};
            button(previous_, prevLabel, false, hover_ == 1, 0);
        }
    } else if (live) {
        // Precedent et Passer a gauche ; Suivant (deja fait) ou Montre-moi a droite.
        float x = b.x + kPad;
        if (index_ > 0) {
            previous_ = {x, y, widthOf(prevLabel, true), buttonsH};
            button(previous_, prevLabel, false, hover_ == 1, -1);
            x = previous_.right() + 8.f;
        }
        skip_ = {x, y, widthOf(skipLabel, false), buttonsH};
        button(skip_, skipLabel, false, hover_ == 2, 0);
        if (already_) {
            const std::string nextLabel = index_ + 1 == steps_.size() ? "Terminer" : "Suivant";
            next_ = {b.right() - kPad - widthOf(nextLabel, true), y, widthOf(nextLabel, true), buttonsH};
            button(next_, nextLabel, true, hover_ == 0, 1);
        } else if (st->showMe) {
            const std::string showLabel = "Montre-moi";
            showMe_ = {b.right() - kPad - widthOf(showLabel, true), y, widthOf(showLabel, true), buttonsH};
            button(showMe_, showLabel, true, hover_ == 3, 1);
        }
    } else {
        // Une visite : Passer a gauche ; Precedent et Suivant / Terminer a droite.
        const bool last = index_ + 1 == steps_.size();
        const std::string nextLabel = last ? "Terminer" : "Suivant";
        const float nw = ctx.r.measure(nextLabel, bodyFont).width + 36.f;
        const float pw = ctx.r.measure(prevLabel, bodyFont).width + 28.f;
        const float kw = ctx.r.measure(skipLabel, bodyFont).width + 28.f;
        next_ = {b.right() - kPad - nw, y, nw, buttonsH};
        previous_ = index_ > 0 ? gfx::Rect{next_.x - 10.f - pw, y, pw, buttonsH} : gfx::Rect{};
        skip_ = last ? gfx::Rect{} : gfx::Rect{b.x + kPad, y, kw, buttonsH};
        button(next_, nextLabel, true, hover_ == 0, 0);
        if (!previous_.empty()) button(previous_, prevLabel, false, hover_ == 1, 0);
        if (!skip_.empty()) button(skip_, skipLabel, false, hover_ == 2, 0);
    }
}

ui::EventResult HmiTutorial::passThrough(const ui::InputEvent& ev) {
    // Ce qui tombe hors de la bulle va ou il serait alle sans elle : une liste
    // deroulante ouverte (un autre proprietaire de la passe du dessus) d'abord,
    // puis l'arbre (WidgetMenu le fait si on rend Ignored).
    if (auto* host = parent()) {
        const auto& kids = host->children();
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
            if (it->get() == this) continue;
            if (ui::Widget* owner = (*it)->findOverlayOwner(); owner && owner != this) {
                if (owner->dispatch(ev) == ui::EventResult::Consumed) return ui::EventResult::Consumed;
                break;
            }
        }
    }
    return ui::EventResult::Ignored;
}

ui::EventResult HmiTutorial::onEvent(const ui::InputEvent& ev) {
    if (!active_) return ui::EventResult::Ignored;
    const bool live = interactive() || completed_;
    const auto partAt = [&](gfx::Point p) {
        if (!next_.empty() && next_.contains(p)) return 0;
        if (!previous_.empty() && previous_.contains(p)) return 1;
        if (!skip_.empty() && skip_.contains(p)) return 2;
        if (!showMe_.empty() && showMe_.contains(p)) return 3;
        if (!keep_.empty() && keep_.contains(p)) return 4;
        if (!undoAll_.empty() && undoAll_.contains(p)) return 5;
        return -1;
    };
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        // Une etape interactive laisse le clavier a l'ecran (Ctrl+Z, Suppr, la saisie).
        if (live) return ui::EventResult::Ignored;
        switch (k->key) {
            case ui::Key::Return: case ui::Key::Right: case ui::Key::Space: success_.clear(); next(); break;
            case ui::Key::Left: case ui::Key::Backspace: previous(); break;
            case ui::Key::Escape: stop(false); break;
            default: break;
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int was = hover_;
        hover_ = pressedOutside_ ? -1 : partAt(m->pos);
        if (was != hover_) invalidate();
        if (live && (pressedOutside_ || !bubble_.contains(m->pos))) return passThrough(ev);
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (live && !bubble_.contains(d->pos)) {
            pressedOutside_ = true;          // le glisser qui commence (un cadre de selection) passe dessous jusqu'au bout
            return passThrough(ev);
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseUp>(&ev)) {
        if (pressedOutside_) {
            pressedOutside_ = false;
            if (live) return passThrough(ev);
        }
        if (live && !bubble_.contains(d->pos)) return passThrough(ev);
        if (d->button == ui::MouseButton::Left) {
            switch (partAt(d->pos)) {
                case 0:
                    success_.clear();
                    if (completed_) stop(true);
                    else next();
                    break;
                case 1: previous(); break;
                case 2: stop(false); break;
                case 3: showMe(); break;
                case 4: keep(); break;
                case 5: undoAll(); break;
                default: break;
            }
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (live && !bubble_.contains(w->pos)) return passThrough(ev);
        return ui::EventResult::Consumed;
    }
    // Le reste - texte, glisser de fichier - : une etape interactive le laisse
    // passer, une visite le garde.
    if (live) return passThrough(ev);
    return ui::EventResult::Consumed;
}

} // namespace app
