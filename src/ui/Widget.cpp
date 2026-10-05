#include "Widget.hpp"

#include <algorithm>

namespace ui {

    // ------------------------------------------------------ platform services ---
    namespace {
        PlatformServices g_services;

        // Until App installs the real thing, derive from the pixel size in FontId. The
        // debug font is fixed-pitch and square, so advance == size is exact for it and
        // a sane over-estimate for a proportional face.
        float fallbackAdvance(gfx::FontId f) { return f.v ? static_cast<float>(f.v) : 13.f; }
    } // namespace

    void installPlatformServices(PlatformServices services) { g_services = std::move(services); }

    // Lot API 7 : la surface d'une fenetre detachee, le temps de son tour.
    namespace {
        bool      g_surfaceOverridden = false;
        gfx::Size g_surfaceOverride{};
    } // namespace

    SurfaceScope::SurfaceScope(gfx::Size size) noexcept
        : previous_(g_surfaceOverride), hadPrevious_(g_surfaceOverridden) {
        g_surfaceOverride = size;
        g_surfaceOverridden = true;
    }

    SurfaceScope::~SurfaceScope() {
        g_surfaceOverride = previous_;
        g_surfaceOverridden = hadPrevious_;
    }

    gfx::Size surfaceSize() {
        if (g_surfaceOverridden) return g_surfaceOverride;
        return g_services.surfaceSize ? g_services.surfaceSize() : gfx::Size{};
    }

    float measureWidth(std::string_view utf8, gfx::FontId font) {
        if (g_services.measureWidth) return g_services.measureWidth(utf8, font);
        std::size_t glyphs = 0;
        for (char c : utf8)
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++glyphs;
        return static_cast<float>(glyphs) * fallbackAdvance(font);
    }

    float lineHeight(gfx::FontId font) {
        return g_services.lineHeight ? g_services.lineHeight(font) : fallbackAdvance(font);
    }

    std::string clipboardText() {
        return g_services.clipboardGet ? g_services.clipboardGet() : std::string{};
    }

    void setClipboardText(std::string_view utf8) {
        if (g_services.clipboardSet) g_services.clipboardSet(utf8);
    }

    // ------------------------------------------- lot macros 1 : les fichiers ---
    namespace {
        std::vector<std::string> g_pickAnswers;          // les reponses rejouees, dans l'ordre
        std::vector<std::string> g_scriptedClipboardFiles;
    } // namespace

    bool pickFile(const FilePick& request, std::function<void(std::string)> done) {
        if (!g_pickAnswers.empty()) {
            // Une session rejouee : la reponse est deja la, l'explorateur ne
            // s'ouvre pas (il attendrait une souris qui ne viendra pas).
            auto answer = std::move(g_pickAnswers.front());
            g_pickAnswers.erase(g_pickAnswers.begin());
            if (done) done(std::move(answer));
            return true;
        }
        if (!g_services.pickFile) return false;
        return g_services.pickFile(request, std::move(done));
    }

    bool canPickFile() { return !g_pickAnswers.empty() || static_cast<bool>(g_services.pickFile); }

    std::vector<std::string> clipboardFiles() {
        if (!g_scriptedClipboardFiles.empty()) return g_scriptedClipboardFiles;
        return g_services.clipboardFiles ? g_services.clipboardFiles() : std::vector<std::string>{};
    }

    void queueFilePick(std::string answer) { g_pickAnswers.push_back(std::move(answer)); }
    void setScriptedClipboardFiles(std::vector<std::string> files) { g_scriptedClipboardFiles = std::move(files); }

    // ---------------------------------------------------------------- Widget ---
    Widget::Widget(std::string id) : id_(std::move(id)) {}
    Widget::~Widget() = default;

    // --------------------------------------------------------------- geometry ---
    void Widget::setBounds(const gfx::Rect& r) {
        if (r.x == bounds_.x && r.y == bounds_.y && r.w == bounds_.w && r.h == bounds_.h) return;
        bounds_ = r;
        dirtyLayout_ = true;
        invalidate();
    }

    gfx::Rect Widget::contentRect() const noexcept {
        return { bounds_.x + padding_.l,
                bounds_.y + padding_.t,
                std::max(0.f, bounds_.w - padding_.l - padding_.r),
                std::max(0.f, bounds_.h - padding_.t - padding_.b) };
    }

