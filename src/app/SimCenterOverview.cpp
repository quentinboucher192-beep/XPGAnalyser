// =============================================================================
//  app/SimCenterOverview.cpp - lot API 8 : Simulation > Vue d'ensemble
// -----------------------------------------------------------------------------
//  Le modele du Centre (les courbes, leur CSV) et la Vue d'ensemble : le
//  bandeau, les trois cartes, la chaine, ce qui merite ton attention, les 30
//  dernieres secondes. Tout est peint ici ; chaque zone cliquable a une cle
//  (partRect) pour les scripts.
// =============================================================================
#include "SimCenter.hpp"
#include "SimCenterKit.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace app {

using namespace simkit;
namespace ss = simstatus;

// ============================================================== le modele ====
bool SimCenterModel::addTrend(const std::string& path, bool hmi, bool boolean, std::string* why) {
    if (path.empty()) {
        if (why) *why = "aucun chemin";
        return false;
    }
    if (trendIndex(path) >= 0) {
        if (why) *why = path + " est d\xC3\xA9j\xC3\xA0 sur les courbes";
        return false;
    }
    if (trends_.size() >= kMaxTrends) {
        if (why) *why = "huit courbes au plus : retire d'abord une autre variable";
        return false;
    }
    SimTrend t;
    t.path = path;
    t.hmi = hmi;
    t.boolean = boolean;
    t.values.assign(times_.size(), std::numeric_limits<double>::quiet_NaN());
    trends_.push_back(std::move(t));
    ++trendRevision_;
    return true;
}

bool SimCenterModel::removeTrend(const std::string& path) {
    const int at = trendIndex(path);
    if (at < 0) return false;
    trends_.erase(trends_.begin() + at);
    ++trendRevision_;
    return true;
}

void SimCenterModel::clearTrends() {
    trends_.clear();
    times_.clear();
    ++trendRevision_;
}

int SimCenterModel::trendIndex(const std::string& path) const {
    const auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const auto want = lower(path);
    for (std::size_t i = 0; i < trends_.size(); ++i)
        if (lower(trends_[i].path) == want) return static_cast<int>(i);
    return -1;
}

