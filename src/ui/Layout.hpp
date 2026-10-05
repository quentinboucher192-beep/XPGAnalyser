// =============================================================================
//  ui/Layout.hpp — layout containers
// -----------------------------------------------------------------------------
//  Two passes, Qt-style: measure (bottom-up SizeHint) then arrange (top-down
//  setBounds). Layouts are widgets themselves, so they nest without a separate
//  layout-object hierarchy.
// =============================================================================
#pragma once

#include "Widget.hpp"

namespace ui {

enum class Orientation : std::uint8_t { Horizontal, Vertical };
enum class Align : std::uint8_t { Start, Center, End, Stretch };

class BoxLayout : public Widget {
public:
    explicit BoxLayout(Orientation o, std::string id = {});
    void setSpacing(float s) { spacing_ = s; invalidateLayout(); }
    void setAlignment(Align a) { align_ = a; invalidateLayout(); }
    [[nodiscard]] SizeHint sizeHint() const override;
protected:
    void onLayout() override;
private:
    Orientation orientation_;
    float       spacing_{4.f};
    Align       align_{Align::Stretch};
};

// Dock layout used by the main window: four edges + centre fill.
class DockLayout : public Widget {
public:
    enum class Side : std::uint8_t { Left, Right, Top, Bottom, Center };
    explicit DockLayout(std::string id = {});
    Widget& dock(WidgetPtr w, Side side, float extent);
    void    setExtent(Widget& w, float extent);
protected:
    void onLayout() override;
private:
    struct Item { Widget* w; Side side; float extent; };
    std::vector<Item> items_;
};

class GridLayout : public Widget {
public:
    GridLayout(int columns, std::string id = {});
    Widget& place(WidgetPtr w, int row, int col, int rowSpan = 1, int colSpan = 1);
    void    setColumnStretch(int col, float s);
    void    setRowStretch(int row, float s);
    [[nodiscard]] SizeHint sizeHint() const override;
protected:
    void onLayout() override;
private:
    struct Cell { Widget* w; int row, col, rowSpan, colSpan; };
    int                 columns_;
    std::vector<Cell>   cells_;
    std::vector<float>  colStretch_, rowStretch_;
};

// ---------------------------------------------------------------------------
//  Somewhere to put the things that float above the interface: a context menu,
//  and later a drag ghost or an inline editor.
//
//  A BoxLayout is the wrong home for them. It hands every non-collapsed child a
//  slot and a spacing gap, so a zero-sized popup still shifts the whole row by
//  a few pixels; and a *collapsed* child is never rendered at all, so it never
//  gets to ask for the top-most paint pass. Neither state is what a floating
//  widget wants, because a floating widget does not want to be laid out - it
//  wants to be painted and to receive events.
//
//  OverlayHost gives its content child the whole area and leaves every other
//  child exactly where it put itself.
// ---------------------------------------------------------------------------
class OverlayHost : public Widget {
public:
    explicit OverlayHost(std::string id = {});

    Widget& setContent(WidgetPtr w);    // filled to the host, laid out normally
    Widget& addOverlay(WidgetPtr w);    // positions itself; added last, so it is
                                        // hit-tested before the content
    [[nodiscard]] SizeHint sizeHint() const override;

protected:
    void onLayout() override;

private:
    Widget* content_{nullptr};
};

} // namespace ui
