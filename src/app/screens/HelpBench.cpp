#include "HelpBench.hpp"
#include "HelpChrome.hpp"

#include "../../help/HelpCodes.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

namespace app {
namespace {

using State = help::TryBench::State;

constexpr int kPeriods[] = {10, 20, 50, 100};

// Le fondu d'une valeur qui vient de changer : 1 a l'instant, 0 apres flashMs.
float flashOf(const ui::PaintContext& ctx, double at) {
    if (at < 0.0) return 0.f;
    const float t = static_cast<float>((ctx.time - at) * 1000.0) / std::max(1.f, ctx.theme.motion.flashMs);
    return t >= 1.f ? 0.f : 1.f - t;
}

// Une LED : un disque, un halo quand elle est allumee. Le halo est ce qui la
// fait lire comme une lumiere et pas comme une pastille de couleur.
void drawLed(const ui::PaintContext& ctx, gfx::Point c, float d, bool on, float glow = 1.f) {
    const auto& th = ctx.theme;
    if (on) {
        ctx.r.fillRoundedRect({c.x - d * 0.5f - 3.f, c.y - d * 0.5f - 3.f, d + 6.f, d + 6.f},
                              th.brand.led.withAlpha(static_cast<std::uint8_t>(70.f * glow)),
                              d * 0.5f + 3.f);
        ctx.r.fillRoundedRect({c.x - d * 0.5f, c.y - d * 0.5f, d, d}, th.brand.led, d * 0.5f);
        // Le reflet : un point clair en haut a gauche.
        ctx.r.fillRoundedRect({c.x - d * 0.25f, c.y - d * 0.3f, d * 0.3f, d * 0.22f},
                              gfx::Color{255, 255, 255, 120}, d * 0.1f);
    } else {
        ctx.r.fillRoundedRect({c.x - d * 0.5f, c.y - d * 0.5f, d, d}, th.brand.ledOff, d * 0.5f);
        ctx.r.fillRoundedRect({c.x - d * 0.5f + 1.5f, c.y - d * 0.5f + 1.5f, d - 3.f, d - 3.f},
                              th.color.panelBg.withAlpha(90), d * 0.5f - 1.5f);
    }
}

// Un cadenas, dessine : il n'y en a pas dans les icones, et « force » doit se
// lire sans legende.
void drawLock(const ui::PaintContext& ctx, gfx::Rect r, gfx::Color c) {
    const float w = std::min(r.w, r.h) * 0.62f;
    const float x = r.x + (r.w - w) * 0.5f;
    const float bodyH = w * 0.72f;
    const float y = r.y + r.h * 0.5f - bodyH * 0.25f;
    ctx.r.fillRoundedRect({x, y, w, bodyH}, c, 2.f);
    const float sx0 = x + w * 0.22f, sx1 = x + w * 0.78f, top = y - w * 0.5f;
    ctx.r.line({sx0, y}, {sx0, top + w * 0.2f}, c, 1.6f);
    ctx.r.line({sx1, y}, {sx1, top + w * 0.2f}, c, 1.6f);
    ctx.r.line({sx0, top + w * 0.2f}, {(sx0 + sx1) * 0.5f, top}, c, 1.6f);
    ctx.r.line({(sx0 + sx1) * 0.5f, top}, {sx1, top + w * 0.2f}, c, 1.6f);
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

// "1234567" -> "1 234 567" : un compteur qui grandit se lit par paquets.
std::string grouped(std::uint64_t v) {
    std::string s = std::to_string(v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), " ");
    return s;
}

std::string seconds(std::int64_t ms) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.2f s", static_cast<double>(ms) / 1000.0);
    std::string s = buf;
    for (auto& ch : s) if (ch == '.') ch = ',';           // la virgule francaise
    return s;
}

ui::Tone seriesTone(std::size_t i) {
    constexpr ui::Tone kTones[] = {ui::Tone::Accent, ui::Tone::Family2, ui::Tone::Family3,
                                   ui::Tone::Family1};
    return kTones[i % 4];
}

const help::WatchRow* findRow(const help::TryBench* b, std::string_view name) {
    if (b == nullptr) return nullptr;
    for (const auto& r : b->rows()) if (r.name == name) return &r;
    return nullptr;
}

} // namespace

// =============================================================================
//  LA TRANSPORT
// =============================================================================
class BenchTransport final : public ui::Widget {
public:
    explicit BenchTransport(std::string id) : ui::Widget(std::move(id)) {}

    void setBench(help::TryBench* b) { bench_ = b; invalidate(); }