void SimCenterModel::pushSample(double t, const std::vector<std::optional<double>>& values, const std::vector<std::string>& shown) {
    if (trends_.empty()) return;
    times_.push_back(t);
    for (std::size_t i = 0; i < trends_.size(); ++i) {
        auto& tr = trends_[i];
        tr.values.push_back(i < values.size() && values[i] ? *values[i] : std::numeric_limits<double>::quiet_NaN());
        if (i < shown.size() && !shown[i].empty()) tr.last = shown[i];
    }
    // Dix minutes, et 30 000 instants au plus.
    std::size_t drop = 0;
    while (drop < times_.size() && (t - times_[drop] > kKeepSeconds || times_.size() - drop > 30000)) ++drop;
    if (drop) {
        times_.erase(times_.begin(), times_.begin() + static_cast<std::ptrdiff_t>(drop));
        for (auto& tr : trends_)
            if (tr.values.size() >= drop) tr.values.erase(tr.values.begin(), tr.values.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    ++trendRevision_;
}

void SimCenterModel::setFrozen(bool on, double at) {
    frozen_ = on;
    frozenAt_ = at;
    ++trendRevision_;
}

void SimCenterModel::setWindow(double seconds) {
    window_ = std::clamp(seconds, 5.0, kKeepSeconds);
    ++trendRevision_;
}

std::string SimCenterModel::trendsCsv() const {
    const auto number = [](double v) {
        if (std::isnan(v)) return std::string{};
        char b[48];
        std::snprintf(b, sizeof b, "%.6g", v);
        std::string s = b;
        for (auto& c : s)
            if (c == '.') c = ',';
        return s;
    };
    std::string out = "Temps (s)";
    for (const auto& t : trends_) out += ";" + std::string(t.hmi ? "IHM " : "") + t.path;
    out += "\r\n";
    const double t0 = times_.empty() ? 0.0 : times_.front();
    for (std::size_t k = 0; k < times_.size(); ++k) {
        char b[32];
        std::snprintf(b, sizeof b, "%.3f", times_[k] - t0);
        std::string row = b;
        for (auto& c : row)
            if (c == '.') c = ',';
        for (const auto& t : trends_) row += ";" + (k < t.values.size() ? number(t.values[k]) : std::string{});
        out += row + "\r\n";
    }
    return out;
}

// ======================================================= la vue d'ensemble ====
SimOverviewPane::SimOverviewPane(std::string id, std::shared_ptr<SimCenterModel> model, SimCenterHosts hosts)
    : ui::Widget(std::move(id)), model_(std::move(model)), hosts_(std::move(hosts)) {}

SimOverviewPane::Plan SimOverviewPane::plan(float width) const {
    Plan pl;
    const float inner = std::max(320.f, width - 2.f * pl.pad);
    float y = pl.pad;
    pl.banner = {pl.pad, y, inner, 112.f};
    y += pl.banner.h + pl.gap;
    pl.wide = inner >= 900.f;
    const float cardH = 184.f;
    if (pl.wide) {
        const float cardW = std::floor((inner - 2.f * pl.gap) / 3.f);
        for (int i = 0; i < 3; ++i) pl.cards[i] = {pl.pad + static_cast<float>(i) * (cardW + pl.gap), y, cardW, cardH};
        y += cardH + pl.gap;
    } else {
        for (int i = 0; i < 3; ++i) {
            pl.cards[i] = {pl.pad, y, inner, cardH};
            y += cardH + pl.gap;
        }
    }
    const std::size_t rows = std::max<std::size_t>(1, model_ ? model_->report.attention.size() : 0);
    const float attH = 48.f + static_cast<float>(rows) * 52.f + 6.f;
    const float chainH = 210.f;
    if (pl.wide) {
        const float leftW = std::floor(inner * 0.5f);
        const float h = std::max(chainH, attH);
        pl.chain = {pl.pad, y, leftW, h};
        pl.attention = {pl.pad + leftW + pl.gap, y, inner - leftW - pl.gap, h};
        y += h + pl.gap;
    } else {
        pl.chain = {pl.pad, y, inner, chainH};
        y += chainH + pl.gap;
        pl.attention = {pl.pad, y, inner, attH};
        y += attH + pl.gap;
    }
    pl.timeline = {pl.pad, y, inner, 150.f};
    y += pl.timeline.h + pl.pad;
    pl.height = y;
    return pl;
}

float SimOverviewPane::maxScroll() const { return std::max(0.f, contentH_ - bounds().h); }

void SimOverviewPane::scrollTo(float y) {
    const float clamped = std::clamp(y, 0.f, maxScroll());
    if (clamped == scrollY_) return;
    scrollY_ = clamped;
    invalidate();
}

gfx::Rect SimOverviewPane::partRect(std::string_view key) const {
    for (const auto& h : hits_)
        if (h.key == key) return h.rect;
    return {};
}

std::vector<std::string> SimOverviewPane::partKeys() const {
    std::vector<std::string> out;
    for (const auto& h : hits_) out.push_back(h.key);
    return out;
}

bool SimOverviewPane::activate(std::string_view key) {
    for (const auto& h : hits_)
        if (h.key == key) {
            const auto go = h.go;       // une copie : go() peut tout redessiner
            if (!go.empty() && hosts_.go) hosts_.go(go);
            return !go.empty();
        }
    return false;
}

int SimOverviewPane::hitAt(gfx::Point p) const {
    for (std::size_t i = 0; i < hits_.size(); ++i)
        if (hits_[i].button && hits_[i].rect.contains(p)) return static_cast<int>(i);
    for (std::size_t i = 0; i < hits_.size(); ++i)
        if (!hits_[i].button && hits_[i].rect.contains(p)) return static_cast<int>(i);
    return -1;
}

std::string SimOverviewPane::liveTooltip(gfx::Point mouse) const {
    const int h = hitAt(mouse);
    return h >= 0 ? hits_[static_cast<std::size_t>(h)].tip : std::string{};
}

void SimOverviewPane::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.windowBg);
    hits_.clear();
    if (!hovered()) hover_ = -1;
    if (!model_) return;
    const auto pl = plan(b.w);
    contentH_ = pl.height;
    scrollY_ = std::clamp(scrollY_, 0.f, maxScroll());
    const auto at = [&](const gfx::Rect& r) { return gfx::Rect{b.x + r.x, b.y + r.y - scrollY_, r.w, r.h}; };
    ctx.r.pushClip(b);
    paintBanner(ctx, at(pl.banner));
    for (int i = 0; i < 3; ++i) paintCard(ctx, at(pl.cards[i]), model_->report.cards[static_cast<std::size_t>(i)]);
    paintChain(ctx, at(pl.chain));
    paintAttention(ctx, at(pl.attention));
    paintTimeline(ctx, at(pl.timeline));
    // L'ascenseur : un trait fin a droite quand tout ne tient pas.
    // 1.11.4 : il se tire.
    if (maxScroll() > 0.f) sbar_.paint(ctx, b, b.h + maxScroll(), b.h, scrollY_);
    ctx.r.popClip();
}

