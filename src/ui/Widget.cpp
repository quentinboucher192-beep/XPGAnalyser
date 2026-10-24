#include "Widget.hpp"

#include "DialogFrame.hpp"   // 1.12.21 : un dialogue qui s'en va oublie sa place
#include "Zoom.hpp"          // 1.12.30 : le zoom de chaque page

#include <algorithm>
#include <cmath>
#include <type_traits>

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
    // 1.12.31 : et la fenetre elle-meme, que la portee d'un zoom ne change pas.
    namespace {
        bool      g_surfaceOverridden = false;
        gfx::Size g_surfaceOverride{};
        bool      g_windowOverridden = false;
        gfx::Size g_windowOverride{};
    } // namespace

    SurfaceScope::SurfaceScope(gfx::Size size, bool zoom) noexcept
        : previous_(g_surfaceOverride), hadPrevious_(g_surfaceOverridden),
          previousWindow_(g_windowOverride), hadPreviousWindow_(g_windowOverridden), zoom_(zoom) {
        g_surfaceOverride = size;
        g_surfaceOverridden = true;
        if (!zoom_) {
            g_windowOverride = size;
            g_windowOverridden = true;
        }
    }

    SurfaceScope::~SurfaceScope() {
        g_surfaceOverride = previous_;
        g_surfaceOverridden = hadPrevious_;
        if (!zoom_) {
            g_windowOverride = previousWindow_;
            g_windowOverridden = hadPreviousWindow_;
        }
    }

    gfx::Size surfaceSize() {
        if (g_surfaceOverridden) return g_surfaceOverride;
        return g_services.surfaceSize ? g_services.surfaceSize() : gfx::Size{};
    }

    gfx::Size windowSurfaceSize() {
        if (g_windowOverridden) return g_windowOverride;
        return g_services.surfaceSize ? g_services.surfaceSize() : gfx::Size{};
    }

    // 1.12.30 : la mesure pendant une mise en page zoomee (MeasureZoomScope).
    namespace {
        float g_measureZoom = 1.f;

        float measureAt(std::string_view utf8, gfx::FontId font) {
            if (g_services.measureWidth) return g_services.measureWidth(utf8, font);
            std::size_t glyphs = 0;
            for (char c : utf8)
                if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++glyphs;
            return static_cast<float>(glyphs) * fallbackAdvance(font);
        }
        float lineHeightAt(gfx::FontId font) {
            return g_services.lineHeight ? g_services.lineHeight(font) : fallbackAdvance(font);
        }
    } // namespace

    MeasureZoomScope::MeasureZoomScope(float factor, bool absolute) noexcept : previous_(g_measureZoom) {
        g_measureZoom = absolute ? factor : previous_ * factor;
    }
    MeasureZoomScope::~MeasureZoomScope() { g_measureZoom = previous_; }
    float measureZoom() noexcept { return g_measureZoom; }

    gfx::FontId zoomedFont(gfx::FontId font, float factor) noexcept {
        const float px = static_cast<float>(font.v ? font.v : 16) * factor;
        return gfx::FontId{ static_cast<std::uint16_t>(std::clamp(std::lround(px), 6L, 400L)) };
    }

    float measureWidth(std::string_view utf8, gfx::FontId font) {
        if (g_measureZoom == 1.f) return measureAt(utf8, font);
        return measureAt(utf8, zoomedFont(font, g_measureZoom)) / g_measureZoom;
    }

    float lineHeight(gfx::FontId font) {
        if (g_measureZoom == 1.f) return lineHeightAt(font);
        return lineHeightAt(zoomedFont(font, g_measureZoom)) / g_measureZoom;
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

    // 1.12.27 : glisser du texte hors de la fenetre.
    int dragText(std::string_view utf8) { return g_services.dragText ? g_services.dragText(utf8) : -1; }
    bool canDragText() { return static_cast<bool>(g_services.dragText); }

    void queueFilePick(std::string answer) { g_pickAnswers.push_back(std::move(answer)); }
    void setScriptedClipboardFiles(std::vector<std::string> files) { g_scriptedClipboardFiles = std::move(files); }

    // ------------------------------------------------- 1.12.30 : les reperes ---
    namespace {
        // Un evenement de souris dont la position passe d'un repere a l'autre
        // (`at`) ; un deplacement relatif suit l'echelle (`scale` : du nouveau
        // repere sur l'ancien).
        template <class F>
        InputEvent remapPointerImpl(const InputEvent& ev, F&& at, float scale) {
            InputEvent out = ev;
            std::visit([&](auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, MouseMove>) {
                    e.pos = at(e.pos);
                    e.delta = { e.delta.x * scale, e.delta.y * scale };
                } else if constexpr (std::is_same_v<T, MouseDown> || std::is_same_v<T, MouseUp> || std::is_same_v<T, MouseWheel>
                                     || std::is_same_v<T, FileDropped> || std::is_same_v<T, TextDropped>) {
                    e.pos = at(e.pos);
                }
            }, out);
            return out;
        }
        Widget::ZoomRequest g_zoomRequest;

        // Le repere d'un widget vers la surface (tous les parents zoomes compris ; un
        // ilot s'arrete a lui : son repere est la surface).
        ZoomMap ownToSurface(const Widget& w) { return w.mapToSurface(); }
    } // namespace

    // ---------------------------------------------------------------- Widget ---
    Widget::Widget(std::string id) : id_(std::move(id)) {}
    namespace {
        Widget* gMouseHolder = nullptr;   // 1.11.4 : la souris tenue (un glisser en cours)
    } // namespace

    Widget::~Widget() {
        if (gMouseHolder == this) gMouseHolder = nullptr;
        dialogframe::forget(this);
    }

    void Widget::captureMouse() noexcept { gMouseHolder = this; }
    void Widget::releaseMouse() noexcept {
        if (gMouseHolder == this) gMouseHolder = nullptr;
    }
    bool Widget::holdsMouse() const noexcept { return gMouseHolder == this; }

    bool Widget::routeCapturedMouse(Widget& root, const InputEvent& ev) {
        Widget* holder = gMouseHolder;
        if (!holder) return false;
        const bool move = std::holds_alternative<MouseMove>(ev);
        const bool up = std::holds_alternative<MouseUp>(ev);
        if (!move && !up) return false;
        const Widget* top = holder;
        while (top->parent_) top = top->parent_;
        if (top != &root) return false;
        // Le tenant n'est plus montre (un onglet change pendant le glisser) : il la rend.
        if (!holder->visible()) {
            gMouseHolder = nullptr;
            return false;
        }
        // 1.12.30 : l'evenement dans son repere (un tableau zoome tire son pouce).
        if (const auto m = ownToSurface(*holder); !m.identity())
            (void)holder->onEvent(remapPointerImpl(ev, [&m](gfx::Point p) { return m.toInner(p); }, 1.f / m.s));
        else
            (void)holder->onEvent(ev);
        if (up) holder->releaseMouse();
        return true;
    }

    // --------------------------------------------------------------- geometry ---
    void Widget::setBounds(const gfx::Rect& r) {
        if (r.x == frame_.x && r.y == frame_.y && r.w == frame_.w && r.h == frame_.h) return;
        frame_ = r;
        // 1.12.30 : zoome, le widget se range dans un repere agrandi (le coin ne bouge pas).
        // 1.12.31 : un ilot, dans celui de la surface.
        bounds_ = scaledNow() ? gfx::Rect{ r.x, r.y, r.w / zoomFactor_, r.h / zoomFactor_ } : islandNow() ? islandRect(r) : r;
        dirtyLayout_ = true;
        invalidate();
    }

    // ------------------------------------------------------- 1.12.30 : le zoom ---
    void Widget::setZoomSupport(ZoomSupport s) {
        if (s == zoomSupport_) return;
        zoomSupport_ = s;
        if (s != ZoomSupport::Scaled) zoomFactor_ = 1.f;
        if (s != ZoomSupport::Native) { islandS_ = 1.f; islandT_ = {}; }
        bounds_ = scaledNow() ? gfx::Rect{ frame_.x, frame_.y, frame_.w / zoomFactor_, frame_.h / zoomFactor_ }
                : islandNow() ? islandRect(frame_) : frame_;
        invalidateLayout();
    }

    // ---------------------------------------------------- 1.12.31 : les ilots ---
    gfx::Rect Widget::islandRect(const gfx::Rect& r) const noexcept {
        // Le cadre vu de la surface, ses bords sur des pixels entiers (un document net).
        const float x0 = std::round(r.x * islandS_ + islandT_.x), y0 = std::round(r.y * islandS_ + islandT_.y);
        const float x1 = std::round((r.x + r.w) * islandS_ + islandT_.x), y1 = std::round((r.y + r.h) * islandS_ + islandT_.y);
        return { x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0) };
    }

    void Widget::refreshIsland() {
        // Le repere du parent vers la surface : un parent zoome (la page) en fait un ilot.
        const ZoomMap m = parent_ ? parent_->mapToSurface() : ZoomMap{};
        if (m.s == islandS_ && m.t.x == islandT_.x && m.t.y == islandT_.y) return;
        islandS_ = m.s;
        islandT_ = m.t;
        bounds_ = islandNow() ? islandRect(frame_) : frame_;
        dirtyLayout_ = true;
        invalidate();
    }

    ZoomMap Widget::mapToParent() const noexcept {
        if (scaledNow()) return ZoomMap::about({ frame_.x, frame_.y }, zoomFactor_);
        // Un ilot : du parent vers lui, p x islandS_ + islandT_ ; de lui vers le parent, l'inverse.
        if (islandNow()) return { 1.f / islandS_, { -islandT_.x / islandS_, -islandT_.y / islandS_ } };
        return {};
    }

    ZoomMap Widget::mapToSurface() const noexcept {
        ZoomMap m;
        for (const Widget* w = this; w; w = w->parent_) {
            if (w->islandNow()) return m;          // le repere d'un ilot est la surface
            if (w->scaledNow()) m = ZoomMap::then(m, w->mapToParent());
        }
        return m;
    }

    void Widget::setZoom(float zoom) {
        // Un widget Native redefinit setZoom ; sans zoom, rien.
        if (zoomSupport_ != ZoomSupport::Scaled || !std::isfinite(zoom)) return;
        const float z = std::clamp(zoom, minZoom(), maxZoom());
        if (std::fabs(z - zoomFactor_) < 1e-4f) return;
        zoomFactor_ = std::fabs(z - 1.f) < 1e-4f ? 1.f : z;
        bounds_ = scaledNow() ? gfx::Rect{ frame_.x, frame_.y, frame_.w / zoomFactor_, frame_.h / zoomFactor_ } : frame_;
        invalidateLayout();
    }

    gfx::Point Widget::toParentSpace(gfx::Point own) const noexcept {
        if (scaledNow()) return { frame_.x + (own.x - frame_.x) * zoomFactor_, frame_.y + (own.y - frame_.y) * zoomFactor_ };
        if (islandNow()) return { (own.x - islandT_.x) / islandS_, (own.y - islandT_.y) / islandS_ };
        return own;
    }

    gfx::Point Widget::fromParentSpace(gfx::Point outer) const noexcept {
        if (scaledNow()) return { frame_.x + (outer.x - frame_.x) / zoomFactor_, frame_.y + (outer.y - frame_.y) / zoomFactor_ };
        if (islandNow()) return { outer.x * islandS_ + islandT_.x, outer.y * islandS_ + islandT_.y };
        return outer;
    }

    gfx::Point Widget::toSurface(gfx::Point own) const noexcept {
        // 1.12.31 : la correspondance composee (un ilot s'y arrete : son repere est la surface).
        const auto m = mapToSurface();
        return m.identity() ? own : m.toOuter(own);
    }

    gfx::Rect Widget::toSurface(const gfx::Rect& own) const noexcept {
        const auto a = toSurface(gfx::Point{ own.x, own.y });
        const auto b = toSurface(gfx::Point{ own.right(), own.bottom() });
        return { a.x, a.y, b.x - a.x, b.y - a.y };
    }

    gfx::Point Widget::fromSurface(gfx::Point surface) const noexcept {
        const auto m = mapToSurface();
        return m.identity() ? surface : m.toInner(surface);
    }

    float Widget::surfaceScale() const noexcept { return mapToSurface().s; }

    gfx::Rect Widget::outerEventBounds() const {
        const auto r = eventBounds();
        if (!scaledNow() && !islandNow()) return r;
        const auto a = toParentSpace({ r.x, r.y }), b = toParentSpace({ r.right(), r.bottom() });
        return { a.x, a.y, b.x - a.x, b.y - a.y };
    }

    bool Widget::nativeZoomAt(gfx::Point own) const {
        for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
            const Widget& c = **it;
            if (!c.visible() || !c.outerEventBounds().contains(own)) continue;
            if (c.zoomSupport_ == ZoomSupport::Native) return true;
            return c.nativeZoomAt(c.fromParentSpace(own));
        }
        return false;
    }

    void Widget::setZoomRequestHandler(ZoomRequest handler) { g_zoomRequest = std::move(handler); }

    void Widget::requestZoomStep(Widget& w, int step) {
        if (g_zoomRequest) { g_zoomRequest(w, step); return; }
        w.setZoom(zoomStep(w.zoom(), step, w.minZoom(), w.maxZoom()));
    }

    EventResult Widget::dispatchFromSurface(const InputEvent& ev) {
        // Le repere du parent : celui dans lequel dispatch() attend l'evenement.
        if (!parent_) return dispatch(ev);
        const auto m = ownToSurface(*parent_);
        if (m.identity()) return dispatch(ev);
        return dispatch(remapPointerImpl(ev, [&m](gfx::Point p) { return m.toInner(p); }, 1.f / m.s));
    }

    std::string Widget::liveTooltipAt(gfx::Point surfaceMouse) const {
        const auto m = ownToSurface(*this);
        return liveTooltip(m.identity() ? surfaceMouse : m.toInner(surfaceMouse));
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

    Widget* Widget::focusedIn(Widget& root) noexcept {
        if (root.focused_) return &root;
        for (auto& c : root.children_)
            if (Widget* f = focusedIn(*c)) return f;
        return nullptr;
    }

    bool Widget::inTree(Widget& root, const Widget* w) noexcept {
        if (!w) return false;
        if (&root == w) return true;
        for (auto& c : root.children_)
            if (inTree(*c, w)) return true;
        return false;
    }

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
        // 1.12.31 : un widget Native suit le zoom de ses parents (un ilot, ou plus).
        if (zoomSupport_ == ZoomSupport::Native) refreshIsland();
        if (scaledNow()) {
            // 1.12.30 : la mise en page dans le repere agrandi - le texte s'y mesure
            // comme il sera dessine, une liste s'y ouvre du bon cote.
            const MeasureZoomScope measure(zoomFactor_);
            // La surface du moment est dans le repere du parent (un parent zoome a pose la sienne).
            const auto surface = ui::surfaceSize();
            const auto inner = fromParentSpace({ surface.w, surface.h });
            const SurfaceScope scope(surface.w > 0.f ? gfx::Size{ inner.x, inner.y } : surface, true);
            if (dirtyLayout_) {
                onLayout();
                dirtyLayout_ = false;
            }
            for (auto& c : children_) c->layout();
            return;
        }
        if (islandNow()) {
            // 1.12.31 : un ilot se met en page a l'echelle de la surface.
            const MeasureZoomScope measure(1.f, true);
            const SurfaceScope scope(windowSurfaceSize(), true);
            if (dirtyLayout_) {
                onLayout();
                dirtyLayout_ = false;
            }
            for (auto& c : children_) c->layout();
            return;
        }
        if (dirtyLayout_) {
            onLayout();
            dirtyLayout_ = false;
        }
        for (auto& c : children_) c->layout();
    }

    void Widget::render(const PaintContext& ctx) {
        if (!visible()) return;

        if (scaledNow()) {
            // 1.12.30 : LE DESSIN A L'ECHELLE. Le widget et ses enfants dessinent dans
            // leur repere ; le ZoomRenderer passe chaque forme au renderer de dessous,
            // agrandie. Ce qui passe par-dessus tout (une liste ouverte) le sera par
            // paintTopMost, avec la meme echelle.
            const gfx::Rect outer = ctx.clip.intersect(frame_);
            if (outer.empty()) return;
            const auto map = ZoomMap::about({ frame_.x, frame_.y }, zoomFactor_);
            ZoomRenderer zr(ctx.r, map);
            const MeasureZoomScope measure(zoomFactor_);
            const gfx::Rect clip = map.toInner(outer).intersect(bounds_);
            PaintContext local{ zr, ctx.theme, clip, ctx.time, ctx.overlays };
            ctx.r.pushClip(outer);
            onPaint(local);
            for (auto& c : children_) c->render(local);
            if (!requestsOverlayPass()) onPaintOverlay(local);
            ctx.r.popClip();
            if (ctx.overlays && requestsOverlayPass()) ctx.overlays->push_back(this);
            dirtyPaint_ = false;
            return;
        }

        if (islandNow()) {
            // 1.12.31 : UN ILOT se dessine avec le renderer de la surface (sous les
            // ZoomRenderer de ses parents), dans son repere - celui de la surface.
            gfx::IRenderer& base = unzoomed(ctx.r);
            const auto a = fromParentSpace({ ctx.clip.x, ctx.clip.y });
            const auto b = fromParentSpace({ ctx.clip.right(), ctx.clip.bottom() });
            const float x0 = std::floor(a.x), y0 = std::floor(a.y);
            const gfx::Rect outer{ x0, y0, std::max(0.f, std::ceil(b.x) - x0), std::max(0.f, std::ceil(b.y) - y0) };
            const gfx::Rect clip = outer.intersect(bounds_);
            if (clip.empty()) return;
            const MeasureZoomScope measure(1.f, true);
            PaintContext local{ base, ctx.theme, clip, ctx.time, ctx.overlays };
            base.pushClip(clip);
            onPaint(local);
            for (auto& c : children_) c->render(local);
            if (!requestsOverlayPass()) onPaintOverlay(local);
            base.popClip();
            if (ctx.overlays && requestsOverlayPass()) ctx.overlays->push_back(this);
            dirtyPaint_ = false;
            return;
        }

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
        // 1.12.30 : sous un widget zoome (une liste ouverte dans un tableau zoome),
        // a la meme echelle que lui.
        if (const auto m = ownToSurface(*this); !m.identity()) {
            ZoomRenderer zr(ctx.r, m);
            const MeasureZoomScope measure(m.s);
            const gfx::Rect clip = m.toInner(ctx.clip);
            ctx.r.pushClip(ctx.clip);
            onPaintOverlay(PaintContext{ zr, ctx.theme, clip, ctx.time, ctx.overlays });
            ctx.r.popClip();
            return;
        }
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
            if (const auto* m = std::get_if<TextDropped>(&ev)) return &m->pos;   // 1.12.27
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
        // p : dans le repere du parent (1.12.30 : celui d'un widget zoome est agrandi).
        if (!visible() || !frame_.contains(p)) return nullptr;
        const gfx::Point q = fromParentSpace(p);
        for (auto it = children_.rbegin(); it != children_.rend(); ++it)
            if (const Widget* found = (*it)->tooltipAt(q)) return found;
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
        if (scaledNow()) {
            // 1.12.30 : l'evenement passe dans le repere agrandi du widget.
            const bool inside = !p || outerEventBounds().contains(*p);
            const InputEvent local = p ? remapPointerImpl(ev, [this](gfx::Point q) { return fromParentSpace(q); }, 1.f / zoomFactor_) : ev;
            const MeasureZoomScope measure(zoomFactor_);
            const auto surface = ui::surfaceSize();
            const auto inner = fromParentSpace({ surface.w, surface.h });
            const SurfaceScope scope(surface.w > 0.f ? gfx::Size{ inner.x, inner.y } : surface, true);
            return dispatchOwn(local, pointerPosition(local), inside);
        }
        if (islandNow()) {
            // 1.12.31 : un ilot - l'evenement revient au repere de la surface.
            const bool inside = !p || outerEventBounds().contains(*p);
            const InputEvent local = p ? remapPointerImpl(ev, [this](gfx::Point q) { return fromParentSpace(q); }, islandS_) : ev;
            const MeasureZoomScope measure(1.f, true);
            const SurfaceScope scope(windowSurfaceSize(), true);
            return dispatchOwn(local, pointerPosition(local), inside);
        }
        return dispatchOwn(ev, p, !p || eventBounds().contains(*p));
    }

    EventResult Widget::dispatchOwn(const InputEvent& ev, const gfx::Point* p, bool inside) {
        if (p) {
            // Hover tracking happens even for events the widget will not consume,
            // otherwise a widget stays highlighted after the cursor has left it.
            setHovered(inside && hitTest(toLocal(*p)));
            if (!inside) {
                for (auto& c : children_) c->dispatch(ev);   // let children un-hover
                return EventResult::Ignored;
            }
        }

        // 1.12.30 : Ctrl + molette sur un widget qui se zoome par la base (hors d'un
        // widget qui zoome lui-meme : un code, un canevas) - un cran, demande a l'hote.
        if (zoomSupport_ == ZoomSupport::Scaled && p)
            if (const auto* w = std::get_if<MouseWheel>(&ev);
                w && w->mods.ctrl && !w->mods.alt && !w->mods.shift && w->dy != 0.f && !nativeZoomAt(*p)) {
                requestZoomStep(*this, w->dy > 0.f ? 1 : -1);
                return EventResult::Consumed;
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