    gfx::Point Widget::toLocal(gfx::Point g) const noexcept {
        return { g.x - bounds_.x, g.y - bounds_.y };
    }

    // ------------------------------------------------------------------ state ---
    void Widget::setVisibility(Visibility v) {
        if (visibility_ == v) return;
        visibility_ = v;
        if (parent_) parent_->invalidateLayout(); else invalidateLayout();
    }

    void Widget::setEnabled(bool e) {
        if (enabled_ == e) return;
        enabled_ = e;
        if (!e && focused_) releaseFocus();
        onEnabledChanged(e);
        invalidate();
    }

    void Widget::setHovered(bool h) {
        if (hovered_ == h) return;
        hovered_ = h;
        hoverChanged->emit(h);
        invalidate();
    }

    void Widget::setFocusedInternal(bool f) {
        if (focused_ == f) return;
        focused_ = f;
        onFocusChanged(f);
        (f ? focusGained : focusLost)->emit();
        invalidate();
    }

    Widget* Widget::rootWidget() noexcept {
        Widget* w = this;
        while (w->parent_) w = w->parent_;
        return w;
    }

    void Widget::clearFocusRecursive(Widget& w, Widget* keep) {
        if (&w != keep) w.setFocusedInternal(false);
        for (auto& c : w.children_) clearFocusRecursive(*c, keep);
    }

    void Widget::grabFocus() {
        if (!acceptsFocus()) return;
        clearFocusRecursive(*rootWidget(), this);
        setFocusedInternal(true);
    }

    void Widget::releaseFocus() { setFocusedInternal(false); }

    // ------------------------------------------------------------------- tree ---
    Widget& Widget::addChild(WidgetPtr child) {
        child->parent_ = this;
        children_.push_back(std::move(child));
        invalidateLayout();
        return *children_.back();
    }

    WidgetPtr Widget::removeChild(Widget& child) {
        auto it = std::find_if(children_.begin(), children_.end(),
            [&](const WidgetPtr& c) { return c.get() == &child; });
        if (it == children_.end()) return nullptr;
        WidgetPtr owned = std::move(*it);
        children_.erase(it);
        owned->parent_ = nullptr;
        invalidateLayout();
        return owned;
    }

    Widget* Widget::findById(std::string_view id) {
        if (id_ == id) return this;
        for (auto& c : children_)
            if (auto* found = c->findById(id)) return found;
        return nullptr;
    }

    // ------------------------------------------------------------------ frame ---
    void Widget::invalidate() noexcept {
        dirtyPaint_ = true;
        for (Widget* p = parent_; p && !p->dirtyPaint_; p = p->parent_) p->dirtyPaint_ = true;
    }

    void Widget::invalidateLayout() noexcept {
        dirtyLayout_ = true;
        invalidate();
        for (Widget* p = parent_; p && !p->dirtyLayout_; p = p->parent_) {
            p->dirtyLayout_ = true;
            p->dirtyPaint_ = true;
        }
    }

    SizeHint Widget::sizeHint() const {
        SizeHint h;
        h.preferred = { bounds_.w, bounds_.h };
        h.stretchX = h.stretchY = 1.f;
        return h;
    }

    void Widget::layout() {
        if (visibility_ == Visibility::Collapsed) return;
        if (dirtyLayout_) {
            onLayout();
            dirtyLayout_ = false;
        }
        for (auto& c : children_) c->layout();
    }

    void Widget::render(const PaintContext& ctx) {
        if (!visible()) return;

        const gfx::Rect clip = ctx.clip.intersect(bounds_);
        if (clip.empty()) return;

        PaintContext local{ ctx.r, ctx.theme, clip, ctx.time, ctx.overlays };
        ctx.r.pushClip(clip);
        onPaint(local);
        for (auto& c : children_) c->render(local);
        // A widget that asked for the top-most pass is painted there, and only
        // there. Painting it here as well draws it twice. That used to be harmless
        // because a dropdown popup was clipped away by its parent anyway, but a
        // full-surface overlay is not clipped, and a shadow at alpha 90 drawn twice
        // is twice as dark.
        if (!requestsOverlayPass()) onPaintOverlay(local);
        ctx.r.popClip();

        if (ctx.overlays && requestsOverlayPass()) ctx.overlays->push_back(this);
        dirtyPaint_ = false;
    }

