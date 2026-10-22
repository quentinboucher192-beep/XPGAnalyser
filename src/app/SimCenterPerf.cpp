// =============================================================================
//  app/SimCenterPerf.cpp - 1.12.17 : Vue d'ensemble > Performances, Lenteurs
// -----------------------------------------------------------------------------
//  LES PERFORMANCES DANS LE TEMPS : la periode (1 min, 10 min, 1 h, depuis le
//  lancement), quatre cases (le cycle, la charge, les depassements, les lenteurs),
//  la courbe du cycle (la mediane, jusqu'au maximum, les depassements, les
//  lenteurs ; le survol dit la seconde, un point orange mene a sa lenteur), quatre
//  petites courbes (le dessin, les expressions, les ecritures, les echanges) et ou
//  passe le temps (chaque poste du cycle). Remettre a zero, Exporter en CSV.
//  LES LENTEURS : les seuils (pour le projet), les filtres, une ligne par lenteur
//  (la plus recente d'abord) qui s'ouvre sur ce qui a pris le temps dedans ; Aller
//  a ; Vider (avec sa confirmation), Exporter en CSV.
//  Les cles commencant par @ sont celles du volet (local) ; les autres vont a l'ecran.
// =============================================================================
#include "SimCenter.hpp"
#include "SimCenterKit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace app {

using namespace simkit;

namespace {

constexpr float       kHead = 44.f;       // le titre, la periode, les boutons
constexpr float       kTiles = 82.f;      // les quatre cases
constexpr float       kChart = 206.f;     // la courbe et sa legende
constexpr float       kMulti = 80.f;      // les quatre petites courbes
constexpr float       kRow = 26.f;        // une ligne du tableau
constexpr float       kSlowRow = 30.f;    // une lenteur
constexpr float       kPart = 19.f;       // une ligne de son " Dedans "
constexpr std::size_t kSlowShown = 14;    // les lenteurs montrees ici (le Journal a les autres)

std::string decimal(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s(buf);
    for (auto& ch : s)
        if (ch == '.') ch = ',';
    return s;
}

std::string percent(double fraction) {
    const double p = fraction * 100.0;
    return (p < 10.0 && p > 0.0 ? decimal(p, 1) : std::to_string(static_cast<long long>(std::lround(p)))) + " %";
}

std::string periodLabel(double s) {
    if (s <= 0) return "depuis le lancement";
    if (s < 120) return "1 min";
    if (s < 1200) return "10 min";
    return "1 h";
}

// Le haut de l'echelle : 1, 2, 5 x 10^n.
double niceTop(double v) {
    if (v <= 0) return 1.0;
    const double p = std::pow(10.0, std::floor(std::log10(v)));
    for (const double m : {1.0, 2.0, 5.0, 10.0})
        if (m * p >= v) return m * p;
    return 10.0 * p;
}

std::size_t groupOf(hmi::SlowKind k) {
    switch (k) {
        case hmi::SlowKind::Action:    return 0;
        case hmi::SlowKind::Script:    return 1;
        case hmi::SlowKind::View:
        case hmi::SlowKind::Popup:     return 2;
        case hmi::SlowKind::Cycle:     return 4;
        case hmi::SlowKind::Equipment: return 5;
    }
    return 0;
}

// Une petite courbe : `values`, de gauche a droite, l'echelle de 0 a leur maximum.
void spark(const ui::PaintContext& ctx, const gfx::Rect& r, const std::vector<float>& values, gfx::Color colour) {
    if (values.size() < 2) return;
    float top = 0.f;
    for (const float v : values) top = std::max(top, v);
    if (top <= 0.f) {
        ctx.r.line({r.x, r.bottom() - 1.f}, {r.right(), r.bottom() - 1.f}, colour.withAlpha(120), 1.f);
        return;
    }
    const float step = r.w / static_cast<float>(values.size() - 1);
    gfx::Point prev{r.x, r.bottom() - r.h * values.front() / top};
    for (std::size_t i = 1; i < values.size(); ++i) {
        const gfx::Point p{r.x + step * static_cast<float>(i), r.bottom() - r.h * values[i] / top};
        ctx.r.line(prev, p, colour, 1.5f);
        prev = p;
    }
}

// Au plus `n` valeurs : la moyenne de chaque tranche.
std::vector<float> squeeze(const std::vector<float>& v, std::size_t n) {
    if (v.size() <= n) return v;
    std::vector<float> out;
    for (std::size_t b = 0; b < n; ++b) {
        const std::size_t from = b * v.size() / n, to = std::max(from + 1, (b + 1) * v.size() / n);
        double sum = 0;
        for (std::size_t i = from; i < to; ++i) sum += v[i];
        out.push_back(static_cast<float>(sum / static_cast<double>(to - from)));
    }
    return out;
}

} // namespace

// ======================================================== l'etat du volet ====
void SimOverviewPane::setPerfPeriod(double seconds) {
    perfPeriod_ = seconds;
    invalidate();
}

void SimOverviewPane::setSlowFilter(int kind) {
    slowFilter_ = kind;
    invalidate();
}

void SimOverviewPane::openSlow(std::uint64_t serial) {
    openedSlow_ = serial;
    invalidate();
}

void SimOverviewPane::reveal(std::string_view block) {
    revealPending_ = std::string(block);
    if (bounds().w <= 0.f) return;
    const auto pl = plan(bounds().w);
    const gfx::Rect& r = block == "lenteurs" ? pl.slow : pl.perf;
    if (r.h > 0.f) {
        contentH_ = pl.height;
        scrollTo(r.y - 8.f);
        revealPending_.clear();
    }
}

