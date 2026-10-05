// =============================================================================
//  ui/widgets/DataViews.cpp
// -----------------------------------------------------------------------------
//  Every paint routine here walks only the rows that intersect the viewport.
//  That is the whole reason a 59 000-row table renders in the same time as a
//  50-row one.
// =============================================================================
#include "DataViews.hpp"
#include "PropertyGridHooks.hpp"   // 1.11 (integration I111, lien 5)
#include "ColorPalette.hpp"
#include "ExprField.hpp"         // 1.10 (chantier K) : les champs a expression, partout pareils
#include "ExprMarkers.hpp"       // 1.10.3 : les reperes ($Vanne$) d'une case se voient
#include "TableFilters.hpp"     // lot recherche : la fenetre du filtre d'une colonne
#include "TrailSymbols.hpp"     // 1.11 (chantier T3, C4) : les symboles traces au trait

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <string_view>
#include <unordered_map>   // 1.11 (R111) : keepExpanded apparie les categories par nom

namespace ui {
namespace {

// Draws text truncated with an ellipsis when it does not fit the cell.
void drawClipped(const PaintContext& ctx, gfx::Point at, std::string_view text,
                 gfx::FontId font, gfx::Color color, float maxWidth) {
    if (maxWidth <= 0.f || text.empty()) return;
    const auto fits = ctx.r.fitCharacters(text, font, maxWidth);
    if (fits >= text.size()) { ctx.r.drawText(at, text, font, color); return; }
    // "..." and not the ellipsis character: the debug font covers Latin-1 only
    // and renders U+2026 as its "unsupported glyph" block.
    // La coupe recule au debut d'un caractere : couper un « é » en deux laisse
    // un octet orphelin, que la police dessine en carre.
    std::size_t keep = fits > 3 ? fits - 3 : 0;
    while (keep > 0 && (static_cast<unsigned char>(text[keep]) & 0xC0) == 0x80) --keep;
    std::string cut(text.substr(0, keep));
    cut += "...";
    ctx.r.drawText(at, cut, font, color);
}

// ---- l'etat d'une ligne, dessine pareil dans les trois vues ----------------
//
// Survol : un voile a peine visible, arrondi, en retrait de quatre pixels - il
// suit la souris sans rien affirmer. Selection : la meme forme, pleine, et
// quand la vue a le clavier une barre d'accent a gauche : c'est ce qui dit OU
// vont aller les fleches. Sans elle, deux listes cote a cote ont chacune une
// ligne bleue, et on ne sait pas laquelle ecoute.
void paintRowState(const PaintContext& ctx, const gfx::Rect& r, bool selected, bool hover,
                   bool keyboard, bool current) {
    const auto& th = ctx.theme;
    const float radius = th.metric.radius > 0.f ? 4.f : 0.f;
    const gfx::Rect in{r.x + 3.f, r.y + 1.f, std::max(0.f, r.w - 6.f), std::max(0.f, r.h - 2.f)};
    if (selected) {
        ctx.r.fillRoundedRect(in, th.color.selectionBg, radius);
        if (keyboard)
            ctx.r.fillRoundedRect({in.x + 1.f, in.y + 4.f, 3.f, std::max(0.f, in.h - 8.f)},
                                  th.color.accent, 1.5f);
    } else if (hover) {
        ctx.r.fillRoundedRect(in, th.brand.hover, radius);
    }
    // Le curseur clavier sur une ligne NON selectionnee (Ctrl+fleches en
    // selection multiple) : un cadre, pour qu'il ne se perde pas.
    if (keyboard && current && !selected) ctx.r.strokeRect(in, th.brand.focusRing, 1.f);
}

// Le cadre d'une vue qui a le clavier. Un trait d'accent au lieu du gris : la
// seule facon de savoir, au clavier, ou l'on est.
void paintFrame(const PaintContext& ctx, const gfx::Rect& b, bool focused) {
    if (focused) ctx.r.strokeRect(b, ctx.theme.brand.focusRing, 1.5f);
    else         ctx.r.strokeRect(b, ctx.theme.color.border, 1.f);
}

gfx::Color iconColourOf(const Theme& th, const CellStyle& st, bool selected) {
    if (st.iconTone != Tone::None) return th.tone(st.iconTone, th.color.textMuted);
    return st.iconColor.value_or(selected ? th.color.selectionText
                                          : st.fg.value_or(th.color.textMuted));
}

gfx::Color textColourOf(const Theme& th, const CellStyle& st, bool selected) {
    if (selected) return th.color.selectionText;
    if (st.fgTone != Tone::None) return th.onSurface(th.tone(st.fgTone, th.color.text));
    return st.fg.value_or(th.color.text);
}

// La pastille de droite. Rend la largeur qu'elle prend, marge comprise, pour
// que le texte s'arrete avant elle.
float drawBadge(const PaintContext& ctx, const gfx::Rect& row, const CellStyle& st) {
    if (st.badge.empty()) return 0.f;
    const auto& th = ctx.theme;
    const auto font = th.font.caption;
    const float lh = ctx.r.lineHeight(font);
    const float h = std::min(std::max(0.f, row.h - 6.f), lh + 4.f);
    const float w = ctx.r.measure(st.badge, font).width + 14.f;
    const gfx::Rect pill{row.right() - w - 8.f, row.y + (row.h - h) * 0.5f, w, h};
    const bool toned = st.badgeTone != Tone::None;
    const auto col = th.tone(st.badgeTone, th.color.textMuted);
    if (toned) {
        ctx.r.fillRoundedRect(pill, col.withAlpha(th.isDark() ? 56 : 34), h * 0.5f);
    } else {
        // Neutre : un trait et un fond discret. Un fond plein de la couleur
        // des bordures devenait, en fort contraste, du blanc sous du gris clair.
        ctx.r.fillRoundedRect(pill, th.color.border, h * 0.5f);
        ctx.r.fillRoundedRect({pill.x + 1.f, pill.y + 1.f, pill.w - 2.f, pill.h - 2.f},
                              th.color.rowAltBg, h * 0.5f - 1.f);
    }
    ctx.r.drawText({pill.x + 7.f, pill.y + (h - lh) * 0.5f}, st.badge, font,
                   toned ? th.onSurface(col) : th.color.textMuted);
    return w + 12.f;
}

// Lot API 2 : les pastilles a icone, a gauche de la pastille de texte (qui
// prend deja `rightReserved`). Rend la largeur prise, marge comprise.
float drawPips(const PaintContext& ctx, const gfx::Rect& row, const CellStyle& st, float rightReserved, bool selected) {
    if (st.pips.empty()) return 0.f;
    const auto& th = ctx.theme;
    const float side = std::min(std::max(0.f, row.h - 6.f), 18.f);
    const float gap = 3.f;
    float x = row.right() - 8.f - rightReserved - side;
    for (std::size_t k = st.pips.size(); k-- > 0;) {
        const auto& pip = st.pips[k];
        const gfx::Rect box{x, row.y + (row.h - side) * 0.5f, side, side};
        const auto col = th.tone(pip.tone, th.color.textMuted);
        if (pip.on) {
            ctx.r.fillRoundedRect(box, col.withAlpha(th.isDark() ? 64 : 40), 4.f);
            drawIcon(ctx.r, pip.icon, {box.x + 3.f, box.y + 3.f, box.w - 6.f, box.h - 6.f},
                     selected ? th.color.selectionText : th.onSurface(col));
        } else {
            drawIcon(ctx.r, pip.icon, {box.x + 3.f, box.y + 3.f, box.w - 6.f, box.h - 6.f},
                     th.color.textMuted.withAlpha(46));
        }
        x -= side + gap;
    }
    return static_cast<float>(st.pips.size()) * (side + gap) + 4.f;
}

// Le faux gras : il n'y a pas de face grasse (uiBold == ui), et un texte
// "gras" identique au texte courant ne hierarchise rien.
void drawMaybeBold(const PaintContext& ctx, gfx::Point at, std::string_view text,
                   gfx::FontId font, gfx::Color colour, float maxWidth, bool bold) {
    drawClipped(ctx, at, text, font, colour, maxWidth);
    if (bold) drawClipped(ctx, {at.x + 0.6f, at.y}, text, font, colour, maxWidth - 0.6f);
}

// 1.9 : le melange de deux couleurs (t de 0 a 1), opaque.
gfx::Color mixColour(gfx::Color a, gfx::Color b, float t) {
    const auto m = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::clamp(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t, 0.f, 255.f));
    };
    return gfx::Color{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), 255};
}

// 1.9 : UNE PASTILLE DE COULEUR (le mode d'un parametre : REF, COPIE, LES
// DEUX), posee a `x` et centree dans la ligne `row`, sur un fond `under`. Rend
// sa largeur (sans marge) ; 0 si elle ne tient pas. Contour : un trait de sa
// couleur, le fond translucide pose sur `under` (le renderer n'a pas de
// contour arrondi). Pleine (fillTo) : un degrade en bandes de 2 px.
float drawColorPill(const PaintContext& ctx, float x, const gfx::Rect& row, const ColorPill& p,
                    float maxW, gfx::Color under) {
    if (p.text.empty()) return 0.f;
    const auto& th = ctx.theme;
    const auto font = th.font.caption;
    const float lh = ctx.r.lineHeight(font);
    const float h = std::min(std::max(0.f, row.h - 8.f), lh + 2.f);
    const float w = ctx.r.measure(p.text, font).width + 14.f;
    if (w > maxW || h <= 4.f) return 0.f;
    const gfx::Rect box{x, row.y + (row.h - h) * 0.5f, w, h};
    const float radius = h * 0.5f;
    if (p.fillTo) {
        ctx.r.fillRoundedRect({box.x, box.y, 2.f * radius, h}, p.fill, radius);
        ctx.r.fillRoundedRect({box.right() - 2.f * radius, box.y, 2.f * radius, h}, *p.fillTo, radius);
        const float x0 = box.x + radius, x1 = box.right() - radius;
        for (float sx = x0; sx < x1; sx += 2.f) {
            const float t = (sx - x0) / std::max(1.f, x1 - x0);
            ctx.r.fillRect({sx, box.y, std::min(2.f, x1 - sx), h}, mixColour(p.fill, *p.fillTo, t));
        }
    } else {
        const float alpha = static_cast<float>(p.fill.a) / 255.f;
        ctx.r.fillRoundedRect(box, p.border, radius);
        ctx.r.fillRoundedRect({box.x + 1.f, box.y + 1.f, box.w - 2.f, box.h - 2.f},
                              mixColour(under, p.fill, alpha), radius - 1.f);
    }
    const gfx::Color ink = p.fillTo ? p.ink : th.onSurface(p.ink);
    drawMaybeBold(ctx, {box.x + 7.f, box.y + (h - lh) * 0.5f}, p.text, font, ink, w - 10.f, true);
    return w;
}

// 1.9 : une case a cocher de 14 px (la bascule de la grille, la case d'une table).
void drawCheckBox(const PaintContext& ctx, const gfx::Rect& box, bool on) {
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(box, on ? c.accent : c.inputBg);
    ctx.r.strokeRect(box, on ? c.accent : c.border, 1.f);
    if (on) {
        ctx.r.line({box.x + 3.f, box.y + 7.f}, {box.x + 6.f, box.y + 10.f}, c.selectionText, 2.f);
        ctx.r.line({box.x + 6.f, box.y + 10.f}, {box.x + 11.f, box.y + 4.f}, c.selectionText, 2.f);
    }
}

// Un chevron plutot qu'une boite +/- : la boite est un heritage de Windows 95,
// et elle se lit comme un bouton de plus sur chaque ligne.
void drawChevron(const PaintContext& ctx, gfx::Point c, bool expanded, gfx::Color colour) {
    const float s = 3.5f;
    if (expanded) {
        ctx.r.line({c.x - s, c.y - s * 0.5f}, {c.x, c.y + s * 0.5f}, colour, 1.5f);
        ctx.r.line({c.x, c.y + s * 0.5f}, {c.x + s, c.y - s * 0.5f}, colour, 1.5f);
    } else {
        ctx.r.line({c.x - s * 0.5f, c.y - s}, {c.x + s * 0.5f, c.y}, colour, 1.5f);
        ctx.r.line({c.x + s * 0.5f, c.y}, {c.x - s * 0.5f, c.y + s}, colour, 1.5f);
    }
}

template <class T>
void toggleSelection(std::vector<T>& sel, T value, bool additive) {
    if (!additive) { sel.clear(); sel.push_back(value); return; }
    const auto it = std::find(sel.begin(), sel.end(), value);
    if (it == sel.end()) sel.push_back(value); else sel.erase(it);
}

} // namespace

// ---- 1.11 (chantier T3, C4, tranche 10) : LES QUATRE SYMBOLES SONT DESSINES ----
// ✓ ✕ ⊘ ⇩ ne sont pas ecrits avec la police : l'interface n'a qu'une police,
// sans repli (platform/FontAtlas) ; DejaVu Sans les a, mais rien ne garantit
// que la police trouvee sur le poste (Segoe UI sous Windows) les ait : on
// verrait la boite de remplacement a leur place. Ils sont donc traces au trait,
// plus epais que le texte, dans la couleur de leur ton ; le reste (« 2 », « / »,
// « · 2 non generes ») est ecrit, en gras.
// Tranche 11 : sortis de l'espace anonyme (TrailSymbols.hpp) ; les infobulles
// les tracent aussi (WidgetHost::paintTooltip), donc la legende du « ? ».
namespace trailsym {

Sym at(std::string_view s, std::size_t pos) {
    if (pos + 3 > s.size()) return Sym::None;
    const auto u = s.substr(pos, 3);
    if (u == "\xE2\x9C\x93") return Sym::Check;
    if (u == "\xE2\x9C\x95") return Sym::Cross;
    if (u == "\xE2\x8A\x98") return Sym::Ban;
    if (u == "\xE2\x87\xA9") return Sym::Down;
    return Sym::None;
}

bool contains(std::string_view s) {
    for (std::size_t k = 0; k + 3 <= s.size(); ++k)
        if (at(s, k) != Sym::None) return true;
    return false;
}

Tone toneOf(Sym sym, bool struck) {
    switch (sym) {
    case Sym::Check: return Tone::Ok;
    case Sym::Cross: return Tone::Error;
    case Sym::Ban:   return Tone::Muted;
    case Sym::Down:  return struck ? Tone::Warning : Tone::Info;   // comme ViewModels.cpp (buildTrail)
    case Sym::None:  break;
    }
    return Tone::None;
}

void drawSymbol(gfx::IRenderer& r, Sym sym, float x, float cy, float side, gfx::Color col) {
    const float h = side, w = side, y = cy - side * 0.5f;
    const float t = std::max(2.f, side * 0.2f);           // le trait : plus epais que le texte, pour se voir
    switch (sym) {
    case Sym::Check:
        r.line({x + w * 0.08f, y + h * 0.55f}, {x + w * 0.38f, y + h * 0.86f}, col, t);
        r.line({x + w * 0.38f, y + h * 0.86f}, {x + w * 0.94f, y + h * 0.16f}, col, t);
        break;
    case Sym::Cross:
        r.line({x + w * 0.14f, y + h * 0.14f}, {x + w * 0.86f, y + h * 0.86f}, col, t);
        r.line({x + w * 0.86f, y + h * 0.14f}, {x + w * 0.14f, y + h * 0.86f}, col, t);
        break;
    case Sym::Ban: {
        const float cx = x + w * 0.5f, rad = side * 0.42f;
        constexpr int kSeg = 20;
        for (int k = 0; k < kSeg; ++k) {
            const float a0 = 6.2831853f * static_cast<float>(k) / kSeg, a1 = 6.2831853f * static_cast<float>(k + 1) / kSeg;
            r.line({cx + rad * std::cos(a0), cy + rad * std::sin(a0)}, {cx + rad * std::cos(a1), cy + rad * std::sin(a1)}, col, t * 0.85f);
        }
        r.line({cx - rad * 0.7f, cy + rad * 0.7f}, {cx + rad * 0.7f, cy - rad * 0.7f}, col, t * 0.85f);
        break;
    }
    case Sym::Down:
        r.line({x + w * 0.5f, y + h * 0.08f}, {x + w * 0.5f, y + h * 0.88f}, col, t);
        r.line({x + w * 0.16f, y + h * 0.55f}, {x + w * 0.5f, y + h * 0.9f}, col, t);
        r.line({x + w * 0.84f, y + h * 0.55f}, {x + w * 0.5f, y + h * 0.9f}, col, t);
        break;
    case Sym::None: break;
    }
}

// La largeur d'une icone de fin de ligne (symboles traces + texte), et son trace.
float width(gfx::IRenderer& r, std::string_view text, gfx::FontId font, float side) {
    float w = 0.f;
    std::size_t runStart = 0;
    for (std::size_t pos = 0; pos < text.size();) {
        if (at(text, pos) != Sym::None) {
            if (pos > runStart) w += r.measure(text.substr(runStart, pos - runStart), font).width;
            w += side + 2.f;
            pos += 3;
            if (text.substr(pos, kStruck.size()) == kStruck) pos += kStruck.size();
            runStart = pos;
        } else {
            ++pos;
        }
    }
    if (text.size() > runStart) w += r.measure(text.substr(runStart), font).width;
    return w;
}

void draw(gfx::IRenderer& r, std::string_view text, gfx::FontId font, float x, float textY, float cy, float side,
          gfx::Color col, const Theme* own) {
    std::size_t runStart = 0;
    auto flush = [&](std::size_t end) {
        if (end <= runStart) return;
        const auto run = text.substr(runStart, end - runStart);
        r.drawText({x, textY}, run, font, col);
        x += r.measure(run, font).width;
    };
    for (std::size_t pos = 0; pos < text.size();) {
        if (const auto sym = at(text, pos); sym != Sym::None) {
            flush(pos);
            pos += 3;
            const bool struck = text.substr(pos, kStruck.size()) == kStruck;
            if (struck) pos += kStruck.size();
            const auto symCol = own ? own->tone(toneOf(sym, struck), col) : col;
            drawSymbol(r, sym, x + 1.f, cy, side, symCol);
            if (struck)            // barre, comme l'arbre (TreeView : ⇩ barre, non genere)
                r.line({x - 1.f, cy + side * 0.45f}, {x + side + 3.f, cy - side * 0.45f}, symCol, 1.5f);
            x += side + 2.f;
            runStart = pos;
        } else {
            ++pos;
        }
    }
    flush(text.size());
}

} // namespace trailsym

// ============================================================== ListView ====
ListView::ListView(std::string id) : Widget(std::move(id)) { setFocusPolicy(true); }

void ListView::setModel(std::shared_ptr<IListModel> m) {
    model_ = std::move(m);
    selection_.clear();
    scrollY_ = 0.f;
    invalidate();
}

int ListView::rowAt(float globalY) const {
    const auto area = contentRect();
    const int r = static_cast<int>((globalY - area.y + scrollY_) / rowHeight_);
    return (r < 0 || !model_ || r >= static_cast<int>(model_->rowCount())) ? -1 : r;
}

void ListView::onPaint(const PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto  area = contentRect();
    const float rowH = rowHeight_ = ctx.theme.metric.rowHeight;

    ctx.r.fillRect(bounds(), c.panelBg);
    paintFrame(ctx, bounds(), focused());
    if (!model_ || rowH <= 0.f) return;

    const auto total = model_->rowCount();
    const auto first = static_cast<std::size_t>(std::max(0.f, scrollY_ / rowH));
    const auto last  = std::min(total, first + static_cast<std::size_t>(area.h / rowH) + 1);

    ctx.r.pushClip(area);
    for (auto i = first; i < last; ++i) {
        const auto row = static_cast<RowIndex>(i);
        const gfx::Rect r{area.x, area.y + static_cast<float>(i) * rowH - scrollY_, area.w, rowH};
        const bool selected = std::find(selection_.begin(), selection_.end(), row) != selection_.end();
        paintRowState(ctx, r, selected, hovered() && hoverRow_ == static_cast<int>(i),
                      focused(), false);

        const auto st = model_->style(row);
        float textX = r.x + 8.f;
        if (st.icon != Icon::None) {
            const float side = std::min(rowH - 6.f, 16.f);
            drawIcon(ctx.r, st.icon, {textX, r.y + (rowH - side) * 0.5f, side, side},
                     iconColourOf(ctx.theme, st, selected));
            textX += side + 6.f;
        } else if (st.image16 && st.image16->size() == 16u * 16u * 4u) {
            // Lot API 6 : l'icone d'un projet, en 16 x 16.
            const float oy = std::floor(r.y + (rowH - 16.f) * 0.5f);
            const auto& px = *st.image16;
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    const auto* p = &px[static_cast<std::size_t>((y * 16 + x) * 4)];
                    if (p[3] == 0) continue;
                    ctx.r.fillRect({textX + static_cast<float>(x), oy + static_cast<float>(y), 1.f, 1.f}, gfx::Color{p[0], p[1], p[2], p[3]});
                }
            textX += 22.f;
        }
        const float badge = drawBadge(ctx, r, st);
        const auto font = st.monospace ? ctx.theme.font.mono : ctx.theme.font.ui;
        drawMaybeBold(ctx, {textX, r.y + (rowH - ctx.r.lineHeight(font)) * 0.5f},
                      model_->text(row), font, textColourOf(ctx.theme, st, selected),
                      r.right() - textX - 6.f - badge, st.bold);
    }
    ctx.r.popClip();
}

EventResult ListView::onEvent(const InputEvent& ev) {
    if (!model_) return EventResult::Ignored;

    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        const int h = bounds().contains(m->pos) ? rowAt(m->pos.y) : -1;
        if (h != hoverRow_) { hoverRow_ = h; invalidate(); }
        return EventResult::Ignored;
    }

    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        if (!bounds().contains(w->pos)) return EventResult::Ignored;
        const float maxScroll =
            std::max(0.f, static_cast<float>(model_->rowCount()) * rowHeight_ - contentRect().h);
        scrollY_ = std::clamp(scrollY_ - w->dy * rowHeight_ * 3.f, 0.f, maxScroll);
        invalidate();
        return EventResult::Consumed;
    }
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (!bounds().contains(d->pos)) return EventResult::Ignored;
        grabFocus();
        const int r = rowAt(d->pos.y);
        if (r >= 0) {
            const auto row = static_cast<RowIndex>(r);
            if (selectionMode_ != SelectionMode::None) {
                toggleSelection(selection_, row,
                                selectionMode_ != SelectionMode::Single && d->mods.ctrl);
                selectionChanged->emit(row);
            }
            if (d->clickCount >= 2) activated->emit(row);
            invalidate();
        }
        return EventResult::Consumed;
    }
    if (const auto* k = std::get_if<KeyDown>(&ev); k && focused() && k->key == Key::Return
                                                    && !selection_.empty()) {
        activated->emit(selection_.front());
        return EventResult::Consumed;
    }
    return EventResult::Ignored;
}

// ============================================================== TreeView ====
TreeView::TreeView(std::string id) : Widget(std::move(id)) {
    setFocusPolicy(true);
    setPadding({2.f, 2.f, 2.f, 2.f});
}