    void Widget::paintTopMost(const PaintContext& ctx) {
        // Painted after the whole tree, clipped only by the window.
        ctx.r.pushClip(ctx.clip);
        onPaintOverlay(ctx);
        ctx.r.popClip();
    }

    bool Widget::hitTest(gfx::Point local) const {
        return local.x >= 0.f && local.y >= 0.f && local.x < bounds_.w && local.y < bounds_.h;
    }

    namespace {
        const gfx::Point* pointerPosition(const InputEvent& ev) {
            if (const auto* m = std::get_if<MouseMove>(&ev))   return &m->pos;
            if (const auto* m = std::get_if<MouseDown>(&ev))   return &m->pos;
            if (const auto* m = std::get_if<MouseUp>(&ev))     return &m->pos;
            if (const auto* m = std::get_if<MouseWheel>(&ev))  return &m->pos;
            if (const auto* m = std::get_if<FileDropped>(&ev)) return &m->pos;
            return nullptr;
        }
    } // namespace

    Widget* Widget::findOverlayOwner() {
        if (!visible()) return nullptr;
        if (requestsOverlayPass()) return this;
        // Front-most first, matching paint order.
        for (auto it = children_.rbegin(); it != children_.rend(); ++it)
            if (Widget* found = (*it)->findOverlayOwner()) return found;
        return nullptr;
    }

    const Widget* Widget::tooltipAt(gfx::Point p) const {
        if (!visible() || !bounds_.contains(p)) return nullptr;
        for (auto it = children_.rbegin(); it != children_.rend(); ++it)
            if (const Widget* found = (*it)->tooltipAt(p)) return found;
        return hasTooltip() ? this : nullptr;
    }

    // Lot 7 : le texte du moment (voir Widget.hpp) - relu a chaque image par
    // l'hote tant que l'infobulle est ouverte.
    std::string Widget::liveTooltip(gfx::Point) const {
        if (tooltipProvider_) return tooltipProvider_();
        return tooltip_;
    }

    EventResult Widget::dispatch(const InputEvent& ev) {
        if (!visible() || !enabled()) return EventResult::Ignored;

        const gfx::Point* p = pointerPosition(ev);
        const bool inside = !p || eventBounds().contains(*p);

        if (p) {
            // Hover tracking happens even for events the widget will not consume,
            // otherwise a widget stays highlighted after the cursor has left it.
            setHovered(inside && hitTest(toLocal(*p)));
            if (!inside) {
                for (auto& c : children_) c->dispatch(ev);   // let children un-hover
                return EventResult::Ignored;
            }
        }

        // Children first, front-most (last added) to back: the visual stacking order
        // reversed is the hit-testing order.
        for (auto it = children_.rbegin(); it != children_.rend(); ++it)
            if ((*it)->dispatch(ev) == EventResult::Consumed) return EventResult::Consumed;

        return onEvent(ev);
    }

    // ------------------------------------------------------------ FocusChain ----
    void FocusChain::focus(Widget* w) {
        if (current_ == w) return;
        if (current_) current_->setFocusedInternal(false);
        current_ = (w && w->acceptsFocus()) ? w : nullptr;
        if (current_) current_->setFocusedInternal(true);
    }

    void FocusChain::clear() { focus(nullptr); }

    namespace {
        void collectFocusable(Widget& w, std::vector<Widget*>& out) {
            if (!w.visible()) return;
            if (w.acceptsFocus()) out.push_back(&w);
            for (const auto& c : w.children()) collectFocusable(*c, out);
        }
    } // namespace

    void FocusChain::focusNext(bool backwards) {
        if (!root_) return;
        std::vector<Widget*> order;
        collectFocusable(*root_, order);
        if (order.empty()) { clear(); return; }

        auto it = std::find(order.begin(), order.end(), current_);
        std::size_t index = 0;
        if (it == order.end()) index = backwards ? order.size() - 1 : 0;
        else {
            const auto pos = static_cast<std::size_t>(it - order.begin());
            index = backwards ? (pos + order.size() - 1) % order.size() : (pos + 1) % order.size();
        }
        focus(order[index]);
    }

} // namespace ui