std::vector<const hmi::SlowEntry*> SimOverviewPane::slowShown(const hmi::SlowLog& log) const {
    std::vector<const hmi::SlowEntry*> out;
    for (auto it = log.entries().rbegin(); it != log.entries().rend(); ++it)
        if (slowFilter_ < 0 || static_cast<int>(groupOf(it->kind)) == slowFilter_) out.push_back(&*it);
    return out;
}

float SimOverviewPane::perfHeight() const {
    const auto view = hosts_.perfView ? hosts_.perfView() : SimPerfView{};
    if (!view.perf || view.perf->seconds().empty()) return kHead + 46.f;
    const auto rows = view.perf->posts(view.now, perfPeriod_).size();
    return kHead + kTiles + 10.f + kChart + 10.f + kMulti + 12.f + 46.f + static_cast<float>(rows) * kRow + 14.f;
}

float SimOverviewPane::slowHeight() const {
    const auto view = hosts_.perfView ? hosts_.perfView() : SimPerfView{};
    float h = kHead + 26.f + 36.f + (confirmClear_ ? 44.f : 0.f);
    if (!view.slow || view.slow->entries().empty()) return h + 40.f;
    const auto shown = slowShown(*view.slow);
    h += 26.f + static_cast<float>(std::min(shown.size(), kSlowShown)) * kSlowRow;
    for (std::size_t i = 0; i < shown.size() && i < kSlowShown; ++i)
        if (shown[i]->serial == openedSlow_) h += static_cast<float>(shown[i]->inside.size()) * kPart + 28.f;
    if (shown.size() > kSlowShown) h += 24.f;
    return h + 14.f;
}