void TreeView::setModel(std::shared_ptr<ITreeModel> m) {
    modelConnections_.clear();
    model_ = std::move(m);
    expanded_.clear();
    selection_.clear();
    current_ = kInvalidNode;
    scrollY_ = 0.f;
    if (model_) {
        modelConnections_ += model_->childrenReady->connect([this](NodeId) { rebuildVisibleRows(); });
        modelConnections_ += model_->modelReset->connect([this] {
            expanded_.clear();
            rebuildVisibleRows();
        });
        expanded_.push_back(model_->root());
        std::sort(expanded_.begin(), expanded_.end());
    }
    rebuildVisibleRows();
}

void TreeView::setShowRootNode(bool s) { showRoot_ = s; rebuildVisibleRows(); }

// ---- Lot API 8 : l'arbre du projet ----
void TreeView::setHighlight(std::string_view query) {
    if (query == treeHighlight_.text()) return;
    treeHighlight_ = SearchQuery(query);
    invalidate();
}

void TreeView::setExpandedNodes(std::vector<NodeId> nodes) {
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    expanded_ = std::move(nodes);
    if (model_)
        for (const auto n : expanded_)
            if (!model_->isLoaded(n)) model_->fetchChildren(n);
    animNode_ = kInvalidNode;             // pas de fondu : tout arrive d'un coup
    rebuildVisibleRows();
}

int TreeView::visibleDepth(NodeId n) const noexcept {
    for (const auto& r : rows_)
        if (r.node == n) return static_cast<int>(r.depth);
    return -1;
}

bool TreeView::chipRect(NodeId n, std::size_t chip, gfx::Rect& out) const {
    for (const auto& h : chipHits_)
        if (h.node == n && h.chip == chip) {
            out = h.rect;
            return true;
        }
    return false;
}

std::vector<NodeId> TreeView::visibleAncestors(NodeId n) const {
    std::vector<NodeId> out;
    std::size_t i = 0;
    while (i < rows_.size() && rows_[i].node != n) ++i;
    if (i == rows_.size()) return out;
    auto depth = rows_[i].depth;
    while (i-- > 0 && depth > 0)
        if (rows_[i].depth < depth) {
            depth = rows_[i].depth;
            out.insert(out.begin(), rows_[i].node);
        }
    return out;
}

// 2e partie : la couleur d'un domaine (CellStyle::domain), prise du theme.
static gfx::Color treeDomainColour(const Theme& th, std::uint8_t domain) {
    switch (domain) {
    case 1: return th.color.accent;
    case 2: return th.color.info;
    case 3: return th.color.ok;
    case 4: return th.color.syntaxKeyword;
    default: return th.color.textMuted;
    }
}
// ---- fin Lot API 8 : l'arbre du projet ----

void TreeView::setFilter(std::function<bool(NodeId)> predicate) {
    filter_ = std::move(predicate);
    rebuildVisibleRows();
}

void TreeView::expand(NodeId n, bool recursive) {
    if (!model_) return;
    if (!std::binary_search(expanded_.begin(), expanded_.end(), n)) {
        expanded_.insert(std::lower_bound(expanded_.begin(), expanded_.end(), n), n);
        if (!model_->isLoaded(n)) model_->fetchChildren(n);
        animNode_  = n;          // ses enfants arriveront en fondu
        animStart_ = -1.0;
    }
    if (recursive)
        for (std::size_t i = 0; i < model_->childCount(n); ++i) expand(model_->childAt(n, i), true);
    rebuildVisibleRows();
}

void TreeView::collapse(NodeId n) {
    const auto it = std::lower_bound(expanded_.begin(), expanded_.end(), n);
    if (it != expanded_.end() && *it == n) expanded_.erase(it);
    rebuildVisibleRows();
}

void TreeView::toggle(NodeId n) {
    std::binary_search(expanded_.begin(), expanded_.end(), n) ? collapse(n) : expand(n);
}

void TreeView::expandToDepth(int depth) {
    if (!model_) return;
    expanded_.clear();
    // Breadth-first so the expansion cost is bounded by what is actually shown.
    std::vector<std::pair<NodeId, int>> queue{{model_->root(), 0}};
    while (!queue.empty()) {
        const auto [node, d] = queue.back();
        queue.pop_back();
        if (d > depth) continue;
        expanded_.push_back(node);
        for (std::size_t i = 0; i < model_->childCount(node); ++i)
            queue.emplace_back(model_->childAt(node, i), d + 1);
    }
    std::sort(expanded_.begin(), expanded_.end());
    rebuildVisibleRows();
}

void TreeView::expandToDepth(int depth, const std::function<bool(NodeId)>& stop) {
    if (!model_) return;
    if (!stop) { expandToDepth(depth); return; }
    expanded_.clear();
    std::vector<std::pair<NodeId, int>> queue{{model_->root(), 0}};
    while (!queue.empty()) {
        const auto [node, d] = queue.back();
        queue.pop_back();
        if (d > depth) continue;
        if (d > 0 && stop(node)) continue;       // lot API 7 : replie, et rien dessous
        expanded_.push_back(node);
        for (std::size_t i = 0; i < model_->childCount(node); ++i)
            queue.emplace_back(model_->childAt(node, i), d + 1);
    }
    std::sort(expanded_.begin(), expanded_.end());
    rebuildVisibleRows();
}

void TreeView::ensureVisible(NodeId n) {
    const auto it = std::find_if(rows_.begin(), rows_.end(),
                                 [n](const VisualRow& r) { return r.node == n; });
    if (it == rows_.end()) return;
    const float rowH = rowHeight_;
    const float y = static_cast<float>(it - rows_.begin()) * rowH;
    const float h = contentRect().h;
    if (h < rowH) { pendingVisible_ = n; return; }
    pendingVisible_ = kInvalidNode;
    if (y < scrollY_)            scrollY_ = y;
    else if (y + rowH > scrollY_ + h) scrollY_ = y + rowH - h;
    // Lot API 8 : l'arbre du projet - le titre colle en haut couvre la premiere
    // ligne : une ligne montree en haut descend d'un cran (un clic de script,
    // les fleches, tomberaient sinon sur le titre).
    if (hasHeads_ && y > 0.f && y < scrollY_ + rowH && h >= 2.f * rowH) scrollY_ = std::max(0.f, y - rowH);
    invalidate();
}

void TreeView::rebuildVisibleRows() {
    rows_.clear();
    if (!model_) { invalidate(); return; }

    // Iterative depth-first walk of the expanded set only. A collapsed branch
    // costs one childCount() call, never a full traversal.
    struct Frame { NodeId node; std::uint16_t depth; };
    std::vector<Frame> stack;

    auto keep = [this](NodeId n) { return !filter_ || filter_(n); };

    const NodeId root = model_->root();
    if (showRoot_) stack.push_back({root, 0});
    else
        for (std::size_t i = model_->childCount(root); i > 0; --i)
            stack.push_back({model_->childAt(root, i - 1), 0});

    while (!stack.empty()) {
        const auto f = stack.back();
        stack.pop_back();

        const bool expandable = model_->hasChildren(f.node);
        const bool isExpanded = std::binary_search(expanded_.begin(), expanded_.end(), f.node);
        if (keep(f.node)) rows_.push_back(VisualRow{f.node, f.depth, expandable, isExpanded});

        if (expandable && isExpanded)
            for (std::size_t i = model_->childCount(f.node); i > 0; --i)
                stack.push_back({model_->childAt(f.node, i - 1),
                                 static_cast<std::uint16_t>(f.depth + 1)});
    }
    invalidate();
}

std::vector<NodeId> TreeView::visibleNodes() const {
    std::vector<NodeId> out;
    out.reserve(rows_.size());
    for (const auto& r : rows_) out.push_back(r.node);
    return out;
}

bool TreeView::rowRect(NodeId n, gfx::Rect& out) const {
    const auto it = std::find_if(rows_.begin(), rows_.end(),
                                 [n](const VisualRow& r) { return r.node == n; });
    if (it == rows_.end()) return false;
    const auto area = contentRect();
    const float y = area.y + static_cast<float>(it - rows_.begin()) * rowHeight_ - scrollY_;
    if (y < area.y || y + rowHeight_ > area.bottom()) return false;
    // Lot API 8 : l'arbre du projet - sous le titre colle en haut, la ligne ne se voit pas.
    if (stickyRow_ >= 0 && it - rows_.begin() != stickyRow_ && y < stickyY_ + rowHeight_ - 1.f) return false;
    out = {area.x, y, area.w, rowHeight_};
    return true;
}

int TreeView::rowAt(float globalY) const {
    const auto area = contentRect();
    // ---- Lot API 8 : l'arbre du projet (le titre colle en haut : c'est lui qu'on touche) ----
    if (stickyRow_ >= 0 && static_cast<std::size_t>(stickyRow_) < rows_.size() && globalY >= stickyY_
        && globalY < stickyY_ + rowHeight_)
        return stickyRow_;
    // ---- fin Lot API 8 ----
    const int r = static_cast<int>((globalY - area.y + scrollY_) / rowHeight_);
    return (r < 0 || r >= static_cast<int>(rows_.size())) ? -1 : r;
}

void TreeView::onLayout() {
    const float maxScroll =
        std::max(0.f, static_cast<float>(rows_.size()) * rowHeight_ - contentRect().h);
    scrollY_ = std::clamp(scrollY_, 0.f, maxScroll);
    if (pendingVisible_ != kInvalidNode && contentRect().h >= rowHeight_) {
        const NodeId n = pendingVisible_;
        pendingVisible_ = kInvalidNode;
        ensureVisible(n);
    }
}

void TreeView::onPaint(const PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto  area = contentRect();
    // Lot API 8 : l'arbre du projet - la densite (rowOverride_) passe avant le theme.
    const float rowH = rowHeight_ = rowOverride_ > 0.f ? rowOverride_ : ctx.theme.metric.rowHeight;
    const float indent = indent_ = ctx.theme.metric.indentPerLevel;

    ctx.r.fillRect(bounds(), c.panelBg);
    paintFrame(ctx, bounds(), focused());
    if (!model_ || rowH <= 0.f) return;

    const auto first = static_cast<std::size_t>(std::max(0.f, scrollY_ / rowH));
    const auto last  = std::min(rows_.size(), first + static_cast<std::size_t>(area.h / rowH) + 1);
    const float textY = (rowH - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f;
    const auto guide = c.border.withAlpha(ctx.theme.isDark() ? 120 : 150);

    chipHits_.clear();                    // lot API 8 : l'arbre du projet (les boutons de ce dessin)
    actionHits_.clear();                  // lot API 8 : l'arbre du projet (2e partie)
    dotHits_.clear();
    stickyRow_ = -1;
    ctx.r.pushClip(area);
    for (auto i = first; i < last; ++i) {
        const auto& vr = rows_[i];
        const gfx::Rect r{area.x, area.y + static_cast<float>(i) * rowH - scrollY_, area.w, rowH};
        const bool selected = std::find(selection_.begin(), selection_.end(), vr.node) != selection_.end();

        paintRowState(ctx, r, selected, hovered() && hoverRow_ == static_cast<int>(i) && !dragging_,
                      focused(), vr.node == current_);

        // Lot API 8 : l'arbre du projet - le style d'abord (la couleur du domaine
        // teint les guides, la barre de la ligne choisie, le titre du domaine).
        const auto st = model_->style(vr.node);
        const auto domainCol = treeDomainColour(ctx.theme, st.domain);
        if (st.domainHead) hasHeads_ = true;
        if (st.domainHead && st.domain != 0)
            ctx.r.fillRect({r.x + 1.f, r.y + 2.f, 3.f, rowH - 4.f}, domainCol);
        else if (selected && st.domain != 0)
            ctx.r.fillRoundedRect({r.x + 4.f, r.y + 5.f, 3.f, std::max(0.f, rowH - 10.f)}, domainCol, 1.5f);

        // Les guides d'indentation : un trait fin par niveau, qui dit a quel
        // parent appartient une ligne quand la liste est longue.
        for (std::uint16_t d = 0; d < vr.depth; ++d) {
            const float gx = r.x + 4.f + static_cast<float>(d) * indent + 6.f;
            ctx.r.line({gx, r.y}, {gx, r.bottom()}, st.domain != 0 && d > 0 ? domainCol.withAlpha(ctx.theme.isDark() ? 90 : 110) : guide, 1.f);
        }

        const float x = r.x + 4.f + static_cast<float>(vr.depth) * indent;
        if (vr.expandable)
            drawChevron(ctx, {x + 6.f, r.y + rowH * 0.5f}, vr.expanded,
                        hovered() && hoverRow_ == static_cast<int>(i) ? c.text : c.textMuted);

        float textX = x + 18.f;
        if (st.icon != Icon::None) {
            const float side = std::min(rowH - 6.f, 16.f);
            drawIcon(ctx.r, st.icon, {textX, r.y + (rowH - side) * 0.5f, side, side},
                     st.domainHead && st.domain != 0 ? domainCol : iconColourOf(ctx.theme, st, selected));
            textX += side + 6.f;
        } else if (st.customIcon != 0 && iconPainter_) {
            // Lot API 2 : une icone de l'hote (le glyphe de famille d'une macro).
            const float side = std::min(rowH - 6.f, 16.f);
            iconPainter_(ctx.r, st.customIcon, {textX, r.y + (rowH - side) * 0.5f, side, side},
                         iconColourOf(ctx.theme, st, selected));
            textX += side + 6.f;
        }
        float badge = drawBadge(ctx, r, st);
        if (!st.pill.empty()) {                            // lot API 8 : l'arbre du projet (la 2e pastille)
            CellStyle ps;
            ps.badge = st.pill;
            ps.badgeTone = st.pillTone;
            badge += drawBadge(ctx, {r.x, r.y, std::max(0.f, badge > 0.f ? r.w - badge + 8.f : r.w), r.h}, ps) - (badge > 0.f ? 4.f : 0.f);
        }
        badge += drawPips(ctx, r, st, badge, selected);   // lot API 2
        // ---- 1.11 (chantier T3, C4) : les icones compilable / generable (et leur infobulle) ----
        if (!st.trail.empty()) {
            const auto trailFont = ctx.theme.font.uiBold;     // tranche 10 : en gras, pour se voir
            const float lh = ctx.r.lineHeight(trailFont);
            const float side = std::clamp(lh * 0.8f, 10.f, 16.f);   // les symboles traces
            float tx = r.right() - 8.f - badge;
            for (std::size_t k = st.trail.size(); k-- > 0;) {
                const auto& t = st.trail[k];
                const float w = trailsym::width(ctx.r, t.glyph, trailFont, side);
                tx -= w;
                const auto col = selected ? c.selectionText : ctx.theme.tone(t.tone, c.textMuted);
                trailsym::draw(ctx.r, t.glyph, trailFont, tx, r.y + (rowH - lh) * 0.5f, r.y + rowH * 0.5f, side, col);
                if (t.struck)            // ⇩ barre : non genere
                    ctx.r.line({tx - 1.f, r.y + rowH * 0.5f + lh * 0.35f}, {tx + w + 1.f, r.y + rowH * 0.5f - lh * 0.35f}, col, 1.5f);
                dotHits_.push_back({vr.node, k, {tx - 2.f, r.y, w + 4.f, rowH}, t.tip});
                tx -= 6.f;
            }
            badge = r.right() - 8.f - tx;
        }
        // ---- fin 1.11 (C4) ----
        // ---- Lot API 8 : l'arbre du projet (une rangee de boutons a la place du texte) ----
        if (!st.chips.empty()) {
            const auto chipFont = ctx.theme.font.smallUi;
            const float ch = std::min(rowH - 4.f, 20.f);
            const float cy = r.y + (rowH - ch) * 0.5f;
            const float lh = ctx.r.lineHeight(chipFont);
            float need = 0.f;
            for (const auto& chip : st.chips) need += 32.f + ctx.r.measure(chip.label, chipFont).width + 4.f;
            const bool labels = need <= r.right() - 6.f - textX;
            const auto iconCol = iconColourOf(ctx.theme, st, false);
            const bool hotRow = hovered() && hoverRow_ == static_cast<int>(i);
            float cx = textX;
            for (std::size_t k = 0; k < st.chips.size(); ++k) {
                const auto& chip = st.chips[k];
                const float cw = labels ? 32.f + ctx.r.measure(chip.label, chipFont).width : 26.f;
                if (cx + cw > r.right() - 2.f) break;
                const gfx::Rect cr{cx, cy, cw, ch};
                ctx.r.fillRoundedRect(cr, hotRow ? c.textMuted : c.border, ch * 0.5f);
                ctx.r.fillRoundedRect({cr.x + 1.f, cr.y + 1.f, cr.w - 2.f, cr.h - 2.f}, c.panelBg, ch * 0.5f - 1.f);
                drawIcon(ctx.r, chip.icon, {cx + (labels ? 8.f : 7.f), cy + (ch - 12.f) * 0.5f, 12.f, 12.f}, iconCol);
                if (labels) ctx.r.drawText({cx + 24.f, cy + (ch - lh) * 0.5f}, chip.label, chipFont, c.textMuted);
                chipHits_.push_back({vr.node, k, cr, chip.label});
                cx += cw + 4.f;
            }
            continue;
        }
        // ---- fin Lot API 8 ----
        // ---- Lot API 8 : l'arbre du projet (le texte garde : surligne, suivi de l'indication) ----
        const std::string nodeText = model_->text(vr.node);
        const auto nodeFont = st.bold ? ctx.theme.font.uiBold : ctx.theme.font.ui;
        const float nodeRoom = r.right() - textX - 4.f - badge;
        if (!treeHighlight_.empty() && nodeRoom > 0.f)
            for (const auto& [a, b] : treeHighlight_.ranges(nodeText)) {
                const float x0 = textX + ctx.r.measure(std::string_view(nodeText).substr(0, a), nodeFont).width;
                if (x0 >= textX + nodeRoom) break;
                const float x1 = std::min(textX + nodeRoom, textX + ctx.r.measure(std::string_view(nodeText).substr(0, b), nodeFont).width);
                if (x1 > x0) ctx.r.fillRoundedRect({x0 - 1.f, r.y + 3.f, x1 - x0 + 2.f, rowH - 6.f}, gfx::Color{230, 180, 60, 110}, 2.f);
            }
        // ---- fin Lot API 8 ----
        drawMaybeBold(ctx, {textX, r.y + textY}, nodeText, nodeFont,
                      textColourOf(ctx.theme, st, selected),
                      nodeRoom, st.bold);
        // ---- Lot API 8 : l'arbre du projet (le point "modifie depuis V47", apres le nom) ----
        const bool dot = st.dotTone != Tone::None;
        if (dot) {
            const float tw = std::min(ctx.r.measure(nodeText, nodeFont).width, std::max(0.f, nodeRoom - 12.f));
            const gfx::Rect dr{textX + tw + 5.f, r.y + (rowH - 7.f) * 0.5f, 7.f, 7.f};
            ctx.r.fillRoundedRect(dr, ctx.theme.tone(st.dotTone, c.warning), 3.5f);
            dotHits_.push_back({vr.node, 0, {dr.x - 3.f, r.y, dr.w + 6.f, rowH}, st.dotTip});
        }
        // ---- fin Lot API 8 ----
        // ---- Lot API 8 : l'arbre du projet (l'indication grise : ou est un resultat) ----
        if (!st.hint.empty()) {
            const float used = ctx.r.measure(nodeText, nodeFont).width + 10.f + (dot ? 10.f : 0.f);
            if (used < nodeRoom - 24.f)
                drawClipped(ctx, {textX + used, r.y + textY}, st.hint, ctx.theme.font.ui,
                            selected ? textColourOf(ctx.theme, st, selected) : c.textMuted, nodeRoom - used);
        }
        // ---- fin Lot API 8 ----
        // ---- Lot API 8 : l'arbre du projet (les actions au survol : epingler, detacher, ...) ----
        if (!hoverActions_.empty() && hovered() && hoverRow_ == static_cast<int>(i) && !dragging_ && !st.domainHead) {
            const float side = std::min(rowH - 4.f, 20.f);
            const float span = static_cast<float>(hoverActions_.size()) * (side + 2.f) + 6.f;
            float ax = r.right() - span;
            if (ax > textX + 40.f) {
                ctx.r.fillRoundedRect({ax - 2.f, r.y + 2.f, span, rowH - 4.f}, selected ? c.selectionBg : c.panelBg, 4.f);
                ax += 2.f;
                for (std::size_t k = 0; k < hoverActions_.size(); ++k) {
                    const gfx::Rect ar{ax, r.y + (rowH - side) * 0.5f, side, side};
                    drawIcon(ctx.r, hoverActions_[k].icon, {ar.x + (side - 12.f) * 0.5f, ar.y + (side - 12.f) * 0.5f, 12.f, 12.f},
                             selected ? c.selectionText : c.textMuted);
                    actionHits_.push_back({vr.node, k, ar, hoverActions_[k].label});
                    ax += side + 2.f;
                }
            }
        }
        // ---- fin Lot API 8 ----

        // ---- le glisser-deposer, dessine ---------------------------------
        //
        //  La ligne qu'on traine s'efface a moitie, et un trait dit OU ca
        //  tombera. Sans le trait, "avant" et "apres" se ressemblent, et on
        //  depose une fois sur deux du mauvais cote - ce qui, dans un ordre
        //  d'execution, est un defaut qu'on ne voit qu'a l'essai.
        if (dragging_ && (vr.node == pressed_ || std::find(dragNodes_.begin(), dragNodes_.end(), vr.node) != dragNodes_.end())) {
            auto voile = c.panelBg;
            voile.a = 140;
            ctx.r.fillRect(r, voile);
        }
        if (dragging_ && dropAllowed_ && vr.node == dropTarget_) {
            const float x0 = r.x + 4.f + static_cast<float>(vr.depth) * indent;
            if (dropWhere_ == DropWhere::Into) {
                ctx.r.strokeRect({x0, r.y + 1.f, r.right() - x0 - 4.f, rowH - 2.f},
                                 c.accent, 1.f);
            } else {
                const float y = dropWhere_ == DropWhere::Before ? r.y : r.y + rowH;
                // Deux pixels, pas un : un trait d'un pixel se perd entre deux
                // lignes serrees, et c'est le seul repere du geste.
                ctx.r.fillRect({x0, y - 1.f, r.right() - x0 - 4.f, 2.f}, c.accent);
                ctx.r.fillRect({x0, y - 4.f, 2.f, 8.f}, c.accent);
            }
        }
    }

    // ---- le depliage, en fondu ------------------------------------------------
    //  Les enfants du noeud qu'on vient d'ouvrir sont recouverts d'un voile qui
    //  s'efface en un sept-centieme de seconde : l'oeil voit d'ou ils sortent.
    if (animNode_ != kInvalidNode) {
        if (animStart_ < 0.0) animStart_ = ctx.time;
        const float p = std::clamp(static_cast<float>((ctx.time - animStart_) * 1000.0)
                                   / std::max(1.f, ctx.theme.motion.pageFadeMs), 0.f, 1.f);
        const auto at = std::find_if(rows_.begin(), rows_.end(),
                                     [this](const VisualRow& vr) { return vr.node == animNode_; });
        if (p >= 1.f || at == rows_.end()) {
            animNode_ = kInvalidNode;
        } else {
            const auto parent = static_cast<std::size_t>(at - rows_.begin());
            std::size_t end = parent + 1;
            while (end < rows_.size() && rows_[end].depth > at->depth) ++end;
            if (end > parent + 1) {
                const float y0 = area.y + static_cast<float>(parent + 1) * rowH - scrollY_;
                const float y1 = area.y + static_cast<float>(end) * rowH - scrollY_;
                ctx.r.fillRect({area.x, y0, area.w, y1 - y0},
                               c.panelBg.withAlpha(static_cast<std::uint8_t>((1.f - p) * 220.f)));
            }
            invalidate();
        }
    }
    // ---- Lot API 8 : l'arbre du projet (le titre du domaine reste colle en haut) ----
    //  Le titre (domainHead) de la premiere ligne montree, s'il est sorti par le
    //  haut : redessine en haut, par-dessus ; le titre suivant le pousse.
    if (scrollY_ > 0.5f && first < rows_.size()) {
        int head = -1;
        std::uint16_t minDepth = static_cast<std::uint16_t>(rows_[first].depth + 1);
        for (std::size_t j = first + 1; j-- > 0;) {                 // first, first - 1, ..., 0 : ses ancetres
            if (rows_[j].depth >= minDepth) continue;
            minDepth = rows_[j].depth;
            if (model_->style(rows_[j].node).domainHead) { head = static_cast<int>(j); break; }
            if (minDepth == 0) break;
        }
        const float headY = head >= 0 ? area.y + static_cast<float>(head) * rowH - scrollY_ : area.y;
        if (head >= 0 && headY < area.y - 0.5f) {
            float sy = area.y;
            if (first + 1 < rows_.size() && model_->style(rows_[first + 1].node).domainHead)
                sy = std::min(sy, area.y + static_cast<float>(first + 1) * rowH - scrollY_ - rowH);
            const auto& hv = rows_[static_cast<std::size_t>(head)];
            const auto hs = model_->style(hv.node);
            const auto col = treeDomainColour(ctx.theme, hs.domain);
            const gfx::Rect r{area.x, sy, area.w, rowH};
            ctx.r.fillRect(r, c.panelBg);
            if (hovered() && hoverRow_ == head) paintRowState(ctx, r, false, true, false, false);
            ctx.r.line({r.x, r.bottom() - 0.5f}, {r.right(), r.bottom() - 0.5f}, c.border, 1.f);
            if (hs.domain != 0) ctx.r.fillRect({r.x + 1.f, r.y + 2.f, 3.f, rowH - 4.f}, col);
            const float x = r.x + 4.f + static_cast<float>(hv.depth) * indent;
            if (hv.expandable) drawChevron(ctx, {x + 6.f, r.y + rowH * 0.5f}, hv.expanded, c.textMuted);
            float tx = x + 18.f;
            if (hs.icon != Icon::None) {
                const float side = std::min(rowH - 6.f, 16.f);
                drawIcon(ctx.r, hs.icon, {tx, r.y + (rowH - side) * 0.5f, side, side},
                         hs.domain != 0 ? col : iconColourOf(ctx.theme, hs, false));
                tx += side + 6.f;
            }
            float b = drawBadge(ctx, r, hs);
            if (!hs.pill.empty()) {
                CellStyle ps;
                ps.badge = hs.pill;
                ps.badgeTone = hs.pillTone;
                b += drawBadge(ctx, {r.x, r.y, std::max(0.f, b > 0.f ? r.w - b + 8.f : r.w), r.h}, ps);
            }
            drawMaybeBold(ctx, {tx, r.y + textY}, model_->text(hv.node), hs.bold ? ctx.theme.font.uiBold : ctx.theme.font.ui,
                          c.text, r.right() - tx - 4.f - b, hs.bold);
            stickyRow_ = head;
            stickyY_ = sy;
        }
    }
    // ---- fin Lot API 8 ----
    ctx.r.popClip();
}

EventResult TreeView::onEvent(const InputEvent& ev) {
    if (!model_) return EventResult::Ignored;
    const float rowH = rowHeight_;      // whatever the last paint used
    const float indent = indent_;

    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        if (!bounds().contains(w->pos)) return EventResult::Ignored;
        const float maxScroll = std::max(0.f, static_cast<float>(rows_.size()) * rowH - contentRect().h);
        scrollY_ = std::clamp(scrollY_ - w->dy * rowH * 3.f, 0.f, maxScroll);
        invalidate();
        return EventResult::Consumed;
    }
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (!bounds().contains(d->pos)) return EventResult::Ignored;
        grabFocus();
        const int ri = rowAt(d->pos.y);
        if (ri < 0) return EventResult::Consumed;

        // Lot API 8 : l'arbre du projet - une COPIE : selectionChanged peut refaire
        // les lignes (un clic sur "Voir les N versions...") et rows_ se realloue.
        const auto vr = rows_[static_cast<std::size_t>(ri)];
        const float expanderX = contentRect().x + 4.f + static_cast<float>(vr.depth) * indent;

        // ---- Lot API 8 : l'arbre du projet (une action au survol : 2e partie) ----
        if (d->button == MouseButton::Left)
            for (const auto& h : actionHits_)
                if (h.node == vr.node && h.rect.contains(d->pos)) {
                    const auto node = h.node;
                    const auto action = h.chip;
                    hoverActionClicked->emit(node, action);
                    return EventResult::Consumed;
                }
        // ---- fin Lot API 8 ----
        // ---- Lot API 8 : l'arbre du projet (un bouton de la ligne) ----
        if (d->button == MouseButton::Left)
            for (const auto& h : chipHits_)
                if (h.node == vr.node && h.rect.contains(d->pos)) {
                    const auto node = h.node;
                    const auto chip = h.chip;
                    chipClicked->emit(node, chip);
                    return EventResult::Consumed;
                }
        // ---- fin Lot API 8 ----
        if (d->button == MouseButton::Right) {
            // Lot 7 : le clic droit choisit aussi le noeud (sans l'ouvrir, comme
            // l'explorateur de Windows) : on voit de quoi parle le menu, et F2 /
            // Suppr ensuite visent le meme. Dans une selection de plusieurs, elle reste.
            if (std::find(selection_.begin(), selection_.end(), vr.node) == selection_.end()) {
                current_ = vr.node;
                if (selectionMode_ != SelectionMode::None) selection_.assign(1, vr.node);
                invalidate();
            }
            contextMenuRequested->emit(vr.node, d->pos);
            return EventResult::Consumed;
        }
        if (vr.expandable && d->pos.x >= expanderX && d->pos.x <= expanderX + 14.f) {
            toggle(vr.node);
            return EventResult::Consumed;
        }
        current_ = vr.node;
        // Lot 21 : saisir un noeud d'une selection de plusieurs, pour les trainer
        // ensemble - elle ne se reduit qu'au relacher, sans glisser.
        const bool inSelection = std::find(selection_.begin(), selection_.end(), vr.node) != selection_.end();
        const bool keep = inSelection && selection_.size() > 1 && !d->mods.ctrl && d->clickCount == 1
                       && canDrag_ && canDrag_(vr.node);
        deferSelect_ = keep ? vr.node : kInvalidNode;
        if (!keep && selectionMode_ != SelectionMode::None)
            toggleSelection(selection_, vr.node,
                            selectionMode_ != SelectionMode::Single && d->mods.ctrl);
        selectionChanged->emit(vr.node);
        if (d->clickCount >= 2) {
            if (vr.expandable) toggle(vr.node);
            activated->emit(vr.node);
        }

        // On retient ce qui est saisi sans encore trainer : le geste ne devient
        // un deplacement qu'au-dela du seuil, plus bas. Decider ici ferait d'un
        // clic avec un doigt qui tremble un deplacement.
        pressed_   = (canDrag_ && canDrag_(vr.node)) ? vr.node : kInvalidNode;
        pressedAt_ = d->pos;
        dragging_  = false;
        // Lot 21 : ce qu'on traine - la selection (dans l'ordre de l'arbre) si le
        // noeud saisi en fait partie, sinon lui seul.
        dragNodes_.clear();
        if (pressed_ != kInvalidNode) {
            if (std::find(selection_.begin(), selection_.end(), pressed_) != selection_.end() && selection_.size() > 1)
                for (const auto& row : rows_)
                    if (std::find(selection_.begin(), selection_.end(), row.node) != selection_.end() && canDrag_(row.node))
                        dragNodes_.push_back(row.node);
            if (dragNodes_.empty()) dragNodes_.push_back(pressed_);
        }

        invalidate();
        return EventResult::Consumed;
    }
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        const int h = bounds().contains(m->pos) ? rowAt(m->pos.y) : -1;
        if (h != hoverRow_) { hoverRow_ = h; invalidate(); }
        if (pressed_ == kInvalidNode) return EventResult::Ignored;

        // Quatre pixels : assez pour absorber un tremblement, assez peu pour
        // que le deplacement parte quand on le veut.
        constexpr float kSeuil = 4.f;
        const float dx = m->pos.x - pressedAt_.x;
        const float dy = m->pos.y - pressedAt_.y;
        if (!dragging_ && dx * dx + dy * dy < kSeuil * kSeuil) return EventResult::Ignored;
        dragging_ = true;

        dropTarget_  = kInvalidNode;
        dropAllowed_ = false;
        const int ri = rowAt(m->pos.y);
        if (ri >= 0) {
            const auto& row = rows_[static_cast<std::size_t>(ri)];
            // Le tiers haut depose AVANT, le tiers bas APRES, le milieu DEDANS.
            // Sans les trois zones, on ne peut pas deposer en premiere position :
            // il n'y a rien au-dessus de la premiere ligne.
            const float top = contentRect().y + static_cast<float>(ri) * rowH - scrollY_;
            const float within = m->pos.y - top;
            DropWhere where = DropWhere::Into;
            if (within < rowH / 3.f)            where = DropWhere::Before;
            else if (within > rowH * 2.f / 3.f) where = DropWhere::After;

            const bool dragged = std::find(dragNodes_.begin(), dragNodes_.end(), row.node) != dragNodes_.end();
            if (row.node != pressed_ && !dragged && canDrop_) {
                // Lot 21 : DEDANS refuse (une feuille), le cote le plus proche.
                const DropWhere alt = within < rowH / 2.f ? DropWhere::Before : DropWhere::After;
                if (canDrop_(pressed_, row.node, where)) {
                    dropTarget_  = row.node;
                    dropWhere_   = where;
                    dropAllowed_ = true;
                } else if (where == DropWhere::Into && canDrop_(pressed_, row.node, alt)) {
                    dropTarget_  = row.node;
                    dropWhere_   = alt;
                    dropAllowed_ = true;
                }
            }
        }
        invalidate();
        return EventResult::Consumed;
    }
    if (const auto* u = std::get_if<MouseUp>(&ev)) {
        const bool wasDragging = dragging_;
        const auto source = pressed_;
        const auto target = dropTarget_;
        const auto where  = dropWhere_;
        const bool ok     = dropAllowed_;
        pressed_ = dropTarget_ = kInvalidNode;
        dragging_ = false;
        dropAllowed_ = false;
        // Lot 21 : un clic (sans glisser) sur une selection de plusieurs la reduit au noeud.
        if (!wasDragging && deferSelect_ != kInvalidNode) {
            selection_.assign(1, deferSelect_);
            selectionChanged->emit(deferSelect_);
        }
        deferSelect_ = kInvalidNode;
        invalidate();
        if (!wasDragging) return EventResult::Ignored;
        (void)u;
        if (ok && target != kInvalidNode) dropped->emit(source, target, where);
        return EventResult::Consumed;
    }
    if (const auto* k = std::get_if<KeyDown>(&ev); k && focused() && !rows_.empty()) {
        auto it = std::find_if(rows_.begin(), rows_.end(),
                               [this](const VisualRow& r) { return r.node == current_; });
        auto index = static_cast<std::size_t>(it == rows_.end() ? 0 : it - rows_.begin());

        switch (k->key) {
            case Key::Down:  index = std::min(rows_.size() - 1, index + 1); break;
            case Key::Up:    index = index ? index - 1 : 0; break;
            case Key::Home:  index = 0; break;
            case Key::End:   index = rows_.size() - 1; break;
            case Key::Right: if (rows_[index].expandable && !rows_[index].expanded) { expand(rows_[index].node); return EventResult::Consumed; } break;
            case Key::Left:  if (rows_[index].expanded) { collapse(rows_[index].node); return EventResult::Consumed; } break;
            case Key::Return: activated->emit(rows_[index].node); return EventResult::Consumed;
            // Lot 7 : F2 et Suppr sur le noeud courant - l'ecran decide ; rien de
            // branche, la touche passe comme avant.
            case Key::F2:
            case Key::Delete: {
                const auto& signal = k->key == Key::F2 ? renameRequested : deleteRequested;
                if (!k->mods.none() || signal->slotCount() == 0 || current_ == kInvalidNode) return EventResult::Ignored;
                signal->emit(current_);
                return EventResult::Consumed;
            }
            default: return EventResult::Ignored;
        }
        current_ = rows_[index].node;
        selection_.assign(1, current_);
        ensureVisible(current_);
        selectionChanged->emit(current_);
        return EventResult::Consumed;
    }
    return EventResult::Ignored;
}