// ------------------------------------------------------------- le bandeau ----
void SimOverviewPane::paintBanner(const ui::PaintContext& ctx, const gfx::Rect& r) {
    const auto& c = ctx.theme.color;
    const auto& bn = model_->report.banner;
    const auto tone = toneColor(ctx, bn.tone);
    ctx.r.fillRoundedRect(r, tone.withAlpha(ctx.theme.isDark() ? 150 : 120), 10.f);
    ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, c.panelBg, 9.f);
    ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, tone.withAlpha(ctx.theme.isDark() ? 38 : 26), 9.f);
    ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, 7.f, r.h - 2.f}, tone, 3.f);
    // Le voyant : il respire quand ca tourne.
    const bool live = bn.tone == ss::Tone::Ok;
    const float pulse = live ? 0.5f + 0.5f * static_cast<float>(std::sin(model_->now * 3.0)) : 0.f;
    if (live) {
        const float halo = 13.f + 4.f * pulse;
        ctx.r.fillRoundedRect({r.x + 36.f - halo, r.y + 34.f - halo, 2.f * halo, 2.f * halo}, tone.withAlpha(static_cast<std::uint8_t>(40 + 30 * pulse)), halo);
    }
    ctx.r.fillRoundedRect({r.x + 27.f, r.y + 25.f, 18.f, 18.f}, tone, 9.f);
    const char* glyph = bn.tone == ss::Tone::Ok ? "\xE2\x9C\x93" : bn.tone == ss::Tone::Error ? "!" : bn.tone == ss::Tone::Warning ? "!"
                      : bn.tone == ss::Tone::Info ? "i" : "\xE2\x80\x93";
    const float gw = textWidth(ctx, glyph, kSmall);
    drawBold(ctx, {r.x + 36.f - gw * 0.5f, r.y + 34.f - ctx.r.lineHeight(kSmall) * 0.5f}, glyph, kSmall, ctx.theme.isDark() ? c.windowBg : c.panelBg);

    // Les boutons, a droite : LE bouton qui repare, le second.
    float right = r.right() - 18.f;
    const float cy = r.y + r.h * 0.5f;
    if (!bn.second.empty()) {
        const bool hot = hover_ == static_cast<int>(hits_.size());
        const auto br = button(ctx, right, cy, bn.second.label, hot, false, tone, true, kBody, 34.f);
        hits_.push_back({br, "bandeau:second", bn.second.key, "Aller \xC3\xA0 : " + goLabel(bn.second.key), true});
        right = br.x - 10.f;
    }
    if (!bn.fix.empty()) {
        const bool hot = hover_ == static_cast<int>(hits_.size());
        const auto br = button(ctx, right, cy, bn.fix.label, hot, true, tone, true, kBody, 34.f);
        hits_.push_back({br, "bandeau:bouton", bn.fix.key, "Ce qui r\xC3\xA8gle ce que dit le bandeau : " + goLabel(bn.fix.key), true});
        right = br.x - 16.f;
    }
    const float tx = r.x + 62.f;
    const float textW = std::max(120.f, right - tx);
    ctx.r.drawText({tx, r.y + 12.f}, bn.word, kTiny, ctx.theme.onSurface(tone));
    ctx.r.drawText({tx + 0.5f, r.y + 12.f}, bn.word, kTiny, ctx.theme.onSurface(tone));
    // Le titre : une ligne en grand ; trop long, deux lignes un peu plus petites.
    float y = r.y + 28.f;
    if (textWidth(ctx, bn.title, kTitle) <= textW) {
        drawBold(ctx, {tx, y}, bn.title, kTitle, c.text);
        y += 30.f;
    } else {
        const auto lines = wrap(ctx, bn.title, gfx::FontId{16}, textW, 2);
        for (const auto& l : lines) {
            drawBold(ctx, {tx, y}, l, gfx::FontId{16}, c.text);
            y += 21.f;
        }
        y += 3.f;
    }
    // Ce que ca veut dire : la suite du titre, en gris, sur ce qui reste de lignes.
    const std::string lead = "Ce que \xC3\xA7" "a veut dire : ";
    const float leadW = textWidth(ctx, lead, kBody) + 2.f;
    const std::size_t room = y + 36.f <= r.bottom() - 4.f ? 2 : 1;
    const auto lines = wrapIndented(ctx, bn.meaning, kBody, textW - leadW, textW, room);
    drawBold(ctx, {tx, y}, lead, kBody, c.textMuted);
    for (std::size_t k = 0; k < lines.size(); ++k)
        ctx.r.drawText({k == 0 ? tx + leadW : tx, y + static_cast<float>(k) * 18.f}, lines[k], kBody, c.textMuted);
}