    // 1 lecture/pause, 2 arret, 3 un cycle, 4 fermer ; 100 + ms : la periode.
    const core::SignalPtr<int> command = core::Signal<int>::create();

    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {600.f, 76.f};
        h.minimum   = {300.f, 76.f};
        h.stretchX  = 1.f;
        return h;
    }

    // Ce qu'un bouton peut faire MAINTENANT, jugé sur le banc et pas sur la
    // derniere image : un clic arrive parfois avant que l'image ait suivi.
    [[nodiscard]] bool enabledFor(int id) const {
        if (bench_ == nullptr) return false;
        if (id == 2) return bench_->scans() > 0;
        return true;
    }
    void fire(int id) {
        if (!enabledFor(id)) return;
        command->emit(id);
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c  = th.color;
        const auto  b  = bounds();
        ctx.r.fillRect(b, c.panelBg);
        ctx.r.line({b.x, b.bottom() - 0.5f}, {b.right(), b.bottom() - 0.5f}, c.border, 1.f);

        const bool has     = bench_ != nullptr;
        const bool running = has && bench_->state() == State::Running;
        const bool paused  = has && bench_->state() == State::Paused;
        zones_.clear();

        const float cy = b.y + b.h * 0.5f;
        float x = b.x + 20.f;

        // ---- les trois boutons ronds -------------------------------------------
        const auto round = [&](int id, float d, ui::Icon icon, bool primary, bool enabled,
                               std::string tip) {
            const gfx::Rect r{x, cy - d * 0.5f, d, d};
            zones_.push_back({r, id, enabled, std::move(tip)});
            const bool hov = enabled && hover_ == id, down = enabled && pressed_ == id;
            if (primary) {
                auto bg = enabled ? (down ? c.accentPressed : hov ? c.accentHover : c.accent) : c.border;
                // L'anneau de l'action principale : elle se trouve sans chercher.
                if (enabled)
                    ctx.r.fillRoundedRect({r.x - 4.f, r.y - 4.f, d + 8.f, d + 8.f},
                                          c.accent.withAlpha(th.isDark() ? 50 : 36), d * 0.5f + 4.f);
                ctx.r.fillRoundedRect(r, bg, d * 0.5f);
            } else {
                ctx.r.fillRoundedRect(r, enabled ? c.borderStrong : c.border, d * 0.5f);
                ctx.r.fillRoundedRect({r.x + 1.5f, r.y + 1.5f, d - 3.f, d - 3.f},
                                      hov ? c.rowAltBg : th.brand.card, d * 0.5f - 1.5f);
                if (down) ctx.r.fillRoundedRect(r, th.brand.hover, d * 0.5f);
            }
            const float s = d * 0.4f;
            ui::drawIcon(ctx.r, icon, {r.x + (d - s) * 0.5f, r.y + (d - s) * 0.5f, s, s},
                         !enabled ? c.textDisabled : primary ? c.textInverted : c.text);
            x += d + 12.f;
        };
        round(2, 40.f, ui::Icon::Stop, false, has && bench_->scans() > 0,
              "Arrêt : remet à zéro, forçages compris");
        round(1, 54.f, running ? ui::Icon::Pause : ui::Icon::Play, true, has,
              running ? "Pause" : "Marche : le cycle tourne, encore et encore (F5)");
        round(3, 40.f, ui::Icon::StepOnce, false, has,
              "Un seul cycle : c'est ainsi qu'on lit un front montant");

        // ---- l'etat, et le compteur ------------------------------------------------
        x += 10.f;
        ctx.r.line({x, b.y + 16.f}, {x, b.bottom() - 16.f}, c.border, 1.f);
        x += 20.f;
        {
            // La LED respire tant que ca tourne ; fixe en pause, eteinte a
            // l'arret. Trois etats, trois aspects, pas de texte a lire.
            const float pulse = running
                ? 0.55f + 0.45f * static_cast<float>(0.5 + 0.5 * std::sin(ctx.time * 5.0)) : 1.f;
            const float ly = b.y + 22.f;
            if (running) {
                drawLed(ctx, {x + 6.f, ly}, 10.f, true, pulse);
                invalidate();
            } else if (paused) {
                ctx.r.fillRoundedRect({x + 1.f, ly - 5.f, 10.f, 10.f}, c.warning, 5.f);
            } else {
                drawLed(ctx, {x + 6.f, ly}, 10.f, false);
            }
            const char* etat = !has ? "AUCUN ESSAI" : running ? "EN MARCHE" : paused ? "EN PAUSE"
                                                             : "À L'ARRÊT";
            const auto tone = running ? th.onSurface(c.ok) : paused ? th.onSurface(c.warning)
                                                                    : c.textMuted;
            const float capH = ctx.r.lineHeight(th.font.caption);
            ctx.r.drawText({x + 18.f, ly - capH * 0.5f}, etat, th.font.caption, tone);
            ctx.r.drawText({x + 18.6f, ly - capH * 0.5f}, etat, th.font.caption, tone);

            const std::string n = has ? grouped(bench_->scans()) : "-";
            const float ny = b.y + 34.f;
            ctx.r.drawText({x, ny}, n, th.font.title, has ? c.text : c.textDisabled);
            ctx.r.drawText({x + 0.7f, ny}, n, th.font.title, has ? c.text : c.textDisabled);
            const float nw = ctx.r.measure(n, th.font.title).width;
            const std::string suite = has
                ? std::string(bench_->scans() > 1 ? "cycles" : "cycle") + "   ·   "
                  + seconds(bench_->elapsedMs()) + " simulées"
                : "ouvrez un bloc qui a un exemple, puis Essayer";
            ctx.r.drawText({x + nw + 10.f, ny + ctx.r.lineHeight(th.font.title)
                                           - ctx.r.lineHeight(th.font.caption) - 5.f},
                           suite, th.font.caption, c.textMuted);
        }

        // ---- a droite : fermer, et la periode ------------------------------------------
        float rx = b.right() - 20.f;
        {
            const std::string label = "Fermer l'essai";
            const float w = ctx.r.measure(label, th.font.ui).width + 16.f + 7.f + 28.f;
            const gfx::Rect r{rx - w, cy - 16.f, w, 32.f};
            zones_.push_back({r, 4, has, "Revient à l'aide. Le projet de poche est jeté."});
            if (has && hover_ == 4) ctx.r.fillRoundedRect(r, th.brand.hover, 16.f);
            ui::drawIcon(ctx.r, ui::Icon::Close, {r.x + 14.f, cy - 7.f, 14.f, 14.f},
                         has ? c.textMuted : c.textDisabled);
            ctx.r.drawText({r.x + 14.f + 14.f + 8.f, cy - ctx.r.lineHeight(th.font.ui) * 0.5f},
                           label, th.font.ui, has ? c.text : c.textDisabled);
            rx = r.x - 24.f;
        }
        {
            // LE SEGMENTE : quatre periodes, une seule allumee. Un curseur libre
            // proposerait 37 ms, que personne ne met dans un automate.
            const float segW = 46.f, segH = 30.f;
            const float total = segW * 4.f;
            const gfx::Rect box{rx - total, cy - segH * 0.5f + 8.f, total, segH};
            ctx.r.drawText({box.x, box.y - ctx.r.lineHeight(th.font.caption) - 4.f},
                           "Période du cycle (ms)", th.font.caption, c.textMuted);
            ctx.r.fillRoundedRect(box, c.border, 8.f);
            ctx.r.fillRoundedRect({box.x + 1.f, box.y + 1.f, box.w - 2.f, box.h - 2.f}, c.rowAltBg, 7.f);
            for (int i = 0; i < 4; ++i) {
                const gfx::Rect seg{box.x + segW * static_cast<float>(i), box.y, segW, segH};
                const int id = 100 + kPeriods[i];
                zones_.push_back({seg, id, has, "Un cycle toutes les " + std::to_string(kPeriods[i]) + " ms"});
                const bool on = has && bench_->periodMs() == kPeriods[i];
                if (on) {
                    ctx.r.fillRoundedRect({seg.x + 3.f, seg.y + 3.f, seg.w - 6.f, seg.h - 6.f},
                                          th.brand.card, 5.f);
                    ctx.r.fillRoundedRect({seg.x + 12.f, seg.bottom() - 6.f, seg.w - 24.f, 2.f},
                                          c.accent, 1.f);
                } else if (has && hover_ == id) {
                    ctx.r.fillRoundedRect({seg.x + 3.f, seg.y + 3.f, seg.w - 6.f, seg.h - 6.f},
                                          th.brand.hover, 5.f);
                }
                const auto txt = std::to_string(kPeriods[i]);
                const float tw = ctx.r.measure(txt, th.font.ui).width;
                const auto col = !has ? c.textDisabled : on ? c.accent : c.textMuted;
                const float ty = seg.y + (segH - ctx.r.lineHeight(th.font.ui)) * 0.5f - 1.f;
                ctx.r.drawText({seg.x + (segW - tw) * 0.5f, ty}, txt, th.font.ui, col);
                if (on) ctx.r.drawText({seg.x + (segW - tw) * 0.5f + 0.6f, ty}, txt, th.font.ui, col);
            }
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            const int h = hitAt(m->pos);
            if (h != hover_) {
                hover_ = h;
                const auto* z = zone(h);
                setTooltip(z != nullptr ? z->tip : std::string{});
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            if (!bounds().contains(d->pos) || d->button != ui::MouseButton::Left)
                return ui::EventResult::Ignored;
            pressed_ = hitAt(d->pos);
            invalidate();
            return pressed_ >= 0 ? ui::EventResult::Consumed : ui::EventResult::Ignored;
        }
        if (const auto* u = std::get_if<ui::MouseUp>(&ev)) {
            if (pressed_ < 0) return ui::EventResult::Ignored;
            const int was = pressed_;
            pressed_ = -1;
            invalidate();
            if (hitAt(u->pos) == was) fire(was);
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    struct Zone { gfx::Rect r; int id; bool enabled; std::string tip; };
    [[nodiscard]] const Zone* zone(int id) const {
        for (const auto& z : zones_) if (z.id == id) return &z;
        return nullptr;
    }
    [[nodiscard]] int hitAt(gfx::Point p) const {
        for (const auto& z : zones_) if (z.enabled && z.r.contains(p)) return z.id;
        return -1;
    }

    help::TryBench*   bench_{nullptr};
    std::vector<Zone> zones_;
    int               hover_{-1}, pressed_{-1};
};

// =============================================================================
//  LA TABLE D'ANIMATION
// =============================================================================
class BenchWatchList final : public ui::Widget {
public:
    explicit BenchWatchList(std::string id) : ui::Widget(std::move(id)) { setFocusPolicy(true); }

    const core::SignalPtr<const std::string&> toggleRequested  = core::Signal<const std::string&>::create();
    const core::SignalPtr<const std::string&> traceRequested   = core::Signal<const std::string&>::create();
    const core::SignalPtr<const std::string&> unforceRequested = core::Signal<const std::string&>::create();

    // `featured` : ce que l'exemple nomme et les broches du bloc. Elles passent
    // devant : sur les 1400 variables d'un bloc d'equipement, ce sont les dix
    // qu'on regarde.
    void setBench(help::TryBench* b, std::unordered_set<std::string> featured) {
        bench_ = b;
        featured_ = std::move(featured);
        flashAt_.clear();
        range_.clear();
        lastScans_ = ~0u;
        scrollY_ = 0.f;
        selected_.clear();
        rebuild();
    }
    void setFilter(std::string term) { filter_ = lower(term); scrollY_ = 0.f; rebuild(); }

    void rebuild() {
        lines_.clear();
        if (bench_ == nullptr) { invalidate(); return; }
        std::vector<int> mine, rest;
        const auto& rows = bench_->rows();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (!filter_.empty() && lower(rows[i].name).find(filter_) == std::string::npos) continue;
            (featured_.count(rows[i].name) ? mine : rest).push_back(static_cast<int>(i));
        }
        if (!mine.empty()) {
            lines_.push_back({-1, "DANS L'EXEMPLE", mine.size()});
            for (const int r : mine) lines_.push_back({r, {}, 0});
        }
        if (!rest.empty()) {
            lines_.push_back({-1, mine.empty() ? "VARIABLES" : "TOUT LE RESTE", rest.size()});
            for (const int r : rest) lines_.push_back({r, {}, 0});
        }
        invalidate();
    }

    [[nodiscard]] std::size_t visibleCount() const {
        std::size_t n = 0;
        for (const auto& l : lines_) n += l.row >= 0;
        return n;
    }
    [[nodiscard]] const std::string& selectedName() const noexcept { return selected_; }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c  = th.color;
        const auto  b  = bounds();
        ctx.r.fillRect(b, c.panelBg);

        // Le releve a change depuis la derniere image : on note l'heure de
        // chaque valeur qui a bouge, pour l'eclair.
        if (bench_ != nullptr && bench_->scans() != lastScans_) {
            lastScans_ = bench_->scans();
            for (const auto& r : bench_->rows()) {
                if (r.changed) flashAt_[r.name] = ctx.time;
                if (!r.boolean) {
                    auto& rg = range_[r.name];
                    rg.first = std::min(rg.first, r.number);
                    rg.second = std::max(rg.second, r.number);
                }
            }
        }

        const float headH = 28.f;
        rowH_ = 28.f;
        // ---- l'en-tete --------------------------------------------------------------
        ctx.r.fillRect({b.x, b.y, b.w, headH}, c.headerBg);
        ctx.r.line({b.x, b.y + headH - 0.5f}, {b.right(), b.y + headH - 0.5f}, c.border, 1.f);
        const auto cap = th.font.caption;
        const float capY = b.y + (headH - ctx.r.lineHeight(cap)) * 0.5f;
        colValueRight_ = b.right() - 118.f;
        ctx.r.drawText({b.x + 40.f, capY}, "Variable", cap, c.textMuted);
        ctx.r.drawText({colValueRight_ - ctx.r.measure("Valeur", cap).width, capY}, "Valeur", cap,
                       c.textMuted);
        ctx.r.drawText({b.right() - 104.f, capY}, "Type", cap, c.textMuted);
        ctx.r.drawText({b.right() - 50.f, capY}, "Courbe", cap, c.textMuted);

        body_ = {b.x, b.y + headH, b.w, std::max(0.f, b.h - headH)};
        if (bench_ == nullptr || lines_.empty()) {
            const char* msg = bench_ == nullptr ? "Aucun essai en cours."
                                                : "Aucune variable ne correspond au filtre.";
            const float tw = ctx.r.measure(msg, th.font.ui).width;
            ctx.r.drawText({b.x + (b.w - tw) * 0.5f, body_.y + 40.f}, msg, th.font.ui, c.textMuted);
            return;
        }

        const float maxScroll = std::max(0.f, static_cast<float>(lines_.size()) * rowH_ - body_.h);
        scrollY_ = std::clamp(scrollY_, 0.f, maxScroll);
        const auto first = static_cast<std::size_t>(scrollY_ / rowH_);
        const auto last = std::min(lines_.size(), first + static_cast<std::size_t>(body_.h / rowH_) + 2);
        const auto& rows = bench_->rows();

        ctx.r.pushClip(body_);
        for (std::size_t i = first; i < last; ++i) {
            const auto& line = lines_[i];
            const gfx::Rect r{b.x, body_.y + static_cast<float>(i) * rowH_ - scrollY_, b.w, rowH_};
            if (line.row < 0) {
                // Une tete de section : une legende et un trait.
                const std::string t = line.title + "  (" + std::to_string(line.count) + ")";
                const float ty = r.y + (rowH_ - ctx.r.lineHeight(cap)) * 0.5f + 2.f;
                ctx.r.drawText({r.x + 12.f, ty}, t, cap, c.accent);
                ctx.r.drawText({r.x + 12.6f, ty}, t, cap, c.accent);
                const float lx = r.x + 20.f + ctx.r.measure(t, cap).width;
                ctx.r.line({lx, r.y + rowH_ * 0.5f + 2.f}, {r.right() - 12.f, r.y + rowH_ * 0.5f + 2.f},
                           c.border, 1.f);
                continue;
            }
            if (static_cast<std::size_t>(line.row) >= rows.size()) continue;
            const auto& w = rows[static_cast<std::size_t>(line.row)];
            const bool sel = w.name == selected_;

            if (w.forced) {
                // FORCE : ambre, et une barre a gauche. C'est le seul geste qui
                // fausse ce qu'on regarde ; il ne doit jamais passer inapercu.
                ctx.r.fillRect(r, c.warning.withAlpha(th.isDark() ? 34 : 26));
                ctx.r.fillRect({r.x, r.y + 2.f, 3.f, rowH_ - 4.f}, c.warning);
            }
            if (sel) ctx.r.fillRoundedRect({r.x + 3.f, r.y + 1.f, r.w - 6.f, rowH_ - 2.f},
                                           c.selectionBg, 4.f);
            else if (hovered() && hover_ == static_cast<int>(i))
                ctx.r.fillRoundedRect({r.x + 3.f, r.y + 1.f, r.w - 6.f, rowH_ - 2.f}, th.brand.hover, 4.f);

            // Le temoin : une LED pour un booleen, une jauge pour un nombre.
            const float cy = r.y + rowH_ * 0.5f;
            if (w.boolean) {
                drawLed(ctx, {r.x + 20.f, cy}, 11.f, w.value == "TRUE");
            } else if (w.type != "STRING" && !w.type.empty()) {
                const auto rg = range_.count(w.name) ? range_[w.name] : std::pair<double, double>{0.0, 0.0};
                const double lo = std::min(0.0, rg.first), hi = std::max(rg.second, lo + 1e-9);
                const float f = static_cast<float>(std::clamp((w.number - lo) / (hi - lo), 0.0, 1.0));
                const gfx::Rect track{r.x + 9.f, cy - 3.f, 22.f, 6.f};
                ctx.r.fillRoundedRect(track, c.rowAltBg, 3.f);
                ctx.r.fillRoundedRect({track.x, track.y, std::max(3.f, track.w * f), track.h},
                                      c.accent.withAlpha(200), 3.f);
            }

            // Le nom : le prefixe attenue, la feuille en clair. "CarteDI_R0S4[5]."
            // se repete sur trente lignes ; ".Val" est ce qu'on lit.
            const float nx = r.x + 40.f;
            const float ty = r.y + (rowH_ - ctx.r.lineHeight(th.font.ui)) * 0.5f;
            const auto dot = w.name.find_last_of('.');
            const auto nameColour = sel ? c.selectionText : c.text;
            float maxName = colValueRight_ - 90.f - nx;
            if (dot != std::string::npos && dot + 1 < w.name.size()) {
                const std::string pre = w.name.substr(0, dot + 1);
                const std::string leaf = w.name.substr(dot + 1);
                const auto fitPre = ctx.r.fitCharacters(pre, th.font.ui, std::max(0.f, maxName * 0.6f));
                std::string shownPre = pre;
                if (fitPre < pre.size()) shownPre = "..." + pre.substr(pre.size() - std::min(pre.size(), fitPre > 3 ? fitPre - 3 : 0));
                ctx.r.drawText({nx, ty}, shownPre, th.font.ui, sel ? c.selectionText : c.textMuted);
                const float pw = ctx.r.measure(shownPre, th.font.ui).width;
                ctx.r.drawText({nx + pw, ty}, leaf, th.font.ui, nameColour);
            } else {
                ctx.r.drawText({nx, ty}, w.name, th.font.ui, nameColour);
            }

            // La valeur, a droite, avec l'eclair si elle vient de changer.
            const float flash = flashOf(ctx, flashAt_.count(w.name) ? flashAt_[w.name] : -1.0);
            const float vw = ctx.r.measure(w.value, th.font.ui).width;
            if (flash > 0.f) {
                ctx.r.fillRoundedRect({colValueRight_ - vw - 8.f, r.y + 4.f, vw + 16.f, rowH_ - 8.f},
                                      c.accent.withAlpha(static_cast<std::uint8_t>(90.f * flash)), 5.f);
                invalidate();
            }
            const auto valueColour = sel ? c.selectionText
                                   : w.forced ? th.onSurface(c.warning)
                                   : w.boolean && w.value == "TRUE" ? th.onSurface(c.ok) : c.text;
            ctx.r.drawText({colValueRight_ - vw, ty}, w.value, th.font.ui, valueColour);
            if (flash > 0.f) ctx.r.drawText({colValueRight_ - vw + 0.6f, ty}, w.value, th.font.ui, valueColour);

            // Le type, en etiquette.
            if (!w.type.empty()) {
                const float tw = ctx.r.measure(w.type, cap).width + 12.f;
                const float ph = std::min(rowH_ - 4.f, ctx.r.lineHeight(cap) + 4.f);
                const gfx::Rect pill{b.right() - 104.f, cy - ph * 0.5f, tw, ph};
                ctx.r.fillRoundedRect(pill, c.border, ph * 0.5f);
                ctx.r.fillRoundedRect({pill.x + 1.f, pill.y + 1.f, pill.w - 2.f, pill.h - 2.f},
                                      c.rowAltBg, ph * 0.5f - 1.f);
                ctx.r.drawText({pill.x + 6.f, pill.y + (ph - ctx.r.lineHeight(cap)) * 0.5f}, w.type,
                               cap, c.textMuted);
            }

            // Le cadenas (clic : liberer) et la courbe (clic : tracer).
            if (w.forced) drawLock(ctx, lockRect(r), th.onSurface(c.warning));
            const bool traced = bench_->inTrend(w.name);
            const auto tr = traceRect(r);
            if (traced) ctx.r.fillRoundedRect(tr, c.accent.withAlpha(th.isDark() ? 60 : 40), 5.f);
            ui::drawIcon(ctx.r, ui::Icon::Chart, {tr.x + 4.f, tr.y + 4.f, tr.w - 8.f, tr.h - 8.f},
                         traced ? c.accent : (hover_ == static_cast<int>(i) ? c.textMuted
                                                                           : c.border));
        }
        ctx.r.popClip();

        // La barre de defilement.
        if (maxScroll > 0.f) {
            const float frac = body_.h / (body_.h + maxScroll);
            const float len = std::max(28.f, body_.h * frac);
            const float y = body_.y + (body_.h - len) * (scrollY_ / maxScroll);
            ctx.r.fillRoundedRect({b.right() - 7.f, y, 5.f, len}, c.scrollbar, 2.5f);
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            const int h = bounds().contains(m->pos) ? lineAt(m->pos.y) : -1;
            if (h != hover_) { hover_ = h; invalidate(); }
            return ui::EventResult::Ignored;
        }
        if (const auto* wh = std::get_if<ui::MouseWheel>(&ev)) {
            if (!bounds().contains(wh->pos)) return ui::EventResult::Ignored;
            scrollY_ = std::max(0.f, scrollY_ - wh->dy * rowH_ * 3.f);
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            if (!bounds().contains(d->pos) || d->button != ui::MouseButton::Left)
                return ui::EventResult::Ignored;
            grabFocus();
            const int li = lineAt(d->pos.y);
            if (li < 0 || lines_[static_cast<std::size_t>(li)].row < 0 || bench_ == nullptr)
                return ui::EventResult::Consumed;
            const auto& w = bench_->rows()[static_cast<std::size_t>(lines_[static_cast<std::size_t>(li)].row)];
            const std::string name = w.name;        // copie : un cycle refait la table
            const gfx::Rect r{bounds().x, body_.y + static_cast<float>(li) * rowH_ - scrollY_,
                              bounds().w, rowH_};
            selected_ = name;
            invalidate();
            if (traceRect(r).contains(d->pos))              { traceRequested->emit(name); }
            else if (w.forced && lockRect(r).contains(d->pos)) { unforceRequested->emit(name); }
            else if (w.boolean && (d->pos.x < r.x + 34.f || d->clickCount >= 2))
                toggleRequested->emit(name);
            return ui::EventResult::Consumed;
        }
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused() && bench_ != nullptr) {
            if (k->key == ui::Key::Down || k->key == ui::Key::Up) {
                moveSelection(k->key == ui::Key::Down ? 1 : -1);
                return ui::EventResult::Consumed;
            }
            if ((k->key == ui::Key::Space || k->key == ui::Key::Return) && !selected_.empty()) {
                const auto* w = findRow(bench_, selected_);
                if (w != nullptr && w->boolean) toggleRequested->emit(std::string(selected_));
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

private:
    struct Line { int row; std::string title; std::size_t count; };

    [[nodiscard]] int lineAt(float y) const {
        if (y < body_.y) return -1;
        const auto i = static_cast<std::size_t>((y - body_.y + scrollY_) / rowH_);
        return i < lines_.size() ? static_cast<int>(i) : -1;
    }
    [[nodiscard]] gfx::Rect traceRect(const gfx::Rect& r) const {
        return {r.right() - 44.f, r.y + 3.f, 22.f, rowH_ - 6.f};
    }
    [[nodiscard]] gfx::Rect lockRect(const gfx::Rect& r) const {
        return {colValueRight_ + 4.f, r.y + 3.f, 18.f, rowH_ - 6.f};
    }
    void moveSelection(int delta) {
        std::vector<std::size_t> selectable;
        std::size_t at = 0;
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            if (lines_[i].row < 0) continue;
            if (bench_->rows()[static_cast<std::size_t>(lines_[i].row)].name == selected_)
                at = selectable.size();
            selectable.push_back(i);
        }
        if (selectable.empty()) return;
        std::size_t next = selected_.empty() ? 0
            : static_cast<std::size_t>(std::clamp<long>(static_cast<long>(at) + delta, 0,
                                                        static_cast<long>(selectable.size()) - 1));
        const auto li = selectable[next];
        selected_ = bench_->rows()[static_cast<std::size_t>(lines_[li].row)].name;
        const float y = static_cast<float>(li) * rowH_;
        if (y < scrollY_) scrollY_ = y;
        if (y + rowH_ > scrollY_ + body_.h) scrollY_ = y + rowH_ - body_.h;
        invalidate();
    }

    help::TryBench*                  bench_{nullptr};
    std::unordered_set<std::string>  featured_;
    std::string                      filter_;
    std::vector<Line>                lines_;
    std::unordered_map<std::string, double> flashAt_;
    std::unordered_map<std::string, std::pair<double, double>> range_;
    std::uint32_t                    lastScans_{~0u};
    std::string                      selected_;
    int                              hover_{-1};
    float                            scrollY_{0.f};
    float                            rowH_{28.f};
    float                            colValueRight_{0.f};
    gfx::Rect                        body_{};
};

// =============================================================================
//  LE CHRONOGRAMME
// =============================================================================
class BenchChronogram final : public ui::Widget {
public:
    explicit BenchChronogram(std::string id) : ui::Widget(std::move(id)) {}
    void setBench(help::TryBench* b) { bench_ = b; invalidate(); }

    const core::SignalPtr<const std::string&> removeRequested = core::Signal<const std::string&>::create();

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c  = th.color;
        const auto  b  = bounds();
        ctx.r.fillRect(b, th.brand.codeBg);
        ctx.r.line({b.x, b.y + 0.5f}, {b.right(), b.y + 0.5f}, c.border, 1.f);

        const auto cap = th.font.caption;
        const float capH = ctx.r.lineHeight(cap);
        ctx.r.drawText({b.x + 14.f, b.y + 8.f}, "CHRONOGRAMME", cap, c.textMuted);
        ctx.r.drawText({b.x + 14.6f, b.y + 8.f}, "CHRONOGRAMME", cap, c.textMuted);

        lanes_.clear();
        const auto names = bench_ != nullptr ? bench_->trendNames() : std::vector<std::string>{};
        if (names.empty()) {
            const float icon = 28.f;
            const std::string msg = bench_ == nullptr
                ? "Le chronogramme s'anime quand un essai tourne."
                : "Aucun signal tracé : cliquez l'icône courbe d'une variable dans la table.";
            const float tw = ctx.r.measure(msg, th.font.ui).width;
            const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
            ui::drawIcon(ctx.r, ui::Icon::Chart, {cx - icon * 0.5f, cy - icon - 6.f, icon, icon},
                         c.textDisabled);
            ctx.r.drawText({cx - tw * 0.5f, cy + 6.f}, msg, th.font.ui, c.textMuted);
            return;
        }

        constexpr float kLabelW = 230.f, kPx = 6.f;
        plot_ = {b.x + kLabelW, b.y + 28.f, std::max(10.f, b.w - kLabelW - 16.f),
                 std::max(10.f, b.h - 36.f)};
        const auto latest = static_cast<std::int64_t>(bench_->scans());
        const auto window = static_cast<std::int64_t>(plot_.w / kPx);
        const auto firstCycle = std::max<std::int64_t>(0, latest - window);
        // Tant que la fenetre n'est pas pleine, la courbe part de la GAUCHE :
        // les cycles a venir se remplissent vers la droite, comme sur un
        // enregistreur. Pleine, elle defile.
        const bool full = latest - firstCycle >= window;
        const auto xOf = [&](std::int64_t scan) {
            return full ? plot_.right() - static_cast<float>(latest - scan) * kPx
                        : plot_.x + static_cast<float>(scan - firstCycle) * kPx;
        };

        // ---- la grille : une ligne par dizaine de cycles, plus forte a cinquante --
        for (std::int64_t cyc = (firstCycle / 10) * 10; cyc <= latest; cyc += 10) {
            if (cyc < firstCycle) continue;
            const float x = xOf(cyc);
            const bool major = cyc % 50 == 0;
            ctx.r.line({x, plot_.y}, {x, plot_.bottom()},
                       major ? c.border : c.gridLine, 1.f);
            if (major) {
                const auto t = std::to_string(cyc);
                ctx.r.drawText({x - ctx.r.measure(t, cap).width * 0.5f, b.y + 8.f}, t, cap, c.textMuted);
            }
        }

        // ---- les couloirs ------------------------------------------------------------
        float y = plot_.y;
        const float gap = 6.f;
        int cursorCycle = -1;
        if (hovered() && plot_.contains(mouse_)) {
            const auto at = full
                ? latest - static_cast<std::int64_t>((plot_.right() - mouse_.x) / kPx + 0.5f)
                : firstCycle + static_cast<std::int64_t>((mouse_.x - plot_.x) / kPx + 0.5f);
            if (at >= firstCycle && at <= latest) cursorCycle = static_cast<int>(at);
        }

        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto& name = names[i];
            const auto* row = findRow(bench_, name);
            const bool boolean = row != nullptr && row->boolean;
            const float laneH = boolean ? 34.f : 60.f;
            if (y + laneH > plot_.bottom() + 1.f) break;
            const gfx::Rect lane{plot_.x, y, plot_.w, laneH};
            const auto colour = th.tone(seriesTone(i), c.accent);
            lanes_.push_back({name, {b.x, y, kLabelW - 8.f, laneH}});

            // La legende : pastille, nom, valeur (au curseur s'il y en a un).
            const auto& serie = bench_->trend(name);
            double shown = row != nullptr ? row->number : 0.0;
            std::string shownText = row != nullptr ? row->value : "-";
            if (cursorCycle >= 0) {
                for (const auto& s : serie)
                    if (static_cast<int>(s.scan) <= cursorCycle) { shown = s.value; }
                char buf[48];
                if (boolean) shownText = shown > 0.5 ? "TRUE" : "FALSE";
                else { std::snprintf(buf, sizeof buf, "%g", shown); shownText = buf; }
            }
            ctx.r.fillRoundedRect({b.x + 14.f, y + laneH * 0.5f - 5.f, 10.f, 10.f}, colour, 3.f);
            // Le nom : la feuille en clair, le prefixe attenue - et c'est le
            // prefixe qu'on raccourcit quand la place manque, jamais la feuille.
            {
                const float nameW = kLabelW - 30.f - 84.f;
                const auto dot = name.find_last_of('.');
                const std::string leaf = dot == std::string::npos ? name : name.substr(dot + 1);
                std::string pre = dot == std::string::npos ? std::string{} : name.substr(0, dot + 1);
                const float leafW = ctx.r.measure(leaf, cap).width;
                const float room = nameW - leafW;
                if (!pre.empty() && ctx.r.measure(pre, cap).width > room) {
                    const auto keep = ctx.r.fitCharacters(pre, cap, std::max(0.f, room - ctx.r.measure("...", cap).width));
                    pre = room > 12.f ? pre.substr(0, keep) + "..." : std::string{};
                }
                const float ly = y + laneH * 0.5f - capH * 0.5f;
                ctx.r.drawText({b.x + 30.f, ly}, pre, cap, c.textMuted);
                ctx.r.drawText({b.x + 30.f + ctx.r.measure(pre, cap).width, ly}, leaf, cap, c.text);
            }
            const float vw = ctx.r.measure(shownText, cap).width;
            const auto valueColour = th.onSurface(colour);
            ctx.r.drawText({b.x + kLabelW - 28.f - vw, y + laneH * 0.5f - capH * 0.5f}, shownText, cap,
                           valueColour);
            ctx.r.drawText({b.x + kLabelW - 27.4f - vw, y + laneH * 0.5f - capH * 0.5f}, shownText, cap,
                           valueColour);
            // La croix qui retire le signal.
            const gfx::Rect cross{b.x + kLabelW - 22.f, y + laneH * 0.5f - 7.f, 14.f, 14.f};
            ui::drawIcon(ctx.r, ui::Icon::Close, {cross.x + 2.f, cross.y + 2.f, 10.f, 10.f},
                         hoverCross_ == static_cast<int>(i) ? c.text : c.textDisabled);

            ctx.r.fillRect(lane, c.panelBg.withAlpha(th.isDark() ? 60 : 140));
            ctx.r.pushClip(lane);
            if (!serie.empty()) {
                double lo = 0.0, hi = 1.0;
                if (!boolean) {
                    lo = hi = serie.back().value;
                    for (const auto& s : serie)
                        if (static_cast<std::int64_t>(s.scan) >= firstCycle) {
                            lo = std::min(lo, s.value);
                            hi = std::max(hi, s.value);
                        }
                    if (hi - lo < 1e-9) { lo -= 1.0; hi += 1.0; }
                }
                const float top = lane.y + 6.f, bottom = lane.bottom() - 6.f;
                const auto yOf = [&](double v) {
                    return bottom - static_cast<float>((v - lo) / (hi - lo)) * (bottom - top);
                };
                // Un palier par echantillon, jusqu'au suivant : une valeur
                // d'automate tient tout le cycle, puis saute.
                for (std::size_t k = 0; k < serie.size(); ++k) {
                    const auto scan = static_cast<std::int64_t>(serie[k].scan);
                    const std::int64_t next = k + 1 < serie.size()
                        ? static_cast<std::int64_t>(serie[k + 1].scan) : latest + 1;
                    if (next < firstCycle) continue;
                    const float x0 = std::max(lane.x, xOf(scan)), x1 = std::min(lane.right(), xOf(next));
                    const float yy = yOf(serie[k].value);
                    if (boolean && serie[k].value > 0.5)
                        ctx.r.fillRect({x0, top, std::max(0.f, x1 - x0), bottom - top},
                                       colour.withAlpha(th.isDark() ? 60 : 45));
                    ctx.r.line({x0, yy}, {x1, yy}, colour, 2.f);
                    if (k + 1 < serie.size()) {
                        const float y2 = yOf(serie[k + 1].value);
                        if (std::abs(y2 - yy) > 0.5f) ctx.r.line({x1, yy}, {x1, y2}, colour, 2.f);
                    }
                }
                if (!boolean) {
                    char buf[48];
                    std::snprintf(buf, sizeof buf, "%g", hi);
                    ctx.r.drawText({lane.x + 4.f, lane.y + 1.f}, buf, cap, c.textDisabled);
                    std::snprintf(buf, sizeof buf, "%g", lo);
                    ctx.r.drawText({lane.x + 4.f, lane.bottom() - capH - 1.f}, buf, cap, c.textDisabled);
                }
            }
            ctx.r.popClip();
            y += laneH + gap;
        }

        // ---- le curseur : une ligne, et le numero du cycle ----------------------------
        if (cursorCycle >= 0) {
            const float x = xOf(cursorCycle);
            ctx.r.line({x, plot_.y}, {x, plot_.bottom()}, c.text.withAlpha(150), 1.f);
            const auto t = "cycle " + std::to_string(cursorCycle);
            const float tw = ctx.r.measure(t, cap).width + 12.f;
            const gfx::Rect pill{std::min(x + 6.f, plot_.right() - tw), b.y + 4.f, tw, capH + 6.f};
            ctx.r.fillRoundedRect(pill, th.brand.tooltipBg, 5.f);
            ctx.r.drawText({pill.x + 6.f, pill.y + 3.f}, t, cap, th.brand.tooltipText);
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            mouse_ = m->pos;
            int cross = -1;
            for (std::size_t i = 0; i < lanes_.size(); ++i) {
                const auto& l = lanes_[i].label;
                const gfx::Rect r{l.right() - 16.f, l.y + l.h * 0.5f - 9.f, 18.f, 18.f};
                if (r.contains(m->pos)) cross = static_cast<int>(i);
            }
            hoverCross_ = cross;
            invalidate();                  // le curseur suit la souris
            return ui::EventResult::Ignored;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            if (!bounds().contains(d->pos) || d->button != ui::MouseButton::Left)
                return ui::EventResult::Ignored;
            for (const auto& l : lanes_) {
                const gfx::Rect r{l.label.right() - 16.f, l.label.y + l.label.h * 0.5f - 9.f, 18.f, 18.f};
                if (r.contains(d->pos)) {
                    const std::string name = l.name;
                    removeRequested->emit(name);
                    return ui::EventResult::Consumed;
                }
            }
        }
        return ui::EventResult::Ignored;
    }