// ====================================================== les performances ====
void SimOverviewPane::paintPerf(const ui::PaintContext& ctx, const gfx::Rect& r) {
    const auto& c = ctx.theme.color;
    const auto view = hosts_.perfView ? hosts_.perfView() : SimPerfView{};
    card(ctx, r, false);
    const float capW = caption(ctx, r.x + 14.f, r.y + 15.f, ui::Icon::Chart, "PERFORMANCES");
    if (view.perf && !view.running && !view.perf->seconds().empty())
        ctx.r.drawText({r.x + 14.f + capW + 10.f, r.y + 14.f}, "la derni\xC3\xA8re marche (arr\xC3\xAAt\xC3\xA9" "e)", kSmall, c.textMuted);
    // Les boutons a droite, puis la periode.
    float right = r.right() - 14.f;
    const float cy = r.y + 21.f;
    {
        const bool hot = hover_ == static_cast<int>(hits_.size());
        const auto br = button(ctx, right, cy, "Exporter en CSV", hot, false, {}, true);
        hits_.push_back({br, "perf:csv", "@csv:performances", "Une ligne par mesure : le cycle, le dessin, les \xC3\xA9" "critures, les \xC3\xA9" "changes", true});
        right = br.x - 8.f;
    }
    {
        const bool hot = hover_ == static_cast<int>(hits_.size());
        const auto br = button(ctx, right, cy, "Remettre \xC3\xA0 z\xC3\xA9ro", hot, false, {}, true);
        hits_.push_back({br, "perf:zero", "@zero", "Repartir d'une page blanche, sans arr\xC3\xAAter l'IHM", true});
        right = br.x - 18.f;
    }
    {
        const std::pair<double, const char*> periods[] = {{60, "1 min"}, {600, "10 min"}, {3600, "1 h"}, {-1, "Depuis le lancement"}};
        float x = right;
        for (auto it = std::rbegin(periods); it != std::rend(periods); ++it) {
            const std::string label = it->second;
            const float w = textWidth(ctx, label, kSmall) + 20.f;
            x -= w;
            const gfx::Rect pr{x, cy - 12.f, w, 24.f};
            const bool on = (it->first <= 0 && perfPeriod_ <= 0) || std::abs(it->first - perfPeriod_) < 1.0;
            const bool hot = hover_ == static_cast<int>(hits_.size());
            ctx.r.fillRoundedRect(pr, on ? c.selectionBg : hot ? ctx.theme.brand.hover : c.border, 12.f);
            if (!on) ctx.r.fillRoundedRect({pr.x + 1.f, pr.y + 1.f, pr.w - 2.f, pr.h - 2.f}, hot ? ctx.theme.brand.hover : c.panelBg, 11.f);
            ctx.r.drawText({pr.x + 10.f, pr.y + (pr.h - ctx.r.lineHeight(kSmall)) * 0.5f}, label, kSmall, on ? c.selectionText : c.text);
            hits_.push_back({pr, "perf:periode:" + std::to_string(static_cast<long long>(it->first)),
                             "@periode:" + std::to_string(static_cast<long long>(it->first)), "La courbe, les cases et le tableau sur " + periodLabel(it->first), true});
            x -= 6.f;
        }
    }
    float y = r.y + kHead;
    if (!view.perf || view.perf->seconds().empty()) {
        const auto lines = wrap(ctx, "Pas encore de mesure : d\xC3\xA9marrez l'IHM (F5). Une mesure par seconde : le cycle, le dessin, les \xC3\xA9" "critures, les \xC3\xA9" "changes.",
                                kBody, r.w - 28.f, 2);
        for (std::size_t k = 0; k < lines.size(); ++k) ctx.r.drawText({r.x + 14.f, y + 4.f + static_cast<float>(k) * 18.f}, lines[k], kBody, c.textMuted);
        return;
    }
    const auto& perf = *view.perf;
    const double now = view.now;
    const auto sm = perf.summary(now, perfPeriod_);
    const double period = perf.periodMs();
    // ---- les quatre cases ----
    std::size_t slowInWindow = 0, slowTotal = 0;
    std::string lastSlow;
    if (view.slow) {
        slowTotal = view.slow->entries().size();
        for (const auto& e : view.slow->entries()) {
            if (perfPeriod_ <= 0 || e.at >= now - perfPeriod_) ++slowInWindow;
            lastSlow = e.clock.substr(0, 8);
        }
    }
    struct Tile { std::string label, value, detail; bool toned; gfx::Color tone; };
    const Tile tiles[4] = {
        {"CYCLE IHM, EN MOYENNE", hmi::slowMs(sm.mean),
         "p95 " + hmi::slowMs(sm.p95) + std::string(kMid) + "max " + hmi::slowMs(sm.max) + (sm.max > 0 ? " \xC3\xA0 " + perf.clockAt(sm.maxAt) : std::string{}), false, c.text},
        {"CHARGE DE LA P\xC3\x89RIODE (" + hmi::slowMs(period) + ")", percent(sm.load),
         "le pire : " + percent(sm.worstLoad) + (sm.max > 0 ? " \xC3\xA0 " + perf.clockAt(sm.worstAt) : std::string{}),
         sm.worstLoad > 1.0, c.warning},
        {"D\xC3\x89PASSEMENTS", std::to_string(sm.overruns),
         (sm.cycles ? percent(static_cast<double>(sm.overruns) / static_cast<double>(sm.cycles)) + " des cycles" + std::string(kMid) : std::string{})
             + "un cycle plus long que sa p\xC3\xA9riode",
         sm.overruns > 0, c.error},
        {"LENTEURS (" + periodLabel(perfPeriod_) + ")", std::to_string(slowInWindow),
         (lastSlow.empty() ? std::string("aucune") : "la derni\xC3\xA8re \xC3\xA0 " + lastSlow) + kMid + std::to_string(slowTotal) + " gard\xC3\xA9" "es",
         slowInWindow > 0, c.warning},
    };
    const float tileW = (r.w - 28.f - 3.f * 10.f) / 4.f;
    for (int i = 0; i < 4; ++i) {
        const gfx::Rect tr{r.x + 14.f + static_cast<float>(i) * (tileW + 10.f), y, tileW, kTiles};
        ctx.r.fillRoundedRect(tr, c.border, 7.f);
        ctx.r.fillRoundedRect({tr.x + 1.f, tr.y + 1.f, tr.w - 2.f, tr.h - 2.f}, c.windowBg, 6.f);
        ctx.r.drawText({tr.x + 12.f, tr.y + 10.f}, fit(ctx, tiles[i].label, kTiny, tr.w - 24.f), kTiny, c.textMuted);
        drawBold(ctx, {tr.x + 12.f, tr.y + 26.f}, fit(ctx, tiles[i].value, kValue, tr.w - 24.f), kValue,
                 tiles[i].toned ? ctx.theme.onSurface(tiles[i].tone) : c.text);
        const auto det = wrap(ctx, tiles[i].detail, kTiny, tr.w - 24.f, 2);
        for (std::size_t k = 0; k < det.size(); ++k)
            ctx.r.drawText({tr.x + 12.f, tr.y + 52.f + static_cast<float>(k) * 13.f}, det[k], kTiny, c.textMuted);
        if (i == 3) hits_.push_back({tr, "perf:lenteurs", "@voir-lenteurs", "Le journal des lenteurs, plus bas", false});
    }
    y += kTiles + 10.f;
    // ---- la courbe du cycle ----
    const auto window = perf.window(now, perfPeriod_);
    const double from = perfPeriod_ > 0 ? std::max(perf.since(), now - perfPeriod_) : perf.since();
    const double to = std::max(from + 1.0, now);
    const gfx::Rect box{r.x + 14.f, y, r.w - 28.f, kChart - 26.f};
    ctx.r.fillRoundedRect(box, c.border, 7.f);
    ctx.r.fillRoundedRect({box.x + 1.f, box.y + 1.f, box.w - 2.f, box.h - 2.f}, c.windowBg, 6.f);
    ctx.r.drawText({box.x + 12.f, box.y + 8.f}, "Cycle IHM, chaque seconde", kSmall, c.text);
    const gfx::Rect plot{box.x + 52.f, box.y + 30.f, box.w - 66.f, box.h - 52.f};
    chartRect_ = plot;
    chartFrom_ = from;
    chartTo_ = to;
    double top = period * 1.25;
    for (const auto* s : window) top = std::max(top, static_cast<double>(s->max) * 1.08);
    top = niceTop(top);
    const auto X = [&](double t) { return plot.x + plot.w * static_cast<float>((t - from) / (to - from)); };
    const auto Y = [&](double ms) { return plot.bottom() - plot.h * static_cast<float>(std::clamp(ms / top, 0.0, 1.0)); };
    for (int k = 0; k <= 2; ++k) {
        const double v = top * k / 2.0;
        const float gy = Y(v);
        ctx.r.fillRect({plot.x, gy, plot.w, 1.f}, c.border.withAlpha(110));
        const std::string lab = hmi::slowMs(v);
        ctx.r.drawText({plot.x - 8.f - textWidth(ctx, lab, kTiny), gy - 7.f}, lab, kTiny, c.textMuted);
    }
    const auto accent = c.accent;
    const auto orange = c.warning;
    const auto red = c.error;
    gfx::Point prev{};
    bool havePrev = false;
    double prevEnd = -1;
    for (const auto* s : window) {
        if (s->cycles == 0) {
            havePrev = false;
            continue;
        }
        const float x0 = X(std::max(from, s->t)), x1 = X(std::min(to, s->t + s->span));
        const float w = std::max(1.f, x1 - x0 - (x1 - x0 > 3.f ? 1.f : 0.f));
        const float ym = Y(s->median), yx = Y(s->max);
        ctx.r.fillRect({x0, yx, w, std::max(1.f, ym - yx)}, accent.withAlpha(60));
        const gfx::Point p{(x0 + x1) * 0.5f, ym};
        if (havePrev && s->t - prevEnd < 0.5) ctx.r.line(prev, p, accent, 1.6f);
        prev = p;
        havePrev = true;
        prevEnd = s->t + s->span;
    }
    // La periode, en pointilles.
    {
        const float py = Y(period);
        dashed(ctx, {plot.x, py}, {plot.right(), py}, red.withAlpha(150), 1.f, 5.f);
        const std::string lab = "p\xC3\xA9riode " + hmi::slowMs(period);
        ctx.r.drawText({plot.right() - textWidth(ctx, lab, kTiny), py - 15.f}, lab, kTiny, ctx.theme.onSurface(red));
    }
    // Les depassements (rouge, au maximum), les lenteurs (orange, en haut : un clic mene a elle).
    for (const auto* s : window) {
        const float xm = X(s->t + s->span * 0.5);
        if (s->overruns) ctx.r.fillRoundedRect({xm - 4.f, Y(s->max) - 4.f, 8.f, 8.f}, red, 4.f);
        if (s->slow) {
            const gfx::Rect dot{xm - 5.f, plot.y - 12.f, 10.f, 10.f};
            ctx.r.fillRoundedRect(dot, orange, 5.f);
            hits_.push_back({{dot.x - 3.f, dot.y - 3.f, 16.f, 16.f}, "perf:lenteur:" + std::to_string(static_cast<long long>(s->t)),
                             "@lenteur-vers:" + decimal(s->t, 3), "Une lenteur \xC3\xA0 " + perf.clockAt(s->t) + " : un clic l'ouvre", true});
        }
    }
    // Le temps, en bas.
    ctx.r.drawText({plot.x, plot.bottom() + 4.f}, perf.clockAt(from), kTiny, c.textMuted);
    {
        const std::string lab = view.running ? std::string("maintenant") : perf.clockAt(to);
        ctx.r.drawText({plot.right() - textWidth(ctx, lab, kTiny), plot.bottom() + 4.f}, lab, kTiny, c.textMuted);
    }
    hits_.push_back({plot, "perf:graphe", {}, {}, false});
    // La legende.
    {
        float lx = box.x + 12.f;
        const float ly = box.bottom() + 8.f;
        const auto item = [&](gfx::Color col, const std::string& label, int shape) {
            if (shape == 0) ctx.r.fillRect({lx, ly + 6.f, 16.f, 2.f}, col);
            else if (shape == 1) ctx.r.fillRect({lx + 4.f, ly + 1.f, 8.f, 12.f}, col.withAlpha(90));
            else ctx.r.fillRoundedRect({lx + 4.f, ly + 3.f, 8.f, 8.f}, col, 4.f);
            ctx.r.drawText({lx + 22.f, ly}, label, kTiny, c.textMuted);
            lx += 22.f + textWidth(ctx, label, kTiny) + 18.f;
        };
        item(accent, "la m\xC3\xA9" "diane de la seconde", 0);
        item(accent, "jusqu'au maximum", 1);
        item(red, "un d\xC3\xA9passement", 2);
        item(orange, "une lenteur (un clic l'ouvre)", 2);
    }
    y += kChart + 10.f;
    // ---- les quatre petites courbes ----
    {
        std::vector<float> paint, eval, writes, exch;
        for (const auto* s : window) {
            paint.push_back(s->paint);
            eval.push_back(s->evaluate);
            writes.push_back(static_cast<float>(s->writes) / std::max(1.f, s->span));
            exch.push_back(static_cast<float>(s->exchanges) / std::max(1.f, s->span));
        }
        struct Multi { std::string label, value; const std::vector<float>* values; };
        const Multi multis[4] = {
            {"Dessin de la vue", hmi::slowMs(sm.paint) + std::string(kMid) + "p95 " + hmi::slowMs(sm.paintP95), &paint},
            {"Expressions de la vue", hmi::slowMs(sm.evaluate) + kMid + std::to_string(sm.expressions) + " expressions", &eval},
            {"\xC3\x89" "critures", decimal(sm.writesPerS, 1) + " /s" + kMid + std::to_string(sm.writes) + " en " + periodLabel(perfPeriod_), &writes},
            {"\xC3\x89" "changes avec les \xC3\xA9quipements",
             decimal(sm.exchangesPerS, 1) + " /s" + (sm.exchangeMs > 0 ? kMid + hmi::slowMs(sm.exchangeMs) + " de r\xC3\xA9ponse" : std::string{}), &exch},
        };
        const float mw = (r.w - 28.f - 3.f * 10.f) / 4.f;
        for (int i = 0; i < 4; ++i) {
            const gfx::Rect mr{r.x + 14.f + static_cast<float>(i) * (mw + 10.f), y, mw, kMulti};
            ctx.r.fillRoundedRect(mr, c.border, 7.f);
            ctx.r.fillRoundedRect({mr.x + 1.f, mr.y + 1.f, mr.w - 2.f, mr.h - 2.f}, c.windowBg, 6.f);
            ctx.r.drawText({mr.x + 12.f, mr.y + 8.f}, fit(ctx, multis[i].label, kTiny, mr.w - 24.f), kTiny, c.textMuted);
            drawBold(ctx, {mr.x + 12.f, mr.y + 23.f}, fit(ctx, multis[i].value, kSmall, mr.w - 24.f), kSmall, c.text);
            spark(ctx, {mr.x + 12.f, mr.y + 46.f, mr.w - 24.f, 24.f}, squeeze(*multis[i].values, 120), accent);
        }
    }
    y += kMulti + 12.f;
    // ---- ou passe le temps ----
    const auto rows = perf.posts(now, perfPeriod_);
    ctx.r.drawText({r.x + 14.f, y}, "LE CYCLE IHM : O\xC3\x99 PASSE LE TEMPS", kTiny, c.textMuted);
    {
        const std::string info = periodLabel(perfPeriod_) + kMid + std::to_string(sm.cycles) + " cycles";
        ctx.r.drawText({r.right() - 14.f - textWidth(ctx, info, kTiny), y}, info, kTiny, c.textMuted);
    }
    y += 20.f;
    const float colPart = r.x + 14.f + (r.w - 28.f) * 0.38f;
    const float colTimes = colPart + 150.f, colMean = colTimes + 70.f, colP95 = colMean + 74.f, colMax = colP95 + 74.f, colTrend = colMax + 78.f;
    const auto head = [&](float x, const char* t) { ctx.r.drawText({x, y}, t, kTiny, c.textMuted); };
    head(r.x + 14.f, "Poste");
    head(colPart, "Part du cycle");
    head(colTimes, "Fois");
    head(colMean, "Moyenne");
    head(colP95, "p95");
    head(colMax, "Max");
    head(colTrend, "Tendance");
    y += 18.f;
    ctx.r.fillRect({r.x + 14.f, y, r.w - 28.f, 1.f}, c.border);
    y += 4.f;
    for (const auto& row : rows) {
        const float ty = y + (kRow - ctx.r.lineHeight(kSmall)) * 0.5f;
        ctx.r.drawText({r.x + 14.f, ty}, fit(ctx, row.label, kSmall, colPart - r.x - 24.f), kSmall, c.text);
        const gfx::Rect track{colPart, y + kRow * 0.5f - 3.f, 80.f, 6.f};
        ctx.r.fillRoundedRect(track, c.border, 3.f);
        ctx.r.fillRoundedRect({track.x, track.y, std::max(3.f, track.w * static_cast<float>(std::clamp(row.share, 0.0, 1.0))), track.h}, accent, 3.f);
        ctx.r.drawText({track.right() + 8.f, ty}, percent(row.share), kSmall, c.text);
        ctx.r.drawText({colTimes, ty}, std::to_string(row.times), kSmall, c.text);
        ctx.r.drawText({colMean, ty}, hmi::slowMs(row.mean), kSmall, c.text);
        ctx.r.drawText({colP95, ty}, hmi::slowMs(row.p95), kSmall, c.text);
        ctx.r.drawText({colMax, ty}, hmi::slowMs(row.max), kSmall, row.max > period ? ctx.theme.onSurface(red) : c.text);
        spark(ctx, {colTrend, y + 5.f, std::max(30.f, r.right() - 14.f - colTrend), kRow - 10.f}, row.trend, accent);
        y += kRow;
    }
}