// --------------------------------------------------------------- une carte ----
void SimOverviewPane::paintCard(const ui::PaintContext& ctx, const gfx::Rect& r, const ss::Card& cd) {
    const auto& c = ctx.theme.color;
    const auto tone = toneColor(ctx, cd.tone);
    const int cardHit = static_cast<int>(hits_.size());
    hits_.push_back({r, "carte:" + cd.key, cd.open, "Ouvrir " + goLabel(cd.open), false});
    const bool hot = hover_ == cardHit;
    card(ctx, r, hot);
    ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, 4.f}, tone.withAlpha(cd.tone == ss::Tone::Off ? 60 : 200), 2.f);
    led(ctx, r.x + 20.f, r.y + 22.f, 5.f, tone, cd.tone == ss::Tone::Ok);
    ctx.r.drawText({r.x + 34.f, r.y + 15.f}, cd.title, kTiny, c.textMuted);
    ctx.r.drawText({r.x + 34.5f, r.y + 15.f}, cd.title, kTiny, c.textMuted);
    if (!cd.state.empty()) pill(ctx, r.right() - 14.f, r.y + 13.f, cd.state, tone);
    const auto sentence = wrap(ctx, cd.sentence, kBody, r.w - 32.f, 2);
    for (std::size_t k = 0; k < sentence.size(); ++k)
        ctx.r.drawText({r.x + 16.f, r.y + 40.f + static_cast<float>(k) * 17.f}, sentence[k], kBody, c.text);
    // Les trois chiffres.
    const float colW = (r.w - 32.f) / 3.f;
    for (std::size_t i = 0; i < cd.figures.size(); ++i) {
        const auto& f = cd.figures[i];
        const float x = r.x + 16.f + static_cast<float>(i) * colW;
        const float w = colW - 10.f;
        ctx.r.drawText({x, r.y + 82.f}, fit(ctx, f.label, kTiny, w), kTiny, c.textMuted);
        const auto valueColour = f.tone == ss::Tone::Off ? c.text : ctx.theme.onSurface(toneColor(ctx, f.tone));
        drawBold(ctx, {x, r.y + 96.f}, fit(ctx, f.value, kValue, w), kValue, valueColour);
        ctx.r.drawText({x, r.y + 122.f}, fit(ctx, f.detail, kTiny, w), kTiny, c.textMuted);
        if (f.bar >= 0.0) {
            const gfx::Rect track{x, r.y + 139.f, std::min(w, 120.f), 4.f};
            ctx.r.fillRoundedRect(track, c.border, 2.f);
            const float fw = static_cast<float>(static_cast<double>(track.w) * std::clamp(f.bar, 0.0, 1.0));
            if (fw > 0.f) ctx.r.fillRoundedRect({track.x, track.y, std::max(4.f, fw), track.h}, toneColor(ctx, f.tone == ss::Tone::Off ? ss::Tone::Ok : f.tone), 2.f);
        }
    }
    // Ses boutons.
    float bx = r.x + 16.f;
    for (std::size_t i = 0; i < cd.buttons.size(); ++i) {
        const auto& a = cd.buttons[i];
        const bool bhot = hover_ == static_cast<int>(hits_.size());
        const auto br = button(ctx, bx, r.bottom() - 22.f, a.label, bhot, i == 0, tone.withAlpha(255), false, kSmall, 26.f);
        if (br.right() > r.right() - 10.f) break;
        hits_.push_back({br, "carte:" + cd.key + ":" + std::to_string(i), a.key, a.label + " \xE2\x80\x94 " + goLabel(a.key), true});
        bx = br.right() + 8.f;
    }
}