private:
    struct Lane { std::string name; gfx::Rect label; };
    help::TryBench*   bench_{nullptr};
    std::vector<Lane> lanes_;
    gfx::Rect         plot_{};
    gfx::Point        mouse_{};
    int               hoverCross_{-1};
};

// =============================================================================
//  LE BLOC, EN FBD
// =============================================================================
class BenchFbd final : public ui::Widget {
public:
    explicit BenchFbd(std::string id) : ui::Widget(std::move(id)) {}

    const core::SignalPtr<const std::string&> toggleRequested = core::Signal<const std::string&>::create();

    struct Pin {
        std::string name;
        std::string type;
        std::string argument;     // ce qui est branche, lu dans l'appel
        std::string comment;
        int         side{0};      // 0 entree, 1 sortie, 2 entree-sortie
    };

    void setup(help::TryBench* b, const project::CatalogEntry* entry, std::string instance,
               const std::vector<help::CallArgument>& args) {
        bench_ = b;
        entry_ = entry;
        instance_ = std::move(instance);
        pins_.clear();
        if (entry_ != nullptr && !instance_.empty()) {
            for (const auto& d : entry_->declarations) {
                const auto s = lower(d.scope);
                const int side = s == "input" ? 0 : s == "output" ? 1 : s == "inout" ? 2 : -1;
                if (side < 0) continue;
                Pin p{d.name, d.type, {}, d.comment, side};
                for (const auto& a : args)
                    if (lower(a.param) == lower(d.name)) p.argument = a.argument;
                pins_.push_back(std::move(p));
            }
        }
        scrollY_ = 0.f;
        invalidate();
    }
    [[nodiscard]] const std::string& instance() const noexcept { return instance_; }
    [[nodiscard]] const std::vector<Pin>& pins() const noexcept { return pins_; }

