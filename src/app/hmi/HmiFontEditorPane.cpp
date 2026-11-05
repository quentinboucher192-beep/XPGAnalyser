// =============================================================================
//  app/hmi/HmiFontEditorPane.cpp - 1.26.0 : l'editeur de polices
// -----------------------------------------------------------------------------
//  Voir HmiFontEditorPane.hpp. Le modele (lire, ecrire, les contours) : hmi::ttf.
// =============================================================================
#include "HmiFontEditorPane.hpp"

#include "HmiImages.hpp"
#include "HmiPaneKit.hpp"
#include "HmiPanels.hpp"
#include "../../hmi/HmiAssets.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/SearchField.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <fstream>

namespace app {

using hmi::Id;
using hmi::kNoId;
using namespace hmikit;
namespace tt = hmi::ttf;
using PG = ui::PropertyGrid;

namespace {

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

gfx::Color rgb(int r, int g, int b, int a = 255) {
    return gfx::Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)};
}

std::string intText(long v) { return std::to_string(v); }

bool parseInt(std::string_view s, int& out) {
    std::string t;
    for (char c : s)
        if (c != ' ' && c != '\xC2' && c != '\xA0') t += c;
    // Le signe moins typographique (U+2212) se lit aussi.
    if (t.rfind("\xE2\x88\x92", 0) == 0) t = "-" + t.substr(3);
    if (t.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(t.c_str(), &end, 10);
    if (!end || *end != 0 || v < -32768 || v > 32767) return false;
    out = static_cast<int>(v);
    return true;
}

// Un fichier sans les caracteres que Windows refuse.
std::string safeName(std::string s) {
    for (auto& c : s)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
    return s.empty() ? std::string("Police") : s;
}

// Les glyphes d'une couverture (0..255) en une image blanche (la couverture dans l'alpha) : teintee au dessin.
gfx::TextureId textureOf(gfx::IRenderer& r, const std::vector<std::uint8_t>& cov, int w, int h) {
    if (w <= 0 || h <= 0) return {};
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 255);
    for (std::size_t i = 0; i < cov.size(); ++i) rgba[i * 4 + 3] = cov[i];
    return r.createImage(rgba.data(), w, h);
}

bool isAccent(std::uint32_t c) { return (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) || (c >= 0x1E00 && c <= 0x1EFF); }

} // namespace

// ================================================================================================ la vue de travail ===
HmiGlyphCanvas::HmiGlyphCanvas(std::string id, HmiFontEditorPane& pane) : ui::Widget(std::move(id)), pane_(pane) {
    setFocusPolicy(true);
}

void HmiGlyphCanvas::onLayout() {
    if (!fitted_ || bounds().w != fittedFor_.w || bounds().h != fittedFor_.h) fit();
}

void HmiGlyphCanvas::fit() {
    const auto b = bounds();
    fittedFor_ = b;
    fitted_ = true;
    const auto& f = pane_.font_;
    if (f.glyphs.empty() || b.w < 40 || b.h < 40) return;
    const tt::Box box = pane_.glyph_ >= 0 ? tt::bounds(f, pane_.glyph_) : tt::Box{};
    const int top = std::max({f.ascent, f.capHeight, box.empty ? 0 : box.yMax});
    const int bottom = std::min({f.descent, box.empty ? 0 : box.yMin, 0});
    const int adv = std::max({pane_.glyph_ >= 0 ? tt::advanceOf(f, pane_.glyph_) : 0, box.empty ? 0 : box.xMax, f.unitsPerEm / 2});
    const double range = std::max(1, top - bottom);
    scale_ = std::max(0.01, std::min((b.h - 50.0) / range, (b.w - 140.0) / (adv * 1.5)));
    originX_ = b.x + (b.w - adv * scale_) / 2.0;
    baselineY_ = b.y + 25.0 + top * scale_;
    invalidate();
}

void HmiGlyphCanvas::ensureView() {
    if (!fitted_) fit();
}

std::pair<double, double> HmiGlyphCanvas::toFont(gfx::Point p) const {
    return {(p.x - originX_) / scale_, (baselineY_ - p.y) / scale_};
}

gfx::Point HmiGlyphCanvas::toScreen(double x, double y) const {
    return {static_cast<float>(originX_ + x * scale_), static_cast<float>(baselineY_ - y * scale_)};
}

void HmiGlyphCanvas::onPaint(const ui::PaintContext& ctx) {
    ensureView();
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.inputBg);
    const auto& f = pane_.font_;
    if (f.glyphs.empty() || pane_.glyph_ < 0) {
        ctx.r.drawText({b.x + 20, b.y + 20}, "Ouvre une police, ou + Nouvelle police.", ctx.theme.font.ui, c.textMuted);
        return;
    }
    ctx.r.pushClip(b);
    const int g = pane_.glyph_;
    const int adv = tt::advanceOf(f, g);
    // ---- le remplissage : le glyphe, et ses voisins (le precedent et le suivant de la grille) en clair ----
    const std::uint64_t key = pane_.revision_ * 1000003ull + static_cast<std::uint64_t>(g) * 7919ull
                            + static_cast<std::uint64_t>(scale_ * 100000) * 31ull + static_cast<std::uint64_t>(originX_ * 10) * 17ull
                            + static_cast<std::uint64_t>(baselineY_ * 10) * 13ull + static_cast<std::uint64_t>(b.w) * 3ull + static_cast<std::uint64_t>(b.h);
    if (fillRenderer_ != &ctx.r || key != fillKey_ || fill_.v == 0) {
        if (fill_.v != 0 && fillRenderer_) fillRenderer_->releaseImage(fill_);
        fill_ = {};
        fillRenderer_ = &ctx.r;
        fillKey_ = key;
        const int w = static_cast<int>(b.w), h = static_cast<int>(b.h);
        const double ox = originX_ - b.x, by = baselineY_ - b.y;
        const auto mine = tt::raster(tt::outline(f, g), scale_, ox, by, w, h);
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        // Les voisins : dans la grille, celui d'avant et celui d'apres.
        const auto& shown = pane_.shown_;
        const auto at = std::find(shown.begin(), shown.end(), g);
        std::vector<std::uint8_t> near(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        if (at != shown.end()) {
            if (at != shown.begin()) {
                const int p = *(at - 1);
                const auto r = tt::raster(tt::outline(f, p), scale_, ox - tt::advanceOf(f, p) * scale_, by, w, h);
                for (std::size_t i = 0; i < near.size(); ++i) near[i] = std::max(near[i], r[i]);
            }
            if (at + 1 != shown.end()) {
                const auto r = tt::raster(tt::outline(f, *(at + 1)), scale_, ox + adv * scale_, by, w, h);
                for (std::size_t i = 0; i < near.size(); ++i) near[i] = std::max(near[i], r[i]);
            }
        }
        for (std::size_t i = 0; i < mine.size(); ++i) {
            // Le glyphe : bleu tres clair ; les voisins : gris tres clair.
            const double a = mine[i] / 255.0, n = near[i] / 255.0 * (1 - a);
            const double alpha = a + n;
            if (alpha <= 0) continue;
            rgba[i * 4 + 0] = static_cast<std::uint8_t>((223 * a + 230 * n) / alpha);
            rgba[i * 4 + 1] = static_cast<std::uint8_t>((231 * a + 234 * n) / alpha);
            rgba[i * 4 + 2] = static_cast<std::uint8_t>((243 * a + 240 * n) / alpha);
            rgba[i * 4 + 3] = static_cast<std::uint8_t>(std::clamp(alpha * 255.0, 0.0, 255.0));
        }
        fill_ = ctx.r.createImage(rgba.data(), w, h);
    }
    if (fill_.v != 0) ctx.r.drawImage(fill_, b);
    // ---- les lignes de la police ----
    const gfx::Color gold = rgb(201, 162, 39), slate = rgb(86, 101, 122), grey = rgb(122, 138, 160);
    const auto hline = [&](int v, const std::string& label, gfx::Color col, bool dashed) {
        const float y = toScreen(0, v).y;
        if (y < b.y || y > b.y + b.h) return;
        if (dashed)
            for (float x = b.x; x < b.x + b.w; x += 9.f) ctx.r.line({x, y}, {std::min(x + 5.f, b.x + b.w), y}, col, 1.f);
        else ctx.r.line({b.x, y}, {b.x + b.w, y}, col, 1.4f);
        ctx.r.drawText({b.x + 52.f, y - 16.f}, label, ctx.theme.font.smallUi, col);
    };
    hline(f.ascent, "Ascendante " + intText(f.ascent), gold.withAlpha(150), true);
    hline(f.capHeight, "Capitales " + intText(f.capHeight), gold, true);
    hline(f.xHeight, "Hauteur d'x " + intText(f.xHeight), gold, true);
    hline(0, "Ligne de base 0", slate, false);
    hline(f.descent, "Descendante " + intText(f.descent), gold, true);
    const gfx::Point o0 = toScreen(0, 0), oa = toScreen(adv, 0);
    ctx.r.line({o0.x, b.y}, {o0.x, b.y + b.h}, grey, 1.f);
    ctx.r.line({oa.x, b.y}, {oa.x, b.y + b.h}, grey, 1.f);
    const float handleY = std::min(b.y + b.h - 14.f, toScreen(0, f.descent).y);
    ctx.r.fillRect({oa.x - 6, handleY - 6, 12, 12}, rgb(255, 255, 255));
    ctx.r.strokeRect({oa.x - 6, handleY - 6, 12, 12}, grey, 1.f);
    ctx.r.drawText({oa.x + 9, handleY - 8}, "Chasse " + intText(adv) + " \xE2\x86\x94", ctx.theme.font.smallUi, slate);
    const tt::Box box = tt::bounds(f, g);
    if (!box.empty) {
        ctx.r.drawText({o0.x + 4, o0.y + 4}, "\xE2\x86\x90 " + intText(box.xMin) + " \xE2\x86\x92", ctx.theme.font.smallUi, slate);
        ctx.r.drawText({toScreen(box.xMax, 0).x + 2, o0.y + 4}, "\xE2\x86\x90 " + intText(adv - box.xMax) + " \xE2\x86\x92", ctx.theme.font.smallUi, slate);
    }
    // ---- le contour ----
    const auto& glyph = f.glyphs[static_cast<std::size_t>(g)];
    const auto strokeContours = [&](const std::vector<tt::Contour>& cs, gfx::Color col, float width) {
        for (const auto& ct : cs)
            for (const auto& s : tt::segments(ct)) {
                if (!s.curve) {
                    ctx.r.line(toScreen(s.x0, s.y0), toScreen(s.x1, s.y1), col, width);
                    continue;
                }
                gfx::Point prev = toScreen(s.x0, s.y0);
                const int steps = std::clamp(static_cast<int>((std::hypot(s.cx - s.x0, s.cy - s.y0) + std::hypot(s.x1 - s.cx, s.y1 - s.cy)) * scale_ / 4), 3, 40);
                for (int k = 1; k <= steps; ++k) {
                    const double t = static_cast<double>(k) / steps, u = 1 - t;
                    const gfx::Point p = toScreen(u * u * s.x0 + 2 * u * t * s.cx + t * t * s.x1, u * u * s.y0 + 2 * u * t * s.cy + t * t * s.y1);
                    ctx.r.line(prev, p, col, width);
                    prev = p;
                }
            }
    };
    if (glyph.composite()) {
        strokeContours(tt::outline(f, g), c.textMuted, 1.2f);
        std::string parts;
        for (const auto& comp : glyph.components) {
            const auto& cg = f.glyphs[static_cast<std::size_t>(std::clamp(comp.glyph, 0, static_cast<int>(f.glyphs.size()) - 1))];
            if (!parts.empty()) parts += " + ";
            parts += cg.codes.empty() ? "glyphe " + intText(comp.glyph) : "\xC2\xAB " + tt::codeText(cg.codes.front()) + " \xC2\xBB";
        }
        const gfx::Rect banner{b.x + 52, b.y + b.h - 34, b.w - 104, 26};
        ctx.r.fillRoundedRect(banner, rgb(255, 247, 214), 5.f);
        ctx.r.strokeRect(banner, rgb(225, 194, 90), 1.f);
        ctx.r.drawText({banner.x + 10, banner.y + 5}, "Un compos\xC3\xA9 (" + parts + ") : modifie ses parties, ou D\xC3\xA9" "composer pour le dessiner ici.",
                       ctx.theme.font.smallUi, rgb(74, 58, 0));
    } else {
        strokeContours(glyph.contours, c.text, 1.4f);
        // Le polygone des controles : des traits fins entre points voisins.
        for (const auto& ct : glyph.contours) {
            const std::size_t n = ct.points.size();
            for (std::size_t i = 0; i < n && n > 1; ++i) {
                const auto& p = ct.points[i];
                const auto& q = ct.points[(i + 1) % n];
                if (p.on && q.on) continue;
                ctx.r.line(toScreen(p.x, p.y), toScreen(q.x, q.y), rgb(154, 167, 184), 0.8f);
            }
        }
        for (std::size_t ci = 0; ci < glyph.contours.size(); ++ci) {
            const auto& ct = glyph.contours[ci];
            for (std::size_t pi = 0; pi < ct.points.size(); ++pi) {
                const auto& p = ct.points[pi];
                const gfx::Point s = toScreen(p.x, p.y);
                const bool sel = pane_.chosen_.count({static_cast<int>(ci), static_cast<int>(pi)}) != 0;
                const bool first = pi == 0;
                if (p.on) {
                    const float h = sel ? 5.5f : 4.f;
                    ctx.r.fillRect({s.x - h, s.y - h, 2 * h, 2 * h}, sel ? c.accent : rgb(255, 255, 255));
                    ctx.r.strokeRect({s.x - h, s.y - h, 2 * h, 2 * h}, sel ? rgb(255, 255, 255) : c.accent, first ? 2.f : 1.5f);
                } else {
                    const float r = sel ? 5.f : 3.8f;
                    ctx.r.fillRoundedRect({s.x - r, s.y - r, 2 * r, 2 * r}, sel ? c.accent : rgb(217, 130, 43), r);
                    if (!sel) ctx.r.fillRoundedRect({s.x - r + 1.5f, s.y - r + 1.5f, 2 * r - 3.f, 2 * r - 3.f}, rgb(255, 255, 255), r - 1.5f);
                }
            }
        }
        // Un seul point choisi : ses coordonnees.
        if (pane_.chosen_.size() == 1) {
            const auto ref = *pane_.chosen_.begin();
            if (ref.contour < static_cast<int>(glyph.contours.size()) && ref.point < static_cast<int>(glyph.contours[static_cast<std::size_t>(ref.contour)].points.size())) {
                const auto& p = glyph.contours[static_cast<std::size_t>(ref.contour)].points[static_cast<std::size_t>(ref.point)];
                const gfx::Point s = toScreen(p.x, p.y);
                const std::string t = "x " + intText(p.x) + "  y " + intText(p.y);
                const float w = ctx.r.measure(t, ctx.theme.font.smallUi).width + 16.f;
                ctx.r.fillRoundedRect({s.x + 12, s.y - 30, w, 20}, rgb(31, 39, 51), 3.f);
                ctx.r.drawText({s.x + 20, s.y - 28}, t, ctx.theme.font.smallUi, rgb(255, 255, 255));
            }
        }
    }
    // ---- le cadre en cours (choisir) ou la forme en cours ----
    if (drag_ == Drag::Band || drag_ == Drag::Shape) {
        const gfx::Rect r{std::min(downAt_.x, lastAt_.x), std::min(downAt_.y, lastAt_.y), std::fabs(lastAt_.x - downAt_.x), std::fabs(lastAt_.y - downAt_.y)};
        if (drag_ == Drag::Band) ctx.r.fillRect(r, c.accent.withAlpha(30));
        ctx.r.strokeRect(r, c.accent, 1.f);
    }
    ctx.r.popClip();
}