// --------------------------------------------------------------- la chaine ----
void SimOverviewPane::paintChain(const ui::PaintContext& ctx, const gfx::Rect& r) {
    const auto& c = ctx.theme.color;
    const auto& rep = model_->report;
    card(ctx, r, false);
    caption(ctx, r.x + 14.f, r.y + 14.f, ui::Icon::Network, "LA CHA\xC3\x8ENE");
    // L'ordre de la maquette : Automate - IHM - Equipements (l'IHM lit l'automate
    // et interroge les equipements en Modbus TCP).
    ctx.r.drawText({r.x + 14.f, r.y + 32.f},
                   fit(ctx, "Qui parle \xC3\xA0 qui, en ce moment : l'IHM lit l'automate et interroge les \xC3\xA9quipements (Modbus TCP)", kSmall, r.w - 28.f),
                   kSmall, c.textMuted);
    struct Box { const char* title; const char* key; ui::Icon icon; std::size_t card; };
    const Box boxes[3] = {{"AUTOMATE", "automate", ui::Icon::Cpu, 0},
                          {"IHM", "ihm", ui::Icon::Screen, 1},
                          {"\xC3\x89QUIPEMENTS", "equipements", ui::Icon::Network, 2}};
    const float boxW = std::clamp((r.w - 28.f) * 0.22f, 96.f, 150.f);
    const float boxH = 62.f;
    const float by = r.y + 70.f;
    gfx::Rect rects[3];
    rects[0] = {r.x + 14.f, by, boxW, boxH};
    rects[2] = {r.right() - 14.f - boxW, by, boxW, boxH};
    rects[1] = {r.x + (r.w - boxW) * 0.5f, by, boxW, boxH};
    for (int i = 0; i < 3; ++i) {
        const auto& bx = boxes[i];
        const auto& cd = rep.cards[bx.card];
        const auto tone = toneColor(ctx, cd.tone);
        const bool hot = hover_ == static_cast<int>(hits_.size());
        hits_.push_back({rects[i], std::string("chaine:") + bx.key, bx.key,
                         std::string("Ouvrir ") + goLabel(bx.key) + (cd.state.empty() ? std::string{} : " \xE2\x80\x94 " + cd.state), false});
        ctx.r.fillRoundedRect(rects[i], hot ? c.borderStrong : tone.withAlpha(cd.tone == ss::Tone::Off ? 110 : 200), 8.f);
        ctx.r.fillRoundedRect({rects[i].x + 2.f, rects[i].y + 2.f, rects[i].w - 4.f, rects[i].h - 4.f}, c.panelBg, 6.f);
        ui::drawIcon(ctx.r, bx.icon, {rects[i].x + 10.f, rects[i].y + 12.f, 16.f, 16.f}, tone);
        drawBold(ctx, {rects[i].x + 32.f, rects[i].y + 11.f}, fit(ctx, bx.title, kSmall, rects[i].w - 40.f), kSmall, c.text);
        ctx.r.drawText({rects[i].x + 10.f, rects[i].y + 36.f}, fit(ctx, cd.state, kSmall, rects[i].w - 20.f), kSmall, ctx.theme.onSurface(tone));
    }
    // Les deux liens : deux traits (un dans chaque sens), vivants, coupes ou au repos.
    for (int i = 0; i < 2; ++i) {
        const auto& lk = rep.chain[i == 0 ? 1u : 0u];   // automate <-> IHM, puis IHM <-> equipements
        const float x0 = rects[i].right() + 8.f, x1 = rects[i + 1].x - 8.f;
        const float my = by + boxH * 0.5f;
        if (x1 - x0 < 30.f) continue;
        const auto colour = lk.broken ? c.error : lk.alive ? c.ok : c.textMuted;
        const float yTop = my - 5.f, yBot = my + 5.f;
        if (lk.broken) {
            const float mid = (x0 + x1) * 0.5f;
            for (const float y : {yTop, yBot}) {
                ctx.r.line({x0, y}, {mid - 10.f, y}, colour, 2.f);
                ctx.r.line({mid + 10.f, y}, {x1, y}, colour, 2.f);
            }
            ctx.r.line({mid - 6.f, my - 7.f}, {mid + 6.f, my + 7.f}, colour, 2.5f);
            ctx.r.line({mid - 6.f, my + 7.f}, {mid + 6.f, my - 7.f}, colour, 2.5f);
        } else if (lk.alive) {
            ctx.r.line({x0, yTop}, {x1 - 6.f, yTop}, colour.withAlpha(150), 2.f);
            ctx.r.line({x0 + 6.f, yBot}, {x1, yBot}, colour.withAlpha(150), 2.f);
            arrowHead(ctx, x1, yTop, 1.f, colour);
            arrowHead(ctx, x0, yBot, -1.f, colour);
            // Des points qui courent : ca circule.
            const float len = x1 - x0 - 14.f;
            for (int d = 0; d < 3; ++d) {
                const double phase = std::fmod(model_->now * 0.7 + d / 3.0, 1.0);
                const float px = x0 + 4.f + static_cast<float>(phase) * len;
                ctx.r.fillRoundedRect({px - 3.f, yTop - 3.f, 6.f, 6.f}, colour, 3.f);
                const float qx = x1 - 4.f - static_cast<float>(phase) * len;
                ctx.r.fillRoundedRect({qx - 3.f, yBot - 3.f, 6.f, 6.f}, colour, 3.f);
            }
        } else {
            dashed(ctx, {x0, yTop}, {x1, yTop}, colour.withAlpha(160), 1.5f, 5.f);
            dashed(ctx, {x0, yBot}, {x1, yBot}, colour.withAlpha(160), 1.5f, 5.f);
        }
        const float w = x1 - x0;
        const auto label = fit(ctx, lk.text, kSmall, w);
        ctx.r.drawText({x0 + (w - textWidth(ctx, label, kSmall)) * 0.5f, yTop - 22.f}, label, kSmall, lk.alive || lk.broken ? c.text : c.textMuted);
        if (!lk.why.empty()) {
            const auto lines = wrap(ctx, lk.why, kTiny, std::max(60.f, w + 20.f), 3);
            for (std::size_t k = 0; k < lines.size(); ++k) {
                const float lw = textWidth(ctx, lines[k], kTiny);
                ctx.r.drawText({x0 + (w - lw) * 0.5f, yBot + 10.f + static_cast<float>(k) * 14.f}, lines[k], kTiny,
                               lk.broken ? ctx.theme.onSurface(c.error) : c.textMuted);
            }
        }
    }
    // La legende.
    float lx = r.x + 14.f;
    const float ly = r.bottom() - 24.f;
    const auto legend = [&](gfx::Color colour, const std::string& text, int style) {
        if (style == 0) ctx.r.line({lx, ly + 7.f}, {lx + 18.f, ly + 7.f}, colour, 2.f);
        else if (style == 1) {
            ctx.r.line({lx, ly + 7.f}, {lx + 6.f, ly + 7.f}, colour, 2.f);
            ctx.r.line({lx + 12.f, ly + 7.f}, {lx + 18.f, ly + 7.f}, colour, 2.f);
        } else dashed(ctx, {lx, ly + 7.f}, {lx + 18.f, ly + 7.f}, colour, 1.5f, 4.f);
        ctx.r.drawText({lx + 24.f, ly}, text, kTiny, c.textMuted);
        lx += 24.f + textWidth(ctx, text, kTiny) + 16.f;
    };
    legend(c.ok, "\xC3\xA7" "a circule", 0);
    legend(c.error, "coup\xC3\xA9", 1);
    legend(c.textMuted, "au repos", 2);
}