// ============================================================ les lenteurs ====
void SimOverviewPane::paintSlow(const ui::PaintContext& ctx, const gfx::Rect& r) {
    const auto& c = ctx.theme.color;
    const auto view = hosts_.perfView ? hosts_.perfView() : SimPerfView{};
    card(ctx, r, false);
    const float capW = caption(ctx, r.x + 14.f, r.y + 15.f, ui::Icon::History, "LENTEURS");
    const std::size_t total = view.slow ? view.slow->entries().size() : 0;
    ctx.r.drawText({r.x + 14.f + capW + 10.f, r.y + 14.f},
                   std::to_string(total) + (total > 1 ? " gard\xC3\xA9" "es" : " gard\xC3\xA9" "e") + std::string(kMid) + "les 500 derni\xC3\xA8res, d'une marche \xC3\xA0 l'autre",
                   kSmall, c.textMuted);
    float right = r.right() - 14.f;
    const float cy = r.y + 21.f;
    {
        const bool hot = hover_ == static_cast<int>(hits_.size());
        const auto br = button(ctx, right, cy, "Vider\xE2\x80\xA6", hot, false, {}, true);
        hits_.push_back({br, "lenteurs:vider", "@vider", "Le journal des lenteurs repart vide (une confirmation d'abord)", true});
        right = br.x - 8.f;
    }
    {
        const bool hot = hover_ == static_cast<int>(hits_.size());
        const auto br = button(ctx, right, cy, "Exporter en CSV", hot, false, {}, true);
        hits_.push_back({br, "lenteurs:csv", "@csv:lenteurs", "Une ligne par lenteur, avec ce qui a pris le temps dedans", true});
    }
    // Les seuils : un clic passe au suivant (pour le projet).
    float y = r.y + kHead;
    {
        float x = r.x + 14.f;
        const auto text = [&](const std::string& t) {
            ctx.r.drawText({x, y + 3.f}, t, kSmall, c.textMuted);
            x += textWidth(ctx, t, kSmall);
        };
        const auto chip = [&](const std::string& label, const std::string& key, const std::string& go, const std::string& tip) {
            const float w = textWidth(ctx, label, kSmall) + 14.f;
            const gfx::Rect cr{x + 3.f, y, w, 22.f};
            const bool hot = hover_ == static_cast<int>(hits_.size());
            ctx.r.fillRoundedRect(cr, hot ? c.accent.withAlpha(90) : c.accent.withAlpha(45), 6.f);
            ctx.r.drawText({cr.x + 7.f, cr.y + (cr.h - ctx.r.lineHeight(kSmall)) * 0.5f}, label, kSmall, c.text);
            hits_.push_back({cr, key, go, tip, true});
            x = cr.right() + 3.f;
        };
        text("Une lenteur : plus de");
        chip(hmi::slowMs(view.rules.absoluteMs), "lenteurs:seuil-ms", "@seuil-ms", "Un clic : le seuil suivant (20, 50, 100, 200, 500 ms) - pour le projet");
        text(", ou plus de");
        chip(decimal(view.rules.factor, 0) + " \xC3\x97", "lenteurs:seuil-fois", "@seuil-fois", "Un clic : le rapport suivant (3, 5, 10, 20) - pour le projet");
        text("son temps habituel (et au moins " + hmi::slowMs(view.rules.floorMs) + "). Un cycle plus long que sa p\xC3\xA9riode, toujours. Un \xC3\xA9quipement : par requ\xC3\xAAte, et une lecture plus longue que sa p\xC3\xA9riode.");
    }
    y += 26.f;
    // Les filtres.
    std::size_t counts[6] = {0, 0, 0, 0, 0, 0};
    if (view.slow)
        for (const auto& e : view.slow->entries()) ++counts[groupOf(e.kind)];
    {
        const std::pair<int, std::string> filters[] = {{-1, "Tout"}, {0, "Actions"}, {1, "Scripts"}, {2, "Vues et popups"}, {4, "Cycles"}, {5, "\xC3\x89quipements"}};
        float x = r.x + 14.f;
        for (const auto& [kind, name] : filters) {
            const std::size_t n = kind < 0 ? total : counts[static_cast<std::size_t>(kind)];
            const std::string label = name + kMid + std::to_string(n);
            const float w = textWidth(ctx, label, kSmall) + 20.f;
            const gfx::Rect fr{x, y, w, 24.f};
            const bool on = slowFilter_ == kind;
            const bool hot = hover_ == static_cast<int>(hits_.size());
            ctx.r.fillRoundedRect(fr, on ? c.selectionBg : hot ? ctx.theme.brand.hover : c.border, 12.f);
            if (!on) ctx.r.fillRoundedRect({fr.x + 1.f, fr.y + 1.f, fr.w - 2.f, fr.h - 2.f}, hot ? ctx.theme.brand.hover : c.panelBg, 11.f);
            ctx.r.drawText({fr.x + 10.f, fr.y + (fr.h - ctx.r.lineHeight(kSmall)) * 0.5f}, label, kSmall, on ? c.selectionText : c.text);
            hits_.push_back({fr, "lenteurs:filtre:" + std::to_string(kind), "@filtre:" + std::to_string(kind), "Montrer : " + name, true});
            x = fr.right() + 6.f;
        }
    }
    y += 36.f;
    // Vider : la confirmation, sur place.
    if (confirmClear_) {
        const gfx::Rect bar{r.x + 14.f, y, r.w - 28.f, 36.f};
        ctx.r.fillRoundedRect(bar, c.error.withAlpha(ctx.theme.isDark() ? 50 : 30), 7.f);
        drawBold(ctx, {bar.x + 12.f, bar.y + 10.f}, "Vider les " + std::to_string(total) + " lenteurs ?", kSmall, c.text);
        const float tx = bar.x + 24.f + textWidth(ctx, "Vider les " + std::to_string(total) + " lenteurs ?", kSmall);
        float bx = bar.right() - 10.f;
        {
            const bool hot = hover_ == static_cast<int>(hits_.size());
            const auto br = button(ctx, bx, bar.y + 18.f, "Vider", hot, true, c.error, true);
            hits_.push_back({br, "lenteurs:vider-oui", "@vider-oui", "Le journal repart vide", true});
            bx = br.x - 8.f;
        }
        {
            const bool hot = hover_ == static_cast<int>(hits_.size());
            const auto br = button(ctx, bx, bar.y + 18.f, "Annuler", hot, false, {}, true);
            hits_.push_back({br, "lenteurs:vider-non", "@vider-non", "Garder les lenteurs", true});
            bx = br.x - 12.f;
        }
        ctx.r.drawText({tx, bar.y + 10.f}, fit(ctx, "Le journal repart vide. L'export CSV d'abord, si tu veux les garder.", kSmall, bx - tx), kSmall,
                       c.textMuted);
        y += 44.f;
    }
    if (total == 0) {
        ctx.r.drawText({r.x + 14.f, y + 6.f}, "Aucune lenteur : tout a r\xC3\xA9pondu \xC3\xA0 temps.", kBody, c.textMuted);
        return;
    }
    // Le tableau : la plus recente d'abord.
    const float colTime = r.x + 14.f, colMs = colTime + 104.f, colUsual = colMs + 72.f, colWhat = colUsual + 128.f;
    const float colWhere = r.right() - 14.f - 90.f - 150.f;
    ctx.r.drawText({colTime, y}, "Heure", kTiny, c.textMuted);
    ctx.r.drawText({colMs, y}, "Dur\xC3\xA9" "e", kTiny, c.textMuted);
    ctx.r.drawText({colUsual, y}, "Habituel", kTiny, c.textMuted);
    ctx.r.drawText({colWhat, y}, "Quoi", kTiny, c.textMuted);
    ctx.r.drawText({colWhere, y}, "O\xC3\xB9", kTiny, c.textMuted);
    y += 18.f;
    ctx.r.fillRect({r.x + 14.f, y, r.w - 28.f, 1.f}, c.border);
    y += 4.f;
    const auto shown = view.slow ? slowShown(*view.slow) : std::vector<const hmi::SlowEntry*>{};
    for (std::size_t i = 0; i < shown.size() && i < kSlowShown; ++i) {
        const auto& e = *shown[i];
        const bool open = e.serial == openedSlow_;
        const gfx::Rect rowRect{r.x + 8.f, y, r.w - 16.f, kSlowRow};
        const bool hot = hover_ == static_cast<int>(hits_.size());
        if (open) ctx.r.fillRoundedRect(rowRect, c.selectionBg.withAlpha(90), 5.f);
        else if (hot) ctx.r.fillRoundedRect(rowRect, ctx.theme.brand.hover, 5.f);
        hits_.push_back({rowRect, "lenteur:" + std::to_string(e.serial), "@lenteur:" + std::to_string(e.serial),
                         open ? std::string("Refermer") : std::string("Ce qui a pris le temps dedans"), false});
        const float ty = y + (kSlowRow - ctx.r.lineHeight(kSmall)) * 0.5f;
        ctx.r.drawText({colTime, ty}, e.clock, kSmall, c.textMuted);
        const auto tone = e.kind == hmi::SlowKind::Cycle ? c.error : c.warning;
        drawBold(ctx, {colMs, ty}, hmi::slowMs(e.ms), kSmall, ctx.theme.onSurface(tone));
        const std::string usual = e.kind == hmi::SlowKind::Cycle ? "p\xC3\xA9riode " + hmi::slowMs(e.period)
                                : e.usual > 0 ? hmi::slowMs(e.usual) + std::string(kMid) + "\xC3\x97" + decimal(e.ratio(), e.ratio() < 10 ? 1 : 0)
                                              : std::string("\xE2\x80\x94");
        ctx.r.drawText({colUsual, ty}, fit(ctx, usual, kSmall, colWhat - colUsual - 8.f), kSmall, c.textMuted);
        const auto pr = pill(ctx, colWhat + textWidth(ctx, hmi::slowKindName(e.kind), kTiny) + 14.f, y + (kSlowRow - 18.f) * 0.5f,
                             hmi::slowKindName(e.kind), tone);
        ctx.r.drawText({pr.right() + 8.f, ty}, fit(ctx, e.what, kSmall, colWhere - pr.right() - 16.f), kSmall, c.text);
        ctx.r.drawText({colWhere, ty}, fit(ctx, e.where, kSmall, 140.f), kSmall, c.text);
        const bool somewhere = e.viewId != hmi::kNoId || e.scriptId != hmi::kNoId || !e.equipment.empty() || e.kind == hmi::SlowKind::Cycle;
        if (somewhere) {
            const std::string go = "Aller \xC3\xA0 \xE2\x80\xBA";
            const float gw = textWidth(ctx, go, kSmall);
            const gfx::Rect gr{r.right() - 22.f - gw, y + 5.f, gw + 8.f, kSlowRow - 10.f};
            const bool ghot = hover_ == static_cast<int>(hits_.size());
            hits_.push_back({gr, "lenteur:" + std::to_string(e.serial) + ":aller", "@aller:" + std::to_string(e.serial),
                             "Ouvrir l'objet, le script ou la popup", true});
            ctx.r.drawText({gr.x + 4.f, ty}, go, kSmall, ghot ? c.text : c.accent);
        }
        y += kSlowRow;
        if (open) {
            for (const auto& part : e.inside) {
                const float px = colWhat + 12.f + static_cast<float>(part.depth) * 18.f;
                const float py = y + (kPart - ctx.r.lineHeight(kSmall)) * 0.5f;
                ctx.r.fillRect({px - 10.f, y + kPart * 0.5f, 6.f, 1.f}, c.border);
                const std::string ms = part.ms > 0 ? hmi::slowMs(part.ms) : std::string{};
                const float msW = textWidth(ctx, ms, kSmall);
                ctx.r.drawText({px, py}, fit(ctx, part.what, kSmall, colWhere - px - msW - 16.f), kSmall, c.text);
                ctx.r.drawText({colWhere - 8.f - msW, py}, ms, kSmall, c.textMuted);
                y += kPart;
            }
            std::string foot = e.user.empty() ? std::string("Personne de connect\xC3\xA9") : "Utilisateur : " + e.user;
            if (!e.view.empty()) foot += std::string(kMid) + "vue : " + e.view;
            if (e.runs > 1) foot += kMid + std::to_string(e.slowRuns) + (e.slowRuns > 1 ? " fois plus lente" : " fois plus lente") + " sur " + std::to_string(e.runs);
            ctx.r.drawText({colWhat + 12.f, y + 5.f}, fit(ctx, foot, kSmall, r.right() - 28.f - colWhat), kSmall, c.textMuted);
            y += 28.f;
        }
        ctx.r.fillRect({r.x + 14.f, y - 1.f, r.w - 28.f, 1.f}, c.border.withAlpha(90));
    }
    if (shown.size() > kSlowShown) {
        const std::string more = "et " + std::to_string(shown.size() - kSlowShown) + " de plus : le Journal, filtre Lenteurs \xE2\x80\xBA";
        const float w = textWidth(ctx, more, kSmall);
        const gfx::Rect mr{r.x + 14.f, y + 4.f, w, 18.f};
        const bool hot = hover_ == static_cast<int>(hits_.size());
        hits_.push_back({mr, "lenteurs:journal", "journal-lenteurs", "Toutes les lenteurs dans le Journal de la simulation", false});
        ctx.r.drawText({mr.x, mr.y}, more, kSmall, hot ? c.text : c.accent);
    }
}

