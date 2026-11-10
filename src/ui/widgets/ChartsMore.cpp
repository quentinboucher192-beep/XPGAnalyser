// =============================================================================
//  ui/widgets/ChartsMore.cpp - 1.47.0 : les graphiques de l'Admin (2/2) - les
//  barres horizontales (BarList), la carte jour x heure (HeatMap), les jours
//  (DayStrip), avant -> maintenant (Dumbbell), la frise des licences
//  (Timeline), la sparkline, les petits graphiques, la carte d'un graphique.
//  La palette, les formats, la base : Charts.cpp.
// =============================================================================
#include "Charts.hpp"

#include "../Icons.hpp"
#include "../Shapes.hpp"

#include <algorithm>
#include <cmath>

namespace ui::charts {

using detail::fit;
using detail::text;
using detail::tw;
using gfx::FontId;
using gfx::Point;
using gfx::Rect;

namespace {
Color keyColor(const ColorKeys* own, std::string_view key) { return own ? own->colorOf(key) : ColorKeys::standard().colorOf(key); }

// La fenetre (la limite d'une bulle) - inutile ici : ChartBase s'en charge.
std::string plural(double n, const std::string& unit) {
    if (unit.empty()) return fr::integer(n);
    return fr::integer(n) + " " + unit + (std::fabs(n) >= 2.0 ? "s" : "");
}
} // namespace

// =============================================================================
//  BarList
// =============================================================================
BarList::BarList(std::string id) : ChartBase(std::move(id)) {}

void BarList::setData(BarListData data) {
    data_ = std::move(data);
    if (active_ >= itemCount()) active_ = -1;
    invalidateLayout();
}

void BarList::setColorKeys(ColorKeys keys) {
    keys_ = std::move(keys);
    ownKeys_ = true;
    invalidate();
}

Color BarList::partColor(std::size_t part) const {
    if (part < data_.partColors.size() && data_.partColors[part].a) return data_.partColors[part];
    if (part < data_.partKeys.size()) return keyColor(ownKeys_ ? &keys_ : nullptr, data_.partKeys[part]);
    return colors::admin(static_cast<int>(part));
}

Color BarList::rowColor(std::size_t row) const {
    if (row < data_.rows.size()) {
        const BarRow& r = data_.rows[row];
        if (r.color.a) return r.color;
        if (!r.key.empty()) return keyColor(ownKeys_ ? &keys_ : nullptr, r.key);
    }
    return data_.color.a ? data_.color : colors::kOne;
}

double BarList::rowTotal(const BarRow& r) const {
    if (data_.mode != BarMode::Share) return r.value;
    if (r.value > 0) return r.value;
    double s = 0;
    for (const double v : r.parts)
        if (std::isfinite(v)) s += v;
    return s;
}

namespace {
FontId barLabelFont(bool strong) { return strong ? gfx::semibold(FontId{12}) : FontId{12}; }
FontId barValueFont(bool strong) { return gfx::semibold(FontId{strong ? std::uint16_t{13} : std::uint16_t{12}}); }
FontId barExtraFont() { return FontId{11}; }
} // namespace

float BarList::Frame::xAt(double v) const noexcept {
    if (!(maxValue > 0.0)) return x0;
    return x0 + static_cast<float>(std::clamp(v / maxValue, 0.0, 1.0) * static_cast<double>(x1 - x0));
}

BarList::Frame BarList::frame() const {
    Frame f;
    const auto& d = data_;
    const Rect b = contentRect();
    const Format fmt = d.format ? d.format : Format([](double v) { return fr::integer(v); });
    // La legende des morceaux.
    std::vector<LegendItem> items;
    if ((d.mode == BarMode::Share || d.mode == BarMode::Grouped) && d.legend != LegendMode::None)
        for (std::size_t i = 0; i < d.partNames.size(); ++i) items.push_back({d.partNames[i], partColor(i), Swatch::Box, {}});
    f.legendHeight = items.size() >= (d.legend == LegendMode::Top ? 1u : 2u) ? legendHeight(items, b.w) + 8.f : 0.f;
    // Les colonnes : le libelle, les barres, la valeur, l'extra.
    float lw = d.labelWidth;
    float wantedLabel = 0.f;
    if (lw <= 0.f) {
        for (const auto& r : d.rows) lw = std::max({lw, tw(r.label, barLabelFont(r.strong)), tw(r.sub, FontId{11})});
        wantedLabel = lw;
        lw = std::min(lw, b.w * (d.mode == BarMode::Ranking || d.mode == BarMode::Track ? 0.55f : 0.45f));
    }
    float vw = d.valueWidth, ew = 0.f;
    if (d.mode != BarMode::Grouped || !d.valueLabels) {
        if (vw <= 0.f)
            for (const auto& r : d.rows) {
                const std::string v = r.off && r.value == 0.0 ? std::string("\xE2\x80\x94") : !r.valueText.empty() ? r.valueText : fmt(rowTotal(r));
                vw = std::max(vw, tw(v, barValueFont(r.strong)));
            }
        for (const auto& r : d.rows) ew = std::max(ew, tw(r.extra, barExtraFont()));
        if (ew > 0.f) ew += 2.f;   // un souffle : la mesure arrondie ne coupe pas la plus longue
    } else {
        vw = 0.f;
        // Grouped : la valeur au bout de chaque barre - de la place a droite pour la plus longue.
        for (const auto& r : d.rows)
            for (const double v : r.parts) vw = std::max(vw, tw(fmt(v), gfx::semibold(FontId{11})));
        vw += 6.f;
    }
    // Le libelle cede la place aux valeurs : il reste au moins 48 px de barre. Trop etroit pour lui (moins de 70 % de
    // ce qu'il voudrait) : un classement ou une piste passent le libelle au-dessus de la barre.
    const float cols = (vw > 0.f ? vw + 10.f : 0.f) + (ew > 0.f ? ew + 10.f : 0.f);
    if (d.labelWidth <= 0.f) {
        const float room = std::max(0.f, b.w - cols - 12.f - 48.f);
        // ... seulement si toutes les lignes tiennent ainsi (une ligne de 34 px chacune) : jamais une ligne perdue.
        const float stackedH = 34.f * static_cast<float>(d.rows.size());
        if ((d.mode == BarMode::Ranking || d.mode == BarMode::Track) && wantedLabel > 0.f && room < wantedLabel * 0.7f &&
            stackedH <= b.bottom() - b.y - f.legendHeight)
            f.stacked = true;
        lw = std::min(lw, room);
    }
    if (d.labelsAbove && (d.mode == BarMode::Ranking || d.mode == BarMode::Track)) f.stacked = true;
    if (f.stacked) {
        f.x0 = b.x;
        f.x1 = b.right();
    } else {
        f.x0 = b.x + (lw > 0.f ? lw + 12.f : 0.f);
        f.x1 = b.right() - cols;
        if (d.mode == BarMode::Grouped && d.valueLabels) f.x1 = b.right() - vw;
    }
    if (f.x1 < f.x0 + 20.f) f.x1 = f.x0 + 20.f;
    // L'echelle.
    double mx = d.maxValue;
    if (!(mx > 0.0)) {
        for (const auto& r : d.rows) {
            if (d.mode == BarMode::Grouped)
                for (const double v : r.parts) mx = std::max(mx, std::isfinite(v) ? v : 0.0);
            else mx = std::max(mx, rowTotal(r));
        }
        if (d.mode == BarMode::Grouped && d.axis) {
            const auto t = d.ticks.empty() ? niceTicks(0.0, mx, 3) : d.ticks;
            mx = std::max(mx, t.back());
        }
    }
    f.maxValue = mx > 0.0 ? mx : 1.0;
    // Les lignes.
    const float base = d.rowHeight > 0.f ? d.rowHeight : d.mode == BarMode::Grouped ? std::max(34.f, 8.f + static_cast<float>(std::max<std::size_t>(1, d.partKeys.size())) * 11.f) : 26.f;
    f.top = b.y + f.legendHeight;
    float wanted = 0.f;
    const auto rowWanted = [&](const BarRow& r) {
        if (f.stacked) return std::max(base, 34.f) + (r.strong ? 4.f : 0.f);
        return base + (r.strong ? 8.f : 0.f) + (!r.sub.empty() && d.mode != BarMode::Grouped ? 10.f : 0.f);
    };
    for (const auto& r : d.rows) wanted += rowWanted(r);
    // Moins de place que voulu (une petite fenetre) : les lignes se serrent un peu ; ce qui ne tient toujours pas
    // n'est pas dessine (onPaint).
    const float avail = b.bottom() - f.top - (d.mode == BarMode::Grouped && d.axis ? 20.f : 0.f);
    const float squeeze = wanted > avail && wanted > 0.f ? std::max(0.7f, avail / wanted) : 1.f;
    float y = f.top;
    for (const auto& r : d.rows) {
        const float h = rowWanted(r) * squeeze;
        f.rowTop.push_back(y);
        f.rowHeight.push_back(h);
        y += h;
    }
    return f;
}

float BarList::preferredHeight() const {
    const Frame f = frame();
    float h = f.legendHeight;
    for (const float r : f.rowHeight) h += r;
    if (data_.mode == BarMode::Grouped && data_.axis) h += 20.f;
    return h + padding().t + padding().b;
}

SizeHint BarList::sizeHint() const {
    SizeHint h = ChartBase::sizeHint();
    h.preferred.h = preferredHeight();
    h.stretchY = 0.f;
    return h;
}

void BarList::onPaint(const PaintContext& ctx) {
    gfx::IRenderer& r = ctx.r;
    const colors::Ink k = colors::ink(ctx.theme);
    const auto& d = data_;
    const Rect b = contentRect();
    if (b.w < 20.f || b.h < 10.f) return;
    const Frame f = frame();
    const Format fmt = d.format ? d.format : Format([](double v) { return fr::integer(v); });
    const bool live = active_ >= 0 && ((hovered() && !keyboard_) || (focused() && keyboard_));
    const int shown = live ? active_ : example_;

    if (f.legendHeight > 0.f) {
        std::vector<LegendItem> items;
        for (std::size_t i = 0; i < d.partNames.size(); ++i) items.push_back({d.partNames[i], partColor(i), Swatch::Box, {}});
        paintLegend(r, ctx.theme, b.x, b.y, b.w, items);
    }
    if (d.rows.empty()) {
        text(r, b.x + b.w * 0.5f, f.top + std::min(40.f, (b.bottom() - f.top) * 0.5f), fit(r, d.emptyText, FontId{12}, b.w), FontId{12}, k.faint, 0);
        return;
    }
    // La colonne des valeurs (et de l'extra).
    float vw = 0.f;
    for (const auto& row : d.rows) {
        const std::string v = row.off && row.value == 0.0 ? std::string("\xE2\x80\x94") : !row.valueText.empty() ? row.valueText : fmt(rowTotal(row));
        vw = std::max(vw, tw(v, barValueFont(row.strong)));
    }
    const float valueRight = d.mode == BarMode::Grouped ? b.right() : f.x1 + 10.f + vw;
    const float extraX = valueRight + 10.f;
    // Un classement : la base des barres, un trait fin.
    if (d.mode == BarMode::Ranking && !d.rows.empty() && !f.stacked)
        r.fillRect({f.x0 - 1.f, f.rowTop.front() + 4.f, 1.f, f.rowTop.back() + f.rowHeight.back() - f.rowTop.front() - 8.f}, k.line2);

    for (std::size_t i = 0; i < d.rows.size(); ++i) {
        const BarRow& row = d.rows[i];
        const float top = f.rowTop[i], h = f.rowHeight[i];
        if (top + h > b.bottom() + 1.f) break;   // une ligne qui ne tient pas n'est pas dessinee a moitie
        const float cy = top + (row.sub.empty() || d.mode == BarMode::Grouped ? h * 0.5f : h * 0.5f - 5.f);
        if (static_cast<int>(i) == shown) r.fillRoundedRect({b.x - 4.f, top + 1.f, b.w + 8.f, h - 2.f}, k.hover, 6.f);
        if (f.stacked) {
            // Le libelle, la valeur et l'extra sur une ligne, la barre dessous (toute la largeur).
            const FontId lf = barLabelFont(row.strong);
            const FontId vf = barValueFont(row.strong);
            const float ly = top + 9.f;
            const std::string v = row.off && row.value == 0.0 ? std::string("\xE2\x80\x94") : !row.valueText.empty() ? row.valueText : fmt(rowTotal(row));
            float right = b.right();
            if (!row.extra.empty()) {
                const std::string ex = fit(r, row.extra, barExtraFont(), b.w * 0.3f);
                text(r, right, ly, ex, barExtraFont(), k.faint, 1);
                right -= tw(ex, barExtraFont()) + 6.f;
            }
            text(r, right, ly, v, vf, row.off ? k.faint : k.ink, 1);
            right -= tw(v, vf) + 10.f;
            text(r, b.x, ly, fit(r, row.label, lf, right - b.x), lf, row.off ? k.faint : (row.strong ? k.ink : k.ink2));
            const float th = d.barThickness > 0.f ? d.barThickness : 8.f;
            const float by = std::round(top + 20.f);
            if (!row.note.empty() && !(rowTotal(row) > 0.0)) {
                const Color nc = row.warn ? ctx.theme.color.warning : k.faint;
                text(r, b.x, by + th * 0.5f, fit(r, row.note, FontId{11}, b.w), FontId{11}, nc);
                continue;
            }
            if (d.mode == BarMode::Track) r.fillRoundedRect({f.x0, by, f.x1 - f.x0, th}, k.track, std::min(4.f, th * 0.5f));
            if (row.value > 0.0) paintColumn(r, {f.x0, by, std::max(2.f, f.xAt(row.value) - f.x0), th}, colors::onTheme(rowColor(i), k), std::min(4.f, th * 0.5f), true, true);
            continue;
        }
        // Le libelle.
        const FontId lf = barLabelFont(row.strong);
        const float lw = f.x0 - 12.f - b.x;
        if (lw > 8.f) {
            text(r, b.x, cy, fit(r, row.label, lf, lw), lf, row.off ? k.faint : (row.strong ? k.ink : k.ink2));
            if (!row.sub.empty()) text(r, b.x, cy + 13.f, fit(r, row.sub, FontId{11}, lw), FontId{11}, k.faint);
        }
        const float th = d.barThickness > 0.f ? d.barThickness + (row.strong ? 2.f : 0.f) : (row.strong ? 12.f : 10.f);
        const Color rc = colors::onTheme(rowColor(i), k);
        // La barre, ou la note a sa place.
        const bool noBar = !row.note.empty() && !(rowTotal(row) > 0.0);
        if (noBar) {
            float nx = f.x0 + 2.f;
            if (row.warn) {
                const Color wc = ctx.theme.color.warning;
                drawIcon(r, Icon::Warning, {nx, cy - 6.f, 12.f, 12.f}, wc);
                nx += 16.f;
                text(r, nx, cy, fit(r, row.note, FontId{11}, f.x1 - nx), FontId{11}, wc);
            } else {
                text(r, nx, cy, fit(r, row.note, FontId{11}, f.x1 - nx), FontId{11}, k.faint);
            }
        } else if (d.mode == BarMode::Ranking || d.mode == BarMode::Track) {
            const Rect bar{f.x0, std::round(cy - th * 0.5f), 0.f, th};
            if (d.mode == BarMode::Track) r.fillRoundedRect({f.x0, bar.y, f.x1 - f.x0, th}, k.track, std::min(4.f, th * 0.5f));
            const float x = f.xAt(row.value);
            if (row.value > 0.0) paintColumn(r, {f.x0, bar.y, std::max(2.f, x - f.x0), th}, rc, std::min(4.f, th * 0.5f), true, true);
        } else if (d.mode == BarMode::Share) {
            std::vector<Color> cols;
            for (std::size_t p = 0; p < row.parts.size(); ++p) cols.push_back(partColor(p));
            const double tot = rowTotal(row);
            const float w = tot > 0.0 ? (d.maxValue > 0.0 ? f.xAt(tot) - f.x0 : f.x1 - f.x0) : 0.f;
            paintSegments(r, ctx.theme, {f.x0, std::round(cy - th * 0.5f), w, th}, row.parts, cols);
        } else {
            // Grouped : une barre fine par groupe, la valeur au bout.
            const std::size_t g = row.parts.size();
            const float bt = d.barThickness > 0.f ? d.barThickness : 8.f;
            const float total = static_cast<float>(g) * bt + static_cast<float>(g ? g - 1 : 0) * 3.f;
            float y = cy - total * 0.5f;
            for (std::size_t p = 0; p < g; ++p) {
                const double v = row.parts[p];
                if (std::isfinite(v) && v > 0.0) {
                    const float x = f.xAt(v);
                    paintColumn(r, {f.x0, std::round(y), std::max(2.f, x - f.x0), bt}, colors::onTheme(partColor(p), k), std::min(4.f, bt * 0.5f), true, true);
                    if (d.valueLabels) text(r, x + 5.f, std::round(y) + bt * 0.5f, fmt(v), gfx::semibold(FontId{11}), k.ink);
                }
                y += bt + 3.f;
            }
        }
        // La valeur, l'extra.
        if (d.mode != BarMode::Grouped) {
            const std::string v = row.off && row.value == 0.0 ? std::string("\xE2\x80\x94") : !row.valueText.empty() ? row.valueText : fmt(rowTotal(row));
            text(r, valueRight, cy, v, barValueFont(row.strong), row.off ? k.faint : k.ink, 1);
            if (!row.extra.empty()) text(r, extraX, cy, fit(r, row.extra, barExtraFont(), b.right() - extraX), barExtraFont(), k.faint);
        }
    }
    // Grouped : l'axe des valeurs en bas.
    if (d.mode == BarMode::Grouped && d.axis && !d.rows.empty()) {
        float y = f.rowTop.front();
        for (std::size_t i = 0; i < f.rowTop.size(); ++i)
            if (f.rowTop[i] + f.rowHeight[i] <= b.bottom() - 18.f) y = f.rowTop[i] + f.rowHeight[i] + 2.f;
        r.fillRect({f.x0, y, f.x1 - f.x0, 1.f}, k.base);
        const auto ticks = d.ticks.empty() ? niceTicks(0.0, f.maxValue, 3) : d.ticks;
        float lastRight = -1e9f;
        for (const double t : ticks) {
            const float x = f.xAt(t);
            const std::string l = fmt(t);
            const float w = tw(l, detail::fontAxis());
            if (x > f.x1 + 1.f || x - w * 0.5f < lastRight + 6.f || x + w * 0.5f > b.right()) continue;
            text(r, x, y + 10.f, l, detail::fontAxis(), k.faint, 0);
            lastRight = x + w * 0.5f;
        }
    }
    if (!d.rows.empty()) paintFocus(ctx, {b.x, f.rowTop.front(), b.w, f.rowTop.back() + f.rowHeight.back() - f.rowTop.front()});
}

std::pair<int, int> BarList::itemAt(Point p) const {
    const Frame f = frame();
    const Rect b = contentRect();
    if (p.x < b.x - 4.f || p.x > b.right() + 4.f) return {-1, -1};
    for (std::size_t i = 0; i < f.rowTop.size(); ++i) {
        if (p.y < f.rowTop[i] || p.y >= f.rowTop[i] + f.rowHeight[i]) continue;
        const BarRow& row = data_.rows[i];
        int sub = -1;
        if (data_.mode == BarMode::Share) {
            double tot = rowTotal(row), acc = 0.0;
            const float w = tot > 0.0 ? (data_.maxValue > 0.0 ? f.xAt(tot) - f.x0 : f.x1 - f.x0) : 0.f;
            for (std::size_t q = 0; q < row.parts.size() && tot > 0.0; ++q) {
                const double v = std::isfinite(row.parts[q]) ? row.parts[q] : 0.0;
                const float a = f.x0 + static_cast<float>(acc / tot) * w, e = f.x0 + static_cast<float>((acc + v) / tot) * w;
                if (v > 0.0 && p.x >= a - 1.f && p.x < e + 1.f) {
                    sub = static_cast<int>(q);
                    break;
                }
                acc += v;
            }
        } else if (data_.mode == BarMode::Grouped) {
            const std::size_t g = row.parts.size();
            const float bt = data_.barThickness > 0.f ? data_.barThickness : 8.f;
            const float total = static_cast<float>(g) * bt + static_cast<float>(g ? g - 1 : 0) * 3.f;
            const float y0 = f.rowTop[i] + f.rowHeight[i] * 0.5f - total * 0.5f;
            const int q = static_cast<int>(std::floor((p.y - y0 + 1.5f) / (bt + 3.f)));
            if (q >= 0 && static_cast<std::size_t>(q) < g) sub = q;
        }
        return {static_cast<int>(i), sub};
    }
    return {-1, -1};
}

Pick BarList::pickFor(int index, int sub) const {
    Pick p;
    if (index < 0 || index >= itemCount()) return p;
    const BarRow& row = data_.rows[static_cast<std::size_t>(index)];
    p.index = index;
    p.series = sub;
    if (sub >= 0 && static_cast<std::size_t>(sub) < row.parts.size()) {
        p.value = row.parts[static_cast<std::size_t>(sub)];
        p.key = static_cast<std::size_t>(sub) < data_.partKeys.size() ? data_.partKeys[static_cast<std::size_t>(sub)] : std::string();
    } else {
        p.value = rowTotal(row);
        p.key = row.key;
    }
    return p;
}

Pick BarList::pickAt(Point p) const {
    const auto [i, s] = itemAt(p);
    return i < 0 ? Pick{} : pickFor(i, s);
}

Tip BarList::tipFor(int index, int) const {
    Tip t;
    if (index < 0 || index >= itemCount()) return t;
    const BarRow& row = data_.rows[static_cast<std::size_t>(index)];
    const Format fmt = data_.format ? data_.format : Format([](double v) { return fr::integer(v); });
    t.title = row.label;
    if (data_.mode == BarMode::Share) {
        const double tot = rowTotal(row);
        for (std::size_t q = 0; q < row.parts.size(); ++q) {
            const double v = row.parts[q];
            if (!std::isfinite(v) || v <= 0.0) continue;
            const std::string name = q < data_.partNames.size() ? data_.partNames[q] : std::string();
            t.rows.push_back({fmt(v), name + (tot > 0.0 ? std::string(" ") + fr::kDot + " " + fr::percent(v * 100.0 / tot) : std::string()), partColor(q), false});
        }
        t.extra = "Total : " + (row.valueText.empty() ? fmt(tot) : row.valueText);
    } else if (data_.mode == BarMode::Grouped) {
        for (std::size_t q = 0; q < row.parts.size(); ++q) {
            if (!std::isfinite(row.parts[q])) continue;
            t.rows.push_back({fmt(row.parts[q]), q < data_.partNames.size() ? data_.partNames[q] : std::string(), partColor(q), false});
        }
    } else if (!row.note.empty() && !(row.value > 0.0)) {
        t.rows.push_back({row.off ? std::string("\xE2\x80\x94") : fmt(row.value), row.note, {}, false});
    } else {
        t.rows.push_back({row.valueText.empty() ? fmt(row.value) : row.valueText, row.extra, rowColor(static_cast<std::size_t>(index)), false});
    }
    if (!row.tip.empty()) t.extra = row.tip;
    t.click = data_.clickHint;
    return t;
}

Rect BarList::tipAnchor(int index, int) const {
    const Frame f = frame();
    if (index < 0 || static_cast<std::size_t>(index) >= f.rowTop.size()) return contentRect();
    const BarRow& row = data_.rows[static_cast<std::size_t>(index)];
    float x = f.x0;
    if (data_.mode == BarMode::Grouped) {
        for (const double v : row.parts)
            if (std::isfinite(v)) x = std::max(x, f.xAt(v));
    } else x = std::max(f.x0, f.xAt(rowTotal(row)));
    return {f.x0, f.rowTop[static_cast<std::size_t>(index)], x - f.x0, f.rowHeight[static_cast<std::size_t>(index)]};
}

// =============================================================================
//  HeatMap
// =============================================================================
HeatMap::HeatMap(std::string id) : ChartBase(std::move(id)) {}

void HeatMap::setData(HeatMapData data) {
    data_ = std::move(data);
    if (active_ >= itemCount()) active_ = -1;
    invalidate();
}

int HeatMap::itemCount() const {
    std::size_t c = data_.colLabels.size();
    for (const auto& row : data_.cells) c = std::max(c, row.size());
    return static_cast<int>(c);
}

int HeatMap::subCount() const { return static_cast<int>(std::max(data_.rowLabels.size(), data_.cells.size())); }

Rect HeatMap::Frame::cell(int r, int c) const noexcept {
    return {x0 + static_cast<float>(c) * (cw + gap), y0 + static_cast<float>(r) * (ch + gap), cw, ch};
}

HeatMap::Frame HeatMap::frame() const {
    Frame f;
    const Rect b = contentRect();
    f.rows = subCount();
    f.cols = itemCount();
    for (const auto& row : data_.cells)
        for (const double v : row)
            if (std::isfinite(v)) f.max = std::max(f.max, v);
    float lw = 0.f;
    for (const auto& l : data_.rowLabels) lw = std::max(lw, tw(l, FontId{11}));
    const float top = data_.bandLabel.empty() ? 6.f : 22.f;
    const float bottom = 18.f + ((data_.scaleLabel.empty() && data_.flagLabel.empty()) ? 0.f : 30.f);
    f.x0 = b.x + lw + 10.f;
    const float availW = b.right() - f.x0 - 2.f;
    const float availH = b.h - top - bottom;
    if (f.cols <= 0 || f.rows <= 0) return f;
    f.cw = std::max(4.f, (availW - f.gap * static_cast<float>(f.cols - 1)) / static_cast<float>(f.cols));
    f.ch = std::max(4.f, std::min(f.cw * 1.1f, (availH - f.gap * static_cast<float>(f.rows - 1)) / static_cast<float>(f.rows)));
    f.y0 = b.y + top;
    return f;
}

void HeatMap::onPaint(const PaintContext& ctx) {
    gfx::IRenderer& r = ctx.r;
    const colors::Ink k = colors::ink(ctx.theme);
    const auto& d = data_;
    const Frame f = frame();
    if (f.rows <= 0 || f.cols <= 0) return;
    const bool live = active_ >= 0 && ((hovered() && !keyboard_) || (focused() && keyboard_));
    const int sc = live ? active_ : example_, sr = live ? activeSub_ : exampleSub_;
    const Color zero = k.dark ? colors::mix(k.surface, k.ink, 0.07f) : colors::kSequential[0];
    // La bande des heures habituelles (derriere les cases).
    if (std::isfinite(d.bandFrom) && std::isfinite(d.bandTo)) {
        const float x0 = f.x0 + static_cast<float>(d.bandFrom) * (f.cw + f.gap) - 3.f;
        const float x1 = f.x0 + static_cast<float>(d.bandTo) * (f.cw + f.gap) + 1.f;
        const Rect band{x0, f.y0 - 4.f, x1 - x0, static_cast<float>(f.rows) * (f.ch + f.gap) + 4.f};
        shapes::strokeRoundedRect(r, band, k.dark ? k.base : Color::rgb(0xB9C6D8), 6.f, 1.5f);
        if (!d.bandLabel.empty()) {
            const std::string bl = fit(r, d.bandLabel, FontId{11}, contentRect().w);
            const float w = tw(bl, FontId{11});
            const float cx = std::max(contentRect().x + w * 0.5f, std::min(contentRect().right() - w * 0.5f, (x0 + x1) * 0.5f));
            text(r, cx, f.y0 - 13.f, bl, FontId{11}, k.muted, 0);
        }
    }
    // Des cases trop basses pour un libelle chacune : une rangee sur deux (ou trois).
    const int labelStep = std::max(1, static_cast<int>(std::ceil(detail::lh(FontId{11}) / (f.ch + f.gap))));
    for (int row = 0; row < f.rows; ++row) {
        const Rect first = f.cell(row, 0);
        if (static_cast<std::size_t>(row) < d.rowLabels.size() && row % labelStep == 0)
            text(r, f.x0 - 9.f, first.y + first.h * 0.5f, d.rowLabels[static_cast<std::size_t>(row)], FontId{11}, k.faint, 1);
        for (int c = 0; c < f.cols; ++c) {
            double v = 0.0;
            if (static_cast<std::size_t>(row) < d.cells.size() && static_cast<std::size_t>(c) < d.cells[static_cast<std::size_t>(row)].size())
                v = d.cells[static_cast<std::size_t>(row)][static_cast<std::size_t>(c)];
            const int step = colors::sequentialStep(v, f.max);
            const Color col = step == 0 ? zero : colors::onTheme(colors::kSequential[step], k);
            r.fillRoundedRect(f.cell(row, c), col, 3.f);
        }
    }
    for (const auto& [row, c] : d.flagged) {
        if (row < 0 || row >= f.rows || c < 0 || c >= f.cols) continue;
        const Rect cell = f.cell(row, c);
        shapes::strokeRoundedRect(r, {cell.x - 2.f, cell.y - 2.f, cell.w + 4.f, cell.h + 4.f}, colors::kBad, 4.f, 2.f);
    }
    if (sc >= 0 && sr >= 0 && sc < f.cols && sr < f.rows) {
        const Rect cell = f.cell(sr, sc);
        shapes::strokeRoundedRect(r, {cell.x - 1.f, cell.y - 1.f, cell.w + 2.f, cell.h + 2.f}, k.ink2, 4.f, 1.5f);
    }
    // Les heures en bas.
    const float yl = f.y0 + static_cast<float>(f.rows) * (f.ch + f.gap) + 7.f;
    // Un libelle tous les `labelEvery` ; plus espaces si la place manque.
    int every = std::max(1, d.labelEvery);
    float widest = 0.f;
    for (const auto& l : d.colLabels) widest = std::max(widest, tw(l, FontId{11}));
    while (every < f.cols && static_cast<float>(every) * (f.cw + f.gap) < widest + 6.f) every += std::max(1, d.labelEvery);
    for (int c = 0; c < f.cols && static_cast<std::size_t>(c) < d.colLabels.size(); c += every) {
        const Rect cell = f.cell(0, c);
        const std::string& l = d.colLabels[static_cast<std::size_t>(c)];
        const float lw = tw(l, FontId{11});
        const float x = std::max(contentRect().x + lw * 0.5f, std::min(contentRect().right() - lw * 0.5f, cell.x + cell.w * 0.5f));
        text(r, x, yl, l, FontId{11}, k.faint, 0);
    }
    // La legende : la rampe, puis le cadre signale.
    if (!d.scaleLabel.empty() || !d.flagLabel.empty()) {
        const float ly = yl + 20.f;
        float x = f.x0;
        const float right = contentRect().right();
        // La rampe (et son libelle) puis le cadre : ce qui ne tient pas sur la ligne n'est pas dessine.
        const float scaleW = d.scaleLabel.empty() ? 0.f : tw(d.scaleLabel, FontId{11}) + 8.f + 7.f * 20.f;
        if (scaleW > 0.f && x + scaleW > right) x = contentRect().x;
        if (scaleW > 0.f && x + scaleW <= right) {
            text(r, x, ly, d.scaleLabel, FontId{11}, k.muted);
            x += tw(d.scaleLabel, FontId{11}) + 8.f;
            for (int i = 0; i < 7; ++i) {
                const Color col = i == 0 ? zero : colors::onTheme(colors::kSequential[i], k);
                r.fillRoundedRect({x + static_cast<float>(i) * 20.f, ly - 5.f, 18.f, 10.f}, col, 2.f);
            }
            text(r, x, ly + 12.f, "0", FontId{10}, k.faint);
            text(r, x + 7.f * 20.f - 2.f, ly + 12.f, fr::integer(f.max), FontId{10}, k.faint, 1);
            x += 7.f * 20.f + 24.f;
        }
        if (!d.flagLabel.empty() && x + 20.f + tw(d.flagLabel, FontId{11}) <= right) {
            shapes::strokeRoundedRect(r, {x, ly - 6.f, 14.f, 12.f}, colors::kBad, 3.f, 2.f);
            text(r, x + 20.f, ly, d.flagLabel, FontId{11}, k.ink2);
        }
    }
    paintFocus(ctx, {f.x0, f.y0, static_cast<float>(f.cols) * (f.cw + f.gap) - f.gap, static_cast<float>(f.rows) * (f.ch + f.gap) - f.gap});
}

std::pair<int, int> HeatMap::itemAt(Point p) const {
    const Frame f = frame();
    if (f.rows <= 0 || f.cols <= 0) return {-1, -1};
    // La case et l'espace qui la suit : pas de trou entre deux cibles.
    const float cx = p.x - f.x0 + f.gap * 0.5f, cy = p.y - f.y0 + f.gap * 0.5f;
    if (cx < 0.f || cy < 0.f) return {-1, -1};
    const int c = static_cast<int>(cx / (f.cw + f.gap));
    const int r = static_cast<int>(cy / (f.ch + f.gap));
    if (c >= f.cols || r >= f.rows) return {-1, -1};
    return {c, r};
}

Pick HeatMap::pickFor(int index, int sub) const {
    Pick p;
    if (index < 0 || index >= itemCount() || sub < 0 || sub >= subCount()) return p;
    p.index = index;
    p.series = sub;
    if (static_cast<std::size_t>(sub) < data_.cells.size() && static_cast<std::size_t>(index) < data_.cells[static_cast<std::size_t>(sub)].size())
        p.value = data_.cells[static_cast<std::size_t>(sub)][static_cast<std::size_t>(index)];
    return p;
}

Pick HeatMap::pickAt(Point p) const {
    const auto [c, r] = itemAt(p);
    return c < 0 ? Pick{} : pickFor(c, r);
}

Tip HeatMap::tipFor(int col, int row) const {
    Tip t;
    if (col < 0 || row < 0 || col >= itemCount() || row >= subCount()) return t;
    if (data_.cellTitle) t.title = data_.cellTitle(row, col);
    else {
        const std::string rl = static_cast<std::size_t>(row) < data_.rowLabels.size() ? data_.rowLabels[static_cast<std::size_t>(row)] : std::string();
        t.title = rl + " " + std::to_string(col) + fr::kNbsp + "h " + fr::kArrow + " " + std::to_string(col + 1) + fr::kNbsp + "h";
    }
    double v = 0.0;
    if (static_cast<std::size_t>(row) < data_.cells.size() && static_cast<std::size_t>(col) < data_.cells[static_cast<std::size_t>(row)].size())
        v = data_.cells[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
    const std::string val = data_.valueFormat ? data_.valueFormat(v) : plural(v, data_.valueUnit);
    double mx = 0.0;
    for (const auto& r : data_.cells)
        for (const double x : r)
            if (std::isfinite(x)) mx = std::max(mx, x);
    t.rows.push_back({val, {}, colors::sequentialStep(v, mx) ? colors::kSequential[colors::sequentialStep(v, mx)] : Color{0, 0, 0, 0}, false});
    for (const auto& [r, c] : data_.flagged)
        if (r == row && c == col) t.extra = data_.flagLabel.empty() ? std::string("signal\xC3\xA9" "e") : data_.flagLabel;
    t.click = data_.clickHint;
    return t;
}

Rect HeatMap::tipAnchor(int index, int sub) const {
    const Frame f = frame();
    if (index < 0 || sub < 0) return contentRect();
    return f.cell(sub, index);
}

// =============================================================================
//  DayStrip
// =============================================================================
DayStrip::DayStrip(std::string id) : ChartBase(std::move(id)) {}

void DayStrip::setData(DayStripData data) {
    data_ = std::move(data);
    if (active_ >= itemCount()) active_ = -1;
    invalidate();
}

Rect DayStrip::cellRect(int i) const {
    const Rect b = contentRect();
    const int n = itemCount();
    if (n <= 0 || i < 0 || i >= n) return {};
    const float gap = 3.f;
    const float cw = std::max(4.f, std::min(26.f, (b.w - gap * static_cast<float>(n - 1)) / static_cast<float>(n)));
    // La place des dates dessous (24 px) ; sans elle, les carres seuls.
    const float ch = std::max(4.f, std::min(cw, b.h >= 34.f ? b.h - 26.f : b.h - 8.f));
    return {b.x + static_cast<float>(i) * (cw + gap), b.y + 2.f, cw, ch};
}

void DayStrip::onPaint(const PaintContext& ctx) {
    gfx::IRenderer& r = ctx.r;
    const colors::Ink k = colors::ink(ctx.theme);
    const auto& d = data_;
    const int n = itemCount();
    if (n <= 0) return;
    double mx = 0.0;
    for (const double v : d.values)
        if (std::isfinite(v)) mx = std::max(mx, v);
    const Color zero = k.dark ? colors::mix(k.surface, k.ink, 0.07f) : colors::kSequential[0];
    const bool live = active_ >= 0 && ((hovered() && !keyboard_) || (focused() && keyboard_));
    const int shown = live ? active_ : example_;
    for (int i = 0; i < n; ++i) {
        const Rect c = cellRect(i);
        const int step = colors::sequentialStep(d.values[static_cast<std::size_t>(i)], mx);
        r.fillRoundedRect(c, step == 0 ? zero : colors::onTheme(colors::kSequential[step], k), 3.f);
        if (i == shown) shapes::strokeRoundedRect(r, {c.x - 1.f, c.y - 1.f, c.w + 2.f, c.h + 2.f}, k.ink2, 4.f, 1.5f);
    }
    for (const int i : d.flagged) {
        if (i < 0 || i >= n) continue;
        const Rect c = cellRect(i);
        shapes::strokeRoundedRect(r, {c.x - 2.f, c.y - 2.f, c.w + 4.f, c.h + 4.f}, colors::kBad, 4.f, 2.f);
    }
    const Rect c0 = cellRect(0);
    if (d.streakFrom >= 0 && d.streakTo >= d.streakFrom && d.streakTo < n) {
        const Rect a = cellRect(d.streakFrom), e = cellRect(d.streakTo);
        r.fillRoundedRect({a.x, a.bottom() + 3.f, e.right() - a.x, 3.f}, colors::onTheme(d.streakColor.a ? d.streakColor : colors::kOne, k), 1.5f);
    }
    std::vector<int> labels = d.labelAt;
    if (contentRect().h < 34.f) labels = {-1};   // pas la place des dates
    else if (labels.empty()) {
        for (int i = 0; i < n; i += 7) labels.push_back(i);
        if (n - 1 - labels.back() >= 4) labels.push_back(n - 1);
        else labels.back() = n - 1;
    }
    float lastRight = -1e9f;
    for (const int i : labels) {
        if (i < 0 || i >= n) continue;
        const Rect c = cellRect(i);
        const std::string s = i == n - 1 && d.lastIsToday ? std::string("auj.") : fr::ddmm(d.first + i);
        const float w = tw(s, FontId{10});
        float x = std::clamp(c.x + c.w * 0.5f - w * 0.5f, contentRect().x, contentRect().right() - w);
        if (x < lastRight + 4.f) continue;
        r.drawText({std::round(x), std::round(c0.bottom() + 9.f)}, s, FontId{10}, k.faint);
        lastRight = x + w;
    }
    paintFocus(ctx, {c0.x, c0.y, cellRect(n - 1).right() - c0.x, c0.h});
}

std::pair<int, int> DayStrip::itemAt(Point p) const {
    const int n = itemCount();
    if (n <= 0) return {-1, -1};
    const Rect a = cellRect(0);
    if (p.y < a.y - 3.f || p.y > a.bottom() + 8.f) return {-1, -1};
    const float step = n > 1 ? cellRect(1).x - a.x : a.w;
    const int i = static_cast<int>(std::floor((p.x - a.x + 1.5f) / step));
    return i >= 0 && i < n ? std::pair<int, int>{i, -1} : std::pair<int, int>{-1, -1};
}

Pick DayStrip::pickAt(Point p) const {
    const auto [i, s] = itemAt(p);
    Pick pk;
    if (i < 0) return pk;
    pk.index = i;
    pk.value = data_.values[static_cast<std::size_t>(i)];
    (void)s;
    return pk;
}

Tip DayStrip::tipFor(int index, int) const {
    Tip t;
    if (index < 0 || index >= itemCount()) return t;
    t.title = fr::dayLong(data_.first + index);
    const double v = data_.values[static_cast<std::size_t>(index)];
    t.rows.push_back({v > 0 ? plural(v, data_.valueUnit) : std::string("aucune ") + data_.valueUnit, {}, {}, false});
    for (const int f : data_.flagged)
        if (f == index) t.extra = "signal\xC3\xA9";
    if (index >= data_.streakFrom && index <= data_.streakTo && data_.streakFrom >= 0) t.extra += (t.extra.empty() ? "" : std::string(" ") + fr::kDot + " ") + std::string("dans la s\xC3\xA9rie en cours");
    t.click = data_.clickHint;
    return t;
}

Rect DayStrip::tipAnchor(int index, int) const { return cellRect(index); }

// =============================================================================
//  Dumbbell
// =============================================================================
Dumbbell::Dumbbell(std::string id) : ChartBase(std::move(id)) {}

void Dumbbell::setData(DumbbellData data) {
    data_ = std::move(data);
    if (active_ >= itemCount()) active_ = -1;
    invalidate();
}

void Dumbbell::setColorKeys(ColorKeys keys) {
    keys_ = std::move(keys);
    ownKeys_ = true;
    invalidate();
}

void Dumbbell::setSharedDomain(double lo, double hi) {
    sharedLo_ = lo;
    sharedHi_ = hi;
    invalidate();
}

std::pair<double, double> Dumbbell::extent() const {
    double hi = 0.0;
    for (const auto& r : data_.rows) hi = std::max({hi, std::isfinite(r.before) ? r.before : 0.0, std::isfinite(r.after) ? r.after : 0.0});
    return {0.0, hi};
}

Color Dumbbell::rowColor(std::size_t i) const {
    const auto& r = data_.rows[i];
    if (r.color.a) return r.color;
    if (!r.key.empty()) return keyColor(ownKeys_ ? &keys_ : nullptr, r.key);
    return colors::admin(static_cast<int>(i));
}

float Dumbbell::Frame::xAt(double v) const noexcept {
    if (!(hi > lo)) return x0;
    return x0 + static_cast<float>((v - lo) / (hi - lo) * static_cast<double>(x1 - x0));
}

namespace {
std::string dumbbellText(const DumbbellRow& r, const Format& fmt) {
    if (!r.text.empty()) return r.text;
    std::string s = fmt(r.after);
    const std::string dp = fr::deltaPercent(r.after, r.before);
    if (!dp.empty()) s += std::string(fr::kNbsp) + fr::kNbsp + dp;
    return s;
}
} // namespace

Dumbbell::Frame Dumbbell::frame() const {
    Frame f;
    const Rect b = contentRect();
    const Format fmt = data_.format ? data_.format : Format([](double v) { return fr::integer(v); });
    float lw = 0.f, rw = 0.f;
    for (const auto& r : data_.rows) {
        lw = std::max(lw, tw(r.label, FontId{12}));
        rw = std::max(rw, tw(dumbbellText(r, fmt), gfx::semibold(FontId{11})));
    }
    auto [lo, hi] = extent();
    if (std::isfinite(sharedHi_)) hi = std::max(hi, sharedHi_);
    f.ticks = data_.ticks.empty() ? niceTicks(lo, hi, 3) : data_.ticks;
    f.lo = std::min(lo, f.ticks.front());
    f.hi = std::max(hi, f.ticks.back());
    f.x0 = b.x + lw + 12.f;
    f.x1 = std::max(f.x0 + 20.f, b.right() - rw - 14.f);
    const float axis = 16.f;
    const int n = std::max(1, itemCount());
    f.rowH = std::clamp((b.h - axis) / static_cast<float>(n), 14.f, 30.f);
    f.top = b.y;
    return f;
}

void Dumbbell::onPaint(const PaintContext& ctx) {
    gfx::IRenderer& r = ctx.r;
    const colors::Ink k = colors::ink(ctx.theme);
    const Frame f = frame();
    const Format fmt = data_.format ? data_.format : Format([](double v) { return fr::integer(v); });
    const int n = itemCount();
    const float bottom = f.top + f.rowH * static_cast<float>(n);
    float lastRight = -1e9f;
    for (const double t : f.ticks) {
        const float x = std::round(f.xAt(t));
        r.fillRect({x, f.top, 1.f, bottom - f.top}, std::fabs(t) < 1e-12 ? k.base : k.grid);
        const std::string l = fmt(t);
        const float w = tw(l, FontId{10});
        if (x - w * 0.5f < lastRight + 5.f || x + w * 0.5f > contentRect().right() + 1.f) continue;   // pas l'un sur l'autre
        text(r, x, bottom + 8.f, l, FontId{10}, k.faint, 0);
        lastRight = x + w * 0.5f;
    }
    const bool live = active_ >= 0 && ((hovered() && !keyboard_) || (focused() && keyboard_));
    const int shown = live ? active_ : example_;
    for (int i = 0; i < n; ++i) {
        const auto& row = data_.rows[static_cast<std::size_t>(i)];
        const float cy = f.top + f.rowH * (static_cast<float>(i) + 0.5f);
        if (i == shown) r.fillRoundedRect({contentRect().x - 4.f, cy - f.rowH * 0.5f + 1.f, contentRect().w + 8.f, f.rowH - 2.f}, k.hover, 6.f);
        text(r, f.x0 - 10.f, cy, row.label, FontId{12}, k.ink2, 1);
        const float a = f.xAt(row.before), e = f.xAt(row.after);
        strokePath(r, {{std::min(a, e), cy}, {std::max(a, e), cy}}, k.dark ? k.base : Color::rgb(0xCBD3DE), 2.f);
        paintDot(r, {a, cy}, 4.5f, colors::kCompare, k.surface);
        paintDot(r, {e, cy}, 5.f, colors::onTheme(rowColor(static_cast<std::size_t>(i)), k), k.surface);
        text(r, std::max(a, e) + 10.f, cy, dumbbellText(row, fmt), gfx::semibold(FontId{11}), k.ink);
    }
    if (n > 0) paintFocus(ctx, {f.x0, f.top, f.x1 - f.x0, bottom - f.top});
}

std::pair<int, int> Dumbbell::itemAt(Point p) const {
    const Frame f = frame();
    const Rect b = contentRect();
    if (p.x < b.x || p.x > b.right()) return {-1, -1};
    const int i = static_cast<int>(std::floor((p.y - f.top) / f.rowH));
    return i >= 0 && i < itemCount() ? std::pair<int, int>{i, -1} : std::pair<int, int>{-1, -1};
}

Pick Dumbbell::pickAt(Point p) const {
    const auto [i, s] = itemAt(p);
    Pick pk;
    if (i < 0) return pk;
    (void)s;
    pk.index = i;
    pk.key = data_.rows[static_cast<std::size_t>(i)].key;
    pk.value = data_.rows[static_cast<std::size_t>(i)].after;
    return pk;
}

Tip Dumbbell::tipFor(int index, int) const {
    Tip t;
    if (index < 0 || index >= itemCount()) return t;
    const auto& row = data_.rows[static_cast<std::size_t>(index)];
    const Format fmt = data_.format ? data_.format : Format([](double v) { return fr::integer(v); });
    t.title = row.label;
    t.rows.push_back({fmt(row.after), data_.afterName, rowColor(static_cast<std::size_t>(index)), false});
    t.rows.push_back({fmt(row.before), data_.beforeName, colors::kCompare, false});
    const std::string dp = fr::deltaPercent(row.after, row.before);
    if (!dp.empty()) t.extra = "\xC3\x89" "cart : " + dp;
    t.click = data_.clickHint;
    return t;
}

Rect Dumbbell::tipAnchor(int index, int) const {
    const Frame f = frame();
    const auto& row = data_.rows[static_cast<std::size_t>(std::clamp(index, 0, std::max(0, itemCount() - 1)))];
    const float x = std::max(f.xAt(row.before), f.xAt(row.after));
    return {f.x0, f.top + f.rowH * static_cast<float>(index), x - f.x0 + 6.f, f.rowH};
}

// =============================================================================
//  Timeline
// =============================================================================
Timeline::Timeline(std::string id) : ChartBase(std::move(id)) {}

void Timeline::setData(TimelineData data) {
    data_ = std::move(data);
    if (active_ >= itemCount()) active_ = -1;
    invalidate();
}

void Timeline::setColorKeys(ColorKeys keys) {
    keys_ = std::move(keys);
    ownKeys_ = true;
    invalidate();
}

float Timeline::Frame::xAt(double day) const noexcept {
    if (!(to > from)) return x0;
    return x0 + static_cast<float>((day - from) / (to - from) * static_cast<double>(x1 - x0));
}

Timeline::Frame Timeline::frame() const {
    Frame f;
    const Rect b = contentRect();
    f.legendHeight = data_.legend.empty() ? 0.f : legendHeight(data_.legend, b.w) + 10.f;
    float lw = 0.f;
    for (const auto& r : data_.rows) lw = std::max(lw, tw(r.label, FontId{12}) + (r.sub.empty() ? 0.f : 10.f + tw(r.sub, FontId{11})));
    f.labelW = std::min(lw, b.w * 0.4f);
    float ew = 0.f;
    for (const auto& r : data_.rows) ew = std::max(ew, tw(r.end, gfx::semibold(FontId{12})) + (r.warn ? 18.f : 0.f));
    f.x0 = b.x + f.labelW + 16.f;
    f.x1 = std::max(f.x0 + 40.f, b.right() - std::min(ew + 12.f, b.w * 0.25f));
    f.from = static_cast<double>(data_.from);
    f.to = static_cast<double>(data_.to > data_.from ? data_.to : data_.from + 1);
    const int n = std::max(1, itemCount());
    const float todayRoom = data_.today ? 16.f : 0.f;
    // Peu de place : la legende s'en va d'abord ; une ligne garde la hauteur de son texte (ce qui ne tient pas n'est
    // pas dessine).
    if (f.legendHeight > 0.f && (b.h - f.legendHeight - todayRoom - 18.f) / static_cast<float>(n) < 20.f) f.legendHeight = 0.f;
    f.top = b.y + f.legendHeight + todayRoom;
    f.rowH = std::clamp((b.bottom() - f.top - 18.f) / static_cast<float>(n), 16.f, 34.f);
    return f;
}

void Timeline::onPaint(const PaintContext& ctx) {
    gfx::IRenderer& r = ctx.r;
    const colors::Ink k = colors::ink(ctx.theme);
    const Rect b = contentRect();
    const Frame f = frame();
    const int n = itemCount();
    if (f.legendHeight > 0.f) paintLegend(r, ctx.theme, b.x, b.y, b.w, data_.legend);
    // Les lignes qui tiennent (l'axe sous la derniere).
    int shownRows = n;
    while (shownRows > 0 && f.top + f.rowH * static_cast<float>(shownRows) > b.bottom() - 18.f) --shownRows;
    const float bottom = f.top + f.rowH * static_cast<float>(shownRows);
    // L'axe : les annees et les milieux d'annee (<< juil. >>).
    int y0, m0, d0, y1, m1, d1;
    fr::civil(data_.from, y0, m0, d0);
    fr::civil(data_.to, y1, m1, d1);
    // Les milieux d'annee seulement s'il y a la place (une annee : au moins 90 px).
    const double yearPx = 365.0 * static_cast<double>(f.x1 - f.x0) / std::max(1.0, f.to - f.from);
    const bool mid = yearPx >= 90.0;
    float lastRight = -1e9f;
    for (int y = y0; y <= y1; ++y)
        for (const int m : {1, 7}) {
            const fr::Day dd = fr::day(y, m, 1);
            if (dd < data_.from || dd > data_.to || (m == 7 && !mid)) continue;
            const float x = std::round(f.xAt(static_cast<double>(dd)));
            r.fillRect({x, f.top, 1.f, bottom - f.top}, m == 1 ? k.base : k.grid);
            const std::string l = m == 1 ? std::to_string(y) : fr::monthShort(m);
            const float lw = tw(l, FontId{11});
            if (x - lw * 0.5f < lastRight + 6.f || x + lw * 0.5f > b.right()) continue;
            text(r, x, bottom + 9.f, l, FontId{11}, m == 1 ? k.muted : k.faint, 0);
            lastRight = x + lw * 0.5f;
        }
    const bool live = active_ >= 0 && ((hovered() && !keyboard_) || (focused() && keyboard_));
    const int shown = live ? active_ : example_;
    const FontId sf = gfx::semibold(FontId{12});
    const float bh = std::clamp(f.rowH - 8.f, 6.f, 12.f);
    for (int i = 0; i < shownRows; ++i) {
        const auto& row = data_.rows[static_cast<std::size_t>(i)];
        const float cy = f.top + f.rowH * (static_cast<float>(i) + 0.5f);
        if (i == shown) r.fillRoundedRect({b.x - 4.f, cy - f.rowH * 0.5f + 1.f, b.w + 8.f, f.rowH - 2.f}, k.hover, 6.f);
        const Color lc = row.faded ? k.faint : k.ink;
        const std::string lab = fit(r, row.label, FontId{12}, f.labelW);
        text(r, b.x, cy, lab, FontId{12}, lc);
        if (!row.sub.empty()) text(r, b.x + tw(lab, FontId{12}) + 10.f, cy, row.sub, FontId{11}, row.faded ? k.faint : k.muted);
        float first = f.x1, last = f.x0;
        for (const auto& s : row.segments) {
            const float a = std::max(f.x0, f.xAt(static_cast<double>(s.from))) + 1.f;
            const float e = std::min(f.x1, f.xAt(static_cast<double>(s.to))) - 1.f;
            if (e <= a) continue;
            Color c = colors::onTheme(keyColor(ownKeys_ ? &keys_ : nullptr, s.key), k);
            if (row.faded) c = colors::mix(c, k.surface, 0.55f);
            r.fillRoundedRect({a, cy - bh * 0.5f, e - a, bh}, c, std::min(4.f, bh * 0.5f));
            first = std::min(first, a);
            last = std::max(last, e);
        }
        if (!row.lead.empty()) text(r, first - 8.f, cy, row.lead, gfx::semibold(FontId{11}), k.accentInk, 1);
        if (!row.end.empty()) {
            float x = last + 8.f;
            if (row.warn) {
                drawIcon(r, Icon::Warning, {x, cy - 7.f, 14.f, 14.f}, ctx.theme.color.warning);
                x += 18.f;
            }
            const std::string e = fit(r, row.end, row.faded ? FontId{11} : sf, b.right() - x);
            text(r, x, cy, e, row.faded ? FontId{11} : sf, row.warn ? ctx.theme.color.warning : row.faded ? k.faint : k.ink);
        }
    }
    if (data_.today >= data_.from && data_.today <= data_.to && data_.today) {
        const float x = std::round(f.xAt(static_cast<double>(data_.today)));
        r.fillRect({x - 0.5f, f.top - 14.f, 1.5f, bottom - f.top + 14.f}, k.ink);
        text(r, x + 4.f, f.top - 8.f, data_.todayLabel, gfx::semibold(FontId{11}), k.ink);
    }
    if (shownRows > 0) paintFocus(ctx, {f.x0, f.top, f.x1 - f.x0, bottom - f.top});
}

std::pair<int, int> Timeline::itemAt(Point p) const {
    const Frame f = frame();
    const Rect b = contentRect();
    if (p.x < b.x || p.x > b.right()) return {-1, -1};
    const int i = static_cast<int>(std::floor((p.y - f.top) / f.rowH));
    if (i < 0 || i >= itemCount()) return {-1, -1};
    // Le morceau sous la souris (la cible : toute la hauteur de la ligne).
    int seg = -1;
    const auto& row = data_.rows[static_cast<std::size_t>(i)];
    for (std::size_t s = 0; s < row.segments.size(); ++s) {
        const float a = f.xAt(static_cast<double>(row.segments[s].from)), e = f.xAt(static_cast<double>(row.segments[s].to));
        if (p.x >= a - 1.f && p.x <= e + 1.f) seg = static_cast<int>(s);
    }
    return {i, seg};
}

Pick Timeline::pickAt(Point p) const {
    const auto [i, s] = itemAt(p);
    Pick pk;
    if (i < 0) return pk;
    pk.index = i;
    pk.series = s;
    if (s >= 0) pk.key = data_.rows[static_cast<std::size_t>(i)].segments[static_cast<std::size_t>(s)].key;
    return pk;
}

Tip Timeline::tipFor(int index, int sub) const {
    Tip t;
    if (index < 0 || index >= itemCount()) return t;
    const auto& row = data_.rows[static_cast<std::size_t>(index)];
    t.title = row.label + (row.sub.empty() ? std::string() : std::string(" ") + fr::kDot + " " + row.sub);
    for (std::size_t s = 0; s < row.segments.size(); ++s) {
        const auto& seg = row.segments[s];
        const std::string what = seg.tip.empty() ? std::string() : seg.tip;
        t.rows.push_back({fr::ddmmyyyy(seg.from) + " " + fr::kArrow + " " + fr::ddmmyyyy(seg.to), what,
                          keyColor(ownKeys_ ? &keys_ : nullptr, seg.key), false});
        (void)sub;
    }
    if (!row.end.empty()) t.extra = row.end;
    t.click = data_.clickHint;
    return t;
}

Rect Timeline::tipAnchor(int index, int) const {
    const Frame f = frame();
    const auto& row = data_.rows[static_cast<std::size_t>(std::clamp(index, 0, std::max(0, itemCount() - 1)))];
    float last = f.x0;
    for (const auto& s : row.segments) last = std::max(last, std::min(f.x1, f.xAt(static_cast<double>(s.to))));
    return {f.x0, f.top + f.rowH * static_cast<float>(index), last - f.x0, f.rowH};
}

// =============================================================================
//  Sparkline
// =============================================================================
void paintSparkline(gfx::IRenderer& r, const Theme& theme, const Rect& box, const std::vector<double>& values, const std::vector<double>* compare,
                    const SparkStyle& style) {
    const std::size_t n = values.size();
    if (n == 0 || box.w < 8.f || box.h < 6.f) return;
    const colors::Ink k = colors::ink(theme);
    double m = std::isfinite(style.yMax) ? style.yMax : 0.0;
    if (!std::isfinite(style.yMax)) {
        for (const double v : values)
            if (std::isfinite(v)) m = std::max(m, v);
        if (compare)
            for (const double v : *compare)
                if (std::isfinite(v)) m = std::max(m, v);
    }
    if (!(m > 0.0)) m = 1.0;
    const float x0 = box.x + 3.f, x1 = box.right() - 5.f, yb = box.bottom() - 4.f, yt = box.y + 6.f;
    const auto pts = [&](const std::vector<double>& v) {
        std::vector<Point> out;
        const std::size_t c = v.size();
        for (std::size_t i = 0; i < c; ++i) {
            if (!std::isfinite(v[i])) continue;
            const float x = c > 1 ? x0 + (x1 - x0) * static_cast<float>(i) / static_cast<float>(c - 1) : (x0 + x1) * 0.5f;
            out.push_back({x, yb - static_cast<float>(std::clamp(v[i] / m, 0.0, 1.0)) * (yb - yt)});
        }
        return out;
    };
    if (compare && !compare->empty()) strokePath(r, pts(*compare), colors::onTheme(colors::kCompare, k), 1.5f);
    const auto p = pts(values);
    const Color c = colors::onTheme(style.color, k);
    if (style.area && p.size() > 1) fillUnder(r, p, yb, c.withAlpha(26));
    if (p.size() > 1) strokePath(r, p, c, style.width);
    if (style.endDot && !p.empty()) paintDot(r, p.back(), 3.5f, c, k.surface);
}

Sparkline::Sparkline(std::string id) : ChartBase(std::move(id)) {}

void Sparkline::setValues(std::vector<double> values, std::vector<double> compare, SparkStyle style) {
    values_ = std::move(values);
    compare_ = std::move(compare);
    style_ = style;
    if (active_ >= itemCount()) active_ = -1;
    invalidate();
}

void Sparkline::setLabels(std::vector<std::string> labels, Format format) {
    labels_ = std::move(labels);
    format_ = std::move(format);
}

SizeHint Sparkline::sizeHint() const {
    SizeHint h;
    h.preferred = {96.f, 30.f};
    h.minimum = {40.f, 16.f};
    return h;
}

void Sparkline::onPaint(const PaintContext& ctx) {
    const Rect b = contentRect();
    paintSparkline(ctx.r, ctx.theme, b, values_, compare_.empty() ? nullptr : &compare_, style_);
    const bool live = active_ >= 0 && ((hovered() && !keyboard_) || (focused() && keyboard_));
    if (live && active_ < itemCount()) {
        const colors::Ink k = colors::ink(ctx.theme);
        const Rect a = tipAnchor(active_, -1);
        ctx.r.fillRect({std::round(a.x), b.y + 2.f, 1.f, b.h - 4.f}, k.rule);
    }
    paintFocus(ctx, b);
}

std::pair<int, int> Sparkline::itemAt(Point p) const {
    const Rect b = contentRect();
    if (!b.contains(p) || values_.empty()) return {-1, -1};
    return {nearestIndex(p.x, b.x + 3.f, b.right() - 5.f, itemCount()), -1};
}

Pick Sparkline::pickAt(Point p) const {
    const auto [i, s] = itemAt(p);
    Pick pk;
    (void)s;
    if (i < 0) return pk;
    pk.index = i;
    pk.value = values_[static_cast<std::size_t>(i)];
    return pk;
}

Tip Sparkline::tipFor(int index, int) const {
    Tip t;
    if (index < 0 || index >= itemCount()) return t;
    const std::size_t ui = static_cast<std::size_t>(index);
    t.title = ui < labels_.size() ? labels_[ui] : std::string();
    const Format f = format_ ? format_ : Format([](double v) { return fr::integer(v); });
    t.rows.push_back({f(values_[ui]), {}, style_.color, true});
    if (ui < compare_.size() && std::isfinite(compare_[ui])) t.rows.push_back({f(compare_[ui]), "avant", colors::kCompare, true});
    return t;
}

Rect Sparkline::tipAnchor(int index, int) const {
    const Rect b = contentRect();
    const int n = itemCount();
    const float x = n > 1 ? b.x + 3.f + (b.w - 8.f) * static_cast<float>(index) / static_cast<float>(n - 1) : b.x + b.w * 0.5f;
    return {x, b.y, 0.f, b.h};
}

// =============================================================================
//  SmallMultiples
// =============================================================================
SmallMultiples::SmallMultiples(std::string id, int columns) : Widget(std::move(id)), columns_(std::max(1, columns)) {}

Widget& SmallMultiples::add(std::string title, std::string aside, WidgetPtr chart) {
    Widget& w = addChild(std::move(chart));
    cells_.push_back({std::move(title), std::move(aside), &w});
    invalidateLayout();
    return w;
}

void SmallMultiples::setColumns(int columns) {
    columns_ = std::max(1, columns);
    invalidateLayout();
}

void SmallMultiples::setSharedScale(bool shared) {
    shared_ = shared;
    invalidateLayout();
}

Rect SmallMultiples::cellRect(int i) const {
    const Rect b = contentRect();
    const int n = std::max(1, cellCount());
    const int cols = std::min(columns_, n);
    const int rows = (n + cols - 1) / cols;
    const float gx = 18.f, gy = 14.f;
    const float cw = (b.w - gx * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const float ch = (b.h - gy * static_cast<float>(rows - 1)) / static_cast<float>(rows);
    return {b.x + static_cast<float>(i % cols) * (cw + gx), b.y + static_cast<float>(i / cols) * (ch + gy), cw, ch};
}

void SmallMultiples::onLayout() {
    // La meme echelle pour tous : le plus grand domaine des graphiques qui en ont un.
    double lo = 0.0, hi = 0.0;
    if (shared_)
        for (const auto& c : cells_) {
            if (const auto* t = dynamic_cast<const TimeChart*>(c.chart)) {
                const auto e = t->extent();
                lo = std::min(lo, e.first);
                hi = std::max(hi, e.second);
            } else if (const auto* d = dynamic_cast<const Dumbbell*>(c.chart)) {
                hi = std::max(hi, d->extent().second);
            }
        }
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const Rect r = cellRect(static_cast<int>(i));
        cells_[i].chart->setBounds({r.x, r.y + 20.f, r.w, std::max(10.f, r.h - 20.f)});
        if (auto* t = dynamic_cast<TimeChart*>(cells_[i].chart)) t->setSharedDomain(shared_ ? lo : kNone, shared_ ? hi : kNone);
        else if (auto* d = dynamic_cast<Dumbbell*>(cells_[i].chart)) d->setSharedDomain(shared_ ? lo : kNone, shared_ ? hi : kNone);
    }
}

void SmallMultiples::onPaint(const PaintContext& ctx) {
    const colors::Ink k = colors::ink(ctx.theme);
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const Rect r = cellRect(static_cast<int>(i));
        const float aw = tw(cells_[i].aside, FontId{11});
        text(ctx.r, r.x, r.y + 8.f, fit(ctx.r, cells_[i].title, gfx::semibold(FontId{12}), r.w - aw - 8.f), gfx::semibold(FontId{12}), k.ink);
        text(ctx.r, r.right(), r.y + 8.f, cells_[i].aside, FontId{11}, k.muted, 1);
    }
}

// =============================================================================
//  ChartCard
// =============================================================================
ChartCard::ChartCard(std::string id, std::string title, std::string small) : Widget(std::move(id)), title_(std::move(title)), small_(std::move(small)) {}

void ChartCard::setTitle(std::string title, std::string small) {
    title_ = std::move(title);
    small_ = std::move(small);
    invalidateLayout();
}

void ChartCard::setValue(std::string value, Delta delta, std::string compare) {
    value_ = std::move(value);
    delta_ = std::move(delta);
    compare_ = std::move(compare);
    invalidateLayout();
}

void ChartCard::setSide(std::vector<SideRow> rows, float width) {
    side_ = std::move(rows);
    sideW_ = width;
    invalidateLayout();
}

Widget& ChartCard::setChart(WidgetPtr chart) {
    if (chart_) removeChild(*chart_);
    chart_ = &addChild(std::move(chart));
    invalidateLayout();
    return *chart_;
}

namespace {
constexpr float kCardPadX = 14.f, kCardPadTop = 11.f, kCardPadBottom = 10.f;
FontId cardTitleFont() { return gfx::semibold(FontId{13}); }
FontId cardValueFont() { return gfx::semibold(FontId{19}); }
float sideWidth(const std::vector<SideRow>& rows, float wanted, float cardW) {
    if (rows.empty()) return 0.f;
    if (wanted > 0.f) return std::min(wanted, cardW * 0.5f);
    float w = 0.f;
    for (const auto& r : rows) w = std::max(w, 17.f + tw(r.label, FontId{12}) + 12.f + tw(r.value, gfx::semibold(FontId{12})) + (r.faint.empty() ? 0.f : 5.f + tw(r.faint, FontId{11})));
    return std::min(w, cardW * 0.5f);
}
} // namespace

Rect ChartCard::chartRect() const {
    const Rect b = bounds();
    float y = b.y + kCardPadTop + 18.f;
    if (!value_.empty()) y += 28.f;
    const float sw = sideWidth(side_, sideW_, b.w - 2.f * kCardPadX);
    const float w = b.w - 2.f * kCardPadX - (sw > 0.f ? sw + 14.f : 0.f);
    return {b.x + kCardPadX, y + 4.f, std::max(10.f, w), std::max(10.f, b.bottom() - kCardPadBottom - y - 4.f)};
}

void ChartCard::onLayout() {
    if (chart_) chart_->setBounds(chartRect());
}

void ChartCard::onPaint(const PaintContext& ctx) {
    gfx::IRenderer& r = ctx.r;
    const colors::Ink k = colors::ink(ctx.theme);
    const Rect b = bounds();
    if (b.empty()) return;
    // La carte : blanche, coins de 12, un trait, une ombre a peine.
    r.fillRoundedRect({b.x, b.y + 1.f, b.w, b.h + 1.f}, ctx.theme.brand.cardShadow, 12.f);
    shapes::fillFramedRect(r, b, k.surface, k.line, 12.f);
    const float x = b.x + kCardPadX, right = b.right() - kCardPadX;
    float y = b.y + kCardPadTop;
    // Le titre et sa precision.
    const FontId tf = cardTitleFont();
    const std::string title = fit(r, title_, tf, right - x);
    text(r, x, y + 9.f, title, tf, k.ink);
    if (!small_.empty()) {
        const float sx = x + r.measure(title, tf).width + 6.f;
        text(r, sx, y + 9.5f, fit(r, small_, FontId{11}, right - sx), FontId{11}, k.faint);
    }
    y += 18.f;
    // La valeur, l'ecart, ce a quoi on compare.
    if (!value_.empty()) {
        const FontId vf = cardValueFont();
        const float vh = r.lineHeight(vf);
        const std::string value = fit(r, value_, vf, right - x);
        r.drawText({x, std::round(y + 2.f)}, value, vf, k.ink);
        float cx = x + r.measure(value, vf).width + 7.f;
        const float base = y + 2.f + vh - r.lineHeight(FontId{12}) - 2.f;
        // L'ecart s'il tient en entier (sinon rien : la valeur passe avant), puis ce a quoi on compare, coupe.
        if (!delta_.text.empty() && cx + deltaWidth(delta_) <= right) cx += paintDelta(r, ctx.theme, cx, base, delta_) + 6.f;
        if (!compare_.empty()) r.drawText({std::round(cx), std::round(base)}, fit(r, compare_, FontId{12}, right - cx), FontId{12}, k.muted);
        y += 28.f;
    }
    // Les valeurs a cote (la legende avec ses chiffres).
    const float sw = sideWidth(side_, sideW_, b.w - 2.f * kCardPadX);
    if (sw > 0.f) {
        const Rect cr = chartRect();
        const float sx = right - sw;
        const float rowH = std::max(18.f, std::min(24.f, cr.h / static_cast<float>(std::max<std::size_t>(1, side_.size()))));
        float sy = cr.y + std::max(0.f, (cr.h - rowH * static_cast<float>(side_.size())) * 0.5f);
        for (std::size_t i = 0; i < side_.size(); ++i) {
            const SideRow& row = side_[i];
            if (sy + rowH > b.bottom() - 4.f) break;   // ce qui ne tient pas dans la carte n'est pas dessine
            const float cy = sy + rowH * 0.5f;
            float lx = sx;
            if (row.color.a) {
                const Color c = colors::onTheme(row.color, k);
                if (row.line) r.fillRoundedRect({lx, cy - 1.f, 10.f, 2.f}, c, 1.f);
                else r.fillRoundedRect({lx, cy - 4.5f, 9.f, 9.f}, c, 2.5f);
            }
            lx += 17.f;
            float vx = right;
            if (!row.faint.empty()) {
                text(r, vx, cy, row.faint, FontId{11}, k.faint, 1);
                vx -= tw(row.faint, FontId{11}) + 5.f;
            }
            text(r, vx, cy, row.value, gfx::semibold(FontId{12}), row.total ? k.ink2 : k.ink, 1);
            vx -= tw(row.value, gfx::semibold(FontId{12})) + 8.f;
            text(r, lx, cy, fit(r, row.label, FontId{12}, vx - lx), FontId{12}, row.total ? k.muted : k.ink2);
            if (i + 1 < side_.size()) r.fillRect({sx, sy + rowH - 0.5f, sw, 1.f}, k.dark ? k.grid : Color::rgb(0xF1F4F8));
            sy += rowH;
        }
    }
}

} // namespace ui::charts
