#include "WidgetHost.hpp"
#include "widgets/TrailSymbols.hpp"   // 1.11 (chantier T3, C4) : ✓ ✕ ⊘ ⇩ traces au trait

#include <algorithm>
#include <string>
#include <vector>

namespace ui {

WidgetHost::WidgetHost(WidgetPtr root) : root_(std::move(root)) {
    focus_.setRoot(root_.get());
}

WidgetHost::~WidgetHost() = default;

void WidgetHost::layout(gfx::Size surface) {
    if (!root_) return;
    root_->setBounds({0.f, 0.f, surface.w, surface.h});
    root_->layout();
}

void WidgetHost::paint(gfx::IRenderer& r, const Theme& theme, gfx::Size surface, double time, bool tooltips) {
    if (!root_) return;
    if (root_->bounds().empty()) layout(surface);

    std::vector<Widget*> overlays;
    const PaintContext ctx{r, theme, {0.f, 0.f, surface.w, surface.h}, time, &overlays};
    root_->render(ctx);
    // La passe du dessus : les listes ouvertes, les menus de la page, qui
    // doivent sortir du rectangle de leur parent.
    for (auto* w : overlays) w->paintTopMost(ctx);

    const Widget* under = tooltips && pointerInside_ ? tooltipTargetAt(*root_, mouse_) : nullptr;
    if (under != tooltipTarget_) {
        tooltipTarget_ = under;
        hoverSince_ = time;             // le temps d'attente repart
        tipPlacement_.valid = false;    // lot 7 : une autre infobulle choisit son cote
    }
    // Lot 7 : le texte du moment, relu a chaque image (une valeur qui change).
    if (tooltipTarget_ && time - hoverSince_ >= 0.9)
        paintTooltip(r, theme, surface, mouse_, tooltipTarget_->liveTooltip(mouse_), &tipPlacement_);
}

EventResult WidgetHost::dispatch(const InputEvent& ev) {
    if (!root_) return EventResult::Ignored;
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        mouse_ = m->pos;
        pointerInside_ = true;
    }
    // Ce qui peint par-dessus tout (une liste ouverte) recoit l'evenement avant
    // l'arbre : sinon le tableau dessous prend le clic destine a la liste.
    if (Widget* overlay = root_->findOverlayOwner())
        if (overlay->dispatch(ev) == EventResult::Consumed) return EventResult::Consumed;
    // Tab : le widget qui a le focus d'abord, puis le suivant dans l'ordre.
    if (const auto* k = std::get_if<KeyDown>(&ev); k && k->key == Key::Tab) {
        if (root_->dispatch(ev) == EventResult::Consumed) return EventResult::Consumed;
        focus_.focusNext(k->mods.shift);
        return EventResult::Consumed;
    }
    return root_->dispatch(ev);
}

void WidgetHost::pointerLeft() {
    pointerInside_ = false;
    tooltipTarget_ = nullptr;
    if (root_) clearHover(*root_);
}

void WidgetHost::clearHover(Widget& w) {
    w.setHovered(false);
    for (auto& c : w.children_) clearHover(*c);
}

void WidgetHost::release(Widget& w) {
    w.setHovered(false);
    w.setFocusedInternal(false);
    for (auto& c : w.children_) release(*c);
}

const Widget* WidgetHost::tooltipTargetAt(Widget& root, gfx::Point p) {
    // Comme WidgetMenu::tooltipTargetAt : l'infobulle d'un widget cache sous une
    // liste ouverte ne perce pas au travers.
    if (Widget* overlay = root.findOverlayOwner(); overlay && overlay->eventBounds().contains(p) && overlay->overlayCovers(p))
        return overlay->tooltipAt(p);
    return root.tooltipAt(p);
}

void WidgetHost::paintTooltip(gfx::IRenderer& r, const Theme& theme, gfx::Size surface, gfx::Point mouse,
                              std::string_view text, TooltipPlacement* placement) {
    // Le meme dessin que WidgetMenu::drawTooltip : une infobulle d'une fenetre
    // detachee ressemble a celle de la fenetre principale.
    if (text.empty()) return;
    const auto& c = theme.color;
    const auto f = theme.font.ui;
    const float pad = 8.f;
    const float maxW = std::max(160.f, std::min(surface.w - 24.f, 760.f)) - 2 * pad;
    // 1.11 (chantier T3, C4, tranche 11) : ✓ ✕ ⊘ ⇩ sont traces au trait, comme dans
    // l'arbre, chacun dans la couleur de son ton (la legende du « ? », le resume d'une
    // unite) : ils ne dependent pas de la police du poste.
    const float lh = r.lineHeight(f);
    const float side = std::clamp(lh * 0.8f, 10.f, 16.f);
    auto widthOf = [&](std::string_view l) {
        return trailsym::contains(l) ? trailsym::width(r, l, f, side) : r.measure(l, f).width;
    };

    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        std::string para(text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start));
        while (!para.empty() && widthOf(para) > maxW) {
            std::size_t cut = para.size();
            std::size_t best = std::string::npos;
            while (cut > 0) {
                const std::size_t sp = para.rfind(' ', cut - 1);
                if (sp == std::string::npos || sp == 0) break;
                if (widthOf(std::string_view(para).substr(0, sp)) <= maxW) { best = sp; break; }
                cut = sp;
            }
            if (best == std::string::npos) break;          // un seul mot trop long : tel quel
            lines.push_back(para.substr(0, best));
            para.erase(0, best + 1);
        }
        lines.push_back(std::move(para));
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    float textW = 0.f;
    for (const auto& l : lines) textW = std::max(textW, widthOf(l));
    const float w = std::min(textW, maxW) + 2 * pad;
    const float h = lh * static_cast<float>(lines.size()) + 2 * pad * 0.6f;

    // Dans la fenetre : a gauche ou au-dessus de la souris quand ca deborde.
    // Lot 7 : le cote choisi a l'ouverture reste tant qu'elle est ouverte au
    // meme endroit (son texte change : elle grandit ou retrecit sur place) ; il
    // ne change que si elle ne tient plus de ce cote-la.
    const bool keep = placement && placement->valid && placement->mouse.x == mouse.x && placement->mouse.y == mouse.y;
    bool left = keep && placement->left;
    bool above = keep && placement->above;
    if (!left && mouse.x + 14.f + w > surface.w) left = true;
    if (!above && mouse.y + 20.f + h > surface.h) above = true;
    const float x = left ? std::max(0.f, mouse.x - w - 6.f) : mouse.x + 14.f;
    const float y = above ? std::max(0.f, mouse.y - h - 6.f) : mouse.y + 20.f;
    if (placement) *placement = TooltipPlacement{true, left, above, mouse};

    const gfx::Rect box{x, y, w, h};
    r.fillRect({box.x + 2.f, box.y + 2.f, box.w, box.h}, gfx::Color{0, 0, 0, 90});   // l'ombre
    r.fillRect(box, c.headerBg);
    r.strokeRect(box, c.borderStrong, 1.f);
    float ly = box.y + pad * 0.6f;
    for (const auto& l : lines) {
        if (trailsym::contains(l))
            trailsym::draw(r, l, f, box.x + pad, ly, ly + lh * 0.5f, side, c.text, &theme);
        else
            r.drawText({box.x + pad, ly}, l, f, c.text);
        ly += lh;
    }
}

} // namespace ui