void TreeView::cancelDrag() {
    pressed_ = dropTarget_ = kInvalidNode;
    dragging_ = false;
    dropAllowed_ = false;
    invalidate();
}

// ---- lot 7 : les infobulles relues tant qu'elles sont ouvertes -------------------
//  L'hote (WidgetMenu, WidgetHost) redemande le texte a chaque image : celui du
//  noeud ou de la ligne sous la souris, calcule MAINTENANT - pas celui du
//  moment ou la souris y est arrivee.
std::string TreeView::liveTooltip(gfx::Point mouse) const {
    // ---- Lot API 8 : l'arbre du projet (un bouton de la ligne : son libelle) ----
    for (const auto& h : chipHits_)
        if (h.rect.contains(mouse)) return h.label;
    for (const auto& h : actionHits_)                  // 2e partie : une action au survol, un point
        if (h.rect.contains(mouse)) return h.label;
    for (const auto& h : dotHits_)
        if (h.rect.contains(mouse) && !h.label.empty()) return h.label;
    // ---- fin Lot API 8 ----
    if (nodeTooltip_ && model_ && contentRect().contains(mouse)) {
        const int r = rowAt(mouse.y);
        if (r >= 0) {
            std::string tip = nodeTooltip_(rows_[static_cast<std::size_t>(r)].node);
            if (!tip.empty()) return tip;
        }
    }
    return Widget::liveTooltip(mouse);
}

std::string TableView::liveTooltip(gfx::Point mouse) const {
    // Une case refusee au collage : sa raison, qui ne bouge pas.
    if (cellTipShown_) return tooltip();
    if (model_ && hoverRow_ >= 0 && static_cast<std::size_t>(hoverRow_) < view_.size()) {
        const RowIndex row = view_[static_cast<std::size_t>(hoverRow_)];
        if (row < model_->rowCount()) {
            std::string tip = model_->rowTooltip(row);
            if (!tip.empty()) return tip;
        }
        // La ligne n'en a pas (ou plus) : celle de la table.
        return rowTooltipShown_ ? tableTooltip_ : tooltip();
    }
    return Widget::liveTooltip(mouse);
}

// ============================================================= TableView ====
TableView::TableView(std::string id) : Widget(std::move(id)) {
    setFocusPolicy(true);
    setPadding({0.f, 0.f, 0.f, 0.f});
    // Lot 20 : le menu du clic droit - copier, coller, tout choisir.
    auto menu = std::make_unique<PopupMenu>(this->id() + ".context");
    context_ = &static_cast<PopupMenu&>(addChild(std::move(menu)));
    // Le menu ferme, le clavier revient a la table (Echap, Ctrl+V...).
    contextLinks_ += context_->dismissed->connect([this] { grabFocus(); });
    contextLinks_ += context_->itemChosen->connect([this](int action) {
        grabFocus();
        switch (action) {
            case 1: (void)copySelection(true); break;
            case 2: (void)copySelection(false); break;
            case 3: (void)pasteFromClipboard(false); break;
            case 4: (void)pasteFromClipboard(true); break;
            case 5:
                if (selectionMode_ != SelectionMode::Single && selectionMode_ != SelectionMode::None) {
                    selection_ = view_;
                    selectionChanged->emit(selection_);
                    invalidate();
                }
                break;
            // Lot recherche : "Effacer les filtres des colonnes", "Filtrer la colonne X".
            case 99: clearColumnFilters(); break;
            default:
                if (action >= 100) (void)openColumnFilter(static_cast<std::size_t>(action - 100));
                break;
        }
    });
    // Lot recherche : la fenetre du filtre d'une colonne (l'entonnoir d'un titre).
    auto popup = std::make_unique<ColumnFilterPopup>(this->id() + ".filtre");
    filterPopup_ = &static_cast<ColumnFilterPopup&>(addChild(std::move(popup)));
    filterLinks_ += filterPopup_->applied->connect([this](const ColumnFilter& f) {
        grabFocus();
        setColumnFilter(f);
    });
    filterLinks_ += filterPopup_->cleared->connect([this](std::size_t column) {
        grabFocus();
        removeColumnFilter(column);
    });
    filterLinks_ += filterPopup_->dismissed->connect([this] { grabFocus(); });
}

// ---- lot 20 : copier, coller -------------------------------------------------------
namespace {
// Une case pour Excel : une tabulation, un retour ou un guillemet la mettent
// entre guillemets (les guillemets doubles), comme Excel l'ecrit lui-meme.
std::string tsvCell(const std::string& s) {
    if (s.find_first_of("\t\r\n\"") == std::string::npos) return s;
    std::string out = "\"";
    for (const char ch : s) {
        if (ch == '"') out += '"';
        out += ch;
    }
    out += '"';
    return out;
}
} // namespace

std::string TableView::copyText(bool withTitles) const {
    if (!model_) return {};
    std::vector<std::size_t> cols;
    for (std::size_t c = 0; c < columns_.size(); ++c)
        if (columns_[c].visible && c < model_->columnCount()) cols.push_back(c);
    std::string out;
    const auto line = [&](const std::function<std::string(std::size_t)>& cell) {
        for (std::size_t i = 0; i < cols.size(); ++i) {
            if (i) out += '\t';
            out += tsvCell(cell(cols[i]));
        }
        out += "\r\n";
    };
    if (withTitles) line([&](std::size_t c) { return columns_[c].title; });
    // L'ordre de la vue (le tri, le filtre), pas celui des clics.
    for (const RowIndex row : view_)
        if (std::find(selection_.begin(), selection_.end(), row) != selection_.end())
            line([&](std::size_t c) {
                std::string text = model_->cellText(row, c);
                // Une table en arbre : la case dit le nom seul (le retrait est un dessin).
                return text;
            });
    return out;
}

std::size_t TableView::copySelection(bool withTitles) {
    std::size_t n = 0;
    for (const RowIndex row : view_)
        if (std::find(selection_.begin(), selection_.end(), row) != selection_.end()) ++n;
    if (n == 0) return 0;
    setClipboardText(copyText(withTitles));
    copied->emit(n, withTitles);
    return n;
}

bool TableView::pasteFromClipboard(bool asNewRows) {
    if (!pasteHandler_) return false;
    PasteRequest rq;
    rq.text = clipboardText();
    if (rq.text.empty()) return false;
    rq.asNewRows = asNewRows;
    rq.anchorColumn = lastClickColumn_;
    rq.anchorViewRow = view_.size();
    if (!selection_.empty()) {
        // La premiere ligne choisie, dans l'ordre de la vue.
        for (std::size_t i = 0; i < view_.size(); ++i)
            if (std::find(selection_.begin(), selection_.end(), view_[i]) != selection_.end()) {
                rq.anchorViewRow = i;
                break;
            }
    }
    pasteHandler_(rq);
    return true;
}

// ---- Lot API 8 : glisser un classeur dans la fenetre ----
bool TableView::pasteText(std::string text, bool asNewRows) {
    if (!pasteHandler_ || text.empty()) return false;
    PasteRequest rq;
    rq.text = std::move(text);
    rq.asNewRows = asNewRows;
    rq.anchorColumn = -1;              // des titres : pas de colonne de depart
    rq.anchorViewRow = view_.size();   // aucune ligne choisie
    pasteHandler_(rq);
    return true;
}
// ---- fin Lot API 8 ----

void TableView::setPasteResult(std::string banner, std::vector<Mark> marks, int keyColumn) {
    banner_ = std::move(banner);
    marks_ = std::move(marks);
    markKeyColumn_ = keyColumn;
    headerHeight_ = titleHeight_ + bannerHeight() + stripHeight();
    invalidate();
}

void TableView::clearPasteResult() {
    if (banner_.empty() && marks_.empty()) return;
    banner_.clear();
    marks_.clear();
    headerHeight_ = titleHeight_ + stripHeight();
    if (cellTipShown_) {
        cellTipShown_ = false;
        setTooltip(tableTooltip_);
    }
    invalidate();
}

const TableView::Mark* TableView::markOf(RowIndex row, int column, MarkKind kind) const {
    if (marks_.empty() || !model_ || row >= model_->rowCount()) return nullptr;
    const std::size_t keyCol = markKeyColumn_ >= 0 ? static_cast<std::size_t>(markKeyColumn_) : firstVisibleColumn();
    const std::string key = model_->cellText(row, keyCol);
    for (const auto& m : marks_)
        if (m.kind == kind && m.key == key && (kind != MarkKind::Refused || m.column == column)) return &m;
    return nullptr;
}

void TableView::openContextMenu(gfx::Point at) {
    if (!context_) return;
    const bool any = !selection_.empty();
    const bool paste = pasteEnabled();
    std::vector<PopupMenu::Item> items;
    items.push_back({"Copier", "Ctrl+C", any ? std::string{} : std::string("aucune ligne choisie"), Icon::None, any, false, 1});
    items.push_back({"Copier sans les titres", "Ctrl+Maj+C", any ? std::string{} : std::string("aucune ligne choisie"), Icon::None, any, false, 2});
    items.push_back({{}, {}, {}, Icon::None, true, true, -1});
    items.push_back({"Coller", "Ctrl+V", paste ? std::string{} : std::string("ce tableau se copie, il ne se colle pas"), Icon::None, paste, false, 3});
    items.push_back({"Coller en nouvelles lignes", "Ctrl+Maj+V", paste ? std::string{} : std::string("ce tableau se copie, il ne se colle pas"), Icon::None, paste, false, 4});
    items.push_back({{}, {}, {}, Icon::None, true, true, -1});
    const bool all = selectionMode_ != SelectionMode::Single && selectionMode_ != SelectionMode::None && !view_.empty();
    items.push_back({"Tout choisir", "Ctrl+A", all ? std::string{} : std::string("une ligne \xC3\xA0 la fois"), Icon::None, all, false, 5});
    // Lot recherche : filtrer la colonne cliquee, effacer les filtres.
    if (filtersEnabled_ || !columnFilters_.empty()) {
        items.push_back({{}, {}, {}, Icon::None, true, true, -1});
        const int col = lastClickColumn_;
        if (filtersEnabled_ && col >= 0 && static_cast<std::size_t>(col) < columns_.size() && columns_[static_cast<std::size_t>(col)].filterable
            && columns_[static_cast<std::size_t>(col)].headerIcon == 0)
            items.push_back({"Filtrer la colonne \xC2\xAB " + columns_[static_cast<std::size_t>(col)].title + " \xC2\xBB\xE2\x80\xA6", {}, {},
                             Icon::Filter, true, false, 100 + col});
        const bool filtered = !columnFilters_.empty();
        items.push_back({"Effacer les filtres des colonnes", {}, filtered ? std::string{} : std::string("aucun filtre"), Icon::None, filtered,
                         false, 99});
    }
    context_->setItems(std::move(items));
    gfx::Size surface{bounds().right() + 400.f, bounds().bottom() + 400.f};
    if (surface_.w > 0.f) surface = surface_;
    context_->openAt(at, surface);
}