// ---------------------------------------------------- ce qui merite ton attention ----
void SimOverviewPane::paintAttention(const ui::PaintContext& ctx, const gfx::Rect& r) {
    const auto& c = ctx.theme.color;
    const auto& list = model_->report.attention;
    card(ctx, r, false);
    caption(ctx, r.x + 14.f, r.y + 14.f, ui::Icon::Warning, "CE QUI M\xC3\x89RITE TON ATTENTION");
    if (!list.empty()) {
        const std::string n = std::to_string(list.size());
        ctx.r.drawText({r.right() - 14.f - textWidth(ctx, n, kSmall), r.y + 13.f}, n, kSmall, c.textMuted);
    }
    float y = r.y + 42.f;
    if (list.empty()) {
        const gfx::Rect mark{r.x + 14.f, y + 4.f, 22.f, 22.f};
        ctx.r.fillRoundedRect(mark, c.ok.withAlpha(ctx.theme.isDark() ? 70 : 45), 11.f);
        const std::string g = "\xE2\x9C\x93";
        ctx.r.drawText({mark.x + (mark.w - textWidth(ctx, g, kSmall)) * 0.5f, mark.y + (mark.h - ctx.r.lineHeight(kSmall)) * 0.5f}, g, kSmall,
                       ctx.theme.onSurface(c.ok));
        drawBold(ctx, {r.x + 46.f, y + 2.f}, "Rien \xC3\xA0 signaler", kBody, c.text);
        const auto lines = wrap(ctx, model_->report.banner.tone == ss::Tone::Off
                                         ? "Rien ne tourne : quand la simulation marchera, ce qui cloche viendra s'\xC3\xA9" "crire ici, le plus grave d'abord."
                                         : "Rien d'arr\xC3\xAAt\xC3\xA9, rien de coup\xC3\xA9, rien de faux : la simulation se d\xC3\xA9roule normalement.",
                                kSmall, r.w - 60.f, 2);
        for (std::size_t k = 0; k < lines.size(); ++k) ctx.r.drawText({r.x + 46.f, y + 22.f + static_cast<float>(k) * 15.f}, lines[k], kSmall, c.textMuted);
        return;
    }
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& a = list[i];
        if (y + 46.f > r.bottom()) break;
        const auto tone = toneColor(ctx, a.tone);
        const gfx::Rect mark{r.x + 14.f, y + 5.f, 22.f, 22.f};
        ctx.r.fillRoundedRect(mark, tone.withAlpha(ctx.theme.isDark() ? 70 : 45), 11.f);
        const std::string g = a.tone == ss::Tone::Info ? "i" : a.tone == ss::Tone::Ok ? "\xE2\x9C\x93" : "!";
        drawBold(ctx, {mark.x + (mark.w - textWidth(ctx, g, kSmall)) * 0.5f, mark.y + (mark.h - ctx.r.lineHeight(kSmall)) * 0.5f}, g, kSmall,
                 ctx.theme.onSurface(tone));
        float textRight = r.right() - 14.f;
        // La maquette : jusqu'a deux boutons, le principal (plein) puis le second ;
        // poses de la droite vers la gauche. "attention:<id>" : le principal,
        // "attention:<id>:2" : le second.
        if (!a.second.empty()) {
            const bool hot = hover_ == static_cast<int>(hits_.size());
            const auto br = button(ctx, textRight, y + 16.f, a.second.label, hot, false, tone, true);
            hits_.push_back({br, "attention:" + a.id + ":2", a.second.key, a.second.label + " \xE2\x80\x94 " + goLabel(a.second.key), true});
            textRight = br.x - 8.f;
        }
        if (!a.action.empty()) {
            const bool hot = hover_ == static_cast<int>(hits_.size());
            const auto br = button(ctx, textRight, y + 16.f, a.action.label, hot, !a.second.empty(), tone, true);
            hits_.push_back({br, "attention:" + a.id, a.action.key, a.action.label + " \xE2\x80\x94 " + goLabel(a.action.key), true});
            textRight = br.x - 10.f;
        }
        const float tx = r.x + 46.f;
        ctx.r.drawText({tx, y + 3.f}, fit(ctx, a.text, kBody, textRight - tx), kBody, c.text);
        const auto det = wrap(ctx, a.detail, kSmall, textRight - tx, 2);
        for (std::size_t k = 0; k < det.size(); ++k)
            ctx.r.drawText({tx, y + 21.f + static_cast<float>(k) * 14.f}, det[k], kSmall, c.textMuted);
        y += 52.f;
        if (i + 1 < list.size()) ctx.r.fillRect({r.x + 14.f, y - 5.f, r.w - 28.f, 1.f}, c.border);
    }
}