ui::EventResult HmiGlyphCanvas::onEvent(const ui::InputEvent& ev) {
    auto& f = pane_.font_;
    if (f.glyphs.empty() || pane_.glyph_ < 0) return ui::EventResult::Ignored;
    const double grab = 7.0 / scale_;
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && bounds().contains(w->pos)) {
        // La molette : le zoom autour de la souris.
        const auto [fx, fy] = toFont(w->pos);
        const double k = w->dy > 0 ? 1.15 : 1 / 1.15;
        scale_ = std::clamp(scale_ * k, 0.01, 20.0);
        originX_ = w->pos.x - fx * scale_;
        baselineY_ = w->pos.y + fy * scale_;
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && bounds().contains(d->pos)) {
        takeFocus();
        downAt_ = lastAt_ = d->pos;
        downFont_ = toFont(d->pos);
        const auto [x, y] = downFont_;
        if (d->button == ui::MouseButton::Middle) {
            drag_ = Drag::Pan;
            return ui::EventResult::Consumed;
        }
        if (d->button != ui::MouseButton::Left) return ui::EventResult::Ignored;
        const auto& glyph = f.glyphs[static_cast<std::size_t>(pane_.glyph_)];
        // La chasse : son carre en bas de sa ligne.
        const gfx::Point oa = toScreen(tt::advanceOf(f, pane_.glyph_), 0);
        const float handleY = std::min(bounds().y + bounds().h - 14.f, toScreen(0, f.descent).y);
        if (std::fabs(d->pos.x - oa.x) <= 8 && std::fabs(d->pos.y - handleY) <= 8 && !glyph.composite()) {
            drag_ = Drag::Advance;
            pane_.beginDrag(true);
            return ui::EventResult::Consumed;
        }
        switch (pane_.tool_) {
            case HmiFontEditorPane::Tool::Pen:
                (void)pane_.penClick(x, y, d->mods.alt, grab);
                return ui::EventResult::Consumed;
            case HmiFontEditorPane::Tool::AddPoint:
                (void)pane_.insertPointNear(x, y, grab * 2);
                return ui::EventResult::Consumed;
            case HmiFontEditorPane::Tool::Rectangle:
            case HmiFontEditorPane::Tool::Ellipse:
                drag_ = Drag::Shape;
                return ui::EventResult::Consumed;
            case HmiFontEditorPane::Tool::Select: break;
        }
        if (glyph.composite()) return ui::EventResult::Consumed;
        const tt::Hit h = tt::hitPoint(glyph.contours, x, y, grab);
        if (d->clickCount >= 2) {
            if (h.point >= 0) {
                pane_.choose({{h.contour, h.point}});
                (void)pane_.toggleChosenKind();
            } else (void)pane_.insertPointNear(x, y, grab * 2);
            return ui::EventResult::Consumed;
        }
        if (h.point >= 0) {
            const HmiFontEditorPane::PointRef ref{h.contour, h.point};
            auto chosen = pane_.chosen_;
            if (d->mods.shift || d->mods.ctrl) {
                if (!chosen.erase(ref)) chosen.insert(ref);
                pane_.choose(chosen);
            } else if (!chosen.count(ref)) pane_.choose({ref});
            if (pane_.chosen_.count(ref)) {
                drag_ = Drag::Points;
                pane_.beginDrag(false);
            }
            return ui::EventResult::Consumed;
        }
        if (!d->mods.shift) pane_.choose({});
        drag_ = Drag::Band;
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev); m && drag_ != Drag::None) {
        lastAt_ = m->pos;
        const auto [x, y] = toFont(m->pos);
        switch (drag_) {
            case Drag::Pan:
                originX_ += m->delta.x;
                baselineY_ += m->delta.y;
                break;
            case Drag::Points:
                pane_.dragBy(static_cast<int>(std::lround(x - downFont_.first)), static_cast<int>(std::lround(y - downFont_.second)));
                break;
            case Drag::Advance:
                pane_.dragBy(static_cast<int>(std::lround(x - downFont_.first)), 0);
                break;
            default: break;
        }
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev); u && drag_ != Drag::None) {
        lastAt_ = u->pos;
        const Drag was = drag_;
        drag_ = Drag::None;
        const auto [x, y] = toFont(u->pos);
        if (was == Drag::Points || was == Drag::Advance) pane_.endDrag();
        else if (was == Drag::Band) {
            const double x0 = std::min(downFont_.first, x), x1 = std::max(downFont_.first, x);
            const double y0 = std::min(downFont_.second, y), y1 = std::max(downFont_.second, y);
            auto chosen = pane_.chosen_;
            const auto& glyph = f.glyphs[static_cast<std::size_t>(pane_.glyph_)];
            for (std::size_t ci = 0; ci < glyph.contours.size(); ++ci)
                for (std::size_t pi = 0; pi < glyph.contours[ci].points.size(); ++pi) {
                    const auto& p = glyph.contours[ci].points[pi];
                    if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) chosen.insert({static_cast<int>(ci), static_cast<int>(pi)});
                }
            pane_.choose(chosen);
        } else if (was == Drag::Shape) {
            const int ax = static_cast<int>(std::lround(downFont_.first)), ay = static_cast<int>(std::lround(downFont_.second));
            const int bx = static_cast<int>(std::lround(x)), by = static_cast<int>(std::lround(y));
            if (std::abs(bx - ax) > 2 && std::abs(by - ay) > 2) (void)pane_.addShape(pane_.tool_, ax, ay, bx, by);
        }
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused()) {
        const int step = k->mods.shift ? 10 : 1;
        if (!k->mods.ctrl && !k->mods.alt) {
            switch (k->key) {
                case ui::Key::Left: return pane_.moveChosen(-step, 0) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
                case ui::Key::Right: return pane_.moveChosen(step, 0) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
                case ui::Key::Up: return pane_.moveChosen(0, step) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
                case ui::Key::Down: return pane_.moveChosen(0, -step) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
                case ui::Key::Delete:
                case ui::Key::Backspace: return pane_.removeChosen() ? ui::EventResult::Consumed : ui::EventResult::Ignored;
                case ui::Key::Escape:
                case ui::Key::Return:
                    if (pane_.penContour_ >= 0) pane_.penEnd();
                    else pane_.choose({});
                    return ui::EventResult::Consumed;
                case ui::Key::V: pane_.setTool(HmiFontEditorPane::Tool::Select); return ui::EventResult::Consumed;
                case ui::Key::P: pane_.setTool(HmiFontEditorPane::Tool::Pen); return ui::EventResult::Consumed;
                case ui::Key::A: pane_.setTool(HmiFontEditorPane::Tool::AddPoint); return ui::EventResult::Consumed;
                case ui::Key::R: pane_.setTool(HmiFontEditorPane::Tool::Rectangle); return ui::EventResult::Consumed;
                case ui::Key::E: pane_.setTool(HmiFontEditorPane::Tool::Ellipse); return ui::EventResult::Consumed;
                default: break;
            }
        }
        if (k->mods.ctrl && k->key == ui::Key::A) {
            pane_.chooseAll();
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

// ================================================================================================ la grille ===
HmiGlyphGrid::HmiGlyphGrid(std::string id, HmiFontEditorPane& pane) : ui::Widget(std::move(id)), pane_(pane) {}

int HmiGlyphGrid::columns() const { return std::max(1, static_cast<int>((bounds().w - 4) / 47.f)); }

gfx::Rect HmiGlyphGrid::cellOf(int glyph) const {
    const auto& shown = pane_.shown_;
    const auto it = std::find(shown.begin(), shown.end(), glyph);
    if (it == shown.end()) return {};
    const int i = static_cast<int>(it - shown.begin()), cols = columns();
    const float cw = (bounds().w - 4) / static_cast<float>(cols), ch = 54.f;
    return {bounds().x + 2 + (i % cols) * cw, bounds().y + 2 + (i / cols) * ch - scroll_, cw, ch};
}

void HmiGlyphGrid::reveal(int glyph) {
    const gfx::Rect r = cellOf(glyph);
    if (r.w <= 0) return;
    if (r.y < bounds().y) scroll_ -= bounds().y - r.y + 2;
    else if (r.y + r.h > bounds().y + bounds().h) scroll_ += r.y + r.h - (bounds().y + bounds().h) + 2;
    scroll_ = std::max(0.f, scroll_);
    invalidate();
}

void HmiGlyphGrid::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.panelBg);
    const auto& f = pane_.font_;
    const auto& shown = pane_.shown_;
    if (renderer_ != &ctx.r) {
        cells_.clear();
        renderer_ = &ctx.r;
    }
    if (f.glyphs.empty()) return;
    const int cols = columns();
    const float cw = (b.w - 4) / static_cast<float>(cols), ch = 54.f;
    const float total = static_cast<float>((shown.size() + static_cast<std::size_t>(cols) - 1) / static_cast<std::size_t>(cols)) * ch + 4;
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, total - b.h));
    ctx.r.pushClip(b);
    const int firstRow = static_cast<int>(scroll_ / ch), lastRow = static_cast<int>((scroll_ + b.h) / ch) + 1;
    std::set<int> visible;
    const double em = std::max(1, f.ascent - std::min(f.descent, 0));
    const int boxH = 34;
    const double s = boxH / em;
    for (int row = firstRow; row <= lastRow; ++row)
        for (int col = 0; col < cols; ++col) {
            const std::size_t i = static_cast<std::size_t>(row) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(col);
            if (i >= shown.size()) break;
            const int g = shown[i];
            visible.insert(g);
            const gfx::Rect r{b.x + 2 + col * cw, b.y + 2 + row * ch - scroll_, cw, ch};
            const bool sel = g == pane_.glyph_;
            ctx.r.fillRect(r, sel ? c.selectionBg : c.inputBg);
            ctx.r.strokeRect(r, sel ? c.accent : c.gridLine, sel ? 2.f : 1.f);
            auto& cell = cells_[g];
            if (cell.revision != pane_.revision_ || cell.tex.v == 0) {
                if (cell.tex.v != 0) ctx.r.releaseImage(cell.tex);
                const int w = static_cast<int>(cw) - 4;
                const int adv = tt::advanceOf(f, g);
                const double ox = (w - adv * s) / 2.0;
                const auto cov = tt::raster(tt::outline(f, g), s, ox, f.ascent * s + 2, w, boxH + 6);
                cell.tex = textureOf(ctx.r, cov, w, boxH + 6);
                cell.revision = pane_.revision_;
            }
            if (cell.tex.v != 0) ctx.r.drawTexture({r.x + 2, r.y + 3, cw - 4, static_cast<float>(boxH + 6)}, cell.tex, c.text);
            const auto& gl = f.glyphs[static_cast<std::size_t>(g)];
            char code[16];
            if (gl.codes.empty()) std::snprintf(code, sizeof code, "#%d", g);
            else std::snprintf(code, sizeof code, "%04X", gl.codes.front());
            const float tw = ctx.r.measure(code, ctx.theme.font.caption).width;
            ctx.r.drawText({r.x + (cw - tw) / 2, r.y + ch - 15}, code, ctx.theme.font.caption, c.textMuted);
            if (pane_.glyphModified(g)) ctx.r.fillRoundedRect({r.x + cw - 10, r.y + 4, 7, 7}, rgb(224, 138, 30), 3.5f);
        }
    // Les cellules qui ne se voient plus rendent leur texture.
    for (auto it = cells_.begin(); it != cells_.end();) {
        if (!visible.count(it->first)) {
            if (it->second.tex.v != 0) ctx.r.releaseImage(it->second.tex);
            it = cells_.erase(it);
        } else ++it;
    }
    if (shown.empty())
        ctx.r.drawText({b.x + 10, b.y + 10}, "Aucun glyphe ne correspond.", ctx.theme.font.smallUi, c.textMuted);
    ctx.r.popClip();
}