void TableView::setModel(std::shared_ptr<ITableModel> m) {
    modelConnections_.clear();
    model_ = std::move(m);
    if (model_)
        modelConnections_ += model_->modelReset->connect([this] { rebuildView(); });
    selection_.clear();
    scrollX_ = scrollY_ = 0.f;
    rebuildView();
}

void TableView::setColumns(std::vector<Column> cols) {
    columns_ = std::move(cols);
    invalidateLayout();
    // Lot API 8 : les filtres retenus attendaient les colonnes (posees a la mise en page).
    if (memoryPending_) recallColumnFilters();
}

void TableView::setFilter(FilterChain chain) {
    // Lot recherche : la recherche d'une chaine est surlignee dans les cases.
    if (chain.globalTerm() != filter_.globalTerm()) highlight_ = SearchQuery(chain.globalTerm());
    filter_ = std::move(chain);
    rebuildView();
}

void TableView::sortBy(std::size_t column, SortOrder order) {
    sortColumn_ = column;
    sortOrder_  = order;
    rebuildView();
}

std::vector<RowIndex> TableView::selectedModelRows() const { return selection_; }

void TableView::selectModelRows(std::vector<RowIndex> rows, bool notify) {
    const std::size_t count = model_ ? model_->rowCount() : 0;
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](RowIndex r) { return r >= count; }), rows.end());
    if (selectionMode_ == SelectionMode::Single && rows.size() > 1) rows.resize(1);
    selection_ = std::move(rows);
    revealSelection();
    invalidate();
    if (notify) selectionChanged->emit(selection_);
}

void TableView::revealSelection() {
    revealPending_ = false;
    if (selection_.empty()) return;
    // Lot 20 : le bandeau du collage compte des maintenant (il ne sera dessine
    // qu'a l'image suivante) - sinon la ligne montree tombait juste dessous.
    headerHeight_ = titleHeight_ + bannerHeight() + stripHeight();
    // Lot 16 : une table pas encore placee (un onglet cache, choisi depuis un
    // autre ecran) n'a pas de hauteur ; la ligne se montre a son placement. Elle
    // defilait d'autant de lignes qu'il y en avait avant elle, hors de la vue.
    const float visible = contentRect().h - headerHeight_;
    if (visible < rowHeight_) {
        revealPending_ = true;
        return;
    }
    // En vue : la ligne de vue de la premiere, si elle est hors de la zone.
    // Lot 20 : hors de la zone, elle vient au premier tiers (on voit ce qui la
    // suit : les autres lignes collees, creees) plutot que collee au bord.
    for (std::size_t i = 0; i < view_.size(); ++i)
        if (view_[i] == selection_.front()) {
            const float top = static_cast<float>(i) * rowHeight_;
            if (top < scrollY_ || top + rowHeight_ > scrollY_ + visible)
                scrollY_ = std::max(0.f, top - std::floor(visible / 3.f / rowHeight_) * rowHeight_);
            const float maxScroll = std::max(0.f, static_cast<float>(view_.size()) * rowHeight_ - visible);
            scrollY_ = std::min(scrollY_, maxScroll);
            break;
        }
}

void TableView::rebuildView() {
    view_.clear();
    if (!model_) { invalidate(); return; }

    view_ = filter_.apply(*model_);
    // Lot recherche : les filtres des colonnes, apres la recherche (ET). Host :
    // le volet les a deja appliques en fabriquant ses lignes.
    if (filterMode_ == ColumnFilterMode::Table) {
        if (!columnFilters_.empty()) view_ = applyColumnFilters(view_, filterShown_, filterTotal_);
        else filterShown_ = filterTotal_ = view_.size();
    }

    if (sortOrder_ != SortOrder::None && sortColumn_ < model_->columnCount()) {
        const bool asc = sortOrder_ == SortOrder::Ascending;
        const auto col = sortColumn_;
        // stable_sort so a second sort key keeps the first one's order.
        std::stable_sort(view_.begin(), view_.end(), [&](RowIndex a, RowIndex b) {
            return asc ? model_->less(a, b, col) : model_->less(b, a, col);
        });
    }
    scrollY_ = 0.f;
    invalidate();
}

void TableView::autoSizeColumn(std::size_t col) {
    if (!model_ || col >= columns_.size()) return;
    // Sampled from the visible window only: measuring 59 000 rows to size a
    // column would cost more than every other operation in the view combined.
    const std::size_t sample = std::min<std::size_t>(view_.size(), 200);
    float widest = measureWidth(columns_[col].title, gfx::FontId{16});
    for (std::size_t i = 0; i < sample; ++i)
        widest = std::max(widest, measureWidth(model_->cellText(view_[i], col), gfx::FontId{16}));
    columns_[col].width = std::clamp(widest + 20.f, columns_[col].minWidth, 600.f);
    invalidateLayout();
}

int TableView::columnAtX(float globalX) const {
    float x = contentRect().x - scrollX_;
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (!columns_[i].visible) continue;
        if (globalX >= x && globalX < x + columns_[i].width) return static_cast<int>(i);
        x += columns_[i].width;
    }
    return -1;
}

void TableView::setScrollOffset(float y) {
    const float maxScroll = std::max(0.f, static_cast<float>(view_.size()) * rowHeight_
                                          - (contentRect().h - headerHeight_));
    scrollY_ = std::clamp(y, 0.f, maxScroll);
    revealPending_ = false;     // la place demandee l'emporte sur « montrer la selection »
    invalidate();
}

void TableView::onLayout() {
    if (context_) context_->setBounds(bounds());      // lot 20 : le menu peint par-dessus tout
    if (filterPopup_) filterPopup_->setBounds(bounds());   // lot recherche : la fenetre du filtre aussi
    if (revealPending_) revealSelection();
    const float maxScroll = std::max(0.f, static_cast<float>(view_.size()) * rowHeight_
                                          - (contentRect().h - headerHeight_));
    scrollY_ = std::clamp(scrollY_, 0.f, maxScroll);
    // Lot 16 : le champ d'une case suit sa case (la colonne elargie, la table deplacee).
    if (cellEditor_) {
        const auto it = std::find(view_.begin(), view_.end(), editRow_);
        gfx::Rect cell;
        if (it != view_.end() && cellRect(static_cast<std::size_t>(it - view_.begin()), editCol_, cell)) cellEditor_->setBounds(cell);
    }
}

std::size_t TableView::firstVisibleColumn() const noexcept {
    for (std::size_t i = 0; i < columns_.size(); ++i)
        if (columns_[i].visible) return i;
    return 0;
}

bool TableView::cellRect(std::size_t viewRowIndex, std::size_t col, gfx::Rect& out) const {
    gfx::Rect row;
    if (col >= columns_.size() || !columns_[col].visible || !rowRect(viewRowIndex, row)) return false;
    float x = contentRect().x - scrollX_;
    for (std::size_t i = 0; i < col; ++i)
        if (columns_[i].visible) x += columns_[i].width;
    out = {x, row.y, columns_[col].width, row.h};
    return true;
}

bool TableView::expanderRect(std::size_t viewRowIndex, gfx::Rect& out) const {
    if (!model_ || viewRowIndex >= view_.size()) return false;
    const std::size_t col = firstVisibleColumn();
    const auto st = model_->cellStyle(view_[viewRowIndex], col);
    gfx::Rect cell;
    if (st.expander < 0 || !cellRect(viewRowIndex, col, cell)) return false;
    out = {cell.x + 4.f + st.indent, cell.y, 16.f, cell.h};
    return true;
}

bool TableView::checkRect(std::size_t viewRowIndex, gfx::Rect& out) const {
    if (!model_ || viewRowIndex >= view_.size()) return false;
    const std::size_t col = firstVisibleColumn();
    const auto st = model_->cellStyle(view_[viewRowIndex], col);
    gfx::Rect cell;
    if (st.check < 0 || !cellRect(viewRowIndex, col, cell)) return false;
    float x = cell.x + 6.f;
    if (st.indent > 0.f || st.expander >= 0) x += st.indent + 16.f;
    out = {x, cell.y + (cell.h - 14.f) * 0.5f, 14.f, 14.f};
    return true;
}

bool TableView::rowRect(std::size_t i, gfx::Rect& out) const {
    if (i >= view_.size()) return false;
    const auto area = contentRect();
    const float y = area.y + headerHeight_ + static_cast<float>(i) * rowHeight_ - scrollY_;
    if (y < area.y + headerHeight_ || y + rowHeight_ > area.bottom()) return false;
    out = {area.x, y, area.w, rowHeight_};
    return true;
}