// ====================================================== le survol de la courbe ====
std::string SimOverviewPane::chartTip(gfx::Point mouse) const {
    if (!chartRect_.contains(mouse) || chartTo_ <= chartFrom_) return {};
    const auto view = hosts_.perfView ? hosts_.perfView() : SimPerfView{};
    if (!view.perf) return {};
    const double t = chartFrom_ + (chartTo_ - chartFrom_) * static_cast<double>((mouse.x - chartRect_.x) / chartRect_.w);
    const hmi::PerfSecond* best = nullptr;
    for (const auto* s : view.perf->window(view.now, perfPeriod_))
        if (s->t <= t && t < s->t + s->span) best = s;
    if (!best) return {};
    std::string tip = view.perf->clockAt(best->t) + std::string(kMid) + "cycle : m\xC3\xA9" "diane " + hmi::slowMs(best->median) + ", max " + hmi::slowMs(best->max);
    tip += "\n" + std::to_string(best->cycles) + " cycles" + (best->span > 1.5f ? " en 10 s" : std::string{});
    if (best->overruns) tip += kMid + std::to_string(best->overruns) + (best->overruns > 1 ? " d\xC3\xA9passements" : " d\xC3\xA9passement");
    if (best->slow) tip += kMid + std::to_string(best->slow) + (best->slow > 1 ? " lenteurs" : " lenteur");
    if (best->paint > 0) tip += "\ndessin " + hmi::slowMs(best->paint);
    return tip;
}