    // Ce qui s'affiche au bout du fil : l'argument de l'appel, ou, pour une
    // entree que l'exemple ne branche pas, le dire. Une sortie non branchee
    // n'affiche rien : c'est le cas courant, pas un oubli.
    [[nodiscard]] static std::string argumentText(const Pin& p) {
        if (!p.argument.empty()) return p.argument;
        return p.side == 1 ? std::string{} : std::string("(non branché)");
    }

    // Le meme chemin que le clic : une entree booleenne se bascule.
    bool clickPin(const std::string& pin) {
        for (const auto& p : pins_) {
            if (p.name != pin || p.side == 1) continue;
            const auto full = instance_ + "." + p.name;
            const auto* row = findRow(bench_, full);
            if (row == nullptr || !row->boolean) return false;
            toggleRequested->emit(full);
            return true;
        }
        return false;
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c  = th.color;
        const auto  b  = bounds();
        ctx.r.fillRect(b, c.panelBg);
        ctx.r.line({b.x + 0.5f, b.y}, {b.x + 0.5f, b.bottom()}, c.border, 1.f);
        const auto cap = th.font.caption;
        const float capH = ctx.r.lineHeight(cap);
        ctx.r.drawText({b.x + 14.f, b.y + 10.f}, "LE BLOC", cap, c.textMuted);
        ctx.r.drawText({b.x + 14.6f, b.y + 10.f}, "LE BLOC", cap, c.textMuted);
        hits_.clear();

        if (bench_ == nullptr || entry_ == nullptr || instance_.empty() || pins_.empty()) {
            // L'etat vide : il dit pourquoi il n'y a rien, et ou regarder.
            const std::string msg = bench_ == nullptr ? "Le bloc se dessine quand un essai tourne."
                : entry_ != nullptr && entry_->kind == project::CatalogKind::DerivedType
                    ? "Un type n'a pas de broches : ses membres sont dans la table."
                    : "L'exemple n'appelle pas ce bloc : rien à dessiner.";
            const float icon = 40.f, cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.45f;
            ui::drawIcon(ctx.r, entry_ != nullptr && entry_->kind == project::CatalogKind::DerivedType
                                    ? ui::Icon::DerivedType : ui::Icon::FunctionBlock,
                         {cx - icon * 0.5f, cy - icon - 8.f, icon, icon}, c.textDisabled);
            const float tw = ctx.r.measure(msg, th.font.ui).width;
            ctx.r.drawText({cx - tw * 0.5f, cy + 4.f}, msg, th.font.ui, c.textMuted);
            return;
        }

        std::vector<const Pin*> left, right;
        for (const auto& p : pins_) (p.side == 1 ? right : left).push_back(&p);

        const float pinH = 26.f, headH = 36.f, margin = 12.f, gap = 8.f;
        const std::size_t rowsN = std::max(left.size(), right.size());
        float labelW = 60.f;
        for (const auto& p : pins_) labelW = std::max(labelW, ctx.r.measure(p.name, cap).width);
        // Ce qui est branche, de chaque cote, decide ou poser le bloc. Centre
        // sur le panneau, il laissait 80 pixels a gauche pour "CarteDI_R0S4"
        // et, a droite, le vide des sorties que l'exemple ne branche pas.
        float argL = 0.f, argR = 0.f;
        for (const auto* p : left)  argL = std::max(argL, ctx.r.measure(argumentText(*p), th.font.ui).width);
        for (const auto* p : right) argR = std::max(argR, ctx.r.measure(argumentText(*p), th.font.ui).width);
        const float sideL = argL > 0.f ? std::min(argL, 200.f) + gap : 0.f;
        const float sideR = argR > 0.f ? std::min(argR, 200.f) + gap : 0.f;
        const float avail = std::max(0.f, b.w - 2.f * margin);
        const float blockW = std::clamp(labelW * 2.f + 48.f, 180.f, std::max(180.f, avail - sideL - sideR - 80.f));
        const float blockH = headH + static_cast<float>(rowsN) * pinH + 10.f;
        const float wire = std::clamp((avail - sideL - sideR - blockW) * 0.5f, 40.f, 90.f);
        const float total = sideL + wire + blockW + wire + sideR;
        // Trop etroit : ce sont les textes qui cedent, chacun en proportion, et
        // le bloc reste entier a l'ecran.
        const float over = std::max(0.f, total - avail);
        const float cutL = sideL + sideR > 0.f ? over * sideL / (sideL + sideR) : 0.f;
        const float bx = b.x + margin + std::max(0.f, (avail - total) * 0.5f)
                       + std::max(0.f, sideL - cutL) + wire;
        const float maxScroll = std::max(0.f, blockH + 90.f - b.h);
        scrollY_ = std::clamp(scrollY_, 0.f, maxScroll);
        const float by = b.y + 52.f - scrollY_;
        const gfx::Rect block{bx, by, blockW, blockH};

        ctx.r.pushClip({b.x, b.y + 28.f, b.w, b.h - 28.f});

        // Le nom de l'instance au-dessus, comme dans l'editeur FBD.
        {
            const float iw = ctx.r.measure(instance_, th.font.ui).width;
            ctx.r.drawText({bx + (blockW - iw) * 0.5f, by - ctx.r.lineHeight(th.font.ui) - 4.f},
                           instance_, th.font.ui, c.text);
            ctx.r.drawText({bx + (blockW - iw) * 0.5f + 0.6f, by - ctx.r.lineHeight(th.font.ui) - 4.f},
                           instance_, th.font.ui, c.text);
        }
        // Le bloc : ombre, bordure, surface, et le bandeau du type.
        ctx.r.fillRoundedRect({block.x, block.y + 3.f, block.w, block.h}, th.brand.cardShadow, 8.f);
        ctx.r.fillRoundedRect(block, c.borderStrong, 8.f);
        ctx.r.fillRoundedRect({block.x + 1.5f, block.y + 1.5f, block.w - 3.f, block.h - 3.f},
                              th.brand.card, 6.5f);
        const auto famille = th.tone(familyTone_, c.accent);
        ctx.r.fillRoundedRect({block.x + 1.5f, block.y + 1.5f, block.w - 3.f, headH - 4.f},
                              famille.withAlpha(th.isDark() ? 50 : 34), 6.5f);
        {
            const auto& tn = entry_->name;
            const float tw = ctx.r.measure(tn, cap).width;
            const float ty = block.y + (headH - capH) * 0.5f - 1.f;
            ctx.r.drawText({bx + (blockW - tw) * 0.5f, ty}, tn, cap, th.onSurface(famille));
            ctx.r.drawText({bx + (blockW - tw) * 0.5f + 0.5f, ty}, tn, cap, th.onSurface(famille));
        }

        const auto drawPin = [&](const Pin& p, std::size_t index, bool onRight) {
            const float cy = block.y + headH + (static_cast<float>(index) + 0.5f) * pinH;
            const std::string full = instance_ + "." + p.name;
            const auto* row = findRow(bench_, full);
            const bool isBool = row != nullptr && row->boolean;
            const bool on = isBool && row->value == "TRUE";
            const float edge = onRight ? block.right() : block.x;
            const float far = onRight ? edge + wire : edge - wire;
            const auto wireColour = on ? th.brand.led : p.side == 2 ? th.brand.scopeInOut
                                  : isBool ? c.borderStrong : c.textMuted;

            // Le fil, puis son bout : un rond cote bloc, rien cote variable.
            ctx.r.line({far, cy}, {edge, cy}, wireColour, on ? 2.5f : 1.5f);
            ctx.r.fillRoundedRect({edge - 3.5f, cy - 3.5f, 7.f, 7.f}, wireColour, 3.5f);

            // L'etiquette de la broche, dans le bloc.
            const float lw = ctx.r.measure(p.name, cap).width;
            const float lx = onRight ? block.right() - 10.f - lw : block.x + 10.f;
            ctx.r.drawText({lx, cy - capH * 0.5f}, p.name, cap,
                           p.side == 2 ? th.onSurface(th.brand.scopeInOut) : c.text);

            // Ce qui est branche, au bout du fil, coupe s'il deborde du panneau.
            const float room = onRight ? b.right() - margin - (far + gap) : far - gap - (b.x + margin);
            const std::string arg = fitText(ctx.r, argumentText(p), th.font.ui, room);
            if (!arg.empty()) {
                const float aw = ctx.r.measure(arg, th.font.ui).width;
                const float ax = onRight ? far + gap : far - gap - aw;
                ctx.r.drawText({ax, cy - ctx.r.lineHeight(th.font.ui) * 0.5f}, arg, th.font.ui,
                               p.argument.empty() ? c.textDisabled : c.text);
            }
            // Ce qui y passe : une LED sur un fil booleen, une valeur sinon.
            const float mid = (far + edge) * 0.5f;
            if (isBool) {
                drawLed(ctx, {mid, cy}, 9.f, on);
            } else if (row != nullptr) {
                const float vw = ctx.r.measure(row->value, cap).width + 12.f;
                const gfx::Rect pill{mid - vw * 0.5f, cy - capH * 0.5f - 3.f, vw, capH + 6.f};
                ctx.r.fillRoundedRect(pill, th.brand.card, (capH + 6.f) * 0.5f);
                ctx.r.fillRoundedRect(pill, c.accent.withAlpha(th.isDark() ? 50 : 30), (capH + 6.f) * 0.5f);
                ctx.r.drawText({pill.x + 6.f, pill.y + 3.f}, row->value, cap, th.onSurface(c.accent));
            }
            const gfx::Rect hit{std::min(far, edge) - (onRight ? 0.f : 60.f), cy - pinH * 0.5f,
                                wire + 60.f + lw + 12.f, pinH};
            if (!onRight && isBool) hits_.push_back({hit, p.name});
            if (hoverPin_ == p.name && !onRight && isBool)
                ctx.r.fillRoundedRect(hit, th.brand.hover, 6.f);
        };
        for (std::size_t i = 0; i < left.size(); ++i) drawPin(*left[i], i, false);
        for (std::size_t i = 0; i < right.size(); ++i) drawPin(*right[i], i, true);

        // Sous le bloc : ce que l'exemple a suppose pour tourner. Une variable
        // declaree en BOOL faute de mieux se dit, sinon on croit l'exemple.
        float ny = block.bottom() + 22.f;
        if (bench_ != nullptr) {
            for (const auto& n : bench_->notes()) {
                ctx.r.drawText({b.x + 16.f, ny}, n, cap, th.onSurface(c.warning));
                ny += capH + 4.f;
            }
            if (!bench_->declared().empty()) {
                ctx.r.drawText({b.x + 16.f, ny}, "Déclaré pour l'exemple :", cap, c.textMuted);
                ny += capH + 4.f;
                for (const auto& d : bench_->declared()) {
                    ctx.r.drawText({b.x + 28.f, ny}, d, cap, c.textMuted);
                    ny += capH + 3.f;
                }
            }
        }
        ctx.r.popClip();
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            std::string h;
            for (const auto& z : hits_) if (z.r.contains(m->pos)) h = z.pin;
            if (h != hoverPin_) {
                hoverPin_ = h;
                std::string tip;
                for (const auto& p : pins_)
                    if (p.name == h) tip = p.name + " : " + p.type
                                         + (p.comment.empty() ? "" : " - " + p.comment)
                                         + "  (clic : basculer)";
                setTooltip(tip);
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* wh = std::get_if<ui::MouseWheel>(&ev)) {
            if (!bounds().contains(wh->pos)) return ui::EventResult::Ignored;
            scrollY_ = std::max(0.f, scrollY_ - wh->dy * 40.f);
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            if (!bounds().contains(d->pos) || d->button != ui::MouseButton::Left)
                return ui::EventResult::Ignored;
            for (const auto& z : hits_)
                if (z.r.contains(d->pos)) { const std::string pin = z.pin; clickPin(pin); return ui::EventResult::Consumed; }
        }
        return ui::EventResult::Ignored;
    }

public:
    ui::Tone familyTone_{ui::Tone::Accent};

private:
    struct Hit { gfx::Rect r; std::string pin; };
    help::TryBench*              bench_{nullptr};
    const project::CatalogEntry* entry_{nullptr};
    std::string                  instance_;
    std::vector<Pin>             pins_;
    std::vector<Hit>             hits_;
    std::string                  hoverPin_;
    float                        scrollY_{0.f};
};