void TableView::onPaint(const PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto  area = contentRect();
    const float rowH = rowHeight_ = ctx.theme.metric.rowHeight;
    // Lot 20 : le bandeau du collage prend sa place au-dessus des titres.
    const float bannerH = bannerHeight();
    const float titleH = titleHeight_ = ctx.theme.metric.headerHeight;
    // Lot recherche : la bande des filtres des colonnes, entre le bandeau et les titres.
    const float stripH = stripHeight();
    const float headerH = headerHeight_ = titleH + bannerH + stripH;
    const float titleTop = area.y + bannerH + stripH;
    surface_ = ctx.r.surfaceSize();

    ctx.r.fillRect(bounds(), c.panelBg);
    paintFrame(ctx, bounds(), focused());
    if (!model_ || rowH <= 0.f) return;

    // ---- lot 20 : le bandeau du collage -----------------------------------
    if (bannerH > 0.f) {
        const gfx::Rect band{area.x, area.y, area.w, bannerH};
        const gfx::Color ok = ctx.theme.color.ok;
        ctx.r.fillRect(band, ok.withAlpha(38));
        ctx.r.fillRect({band.x, band.y, 3.f, band.h}, ok);
        const auto& f = ctx.theme.font.ui;
        const float ty = band.y + (bannerH - ctx.r.lineHeight(f)) * 0.5f;
        drawMaybeBold(ctx, {band.x + 10.f, ty}, banner_, f, c.text, band.w - 44.f, false);
        // La croix : effacer les marques.
        const float cx = band.right() - 16.f, cy = band.y + bannerH * 0.5f;
        ctx.r.line({cx - 4.f, cy - 4.f}, {cx + 4.f, cy + 4.f}, c.textMuted, 1.5f);
        ctx.r.line({cx - 4.f, cy + 4.f}, {cx + 4.f, cy - 4.f}, c.textMuted, 1.5f);
    }

    // ---- lot recherche : la bande des filtres des colonnes ----------------
    if (stripH > 0.f) paintFilterStrip(ctx, {area.x, area.y + bannerH, area.w, stripH});

    // ---- header ----------------------------------------------------------
    ctx.r.fillRect({area.x, titleTop, area.w, titleH}, c.headerBg);
    {
        float x = area.x - scrollX_;
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            if (!columns_[i].visible) continue;
            const gfx::Rect cell{x, titleTop, columns_[i].width, titleH};
            // Lot recherche : l'entonnoir, plein quand la colonne est filtree.
            const gfx::Rect funnel = filterIconRect(i);
            const float funnelW = funnel.w > 0.f ? 20.f : 0.f;
            if (columns_[i].headerIcon != 0 && iconPainter_) {
                // Lot 21 : une colonne de marques - son icone a la place du titre.
                const float side = std::min(titleH - 8.f, 16.f);
                iconPainter_(ctx.r, columns_[i].headerIcon, {cell.x + 6.f, cell.y + (titleH - side) * 0.5f, side, side}, c.textMuted);
            } else
            drawMaybeBold(ctx, {cell.x + 8.f, cell.y + (titleH - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f},
                          columns_[i].title, ctx.theme.font.uiBold,
                          i == sortColumn_ && sortOrder_ != SortOrder::None ? c.text : c.textMuted,
                          cell.w - 26.f - funnelW, true);
            if (funnel.w > 0.f) {
                const bool on = columnFilter(i) != nullptr;
                if (on) ctx.r.fillRoundedRect({funnel.x - 3.f, funnel.y - 3.f, funnel.w + 6.f, funnel.h + 6.f}, c.accent.withAlpha(60), 3.f);
                drawIcon(ctx.r, Icon::Filter, funnel, on ? c.accent : c.textMuted.withAlpha(150));
            }
            if (i == sortColumn_ && sortOrder_ != SortOrder::None) {
                const float ax = cell.right() - 12.f - funnelW, ay = cell.y + titleH * 0.5f;
                const bool asc = sortOrder_ == SortOrder::Ascending;
                ctx.r.line({ax - 4.f, ay + (asc ? 2.f : -2.f)}, {ax, ay + (asc ? -2.f : 2.f)}, c.accent, 1.5f);
                ctx.r.line({ax, ay + (asc ? -2.f : 2.f)}, {ax + 4.f, ay + (asc ? 2.f : -2.f)}, c.accent, 1.5f);
            }
            ctx.r.line({cell.right(), cell.y + 4.f}, {cell.right(), cell.bottom() - 4.f}, c.border, 1.f);
            x += columns_[i].width;
        }
    }
    ctx.r.line({area.x, area.y + headerH}, {area.right(), area.y + headerH}, c.border, 1.f);

    // ---- rows: only the visible slice ------------------------------------
    const gfx::Rect body{area.x, area.y + headerH, area.w, std::max(0.f, area.h - headerH)};
    ctx.r.pushClip(body);

    const auto first = static_cast<std::size_t>(std::max(0.f, scrollY_ / rowH));
    const auto last  = std::min(view_.size(), first + static_cast<std::size_t>(body.h / rowH) + 1);
    const float textY = (rowH - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f;

    for (auto i = first; i < last; ++i) {
        const RowIndex row = view_[i];
        const gfx::Rect r{body.x, body.y + static_cast<float>(i) * rowH - scrollY_, body.w, rowH};
        const bool selected = std::find(selection_.begin(), selection_.end(), row) != selection_.end();

        if (!selected && altRows_ && (i % 2) == 1) ctx.r.fillRect(r, c.rowAltBg);
        if (selected) {
            // Pleine largeur : une table se lit ligne a ligne, une selection
            // en retrait couperait la premiere colonne.
            ctx.r.fillRect(r, c.selectionBg);
            if (focused()) ctx.r.fillRect({r.x, r.y + 3.f, 3.f, std::max(0.f, rowH - 6.f)}, c.accent);
        } else if (hovered() && hoverRow_ == static_cast<int>(i)) {
            ctx.r.fillRect(r, ctx.theme.brand.hover);
        }

        float x = body.x - scrollX_;
        const std::size_t firstCol = firstVisibleColumn();
        bool spanned = false;
        for (std::size_t col = 0; col < columns_.size(); ++col) {
            if (!columns_[col].visible) continue;
            if (spanned) break;
            const auto st = model_->cellStyle(row, col);
            auto text = model_->cellText(row, col);
            const auto font = st.monospace ? ctx.theme.font.mono
                            : st.bold      ? ctx.theme.font.uiBold : ctx.theme.font.ui;
            // Lot 16 : un titre de groupe court sur toute la ligne.
            const bool span = col == firstCol && st.spanRow;
            const float cellW = span ? std::max(columns_[col].width, body.right() - x) : columns_[col].width;
            spanned = span;
            if (st.bg) ctx.r.fillRect({span ? body.x : x, r.y, span ? body.w : cellW, rowH}, *st.bg);

            float tx = x + 6.f;
            float avail = cellW - 12.f;
            // Lot 16 : la table en arbre - le retrait, puis la fleche.
            if (col == firstCol && (st.indent > 0.f || st.expander >= 0)) {
                tx += st.indent;
                avail -= st.indent;
                if (st.expander >= 0) {
                    const float ax = tx + 5.f, ay = r.y + rowH * 0.5f;
                    const gfx::Color arrow = selected ? c.text : c.textMuted;
                    if (st.expander == 1) {
                        ctx.r.line({ax - 4.f, ay - 2.f}, {ax, ay + 2.f}, arrow, 1.5f);
                        ctx.r.line({ax, ay + 2.f}, {ax + 4.f, ay - 2.f}, arrow, 1.5f);
                    } else {
                        ctx.r.line({ax - 2.f, ay - 4.f}, {ax + 2.f, ay}, arrow, 1.5f);
                        ctx.r.line({ax + 2.f, ay}, {ax - 2.f, ay + 4.f}, arrow, 1.5f);
                    }
                }
                tx += 16.f;
                avail -= 16.f;
            }
            // 1.9 : une vraie case a cocher (premiere colonne), avant l'icone.
            if (col == firstCol && st.check >= 0 && avail > 20.f) {
                drawCheckBox(ctx, {tx, r.y + (rowH - 14.f) * 0.5f, 14.f, 14.f}, st.check > 0);
                tx    += 20.f;
                avail -= 20.f;
            }
            if (st.icon != Icon::None) {
                const float side = std::min(rowH - 6.f, 16.f);
                drawIcon(ctx.r, st.icon, {tx, r.y + (rowH - side) * 0.5f, side, side},
                         iconColourOf(ctx.theme, st, selected));
                tx    += side + 5.f;
                avail -= side + 5.f;
            } else if (st.customIcon != 0 && iconPainter_) {
                // Lot 21 : une icone de l'hote (la tuile d'un objet, un chapitre).
                const float side = std::min(rowH - 6.f, 16.f);
                iconPainter_(ctx.r, st.customIcon, {tx, r.y + (rowH - side) * 0.5f, side, side},
                             iconColourOf(ctx.theme, st, selected));
                tx    += side + 5.f;
                avail -= side + 5.f;
            }
            // 1.9 : l'etiquette devant le texte ("AUTO"), bordee de sa couleur.
            if (!st.lead.empty() && avail > 40.f) {
                const auto lf = ctx.theme.font.caption;
                const float lh = ctx.r.lineHeight(lf);
                const float h = std::min(std::max(0.f, rowH - 8.f), lh + 2.f);
                const float w = ctx.r.measure(st.lead, lf).width + 10.f;
                const gfx::Color leadCol = selected ? c.selectionText : ctx.theme.onSurface(ctx.theme.tone(st.leadTone, c.textMuted));
                const gfx::Rect pill{tx, r.y + (rowH - h) * 0.5f, w, h};
                ctx.r.strokeRect(pill, leadCol, 1.f);
                ctx.r.drawText({pill.x + 5.f, pill.y + (h - lh) * 0.5f}, st.lead, lf, leadCol);
                tx    += w + 6.f;
                avail -= w + 6.f;
            }
            // 1.9 : une pastille de couleur devant le texte (le mode d'un parametre).
            if (st.colorLead) {
                const gfx::Color under = selected ? c.selectionBg : st.bg.value_or(c.panelBg);
                if (const float w = drawColorPill(ctx, tx, r, *st.colorLead, avail, under); w > 0.f) {
                    tx    += w + 6.f;
                    avail -= w + 6.f;
                } else if (text.empty()) {
                    // 1.9 finale : une colonne trop etroite pour la pastille (LES DEUX dans
                    // l'onglet Popups) - son texte, coupe s'il le faut, plutot qu'une case vide.
                    text = st.colorLead->text;
                }
            }
            if (!st.badge.empty()) {
                const gfx::Rect badgeCell{x, r.y, cellW, rowH};
                avail -= drawBadge(ctx, badgeCell, st);
            }
            if (columns_[col].align == Align::End && !span) {
                const auto m = ctx.r.measure(text, font);
                tx = x + columns_[col].width - 6.f - m.width;
            }
            // Lot recherche : les mots cherches, surlignes sous le texte.
            if (!highlight_.empty()) paintHighlights(ctx, text, tx, r.y, rowH, avail, font);
            drawMaybeBold(ctx, {tx, r.y + textY}, text, font,
                          textColourOf(ctx.theme, st, selected), avail, st.bold);
            // Lot 20 : une case refusee au collage - un cadre rouge.
            if (!marks_.empty() && markOf(row, static_cast<int>(col), MarkKind::Refused))
                ctx.r.strokeRect({x + 1.f, r.y + 1.f, cellW - 2.f, rowH - 2.f}, c.error, 1.5f);
            x += columns_[col].width;
        }
        // Lot 20 : la ligne creee (verte) ou mise a jour (bleue) par le collage.
        if (!marks_.empty()) {
            if (markOf(row, -1, MarkKind::Created)) {
                ctx.r.fillRect(r, c.ok.withAlpha(26));
                ctx.r.fillRect({r.x, r.y, 3.f, rowH}, c.ok);
            } else if (markOf(row, -1, MarkKind::Updated)) {
                // Mise a jour : le bleu de l'information, plus clair que la ligne choisie.
                ctx.r.fillRect(r, c.info.withAlpha(20));
                ctx.r.fillRect({r.x, r.y, 3.f, rowH}, c.info);
            }
        }
        ctx.r.line({r.x, r.bottom()}, {r.right(), r.bottom()}, c.gridLine, 1.f);
    }
    // Lot 16 : le glisser d'une ligne - ou elle tomberait, et son nom sous la souris.
    if (dragActive_) {
        // Lot 21 : les lignes trainees s'effacent a moitie.
        for (std::size_t i = 0; i < view_.size(); ++i) {
            if (std::find(dragRows_.begin(), dragRows_.end(), view_[i]) == dragRows_.end()) continue;
            gfx::Rect rr;
            if (rowRect(i, rr)) ctx.r.fillRect(rr, c.panelBg.withAlpha(130));
        }
        if (dropValid_ && dropTo_ != kNoRow) {
            for (std::size_t i = 0; i < view_.size(); ++i) {
                if (view_[i] != dropTo_) continue;
                gfx::Rect rr;
                if (!rowRect(i, rr)) continue;
                if (dropWhere_ == DropWhere::Into) {
                    ctx.r.fillRect(rr, c.accent.withAlpha(50));
                    ctx.r.strokeRect({rr.x + 1.f, rr.y + 1.f, rr.w - 2.f, rr.h - 2.f}, c.accent, 2.f);
                } else {
                    // Entre deux lignes : un trait de deux pixels et un repere a gauche.
                    const float y = dropWhere_ == DropWhere::Before ? rr.y : rr.bottom();
                    ctx.r.fillRect({rr.x + 4.f, y - 1.f, rr.w - 8.f, 2.f}, c.accent);
                    ctx.r.fillRoundedRect({rr.x + 2.f, y - 4.f, 8.f, 8.f}, c.accent, 4.f);
                }
            }
        } else if (dropValid_) {
            // Sous les lignes : a la racine.
            const float y = std::min(area.bottom() - 2.f, area.y + headerHeight_ + static_cast<float>(view_.size()) * rowHeight_ - scrollY_ + 2.f);
            ctx.r.fillRect({area.x + 4.f, y, area.w - 8.f, 2.f}, c.accent);
        }
        std::string label = model_ && dragFrom_ < model_->rowCount() ? model_->cellText(dragFrom_, firstVisibleColumn()) : std::string{};
        // Lot 21 : plusieurs lignes - la premiere, et combien d'autres.
        while (!label.empty() && label.front() == ' ') label.erase(label.begin());
        if (dragRows_.size() > 1) label += "  +" + std::to_string(dragRows_.size() - 1);
        if (!label.empty()) {
            const auto& f = ctx.theme.font.ui;
            const float w = ctx.r.measure(label, f).width + 16.f, h = ctx.r.lineHeight(f) + 8.f;
            const gfx::Rect ghost{dragPos_.x + 14.f, dragPos_.y + 8.f, w, h};
            ctx.r.fillRoundedRect(ghost, dropValid_ ? c.accent.withAlpha(220) : c.headerBg.withAlpha(230), 4.f);
            ctx.r.drawText({ghost.x + 8.f, ghost.y + 4.f}, label, f, dropValid_ ? gfx::Color::rgb(0xFFFFFF) : c.textMuted);
        }
    }
    ctx.r.popClip();
}

EventResult TableView::onEvent(const InputEvent& ev) {
    if (!model_) return EventResult::Ignored;
    const float rowH = rowHeight_, headerH = headerHeight_;   // as last painted
    const auto  area = contentRect();

    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        if (!bounds().contains(w->pos)) return EventResult::Ignored;
        // Lot 16 : la case en cours d'edition est validee avant de defiler.
        if (cellEditor_ && !cellIsList_)
            if (auto* f = activeCellField()) finishCellEdit(true, f->text());
        const float maxScroll = std::max(0.f, static_cast<float>(view_.size()) * rowH - (area.h - headerH));
        scrollY_ = std::clamp(scrollY_ - w->dy * rowH * 3.f, 0.f, maxScroll);
        if (w->mods.shift) scrollX_ = std::max(0.f, scrollX_ - w->dy * 60.f);
        invalidate();
        return EventResult::Consumed;
    }

    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (!bounds().contains(d->pos)) return EventResult::Ignored;
        grabFocus();
        dragArmed_ = dragActive_ = dropValid_ = false;     // un glisser lache hors de la table

        // Lot 20 : le bandeau du collage - sa croix efface les marques.
        if (const float bh = bannerHeight(); bh > 0.f && d->pos.y < area.y + bh) {
            if (d->pos.x > area.right() - 30.f) clearPasteResult();
            return EventResult::Consumed;
        }
        // Lot recherche : la bande des filtres (une pastille, sa croix, + Filtre,
        // Tout effacer), puis l'entonnoir d'un titre.
        if (d->button == MouseButton::Left && filterStripClick(d->pos)) return EventResult::Consumed;
        if (d->button == MouseButton::Left && filtersEnabled_ && d->pos.y < area.y + headerH)
            for (std::size_t i = 0; i < columns_.size(); ++i)
                if (columns_[i].visible && filterIconRect(i).contains(d->pos)) {
                    (void)openColumnFilter(i);
                    return EventResult::Consumed;
                }
        // Lot 20 : le clic droit - la ligne sous la souris (gardee si elle est
        // deja dans le choix, comme Excel), puis le menu.
        if (d->button == MouseButton::Right) {
            if (cellEditor_) {
                if (auto* f = activeCellField()) finishCellEdit(true, f->text());
                else finishCellEdit(false, {});
            }
            // Lot recherche : sur les titres aussi, la colonne cliquee (Filtrer la colonne).
            if (d->pos.y < area.y + headerH) lastClickColumn_ = columnAtX(d->pos.x);
            if (d->pos.y >= area.y + headerH) {
                const int vi = static_cast<int>((d->pos.y - area.y - headerH + scrollY_) / rowH);
                lastClickColumn_ = columnAtX(d->pos.x);
                if (vi >= 0 && vi < static_cast<int>(view_.size()) && selectionMode_ != SelectionMode::None) {
                    const RowIndex row = view_[static_cast<std::size_t>(vi)];
                    if (std::find(selection_.begin(), selection_.end(), row) == selection_.end()) {
                        selection_.assign(1, row);
                        selectionChanged->emit(selection_);
                    }
                }
            }
            openContextMenu(d->pos);
            invalidate();
            return EventResult::Consumed;
        }

        // Header: sort, or start a column resize when on a boundary.
        if (d->pos.y < area.y + headerH) {
            float x = area.x - scrollX_;
            for (std::size_t i = 0; i < columns_.size(); ++i) {
                if (!columns_[i].visible) continue;
                const float edge = x + columns_[i].width;
                if (columns_[i].resizable && std::abs(d->pos.x - edge) < 4.f) {
                    resizingColumn_ = static_cast<int>(i);
                    resizeOrigin_   = d->pos.x;
                    return EventResult::Consumed;
                }
                x = edge;
            }
            const int col = columnAtX(d->pos.x);
            if (col >= 0 && columns_[static_cast<std::size_t>(col)].sortable) {
                const auto c = static_cast<std::size_t>(col);
                sortBy(c, (sortColumn_ == c && sortOrder_ == SortOrder::Ascending)
                              ? SortOrder::Descending : SortOrder::Ascending);
            }
            return EventResult::Consumed;
        }

        // Lot 16 : un clic hors de la case en cours d'edition la valide.
        if (cellEditor_) {
            if (auto* f = activeCellField()) finishCellEdit(true, f->text());
            else finishCellEdit(false, {});
        }
        const int vi = static_cast<int>((d->pos.y - area.y - headerH + scrollY_) / rowH);
        lastClickColumn_ = columnAtX(d->pos.x);          // lot 20 : la case d'ou coller
        if (vi >= 0 && vi < static_cast<int>(view_.size())) {
            const RowIndex row = view_[static_cast<std::size_t>(vi)];
            // Lot 16 : la fleche d'une table en arbre - deplier ou replier.
            gfx::Rect er;
            if (d->button == MouseButton::Left && expanderRect(static_cast<std::size_t>(vi), er) && er.contains(d->pos)) {
                if (selectionMode_ != SelectionMode::None) {
                    selection_.assign(1, row);
                    selectionChanged->emit(selection_);
                }
                expanderClicked->emit(row);
                invalidate();
                return EventResult::Consumed;
            }
            // 1.9 : la case a cocher de la ligne - l'hote coche ou decoche.
            gfx::Rect cr;
            if (d->button == MouseButton::Left && checkRect(static_cast<std::size_t>(vi), cr)
                && gfx::Rect{cr.x - 3.f, cr.y - 4.f, cr.w + 6.f, cr.h + 8.f}.contains(d->pos)) {
                if (selectionMode_ != SelectionMode::None) {
                    selection_.assign(1, row);
                    selectionChanged->emit(selection_);
                }
                checkClicked->emit(row);
                invalidate();
                return EventResult::Consumed;
            }
            // Lot 21 : saisir une ligne d'une selection de plusieurs, pour les
            // trainer ensemble - la selection ne se reduit qu'au relacher, si
            // aucun glisser n'a eu lieu.
            const bool inSelection = std::find(selection_.begin(), selection_.end(), row) != selection_.end();
            deferSelect_ = rowDrag_ && d->button == MouseButton::Left && d->clickCount == 1 && inSelection
                        && selection_.size() > 1 && !d->mods.ctrl && !d->mods.shift;
            deferRow_ = row;
            if (deferSelect_) {
                // rien : la selection reste, le glisser la prendra
            } else if (selectionMode_ == SelectionMode::Extended && d->mods.shift && !selection_.empty()) {
                // Range select over *view* order, stored as model rows.
                const auto anchor = std::find(view_.begin(), view_.end(), selection_.back());
                if (anchor != view_.end()) {
                    auto lo = static_cast<std::size_t>(anchor - view_.begin());
                    auto hi = static_cast<std::size_t>(vi);
                    if (lo > hi) std::swap(lo, hi);
                    selection_.clear();
                    for (auto i = lo; i <= hi; ++i) selection_.push_back(view_[i]);
                }
            } else if (selectionMode_ != SelectionMode::None) {
                toggleSelection(selection_, row,
                                selectionMode_ != SelectionMode::Single && d->mods.ctrl);
            }
            if (!deferSelect_) selectionChanged->emit(selection_);
            // Lot 16 : saisie pour un glisser (il ne commence qu'au-dela de quelques pixels).
            if (rowDrag_ && d->button == MouseButton::Left && d->clickCount == 1) {
                dragArmed_ = true;
                dragActive_ = false;
                dragFrom_ = row;
                dragOrigin_ = d->pos;
                // Lot 21 : ce qu'on traine - la selection (dans l'ordre de la vue)
                // si la ligne saisie en fait partie, sinon elle seule.
                dragRows_.clear();
                if (std::find(selection_.begin(), selection_.end(), row) != selection_.end()) {
                    for (const auto r : view_)
                        if (std::find(selection_.begin(), selection_.end(), r) != selection_.end()) dragRows_.push_back(r);
                }
                if (dragRows_.empty()) dragRows_.push_back(row);
            }
            if (d->clickCount >= 2) {
                // Lot 16 : double-clic sur une case editable : on l'edite ; sinon, activer.
                const int col = columnAtX(d->pos.x);
                if (col >= 0 && model_->editable(row, static_cast<std::size_t>(col))) (void)beginCellEdit(row, static_cast<std::size_t>(col));
                else activated->emit(row);
            }
            invalidate();
        }
        return EventResult::Consumed;
    }

    // Lot 16 : le glisser d'une ligne.
    if (const auto* m = std::get_if<MouseMove>(&ev); m && dragArmed_) {
        if (!dragActive_ && std::hypot(m->pos.x - dragOrigin_.x, m->pos.y - dragOrigin_.y) > 6.f) dragActive_ = true;
        if (dragActive_) {
            dragPos_ = m->pos;
            // Lot 21 : pres du haut ou du bas, la table defile (une longue liste).
            const float bodyTop = area.y + headerH;           // (le bandeau du collage compris)
            const float maxScroll = std::max(0.f, static_cast<float>(view_.size()) * rowH - (area.h - headerH));
            if (m->pos.y < bodyTop + 14.f) scrollY_ = std::max(0.f, scrollY_ - rowH * 0.5f);
            else if (m->pos.y > area.bottom() - 14.f) scrollY_ = std::min(maxScroll, scrollY_ + rowH * 0.5f);
            const float localY = m->pos.y - area.y - headerH + scrollY_;
            const int vi = localY >= 0.f ? static_cast<int>(localY / rowH) : -1;
            const auto dragged = [&](RowIndex r) { return std::find(dragRows_.begin(), dragRows_.end(), r) != dragRows_.end(); };
            dropValid_ = false;
            dropWhere_ = DropWhere::Into;
            if (m->pos.y >= area.y + headerH && vi >= 0 && vi < static_cast<int>(view_.size())) {
                dropTo_ = view_[static_cast<std::size_t>(vi)];
                // Le quart haut propose AVANT, le quart bas APRES, le milieu DANS ;
                // refuse, on essaie le cote le plus proche.
                const float frac = (localY - static_cast<float>(vi) * rowH) / rowH;
                DropWhere order[3] = {DropWhere::Into, frac < 0.5f ? DropWhere::Before : DropWhere::After,
                                      frac < 0.5f ? DropWhere::After : DropWhere::Before};
                if (frac < 0.25f) { order[0] = DropWhere::Before; order[1] = DropWhere::Into; order[2] = DropWhere::After; }
                else if (frac > 0.75f) { order[0] = DropWhere::After; order[1] = DropWhere::Into; order[2] = DropWhere::Before; }
                if (!dragged(dropTo_))
                    for (const auto w : order)
                        if (model_->canDropRows(dragRows_, dropTo_, w)) {
                            dropWhere_ = w;
                            dropValid_ = true;
                            break;
                        }
            } else {
                dropTo_ = kNoRow;
                dropValid_ = model_->canDropRows(dragRows_, kNoRow, DropWhere::Into);
            }
            invalidate();
            return EventResult::Consumed;
        }
    }
    if (std::get_if<MouseUp>(&ev) && dragArmed_) {
        const bool was = dragActive_;
        const bool drop = dragActive_ && dropValid_;
        const RowIndex from = dragFrom_, to = dropTo_;
        const auto where = dropWhere_;
        const auto rows = dragRows_;
        dragArmed_ = dragActive_ = dropValid_ = false;
        // Lot 21 : un clic (sans glisser) sur une selection de plusieurs la reduit a la ligne.
        if (!was && deferSelect_) {
            selection_.assign(1, deferRow_);
            selectionChanged->emit(selection_);
        }
        deferSelect_ = false;
        invalidate();
        if (drop) {
            rowsDropped->emit(rows, to, where);
            if (rows.size() == 1 && where == DropWhere::Into) rowDropped->emit(from, to);
        }
        if (was) return EventResult::Consumed;
    }
    if (const auto* m = std::get_if<MouseMove>(&ev); m && resizingColumn_ < 0) {
        int h = -1;
        if (bounds().contains(m->pos) && m->pos.y >= area.y + headerH) {
            const int vi = static_cast<int>((m->pos.y - area.y - headerH + scrollY_) / rowH);
            if (vi >= 0 && vi < static_cast<int>(view_.size())) h = vi;
        }
        // Lot 20 : sur une case refusee au collage, sa raison en infobulle.
        if (!marks_.empty()) {
            const int col = h >= 0 ? columnAtX(m->pos.x) : -1;
            const Mark* refused = h >= 0 && col >= 0 ? markOf(view_[static_cast<std::size_t>(h)], col, MarkKind::Refused) : nullptr;
            if (refused) {
                if (!cellTipShown_ && !rowTooltipShown_) tableTooltip_ = tooltip();
                cellTipShown_ = true;
                if (tooltip() != refused->why) setTooltip(refused->why);
                hoverCol_ = col;
            } else if (cellTipShown_) {
                cellTipShown_ = false;
                hoverCol_ = -1;
                setTooltip(tableTooltip_);
                rowTooltipShown_ = false;
                hoverRow_ = -2;      // l'infobulle de la ligne sera recalculee
            }
            if (refused) {
                hoverRow_ = h;
                invalidate();
                return EventResult::Ignored;
            }
        }
        if (h != hoverRow_) {
            hoverRow_ = h;
            // Lot 16 : l'infobulle de la ligne survolee, si le modele en donne une.
            const std::string tip = h >= 0 ? model_->rowTooltip(view_[static_cast<std::size_t>(h)]) : std::string{};
            if (!tip.empty()) {
                if (!rowTooltipShown_) tableTooltip_ = tooltip();
                rowTooltipShown_ = true;
                setTooltip(tip);
            } else if (rowTooltipShown_) {
                rowTooltipShown_ = false;
                setTooltip(tableTooltip_);
            }
            invalidate();
        }
        return EventResult::Ignored;
    }
    if (const auto* m = std::get_if<MouseMove>(&ev); m && resizingColumn_ >= 0) {
        auto& col = columns_[static_cast<std::size_t>(resizingColumn_)];
        col.width = std::max(col.minWidth, col.width + (m->pos.x - resizeOrigin_));
        resizeOrigin_ = m->pos.x;
        invalidateLayout();
        return EventResult::Consumed;
    }
    if (std::get_if<MouseUp>(&ev) && resizingColumn_ >= 0) {
        resizingColumn_ = -1;
        return EventResult::Consumed;
    }

    // Lot 20 : copier, coller (meme dans une table vide : on y colle).
    if (const auto* k = std::get_if<KeyDown>(&ev); k && focused() && k->mods.ctrl && !k->mods.alt && !cellEditor_) {
        if (k->key == Key::C) {
            (void)copySelection(!k->mods.shift);
            return EventResult::Consumed;
        }
        if (k->key == Key::V && pasteEnabled()) {
            (void)pasteFromClipboard(k->mods.shift);
            return EventResult::Consumed;
        }
    }
    if (const auto* k = std::get_if<KeyDown>(&ev); k && focused() && k->key == Key::Escape && k->mods.none()
        && !cellEditor_ && (!banner_.empty() || !marks_.empty())) {
        clearPasteResult();
        return EventResult::Consumed;
    }
    if (const auto* k = std::get_if<KeyDown>(&ev); k && focused() && !view_.empty()) {
        // Lot 16 : F2 edite la premiere case editable de la ligne choisie ; les
        // fleches gauche et droite replient et deplient une ligne d'arbre.
        if (!selection_.empty() && (k->key == Key::F2 || k->key == Key::Left || k->key == Key::Right) && k->mods.none()) {
            const RowIndex row = selection_.back();
            if (k->key == Key::F2) {
                for (std::size_t c = 0; c < columns_.size(); ++c)
                    if (columns_[c].visible && model_->editable(row, c)) {
                        (void)beginCellEdit(row, c);
                        return EventResult::Consumed;
                    }
                return EventResult::Ignored;
            }
            const auto st = model_->cellStyle(row, firstVisibleColumn());
            if ((k->key == Key::Right && st.expander == 0) || (k->key == Key::Left && st.expander == 1)) {
                expanderClicked->emit(row);
                invalidate();
                return EventResult::Consumed;
            }
        }
        if (k->key == Key::A && k->mods.ctrl && selectionMode_ != SelectionMode::Single) {
            selection_ = view_;
            selectionChanged->emit(selection_);
            invalidate();
            return EventResult::Consumed;
        }
        if (k->key == Key::Down || k->key == Key::Up) {
            std::size_t index = 0;
            if (!selection_.empty()) {
                const auto it = std::find(view_.begin(), view_.end(), selection_.back());
                if (it != view_.end()) index = static_cast<std::size_t>(it - view_.begin());
            }
            index = (k->key == Key::Down) ? std::min(view_.size() - 1, index + 1)
                                          : (index ? index - 1 : 0);
            selection_.assign(1, view_[index]);
            const float y = static_cast<float>(index) * rowH;
            const float h = area.h - headerH;
            if (y < scrollY_) scrollY_ = y;
            else if (y + rowH > scrollY_ + h) scrollY_ = y + rowH - h;
            selectionChanged->emit(selection_);
            invalidate();
            return EventResult::Consumed;
        }
    }
    return EventResult::Ignored;
}

// ========================================================== PropertyGrid ====
namespace {

// Le champ d'une case : Echap ANNULE. InputText, lui, valide en perdant le
// focus - y compris quand on le lui retire avec Echap.
class CellField final : public InputText {
public:
    using InputText::InputText;
    bool cancelled{false};
    // 1.10 (chantier K) : Ctrl+Suppr dans une case a expression la retire.
    std::function<void()> clearExpression;
    void start() {
        grabFocus();
        // Tout selectionne : taper remplace, les fleches gardent.
        (void)InputText::onEvent(KeyDown{Key::End, {}, false});
        (void)InputText::onEvent(KeyDown{Key::Home, KeyMods{false, true, false, false}, false});
    }
protected:
    EventResult onEvent(const InputEvent& ev) override {
        // Echap ferme d'abord la liste d'aide a la saisie ; c'est le suivant qui annule.
        if (const auto* k = std::get_if<KeyDown>(&ev); k && k->key == Key::Escape && focused() && !suggestionsOpen())
            cancelled = true;
        if (const auto* k = std::get_if<KeyDown>(&ev); k && k->key == Key::Delete && k->mods.ctrl && clearExpression && focused()) {
            const auto clear = clearExpression;   // la case se ferme pendant l'appel
            clear();
            return EventResult::Consumed;
        }
        return InputText::onEvent(ev);
    }
};

bool isTrueText(std::string_view v) {
    return v == "TRUE" || v == "true" || v == "True" || v == "1" || v == "VRAI" || v == "Vrai" || v == "vrai"
        || v == "OUI" || v == "Oui" || v == "oui";
}

// "#RRGGBB" ou "#RRGGBBAA" -> couleur, pour la pastille des cases Color.
bool hexColor(std::string_view v, gfx::Color& out) {
    if ((v.size() != 7 && v.size() != 9) || v[0] != '#') return false;
    auto nibble = [](char ch, unsigned& x) {
        if (ch >= '0' && ch <= '9') { x = static_cast<unsigned>(ch - '0'); return true; }
        if (ch >= 'a' && ch <= 'f') { x = static_cast<unsigned>(ch - 'a' + 10); return true; }
        if (ch >= 'A' && ch <= 'F') { x = static_cast<unsigned>(ch - 'A' + 10); return true; }
        return false;
    };
    unsigned vals[4] = {0, 0, 0, 255};
    const std::size_t n = (v.size() - 1) / 2;
    for (std::size_t i = 0; i < n; ++i) {
        unsigned hi = 0, lo = 0;
        if (!nibble(v[1 + 2 * i], hi) || !nibble(v[2 + 2 * i], lo)) return false;
        vals[i] = hi * 16 + lo;
    }
    out = gfx::Color{static_cast<std::uint8_t>(vals[0]), static_cast<std::uint8_t>(vals[1]),
                     static_cast<std::uint8_t>(vals[2]), static_cast<std::uint8_t>(vals[3])};
    return true;
}

} // namespace