ui::EventResult HmiGlyphGrid::onEvent(const ui::InputEvent& ev) {
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && bounds().contains(w->pos)) {
        scroll_ = std::max(0.f, scroll_ - w->dy * 54.f);
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && bounds().contains(d->pos)) {
        for (int g : pane_.shown_) {
            const gfx::Rect r = cellOf(g);
            if (r.contains(d->pos)) {
                (void)pane_.selectGlyph(g);
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ================================================================================================ l'apercu ===
HmiFontLivePreview::HmiFontLivePreview(std::string id, HmiFontEditorPane& pane) : ui::Widget(std::move(id)), pane_(pane) {}

void HmiFontLivePreview::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.inputBg);
    if (!pane_.hasFont()) return;
    const auto bytes = pane_.liveBytes();
    if (!bytes) return;
    const std::uint64_t key = reinterpret_cast<std::uintptr_t>(bytes.get()) ^ std::hash<std::string>{}(pane_.sample());
    if (renderer_ != &ctx.r || key != key_) {
        for (auto& [px, l] : lines_)
            if (l.tex.v != 0 && renderer_) renderer_->releaseImage(l.tex);
        lines_.clear();
        renderer_ = &ctx.r;
        key_ = key;
        for (int px : {12, 20, 36}) {
            hmi::Rgba img;
            Line l;
            if (!pane_.sample().empty() && hmi::renderFontText(*bytes, pane_.sample(), static_cast<float>(px), img)) {
                l.tex = ctx.r.createImage(img.pixels.data(), img.width, img.height);
                l.w = img.width;
                l.h = img.height;
            }
            lines_[px] = l;
        }
    }
    ctx.r.pushClip(b);
    float x = b.x + 12;
    for (int px : {12, 20, 36}) {
        const auto& l = lines_[px];
        ctx.r.drawText({x, b.y + b.h / 2 - 7}, intText(px), ctx.theme.font.caption, c.textMuted);
        x += 24;
        if (l.tex.v != 0) {
            ctx.r.drawTexture({x, b.y + (b.h - l.h) / 2, static_cast<float>(l.w), static_cast<float>(l.h)}, l.tex, c.text);
            x += static_cast<float>(l.w) + 24;
        } else x += 40;
        if (x > b.x + b.w) break;
    }
    ctx.r.popClip();
}

// ================================================================================================ le volet ===
HmiFontEditorPane::HmiFontEditorPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ActNew, HmiGlyph::Plus, "Nouvelle police : les lettres, les chiffres, les accentu\xC3\xA9" "es, vides, \xC3\xA0 dessiner", "Nouvelle police");
    tools->add(ActDuplicate, HmiGlyph::Duplicate, "Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 : une copie d'une police fournie, de Mes polices ou du projet, \xC3\xA0 modifier",
               "Dupliquer \xC3\xA0 partir de\xE2\x80\xA6");
    tools->separator();
    tools->add(ActSaveMine, HmiGlyph::Check, "Enregistrer dans Mes polices (Ctrl+S) : elle te suit d'un projet \xC3\xA0 l'autre", "Enregistrer (Mes polices)");
    tools->add(ActSaveProject, HmiGlyph::Import, "Enregistrer dans le projet : sa ressource police (remplac\xC3\xA9" "e : les objets suivent)", "Enregistrer dans le projet");
    tools->add(ActExport, HmiGlyph::Export, "Exporter en .ttf\xE2\x80\xA6 : un fichier que Windows installe", "Exporter en .ttf\xE2\x80\xA6");
    tools->add(ActDiscard, HmiGlyph::Undo, "Abandonner les modifications : revenir \xC3\xA0 la police enregistr\xC3\xA9" "e", "Abandonner");
    tools->separator();
    tools->add(ActUndo, HmiGlyph::Undo, "Annuler (Ctrl+Z)", {});
    tools->add(ActRedo, HmiGlyph::Redo, "R\xC3\xA9tablir (Ctrl+Y)", {});
    tools->separator();
    tools->add(ActCopyFrom, HmiGlyph::Copy, "Copier le glyphe de\xE2\x80\xA6 : le m\xC3\xAAme caract\xC3\xA8re dans une autre police, \xC3\xA0 l'\xC3\xA9" "chelle", "Copier le glyphe de\xE2\x80\xA6");
    tools->add(ActRevert, HmiGlyph::Refresh, "R\xC3\xA9tablir l'original : le glyphe tel qu'\xC3\xA0 l'ouverture", "R\xC3\xA9tablir l'original");
    tools->add(ActDecompose, HmiGlyph::Ungroup, "D\xC3\xA9" "composer : le compos\xC3\xA9 devient un glyphe \xC3\xA0 dessiner ici", "D\xC3\xA9" "composer");
    tools->add(ActFit, HmiGlyph::ZoomFit, "Ajuster : le glyphe et ses hauteurs tiennent dans la vue", "Ajust\xC3\xA9");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    const auto fontOpen = [this] { return this->hasFont(); };
    tools_->setEnabledWhen(ActSaveMine, [this] { return this->hasFont() && (modified() || userFile_.empty()); });
    tools_->setEnabledWhen(ActSaveProject, [this] { return this->hasFont() && (modified() || resource_ == kNoId); });
    tools_->setEnabledWhen(ActExport, fontOpen);
    tools_->setEnabledWhen(ActDiscard, [this] { return modified(); });
    tools_->setEnabledWhen(ActUndo, [this] { return canUndo(); });
    tools_->setEnabledWhen(ActRedo, [this] { return canRedo(); });
    tools_->setEnabledWhen(ActCopyFrom, [this] { return glyph_ >= 0 && static_cast<bool>(hosts_.sources); });
    tools_->setEnabledWhen(ActDuplicate, [this] { return static_cast<bool>(hosts_.sources); });
    tools_->setEnabledWhen(ActRevert, [this] { return glyph_ >= 0 && glyphModified(glyph_); });
    tools_->setEnabledWhen(ActDecompose, [this] { return glyph_ >= 0 && font_.glyphs[static_cast<std::size_t>(glyph_)].composite(); });
    tools_->setEnabledWhen(ActFit, fontOpen);

    // ---- a gauche : chercher, filtrer, la grille
    searchField_ = &static_cast<ui::SearchField&>(addChild(std::make_unique<ui::SearchField>(base + ".search", "a, \xC3\xA9, U+00E9\xE2\x80\xA6",
                                                                                           "Un caract\xC3\xA8re, plusieurs, ou un code (U+00E9)")));
    auto filters = std::make_unique<HmiToolStrip>(base + ".filters");
    filters->add(FilterAll, HmiGlyph::None, "Tous les glyphes", "Tout");
    filters->add(FilterModified, HmiGlyph::None, "Ceux modifi\xC3\xA9s depuis l'ouverture", "Modifi\xC3\xA9s");
    filters->add(FilterUpper, HmiGlyph::None, "Les majuscules A \xC3\xA0 Z", "A\xE2\x80\x93Z");
    filters->add(FilterLower, HmiGlyph::None, "Les minuscules a \xC3\xA0 z", "a\xE2\x80\x93z");
    filters->add(FilterDigits, HmiGlyph::None, "Les chiffres", "0\xE2\x80\x93" "9");
    filters->add(FilterAccents, HmiGlyph::None, "Les lettres accentu\xC3\xA9" "es", "Accents");
    filters->add(FilterSymbols, HmiGlyph::None, "Le reste : signes, symboles, glyphes sans caract\xC3\xA8re", "Symboles");
    filters_ = &static_cast<HmiToolStrip&>(addChild(std::move(filters)));
    for (int a = FilterAll; a <= FilterSymbols; ++a) filters_->setCheckedWhen(a, [this, a] { return filter_ == a; });
    filters_->setLabelWhen(FilterModified, [this] {
        std::size_t n = 0;
        for (int g : touched_) n += glyphModified(g);
        return n ? "Modifi\xC3\xA9s " + std::to_string(n) : std::string("Modifi\xC3\xA9s");
    });
    grid_ = &static_cast<HmiGlyphGrid&>(addChild(std::make_unique<HmiGlyphGrid>(base + ".grid", *this)));

    // ---- au milieu : les outils, la vue de travail
    auto box = std::make_unique<HmiToolStrip>(base + ".toolbox");
    box->add(ToolSelect, HmiGlyph::Select, "Choisir et d\xC3\xA9placer les points (V)", {});
    box->add(ToolPen, HmiGlyph::Polygon, "La plume (P) : un clic, un point ; Alt+clic : hors de la courbe ; clic sur le premier : ferm\xC3\xA9", {});
    box->add(ToolPoint, HmiGlyph::Plus, "Ajouter un point sur le trait (A), sans changer la forme", {});
    box->add(ToolRect, HmiGlyph::Rectangle, "Rectangle (R) : glisser", {});
    box->add(ToolEllipse, HmiGlyph::Ellipse, "Ellipse (E) : glisser", {});
    toolbox_ = &static_cast<HmiToolStrip&>(addChild(std::move(box)));
    toolbox_->setCheckedWhen(ToolSelect, [this] { return tool_ == Tool::Select; });
    toolbox_->setCheckedWhen(ToolPen, [this] { return tool_ == Tool::Pen; });
    toolbox_->setCheckedWhen(ToolPoint, [this] { return tool_ == Tool::AddPoint; });
    toolbox_->setCheckedWhen(ToolRect, [this] { return tool_ == Tool::Rectangle; });
    toolbox_->setCheckedWhen(ToolEllipse, [this] { return tool_ == Tool::Ellipse; });
    canvas_ = &static_cast<HmiGlyphCanvas&>(addChild(std::make_unique<HmiGlyphCanvas>(base + ".canvas", *this)));

    // ---- a droite : les reglages
    settings_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".settings")));

    // ---- en bas : l'apercu en direct
    auto live = std::make_unique<HmiToolStrip>(base + ".live");
    live->add(ActLive, HmiGlyph::Eye, "Les vues ouvertes qui emploient cette police du projet la dessinent telle qu'ici, avant l'enregistrement",
              "Les vues suivent");
    liveTools_ = &static_cast<HmiToolStrip&>(addChild(std::move(live)));
    liveTools_->setCheckedWhen(ActLive, [this] { return live_; });
    liveTools_->setEnabledWhen(ActLive, [this] { return resource_ != kNoId; });
    sampleField_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".sample")));
    sampleField_->setText(sample_);
    preview_ = &static_cast<HmiFontLivePreview&>(addChild(std::make_unique<HmiFontLivePreview>(base + ".preview", *this)));
    menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(base + ".menu")));

    const auto run = [this](int a) {
        std::string why;
        switch (a) {
            case ActNew: (void)newFont(&why); break;
            case ActDuplicate: openMenu(ActDuplicate, sourceItems(1000)); break;
            case ActCopyFrom: openMenu(ActCopyFrom, sourceItems(2000)); break;
            case ActSaveMine: (void)saveMine(&why); break;
            case ActSaveProject: (void)saveProject(&why); break;
            case ActExport:
                if (hosts_.askExport) hosts_.askExport();
                break;
            case ActDiscard: discard(); break;
            case ActUndo: (void)undo(); break;
            case ActRedo: (void)redo(); break;
            case ActRevert: (void)revertGlyph(); break;
            case ActDecompose: (void)decompose(); break;
            case ActFit: canvas_->fit(); break;
            case ActLive: setLive(!live_); break;
            case ToolSelect: setTool(Tool::Select); break;
            case ToolPen: setTool(Tool::Pen); break;
            case ToolPoint: setTool(Tool::AddPoint); break;
            case ToolRect: setTool(Tool::Rectangle); break;
            case ToolEllipse: setTool(Tool::Ellipse); break;
            default:
                if (a >= FilterAll && a <= FilterSymbols) setFilter(a);
                break;
        }
        invalidate();
    };
    links_ += tools_->triggered->connect(run);
    links_ += filters_->triggered->connect(run);
    links_ += toolbox_->triggered->connect(run);
    links_ += liveTools_->triggered->connect(run);
    links_ += menu_->itemChosen->connect([this](int i) {
        const int first = menuAction_ == ActDuplicate ? 1000 : 2000;
        if (i < first || static_cast<std::size_t>(i - first) >= menuSources_.size()) return;
        const Source s = menuSources_[static_cast<std::size_t>(i - first)];
        std::string why;
        if (menuAction_ == ActDuplicate) (void)duplicateFrom(s, {}, &why);
        else (void)copyGlyphFrom(s, &why);
    });
    links_ += searchField_->changed->connect([this] { setSearch(searchField_->text()); });
    links_ += sampleField_->textChanged->connect([this](const std::string& t) {
        sample_ = t;
        preview_->invalidate();
    });
    links_ += settings_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    rebuildShown();
    rebuildSettings();
}