// =============================================================================
//  LE PANNEAU
// =============================================================================
namespace {
int familyOf(std::string_view category) {
    // La meme regle que LibraryHelpModels::familyIndex, sans en dependre : ce
    // fichier ne doit rien savoir des modeles de l'arbre.
    std::string l = lower(category);
    if (l.find("alarm") != std::string::npos) return 0;
    if (l.find("control") != std::string::npos) return 1;
    if (l.find("equip") != std::string::npos) return 2;
    if (l == "io" || l.rfind("io", 0) == 0) return 3;
    if (l.find("macro") != std::string::npos) return 4;
    return 5;
}
} // namespace

BenchPane::BenchPane(std::string id) : ui::DockLayout(std::move(id)) {
    auto transport = std::make_unique<BenchTransport>(this->id() + ".transport");
    transport_ = transport.get();
    links_ += transport_->command->connect([this](int cmd) {
        if (cmd == 4) { closed->emit(); return; }
        if (!bench_) return;
        if (cmd == 1) bench_->state() == State::Running ? bench_->pause() : bench_->play();
        else if (cmd == 2) bench_->stop();
        else if (cmd == 3) bench_->step();
        else if (cmd >= 100) bench_->setPeriodMs(cmd - 100);
        refresh();
    });
    dock(std::move(transport), Side::Top, 76.f);

    auto status = std::make_unique<ui::StatusBar>(this->id() + ".status");
    status_ = status.get();
    dock(std::move(status), Side::Bottom, 26.f);

    auto chrono = std::make_unique<BenchChronogram>(this->id() + ".chrono");
    chrono_ = chrono.get();
    links_ += chrono_->removeRequested->connect([this](const std::string& name) {
        if (bench_) bench_->removeFromTrend(name);
        refresh();
    });
    dock(std::move(chrono), Side::Bottom, 214.f);

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, this->id() + ".split");
    {
        auto left = std::make_unique<ui::DockLayout>(this->id() + ".left");
        auto filter = std::make_unique<ui::InputText>(this->id() + ".filter");
        filter->setPlaceholder("filtrer les variables ; « Nom = valeur » dans la barre du haut force");
        auto* f = filter.get();
        links_ += f->textChanged->connect([this](const std::string& t) {
            if (watch_) watch_->setFilter(t);
        });
        left->dock(std::move(filter), Side::Top, 34.f);

        auto watch = std::make_unique<BenchWatchList>(this->id() + ".watch");
        watch_ = watch.get();
        links_ += watch_->toggleRequested->connect([this](const std::string& name) {
            if (!bench_) return;
            if (!bench_->toggle(name)) message->emit(name + " n'est pas un booléen : rien à basculer", true);
            else message->emit(name + " basculé, un cycle a tourné", false);
            refresh();
        });
        links_ += watch_->traceRequested->connect([this](const std::string& name) {
            if (!bench_) return;
            if (bench_->inTrend(name)) bench_->removeFromTrend(name);
            else if (bench_->trendNames().size() >= 4)
                message->emit("quatre signaux au plus : retirez-en un du chronogramme", true);
            else bench_->addToTrend(name);
            refresh();
        });
        links_ += watch_->unforceRequested->connect([this](const std::string& name) {
            if (!bench_) return;
            (void)bench_->unforce(name);
            message->emit(name + " n'est plus forcé", false);
            refresh();
        });
        left->dock(std::move(watch), Side::Center, 0.f);
        split->addPane(std::move(left), 0.56f, 280.f);
    }
    {
        auto fbd = std::make_unique<BenchFbd>(this->id() + ".fbd");
        fbd_ = fbd.get();
        links_ += fbd_->toggleRequested->connect([this](const std::string& name) {
            if (!bench_) return;
            if (bench_->toggle(name)) message->emit(name + " basculé, un cycle a tourné", false);
            refresh();
        });
        split->addPane(std::move(fbd), 0.44f, 260.f);
    }
    dock(std::move(split), Side::Center, 0.f);
    refresh();
}