// ---- 1.10 (chantier K) : les champs a expression, partout pareils ----
namespace exprfield {
namespace {
using Property = PropertyGrid::Property;
std::string_view trimmedView(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}
constexpr std::string_view kSep = " \xC2\xB7 ";
} // namespace

std::string_view word(Expect e) noexcept {
    switch (e) {
        case Expect::Bool:     return "TRUE, FALSE";   // maquette 1.10 : "TRUE, FALSE ou = expression"
        case Expect::Number:   return "nombre";
        case Expect::Text:     return "texte";
        case Expect::Color:    return "couleur";
        case Expect::View:     return "vue";
        case Expect::List:     return "choix";
        case Expect::Template: return "texte, {variable}";   // maquette : "texte, {variable} ou = expression"
        case Expect::Time:     return "dur\xC3\xA9" "e";
        case Expect::Value:    break;
    }
    return "valeur";
}

std::string invite(Expect e) { return std::string(word(e)) + " ou " + std::string(kTail); }

bool accepts(const Property& p) noexcept {
    const std::string_view s = p.placeholder;
    return s.size() >= kTail.size() && s.substr(s.size() - kTail.size()) == kTail;
}

Expect expectOf(const Property& p) noexcept {
    if (!accepts(p)) return Expect::Value;
    std::string_view s = p.placeholder;
    s.remove_suffix(kTail.size());
    if (s.size() >= 4 && s.substr(s.size() - 4) == " ou ") s.remove_suffix(4);
    if (const auto at = s.rfind(kSep); at != std::string_view::npos) s = s.substr(at + kSep.size());
    for (const Expect e : {Expect::Bool, Expect::Number, Expect::Text, Expect::Color, Expect::View, Expect::List,
                           Expect::Template, Expect::Time})
        if (s == word(e)) return e;
    return Expect::Value;
}

void mark(Property& p, Expect e, bool ownDefault) {
    if (accepts(p)) return;
    p.placeholder = p.placeholder.empty() ? invite(e) : p.placeholder + std::string(kSep) + invite(e);
    // "Revenir a la valeur par defaut" : une case videe (sauf si l'hote sait faire).
    if (ownDefault || !p.commit) return;
    p.commit = [inner = std::move(p.commit)](std::string_view s) { return inner(s == kToDefault ? std::string_view{} : s); };
}

void markWhole(Property& p, Expect e, bool asExpression) {
    mark(p, e);
    if (asExpression && p.expression.empty() && !trimmedView(p.value).empty()) p.expression = p.value;
    if (!p.commit) return;
    p.commit = [inner = std::move(p.commit)](std::string_view s) {
        if (s == kToDefault) return inner({});
        std::string_view t = trimmedView(s);
        if (!t.empty() && t.front() == '=') t = trimmedView(t.substr(1));
        return inner(t);
    };
}

std::string_view shownLabel(const Property& p) noexcept {
    std::string_view s = p.name;
    constexpr std::string_view kTag = " (expression)";
    if (accepts(p) && s.size() > kTag.size() && s.substr(s.size() - kTag.size()) == kTag) s.remove_suffix(kTag.size());
    return s;
}

std::string shownInvite(const Property& p, bool typeShown) {
    if (!accepts(p)) return p.placeholder;
    std::string_view s = p.placeholder;
    s.remove_suffix(kTail.size());
    if (s.size() >= 4 && s.substr(s.size() - 4) == " ou ") s.remove_suffix(4);
    std::string_view head, type = s;
    if (const auto at = s.rfind(kSep); at != std::string_view::npos) {
        head = s.substr(0, at);
        type = s.substr(at + kSep.size());
    }
    // Un texte a trous garde son "{variable}" : c'est une syntaxe, pas un type.
    if (typeShown) type = type == word(Expect::Template) ? std::string_view("{variable}") : std::string_view{};
    if (head.empty()) return std::string(type);
    return type.empty() ? std::string(head) : std::string(head) + std::string(kSep) + std::string(type);
}

std::string editText(const Property& p) { return p.expression.empty() ? p.value : "=" + p.expression; }

std::string normalized(const Property& p, std::string_view typed) {
    const std::string_view t = trimmedView(typed);
    if (!p.expression.empty() && (t.empty() || t == "=")) return "=";
    return std::string(typed);
}

bool clearRect(const PropertyGrid& grid, std::string_view name, gfx::Rect& out) {
    gfx::Rect cell;
    if (!grid.valueRect(name, cell)) return false;
    const Property* found = nullptr;
    struct Walk {
        std::string_view name;
        const Property*& found;
        void operator()(const std::vector<PropertyGrid::Category>& cats) const {
            for (const auto& c : cats) {
                for (const auto& p : c.properties)
                    if (!found && p.name == name) found = &p;
                (*this)(c.children);
            }
        }
    } walk{name, found};
    walk(grid.categories());
    if (!found || !accepts(*found) || found->expression.empty() || !found->commit) return false;
    // Le meme carre que le bouton de retour, au bout de la ligne (la case s'arrete a 1 px du bord) ;
    // 1.11.3 : a gauche du carre de legende.
    const float side = std::min(std::max(8.f, cell.h + 2.f - 8.f), 14.f);
    const float legend = found->legend ? std::min(std::max(10.f, cell.h + 2.f - 6.f), 18.f) + 8.f : 0.f;
    out = {cell.right() + 1.f - legend - side - 6.f, cell.y - 1.f + (cell.h + 2.f - side) * 0.5f, side, side};
    return true;
}
// ---- 1.10.3 : les reperes ($Vanne$) d'une case ----
namespace {
MarkerFinder gMarkerFinder = nullptr;
}

void setMarkerFinder(MarkerFinder finder) { gMarkerFinder = finder; }

std::vector<MarkerSpan> markersIn(std::string_view text, bool expression) {
    if (!gMarkerFinder || text.find('$') == std::string_view::npos) return {};
    return gMarkerFinder(text, expression);
}

std::vector<MarkerSpan> markersOf(const Property& p) {
    if (!p.expression.empty()) return markersIn(p.expression, true);
    return markersIn(p.value, false);   // 1.11 : une valeur se lit comme un texte (libelle, message)
}

bool hasMarker(const Property& p) { return !markersOf(p).empty(); }

std::string markerTip(const Property& p) {
    std::vector<std::string> names, keys;
    for (const auto& m : markersOf(p)) {
        std::string key = m.name;
        for (auto& ch : key) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (std::find(keys.begin(), keys.end(), key) != keys.end()) continue;
        keys.push_back(std::move(key));
        names.push_back("$" + m.name + "$");
    }
    // 1.11 (REP) : un repere ne change pas le calcul ; il dit ce qui varie quand on duplique.
    if (names.empty()) return {};
    if (names.size() == 1)
        return "Rep\xC3\xA8re " + names.front() + " : ce morceau varie quand on duplique (Dupliquer\xE2\x80\xA6, Ctrl+D). "
               "Ses $ ne changent pas le calcul.";
    std::string list;
    for (const auto& n : names) list += (list.empty() ? "" : ", ") + n;
    return "Rep\xC3\xA8res " + list + " : ces morceaux varient quand on duplique (Dupliquer\xE2\x80\xA6, Ctrl+D). "
           "Leurs $ ne changent pas le calcul.";
}

} // namespace exprfield
// ---- fin 1.10 (chantier K) ----

namespace {
// 1.10 (chantier K) : "Modifier l'expression..." sur une case sans expression :
// beginEdit ouvre alors un champ "=" (pas la bascule, la liste ou la palette).
bool gForceExprText = false;
// La place de la pastille fx devant la valeur (mesuree au dessin) et celle du X
// au bout de la ligne : le champ ouvert les laisse voir (maquette 1.10, scene 1).
float gFxSlot = 28.f;
bool hasClear(const PropertyGrid::Property& p) {
    return exprfield::accepts(p) && !p.expression.empty() && p.commit && p.type != PropertyGrid::ValueType::ReadOnly;
}
// 1.11.3 : la place du carre de legende au bout de la case (sa largeur et l'ecart).
float legendSide(float rowH) { return std::min(std::max(10.f, rowH - 6.f), 18.f); }
float legendSlot(const PropertyGrid::Property& p, float rowH) { return p.legend ? legendSide(rowH) + 8.f : 0.f; }
gfx::Rect fieldCell(const PropertyGrid::Property& p, gfx::Rect cell) {
    // 1.11.3 : le champ ouvert laisse voir le carre de legende.
    const float legend = legendSlot(p, cell.h + 2.f);
    if (legend > 0.f && cell.w - legend >= 60.f) cell.w -= legend;
    if (!exprfield::accepts(p) || p.type == PropertyGrid::ValueType::ReadOnly) return cell;
    const float right = hasClear(p) ? std::min(std::max(8.f, cell.h + 2.f - 8.f), 14.f) + 10.f : 0.f;
    if (cell.w - gFxSlot - right < 60.f) return cell;
    return {cell.x + gFxSlot, cell.y, cell.w - gFxSlot - right, cell.h};
}
// Le type ecrit en petit au bout du nom (maquette : BOOL, nombre, couleur, vue...).
std::string_view tagOf(exprfield::Expect e) noexcept {
    using E = exprfield::Expect;
    switch (e) {
        case E::Bool: return "BOOL";
        case E::Number: return "nombre";
        case E::Text: case E::Template: return "texte";
        case E::Color: return "couleur";
        case E::View: return "vue";
        case E::List: return "choix";
        case E::Time: return "TIME";
        case E::Value: break;
    }
    return {};
}
} // namespace

namespace propertygrid {
// 1.11 (integration I111, lien 5) : le crochet de "La page des expressions..." (App le branche).
namespace {
ExpressionPageHook& hookSlot() {
    static ExpressionPageHook hook;
    return hook;
}
} // namespace
void setExpressionPageHook(ExpressionPageHook hook) { hookSlot() = std::move(hook); }
const ExpressionPageHook& expressionPageHook() { return hookSlot(); }
} // namespace propertygrid

PropertyGrid::PropertyGrid(std::string id) : Widget(std::move(id)) {
    setFocusPolicy(true);
    setPadding({2.f, 2.f, 2.f, 2.f});
}

namespace {
// Memes categories, memes proprietes, dans le meme ordre : seules les valeurs
// ont pu changer.
bool sameShape(const std::vector<PropertyGrid::Category>& a, const std::vector<PropertyGrid::Category>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name || a[i].properties.size() != b[i].properties.size()) return false;
        for (std::size_t k = 0; k < a[i].properties.size(); ++k)
            if (a[i].properties[k].name != b[i].properties[k].name) return false;
        if (!sameShape(a[i].children, b[i].children)) return false;
    }
    return true;
}
// 1.10 finale : les memes categories (par leur nom), meme si une propriete apparait
// ou disparait (une expression posee, une raison sous la ligne rouge) : le meme
// objet, ou un objet du meme genre - on garde le defilement.
bool sameCategoryNames(const std::vector<PropertyGrid::Category>& a, const std::vector<PropertyGrid::Category>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name) return false;
    return true;
}
// 1.11 (R111) : le depliage suit chaque categorie PAR SON NOM, plus par son rang.
// Un groupe d'alarmes regle pour la premiere fois devient declare et monte en tete
// (hmi::alarmGroupsOf : les declares d'abord) : par le rang, le depliage passait
// a un autre groupe. D'abord le meme rang et le meme nom (le cas courant), puis le
// meme nom ailleurs (le premier pas encore repris : des noms pareils gardent leur
// ordre) ; les restes (une categorie renommee, celles d'un autre objet du meme
// genre) se reprennent dans l'ordre, comme avant. Une categorie nouvelle, sans
// reste en face, garde le depliage que l'hote lui donne.
void keepExpanded(const std::vector<PropertyGrid::Category>& from, std::vector<PropertyGrid::Category>& to) {
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::vector<std::size_t> match(to.size(), kNone);
    std::vector<bool> taken(from.size(), false);
    for (std::size_t i = 0; i < from.size() && i < to.size(); ++i)
        if (from[i].name == to[i].name) { match[i] = i; taken[i] = true; }
    std::unordered_map<std::string_view, std::vector<std::size_t>> byName;   // les autres, du dernier au premier
    for (std::size_t j = from.size(); j-- > 0;)
        if (!taken[j]) byName[from[j].name].push_back(j);
    for (std::size_t i = 0; i < to.size(); ++i) {
        if (match[i] != kNone) continue;
        const auto it = byName.find(to[i].name);
        if (it == byName.end() || it->second.empty()) continue;
        match[i] = it->second.back();
        it->second.pop_back();
        taken[match[i]] = true;
    }
    std::size_t rest = 0;
    for (std::size_t i = 0; i < to.size(); ++i) {
        if (match[i] != kNone) continue;
        while (rest < from.size() && taken[rest]) ++rest;
        if (rest == from.size()) break;
        match[i] = rest;
        taken[rest] = true;
    }
    for (std::size_t i = 0; i < to.size(); ++i) {
        if (match[i] == kNone) continue;
        to[i].expanded = from[match[i]].expanded;
        keepExpanded(from[match[i]].children, to[i].children);
    }
}
} // namespace

// ---- 1.10 (chantier K2) : la raison sous la ligne rouge ----
//  Sous une case a expression en erreur, une rangee "note" (le bit haut de `depth`) :
//  la raison ("X n'existe pas : veux-tu dire Y ?") et, quand la verification propose
//  un nom, le lien "Remplacer par Y" (un clic : l'expression corrigee, une commande).
//  Elle suit sa propriete : les recherches par nom trouvent la propriete d'abord.
namespace {
constexpr std::uint16_t kNoteRow = 0x8000;
// "Debit_Entre n'existe pas : veux-tu dire Debit_Entree ?" -> Debit_Entre, Debit_Entree.
bool noteFix(std::string_view why, std::string& from, std::string& to) {
    static constexpr std::string_view kMissing = " n'existe pas : veux-tu dire ";
    const auto miss = why.find(kMissing);
    const auto end = why.rfind(" ?");
    if (miss == std::string_view::npos || end == std::string_view::npos || end < miss + kMissing.size()) return false;
    from = std::string(why.substr(0, miss));
    to = std::string(why.substr(miss + kMissing.size(), end - miss - kMissing.size()));
    const auto plain = [](const std::string& w) {
        return !w.empty() && std::all_of(w.begin(), w.end(), [](char ch) {
            return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '.';
        });
    };
    return plain(from) && plain(to);
}
// `from` remplace par `to` quand il commence un chemin (pas apres un point), hors des chaines.
std::string replaceRoot(std::string_view expr, std::string_view from, std::string_view to) {
    const auto ident = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_'; };
    const auto same = [&](std::size_t at) {
        if (at + from.size() > expr.size()) return false;
        for (std::size_t k = 0; k < from.size(); ++k)
            if (std::toupper(static_cast<unsigned char>(expr[at + k])) != std::toupper(static_cast<unsigned char>(from[k]))) return false;
        return true;
    };
    std::string out;
    char quote = 0;
    for (std::size_t i = 0; i < expr.size();) {
        const char ch = expr[i];
        if (quote) { out += ch; if (ch == quote) quote = 0; ++i; continue; }
        if (ch == '\'' || ch == '"') { quote = ch; out += ch; ++i; continue; }
        if ((i == 0 || (!ident(expr[i - 1]) && expr[i - 1] != '.')) && same(i)
            && (i + from.size() >= expr.size() || !ident(expr[i + from.size()]))) {
            out += to;
            i += from.size();
            continue;
        }
        out += ch;
        ++i;
    }
    return out;
}
} // namespace
// ---- fin 1.10 (chantier K2) ----

void PropertyGrid::setCategories(std::vector<Category> cats) {
    // LES MEMES CATEGORIES, D'AUTRES VALEURS (on vient de modifier une
    // propriete) : on garde le defilement et ce qui etait replie. Revenir en
    // haut apres chaque saisie ferait chercher sa ligne a chaque fois.
    const bool same = sameShape(categories_, cats) || sameCategoryNames(categories_, cats);
    if (same) keepExpanded(categories_, cats);
    categories_ = std::move(cats);
    if (!same) scrollY_ = 0.f;   // onLayout ramene le defilement dans les bornes
    rebuildRows();
    // Le champ ouvert suit sa propriete ; si elle n'existe plus, il se ferme
    // sans rien valider - il editait quelque chose qui a disparu.
    if (editor_) {
        const int at = rowOf(editCategory_, editName_);
        if (at < 0) finishEdit(false, {});
        else {
            editCommit_ = rows_[static_cast<std::size_t>(at)].prop->commit;
            const auto cell = valueCell(static_cast<std::size_t>(at));
            editor_->setBounds(editIsList_ || editIsPalette_ ? cell : fieldCell(*rows_[static_cast<std::size_t>(at)].prop, cell));
        }
    }
}

int PropertyGrid::rowOf(std::string_view category, std::string_view name) const {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].prop && rows_[i].cat->name == category && rows_[i].prop->name == name) return static_cast<int>(i);
    return -1;
}

gfx::Rect PropertyGrid::valueCell(std::size_t row) const {
    const auto area = contentRect();
    const float split = area.x + area.w * nameRatio_;
    const float y = area.y + static_cast<float>(row) * rowHeight_ - scrollY_;
    return {split + 1.f, y + 1.f, area.right() - split - 2.f, rowHeight_ - 2.f};
}

bool PropertyGrid::valueRect(std::string_view name, gfx::Rect& out) const {
    const auto area = contentRect();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (!rows_[i].prop || rows_[i].prop->name != name) continue;
        out = valueCell(i);
        return out.y >= area.y && out.bottom() <= area.bottom() + 1.f;
    }
    return false;
}

bool PropertyGrid::revealValue(std::string_view name) {
    const auto area = contentRect();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (!rows_[i].prop || rows_[i].prop->name != name) continue;
        const float y = static_cast<float>(i) * rowHeight_;
        if (y < scrollY_) scrollY_ = y;
        else if (y + rowHeight_ > scrollY_ + area.h) scrollY_ = y + rowHeight_ - area.h;
        invalidateLayout();
        return true;
    }
    return false;
}

void PropertyGrid::beginEdit(std::size_t row) {
    finishEdit(false, {});
    if (row >= rows_.size() || !rows_[row].prop || (rows_[row].depth & kNoteRow) != 0) return;   // 1.10 (K2) : la raison
    const Property& p = *rows_[row].prop;
    if (!p.commit || p.type == ValueType::ReadOnly) return;
    // 1.10 (chantier K) : une case pilotee par une expression s'edite en texte
    // ("=Marche"), meme une bascule, une liste ou une couleur : on modifie
    // l'expression, pas la valeur fixe qu'elle cache.
    const bool exprText = exprfield::accepts(p) && (!p.expression.empty() || gForceExprText);

    // Une bascule n'a pas besoin de champ : le clic EST la modification.
    if (p.type == ValueType::Boolean && !exprText) {
        const std::string name = p.name;
        const std::string next = isTrueText(p.value) ? "FALSE" : "TRUE";
        auto commit = p.commit;           // la grille peut etre refaite pendant commit
        if (commit(next)) propertyChanged->emit(name, next);
        else propertyRejected->emit(name, next);
        return;
    }

    editCategory_ = rows_[row].cat->name;
    editName_     = p.name;
    editCommit_   = p.commit;
    const auto cell = valueCell(row);

    if (p.type == ValueType::Color && !exprText) {
        // LA PALETTE : une pastille choisie est appliquee, et la case se ferme.
        auto palette = std::make_unique<ColorPalette>(id() + ".palette");
        palette->setValue(p.value);
        palette->setTitle(p.name);    // 1.10 (chantier Q) : la propriete en titre de la palette
        if (paletteColors_) palette->setProjectColors(paletteColors_());
        auto* raw = palette.get();
        editor_ = &addChild(std::move(palette));
        editIsList_ = false;
        editIsPalette_ = true;
        editor_->setBounds(cell);
        raw->applied->connect([this, raw](const std::string& color) {
            if (editor_ != raw) return;
            finishEdit(true, color);
        }).release();
        raw->closed->connect([this, raw] {
            if (editor_ == raw) finishEdit(false, {});
        }).release();
        raw->open();                  // et prend le clavier (le code #RRGGBB, Echap)
        invalidate();
        return;
    }

    if (p.type == ValueType::Enum && !p.enumValues.empty() && !exprText) {
        auto list = std::make_unique<DropDown>(id() + ".edit");
        std::vector<DropDown::Item> items;
        int current = -1;
        for (std::size_t i = 0; i < p.enumValues.size(); ++i) {
            items.push_back({p.enumValues[i].empty() ? std::string("(aucun)") : p.enumValues[i], p.enumValues[i]});
            if (p.enumValues[i] == p.value) current = static_cast<int>(i);
        }
        list->setItems(std::move(items));
        list->setSelectedIndex(current);
        auto* raw = list.get();
        editor_ = &addChild(std::move(list));
        editIsList_ = true;
        editor_->setBounds(cell);
        raw->selectionChanged->connect([this, raw](int) {
            if (editor_ != raw) return;
            const auto* item = raw->selectedItem();
            finishEdit(item != nullptr, item ? item->value : std::string{});
        }).release();
        // Deroulee tout de suite : un clic de plus pour l'ouvrir serait un clic
        // pour rien.
        const gfx::Point at{cell.x + cell.w * 0.5f, cell.y + cell.h * 0.5f};
        raw->dispatch(MouseDown{at, MouseButton::Left, 1, {}});
        raw->dispatch(MouseUp{at, MouseButton::Left, {}});
    } else {
        auto field = std::make_unique<CellField>(id() + ".edit");
        // 1.10 (chantier K) : le "=" reste devant l'expression ; vide, l'invite.
        const bool accepts = exprfield::accepts(p);
        field->setText(accepts ? (gForceExprText && p.expression.empty() ? std::string("=") : exprfield::editText(p)) : p.value);
        if (accepts) field->setPlaceholder(exprfield::shownInvite(p, true));   // 1.10.1 (U2) : sans "ou = expression"
        if (fieldAssist_)
            if (auto assist = fieldAssist_(rows_[row].cat->name, p)) field->setAssist(std::move(assist));
        // 1.10 (chantier K) : Ctrl+Suppr retire l'expression (comme le X).
        if (hasClear(p)) {
            const std::string cat = rows_[row].cat->name, name = p.name;
            field->clearExpression = [this, cat, name] {
                const int at = rowOf(cat, name);
                const auto commit = at >= 0 ? rows_[static_cast<std::size_t>(at)].prop->commit : nullptr;
                finishEdit(false, {});
                if (!commit) return;
                if (commit("=")) propertyChanged->emit(name, "=");
                else propertyRejected->emit(name, "=");
                invalidate();
            };
        }
        auto* raw = field.get();
        editor_ = &addChild(std::move(field));
        editIsList_ = false;
        // 1.10 (chantier K) : la pastille fx et le X restent visibles a cote du champ.
        editor_->setBounds(fieldCell(p, cell));
        raw->editingDone->connect([this, raw](const std::string& text) {
            if (editor_ != raw) return;   // deja ferme (Entree, puis perte du focus)
            finishEdit(!raw->cancelled, text);
        }).release();
        raw->start();
        // 1.10 (chantier K) : "Modifier l'expression..." - le curseur apres le "=".
        if (gForceExprText) (void)raw->dispatch(KeyDown{Key::End, {}, false});
    }
    invalidate();
}

// ---- lot 16 : l'edition dans une case de TableView -----------------------------
bool TableView::beginCellEdit(RowIndex modelRow, std::size_t col) {
    if (cellEditor_) finishCellEdit(false, {});
    if (!model_ || col >= columns_.size() || !columns_[col].visible || !model_->editable(modelRow, col)) return false;
    const auto it = std::find(view_.begin(), view_.end(), modelRow);
    if (it == view_.end()) return false;
    const auto vi = static_cast<std::size_t>(it - view_.begin());
    // La ligne amenee en vue, pour que la case ait une place a l'ecran.
    const float y = static_cast<float>(vi) * rowHeight_;
    const float h = contentRect().h - headerHeight_;
    if (y < scrollY_) scrollY_ = y;
    else if (y + rowHeight_ > scrollY_ + h) scrollY_ = std::max(0.f, y + rowHeight_ - h);
    gfx::Rect cell;
    if (!cellRect(vi, col, cell)) cell = {contentRect().x, contentRect().y + headerHeight_, columns_[col].width, rowHeight_};
    editRow_ = modelRow;
    editCol_ = col;
    const auto choices = model_->cellChoices(modelRow, col);
    const std::string current = model_->cellText(modelRow, col);
    if (!choices.empty()) {
        auto list = std::make_unique<DropDown>(id() + ".celledit");
        std::vector<DropDown::Item> items;
        int selected = -1;
        for (std::size_t i = 0; i < choices.size(); ++i) {
            items.push_back({choices[i].empty() ? std::string("(aucun)") : choices[i], choices[i]});
            if (choices[i] == current) selected = static_cast<int>(i);
        }
        list->setItems(std::move(items));
        list->setSelectedIndex(selected);
        auto* raw = list.get();
        cellEditor_ = &addChild(std::move(list));
        cellIsList_ = true;
        cellEditor_->setBounds(cell);
        raw->selectionChanged->connect([this, raw](int) {
            if (cellEditor_ != raw) return;
            const auto* item = raw->selectedItem();
            finishCellEdit(item != nullptr, item ? item->value : std::string{});
        }).release();
        const gfx::Point at{cell.x + cell.w * 0.5f, cell.y + cell.h * 0.5f};
        raw->dispatch(MouseDown{at, MouseButton::Left, 1, {}});
        raw->dispatch(MouseUp{at, MouseButton::Left, {}});
    } else {
        auto field = std::make_unique<CellField>(id() + ".celledit");
        field->setText(current);
        auto* raw = field.get();
        cellEditor_ = &addChild(std::move(field));
        cellIsList_ = false;
        cellEditor_->setBounds(cell);
        raw->editingDone->connect([this, raw](const std::string& text) {
            if (cellEditor_ != raw) return;   // deja ferme (Entree, puis perte du focus)
            finishCellEdit(!raw->cancelled, text);
        }).release();
        raw->start();
    }
    invalidate();
    return true;
}

