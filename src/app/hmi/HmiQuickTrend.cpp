// =============================================================================
//  app/hmi/HmiQuickTrend.cpp - la fenetre graphique temporaire (1.10, chantier O)
// =============================================================================
#include "HmiQuickTrend.hpp"

#include "../Capture.hpp"                 // l'image de la fenetre (PNG)

#include "../../ui/Theme.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>

namespace app {

namespace {

const gfx::Color kTraces[] = {gfx::Color::rgb(0x5B9BF0), gfx::Color::rgb(0x4CC38A), gfx::Color::rgb(0xE8B04A),
                              gfx::Color::rgb(0x45C4D8), gfx::Color::rgb(0xD07FC6), gfx::Color::rgb(0xE8835A),
                              gfx::Color::rgb(0xA3D45E), gfx::Color::rgb(0xF06B7E)};
constexpr std::size_t kTraceCount = sizeof(kTraces) / sizeof(kTraces[0]);
constexpr double kKeep = 1800.0;          // ce qu'on garde : la plus longue duree
constexpr float  kBarH = 30.f, kLegendH = 26.f, kPad = 6.f;

// 12.5 -> "12,5" ; 3 -> "3" (comme Excel en francais).
std::string number(double v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    std::string s = buf;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

std::string durationLabel(double s) {
    if (s < 60.0) return std::to_string(static_cast<int>(s)) + " s";
    return std::to_string(static_cast<int>(s / 60.0)) + " min";
}

// La valeur d'une courbe a l'heure t : le dernier point a t ou avant (faux : aucun).
bool valueAt(const HmiQuickTrend::Series& s, double t, double& out) {
    bool found = false;
    for (const auto& p : s.points) {
        if (p.t > t + 1e-9) break;
        out = p.v;
        found = true;
    }
    return found;
}

} // namespace

HmiQuickTrend::HmiQuickTrend(std::string id, std::vector<std::string> paths, Source source)
    : ui::Widget(std::move(id)), source_(std::move(source)) {
    for (const auto& p : paths) (void)addVariable(p);
}

std::string HmiQuickTrend::title() const {
    std::string out = "Graphique : ";
    for (std::size_t i = 0; i < series_.size(); ++i) {
        if (i == 4) {
            out += " +" + std::to_string(series_.size() - 4);
            break;
        }
        out += (i ? ", " : "") + series_[i].path;
    }
    return out;
}

bool HmiQuickTrend::addVariable(const std::string& path) {
    if (path.empty()) return false;
    for (const auto& s : series_)
        if (s.path == path) return false;
    Series s;
    s.path = path;
    series_.push_back(std::move(s));
    changed->emit();
    invalidate();
    return true;
}

bool HmiQuickTrend::removeVariable(std::size_t index) {
    if (index >= series_.size()) return false;
    series_.erase(series_.begin() + static_cast<std::ptrdiff_t>(index));
    changed->emit();
    invalidate();
    return true;
}

void HmiQuickTrend::setHidden(std::size_t index, bool hidden) {
    if (index >= series_.size()) return;
    series_[index].hidden = hidden;
    invalidate();
}

bool HmiQuickTrend::live() const { return source_.running && source_.running(); }

double HmiQuickTrend::now() const {
    if (source_.clock) return source_.clock();
    static const auto origin = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - origin).count();
}

std::size_t HmiQuickTrend::sample(double t) {
    if (!live() || !source_.read) return 0;
    std::size_t n = 0;
    for (auto& s : series_) {
        double v = 0.0;
        bool isBool = false;
        if (!source_.read(s.path, v, isBool)) continue;
        s.isBool = isBool;
        if (!s.points.empty() && s.points.back().t >= t) s.points.back() = {t, v};   // meme instant : la derniere lecture
        else s.points.push_back({t, v});
        ++n;
    }
    if (n > 0) {
        last_ = t;
        any_ = true;
        trim();
        invalidate();
    }
    return n;
}

void HmiQuickTrend::tick() { (void)sample(now()); }

void HmiQuickTrend::trim() {
    for (auto& s : series_)
        while (s.points.size() > 1 && s.points.front().t < last_ - kKeep) s.points.pop_front();
}

void HmiQuickTrend::setDuration(double seconds) {
    duration_ = std::clamp(seconds, 1.0, kKeep);
    invalidate();
}

void HmiQuickTrend::setPaused(bool on) {
    if (on == paused_) return;
    paused_ = on;
    frozen_ = last_;
    invalidate();
}

void HmiQuickTrend::setAutoScale() {
    auto_ = true;
    invalidate();
}

void HmiQuickTrend::setFixedScale(double lo, double hi) {
    if (!(hi > lo)) return;
    auto_ = false;
    lo_ = lo;
    hi_ = hi;
    invalidate();
}

std::pair<double, double> HmiQuickTrend::window() const {
    const double end = paused_ ? frozen_ : last_;
    return {end - duration_, end};
}

std::pair<double, double> HmiQuickTrend::scale() const {
    if (!auto_) return {lo_, hi_};
    const auto [t0, t1] = window();
    bool any = false;
    double lo = 0.0, hi = 0.0;
    for (const auto& s : series_) {
        if (s.hidden) continue;
        for (const auto& p : s.points) {
            if (p.t < t0 - 1e-9 || p.t > t1 + 1e-9) continue;
            if (!any) lo = hi = p.v;
            lo = std::min(lo, p.v);
            hi = std::max(hi, p.v);
            any = true;
        }
    }
    if (!any) return {0.0, 1.0};
    if (hi - lo < 1e-9) {
        lo -= 1.0;
        hi += 1.0;
    }
    const double m = (hi - lo) * 0.05;
    return {lo - m, hi + m};
}

void HmiQuickTrend::setCursor(double t) {
    cursorOn_ = true;
    cursorT_ = t;
    invalidate();
}

void HmiQuickTrend::clearCursor() {
    cursorOn_ = false;
    invalidate();
}

std::vector<std::pair<std::string, std::string>> HmiQuickTrend::cursorValues() const {
    std::vector<std::pair<std::string, std::string>> out;
    const double t = cursorOn_ ? cursorT_ : window().second;
    for (const auto& s : series_) {
        double v = 0.0;
        out.emplace_back(s.path, valueAt(s, t, v) ? (s.isBool ? std::string(v != 0.0 ? "1" : "0") : number(v)) : std::string("\xE2\x80\x94"));
    }
    return out;
}

std::string HmiQuickTrend::csv() const {
    std::map<double, std::vector<std::string>> lines;
    for (std::size_t i = 0; i < series_.size(); ++i)
        for (const auto& p : series_[i].points) {
            auto& row = lines[p.t];
            row.resize(series_.size());
            row[i] = series_[i].isBool ? std::string(p.v != 0.0 ? "1" : "0") : number(p.v);
        }
    std::string out = "Temps (s)";
    for (const auto& s : series_) out += ";" + s.path;
    out += "\r\n";
    const double t0 = lines.empty() ? 0.0 : lines.begin()->first;
    for (auto& [t, row] : lines) {
        row.resize(series_.size());
        char buf[48];
        std::snprintf(buf, sizeof buf, "%.3f", t - t0);
        std::string ts = buf;
        std::replace(ts.begin(), ts.end(), '.', ',');
        out += ts;
        for (const auto& c : row) out += ";" + c;
        out += "\r\n";
    }
    return out;
}

std::string HmiQuickTrend::statusText() const {
    if (series_.empty()) return "Aucune variable : glisse-en une depuis l'onglet, ou clique +.";
    if (!live() && !any_) return "D\xC3\xA9marre la simulation pour voir les valeurs.";
    if (!live()) return "Simulation arr\xC3\xAAt\xC3\xA9" "e : les derni\xC3\xA8res valeurs restent affich\xC3\xA9" "es.";
    if (paused_) return "En pause : les valeurs continuent d'arriver, Reprendre les montre.";
    return "En direct \xC2\xB7 " + durationLabel(duration_) + " affich\xC3\xA9" "es";
}

// ---- la mise en page --------------------------------------------------------------
gfx::Rect HmiQuickTrend::plotRect() const {
    const auto b = bounds();
    const float left = 56.f;
    return {b.x + left, b.y + kBarH + kPad, std::max(10.f, b.w - left - kPad - 4.f), std::max(10.f, b.h - kBarH - kLegendH - 2.f * kPad - 14.f)};
}

float HmiQuickTrend::legendWidth(std::size_t i) const {
    return 40.f + 7.5f * static_cast<float>(series_[i].path.size()) + 70.f + 16.f;   // + le x qui retire la courbe
}

void HmiQuickTrend::requestImage(std::function<void(std::vector<std::uint8_t>)> done) {
    imageDone_ = std::move(done);
    invalidate();
}

bool HmiQuickTrend::partRect(int part, gfx::Rect& out) const {
    const auto b = bounds();
    float x = b.x + kPad;
    const float y = b.y + 3.f, h = kBarH - 6.f;
    for (int i = 0; i < 4; ++i) {
        if (part == PDuration0 + i) {
            out = {x, y, 54.f, h};
            return true;
        }
        x += 56.f;
    }
    x += 8.f;
    const struct { int part; float w; } bar[] = {{PPause, 86.f}, {PScale, 104.f}, {PAdd, 30.f}, {PExportPng, 58.f}, {PExportCsv, 58.f}};
    for (const auto& e : bar) {
        if (part == e.part) {
            out = {x, y, e.w, h};
            return true;
        }
        x += e.w + 4.f;
    }
    if (part == PPlot) {
        out = plotRect();
        return true;
    }
    if (part == PStart) {                              // sous le message, quand rien ne tourne
        if (live() || series_.empty()) return false;
        const auto pr = plotRect();
        const float w = std::min(pr.w - 8.f, 250.f);
        out = {pr.x + (pr.w - w) / 2.f, pr.y + pr.h / 2.f + 14.f, w, kBarH - 6.f};
        return true;
    }
    if (part >= PLegend0 && static_cast<std::size_t>(part - PLegend0) < series_.size()) {
        float lx = b.x + kPad;
        for (std::size_t i = 0; i < series_.size(); ++i) {
            const float w = legendWidth(i);
            if (static_cast<int>(i) == part - PLegend0) {
                out = {lx, b.bottom() - kLegendH - 2.f, w, kLegendH - 4.f};
                return true;
            }
            lx += w + 6.f;
        }
    }
    return false;
}

// ---- le dessin --------------------------------------------------------------------
void HmiQuickTrend::onPaint(const ui::PaintContext& ctx) {
    tick();                                            // en direct : une lecture par image (la pause fige l'affichage)
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto font = ctx.theme.font.smallUi;
    const auto b = bounds();
    r.fillRect(b, c.panelBg);
    r.fillRect({b.x, b.y, b.w, kBarH}, c.headerBg);
    const float lh = r.lineHeight(font);
    auto button = [&](int part, const std::string& text, bool on) {
        gfx::Rect pr{};
        if (!partRect(part, pr)) return;
        r.fillRoundedRect(pr, on ? c.accent : c.inputBg, 4.f);
        r.strokeRect(pr, c.border);
        const float tw = r.measure(text, font).width;
        r.drawText({pr.x + (pr.w - tw) / 2.f, pr.y + (pr.h - lh) / 2.f}, text, font, on ? c.textInverted : c.text);
    };
    for (int i = 0; i < 4; ++i) button(PDuration0 + i, durationLabel(kDurations[i]), std::abs(duration_ - kDurations[i]) < 1e-6);
    button(PPause, paused_ ? "Reprendre" : "Pause", paused_);
    button(PScale, auto_ ? "\xC3\x89" "chelle auto" : "\xC3\x89" "chelle fixe", false);
    button(PAdd, "+", false);
    button(PExportPng, "PNG\xE2\x80\xA6", false);
    button(PExportCsv, "CSV\xE2\x80\xA6", false);

    const auto pr = plotRect();
    r.fillRect(pr, c.inputBg);
    r.strokeRect(pr, c.border);
    const auto [lo, hi] = scale();
    const auto [t0, t1] = window();
    for (int k = 0; k <= 4; ++k) {
        const float y = pr.y + pr.h * static_cast<float>(k) / 4.f;
        r.line({pr.x, y}, {pr.right(), y}, c.gridLine);
        const std::string label = number(hi - (hi - lo) * k / 4.0);
        const float tw = r.measure(label, font).width;
        r.drawText({pr.x - tw - 6.f, y - lh / 2.f}, label, font, c.textMuted);
    }
    r.drawText({pr.x, pr.bottom() + 2.f}, "-" + durationLabel(duration_), font, c.textMuted);
    const std::string zero = paused_ ? "pause" : "maintenant";
    r.drawText({pr.right() - r.measure(zero, font).width, pr.bottom() + 2.f}, zero, font, c.textMuted);
    auto xOf = [&](double t) { return pr.x + static_cast<float>((t - t0) / std::max(1e-9, t1 - t0)) * pr.w; };
    auto yOf = [&](double v) { return pr.bottom() - static_cast<float>((v - lo) / std::max(1e-12, hi - lo)) * pr.h; };
    r.pushClip(pr);
    for (std::size_t i = 0; i < series_.size(); ++i) {
        const auto& s = series_[i];
        if (s.hidden) continue;
        const auto col = kTraces[i % kTraceCount];
        bool have = false;
        gfx::Point prev{};
        for (const auto& p : s.points) {
            if (p.t < t0 - duration_ * 0.02 || p.t > t1 + 1e-9) {
                if (p.t < t0) {
                    prev = {xOf(p.t), yOf(p.v)};
                    have = true;
                }
                continue;
            }
            const gfx::Point cur{xOf(p.t), yOf(p.v)};
            if (have) {
                if (s.isBool) {
                    r.line(prev, {cur.x, prev.y}, col, 2.f);       // l'escalier : a plat, puis le saut
                    r.line({cur.x, prev.y}, cur, col, 2.f);
                } else {
                    r.line(prev, cur, col, 2.f);
                }
            }
            prev = cur;
            have = true;
        }
    }
    if (cursorOn_ && cursorT_ >= t0 && cursorT_ <= t1) {
        const float x = xOf(cursorT_);
        r.line({x, pr.y}, {x, pr.bottom()}, c.textMuted, 1.f);
    }
    r.popClip();
    const std::string status = statusText();
    if (!live() || series_.empty() || paused_) {
        const float tw = r.measure(status, font).width;
        r.drawText({pr.x + (pr.w - tw) / 2.f, pr.y + pr.h / 2.f - lh / 2.f}, status, font, c.textMuted);
    }
    button(PStart, "D\xC3\xA9marrer la simulation de l'IHM", true);   // rien ne tourne : le proposer

    // La legende : une case par courbe (cliquer la masque), sa valeur au curseur.
    const auto values = cursorValues();
    for (std::size_t i = 0; i < series_.size(); ++i) {
        gfx::Rect lr{};
        if (!partRect(PLegend0 + static_cast<int>(i), lr)) continue;
        const auto col = kTraces[i % kTraceCount];
        const gfx::Rect box{lr.x + 2.f, lr.y + (lr.h - 12.f) / 2.f, 12.f, 12.f};
        if (series_[i].hidden) r.strokeRect(box, c.textMuted);
        else r.fillRect(box, col);
        r.line({box.right() + 4.f, lr.y + lr.h / 2.f}, {box.right() + 18.f, lr.y + lr.h / 2.f}, col, 2.f);
        const std::string text = series_[i].path + " = " + (i < values.size() ? values[i].second : std::string{});
        r.drawText({box.right() + 22.f, lr.y + (lr.h - lh) / 2.f}, text, font, series_[i].hidden ? c.textMuted : c.text);
        // Le x : retirer la courbe (1.10, comme la maquette).
        const gfx::Point xc{lr.right() - 8.f, lr.y + lr.h / 2.f};
        r.line({xc.x - 3.5f, xc.y - 3.5f}, {xc.x + 3.5f, xc.y + 3.5f}, c.textMuted, 1.5f);
        r.line({xc.x - 3.5f, xc.y + 3.5f}, {xc.x + 3.5f, xc.y - 3.5f}, c.textMuted, 1.5f);
    }
    if (live()) invalidate();                           // la prochaine image relit les valeurs
    // L'image demandee (Exporter l'image) : ce qui vient d'etre dessine, lu dans
    // le rendu ; rien ne s'ouvre ici, `done` ecrit.
    if (imageDone_) {
        auto done = std::move(imageDone_);
        imageDone_ = nullptr;
        std::vector<std::uint8_t> png;
        (void)regionToPng(r, static_cast<int>(b.x), static_cast<int>(b.y), static_cast<int>(b.w), static_cast<int>(b.h), png);
        done(std::move(png));
    }
}

ui::EventResult HmiQuickTrend::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const auto pr = plotRect();
        if (pr.contains(m->pos)) {
            const auto [t0, t1] = window();
            setCursor(t0 + (t1 - t0) * static_cast<double>((m->pos.x - pr.x) / std::max(1.f, pr.w)));
            return ui::EventResult::Consumed;
        }
        if (cursorOn_) clearCursor();                    // hors du trace : plus de curseur
        return ui::EventResult::Ignored;
    }
    const auto* d = std::get_if<ui::MouseDown>(&ev);
    if (!d || d->button != ui::MouseButton::Left) return ui::EventResult::Ignored;
    gfx::Rect pr{};
    for (int i = 0; i < 4; ++i)
        if (partRect(PDuration0 + i, pr) && pr.contains(d->pos)) {
            setDuration(kDurations[i]);
            return ui::EventResult::Consumed;
        }
    if (partRect(PPause, pr) && pr.contains(d->pos)) {
        setPaused(!paused_);
        return ui::EventResult::Consumed;
    }
    if (partRect(PScale, pr) && pr.contains(d->pos)) {
        if (auto_) {
            const auto [lo, hi] = scale();
            setFixedScale(lo, hi);
        } else {
            setAutoScale();
        }
        return ui::EventResult::Consumed;
    }
    if (partRect(PAdd, pr) && pr.contains(d->pos)) {
        addRequested->emit();
        return ui::EventResult::Consumed;
    }
    if (partRect(PExportPng, pr) && pr.contains(d->pos)) {
        exportRequested->emit(static_cast<int>(Export::Png));
        return ui::EventResult::Consumed;
    }
    if (partRect(PExportCsv, pr) && pr.contains(d->pos)) {
        exportRequested->emit(static_cast<int>(Export::Csv));
        return ui::EventResult::Consumed;
    }
    if (partRect(PStart, pr) && pr.contains(d->pos)) {
        startRequested->emit();
        return ui::EventResult::Consumed;
    }
    for (std::size_t i = 0; i < series_.size(); ++i)
        if (partRect(PLegend0 + static_cast<int>(i), pr) && pr.contains(d->pos)) {
            if (d->pos.x >= pr.right() - 16.f) (void)removeVariable(i);   // le x : retirer la courbe
            else setHidden(i, !series_[i].hidden);
            return ui::EventResult::Consumed;
        }
    return ui::EventResult::Ignored;
}

} // namespace app
