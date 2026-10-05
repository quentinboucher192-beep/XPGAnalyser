#include "RackView.hpp"

#include "../ui/Icons.hpp"

#include <algorithm>
#include <cstdio>

namespace app {

using namespace ui;
using namespace domain;

namespace {

// One module card at zoom 1. Chosen so a 12-slot rack fits a 1500 px window
// without scrolling, and so a 64-point module still has room for two lines of
// text under its reference.
constexpr float kSlotW     = 104.f;
constexpr float kSlotH     = 148.f;
constexpr float kSlotGap   = 6.f;
constexpr float kRackPadX  = 14.f;
constexpr float kRackPadTop = 34.f;
constexpr float kRackPadBottom = 14.f;
constexpr float kRackGap   = 22.f;

std::string shortKind(ModuleKind k) {
    switch (k) {
        case ModuleKind::Cpu:            return "CPU";
        case ModuleKind::PowerSupply:    return "PSU";
        case ModuleKind::DiscreteInput:  return "DI";
        case ModuleKind::DiscreteOutput: return "DO";
        case ModuleKind::DiscreteMixed:  return "DI/DO";
        case ModuleKind::AnalogInput:    return "AI";
        case ModuleKind::AnalogOutput:   return "AO";
        case ModuleKind::Communication:  return "COM";
        case ModuleKind::Counting:       return "CNT";
        case ModuleKind::Motion:         return "MOT";
        default:                         return "MOD";
    }
}

Icon iconFor(ModuleKind k) {
    switch (k) {
        case ModuleKind::Cpu:           return Icon::Cpu;
        case ModuleKind::PowerSupply:   return Icon::Play;
        case ModuleKind::Communication: return Icon::Library;
        case ModuleKind::AnalogInput:
        case ModuleKind::AnalogOutput:  return Icon::Chart;
        default:                        return Icon::Module;
    }
}

} // namespace

RackView::RackView(std::string id) : Widget(std::move(id)) {
    setFocusPolicy(true);
    setPadding({10.f, 10.f, 10.f, 10.f});
}

void RackView::setHardware(std::shared_ptr<const Project> project) {
    project_ = std::move(project);
    scroll_ = {};
    selectedRack_ = -1;
    dirtyGeometry_ = true;
    invalidateLayout();
}

void RackView::setZoom(float zoom) {
    const float z = std::clamp(zoom, 0.5f, 2.5f);
    if (z == zoom_) return;
    zoom_ = z;
    dirtyGeometry_ = true;
    invalidateLayout();
}

SizeHint RackView::sizeHint() const {
    SizeHint h;
    h.preferred = {600.f, 300.f};
    h.minimum   = {240.f, 160.f};
    h.stretchX = h.stretchY = 1.f;
    return h;
}

void RackView::onLayout() { dirtyGeometry_ = true; }

void RackView::rebuild(const PaintContext& ctx) {
    slots_.clear();
    content_ = {};
    if (!project_) return;

    const auto  area = contentRect();
    const float slotW = kSlotW * zoom_;
    const float slotH = kSlotH * zoom_;
    const float gap   = kSlotGap * zoom_;
    const float padX  = kRackPadX * zoom_;
    const float padTop = kRackPadTop * zoom_;
    const float padBottom = kRackPadBottom * zoom_;

    float y = area.y - scroll_.y;
    float widest = 0.f;

    for (const auto& rack : project_->hardware.racks) {
        // A rack is as wide as its declared slot count, so an eight-slot
        // backplane with five modules still reads as an eight-slot backplane.
        const auto declared = rack.slotCount ? rack.slotCount : static_cast<std::uint16_t>(
            rack.modules.empty() ? 0 : rack.modules.back().slot + 1);

        const bool hasSupply = !rack.modules.empty()
                            && rack.modules.front().kind == ModuleKind::PowerSupply;
        const auto cells = static_cast<std::size_t>(declared) + (hasSupply ? 1u : 0u);

        float x = area.x - scroll_.x + padX;
        const float rackWidth = padX * 2 + static_cast<float>(cells) * (slotW + gap) - gap;
        widest = std::max(widest, rackWidth);

        if (hasSupply) {
            slots_.push_back(SlotBox{{x, y + padTop, slotW, slotH}, rack.number, -1,
                                     &rack.modules.front()});
            x += slotW + gap;
        }
        for (std::uint16_t slot = 0; slot < declared; ++slot) {
            const Module* found = nullptr;
            for (const auto& m : rack.modules)
                if (m.slot == static_cast<std::int16_t>(slot)) { found = &m; break; }
            slots_.push_back(SlotBox{{x, y + padTop, slotW, slotH}, rack.number,
                                     static_cast<std::int16_t>(slot), found});
            x += slotW + gap;
        }
        y += padTop + slotH + padBottom + kRackGap * zoom_;
    }

    content_ = {widest, y - (area.y - scroll_.y)};
    dirtyGeometry_ = false;
    (void)ctx;
}

gfx::Color RackView::colorFor(ModuleKind k, const Palette& c) const {
    switch (k) {
        case ModuleKind::Cpu:            return c.accent;
        case ModuleKind::PowerSupply:    return c.warning;
        case ModuleKind::DiscreteInput:
        case ModuleKind::AnalogInput:    return c.info;
        case ModuleKind::DiscreteOutput:
        case ModuleKind::AnalogOutput:   return c.ok;
        case ModuleKind::DiscreteMixed:  return gfx::Color::rgb(0xB58BD6);
        case ModuleKind::Communication:  return gfx::Color::rgb(0xD6A25C);
        default:                         return c.textMuted;
    }
}

void RackView::drawEmptySlot(const PaintContext& ctx, const SlotBox& s) const {
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(s.rect, c.windowBg);
    ctx.r.strokeRect(s.rect, c.border, 1.f);

    // A dashed feel without a dash primitive: short ticks down the middle.
    const float midX = s.rect.x + s.rect.w * 0.5f;
    for (float ty = s.rect.y + 12.f; ty < s.rect.bottom() - 12.f; ty += 10.f * zoom_)
        ctx.r.line({midX, ty}, {midX, ty + 4.f * zoom_}, c.border, 1.f);

    const auto label = std::to_string(s.slot);
    const auto m = ctx.r.measure(label, ctx.theme.font.smallUi);
    ctx.r.drawText({s.rect.x + (s.rect.w - m.width) * 0.5f, s.rect.bottom() - 18.f * zoom_},
                   label, ctx.theme.font.smallUi, c.textDisabled);
}

void RackView::drawModule(const PaintContext& ctx, const SlotBox& s) const {
    const auto& c = ctx.theme.color;
    const auto& m = *s.module;
    const auto accent = colorFor(m.kind, c);
    const bool selected = s.rack == selectedRack_ && s.slot == selectedSlot_;

    ctx.r.fillRect(s.rect, selected ? c.selectionBg : c.panelBg);
    ctx.r.strokeRect(s.rect, selected ? c.accent : c.borderStrong, selected ? 2.f : 1.f);
    // Coloured spine: the fastest way to read a rack is by colour, not by text.
    ctx.r.fillRect({s.rect.x, s.rect.y, 4.f * zoom_, s.rect.h}, accent);

    const float pad = 8.f * zoom_;
    float ty = s.rect.y + pad;
    const auto small = ctx.theme.font.smallUi;
    const auto body  = ctx.theme.font.ui;

    // Header strip: slot number and short kind.
    const auto slotLabel = (s.slot < 0) ? std::string("PS") : std::to_string(s.slot);
    ctx.r.fillRect({s.rect.x + 4.f * zoom_, s.rect.y, s.rect.w - 4.f * zoom_, 20.f * zoom_},
                   c.headerBg);
    ctx.r.drawText({s.rect.x + pad, ty}, slotLabel, small, c.text);
    {
        const auto k = shortKind(m.kind);
        const auto km = ctx.r.measure(k, small);
        ctx.r.drawText({s.rect.right() - pad - km.width, ty}, k, small, accent);
    }
    ty += 24.f * zoom_;

    // Icon.
    const float side = std::min(26.f * zoom_, s.rect.w - 2 * pad);
    drawIcon(ctx.r, iconFor(m.kind),
             {s.rect.x + (s.rect.w - side) * 0.5f, ty, side, side}, accent);
    ty += side + 6.f * zoom_;

    // Reference, wrapped over two lines when it does not fit.
    {
        const float avail = s.rect.w - 2 * pad;
        const auto fits = ctx.r.fitCharacters(m.reference, small, avail);
        if (fits >= m.reference.size()) {
            const auto mm = ctx.r.measure(m.reference, small);
            ctx.r.drawText({s.rect.x + (s.rect.w - mm.width) * 0.5f, ty}, m.reference, small, c.text);
            ty += ctx.r.lineHeight(small) + 2.f;
        } else {
            const auto head = std::string_view(m.reference).substr(0, fits);
            const auto tail = std::string_view(m.reference).substr(fits);
            ctx.r.drawText({s.rect.x + pad, ty}, head, small, c.text);
            ty += ctx.r.lineHeight(small);
            ctx.r.drawText({s.rect.x + pad, ty}, tail, small, c.text);
            ty += ctx.r.lineHeight(small) + 2.f;
        }
    }

    // Firmware.
    if (!m.firmware.empty()) {
        const auto v = "v" + m.firmware;
        const auto vm = ctx.r.measure(v, small);
        ctx.r.drawText({s.rect.x + (s.rect.w - vm.width) * 0.5f, ty}, v, small, c.textMuted);
        ty += ctx.r.lineHeight(small) + 4.f;
    }

    // Lot API 4 : les voies employees par le programme, au-dessus des points.
    for (const auto& u : usage_) {
        if (u.rack != s.rack || u.slot != s.slot || u.total == 0) continue;
        const std::string text = std::to_string(u.used) + "/" + std::to_string(u.total);
        const auto tm = ctx.r.measure(text, small);
        const float y = s.rect.bottom() - (m.points() > 0 ? 42.f : 22.f) * zoom_;
        ctx.r.drawText({s.rect.right() - pad - tm.width, y}, text, small, u.faulty ? c.warning : (u.used ? c.text : c.textMuted));
    }

    // Points. The "?" marks a figure the file could not settle - it came from
    // the catalog, and that is a weaker claim than a measured one.
    if (m.points() > 0) {
        char buf[64];
        if (m.inputPoints && m.outputPoints)
            std::snprintf(buf, sizeof buf, "%u I / %u Q%s", m.inputPoints, m.outputPoints,
                          m.pointsFromCatalog ? " ?" : "");
        else if (m.inputPoints)
            std::snprintf(buf, sizeof buf, "%u inputs%s", m.inputPoints,
                          m.pointsFromCatalog ? " ?" : "");
        else
            std::snprintf(buf, sizeof buf, "%u outputs%s", m.outputPoints,
                          m.pointsFromCatalog ? " ?" : "");

        const gfx::Rect chip{s.rect.x + pad, s.rect.bottom() - 22.f * zoom_,
                             s.rect.w - 2 * pad, 16.f * zoom_};
        ctx.r.fillRoundedRect(chip, accent.withAlpha(48), 3.f);
        const auto bm = ctx.r.measure(buf, small);
        ctx.r.drawText({chip.x + (chip.w - bm.width) * 0.5f,
                        chip.y + (chip.h - bm.height) * 0.5f}, buf, small, accent);
    } else if (!m.channels.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%zu channel%s", m.channels.size(),
                      m.channels.size() == 1 ? "" : "s");
        const auto bm = ctx.r.measure(buf, small);
        ctx.r.drawText({s.rect.x + (s.rect.w - bm.width) * 0.5f, s.rect.bottom() - 20.f * zoom_},
                       buf, small, c.textMuted);
    }
    (void)body;
}