BenchPane::~BenchPane() = default;

void BenchPane::setBench(std::shared_ptr<help::TryBench> bench, const project::CatalogEntry* entry) {
    bench_ = std::move(bench);
    entry_ = bench_ ? entry : nullptr;

    std::string instance;
    std::vector<help::CallArgument> args;
    std::unordered_set<std::string> featured;
    if (bench_ && entry_ != nullptr) {
        instance = help::instanceOf(bench_->declared(), entry_->name);
        args = help::callArguments(bench_->example(), instance);
        // Ce que l'exemple nomme tel quel, et les broches du bloc.
        const auto& ex = bench_->example();
        for (const auto& r : bench_->rows()) {
            const auto at = ex.find(r.name);
            if (at != std::string::npos) {
                const auto end = at + r.name.size();
                const bool whole = end >= ex.size()
                    || !(std::isalnum(static_cast<unsigned char>(ex[end])) || ex[end] == '_'
                         || ex[end] == '[' || ex[end] == '.');
                if (whole) featured.insert(r.name);
            }
        }
        if (!instance.empty())
            for (const auto& d : entry_->declarations)
                if (!d.isLocal() && !d.name.empty()) featured.insert(instance + "." + d.name);
    }
    transport_->setBench(bench_.get());
    watch_->setBench(bench_.get(), std::move(featured));
    chrono_->setBench(bench_.get());
    fbd_->familyTone_ = entry_ != nullptr ? ui::familyTone(familyOf(entry_->category)) : ui::Tone::Accent;
    fbd_->setup(bench_.get(), entry_, instance, args);
    autoTrace();
    lastState_ = bench_ ? bench_->state() : State::Stopped;
    refresh();
}