HmiFontEditorPane::~HmiFontEditorPane() {
    if (!resourceName_.empty()) HmiImageCache::setLiveFont(resourceName_, nullptr);
}

// ---------------------------------------------------------------------------------------------------- l'etat ---
void HmiFontEditorPane::say(std::string text, bool error) {
    if (error) PG::refuse(text);
    message_ = std::move(text);
    invalidate();
}

bool HmiFontEditorPane::fail(std::string text, std::string* why) {
    say(text, true);
    if (why) *why = std::move(text);
    return false;
}

std::string HmiFontEditorPane::title() const {
    if (!hasFont()) return "Police";
    return "Police \xC2\xB7 " + font_.family + (modified() ? " \xE2\x97\x8F" : "");
}

std::string HmiFontEditorPane::originText() const { return origin_; }

bool HmiFontEditorPane::modified() const noexcept {
    return dragStep_.has_value() || (undo_.empty() ? std::uint64_t{0} : undo_.back().id) != savedAt_;
}

tt::Glyph* HmiFontEditorPane::current() {
    if (glyph_ < 0 || glyph_ >= static_cast<int>(font_.glyphs.size())) return nullptr;
    return &font_.glyphs[static_cast<std::size_t>(glyph_)];
}

bool HmiFontEditorPane::glyphModified(int g) const {
    if (!touched_.count(g)) return false;
    if (g >= static_cast<int>(original_.glyphs.size())) return true;
    return !(font_.glyphs[static_cast<std::size_t>(g)] == original_.glyphs[static_cast<std::size_t>(g)]);
}

bool HmiFontEditorPane::guard(std::string* why, bool force) {
    if (force || !modified()) return true;
    return fail("Enregistre ou abandonne d'abord les modifications de " + font_.family + ".", why);
}

bool HmiFontEditorPane::load(tt::Font f, const std::string& origin, bool locked, std::string userFile, Id resource) {
    if (!resourceName_.empty()) HmiImageCache::setLiveFont(resourceName_, nullptr);
    font_ = std::move(f);
    original_ = font_;
    origin_ = origin;
    lockedSource_ = locked;
    userFile_ = std::move(userFile);
    resource_ = resource;
    resourceName_.clear();
    if (resource_ != kNoId)
        for (const auto& r : doc_->project.assets.resources)
            if (r.id == resource_) resourceName_ = r.name;
    undo_.clear();
    redo_.clear();
    touched_.clear();
    chosen_.clear();
    penContour_ = -1;
    ++revision_;
    savedAt_ = 0;
    bytes_.reset();
    bytesRevision_ = 0;
    glyph_ = -1;
    rebuildShown();
    int start = tt::glyphOf(font_, 'a');
    if (start < 0 && !shown_.empty()) start = shown_.front();
    (void)selectGlyph(start);
    canvas_->fit();
    titleChanged->emit();
    return true;
}

bool HmiFontEditorPane::openFont(const Source& s, std::string* why, bool force) {
    if (!guard(why, force)) return false;
    if (!s.data) return fail(s.label + " : contenu absent", why);
    tt::Font f;
    std::string err;
    if (!tt::read(*s.data, f, &err)) return fail(s.label + " : " + err, why);
    if (s.builtIn) {
        // Une police fournie ne change pas : on en modifie une copie, sous un autre nom.
        return duplicateFrom(s, {}, why, true);
    }
    const std::string origin = s.resource != kNoId ? "Ressource du projet (" + s.label + ")"
                             : !s.userFile.empty() ? "Mes polices (" + std::filesystem::path(s.userFile).filename().string() + ")"
                                                   : s.label;
    load(std::move(f), origin, false, s.userFile, s.resource);
    say("Ouverte : " + font_.family + ".");
    return true;
}