void TableView::finishCellEdit(bool commit, const std::string& text) {
    if (!cellEditor_) return;
    activeCellEditor_ = removeChild(*cellEditor_);   // pas detruit tout de suite : il emet peut-etre
    cellEditor_ = nullptr;
    cellIsList_ = false;
    const RowIndex row = editRow_;
    const std::size_t col = editCol_;
    invalidate();
    grabFocus();
    if (!commit || !model_) return;
    if (text == model_->cellText(row, col)) return;          // rien de change
    const bool ok = model_->setCellText(row, col, text);
    cellEdited->emit(row, col, text, ok);
}

InputText* TableView::activeCellField() const noexcept {
    return cellIsList_ ? nullptr : dynamic_cast<InputText*>(cellEditor_);
}

DropDown* TableView::activeCellList() const noexcept {
    return cellIsList_ ? dynamic_cast<DropDown*>(cellEditor_) : nullptr;
}

InputText* PropertyGrid::activeField() const noexcept {
    return editIsList_ ? nullptr : dynamic_cast<InputText*>(editor_);
}

ColorPalette* PropertyGrid::activePalette() const noexcept {
    return editIsPalette_ ? dynamic_cast<ColorPalette*>(editor_) : nullptr;
}

void PropertyGrid::finishEdit(bool commit, const std::string& text) {
    if (!editor_) return;
    // 1.10 (chantier K) : une case a expression videe, ou laissee a "=", retire
    // l'expression (commit("=")) : la propriete revient a sa valeur fixe.
    std::string sent = text;
    if (commit && !editIsList_ && !editIsPalette_)
        if (const int at = rowOf(editCategory_, editName_); at >= 0 && exprfield::accepts(*rows_[static_cast<std::size_t>(at)].prop))
            sent = exprfield::normalized(*rows_[static_cast<std::size_t>(at)].prop, text);
    auto apply = std::move(editCommit_);
    const std::string name = editName_;
    // Pas detruit tout de suite : c'est peut-etre lui qui emet en ce moment.
    activeEditor_ = removeChild(*editor_);
    editor_ = nullptr;
    editIsList_ = false;
    editIsPalette_ = false;
    editCategory_.clear();
    editName_.clear();
    editCommit_ = nullptr;
    invalidate();
    if (!commit || !apply) return;
    if (apply(sent)) propertyChanged->emit(name, sent);
    else propertyRejected->emit(name, sent);
}

void PropertyGrid::clearProperties() { categories_.clear(); rebuildRows(); }

void PropertyGrid::setNameColumnRatio(float r) {
    nameRatio_ = std::clamp(r, 0.15f, 0.85f);
    invalidate();
}

void PropertyGrid::setShowDescriptionPane(bool s) { descriptionPane_ = s; invalidateLayout(); }

namespace {
void setExpandedRecursive(std::vector<PropertyGrid::Category>& cats, bool expanded) {
    for (auto& c : cats) {
        c.expanded = expanded;
        setExpandedRecursive(c.children, expanded);
    }
}
} // namespace

void PropertyGrid::expandAll()   { setExpandedRecursive(categories_, true);  rebuildRows(); }
void PropertyGrid::collapseAll() { setExpandedRecursive(categories_, false); rebuildRows(); }


void PropertyGrid::rebuildRows() {
    rows_.clear();
    // Recursive flatten; category rows carry a null property, property rows a
    // null category, so one row type serves both.
    struct Walker {
        std::vector<VisualRow>& out;
        void operator()(const Category& c, std::uint16_t depth) {
            out.push_back(VisualRow{&c, nullptr, depth});
            if (!c.expanded) return;
            for (const auto& p : c.properties) {
                out.push_back(VisualRow{&c, &p, static_cast<std::uint16_t>(depth + 1)});
                // 1.10 (chantier K2) : la raison sous la ligne rouge.
                if (exprfield::accepts(p) && !p.expression.empty() && !p.exprError.empty())
                    out.push_back(VisualRow{&c, &p, static_cast<std::uint16_t>((depth + 1) | kNoteRow)});
            }
            for (const auto& child : c.children)
                (*this)(child, static_cast<std::uint16_t>(depth + 1));
        }
    } walk{rows_};
    for (const auto& c : categories_) walk(c, 0);
    invalidate();
}

int PropertyGrid::rowAt(float globalY) const {
    const int r = static_cast<int>((globalY - contentRect().y + scrollY_) / rowHeight_);
    return (r < 0 || r >= static_cast<int>(rows_.size())) ? -1 : r;
}

namespace {
// 1.9 : le bouton de retour a la valeur d'origine, au bout de la ligne `row`.
gfx::Rect revertBox(const gfx::Rect& row) {
    const float side = std::min(std::max(8.f, row.h - 8.f), 14.f);
    return {row.right() - side - 6.f, row.y + (row.h - side) * 0.5f, side, side};
}
// 1.11.3 : l'infobulle d'un carre sans texte.
constexpr std::string_view valuekindHint = "Le carr\xC3\xA9 dit d'o\xC3\xB9 vient la valeur.";
// 1.11.3 : le carre de legende, tout au bout de la ligne ; le reste (le X, le
// retour, la valeur) se range a sa gauche (endOf).
gfx::Rect legendBox(const gfx::Rect& row) {
    const float side = legendSide(row.h);
    return {row.right() - side - 6.f, row.y + (row.h - side) * 0.5f, side, side};
}
gfx::Rect endOf(const PropertyGrid::Property& p, const gfx::Rect& row) {
    if (!p.legend) return row;
    return {row.x, row.y, std::max(0.f, row.w - legendSlot(p, row.h)), row.h};
}
// 1.9 : le nom montre d'une propriete a pastille de couleur - sans la fin
// "  .  <texte de la pastille>" que la pastille dit deja (le nom reste entier
// pour l'hote, les tests et les scripts).
std::string_view withoutPillTail(std::string_view name, const std::optional<ColorPill>& pill) {
    if (!pill || pill->text.empty()) return name;
    const std::string tail = "  \xC2\xB7  " + pill->text;
    if (name.size() > tail.size() && name.substr(name.size() - tail.size()) == tail) name.remove_suffix(tail.size());
    return name;
}
std::string_view shownName(const PropertyGrid::Property& p) {
    std::string_view s = withoutPillTail(p.name, p.pill);
    // 1.10.1 (U2) : une case a expression sans son "(expression)" : la pastille fx et
    // le type au bout du nom le disent deja (Visibilite (expression) -> Visibilite).
    constexpr std::string_view kTag = " (expression)";
    if (exprfield::accepts(p) && s.size() > kTag.size() && s.substr(s.size() - kTag.size()) == kTag) s.remove_suffix(kTag.size());
    return s;
}
// 1.10 (chantier K) : le bouton X "Retirer l'expression" d'une case a expression,
// au bout de la ligne (revertBox) ; le bouton de retour d'une surcharge passe a sa gauche.
bool showsClear(const PropertyGrid::Property& p) {
    return exprfield::accepts(p) && !p.expression.empty() && p.commit && p.type != PropertyGrid::ValueType::ReadOnly;
}
gfx::Rect beforeClear(const PropertyGrid::Property& p, const gfx::Rect& row) {
    if (!showsClear(p)) return row;
    const gfx::Rect b = revertBox(row);
    return {row.x, row.y, std::max(0.f, b.x - 2.f - row.x), row.h};
}
// Retirer l'expression : commit("=") (l'hote revient a la valeur fixe). La
// fonction est copiee : l'hote refait souvent la grille pendant commit.
void removeExpression(PropertyGrid& grid, const PropertyGrid::Property& p) {
    const std::string name = p.name;
    const auto commit = p.commit;
    if (!commit) return;
    if (commit("=")) grid.propertyChanged->emit(name, "=");
    else grid.propertyRejected->emit(name, "=");
}
} // namespace

// ---- Lot API 8 : les expressions impossibles ----
bool PropertyGrid::hasTooltip() const {
    if (Widget::hasTooltip()) return true;
    for (const auto& vr : rows_)
        if (vr.prop && (!vr.prop->expression.empty() || vr.prop->overridden || vr.prop->revert || exprfield::accepts(*vr.prop)
                        || vr.prop->legend))
            return true;
    return false;
}

std::string PropertyGrid::liveTooltip(gfx::Point mouse) const {
    const int ri = contentRect().contains(mouse) ? rowAt(mouse.y) : -1;
    // 1.10 (chantier K2) : la rangee de la raison dit ce que fait un clic.
    if (ri >= 0 && (rows_[static_cast<std::size_t>(ri)].depth & kNoteRow) != 0 && rows_[static_cast<std::size_t>(ri)].prop) {
        const auto& why = rows_[static_cast<std::size_t>(ri)].prop->exprError;
        std::string from, to;
        return noteFix(why, from, to) ? why + "\nClic : remplacer " + from + " par " + to + " (Ctrl+Z pour annuler)." : why;
    }
    const Property* p = ri >= 0 ? rows_[static_cast<std::size_t>(ri)].prop : nullptr;
    // 1.10.3 : une case a repere ($Vanne$) le dit en tete de son infobulle.
    const std::string mtip = p ? exprfield::markerTip(*p) : std::string{};
    if (p && mtip.size() && p->expression.empty() && !p->overridden && !p->revert && !exprfield::accepts(*p) && !p->legend) {
        const std::string base = Widget::liveTooltip(mouse);
        return base.empty() ? mtip : mtip + "\n" + base;
    }
    if (!p || (p->expression.empty() && !p->overridden && !p->revert && !exprfield::accepts(*p) && !p->legend))
        return Widget::liveTooltip(mouse);
    // 1.10 (chantier K) : le X dit ce qu'il fait ; une case vide, ce qu'elle accepte.
    {
        const auto area = contentRect();
        const float y = area.y + static_cast<float>(ri) * rowHeight_ - scrollY_;
        const gfx::Rect row{area.x, y, area.w, rowHeight_};
        // 1.11.3 : le carre de legende dit d'ou vient la valeur, et ce que fait un clic.
        if (p->legend && legendBox(row).contains(mouse)) {
            std::string tip = p->legend->tip;
            if (legendClickable_)
                tip += (tip.empty() ? "" : "\n") + std::string("Clic : la liste des carr\xC3\xA9s, puis le s\xC3\xA9lecteur de valeur.");
            return tip.empty() ? std::string(valuekindHint) : tip;
        }
        if (showsClear(*p) && revertBox(endOf(*p, row)).contains(mouse)) return std::string(exprfield::kRemoveTip);
        // 1.9 : le bouton de retour dit ce qu'il fait.
        if (p->revert && revertBox(beforeClear(*p, endOf(*p, row))).contains(mouse))
            return p->revertTip.empty() ? std::string("Revenir \xC3\xA0 la valeur du symbole") : p->revertTip;
    }
    if (p->expression.empty() && !p->overridden && exprfield::accepts(*p)) {
        std::string tip = "Accepte une expression : tape = puis l'expression (Ctrl+Espace : les propositions).";
        if (const std::string invite = exprfield::shownInvite(*p, false); p->value.empty() && !invite.empty())
            tip = "Vide : " + invite + ".\n" + tip;
        return mtip.empty() ? tip : mtip + "\n" + tip;
    }
    std::string tip = mtip;
    if (!p->expression.empty()) {
        tip += (tip.empty() ? "" : "\n") + ("Expression : " + (exprfield::accepts(*p) ? "=" + p->expression : p->expression));
        const std::string value = exprValueFor_ ? exprValueFor_(*p) : std::string{};
        if (!value.empty() || !p->exprValue.empty()) tip += "\nValeur actuelle : " + (value.empty() ? p->exprValue : value);
        if (!p->exprError.empty()) tip += "\nErreur : " + p->exprError;
    }
    // 1.9 : une valeur surchargee montre la valeur d'origine (celle du symbole).
    if (p->overridden) {
        if (!tip.empty()) tip += "\n";
        tip += "Surcharg\xC3\xA9 sur cet objet\n";
        tip += (p->overrideFrom.empty() ? std::string("Valeur du symbole") : p->overrideFrom) + " : "
             + (p->overrideDefault.empty() ? std::string("(vide)") : p->overrideDefault);
        if (p->revert) tip += "\n\xE2\x86\xBA au bout de la ligne : revenir \xC3\xA0 cette valeur";
    }
    if (tip.empty()) return Widget::liveTooltip(mouse);
    return tip;
}

bool PropertyGrid::revertRect(std::string_view name, gfx::Rect& out) const {
    const auto area = contentRect();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (!rows_[i].prop || rows_[i].prop->name != name) continue;
        if (!rows_[i].prop->revert) return false;
        const float y = area.y + static_cast<float>(i) * rowHeight_ - scrollY_;
        out = revertBox(beforeClear(*rows_[i].prop, endOf(*rows_[i].prop, {area.x, y, area.w, rowHeight_})));
        return y >= area.y && y + rowHeight_ <= area.bottom() + 1.f;
    }
    return false;
}

bool PropertyGrid::legendRect(std::string_view name, gfx::Rect& out) const {
    const auto area = contentRect();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (!rows_[i].prop || (rows_[i].depth & kNoteRow) != 0 || rows_[i].prop->name != name) continue;
        if (!rows_[i].prop->legend) return false;
        const float y = area.y + static_cast<float>(i) * rowHeight_ - scrollY_;
        out = legendBox({area.x, y, area.w, rowHeight_});
        return y >= area.y && y + rowHeight_ <= area.bottom() + 1.f;
    }
    return false;
}

gfx::Color legendColor(PropertyGrid::LegendStyle st, bool dark) noexcept {
    using S = PropertyGrid::LegendStyle;
    switch (st) {
        case S::Api:      return dark ? gfx::Color{92, 163, 255, 255} : gfx::Color{29, 98, 204, 255};
        case S::Hmi:      return dark ? gfx::Color{53, 198, 183, 255} : gfx::Color{10, 122, 112, 255};
        case S::System:   return dark ? gfx::Color{242, 168, 62, 255} : gfx::Color{158, 88, 0, 255};
        case S::Local:    return dark ? gfx::Color{186, 149, 255, 255} : gfx::Color{109, 65, 195, 255};
        case S::Error:    return dark ? gfx::Color{255, 103, 103, 255} : gfx::Color{196, 39, 39, 255};
        case S::Formula:
        case S::Markers:  return dark ? gfx::Color{160, 166, 176, 255} : gfx::Color{79, 86, 97, 255};
        case S::Constant:
        case S::Empty:    break;
    }
    return dark ? gfx::Color{154, 161, 172, 255} : gfx::Color{124, 132, 144, 255};
}

void paintLegend(const PaintContext& ctx, gfx::Rect box, const PropertyGrid::Legend& l) {
    using S = PropertyGrid::LegendStyle;
    const bool dark = ctx.theme.isDark();
    const gfx::Color col = legendColor(l.style, dark);
    const gfx::Color ink = dark ? gfx::Color{16, 18, 22, 255} : gfx::Color{255, 255, 255, 255};
    const float radius = std::max(2.f, box.h * 0.2f);
    gfx::Color textCol = ink;
    if (l.style == S::Constant || l.style == S::Empty) {
        // Une constante : un cadre, pas de fond ; vide : un cadre estompe.
        ctx.r.fillRoundedRect(box, l.style == S::Empty ? ctx.theme.color.border : col, radius);
        ctx.r.fillRoundedRect({box.x + 1.5f, box.y + 1.5f, box.w - 3.f, box.h - 3.f}, ctx.theme.color.panelBg, std::max(1.f, radius - 1.f));
        textCol = dark ? gfx::Color{214, 218, 224, 255} : gfx::Color{60, 67, 77, 255};
    } else {
        ctx.r.fillRoundedRect(box, col, radius);
    }
    if (l.text.empty()) return;
    const auto font = box.h >= 17.f && l.text.size() <= 1 ? ctx.theme.font.uiBold : ctx.theme.font.smallUi;
    const float lh = ctx.r.lineHeight(font);
    const float tw = ctx.r.measure(l.text, font).width;
    const bool dots = !l.dots.empty() && (l.style == S::Formula || l.style == S::Markers);
    const float ty = dots ? box.y + 1.f : box.y + (box.h - lh) * 0.5f;
    const gfx::Point at{box.x + (box.w - tw) * 0.5f, ty};
    ctx.r.drawText(at, l.text, font, textCol);
    ctx.r.drawText({at.x + 0.6f, at.y}, l.text, font, textCol);   // le gras simule (Fonts : pas de face grasse)
    if (dots) {
        const float d = 3.f, gap = 2.f;
        const float total = static_cast<float>(l.dots.size()) * d + static_cast<float>(l.dots.size() - 1) * gap;
        float x = box.x + (box.w - total) * 0.5f;
        for (const auto z : l.dots) {
            const gfx::Rect dot{x, box.bottom() - d - 2.f, d, d};
            ctx.r.fillRoundedRect({dot.x - 0.5f, dot.y - 0.5f, d + 1.f, d + 1.f}, ink, (d + 1.f) * 0.5f);
            ctx.r.fillRoundedRect(dot, legendColor(z, dark), d * 0.5f);
            x += d + gap;
        }
    }
}

void PropertyGrid::onLayout() {
    const float maxScroll =
        std::max(0.f, static_cast<float>(rows_.size()) * rowHeight_ - contentRect().h);
    scrollY_ = std::clamp(scrollY_, 0.f, maxScroll);
    if (editor_) {
        const int at = rowOf(editCategory_, editName_);
        if (at >= 0) {
            const auto cell = valueCell(static_cast<std::size_t>(at));
            editor_->setBounds(editIsList_ || editIsPalette_ ? cell : fieldCell(*rows_[static_cast<std::size_t>(at)].prop, cell));
        }
    }
}

