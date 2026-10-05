#include "Layout.hpp"

#include <algorithm>
#include <numeric>

namespace ui {

    // ------------------------------------------------------------- BoxLayout ----
    BoxLayout::BoxLayout(Orientation o, std::string id)
        : Widget(std::move(id)), orientation_(o) {}

    SizeHint BoxLayout::sizeHint() const {
        SizeHint h;
        const bool horizontal = orientation_ == Orientation::Horizontal;
        int counted = 0;
        for (const auto& c : children()) {
            if (c->visibility() == Visibility::Collapsed) continue;
            const auto ch = c->sizeHint();
            if (horizontal) {
                h.preferred.w += ch.preferred.w;
                h.minimum.w += ch.minimum.w;
                h.preferred.h = std::max(h.preferred.h, ch.preferred.h);
                h.minimum.h = std::max(h.minimum.h, ch.minimum.h);
            }
            else {
                h.preferred.h += ch.preferred.h;
                h.minimum.h += ch.minimum.h;
                h.preferred.w = std::max(h.preferred.w, ch.preferred.w);
                h.minimum.w = std::max(h.minimum.w, ch.minimum.w);
            }
            h.stretchX = std::max(h.stretchX, ch.stretchX);
            h.stretchY = std::max(h.stretchY, ch.stretchY);
            ++counted;
        }
        const float gaps = spacing_ * static_cast<float>(std::max(0, counted - 1));
        (horizontal ? h.preferred.w : h.preferred.h) += gaps;
        (horizontal ? h.minimum.w : h.minimum.h) += gaps;
        return h;
    }

