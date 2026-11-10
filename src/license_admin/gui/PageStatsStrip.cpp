// =============================================================================
//  license_admin/gui/PageStatsStrip.cpp - 1.47.0 : LA RANGEE DE COURBES DU
//  TABLEAU DE BORD (le resume des statistiques ; la maquette, planches 08 et 11)
// -----------------------------------------------------------------------------
//  Quatre petites cartes sur les 30 derniers jours, comparees aux 30 d'avant :
//  le montant encaisse (par jour), les licences actives, les PC actifs, les
//  demandes recues - leur valeur, l'ecart, leur courbe et sa bulle. Un clic sur
//  une carte (ou un point) : la page Statistiques. Les chiffres : un seul appel
//  (GET /admin/stats/serie, par runQuiet), relu toutes les 10 minutes tant que le
//  tableau de bord est montre. Sans service regle (ou sans cle qui signe, ou un
//  service d'avant la 1.47.0 : le tableau de bord le dit deja) : la rangee n'est
//  pas montree (shown() faux).
// =============================================================================
#include "StatsKit.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui::statskit {

namespace {

constexpr double kReloadSeconds = 600.0;

double steadyNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

class Strip final : public DashboardStrip {
public:
    explicit Strip(ShellHost host) : DashboardStrip("tableau.courbes"), host_(std::move(host)) {
        installSamplesFromEnvironment([this] { return host_.workspace().now(); });
        const struct {
            const char* id;
            const char* title;
            bool        bars;
        } cards[4] = {{"montant", "Montant encaiss\xC3\xA9", true}, {"licences", "Licences actives", false}, {"pc", "PC actifs", false}, {"demandes", "Demandes", false}};
        for (const auto& c : cards) {
            auto card = std::make_unique<ch::ChartCard>(std::string("tableau.courbes.") + c.id + ".carte", c.title, "30 j");
            ch::TimeChart* chart = &card->setChartAs(std::make_unique<ch::TimeChart>(std::string("tableau.courbes.") + c.id));
            chart->setOnPick([this](const ch::Pick&) {
                if (host_.navigate) host_.navigate("statistiques");
            });
            cards_.push_back(&static_cast<ch::ChartCard&>(addChild(std::move(card))));
            charts_.push_back(chart);
            bars_.push_back(c.bars);
        }
        fill();
    }