void RackView::onPaint(const PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(bounds(), c.windowBg);
    ctx.r.strokeRect(bounds(), c.border, 1.f);

    if (!project_ || project_->hardware.racks.empty()) {
        const std::string msg = project_ && project_->hardware.inferred
            ? "Pas de configuration mat\xC3\xA9rielle dans cet export : importer le .XHW (ou le .XEF) de Control Expert."
            : "Aucun projet ouvert.";
        const auto m = ctx.r.measure(msg, ctx.theme.font.ui);
        ctx.r.drawText({bounds().x + (bounds().w - m.width) * 0.5f,
                        bounds().y + bounds().h * 0.5f}, msg, ctx.theme.font.ui, c.textMuted);
        return;
    }

    if (dirtyGeometry_) const_cast<RackView*>(this)->rebuild(ctx);

    const auto area = contentRect();
    const float slotH = kSlotH * zoom_;
    const float padTop = kRackPadTop * zoom_;
    const float padX = kRackPadX * zoom_;

    // Rack chassis behind the slots.
    std::size_t index = 0;
    for (const auto& rack : project_->hardware.racks) {
        if (index >= slots_.size()) break;
        const auto first = slots_[index];
        std::size_t count = 0;
        while (index + count < slots_.size() && slots_[index + count].rack == rack.number) ++count;
        if (count == 0) break;
        const auto last = slots_[index + count - 1];

        const gfx::Rect chassis{first.rect.x - padX, first.rect.y - padTop,
                                (last.rect.right() + padX) - (first.rect.x - padX),
                                padTop + slotH + kRackPadBottom * zoom_};
        ctx.r.fillRect(chassis, ctx.theme.color.panelBg);
        ctx.r.strokeRect(chassis, ctx.theme.color.borderStrong, 1.f);

        char title[160];
        std::snprintf(title, sizeof title, "Rack %u   %s   %u slots",
                      rack.number, rack.reference.c_str(),
                      rack.slotCount ? rack.slotCount : static_cast<std::uint16_t>(count));
        ctx.r.drawText({chassis.x + 10.f, chassis.y + 7.f * zoom_}, title,
                       ctx.theme.font.uiBold, ctx.theme.color.text);

        index += count;
    }

    for (const auto& s : slots_) {
        if (s.rect.bottom() < area.y || s.rect.y > area.bottom()) continue;   // off screen
        if (s.module) drawModule(ctx, s); else drawEmptySlot(ctx, s);
    }

    // Scroll hint, drawn only when there is something below the fold.
    // 1.11.4 : la barre se tire.
    sbar_.paint(ctx, {area.x, area.y, bounds().right() - area.x, area.h}, content_.h, area.h, scroll_.y);
}