    void BoxLayout::onLayout() {
        const auto area = contentRect();
        const bool horizontal = orientation_ == Orientation::Horizontal;

        std::vector<Widget*> items;
        for (const auto& c : children())
            if (c->visibility() != Visibility::Collapsed) items.push_back(c.get());
        if (items.empty()) return;

        const float extent = horizontal ? area.w : area.h;
        const float gaps = spacing_ * static_cast<float>(items.size() - 1);

        // Pass 1: give everyone its preferred size. Pass 2: distribute what is left
        // among the stretchable ones, proportionally to their stretch factor.
        std::vector<float> sizes;
        float used = 0.f, stretchTotal = 0.f;
        sizes.reserve(items.size());
        for (auto* w : items) {
            const auto h = w->sizeHint();
            const float pref = horizontal ? h.preferred.w : h.preferred.h;
            sizes.push_back(pref);
            used += pref;
            stretchTotal += horizontal ? h.stretchX : h.stretchY;
        }

        const float leftover = extent - gaps - used;

        if (leftover >= 0.f) {
            // Room to spare: hand it to whoever asked to stretch.
            if (stretchTotal > 0.f && leftover > 0.f)
                for (std::size_t i = 0; i < items.size(); ++i) {
                    const auto h = items[i]->sizeHint();
                    const float s = horizontal ? h.stretchX : h.stretchY;
                    if (s > 0.f) sizes[i] = std::max(0.f, sizes[i] + leftover * (s / stretchTotal));
                }
        }
        else {
            // NOT ENOUGH ROOM. This used to hand every child its preferred size
            // anyway and let the row run past the edge, where the clip rectangle
            // silently swallowed it. A control that exists, is laid out, and cannot
            // be seen or clicked is worse than one that is missing: nothing about
            // the screen says it is there.
            //
            // Shrink instead, proportionally to how much each child can give up
            // before hitting its own minimum.
            float deficit = -leftover;
            float shrinkable = 0.f;
            std::vector<float> room(items.size(), 0.f);
            for (std::size_t i = 0; i < items.size(); ++i) {
                const auto h = items[i]->sizeHint();
                const float minimum = horizontal ? h.minimum.w : h.minimum.h;
                room[i] = std::max(0.f, sizes[i] - minimum);
                shrinkable += room[i];
            }
            if (shrinkable > 0.f) {
                const float take = std::min(deficit, shrinkable);
                for (std::size_t i = 0; i < items.size(); ++i)
                    sizes[i] -= take * (room[i] / shrinkable);
                deficit -= take;
            }
            if (deficit > 0.f) {
                // Even the minimums do not fit. Scale everything down together
                // rather than letting the tail fall off the end.
                float total = 0.f;
                for (float v : sizes) total += v;
                const float scale = total > 0.f ? std::max(0.f, (extent - gaps) / total) : 0.f;
                for (auto& v : sizes) v *= scale;
            }
        }

        float cursor = horizontal ? area.x : area.y;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto h = items[i]->sizeHint();
            const float cross = horizontal ? area.h : area.w;
            const float wanted = horizontal ? h.preferred.h : h.preferred.w;
            float crossSize = (align_ == Align::Stretch) ? cross : std::min(cross, wanted);
            float crossPos = horizontal ? area.y : area.x;
            if (align_ == Align::Center) crossPos += (cross - crossSize) * 0.5f;
            else if (align_ == Align::End) crossPos += cross - crossSize;

            if (horizontal) items[i]->setBounds({ cursor, crossPos, sizes[i], crossSize });
            else            items[i]->setBounds({ crossPos, cursor, crossSize, sizes[i] });
            cursor += sizes[i] + spacing_;
        }
    }

    // ------------------------------------------------------------ DockLayout ----
    DockLayout::DockLayout(std::string id) : Widget(std::move(id)) {}

    Widget& DockLayout::dock(WidgetPtr w, Side side, float extent) {
        Widget& ref = addChild(std::move(w));
        items_.push_back(Item{ &ref, side, extent });
        invalidateLayout();
        return ref;
    }

    void DockLayout::setExtent(Widget& w, float extent) {
        for (auto& i : items_)
            if (i.w == &w) { i.extent = extent; invalidateLayout(); return; }
    }

    void DockLayout::onLayout() {
        gfx::Rect area = contentRect();

        // Edges are consumed in registration order, so a toolbar docked before a
        // rail spans the full width and the rail starts below it — which is the
        // arrangement in the reference layout.
        for (const auto& i : items_) {
            if (i.w->visibility() == Visibility::Collapsed) continue;
            switch (i.side) {
            case Side::Top:
                i.w->setBounds({ area.x, area.y, area.w, i.extent });
                area.y += i.extent; area.h -= i.extent; break;
            case Side::Bottom:
                i.w->setBounds({ area.x, area.bottom() - i.extent, area.w, i.extent });
                area.h -= i.extent; break;
            case Side::Left:
                i.w->setBounds({ area.x, area.y, i.extent, area.h });
                area.x += i.extent; area.w -= i.extent; break;
            case Side::Right:
                i.w->setBounds({ area.right() - i.extent, area.y, i.extent, area.h });
                area.w -= i.extent; break;
            case Side::Center:
                break;
            }
            area.w = std::max(0.f, area.w);
            area.h = std::max(0.f, area.h);
        }
        for (const auto& i : items_)
            if (i.side == Side::Center && i.w->visibility() != Visibility::Collapsed)
                i.w->setBounds(area);
    }

    // ------------------------------------------------------------ GridLayout ----
    GridLayout::GridLayout(int columns, std::string id)
        : Widget(std::move(id)), columns_(std::max(1, columns)) {
        colStretch_.assign(static_cast<std::size_t>(columns_), 1.f);
    }

    Widget& GridLayout::place(WidgetPtr w, int row, int col, int rowSpan, int colSpan) {
        Widget& ref = addChild(std::move(w));
        cells_.push_back(Cell{ &ref, row, col, std::max(1, rowSpan), std::max(1, colSpan) });
        if (static_cast<std::size_t>(row + rowSpan) > rowStretch_.size())
            rowStretch_.resize(static_cast<std::size_t>(row + rowSpan), 1.f);
        invalidateLayout();
        return ref;
    }

    void GridLayout::setColumnStretch(int col, float s) {
        if (col >= 0 && static_cast<std::size_t>(col) < colStretch_.size()) {
            colStretch_[static_cast<std::size_t>(col)] = s;
            invalidateLayout();
        }
    }

    void GridLayout::setRowStretch(int row, float s) {
        if (row < 0) return;
        if (static_cast<std::size_t>(row) >= rowStretch_.size())
            rowStretch_.resize(static_cast<std::size_t>(row) + 1, 1.f);
        rowStretch_[static_cast<std::size_t>(row)] = s;
        invalidateLayout();
    }

    SizeHint GridLayout::sizeHint() const {
        SizeHint h;
        for (const auto& c : cells_) {
            const auto ch = c.w->sizeHint();
            h.preferred.w = std::max(h.preferred.w, ch.preferred.w * static_cast<float>(columns_));
            h.preferred.h += ch.preferred.h / static_cast<float>(std::max<std::size_t>(1, rowStretch_.size()));
        }
        h.stretchX = h.stretchY = 1.f;
        return h;
    }

    void GridLayout::onLayout() {
        const auto area = contentRect();
        const auto rows = std::max<std::size_t>(1, rowStretch_.size());
        const float spacing = 6.f;

        const float colTotal = std::accumulate(colStretch_.begin(), colStretch_.end(), 0.f);
        const float rowTotal = std::accumulate(rowStretch_.begin(), rowStretch_.end(), 0.f);
        if (colTotal <= 0.f || rowTotal <= 0.f) return;

        const float usableW = area.w - spacing * static_cast<float>(columns_ - 1);
        const float usableH = area.h - spacing * static_cast<float>(rows - 1);

        // Column and row offsets, precomputed so spans are a subtraction.
        std::vector<float> colX(colStretch_.size() + 1, area.x);
        for (std::size_t i = 0; i < colStretch_.size(); ++i)
            colX[i + 1] = colX[i] + usableW * (colStretch_[i] / colTotal) + spacing;
        std::vector<float> rowY(rows + 1, area.y);
        for (std::size_t i = 0; i < rows; ++i)
            rowY[i + 1] = rowY[i] + usableH * (rowStretch_[i] / rowTotal) + spacing;

        for (const auto& c : cells_) {
            const auto c0 = static_cast<std::size_t>(std::clamp(c.col, 0, columns_ - 1));
            const auto c1 = std::min(colX.size() - 1, c0 + static_cast<std::size_t>(c.colSpan));
            const auto r0 = std::min(rows - 1, static_cast<std::size_t>(std::max(0, c.row)));
            const auto r1 = std::min(rowY.size() - 1, r0 + static_cast<std::size_t>(c.rowSpan));
            c.w->setBounds({ colX[c0], rowY[r0],
                            std::max(0.f, colX[c1] - colX[c0] - spacing),
                            std::max(0.f, rowY[r1] - rowY[r0] - spacing) });
        }
    }


    // ============================================================ OverlayHost ====
    OverlayHost::OverlayHost(std::string id) : Widget(std::move(id)) {}

    Widget& OverlayHost::setContent(WidgetPtr w) {
        Widget& ref = addChild(std::move(w));
        content_ = &ref;
        invalidateLayout();
        return ref;
    }

    Widget& OverlayHost::addOverlay(WidgetPtr w) { return addChild(std::move(w)); }

    SizeHint OverlayHost::sizeHint() const {
        // The host is as big as whatever it wraps; the floating children have no
        // say, which is the point.
        return content_ ? content_->sizeHint() : Widget::sizeHint();
    }

    void OverlayHost::onLayout() {
        const auto area = contentRect();
        if (content_) content_->setBounds(area);

        // AND THE FLOATING CHILDREN GET THE WHOLE AREA TOO.
        //
        // The first version left them alone entirely - "it positions itself" - and
        // that was wrong in a way that produced the worst possible symptom. A
        // floating child positions its *drawing* itself, but it is still a Widget,
        // and Widget::render clips to bounds_ and returns early when the result is
        // empty:
        //
        //     const gfx::Rect clip = ctx.clip.intersect(bounds_);
        //     if (clip.empty()) return;
        //
        // With bounds_ at {0,0,0,0} the menu was never rendered, so it never
        // reached the `ctx.overlays->push_back(this)` at the end of render(), so
        // the top-most pass never painted it either. Meanwhile it was still
        // *visible()* and still claimed the pointer through eventBounds(), so it
        // opened, swallowed every click, and could not be seen. The second click
        // landed on whatever entry happened to be under it.
        //
        // An invisible widget that eats input is worse than a missing one: nothing
        // on screen says it is there.
        for (const auto& c : children())
            if (c.get() != content_) c->setBounds(area);
    }


} // namespace ui