// ========================================================= les cles du volet ====
bool SimOverviewPane::local(const std::string& key) {
    const auto view = hosts_.perfView ? hosts_.perfView() : SimPerfView{};
    const auto arg = [&key](std::string_view prefix) { return key.rfind(prefix, 0) == 0 ? key.substr(prefix.size()) : std::string{}; };
    if (const auto p = arg("periode:"); !p.empty()) {
        setPerfPeriod(std::atof(p.c_str()));
        return true;
    }
    if (key == "zero") {
        if (hosts_.resetPerf) hosts_.resetPerf();
        invalidate();
        return true;
    }
    if (const auto w = arg("csv:"); !w.empty()) {
        if (hosts_.exportCsv) hosts_.exportCsv(w);
        return true;
    }
    if (const auto f = arg("filtre:"); !f.empty()) {
        setSlowFilter(std::atoi(f.c_str()));
        return true;
    }
    if (const auto s = arg("lenteur:"); !s.empty()) {
        const auto serial = std::strtoull(s.c_str(), nullptr, 10);
        openSlow(openedSlow_ == serial ? 0 : serial);
        return true;
    }
    if (const auto t = arg("lenteur-vers:"); !t.empty() && view.slow) {
        std::string at = t;
        for (auto& ch : at)
            if (ch == ',') ch = '.';
        const double when = std::atof(at.c_str());
        const hmi::SlowEntry* found = nullptr;
        for (const auto& e : view.slow->entries())
            if (e.at >= when - 0.001 && e.at < when + 10.0 && !found) found = &e;
        if (found) {
            slowFilter_ = -1;
            openSlow(found->serial);
            reveal("lenteurs");
        }
        return true;
    }
    if (key == "voir-lenteurs") {
        reveal("lenteurs");
        return true;
    }
    if (const auto s = arg("aller:"); !s.empty()) {
        if (hosts_.goSlow) hosts_.goSlow(std::strtoull(s.c_str(), nullptr, 10));
        return true;
    }
    if (key == "vider") {
        confirmClear_ = true;
        invalidate();
        return true;
    }
    if (key == "vider-non") {
        confirmClear_ = false;
        invalidate();
        return true;
    }
    if (key == "vider-oui") {
        confirmClear_ = false;
        openedSlow_ = 0;
        if (hosts_.clearSlow) hosts_.clearSlow();
        invalidate();
        return true;
    }
    if (key == "seuil-ms" || key == "seuil-fois") {
        auto rules = view.rules;
        if (key == "seuil-ms") {
            const double steps[] = {20, 50, 100, 200, 500};
            double next = steps[0];
            for (const double s : steps)
                if (s > rules.absoluteMs + 0.5) {
                    next = s;
                    break;
                }
            rules.absoluteMs = rules.absoluteMs >= 500 ? steps[0] : next;
        } else {
            const double steps[] = {3, 5, 10, 20};
            double next = steps[0];
            for (const double s : steps)
                if (s > rules.factor + 0.5) {
                    next = s;
                    break;
                }
            rules.factor = rules.factor >= 20 ? steps[0] : next;
        }
        if (hosts_.setSlowRules) hosts_.setSlowRules(rules);
        invalidate();
        return true;
    }
    return false;
}

void SimOverviewPane::click(const std::string& go) {
    if (go.empty()) return;
    if (go.front() == '@') {
        (void)local(go.substr(1));
        return;
    }
    if (hosts_.go) hosts_.go(go);
}

} // namespace app