std::string HmiFontEditorPane::freeFamily(const std::string& wanted) const {
    std::set<std::string> taken;
    if (hosts_.sources)
        for (const auto& s : hosts_.sources()) taken.insert(lower(s.label));
    for (const auto& r : doc_->project.assets.resources) taken.insert(lower(std::filesystem::path(r.name).stem().string()));
    if (!taken.count(lower(wanted))) return wanted;
    for (int k = 2; k < 1000; ++k) {
        const std::string n = wanted + " " + std::to_string(k);
        if (!taken.count(lower(n))) return n;
    }
    return wanted;
}

bool HmiFontEditorPane::newFont(std::string* why, bool force) {
    if (!guard(why, force)) return false;
    load(tt::blank(freeFamily("Ma police")), "Nouvelle police", false, {}, kNoId);
    say("Nouvelle police : " + font_.family + ". Choisis un glyphe, dessine (la plume, un rectangle, une ellipse).");
    return true;
}

bool HmiFontEditorPane::duplicateFrom(const Source& s, std::string family, std::string* why, bool force) {
    if (!guard(why, force)) return false;
    if (!s.data) return fail(s.label + " : contenu absent", why);
    tt::Font src;
    std::string err;
    if (!tt::read(*s.data, src, &err)) return fail(s.label + " : " + err, why);
    if (family.empty()) family = freeFamily("Ma police");
    if (!tt::nameAllowed(src, family, &err)) return fail(err, why);
    load(tt::duplicate(src, family), "Copie de " + src.family, false, {}, kNoId);
    say("Copie de " + src.family + " : " + font_.family + ". Renomme-la dans Famille, \xC3\xA0 droite.");
    return true;
}

void HmiFontEditorPane::discard() {
    if (!modified()) return;
    // L'etat enregistre (ou ouvert) : defaire jusqu'a lui ; s'il n'est plus dans la pile, le reprendre tel quel.
    while (!undo_.empty() && undo_.back().id != savedAt_) (void)undo();
    if ((undo_.empty() ? std::uint64_t{0} : undo_.back().id) != savedAt_) {
        font_ = original_;
        undo_.clear();
        savedAt_ = 0;
        ++revision_;
    }
    redo_.clear();
    chosen_.clear();
    pushLive();
    rebuildShown();
    rebuildSettings();
    titleChanged->emit();
    say("Modifications abandonn\xC3\xA9" "es.");
}