// ------------------------------------------------- les 30 dernieres secondes ----
void SimOverviewPane::paintTimeline(const ui::PaintContext& ctx, const gfx::Rect& r) {
    const auto& c = ctx.theme.color;
    card(ctx, r, false);
    caption(ctx, r.x + 14.f, r.y + 14.f, ui::Icon::History, "LES 30 DERNI\xC3\x88RES SECONDES");
    {
        const std::string link = "Tout le journal" + (model_->journal ? " (" + std::to_string(model_->journal->size()) + ")" : std::string{}) + " \xE2\x80\xBA";
        const float w = textWidth(ctx, link, kSmall);
        const gfx::Rect lr{r.right() - 14.f - w, r.y + 11.f, w, 18.f};
        const bool hot = hover_ == static_cast<int>(hits_.size());
        hits_.push_back({lr, "frise:journal", "journal", "Tous les \xC3\xA9v\xC3\xA9nements de la simulation, filtr\xC3\xA9s et expliqu\xC3\xA9s", false});
        ctx.r.drawText({lr.x, lr.y}, link, kSmall, hot ? c.text : c.accent);
    }
    static const SimSource lanes[4] = {SimSource::Automate, SimSource::Ihm, SimSource::Equipements, SimSource::Debogage};
    static const char* const laneNames[4] = {"Automate", "IHM", "\xC3\x89quipements", "Toi / d\xC3\xA9" "bogage"};   // les mots de la maquette
    const float labelW = 96.f;
    const float x0 = r.x + 14.f + labelW, x1 = r.right() - 18.f;
    const float top = r.y + 40.f, laneH = 16.f;
    for (int i = 0; i < 4; ++i) {
        const float y = top + static_cast<float>(i) * laneH;
        ctx.r.drawText({r.x + 14.f, y + 1.f}, laneNames[i], kTiny, c.textMuted);
        ctx.r.fillRect({x0, y + laneH * 0.5f, x1 - x0, 1.f}, c.border);
    }
    const float axisY = top + 4.f * laneH + 4.f;
    for (int s = 0; s <= 30; s += 5) {
        const float x = x0 + (x1 - x0) * static_cast<float>(s) / 30.f;
        ctx.r.fillRect({x, top - 2.f, 1.f, axisY - top + 4.f}, c.border.withAlpha(120));
        if (s % 10 == 0) {
            const std::string lab = s == 30 ? std::string("maintenant") : "\xE2\x88\x92" + std::to_string(30 - s) + " s";
            const float w = textWidth(ctx, lab, kTiny);
            ctx.r.drawText({s == 30 ? x - w : s == 0 ? x : x - w * 0.5f, axisY + 4.f}, lab, kTiny, c.textMuted);
        }
    }
    const auto* journal = model_->journal;
    const double now = model_->now;
    std::vector<const SimEvent*> recent;
    if (journal) recent = journal->since(30.0);
    if (recent.empty()) {
        const std::string none = "Rien ces 30 derni\xC3\xA8res secondes.";
        ctx.r.drawText({x0 + (x1 - x0 - textWidth(ctx, none, kSmall)) * 0.5f, top + 1.5f * laneH}, none, kSmall, c.textMuted);
    }
    for (const auto* e : recent) {
        int lane = 3;
        for (int i = 0; i < 4; ++i)
            if (lanes[i] == e->source) lane = i;
        const double age = std::clamp(now - e->time, 0.0, 30.0);
        const float x = x0 + (x1 - x0) * static_cast<float>((30.0 - age) / 30.0);
        const float y = top + static_cast<float>(lane) * laneH + laneH * 0.5f;
        const auto colour = severityColor(ctx, e->severity);
        const bool hot = hover_ == static_cast<int>(hits_.size());
        const float rad = hot ? 6.5f : 5.f;
        if (e->repeats > 1) ctx.r.fillRoundedRect({x - rad - 3.f, y - rad - 3.f, 2.f * rad + 6.f, 2.f * rad + 6.f}, colour.withAlpha(60), rad + 3.f);
        ctx.r.fillRoundedRect({x - rad, y - rad, 2.f * rad, 2.f * rad}, colour, rad);
        std::string tip = e->clock + kMid + SimJournal::sourceName(e->source) + kMid + e->text;
        if (e->repeats > 1) tip += " (x " + std::to_string(e->repeats) + ")";
        tip += "\n" + SimJournal::explanationOf(*e);
        if (!e->go.empty()) tip += "\nUn clic : " + goLabel(e->go);
        hits_.push_back({{x - 8.f, y - 8.f, 16.f, 16.f}, "frise:" + std::to_string(e->id), e->go.empty() ? std::string("journal") : e->go, tip, true});
    }
    // Le dernier evenement, en clair.
    if (journal && !journal->empty()) {
        const auto& last = journal->events().back();
        const std::string lead = "Dernier : ";
        const float ly = axisY + 24.f;
        drawBold(ctx, {r.x + 14.f, ly}, lead, kSmall, c.textMuted);
        const float lx = r.x + 14.f + textWidth(ctx, lead, kSmall) + 2.f;
        ctx.r.fillRoundedRect({lx, ly + 4.f, 8.f, 8.f}, severityColor(ctx, last.severity), 4.f);
        std::string text = last.clock + kMid + SimJournal::sourceName(last.source) + kMid + last.text;
        if (last.repeats > 1) text += " (x " + std::to_string(last.repeats) + ")";
        ctx.r.drawText({lx + 14.f, ly}, fit(ctx, text, kSmall, r.right() - 14.f - lx - 14.f), kSmall, c.text);
    }
}