void BenchPane::autoTrace() {
    // UN CHRONOGRAMME VIDE N'APPREND RIEN. On y met d'office les sorties du
    // bloc (ce qu'il calcule), puis ses entrees booleennes (ce qu'on bascule) :
    // quatre au plus, et chacune se retire d'un clic.
    if (!bench_ || !bench_->trendNames().empty()) return;
    const auto instance = fbd_->instance();
    std::vector<std::string> wanted;
    for (const auto& p : fbd_->pins())
        if (p.side == 1) wanted.push_back(instance + "." + p.name);
    for (const auto& p : fbd_->pins())
        if (p.side == 0) {
            const auto* r = findRow(bench_.get(), instance + "." + p.name);
            if (r != nullptr && r->boolean) wanted.push_back(r->name);
        }
    for (const auto& w : wanted) {
        if (bench_->trendNames().size() >= 4) break;
        const auto* r = findRow(bench_.get(), w);
        if (r != nullptr && (r->boolean || !r->type.empty()) && r->type != "STRING") bench_->addToTrend(w);
    }
}

void BenchPane::tick(double deltaSeconds) {
    if (!bench_) return;
    const auto ran = bench_->tick(static_cast<std::int64_t>(deltaSeconds * 1000.0));
    if (ran > 0 || bench_->state() != lastState_) refresh();
}