void PropertyGrid::onPaint(const PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto  area = contentRect();
    const float rowH = rowHeight_ = ctx.theme.metric.rowHeight;

    // Un champ ferme a fini d'emettre depuis longtemps : on peut le detruire.
    // Une liste refermee sans choix se ferme, sans rien changer.
    activeEditor_.reset();
    if (editor_ && editIsList_) {
        const auto* list = dynamic_cast<const DropDown*>(editor_);
        if (list && !list->isOpen()) finishEdit(false, {});
    }

    ctx.r.fillRect(bounds(), c.panelBg);
    ctx.r.strokeRect(bounds(), c.border, 1.f);
    if (rowH <= 0.f) return;

    const float split = area.x + area.w * nameRatio_;
    const auto first = static_cast<std::size_t>(std::max(0.f, scrollY_ / rowH));
    const auto last  = std::min(rows_.size(), first + static_cast<std::size_t>(area.h / rowH) + 1);
    const float textY = (rowH - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f;

    // 1.9 : la case de la valeur d'une propriete SURCHARGEE (fond accent leger,
    // lisere de 3 px a gauche, etiquette SURCHARGE a droite) et le bouton de
    // retour au bout de la ligne. Rend la limite droite du texte de la valeur.
    const auto paintOverride = [&](const Property& p, const gfx::Rect& r) {
        float right = r.right() - 6.f;
        if (p.revert) {
            const gfx::Rect b = revertBox(r);
            drawIcon(ctx.r, Icon::Undo, b, p.overridden ? c.accent : c.textMuted);
            right = b.x - 6.f;
        }
        if (p.overridden) {
            ctx.r.fillRect({split + 1.f, r.y, r.right() - split - 1.f, rowH}, c.accent.withAlpha(26));
            ctx.r.fillRect({split + 1.f, r.y + 1.f, 3.f, rowH - 2.f}, c.accentHover);
            static const std::string tag = "SURCHARG\xC3\x89";
            const float tagW = ctx.r.measure(tag, ctx.theme.font.caption).width + 14.f;
            // 1.9 finale : une case a cocher (Active d'une alarme) ne demande que sa case ;
            // un texte, 60 px - l'etiquette se voit aussi dans un volet etroit.
            const float need = p.type == ValueType::Boolean ? 26.f : 60.f;
            if (right - tagW > split + need) {
                const ColorPill pill{tag, c.accent, c.accent, c.accent.withAlpha(30), std::nullopt};
                drawColorPill(ctx, right - tagW, r, pill, tagW, c.panelBg);
                right -= tagW + 6.f;
            }
        }
        return right;
    };

    for (auto i = first; i < last; ++i) {
        const auto& vr = rows_[i];
        const gfx::Rect r{area.x, area.y + static_cast<float>(i) * rowH - scrollY_, area.w, rowH};
        // 1.10 (chantier K2) : la raison sous la ligne rouge, et "Remplacer par ...".
        if ((vr.depth & kNoteRow) != 0 && vr.prop) {
            ctx.r.fillRect(r, c.error.withAlpha(22));
            ctx.r.fillRect({r.x, r.y, 3.f, rowH}, c.error);
            const auto font = ctx.theme.font.smallUi;
            const float lh = ctx.r.lineHeight(font);
            const float ty = r.y + (rowH - lh) * 0.5f;
            float right = r.right() - 8.f;
            std::string from, to;
            if (vr.prop->commit && noteFix(vr.prop->exprError, from, to)) {
                const std::string link = "Remplacer par " + to;
                const float lw = ctx.r.measure(link, font).width;
                const float lx = std::max(r.x + 18.f, right - lw);
                ctx.r.drawText({lx, ty}, link, font, c.accent);
                ctx.r.line({lx, ty + lh - 1.f}, {lx + lw, ty + lh - 1.f}, c.accent, 1.f);
                right = lx - 10.f;
            }
            drawClipped(ctx, {r.x + 18.f, ty}, vr.prop->exprError, font, c.error, right - r.x - 18.f);
            continue;
        }
        const float indent = r.x + 4.f + static_cast<float>(vr.depth) * 12.f;

        if (!vr.prop) {                        // category header row
            ctx.r.fillRect(r, c.headerBg);
            // 1.9 : une partie teintee (le violet du simule) : son fond et son titre.
            const bool tinted = vr.cat->accent.has_value();
            if (tinted) ctx.r.fillRect(r, vr.cat->accent->withAlpha(ctx.theme.isDark() ? 52 : 40));
            const gfx::Color title = tinted ? ctx.theme.onSurface(*vr.cat->accent) : c.text;
            drawChevron(ctx, {indent + 6.f, r.y + rowH * 0.5f}, vr.cat->expanded, tinted ? title : c.textMuted);
            // 1.9 (chantier U) : la pastille de couleur apres le titre (sans la fin qu'elle dit).
            const std::string_view head = withoutPillTail(vr.cat->name, vr.cat->pill);
            drawClipped(ctx, {indent + 18.f, r.y + textY}, head,
                        ctx.theme.font.uiBold, title, r.w - 24.f);
            if (vr.cat->pill) {
                const float px = indent + 18.f + ctx.r.measure(head, ctx.theme.font.uiBold).width + 8.f;
                (void)drawColorPill(ctx, px, r, *vr.cat->pill, r.right() - px - 6.f, c.headerBg);
            }
        } else if (!vr.prop->expression.empty()) {
            // ---- Lot API 8 : les expressions impossibles ----
            // Une propriete pilotee par une expression se voit de loin : la ligne
            // teintee et une barre a gauche, une pastille "fx" pleine, l'expression
            // en chasse fixe. En erreur : le rouge du theme, l'expression soulignee.
            const bool broken = !vr.prop->exprError.empty();
            // 1.10.3 : une expression a repere ($Vanne$) : l'ambre, la pastille "$".
            const auto marks = exprfield::markersOf(*vr.prop);
            const gfx::Color tone = broken ? c.error : !marks.empty() ? c.warning : c.accent;
            ctx.r.fillRect(r, tone.withAlpha(36));
            ctx.r.fillRect({r.x, r.y, 3.f, rowH}, tone);
            const auto pillFont = ctx.theme.font.uiBold;
            const float pillW = ctx.r.measure("fx", pillFont).width + 10.f;
            const float pillH = std::min(rowH - 6.f, ctx.r.lineHeight(pillFont) + 2.f);
            // 1.10 (chantier K) : une case a expression porte sa pastille dans la
            // valeur, devant "=expression" (la pastille en creux d'une case sans
            // expression est au meme endroit) ; le nom garde sa place.
            const bool inValue = exprfield::accepts(*vr.prop);
            const gfx::Rect pill{inValue ? split + 6.f : indent + 4.f, r.y + (rowH - pillH) * 0.5f, pillW, pillH};
            const auto drawPill = [&] {
                ctx.r.fillRoundedRect(pill, tone, pillH * 0.5f);
                if (marks.empty() || broken) {
                    ctx.r.drawText({pill.x + 5.f, pill.y + (pillH - ctx.r.lineHeight(pillFont)) * 0.5f}, "fx", pillFont, c.selectionText);
                } else {
                    const float gw = ctx.r.measure("$", pillFont).width;
                    ctx.r.drawText({pill.x + (pill.w - gw) * 0.5f, pill.y + (pillH - ctx.r.lineHeight(pillFont)) * 0.5f}, "$", pillFont,
                                   gfx::Color{40, 28, 0, 255});
                }
            };
            if (!inValue) drawPill();          // dans la valeur : apres le fond d'une surcharge
            else gFxSlot = pillW + 6.f;
            // Le nom, sans le "f" crochet que la grille remplace par la pastille.
            std::string_view name = shownName(*vr.prop);
            if (const auto at = name.find("  \xC6\x92"); at != std::string_view::npos) name = name.substr(0, at);
            const float nameX = inValue ? indent + 6.f : pill.right() + 6.f;
            float nameRight = split - 6.f;
            if (inValue) {
                // Le type attendu, en petit au bout du nom.
                const std::string_view tag = tagOf(exprfield::expectOf(*vr.prop));
                const float tw = tag.empty() ? 0.f : ctx.r.measure(tag, ctx.theme.font.smallUi).width;
                // 1.11 (R111, recette T2-17) : le nom d'abord - le type ne se met au
                // bout que si le nom entier tient encore (« Taille du texte », et non
                // « Taille du t... nombre »).
                if (tw > 0.f && nameRight - tw - 6.f - nameX >= ctx.r.measure(name, ctx.theme.font.ui).width) {
                    ctx.r.drawText({nameRight - tw, r.y + (rowH - ctx.r.lineHeight(ctx.theme.font.smallUi)) * 0.5f}, tag,
                                   ctx.theme.font.smallUi, c.textDisabled);
                    nameRight -= tw + 6.f;
                }
            }
            drawClipped(ctx, {nameX, r.y + textY}, name, ctx.theme.font.ui, c.text, nameRight - nameX);
            // 1.9 : surchargee (le lisere et l'etiquette sur la valeur), et la
            // pastille de couleur devant l'expression.
            // 1.10 (chantier K) : le X "Retirer l'expression" au bout de la ligne.
            if (showsClear(*vr.prop)) drawIcon(ctx.r, Icon::Close, revertBox(endOf(*vr.prop, r)), c.textMuted);
            const float exprRight = paintOverride(*vr.prop, beforeClear(*vr.prop, endOf(*vr.prop, r)));
            if (inValue) drawPill();
            float exprX = inValue ? pill.right() + 6.f : split + 6.f;
            if (vr.prop->pill)
                if (const float w = drawColorPill(ctx, exprX, r, *vr.prop->pill, exprRight - exprX - 30.f, c.panelBg); w > 0.f)
                    exprX += w + 6.f;
            const float exprW = std::max(0.f, exprRight - exprX);
            const auto mono = ctx.theme.font.mono;
            const float monoY = r.y + (rowH - ctx.r.lineHeight(mono)) * 0.5f;
            // 1.10 (chantier K) : le "=" reste devant l'expression.
            const std::string shownExpr = exprfield::accepts(*vr.prop) ? "=" + vr.prop->expression : vr.prop->expression;
            // 1.10.3 : chaque repere surligne en ambre, sous le texte.
            {
                const std::size_t lead = exprfield::accepts(*vr.prop) ? 1u : 0u;
                const std::string_view se(shownExpr);
                for (const auto& m : marks) {
                    if (lead + m.at + m.length > se.size()) break;
                    const float x0 = exprX + ctx.r.measure(se.substr(0, lead + m.at), mono).width;
                    const float w = ctx.r.measure(se.substr(lead + m.at, m.length), mono).width;
                    if (x0 >= exprX + exprW) break;
                    ctx.r.fillRoundedRect({x0 - 1.f, monoY, std::min(w + 2.f, exprX + exprW - x0 + 1.f), ctx.r.lineHeight(mono)},
                                          c.warning.withAlpha(110), 3.f);
                }
            }
            drawClipped(ctx, {exprX, monoY}, shownExpr, mono, broken ? c.error : c.text, exprW);
            if (broken) {
                const float w = std::min(exprW, ctx.r.measure(shownExpr, mono).width);
                const float y = monoY + ctx.r.lineHeight(mono) - 1.f;
                ctx.r.line({exprX, y}, {exprX + w, y}, c.error, 2.f);
            }
            // ---- fin Lot API 8 ----
        } else {
            const bool locked = vr.prop->locked;
            const bool readOnly = vr.prop->type == ValueType::ReadOnly || !vr.prop->commit;
            // 1.10 (chantier K) : une case qui accepte une expression le dit - le type
            // attendu en petit au bout du nom, la pastille fx en creux devant la valeur.
            const bool fxHollow = !readOnly && exprfield::accepts(*vr.prop);
            // 1.10.3 : un texte a repere ($Vanne$) - la barre et la pastille en ambre, le repere surligne.
            const auto marks = vr.prop->type == ValueType::Boolean ? std::vector<exprfield::MarkerSpan>{} : exprfield::markersOf(*vr.prop);
            if (!marks.empty()) ctx.r.fillRect({r.x, r.y, 3.f, rowH}, c.warning);
            float nameRight = split - 6.f;
            bool tagShown = false;                       // 1.10.1 (U2) : l'invite n'a pas a le redire
            if (fxHollow) {
                const std::string_view tag = tagOf(exprfield::expectOf(*vr.prop));
                const float tw = tag.empty() ? 0.f : ctx.r.measure(tag, ctx.theme.font.smallUi).width;
                // 1.11 (R111, recette T2-17) : le type seulement si le nom entier tient.
                if (tw > 0.f && nameRight - tw - 6.f - (indent + 6.f) >= ctx.r.measure(shownName(*vr.prop), ctx.theme.font.ui).width) {
                    ctx.r.drawText({nameRight - tw, r.y + (rowH - ctx.r.lineHeight(ctx.theme.font.smallUi)) * 0.5f}, tag,
                                   ctx.theme.font.smallUi, c.textDisabled);
                    nameRight -= tw + 6.f;
                    tagShown = true;
                }
            }
            drawClipped(ctx, {indent + 6.f, r.y + textY}, shownName(*vr.prop),
                        ctx.theme.font.ui, locked ? c.textDisabled : c.textMuted, nameRight - indent - 6.f);
            const auto font = vr.prop->type == ValueType::Address ? ctx.theme.font.mono
                                                                  : ctx.theme.font.ui;
            // 1.9 : surchargee - le fond, le lisere, l'etiquette ; le bouton de retour.
            float valueRight = paintOverride(*vr.prop, endOf(*vr.prop, r));
            float valueX = split + 6.f;
            if (fxHollow) {
                const auto pf = ctx.theme.font.uiBold;
                const float pw = ctx.r.measure("fx", pf).width + 10.f;
                const float ph = std::min(rowH - 6.f, ctx.r.lineHeight(pf) + 2.f);
                const gfx::Rect hollow{valueX, r.y + (rowH - ph) * 0.5f, pw, ph};
                // La case ouverte commence par "=" : la pastille se remplit pendant
                // qu'on tape (maquette 1.10 : "tape = : la pastille se remplit").
                bool live = false;
                if (const auto* field = activeField(); field && vr.cat && editName_ == vr.prop->name && editCategory_ == vr.cat->name) {
                    std::string_view typed = field->text();
                    while (!typed.empty() && typed.front() == ' ') typed.remove_prefix(1);
                    live = !typed.empty() && typed.front() == '=';
                }
                if (live) {
                    ctx.r.fillRoundedRect(hollow, c.accent, ph * 0.5f);
                    ctx.r.drawText({hollow.x + 5.f, hollow.y + (ph - ctx.r.lineHeight(pf)) * 0.5f}, "fx", pf, c.selectionText);
                } else if (!marks.empty()) {
                    // le creux d'abord (le fond du panneau), puis sa teinte ambre : le "$" ambre s'y lit
                    const gfx::Rect inner{hollow.x + 1.f, hollow.y + 1.f, hollow.w - 2.f, hollow.h - 2.f};
                    ctx.r.fillRoundedRect(hollow, c.warning, ph * 0.5f);
                    ctx.r.fillRoundedRect(inner, c.panelBg, ph * 0.5f - 1.f);
                    ctx.r.fillRoundedRect(inner, c.warning.withAlpha(40), ph * 0.5f - 1.f);
                    const float gw = ctx.r.measure("$", pf).width;
                    ctx.r.drawText({hollow.x + (hollow.w - gw) * 0.5f, hollow.y + (ph - ctx.r.lineHeight(pf)) * 0.5f}, "$", pf, c.warning);
                } else {
                    ctx.r.fillRoundedRect(hollow, c.textMuted, ph * 0.5f);
                    ctx.r.fillRoundedRect({hollow.x + 1.f, hollow.y + 1.f, hollow.w - 2.f, hollow.h - 2.f}, c.panelBg, ph * 0.5f - 1.f);
                    ctx.r.drawText({hollow.x + 5.f, hollow.y + (ph - ctx.r.lineHeight(pf)) * 0.5f}, "fx", pf, c.textMuted);
                }
                valueX += pw + 6.f;
                gFxSlot = pw + 6.f;
            }
            // Une couleur se voit : une pastille avant son code.
            gfx::Color swatch{};
            if (vr.prop->type == ValueType::Color && hexColor(vr.prop->value, swatch)) {
                const gfx::Rect sw{valueX, r.y + (rowH - 14.f) * 0.5f, 14.f, 14.f};
                ctx.r.fillRect(sw, swatch);
                ctx.r.strokeRect(sw, c.border, 1.f);
                valueX += 20.f;
            }
            // Une bascule se lit d'un coup d'oeil : une case, cochee ou non.
            if (vr.prop->type == ValueType::Boolean) {
                const gfx::Rect box{valueX, r.y + (rowH - 14.f) * 0.5f, 14.f, 14.f};
                const bool on = isTrueText(vr.prop->value);
                ctx.r.fillRect(box, on ? c.accent : c.inputBg);
                ctx.r.strokeRect(box, on ? c.accent : c.border, 1.f);
                if (on) {
                    ctx.r.line({box.x + 3.f, box.y + 7.f}, {box.x + 6.f, box.y + 10.f}, c.selectionText, 2.f);
                    ctx.r.line({box.x + 6.f, box.y + 10.f}, {box.x + 11.f, box.y + 4.f}, c.selectionText, 2.f);
                }
                valueX += 20.f;
                // 1.10.1 (U2) : plus de "ou = expression" a cote de la case : la pastille fx le dit.
            }
            // 1.9 : la valeur reprise d'ailleurs - un petit cadenas a droite.
            if (locked) {
                const float side = std::min(rowH - 10.f, 12.f);
                const gfx::Color lc = vr.prop->lockTint ? ctx.theme.onSurface(*vr.prop->lockTint) : c.textMuted;
                drawIcon(ctx.r, Icon::Lock, {valueRight - side - 2.f, r.y + (rowH - side) * 0.5f, side, side}, lc);
                valueRight -= side + 8.f;
            }
            // 1.9 : la pastille de couleur au debut de la case (le mode d'un parametre).
            if (vr.prop->pill)
                if (const float w = drawColorPill(ctx, valueX, r, *vr.prop->pill, valueRight - valueX, c.panelBg); w > 0.f)
                    valueX += w + 6.f;
            gfx::Color valueCol = readOnly ? c.textMuted : c.text;
            if (vr.prop->valueTone != Tone::None) valueCol = ctx.theme.onSurface(ctx.theme.tone(vr.prop->valueTone, valueCol));
            else if (vr.prop->valueColor) valueCol = ctx.theme.onSurface(*vr.prop->valueColor);
            // La case suffit a une bascule : "TRUE" a cote la repeterait.
            if (vr.prop->type != ValueType::Boolean && !(vr.prop->pill && vr.prop->pillOnly)) {
                // 1.9 (chantier U) : une case vide montre son texte d'attente, en gris.
                // 1.10 (chantier K) : vide, la pastille en creux (plus haut) puis l'invite en gris.
                // 1.10.1 (U2) : sans "ou = expression", ni le type s'il est au bout du nom.
                const std::string invite = fxHollow ? exprfield::shownInvite(*vr.prop, tagShown) : vr.prop->placeholder;
                const bool waiting = vr.prop->value.empty() && !invite.empty();
                // 1.10.3 : chaque repere surligne en ambre, sous le texte.
                if (!waiting) {
                    const std::string_view sv(vr.prop->value);
                    const float lh = ctx.r.lineHeight(font);
                    for (const auto& m : marks) {
                        if (m.at + m.length > sv.size()) break;
                        const float x0 = valueX + ctx.r.measure(sv.substr(0, m.at), font).width;
                        const float w = ctx.r.measure(sv.substr(m.at, m.length), font).width;
                        if (x0 >= valueRight) break;
                        ctx.r.fillRoundedRect({x0 - 1.f, r.y + (rowH - lh) * 0.5f, std::min(w + 2.f, valueRight - x0 + 1.f), lh},
                                              c.warning.withAlpha(110), 3.f);
                    }
                }
                drawClipped(ctx, {valueX, r.y + textY}, waiting ? std::string_view(invite) : std::string_view(vr.prop->value), font,
                            waiting ? c.textDisabled : valueCol, valueRight - valueX);
            }
        }
        // 1.11.3 : le carre de legende, au bout de la ligne.
        if (vr.prop && vr.prop->legend) paintLegend(ctx, legendBox(r), *vr.prop->legend);
        ctx.r.line({r.x, r.bottom()}, {r.right(), r.bottom()}, c.gridLine, 1.f);
    }
    ctx.r.line({split, area.y}, {split, area.bottom()}, c.border, 1.f);
}

EventResult PropertyGrid::onEvent(const InputEvent& ev) {
    const float rowH = rowHeight_;

    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        if (!bounds().contains(w->pos)) return EventResult::Ignored;
        const float maxScroll = std::max(0.f, static_cast<float>(rows_.size()) * rowH - contentRect().h);
        scrollY_ = std::clamp(scrollY_ - w->dy * rowH * 3.f, 0.f, maxScroll);
        invalidate();
        return EventResult::Consumed;
    }
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (!bounds().contains(d->pos)) return EventResult::Ignored;
        grabFocus();
        const int ri = rowAt(d->pos.y);
        if (ri < 0) return EventResult::Consumed;

        const auto& vr = rows_[static_cast<std::size_t>(ri)];
        // 1.10 (chantier K2) : la rangee de la raison - un clic remplace le nom inconnu par
        // celui que la verification propose (une commande de l'hote : Ctrl+Z).
        if ((vr.depth & kNoteRow) != 0) {
            std::string from, to;
            if (d->button == MouseButton::Left && vr.prop && vr.prop->commit && noteFix(vr.prop->exprError, from, to)) {
                const std::string fixed = replaceRoot(vr.prop->expression, from, to);
                if (fixed != vr.prop->expression) {
                    const auto commit = vr.prop->commit;   // la grille est refaite pendant commit
                    const std::string sent = exprfield::normalized(*vr.prop, "=" + fixed);
                    finishEdit(false, {});
                    (void)commit(sent);
                }
            }
            return EventResult::Consumed;
        }
        if (vr.prop) {
            // La case valeur d'une propriete modifiable : on l'edite.
            const auto area = contentRect();
            const float split = area.x + area.w * nameRatio_;
            // ---- 1.10 (chantier K) : les champs a expression, partout pareils ----
            // Le X au bout de la ligne retire l'expression ; le clic droit ouvre
            // Retirer l'expression, Modifier l'expression..., Copier.
            const float rowY = area.y + static_cast<float>(ri) * rowH - scrollY_;
            const gfx::Rect rowRect{area.x, rowY, area.w, rowH};
            // 1.11.3 : le carre de legende - l'hote ouvre la liste des carres (la grille
            // peut etre refaite pendant l'appel : le nom et la categorie sont copies).
            if (d->button == MouseButton::Left && vr.prop->legend && legendClickable_) {
                const gfx::Rect b = legendBox(rowRect);
                if (gfx::Rect{b.x - 3.f, rowY, b.w + 6.f, rowH}.contains(d->pos)) {
                    finishEdit(false, {});
                    const std::string cat = vr.cat ? vr.cat->name : std::string{};
                    const std::string name = vr.prop->name;
                    legendClicked->emit(cat, name, b);
                    invalidate();
                    return EventResult::Consumed;
                }
            }
            if (d->button == MouseButton::Left && showsClear(*vr.prop)) {
                const gfx::Rect b = revertBox(endOf(*vr.prop, rowRect));
                if (gfx::Rect{b.x - 3.f, rowY, b.w + 6.f, rowH}.contains(d->pos)) {
                    finishEdit(false, {});
                    removeExpression(*this, *vr.prop);
                    invalidate();
                    return EventResult::Consumed;
                }
            }
            if (d->button == MouseButton::Right && exprfield::accepts(*vr.prop)) {
                finishEdit(false, {});
                auto* menu = dynamic_cast<PopupMenu*>(findById(exprfield::menuId(*this)));
                if (!menu) {
                    menu = &static_cast<PopupMenu&>(addChild(std::make_unique<PopupMenu>(exprfield::menuId(*this))));
                    menu->itemChosen->connect([this, menu](int action) {
                        menu->close();   // choisi d'ailleurs (un script, un essai) : ferme aussi
                        if (menu->items().empty()) return;
                        // La tete du menu dit la propriete (son nom) et sa categorie (a droite).
                        const auto& head = menu->items().front();
                        const int at = rowOf(head.shortcut, head.label);
                        if (at < 0) return;
                        const Property& target = *rows_[static_cast<std::size_t>(at)].prop;
                        if (action == 1) {
                            removeExpression(*this, target);
                        } else if (action == 2) {
                            gForceExprText = true;
                            beginEdit(static_cast<std::size_t>(at));
                            gForceExprText = false;
                        } else if (action == 3) {
                            setClipboardText(target.expression.empty() ? target.value : "=" + target.expression);
                        } else if (action == 5) {
                            // 1.11 (integration I111, lien 5) : la page des expressions, sur ce type.
                            // Une copie : l'hote ouvre un ecran, la grille peut etre refaite.
                            if (const auto hook = propertygrid::expressionPageHook()) hook(Property(target));
                        } else if (action == 4) {
                            // Revenir a la valeur par defaut : le retour de la 1.9 s'il y en a un
                            // (la valeur du symbole), sinon l'hote (ExprField.hpp, kToDefault).
                            // Copies : l'hote refait souvent la grille.
                            if (const auto revert = target.revert) revert();
                            else if (const auto commit = target.commit) (void)commit(exprfield::kToDefault);
                        }
                        invalidate();
                    }).release();
                    menu->dismissed->connect([this] { grabFocus(); }).release();
                }
                const Property& p = *vr.prop;
                const bool has = !p.expression.empty() && p.commit && p.type != ValueType::ReadOnly;
                const bool editable = p.commit && p.type != ValueType::ReadOnly;
                std::vector<PopupMenu::Item> items;
                PopupMenu::Item head;
                head.label = p.name;
                head.shortcut = vr.cat->name;
                head.heading = true;
                items.push_back(std::move(head));
                // L'ordre et les raccourcis de la maquette 1.10 (scene 1).
                items.push_back({!p.expression.empty() ? std::string(exprfield::kModify)
                                                       : std::string("\xC3\x89" "crire une expression\xE2\x80\xA6"),
                                 "=", editable ? std::string{} : std::string("lecture seule"), Icon::None, editable, false, 2});
                items.push_back({std::string(exprfield::kRemove), "Ctrl+Suppr", has ? std::string{} : std::string("pas d'expression"),
                                 Icon::Close, has, false, 1});
                items.push_back({std::string(exprfield::kCopy), "Ctrl+C", {}, Icon::None, true, false, 3});
                PopupMenu::Item rule;
                rule.separator = true;
                items.push_back(std::move(rule));
                items.push_back({std::string(exprfield::kDefault), {}, editable ? std::string{} : std::string("lecture seule"),
                                 Icon::None, editable, false, 4});
                if (propertygrid::expressionPageHook())   // 1.11 (integration I111, lien 5)
                    items.push_back({std::string(propertygrid::kExpressionPageItem), {}, {}, Icon::None, true, false, 5});
                menu->setItems(std::move(items));
                menu->setBounds(bounds());
                // 1.10 finale : la fenetre (et non la grille + 400 px) - l'inspecteur est
                // au bord droit : le menu se retourne au lieu de sortir de l'ecran.
                gfx::Size surface = surfaceSize();
                if (surface.w <= 0.f || surface.h <= 0.f) surface = {bounds().right() + 400.f, bounds().bottom() + 400.f};
                menu->openAt(d->pos, surface);
                return EventResult::Consumed;
            }
            // ---- fin 1.10 (chantier K) ----
            // 1.9 : le bouton de retour a la valeur d'origine. L'hote refait
            // souvent la grille : la fonction est copiee avant l'appel.
            if (d->button == MouseButton::Left && vr.prop->revert) {
                const float y = area.y + static_cast<float>(ri) * rowH - scrollY_;
                const gfx::Rect b = revertBox(beforeClear(*vr.prop, endOf(*vr.prop, {area.x, y, area.w, rowH})));
                if (gfx::Rect{b.x - 3.f, y, b.w + 6.f, rowH}.contains(d->pos)) {
                    finishEdit(false, {});
                    const auto revert = vr.prop->revert;
                    revert();
                    invalidate();
                    return EventResult::Consumed;
                }
            }
            if (d->button == MouseButton::Left && d->pos.x >= split && vr.prop->commit
                && vr.prop->type != ValueType::ReadOnly)
                beginEdit(static_cast<std::size_t>(ri));
            return EventResult::Consumed;
        }
        if (!vr.prop) {
            // Categories are stored by value; find the mutable one by name.
            struct Toggle {
                const Category* target;
                bool operator()(std::vector<Category>& cats) const {
                    for (auto& c : cats) {
                        if (&c == target) { c.expanded = !c.expanded; return true; }
                        if ((*this)(c.children)) return true;
                    }
                    return false;
                }
            } toggle{vr.cat};
            toggle(categories_);
            rebuildRows();
        }
        return EventResult::Consumed;
    }
    return EventResult::Ignored;
}

} // namespace ui