// ----------------------------------------------------------------- la souris ----
ui::EventResult SimOverviewPane::onEvent(const ui::InputEvent& ev) {
    {
        float off = scrollY_;   // 1.11.4 : l'ascenseur se tire
        if (maxScroll() > 0.f && sbar_.handle(*this, ev, off)) {
            scrollTo(off);
            return ui::EventResult::Consumed;
        }
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (!bounds().contains(w->pos) || maxScroll() <= 0.f) return ui::EventResult::Ignored;
        scrollTo(scrollY_ - w->dy * 48.f);
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = hitAt(m->pos);
        if (h != hover_) {
            hover_ = h;
            setTooltip(h >= 0 ? hits_[static_cast<std::size_t>(h)].tip : std::string{});
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && m->button == ui::MouseButton::Left) {
        pressed_ = hitAt(m->pos);
        if (pressed_ >= 0) return ui::EventResult::Consumed;
    }
    // Un clic : l'appui ET le relacher sur la meme zone.
    if (const auto* m = std::get_if<ui::MouseUp>(&ev); m && m->button == ui::MouseButton::Left) {
        const int h = hitAt(m->pos);
        const bool click = h >= 0 && h == pressed_;
        pressed_ = -1;
        if (click) {
            const auto go = hits_[static_cast<std::size_t>(h)].go;
            if (!go.empty() && hosts_.go) hosts_.go(go);
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

} // namespace app