// ---------------------------------------------------------------------------------------------------- les glyphes ---
void HmiFontEditorPane::rebuildShown() {
    shown_.clear();
    std::vector<std::pair<std::uint32_t, int>> mapped;
    std::vector<int> other;
    for (std::size_t i = 1; i < font_.glyphs.size(); ++i) {
        const auto& g = font_.glyphs[i];
        if (g.codes.empty()) other.push_back(static_cast<int>(i));
        else mapped.emplace_back(g.codes.front(), static_cast<int>(i));
    }
    std::sort(mapped.begin(), mapped.end());
    if (!font_.glyphs.empty()) other.insert(other.begin(), 0);
    // La recherche : un ou des caracteres, ou un code U+XXXX.
    std::set<std::uint32_t> wanted;
    std::string q = search_;
    while (!q.empty() && q.back() == ' ') q.pop_back();
    while (!q.empty() && q.front() == ' ') q.erase(q.begin());
    if (!q.empty()) {
        std::string t = q;
        if (t.size() >= 3 && (t[0] == 'U' || t[0] == 'u') && t[1] == '+') {
            wanted.insert(static_cast<std::uint32_t>(std::strtoul(t.c_str() + 2, nullptr, 16)));
        } else {
            for (std::size_t i = 0; i < t.size();) {
                const auto c = static_cast<unsigned char>(t[i]);
                std::uint32_t cp = c;
                std::size_t n = 1;
                if (c >= 0xF0 && i + 3 < t.size()) { cp = ((c & 0x07u) << 18) | ((t[i + 1] & 0x3Fu) << 12) | ((t[i + 2] & 0x3Fu) << 6) | (t[i + 3] & 0x3Fu); n = 4; }
                else if (c >= 0xE0 && i + 2 < t.size()) { cp = ((c & 0x0Fu) << 12) | ((t[i + 1] & 0x3Fu) << 6) | (t[i + 2] & 0x3Fu); n = 3; }
                else if (c >= 0xC0 && i + 1 < t.size()) { cp = ((c & 0x1Fu) << 6) | (t[i + 1] & 0x3Fu); n = 2; }
                if (cp != ' ') wanted.insert(cp);
                i += n;
            }
        }
    }
    const auto keep = [this, &wanted, &q](int g, std::uint32_t code, bool hasCode) {
        if (!q.empty()) {
            if (!hasCode) return false;
            bool any = false;
            for (auto c : font_.glyphs[static_cast<std::size_t>(g)].codes) any = any || wanted.count(c);
            if (!any) return false;
        }
        switch (filter_) {
            case FilterModified: return glyphModified(g);
            case FilterUpper: return hasCode && code >= 'A' && code <= 'Z';
            case FilterLower: return hasCode && code >= 'a' && code <= 'z';
            case FilterDigits: return hasCode && code >= '0' && code <= '9';
            case FilterAccents: return hasCode && isAccent(code);
            case FilterSymbols:
                return !hasCode || !((code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z') || (code >= '0' && code <= '9') || isAccent(code));
            default: return true;
        }
    };
    for (const auto& [code, g] : mapped)
        if (keep(g, code, true)) shown_.push_back(g);
    for (int g : other)
        if (keep(g, 0, false)) shown_.push_back(g);
    if (grid_) grid_->invalidate();
}

void HmiFontEditorPane::setSearch(const std::string& text) {
    search_ = text;
    rebuildShown();
    if (!shown_.empty() && std::find(shown_.begin(), shown_.end(), glyph_) == shown_.end()) (void)selectGlyph(shown_.front());
}

void HmiFontEditorPane::setFilter(int a) {
    filter_ = a;
    rebuildShown();
    if (!shown_.empty() && std::find(shown_.begin(), shown_.end(), glyph_) == shown_.end()) (void)selectGlyph(shown_.front());
    invalidate();
}

bool HmiFontEditorPane::selectGlyph(int g) {
    if (g < 0 || g >= static_cast<int>(font_.glyphs.size())) return false;
    if (g != glyph_) {
        penEnd();
        chosen_.clear();
    }
    glyph_ = g;
    if (grid_) grid_->reveal(g);
    rebuildSettings();
    if (canvas_) canvas_->invalidate();
    invalidate();
    return true;
}

bool HmiFontEditorPane::selectCode(std::uint32_t code) { return selectGlyph(tt::glyphOf(font_, code)); }

void HmiFontEditorPane::choose(std::set<PointRef> points) {
    chosen_ = std::move(points);
    rebuildSettings();
    if (canvas_) canvas_->invalidate();
}

void HmiFontEditorPane::chooseAll() {
    std::set<PointRef> all;
    if (auto* g = current())
        for (std::size_t ci = 0; ci < g->contours.size(); ++ci)
            for (std::size_t pi = 0; pi < g->contours[ci].points.size(); ++pi) all.insert({static_cast<int>(ci), static_cast<int>(pi)});
    choose(std::move(all));
}

// ---------------------------------------------------------------------------------------------------- les gestes ---
HmiFontEditorPane::Meta HmiFontEditorPane::metaOf() const {
    Meta m;
    m.family = font_.family;
    m.version = font_.version;
    m.bold = font_.bold;
    m.italic = font_.italic;
    m.weight = font_.weight;
    m.ascent = font_.ascent;
    m.descent = font_.descent;
    m.capHeight = font_.capHeight;
    m.xHeight = font_.xHeight;
    m.lineGap = font_.lineGap;
    return m;
}

void HmiFontEditorPane::setMeta(const Meta& m) {
    font_.family = m.family;
    font_.version = m.version;
    font_.bold = m.bold;
    font_.italic = m.italic;
    font_.weight = m.weight;
    font_.ascent = m.ascent;
    font_.descent = m.descent;
    font_.capHeight = m.capHeight;
    font_.xHeight = m.xHeight;
    font_.lineGap = m.lineGap;
}

HmiFontEditorPane::Step HmiFontEditorPane::capture(const std::string& label, const std::vector<int>& glyphs, bool meta) const {
    Step s;
    s.label = label;
    s.glyph = glyph_;
    for (int g : glyphs)
        if (g >= 0 && g < static_cast<int>(font_.glyphs.size())) s.glyphs.emplace_back(g, font_.glyphs[static_cast<std::size_t>(g)]);
    if (meta) s.meta = metaOf();
    return s;
}

void HmiFontEditorPane::restore(Step& s) {
    // Echange : `s` recoit l'etat d'avant la restauration (il sert a l'autre pile) et garde son identite.
    Step other;
    other.label = s.label;
    other.id = s.id;
    other.glyph = glyph_;
    if (s.meta) {
        other.meta = metaOf();
        setMeta(*s.meta);
    }
    for (auto& [g, glyph] : s.glyphs)
        if (g >= 0 && g < static_cast<int>(font_.glyphs.size())) {
            other.glyphs.emplace_back(g, font_.glyphs[static_cast<std::size_t>(g)]);
            font_.glyphs[static_cast<std::size_t>(g)] = glyph;
        }
    const int show = s.glyph;
    s = std::move(other);
    chosen_.clear();
    penContour_ = -1;
    if (show >= 0 && show < static_cast<int>(font_.glyphs.size())) glyph_ = show;
}

bool HmiFontEditorPane::edit(const std::string& label, std::vector<int> glyphs, bool meta, const std::function<bool()>& fn) {
    if (!hasFont()) return false;
    Step before = capture(label, glyphs, meta);
    if (!fn()) {
        // Rien ne change : remettre tel quel.
        Step undoIt = before;
        restore(undoIt);
        return false;
    }
    for (int g : glyphs) touched_.insert(g);
    before.id = nextStep_++;
    undo_.push_back(std::move(before));
    if (undo_.size() > 400) undo_.erase(undo_.begin());
    redo_.clear();
    changed();
    return true;
}

void HmiFontEditorPane::changed() {
    ++revision_;
    // Les composes qui emploient un glyphe touche changent aussi (la grille les redessine : la revision suffit).
    pushLive();
    rebuildSettings();
    if (filter_ == FilterModified) rebuildShown();
    if (canvas_) canvas_->invalidate();
    if (grid_) grid_->invalidate();
    if (preview_) preview_->invalidate();
    titleChanged->emit();
    invalidate();
}

bool HmiFontEditorPane::undo() {
    if (undo_.empty()) return false;
    Step s = std::move(undo_.back());
    undo_.pop_back();
    const std::string label = s.label;
    restore(s);
    redo_.push_back(std::move(s));
    ++revision_;
    pushLive();
    rebuildShown();
    rebuildSettings();
    titleChanged->emit();
    say("Annul\xC3\xA9 : " + label + ".");
    return true;
}

bool HmiFontEditorPane::redo() {
    if (redo_.empty()) return false;
    Step s = std::move(redo_.back());
    redo_.pop_back();
    const std::string label = s.label;
    restore(s);
    undo_.push_back(std::move(s));
    changed();
    say("R\xC3\xA9tabli : " + label + ".");
    return true;
}

bool HmiFontEditorPane::moveChosen(int dx, int dy) {
    auto* g = current();
    if (!g || chosen_.empty() || g->composite() || (dx == 0 && dy == 0)) return false;
    const auto keep = chosen_;
    const bool ok = edit("D\xC3\xA9placer " + std::to_string(chosen_.size()) + (chosen_.size() > 1 ? " points" : " point"), {glyph_}, false, [&] {
        auto* gl = current();
        for (const auto& r : keep)
            if (r.contour < static_cast<int>(gl->contours.size()) && r.point < static_cast<int>(gl->contours[static_cast<std::size_t>(r.contour)].points.size())) {
                auto& p = gl->contours[static_cast<std::size_t>(r.contour)].points[static_cast<std::size_t>(r.point)];
                p.x += dx;
                p.y += dy;
            }
        return true;
    });
    chosen_ = keep;
    rebuildSettings();
    return ok;
}

bool HmiFontEditorPane::removeChosen() {
    auto* g = current();
    if (!g || chosen_.empty() || g->composite()) return false;
    const auto keep = chosen_;
    const bool ok = edit(keep.size() > 1 ? "Retirer " + std::to_string(keep.size()) + " points" : std::string("Retirer un point"), {glyph_}, false, [&] {
        auto* gl = current();
        // Du dernier au premier : les rangs restent justes.
        for (auto it = keep.rbegin(); it != keep.rend(); ++it)
            if (it->contour < static_cast<int>(gl->contours.size()))
                tt::removePoint(gl->contours[static_cast<std::size_t>(it->contour)], it->point);
        // Un contour de moins de 2 points ne dessine rien : il part.
        gl->contours.erase(std::remove_if(gl->contours.begin(), gl->contours.end(), [](const tt::Contour& c) { return c.points.size() < 2; }),
                           gl->contours.end());
        return true;
    });
    chosen_.clear();
    rebuildSettings();
    return ok;
}

bool HmiFontEditorPane::toggleChosenKind() {
    auto* g = current();
    if (!g || chosen_.empty() || g->composite()) return false;
    const auto keep = chosen_;
    const bool ok = edit("Sur / hors de la courbe", {glyph_}, false, [&] {
        auto* gl = current();
        for (const auto& r : keep)
            if (r.contour < static_cast<int>(gl->contours.size()) && r.point < static_cast<int>(gl->contours[static_cast<std::size_t>(r.contour)].points.size())) {
                auto& p = gl->contours[static_cast<std::size_t>(r.contour)].points[static_cast<std::size_t>(r.point)];
                p.on = !p.on;
            }
        return true;
    });
    chosen_ = keep;
    rebuildSettings();
    return ok;
}

bool HmiFontEditorPane::insertPointNear(double x, double y, double radius) {
    auto* g = current();
    if (!g || g->composite()) return false;
    const tt::Hit h = tt::hitSegment(g->contours, x, y, radius);
    if (h.segment < 0) return fail("Pas de trait ici : clique sur le contour.", nullptr);
    int at = -1;
    const bool ok = edit("Ajouter un point", {glyph_}, false, [&] {
        at = tt::insertPoint(current()->contours[static_cast<std::size_t>(h.contour)], h.segment, h.t);
        return at >= 0;
    });
    if (ok) choose({{h.contour, at}});
    return ok;
}

bool HmiFontEditorPane::addShape(Tool shape, int x0, int y0, int x1, int y1) {
    auto* g = current();
    if (!g) return false;
    if (g->composite()) return fail("Un compos\xC3\xA9 : D\xC3\xA9" "composer d'abord.", nullptr);
    return edit(shape == Tool::Ellipse ? "Ajouter une ellipse" : "Ajouter un rectangle", {glyph_}, false, [&] {
        auto* gl = current();
        if (shape == Tool::Ellipse) tt::addEllipse(*gl, (x0 + x1) / 2, (y0 + y1) / 2, std::abs(x1 - x0) / 2, std::abs(y1 - y0) / 2);
        else tt::addRectangle(*gl, x0, y0, x1, y1);
        return true;
    });
}

bool HmiFontEditorPane::reverseContourOf(const PointRef& r) {
    auto* g = current();
    if (!g || g->composite() || r.contour < 0 || r.contour >= static_cast<int>(g->contours.size())) return false;
    return edit("Retourner le contour", {glyph_}, false, [&] {
        tt::reverse(current()->contours[static_cast<std::size_t>(r.contour)]);
        return true;
    });
}

bool HmiFontEditorPane::penClick(double x, double y, bool offCurve, double radius) {
    auto* g = current();
    if (!g) return false;
    if (g->composite()) return fail("Un compos\xC3\xA9 : D\xC3\xA9" "composer d'abord.", nullptr);
    const int px = static_cast<int>(std::lround(x)), py = static_cast<int>(std::lround(y));
    // Sur le premier point du contour en cours (3 points au moins) : il se ferme.
    if (penContour_ >= 0 && penContour_ < static_cast<int>(g->contours.size())) {
        const auto& ct = g->contours[static_cast<std::size_t>(penContour_)];
        if (ct.points.size() >= 3 && std::hypot(ct.points.front().x - x, ct.points.front().y - y) <= radius) {
            penEnd();
            say("Contour ferm\xC3\xA9.");
            return true;
        }
    }
    const bool fresh = penContour_ < 0 || penContour_ >= static_cast<int>(g->contours.size());
    const bool ok = edit(fresh ? "Plume : un contour" : "Plume : un point", {glyph_}, false, [&] {
        auto* gl = current();
        if (fresh) {
            gl->contours.push_back({});
            penContour_ = static_cast<int>(gl->contours.size()) - 1;
        }
        gl->contours[static_cast<std::size_t>(penContour_)].points.push_back({px, py, !offCurve});
        return true;
    });
    if (ok) {
        const auto& ct = current()->contours[static_cast<std::size_t>(penContour_)];
        chosen_ = {{penContour_, static_cast<int>(ct.points.size()) - 1}};
        rebuildSettings();
    }
    return ok;
}

void HmiFontEditorPane::penEnd() {
    if (penContour_ < 0) return;
    penContour_ = -1;
    // Un contour d'un seul point ne dessine rien : il part (sans geste de plus : la plume l'avait cree).
    if (auto* g = current())
        if (!g->contours.empty() && g->contours.back().points.size() < 2) {
            g->contours.pop_back();
            ++revision_;
        }
    if (canvas_) canvas_->invalidate();
}

void HmiFontEditorPane::beginDrag(bool advance) {
    auto* g = current();
    if (!g) return;
    dragAdvance_ = advance;
    dragBase_ = *g;
    dragAdvanceBase_ = g->advance;
    dragStep_ = capture(advance ? "Changer la chasse" : (chosen_.size() > 1 ? "D\xC3\xA9placer " + std::to_string(chosen_.size()) + " points" : std::string("D\xC3\xA9placer un point")),
                        {glyph_}, false);
}

void HmiFontEditorPane::dragBy(int dx, int dy) {
    auto* g = current();
    if (!g || !dragStep_) return;
    if (dragAdvance_) g->advance = std::max(0, dragAdvanceBase_ + dx);
    else
        for (const auto& r : chosen_)
            if (r.contour < static_cast<int>(g->contours.size()) && r.point < static_cast<int>(g->contours[static_cast<std::size_t>(r.contour)].points.size())) {
                const auto& base = dragBase_.contours[static_cast<std::size_t>(r.contour)].points[static_cast<std::size_t>(r.point)];
                auto& p = g->contours[static_cast<std::size_t>(r.contour)].points[static_cast<std::size_t>(r.point)];
                p.x = base.x + dx;
                p.y = base.y + dy;
            }
    // Pendant le glissement : la vue, la grille, l'apercu suivent (le fichier, au plus toutes les 120 ms).
    ++revision_;
    if (canvas_) canvas_->invalidate();
    if (grid_) grid_->invalidate();
    if (preview_) preview_->invalidate();
}

void HmiFontEditorPane::endDrag() {
    if (!dragStep_) return;
    Step s = std::move(*dragStep_);
    dragStep_.reset();
    const auto* g = current();
    if (!g || *g == dragBase_) return;
    touched_.insert(glyph_);
    s.id = nextStep_++;
    undo_.push_back(std::move(s));
    redo_.clear();
    changed();
}

bool HmiFontEditorPane::decompose() {
    auto* g = current();
    if (!g || !g->composite()) return false;
    return edit("D\xC3\xA9" "composer", {glyph_}, false, [&] {
        tt::decompose(font_, glyph_);
        return true;
    });
}

bool HmiFontEditorPane::copyGlyphFrom(const Source& s, std::string* why) {
    auto* g = current();
    if (!g) return false;
    if (g->codes.empty()) return fail("Ce glyphe n'a pas de caract\xC3\xA8re : rien \xC3\xA0 copier.", why);
    tt::Font src;
    std::string err;
    if (!s.data || !tt::read(*s.data, src, &err)) return fail(s.label + " : " + err, why);
    const int from = tt::glyphOf(src, g->codes.front());
    if (from < 0) return fail(s.label + " n'a pas \xC2\xAB " + tt::codeText(g->codes.front()) + " \xC2\xBB.", why);
    const bool ok = edit("Copier \xC2\xAB " + tt::codeText(g->codes.front()) + " \xC2\xBB de " + src.family, {glyph_}, false, [&] {
        tt::copyGlyph(font_, glyph_, src, from);
        return true;
    });
    if (ok) say("Copi\xC3\xA9 de " + src.family + ".");
    return ok;
}

bool HmiFontEditorPane::revertGlyph() {
    if (glyph_ < 0 || !glyphModified(glyph_)) return false;
    if (glyph_ >= static_cast<int>(original_.glyphs.size())) return false;
    return edit("R\xC3\xA9tablir l'original", {glyph_}, false, [&] {
        font_.glyphs[static_cast<std::size_t>(glyph_)] = original_.glyphs[static_cast<std::size_t>(glyph_)];
        return true;
    });
}

bool HmiFontEditorPane::familyAllowed(const std::string& family, std::string* why) const {
    tt::Font guardFont;
    guardFont.family.clear();
    guardFont.reserved = font_.reserved;
    guardFont.copyright = font_.copyright;
    guardFont.license = font_.license;
    if (guardFont.reserved.empty()) guardFont.reserved.push_back("\x01");   // pas de nom reserve : rien a refuser
    return tt::nameAllowed(guardFont, family, why);
}

bool HmiFontEditorPane::setField(const std::string& key, const std::string& value, std::string* why) {
    if (!hasFont()) return fail("Pas de police ouverte.", why);
    auto* g = current();
    int v = 0;
    const auto needInt = [&](int lo, int hi) {
        if (!parseInt(value, v) || v < lo || v > hi) {
            fail("\xC2\xAB " + value + " \xC2\xBB : un nombre entier de " + std::to_string(lo) + " \xC3\xA0 " + std::to_string(hi), why);
            return false;
        }
        return true;
    };
    const int em = font_.unitsPerEm;
    if (key == "chasse" || key == "approche_gauche" || key == "approche_droite") {
        if (!g) return false;
        if (g->composite() && key != "chasse") return fail("Un compos\xC3\xA9 : D\xC3\xA9" "composer d'abord.", why);
        if (!needInt(key == "chasse" ? 0 : -em, em * 4)) return false;
        return edit(key == "chasse" ? "Changer la chasse" : "Changer l'approche", {glyph_}, false, [&] {
            auto* gl = current();
            const tt::Box b = tt::bounds(font_, glyph_);
            if (key == "chasse") gl->advance = v;
            else if (key == "approche_droite") gl->advance = std::max(0, (b.empty ? 0 : b.xMax) + v);
            else {
                // L'approche gauche : le dessin glisse, l'approche droite reste.
                const int dx = v - (b.empty ? 0 : b.xMin);
                for (auto& c : gl->contours)
                    for (auto& p : c.points) p.x += dx;
                gl->advance = std::max(0, gl->advance + dx);
            }
            return true;
        });
    }
    if (key == "x" || key == "y" || key == "sorte") {
        if (!g || chosen_.size() != 1) return fail("Choisis un point.", why);
        const auto r = *chosen_.begin();
        if (key == "sorte") {
            const bool on = value.find("sur") != std::string::npos;
            auto& p = g->contours[static_cast<std::size_t>(r.contour)].points[static_cast<std::size_t>(r.point)];
            if (p.on == on) return true;
            return toggleChosenKind();
        }
        if (!needInt(-em * 4, em * 4)) return false;
        const auto keep = chosen_;
        const bool ok = edit("Placer le point", {glyph_}, false, [&] {
            auto& p = current()->contours[static_cast<std::size_t>(r.contour)].points[static_cast<std::size_t>(r.point)];
            (key == "x" ? p.x : p.y) = v;
            return true;
        });
        chosen_ = keep;
        rebuildSettings();
        return ok;
    }
    if (key == "famille") {
        std::string name = value;
        while (!name.empty() && name.back() == ' ') name.pop_back();
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        if (name == font_.family) return true;
        std::string err;
        if (!familyAllowed(name, &err)) return fail(err, why);
        return edit("Renommer la police", {}, true, [&] {
            font_.family = name;
            return true;
        });
    }
    if (key == "style") {
        const bool bold = value.find("Gras") != std::string::npos || value.find("gras") != std::string::npos;
        const bool italic = value.find("talique") != std::string::npos;
        return edit("Changer le style", {}, true, [&] {
            font_.bold = bold;
            font_.italic = italic;
            font_.weight = bold ? 700 : 400;
            return true;
        });
    }
    if (key == "version") {
        if (value.empty() || value.find_first_not_of("0123456789.") != std::string::npos) return fail("Une version : 1.000, 2.1\xE2\x80\xA6", why);
        return edit("Changer la version", {}, true, [&] {
            font_.version = value;
            return true;
        });
    }
    using TF = tt::Font;
    struct Metric {
        const char* key;
        int TF::*field;
        int lo, hi;
        const char* label;
    };
    const Metric metrics[] = {{"ascendante", &TF::ascent, 0, em * 3, "la hauteur ascendante"},
                              {"descendante", &TF::descent, -em * 3, 0, "la hauteur descendante"},
                              {"capitales", &TF::capHeight, 0, em * 3, "la hauteur des capitales"},
                              {"hauteur_x", &TF::xHeight, 0, em * 3, "la hauteur d'x"},
                              {"interligne", &TF::lineGap, 0, em * 3, "l'interligne"}};
    for (const auto& m : metrics)
        if (key == m.key) {
            if (!needInt(m.lo, m.hi)) return false;
            return edit(std::string("Changer ") + m.label, {}, true, [&] {
                font_.*(m.field) = v;
                return true;
            });
        }
    return fail("R\xC3\xA9glage inconnu : " + key, why);
}

// ---------------------------------------------------------------------------------------------------- l'outil ---
void HmiFontEditorPane::setTool(Tool t) {
    if (t != Tool::Pen) penEnd();
    tool_ = t;
    static const char* const help[] = {
        "Choisir : clic, Maj+clic, un cadre ; glisser ; fl\xC3\xA8" "ches ; Suppr ; double-clic : sur / hors de la courbe.",
        "Plume : un clic, un point ; Alt+clic : hors de la courbe ; clic sur le premier point, Entr\xC3\xA9" "e ou \xC3\x89" "chap : ferm\xC3\xA9.",
        "Ajouter un point : clique sur le trait (la forme ne change pas).",
        "Rectangle : glisse.",
        "Ellipse : glisse.",
    };
    say(help[static_cast<int>(t)]);
    if (canvas_) canvas_->invalidate();
}

std::vector<ui::PopupMenu::Item> HmiFontEditorPane::sourceItems(int base) const {
    std::vector<ui::PopupMenu::Item> items;
    auto& sources = const_cast<HmiFontEditorPane*>(this)->menuSources_;
    sources = hosts_.sources ? hosts_.sources() : std::vector<Source>{};
    std::string group;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto& s = sources[i];
        if (s.group != group) {
            group = s.group;
            ui::PopupMenu::Item h;
            h.label = group;
            h.heading = true;
            items.push_back(h);
        }
        ui::PopupMenu::Item it;
        it.label = s.label;
        it.id = base + static_cast<int>(i);
        items.push_back(std::move(it));
    }
    if (items.empty()) {
        ui::PopupMenu::Item h;
        h.label = "Aucune police";
        h.heading = true;
        items.push_back(h);
    }
    return items;
}

void HmiFontEditorPane::openMenu(int action, std::vector<ui::PopupMenu::Item> items) {
    menuAction_ = action;
    const gfx::Rect r = tools_->rectOf(action);
    const gfx::Point at = r.w > 0 ? gfx::Point{r.x, r.y + r.h} : gfx::Point{bounds().x + 20, bounds().y + 40};
    menu_->setItems(std::move(items));
    menu_->setBounds(bounds());
    gfx::Size surface = ui::surfaceSize();
    if (surface.w <= 0.f || surface.h <= 0.f) surface = {bounds().right() + 400.f, bounds().bottom() + 400.f};
    menu_->openAt(at, surface);
}

// ---------------------------------------------------------------------------------------------------- enregistrer ---
hmi::BlobPtr HmiFontEditorPane::liveBytes(bool now) {
    if (!hasFont()) return nullptr;
    if (bytes_ && bytesRevision_ == revision_) return bytes_;
    const double t = nowSeconds();
    if (bytes_ && !now && dragStep_ && t - bytesAt_ < 0.12) return bytes_;
    bytes_ = std::make_shared<const hmi::Bytes>(tt::write(font_));
    bytesRevision_ = revision_;
    bytesAt_ = t;
    return bytes_;
}

void HmiFontEditorPane::pushLive() {
    if (resourceName_.empty()) return;
    if (live_ && modified()) HmiImageCache::setLiveFont(resourceName_, liveBytes(true));
    else HmiImageCache::setLiveFont(resourceName_, nullptr);
}

void HmiFontEditorPane::setLive(bool on) {
    live_ = on;
    pushLive();
    say(on ? "Les vues dessinent la police telle qu'ici." : "Les vues dessinent la police enregistr\xC3\xA9" "e.");
}

void HmiFontEditorPane::setSample(std::string text) {
    sample_ = std::move(text);
    if (sampleField_ && sampleField_->text() != sample_) sampleField_->setText(sample_);
    if (preview_) preview_->invalidate();
}

bool HmiFontEditorPane::saveMine(std::string* why) {
    if (!hasFont()) return fail("Pas de police ouverte.", why);
    if (!hosts_.saveMine) return fail("Mes polices n'est pas disponible ici.", why);
    const auto bytes = liveBytes(true);
    std::string stored, err;
    const std::string file = safeName(font_.family) + ".ttf";
    // Le meme fichier si la police vient de Mes polices et garde son nom ; sinon un nouveau.
    const bool same = !userFile_.empty() && std::filesystem::path(userFile_).stem().string() == safeName(font_.family);
    if (!hosts_.saveMine(file, *bytes, same ? userFile_ : std::string(), &stored, &err)) return fail("Mes polices : " + err, why);
    userFile_ = stored;
    savedAt_ = undo_.empty() ? 0 : undo_.back().id;
    original_ = font_;
    touched_.clear();
    pushLive();
    titleChanged->emit();
    say("Enregistr\xC3\xA9" "e dans Mes polices : " + std::filesystem::path(stored).filename().string() + ".");
    return true;
}

bool HmiFontEditorPane::saveProject(std::string* why) {
    if (!hasFont()) return fail("Pas de police ouverte.", why);
    const auto bytes = liveBytes(true);
    const std::string family = font_.family;
    Id target = resource_;
    bool exists = false;
    for (const auto& r : doc_->project.assets.resources) exists = exists || r.id == target;
    std::string name;
    auto cmd = hmi::changeProject(doc_, exists ? "Modifier la police " + family : "Ajouter la police " + family, [&](hmi::Project& p) {
        if (exists) {
            for (auto& r : p.assets.resources)
                if (r.id == target) {
                    r.data = bytes;
                    hmi::describeResource(r);
                    name = r.name;
                }
            return;
        }
        hmi::Resource r = hmi::makeResource(p, safeName(family) + ".ttf", bytes, "\xC3\x89" "diteur de polices");
        target = r.id;
        name = r.name;
        p.assets.resources.push_back(std::move(r));
    });
    if (!cmd) return fail("La police n'a pas pu aller dans le projet.", why);
    apply_(std::move(cmd));
    if (!resourceName_.empty() && resourceName_ != name) HmiImageCache::setLiveFont(resourceName_, nullptr);
    resource_ = target;
    resourceName_ = name;
    savedAt_ = undo_.empty() ? 0 : undo_.back().id;
    original_ = font_;
    touched_.clear();
    pushLive();
    titleChanged->emit();
    say(exists ? "Enregistr\xC3\xA9" "e dans le projet : la ressource " + name + " est remplac\xC3\xA9" "e, les objets suivent (Ctrl+Z dans le projet la rend)."
               : "Ajout\xC3\xA9" "e au projet : la ressource " + name + ". Choisis-la dans la case Police des objets.");
    return true;
}

bool HmiFontEditorPane::exportTo(const std::string& path, std::string* why) {
    if (!hasFont()) return fail("Pas de police ouverte.", why);
    const auto bytes = liveBytes(true);
    std::ofstream out(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!out) return fail("\xC3\x89" "criture impossible : " + path, why);
    out.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!out) return fail("\xC3\x89" "criture impossible : " + path, why);
    say("Export\xC3\xA9" "e : " + path + ".");
    return true;
}