void BenchPane::refresh() {
    if (watch_) { watch_->rebuild(); }
    if (transport_) transport_->invalidate();
    if (chrono_) chrono_->invalidate();
    if (fbd_) fbd_->invalidate();

    const auto state = bench_ ? bench_->state() : State::Stopped;
    if (state != lastState_) {
        lastState_ = state;
        stateChanged->emit(state);
    }
    if (!status_) return;
    if (!bench_) {
        status_->setMessage("aucun essai en cours : ouvrez un bloc qui a un exemple, puis appuyez sur Essayer",
                            ui::StatusBar::Severity::Info);
        return;
    }
    if (!bench_->failure().empty()) {
        status_->setMessage(help::withCode(bench_->failure()), ui::StatusBar::Severity::Error);
        return;
    }
    std::size_t forced = 0;
    for (const auto& r : bench_->rows()) forced += r.forced;
    std::string m = state == State::Running ? "en marche" : state == State::Paused ? "en pause" : "à l'arrêt";
    m += "   ·   période " + std::to_string(bench_->periodMs()) + " ms";
    if (forced > 0) m += "   ·   " + std::to_string(forced) + (forced > 1 ? " variables forcées" : " variable forcée");
    m += "   ·   clic sur une LED : basculer   ·   icône courbe : tracer";
    status_->setMessage(m, forced > 0 ? ui::StatusBar::Severity::Warning
                          : state == State::Running ? ui::StatusBar::Severity::Success
                                                    : ui::StatusBar::Severity::Info);
}

ui::EventResult BenchPane::onEvent(const ui::InputEvent& ev) {
    // Espace fait marche / pause, comme un lecteur - sauf quand on tape dans le
    // filtre, qui a le clavier et consomme la touche avant nous.
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && bench_ && k->mods.none() && !k->repeat) {
        if (k->key == ui::Key::Escape) { closed->emit(); return ui::EventResult::Consumed; }
    }
    return ui::DockLayout::onEvent(ev);
}

// --- seams de test -----------------------------------------------------------
void BenchPane::transportForTest(int command) { if (transport_) transport_->fire(command); }
void BenchPane::periodForTest(int ms) { if (transport_) transport_->fire(100 + ms); }
bool BenchPane::clickLedForTest(const std::string& name) {
    const auto* r = findRow(bench_.get(), name);
    if (r == nullptr || !r->boolean) return false;
    watch_->toggleRequested->emit(name);
    return true;
}
bool BenchPane::clickTraceForTest(const std::string& name) {
    if (findRow(bench_.get(), name) == nullptr) return false;
    watch_->traceRequested->emit(name);
    return true;
}
bool BenchPane::clickLockForTest(const std::string& name) {
    const auto* r = findRow(bench_.get(), name);
    if (r == nullptr || !r->forced) return false;
    watch_->unforceRequested->emit(name);
    return true;
}
bool BenchPane::clickPinForTest(const std::string& pin) { return fbd_ && fbd_->clickPin(pin); }
void BenchPane::filterForTest(std::string term) { if (watch_) watch_->setFilter(std::move(term)); }
std::size_t BenchPane::visibleRowsForTest() const { return watch_ ? watch_->visibleCount() : 0; }
std::string BenchPane::instanceForTest() const { return fbd_ ? fbd_->instance() : std::string{}; }
std::size_t BenchPane::pinCountForTest() const { return fbd_ ? fbd_->pins().size() : 0; }

} // namespace app