EventResult RackView::onEvent(const InputEvent& ev) {
    {
        float off = scroll_.y;   // 1.11.4 : la barre de defilement se tire
        if (sbar_.handle(*this, ev, off)) {
            scroll_.y = std::clamp(off, 0.f, std::max(0.f, content_.h - contentRect().h));
            dirtyGeometry_ = true;
            invalidate();
            return EventResult::Consumed;
        }
    }
    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        if (!bounds().contains(w->pos)) return EventResult::Ignored;
        if (w->mods.ctrl) {
            setZoom(zoom_ + (w->dy > 0 ? 0.1f : -0.1f));   // Ctrl+wheel zooms
            return EventResult::Consumed;
        }
        const auto area = contentRect();
        const float maxY = std::max(0.f, content_.h - area.h);
        const float maxX = std::max(0.f, content_.w - area.w);
        scroll_.y = std::clamp(scroll_.y - w->dy * 48.f, 0.f, maxY);
        if (w->mods.shift) scroll_.x = std::clamp(scroll_.x - w->dy * 48.f, 0.f, maxX);
        dirtyGeometry_ = true;
        invalidate();
        return EventResult::Consumed;
    }

    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (!bounds().contains(d->pos)) return EventResult::Ignored;
        grabFocus();
        for (const auto& s : slots_)
            if (s.rect.contains(d->pos)) {
                selectedRack_ = s.rack;
                selectedSlot_ = s.slot;
                invalidate();
                if (s.module) moduleSelected->emit(s.rack, s.slot);
                return EventResult::Consumed;
            }
        return EventResult::Consumed;
    }

    if (const auto* k = std::get_if<KeyDown>(&ev); k && focused()) {
        if (k->mods.ctrl && k->key == Key::Space) { setZoom(1.f); return EventResult::Consumed; }
        if (k->key == Key::Down)  { scroll_.y += 40.f; dirtyGeometry_ = true; invalidate(); return EventResult::Consumed; }
        if (k->key == Key::Up)    { scroll_.y = std::max(0.f, scroll_.y - 40.f); dirtyGeometry_ = true; invalidate(); return EventResult::Consumed; }
    }
    return EventResult::Ignored;
}

// ------------------------------------------------------- lot API 4 ----
void RackView::setUsage(std::vector<Usage> usage) {
    usage_ = std::move(usage);
    invalidate();
}

void RackView::select(std::uint16_t rack, std::int16_t slot) {
    selectedRack_ = rack;
    selectedSlot_ = slot;
    invalidate();
}

bool RackView::slotRect(std::uint16_t rack, std::int16_t slot, gfx::Rect& out) const {
    for (const auto& s : slots_)
        if (s.rack == rack && s.slot == slot) {
            out = s.rect;
            return true;
        }
    return false;
}

} // namespace app