// ---------------------------------------------------------------------------------------------------- les reglages ---
void HmiFontEditorPane::rebuildSettings() {
    if (!settings_ || refreshing_) return;
    refreshing_ = true;
    const auto commit = [this](const char* key) {
        return [this, key](std::string_view v) {
            message_.clear();
            return setField(key, std::string(v));
        };
    };
    std::vector<PG::Category> cats;
    if (!hasFont()) {
        settings_->setCategories({});
        refreshing_ = false;
        return;
    }
    if (const auto* g = glyph_ >= 0 ? &font_.glyphs[static_cast<std::size_t>(glyph_)] : nullptr) {
        PG::Category c;
        c.name = g->codes.empty() ? "Glyphe " + intText(glyph_) : "Glyphe \xC2\xAB " + tt::codeText(g->codes.front()) + " \xC2\xBB";
        std::string codes;
        for (auto cp : g->codes) {
            char b[16];
            std::snprintf(b, sizeof b, "U+%04X", cp);
            codes += (codes.empty() ? "" : ", ") + std::string(b);
        }
        c.properties.push_back(prop("Caract\xC3\xA8re", codes.empty() ? std::string("aucun (glyphe ") + intText(glyph_) + ")" : codes, PG::ValueType::ReadOnly));
        const tt::Box b = tt::bounds(font_, glyph_);
        const int adv = tt::advanceOf(font_, glyph_);
        c.properties.push_back(prop("Chasse", intText(adv), PG::ValueType::Integer, commit("chasse"), "La largeur du caract\xC3\xA8re, espaces compris (unit\xC3\xA9s de la police)."));
        if (!g->composite()) {
            c.properties.push_back(prop("Approche gauche", intText(b.empty ? 0 : b.xMin), PG::ValueType::Integer, commit("approche_gauche"),
                                        "L'espace avant le dessin : le dessin glisse, l'approche droite reste."));
            c.properties.push_back(prop("Approche droite", intText(b.empty ? adv : adv - b.xMax), PG::ValueType::Integer, commit("approche_droite"),
                                        "L'espace apr\xC3\xA8s le dessin : la chasse suit."));
            std::size_t pts = 0;
            for (const auto& ct : g->contours) pts += ct.points.size();
            c.properties.push_back(prop("Contours", intText(static_cast<long>(g->contours.size())) + " \xC2\xB7 " + intText(static_cast<long>(pts)) + " points", PG::ValueType::ReadOnly));
        } else {
            std::string parts;
            for (const auto& comp : g->components) {
                const auto& cg = font_.glyphs[static_cast<std::size_t>(std::clamp(comp.glyph, 0, static_cast<int>(font_.glyphs.size()) - 1))];
                parts += (parts.empty() ? "" : " + ") + (cg.codes.empty() ? "glyphe " + intText(comp.glyph) : tt::codeText(cg.codes.front()));
            }
            c.properties.push_back(prop("Compos\xC3\xA9 de", parts, PG::ValueType::ReadOnly));
        }
        const auto users = tt::usersOf(font_, glyph_);
        if (!users.empty()) {
            std::string u;
            for (std::size_t i = 0; i < users.size() && i < 12; ++i) {
                const auto& ug = font_.glyphs[static_cast<std::size_t>(users[i])];
                u += (u.empty() ? "" : " ") + (ug.codes.empty() ? "#" + intText(users[i]) : tt::codeText(ug.codes.front()));
            }
            if (users.size() > 12) u += " \xE2\x80\xA6";
            c.properties.push_back(prop("Employ\xC3\xA9 par", u, PG::ValueType::ReadOnly));
        }
        cats.push_back(std::move(c));
        if (!g->composite() && !chosen_.empty()) {
            PG::Category pc;
            pc.name = chosen_.size() == 1 ? "Point choisi" : intText(static_cast<long>(chosen_.size())) + " points choisis";
            if (chosen_.size() == 1) {
                const auto r = *chosen_.begin();
                if (r.contour < static_cast<int>(g->contours.size()) && r.point < static_cast<int>(g->contours[static_cast<std::size_t>(r.contour)].points.size())) {
                    const auto& p = g->contours[static_cast<std::size_t>(r.contour)].points[static_cast<std::size_t>(r.point)];
                    pc.properties.push_back(prop("x", intText(p.x), PG::ValueType::Integer, commit("x")));
                    pc.properties.push_back(prop("y", intText(p.y), PG::ValueType::Integer, commit("y")));
                    pc.properties.push_back(prop("Sorte", p.on ? "sur la courbe" : "hors de la courbe", PG::ValueType::Enum, commit("sorte"),
                                                 "Sur la courbe : le trait y passe (un carr\xC3\xA9). Hors de la courbe : il l'attire (un rond).",
                                                 {"sur la courbe", "hors de la courbe"}));
                }
            } else pc.properties.push_back(prop("Fl\xC3\xA8" "ches", "1 unit\xC3\xA9 ; Maj : 10", PG::ValueType::ReadOnly));
            cats.push_back(std::move(pc));
        }
    }
    {
        PG::Category c;
        c.name = "Police";
        c.properties.push_back(prop("Famille", font_.family, PG::ValueType::Text, commit("famille"),
                                    "Son nom dans les listes et dans Windows. Une copie ne porte pas les noms r\xC3\xA9serv\xC3\xA9s par la licence de sa source."));
        c.properties.push_back(prop("Style", tt::styleLabel(font_), PG::ValueType::Enum, commit("style"), {}, {"Normal", "Gras", "Italique", "Gras italique"}));
        c.properties.push_back(prop("Version", font_.version, PG::ValueType::Text, commit("version")));
        c.properties.push_back(prop("Unit\xC3\xA9s par cadratin", intText(font_.unitsPerEm), PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Ascendante", intText(font_.ascent), PG::ValueType::Integer, commit("ascendante"), "Le haut de la ligne (au-dessus de la ligne de base)."));
        c.properties.push_back(prop("Descendante", intText(font_.descent), PG::ValueType::Integer, commit("descendante"), "Le bas de la ligne (n\xC3\xA9gatif : sous la ligne de base)."));
        c.properties.push_back(prop("Capitales", intText(font_.capHeight), PG::ValueType::Integer, commit("capitales"), "La hauteur d'un H."));
        c.properties.push_back(prop("Hauteur d'x", intText(font_.xHeight), PG::ValueType::Integer, commit("hauteur_x"), "La hauteur d'un x."));
        c.properties.push_back(prop("Interligne", intText(font_.lineGap), PG::ValueType::Integer, commit("interligne"), "L'espace ajout\xC3\xA9 entre deux lignes."));
        c.properties.push_back(prop("Glyphes", intText(static_cast<long>(font_.glyphs.size())), PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
    }
    {
        PG::Category c;
        c.name = "Origine";
        c.properties.push_back(prop("Origine", origin_, PG::ValueType::ReadOnly));
        if (!font_.license.empty()) {
            std::string lic = font_.license.substr(0, std::min<std::size_t>(font_.license.size(), 90));
            if (font_.license.size() > 90) lic += "\xE2\x80\xA6";
            c.properties.push_back(prop("Licence", lic, PG::ValueType::ReadOnly, {}, font_.license));
        }
        if (!font_.reserved.empty()) {
            std::string r;
            for (const auto& n : font_.reserved) r += (r.empty() ? "" : ", ") + n;
            c.properties.push_back(prop("Noms r\xC3\xA9serv\xC3\xA9s", r, PG::ValueType::ReadOnly, {},
                                        "La licence de la police d'origine les r\xC3\xA9serve : une copie ne les porte pas."));
        }
        std::string where = userFile_.empty() ? std::string() : "Mes polices";
        if (resource_ != kNoId) where += (where.empty() ? "" : " et ") + std::string("le projet (") + resourceName_ + ")";
        c.properties.push_back(prop("Enregistr\xC3\xA9" "e dans", where.empty() ? std::string("pas encore") : where, PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
    }
    settings_->setCategories(std::move(cats));
    refreshing_ = false;
}

// ---------------------------------------------------------------------------------------------------- la mise en page ---
void HmiFontEditorPane::onLayout() {
    const auto b = bounds();
    const float bar = 36.f, statusH = 24.f, liveH = 110.f;
    const float leftW = std::clamp(b.w * 0.22f, 220.f, 310.f), rightW = std::clamp(b.w * 0.22f, 240.f, 300.f);
    tools_->setBounds({b.x, b.y, b.w, bar});
    const float top = b.y + bar, bottom = b.y + b.h - statusH - liveH;
    const float h = std::max(0.f, bottom - top);
    gridHead_ = {b.x, top, leftW, 24.f};
    searchField_->setBounds({b.x + 6, top + 26, leftW - 12, 26});
    filters_->setBounds({b.x, top + 54, leftW, 30});
    grid_->setBounds({b.x, top + 86, leftW, std::max(0.f, h - 86)});
    const float cx = b.x + leftW + 1.f, cw = std::max(0.f, b.w - leftW - rightW - 2.f);
    toolbox_->setBounds({cx, top, cw, 30});
    canvas_->setBounds({cx, top + 30, cw, std::max(0.f, h - 30)});
    settings_->setBounds({cx + cw + 1.f, top, rightW, h});
    liveHead_ = {b.x, bottom, b.w, 24.f};
    liveTools_->setBounds({b.x + b.w - 200.f, bottom, 200.f, 24.f});
    sampleField_->setBounds({b.x + 10, bottom + 28, std::min(520.f, b.w - 20), 26});
    preview_->setBounds({b.x, bottom + 56, b.w, liveH - 56});
    statusRect_ = {b.x, b.y + b.h - statusH, b.w, statusH};
}

void HmiFontEditorPane::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.panelBg);
    ctx.r.fillRect(gridHead_, c.headerBg);
    std::string head = "GLYPHES";
    if (hasFont()) head += "  " + std::to_string(shown_.size()) + " / " + std::to_string(font_.glyphs.size());
    ctx.r.drawText({gridHead_.x + 8, gridHead_.y + 4}, head, ctx.theme.font.smallUi, c.textMuted);
    ctx.r.fillRect(liveHead_, c.headerBg);
    ctx.r.drawText({liveHead_.x + 8, liveHead_.y + 4}, "APER\xC3\x87U EN DIRECT", ctx.theme.font.smallUi, c.textMuted);
    ctx.r.fillRect(statusRect_, c.headerBg);
    std::string status = message_;
    if (status.empty())
        status = hasFont() ? font_.family + " \xC2\xB7 " + tt::styleLabel(font_) + (modified() ? " \xC2\xB7 modifi\xC3\xA9" "e (non enregistr\xC3\xA9" "e)" : " \xC2\xB7 enregistr\xC3\xA9" "e")
                           : "Ouvre une police depuis la biblioth\xC3\xA8que de polices, ou + Nouvelle police.";
    ctx.r.pushClip(statusRect_);
    ctx.r.drawText({statusRect_.x + 10, statusRect_.y + 4}, status, ctx.theme.font.smallUi, c.text);
    ctx.r.popClip();
}

ui::EventResult HmiFontEditorPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->mods.ctrl && !k->mods.alt && visible()) {
        if (k->key == ui::Key::Z && !k->mods.shift) return undo() ? ui::EventResult::Consumed : ui::EventResult::Ignored;
        if (k->key == ui::Key::Y || (k->key == ui::Key::Z && k->mods.shift)) return redo() ? ui::EventResult::Consumed : ui::EventResult::Ignored;
        if (k->key == ui::Key::S && !k->mods.shift && hasFont()) {
            if (resource_ != kNoId && userFile_.empty()) (void)saveProject();
            else (void)saveMine();
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

} // namespace app