    void reload() override {
        lastLoad_ = -1e9;
        start();
    }
    [[nodiscard]] bool shown() const override {
        bool old = false;
        return blocked(host_.workspace(), &old).empty();
    }
    [[nodiscard]] const stats::SeriesReply& reply() const noexcept { return reply_; }

protected:
    void onLayout() override {
        const gfx::Rect b = bounds();
        const float gap = 12.f;
        const float w = (b.w - gap * 3.f) / 4.f;
        for (std::size_t i = 0; i < cards_.size(); ++i) cards_[i]->setBounds({b.x + static_cast<float>(i) * (w + gap), b.y, w, b.h});
    }
    void onPaint(const ui::PaintContext&) override {
        if (steadyNow() - lastLoad_ > kReloadSeconds || again_) start();
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && bounds().contains(m->pos)) {
            if (host_.navigate) host_.navigate("statistiques");
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    void start() {
        if (loading_ || !shown()) return;
        again_ = false;
        Workspace& w = host_.workspace();
        stats::SeriesQuery q;
        q.measures = {"ventes_montant", "licences_actives", "pc_actifs", "demandes_recues"};
        q.period = stats::lastDays(w.now(), 30);
        q.step = stats::Step::Day;
        q.compare = stats::Compare::Previous;
        const Workspace* wp = &w;
        auto out = std::make_shared<stats::SeriesReply>();
        loading_ = true;
        lastLoad_ = steadyNow();
        std::weak_ptr<bool> alive = alive_;
        const bool started = runQuietly(
            host_, "Tableau de bord : les courbes", [wp, q, out] { return fetchSeries(*wp, q, *out); },
            [this, alive, out](const OpResult& r) {
                if (alive.expired()) return;
                loading_ = false;
                if (r.ok) {
                    reply_ = *out;
                    problem_.clear();
                } else {
                    problem_ = r.message;
                }
                fill();
            });
        if (!started) {
            loading_ = false;
            again_ = true;
        }
    }

    void fill() {
        const char* names[4] = {"ventes_montant", "licences_actives", "pc_actifs", "demandes_recues"};
        const std::string today = stats::dayOf(host_.workspace().now());
        for (std::size_t i = 0; i < 4; ++i) {
            const stats::Series* s = reply_.find(names[i]);
            ch::TimeChartData t;
            if (!s || s->refused) {
                t.emptyText = !problem_.empty() ? problem_
                              : s && s->refused ? std::string("Il te manque un droit pour ces chiffres.")
                                                : std::string("Lecture des chiffres\xE2\x80\xA6");
                charts_[i]->setData(t);
                cards_[i]->setValue("\xE2\x80\x94");
                continue;
            }
            const bool money = i == 0, level = i == 1 || i == 2;
            t.kind = bars_[i] ? ch::Kind::Bars : ch::Kind::Line;
            t.x = axisOf(reply_.buckets, reply_.step, level);
            t.xTicks = {{0, -1}, {static_cast<int>(t.x.size()) - 1, 1}};
            t.partialFrom = level ? -1 : partialFrom(reply_.buckets, reply_.step, today);
            ch::Series a;
            a.key = "un";
            a.name = "ces 30 jours";
            a.values = valuesOf(s->points, money ? 0.01 : 1.0);
            a.area = !bars_[i];
            a.endDot = !bars_[i];
            t.series.push_back(a);
            if (s->hasComparison() && level) {
                ch::Series c;
                c.key = "cmp";
                c.name = "les 30 jours d'avant";
                c.values = valuesOf(s->before, 1.0);
                c.role = ch::Role::Compare;
                t.series.push_back(c);
            }
            t.legend = ch::LegendMode::None;
            t.yTickCount = 2;
            if (level) {
                double lo = 1e18;
                for (const auto& x : t.series)
                    for (const double v : x.values)
                        if (stats::isNumber(v)) lo = std::min(lo, v);
                if (lo < 1e17 && lo > 0) t.yMin = std::max(0.0, std::floor(lo * 0.85 / ch::niceStep(lo / 4.0)) * ch::niceStep(lo / 4.0));
            }
            if (money) t.valueFormat = [](double v) { return ch::fr::euros(v); };
            t.clickHint = "Clic : la page Statistiques";
            charts_[i]->setData(t);
            const std::string value = money ? stats::euros(s->total) : stats::isNumber(s->total) ? ch::fr::integer(s->total) : std::string("\xE2\x80\x94");
            cards_[i]->setValue(value, s->hasComparison() ? ch::deltaOf(s->total, s->beforeTotal) : ch::Delta{},
                                s->hasComparison() ? (money ? "contre " + stats::euros(s->beforeTotal) : "contre " + ch::fr::integer(s->beforeTotal)) : std::string());
        }
        invalidateLayout();
        invalidate();
    }

    ShellHost                   host_;
    std::vector<ch::ChartCard*> cards_;
    std::vector<ch::TimeChart*> charts_;
    std::vector<bool>           bars_;
    stats::SeriesReply          reply_;
    std::string                 problem_;
    bool                        loading_{false}, again_{false};
    double                      lastLoad_{-1e9};
    std::shared_ptr<bool>       alive_{std::make_shared<bool>(true)};
};

} // namespace

std::unique_ptr<DashboardStrip> makeDashboardStrip(ShellHost host) { return std::make_unique<Strip>(std::move(host)); }

} // namespace license_admin::gui::statskit
