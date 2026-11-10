// =============================================================================
//  license_admin/gui/PageChartsGallery.cpp - 1.47.0 : LA GALERIE DES GRAPHIQUES
//  (une page cachee : --page graphes-essai, jamais dans le menu)
// -----------------------------------------------------------------------------
//  Chaque graphique de ui/widgets/Charts.hpp sur des donnees d'essai (rien de
//  reel : Camille, Lea, Hugo, Nina Essai ; Atelier Exemple, Societe d'essai...),
//  range en planches comme la maquette 1.47.0 : les ventes et les licences
//  (planche 08), les mises a jour (09), un client (10), un admin (02 a 04), qui
//  fait quoi (05), puis les cas limites (vide, un point, tout a zero, tres
//  grandes valeurs, negatives, 365 points, libelles trop longs) et le theme
//  sombre. Pour comparer avec les planches, et pour les captures :
//    --page graphes-essai --cliquer graphes.p3 --capture ...
//  Un clic sur un point (ou Entree) : un avis dit ce que la vraie page filtrerait.
// =============================================================================
#include "Page.hpp"

#include "../../ui/widgets/Charts.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace license_admin::gui {

namespace {

namespace ch = ui::charts;
using ch::fr::Day;
using gfx::Color;
using gfx::FontId;
using gfx::Rect;

// Les memes chiffres a chaque rendu.
struct Rng {
    std::uint32_t s;
    double next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<double>(s) / 4294967296.0;
    }
    double in(double a, double b) { return a + (b - a) * next(); }
    double whole(double a, double b) { return std::round(in(a, b)); }
};

double sum(const std::vector<double>& v) {
    double s = 0;
    for (const double x : v)
        if (std::isfinite(x)) s += x;
    return s;
}

// ---- une planche : une grille de cartes (colonnes egales, rangees ponderees) ----
class Board final : public ui::Widget {
public:
    Board(std::string id, int columns, std::vector<float> rows) : ui::Widget(std::move(id)), columns_(columns), rows_(std::move(rows)) {}
    template <class T>
    T& put(std::unique_ptr<T> w, int col, int row, int colSpan = 1, int rowSpan = 1) {
        T& ref = static_cast<T&>(addChild(std::move(w)));
        cells_.push_back({&ref, col, row, colSpan, rowSpan});
        invalidateLayout();
        return ref;
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float gap = 12.f;
        const float cw = (b.w - gap * static_cast<float>(columns_ - 1)) / static_cast<float>(columns_);
        float total = 0.f;
        for (const float r : rows_) total += r;
        const float avail = b.h - gap * static_cast<float>(rows_.size() - 1);
        std::vector<float> top, height;
        float y = b.y;
        for (const float r : rows_) {
            top.push_back(y);
            height.push_back(avail * r / total);
            y += avail * r / total + gap;
        }
        for (const auto& c : cells_) {
            const float x = b.x + static_cast<float>(c.col) * (cw + gap);
            const float w = cw * static_cast<float>(c.colSpan) + gap * static_cast<float>(c.colSpan - 1);
            const std::size_t r0 = static_cast<std::size_t>(c.row), r1 = static_cast<std::size_t>(c.row + c.rowSpan - 1);
            c.w->setBounds({x, top[r0], w, top[r1] + height[r1] - top[r0]});
        }
    }

private:
    struct Cell {
        ui::Widget* w;
        int         col, row, colSpan, rowSpan;
    };
    int                columns_;
    std::vector<float> rows_;
    std::vector<Cell>  cells_;
};

// ---- une tuile (planche 02) : le libelle, la valeur, l'ecart, et sa courbe sur 12 semaines (paintSparkline) ----
class Tile final : public ui::Widget {
public:
    Tile(std::string id, std::string label, std::string value, ch::Delta delta, std::string sub, std::vector<double> spark, Color color)
        : ui::Widget(std::move(id)), label_(std::move(label)), value_(std::move(value)), delta_(std::move(delta)), sub_(std::move(sub)),
          spark_(std::move(spark)), color_(color) {
        setTooltip(label_ + " : " + value_);
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const ch::colors::Ink k = ch::colors::ink(ctx.theme);
        const Rect b = bounds();
        r.fillRoundedRect({b.x, b.y + 1.f, b.w, b.h + 1.f}, ctx.theme.brand.cardShadow, 12.f);
        r.fillRoundedRect(b, k.line, 12.f);
        r.fillRoundedRect(b.inset(1.f, 1.f), k.surface, 11.f);
        const float x = b.x + 14.f;
        r.drawText({x, b.y + 11.f}, label_, FontId{12}, k.muted);
        const FontId vf = gfx::semibold(FontId{22});
        r.drawText({x, b.y + 28.f}, value_, vf, k.ink);
        const float y2 = b.y + 28.f + r.lineHeight(vf) + 3.f;
        const float dw = ch::paintDelta(r, ctx.theme, x, y2, delta_, FontId{12});
        r.drawText({x + dw + (dw > 0.f ? 6.f : 0.f), y2}, sub_, FontId{12}, k.muted);
        const float sw = std::min(90.f, b.w * 0.36f);
        ch::SparkStyle st;
        st.color = color_;
        ch::paintSparkline(r, ctx.theme, {b.right() - 14.f - sw, b.y + 30.f, sw, 30.f}, spark_, nullptr, st);
    }

private:
    std::string         label_, value_;
    ch::Delta           delta_;
    std::string         sub_;
    std::vector<double> spark_;
    Color               color_;
};

// ---- le theme sombre : des graphiques dessines avec Theme::dark() (sans survol : un apercu) ----
class DarkPreview final : public ui::Widget {
public:
    explicit DarkPreview(std::string id) : ui::Widget(std::move(id)), dark_(ui::Theme::dark()) {}
    ch::TimeChart& add(ch::TimeChartData d) {
        auto c = std::make_unique<ch::TimeChart>(id() + "." + std::to_string(charts_.size()));
        c->setData(std::move(d));
        charts_.push_back(std::move(c));
        return *charts_.back();
    }
    ch::BarList& addBars(ch::BarListData d) {
        bars_ = std::make_unique<ch::BarList>(id() + ".barres");
        bars_->setData(std::move(d));
        return *bars_;
    }

protected:
    void onLayout() override {
        const Rect b = bounds().inset(14.f, 12.f);
        const float top = b.y + 22.f;
        const std::size_t n = charts_.size() + (bars_ ? 1 : 0);
        const float h = (b.bottom() - top - 10.f * static_cast<float>(n - 1)) / static_cast<float>(std::max<std::size_t>(1, n));
        float y = top;
        for (auto& c : charts_) {
            c->setBounds({b.x, y, b.w, h});
            c->layout();
            y += h + 10.f;
        }
        if (bars_) {
            bars_->setBounds({b.x, y, b.w, h});
            bars_->layout();
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        ctx.r.fillRoundedRect(b, dark_.color.border, 12.f);
        ctx.r.fillRoundedRect(b.inset(1.f, 1.f), dark_.brand.card.a ? dark_.brand.card : dark_.color.panelBg, 11.f);
        ctx.r.drawText({b.x + 14.f, b.y + 11.f}, "Le th\xC3\xA8me sombre (aper\xC3\xA7u)", gfx::semibold(FontId{13}), dark_.color.text);
        const ui::PaintContext local{ctx.r, dark_, ctx.clip, ctx.time, nullptr};
        for (auto& c : charts_) c->render(local);
        if (bars_) bars_->render(local);
    }

private:
    ui::Theme                                  dark_;
    std::vector<std::unique_ptr<ch::TimeChart>> charts_;
    std::unique_ptr<ch::BarList>               bars_;
};

const char* const kPlanches[] = {"Ventes", "Licences et PC", "Mises \xC3\xA0 jour", "Un client", "Un admin", "Son activit\xC3\xA9", "Qui fait quoi", "Cas limites"};
constexpr int kPlancheCount = 8;

class ChartsGalleryPage final : public Page {
public:
    explicit ChartsGalleryPage(ShellHost host) : Page("graphes-essai", std::move(host)) {
        for (int i = 0; i < kPlancheCount; ++i) {
            auto b = std::make_unique<ui::Button>(std::to_string(i + 1) + " " + kPlanches[i], "graphes.p" + std::to_string(i + 1));
            ui::Button* raw = &static_cast<ui::Button&>(addChild(std::move(b)));
            links_ += raw->clicked->connect([this, i] { show(i); });
            tabs_.push_back(raw);
        }
        boards_.push_back(&addBoard(buildSales()));
        boards_.push_back(&addBoard(buildLicences()));
        boards_.push_back(&addBoard(buildUpdates()));
        boards_.push_back(&addBoard(buildClient()));
        boards_.push_back(&addBoard(buildAdmin()));
        boards_.push_back(&addBoard(buildActivity()));
        boards_.push_back(&addBoard(buildWhoDoesWhat()));
        boards_.push_back(&addBoard(buildEdgeCases()));
        show(0);
    }

    [[nodiscard]] std::string title() const override { return "Graphiques (essai)"; }
    void refresh() override {}
    [[nodiscard]] int current() const noexcept { return current_; }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float pad = 22.f;
        float x = b.x + pad;
        const float y = b.y + 58.f;
        for (ui::Button* t : tabs_) {
            const float w = std::max(60.f, t->sizeHint().preferred.w);
            t->setBounds({x, y, w, 30.f});
            x += w + 6.f;
        }
        const Rect area{b.x + pad, y + 42.f, std::max(200.f, b.w - 2.f * pad), std::max(200.f, b.bottom() - pad + 6.f - (y + 42.f))};
        for (Board* bd : boards_) bd->setBounds(area);
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        ctx.r.fillRect(b, ctx.theme.color.windowBg);
        ctx.r.drawText({b.x + 22.f, b.y + 14.f}, title(), gfx::semibold(FontId{22}), ctx.theme.color.text);
        const float tw = ctx.r.measure(title(), gfx::semibold(FontId{22})).width;
        ctx.r.drawText({b.x + 22.f + tw + 14.f, b.y + 22.f},
                       "page cach\xC3\xA9" "e (--page graphes-essai) : chaque graphique de la 1.47.0 sur des donn\xC3\xA9" "es d'essai ; un clic sur un point dit ce que la page filtrerait",
                       FontId{12}, ctx.theme.color.textMuted);
    }

private:
    Board& addBoard(std::unique_ptr<Board> b) { return static_cast<Board&>(addChild(std::move(b))); }

    void show(int i) {
        current_ = std::clamp(i, 0, kPlancheCount - 1);
        for (int j = 0; j < kPlancheCount; ++j) {
            boards_[static_cast<std::size_t>(j)]->setVisibility(j == current_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
            tabs_[static_cast<std::size_t>(j)]->setStyle(j == current_ ? ui::Button::Style::Primary : ui::Button::Style::Default);
        }
        invalidateLayout();
    }

    // Le clic sur un graphique : ce que la vraie page ferait (la liste filtree).
    void said(const std::string& chart, const std::string& what) {
        if (host_.flash) host_.flash("Clic sur \xC2\xAB " + chart + " \xC2\xBB : " + what + " \xE2\x80\x94 la page filtrerait sa liste.", Tone::Info);
    }
    template <class C>
    void wire(C& chart, const std::string& name) {
        C* c = &chart;
        chart.setOnPick([this, c, name](const ch::Pick& p) {
            const ch::Tip t = c->tipFor(p.index, p.series);
            std::string what = t.title;
            if (!t.rows.empty()) what += " (" + t.rows.front().value + (t.rows.front().name.empty() ? "" : " " + t.rows.front().name) + ")";
            said(name, what);
        });
    }

    // Une carte et son graphique.
    template <class T>
    T& card(Board& b, const std::string& id, const std::string& title, const std::string& small, int col, int row, int cs, int rs,
            std::unique_ptr<T> chart, const std::string& value = {}, ch::Delta delta = {}, const std::string& compare = {},
            std::vector<ch::SideRow> side = {}) {
        auto c = std::make_unique<ch::ChartCard>(id + ".carte", title, small);
        if (!value.empty()) c->setValue(value, delta, compare);
        if (!side.empty()) c->setSide(std::move(side));
        T& ref = c->setChartAs(std::move(chart));
        b.put(std::move(c), col, row, cs, rs);
        wire(ref, title);
        return ref;
    }
    std::unique_ptr<ch::TimeChart> tchart(const std::string& id, ch::TimeChartData d) {
        auto c = std::make_unique<ch::TimeChart>(id);
        c->setData(std::move(d));
        return c;
    }
    std::unique_ptr<ch::BarList> barList(const std::string& id, ch::BarListData d) {
        auto c = std::make_unique<ch::BarList>(id);
        c->setData(std::move(d));
        return c;
    }

    // ---- les axes communs : 13 semaines (17/08 -> 09/11/2026), la derniere en cours ----
    static std::vector<ch::Category> weeks() { return ch::weeklyAxis(ch::fr::day(2026, 8, 17), 13); }
    static std::vector<std::pair<int, int>> weekTicks() { return {{0, 0}, {4, 0}, {8, 0}, {12, 0}}; }
    static ch::Series serie(std::string key, std::string name, std::vector<double> v) {
        ch::Series s;
        s.key = std::move(key);
        s.name = std::move(name);
        s.values = std::move(v);
        return s;
    }
    static ch::Series compare(std::string name, std::vector<double> v) {
        ch::Series s = serie("cmp", std::move(name), std::move(v));
        s.role = ch::Role::Compare;
        return s;
    }
    static ch::Format eurosF() { return [](double v) { return ch::fr::euros(v); }; }
    static ch::Format intF() { return [](double v) { return ch::fr::integer(v); }; }
    static ch::Format pctF() { return [](double v) { return ch::fr::percent(v); }; }

    // =========================================================== 1. LES VENTES (08) ==
    std::unique_ptr<Board> buildSales() {
        auto b = std::make_unique<Board>("graphes.ventes", 4, std::vector<float>{1.f, 1.08f, 1.f});
        const std::vector<double> eur = {1450, 2180, 1720, 1990, 1610, 2240, 1880, 2050, 2480, 1720, 2520, 2390, 690};
        const std::vector<double> eurP = {1650, 1720, 1810, 1900, 1580, 1990, 1700, 1870, 2050, 1930, 2090, 1880, 1830};
        const std::vector<double> cmd = {7, 9, 8, 9, 8, 10, 8, 9, 11, 8, 10, 10, 3};
        const std::vector<double> cmdP = {8, 8, 7, 9, 7, 9, 8, 8, 9, 9, 8, 8, 8};
        {
            ch::TimeChartData d;
            d.kind = ch::Kind::Bars;
            d.x = weeks();
            d.xTicks = weekTicks();
            d.series = {serie("un", "2026", eur), compare("l'an dernier (les m\xC3\xAAmes semaines de 2025)", eurP)};
            d.valueFormat = eurosF();
            d.partialFrom = 12;
            d.tipExtra = [cmd, eur](int i) {
                const double c = cmd[static_cast<std::size_t>(i)];
                return ch::fr::integer(c) + " commandes " + ch::fr::kDot + " panier " + ch::fr::euros(c > 0 ? eur[static_cast<std::size_t>(i)] / c : 0);
            };
            d.clickHint = "Clic : ces commandes";
            d.exampleTip = 10;
            card(*b, "graphes.montant", "Montant encaiss\xC3\xA9", "par semaine", 0, 0, 1, 1, tchart("graphes.montant", d), ch::fr::euros(sum(eur)),
                 ch::deltaOf(sum(eur), sum(eurP)), "contre " + ch::fr::euros(sum(eurP)) + " l'an dernier");
        }
        {
            // Le cumul de l'annee, semaine apres semaine (45 semaines depuis le 05/01).
            Rng g{7};
            std::vector<double> c26, c25;
            double a = 0, p = 0;
            std::vector<ch::Category> x;
            std::vector<std::pair<int, int>> ticks;
            const Day first = ch::fr::day(2026, 1, 5);
            int lastMonth = 0;
            for (int i = 0; i < 45; ++i) {
                a += g.in(1550, 2250);
                p += g.in(1450, 2050);
                c26.push_back(a);
                c25.push_back(p);
                int y, m, dd;
                ch::fr::civil(first + 7 * i, y, m, dd);
                x.push_back({ch::fr::monthShort(m), ch::fr::weekRange(first + 7 * i), false});
                if (m != lastMonth && (m == 1 || m == 4 || m == 7 || m == 10)) ticks.push_back({i, 0});
                lastMonth = m;
            }
            ch::TimeChartData d;
            d.x = x;
            d.xTicks = ticks;
            ch::Series s = serie("un", "2026", c26);
            s.area = true;
            s.endDot = true;
            d.series = {s, compare("2025", c25)};
            d.yFormat = [](double v) { return ch::fr::compact(v); };
            d.valueFormat = eurosF();
            d.yTickCount = 2;
            d.notes.push_back({44, c26.back(), ch::fr::eurosCompact(c26.back()), ch::Note::Anchor::End, -10.f});
            d.clickHint = "Clic : les commandes jusque-l\xC3\xA0";
            card(*b, "graphes.cumul", "Cumul\xC3\xA9 sur l'ann\xC3\xA9" "e", "", 1, 0, 1, 1, tchart("graphes.cumul", d), ch::fr::euros(c26.back()),
                 ch::deltaOf(c26.back(), c25.back()), "au 10/11");
        }
        {
            ch::TimeChartData d;
            d.kind = ch::Kind::Bars;
            d.x = weeks();
            d.xTicks = weekTicks();
            d.series = {serie("un", "2026", cmd), compare("l'an dernier", cmdP)};
            d.partialFrom = 12;
            d.yTickCount = 2;
            d.clickHint = "Clic : ces commandes";
            card(*b, "graphes.commandes", "Commandes", "", 2, 0, 1, 1, tchart("graphes.commandes", d), ch::fr::integer(sum(cmd)),
                 ch::deltaOf(sum(cmd), sum(cmdP)), "contre " + ch::fr::integer(sum(cmdP)));
        }
        {
            std::vector<double> basket, basketP;
            for (std::size_t i = 0; i < eur.size(); ++i) {
                basket.push_back(std::round(eur[i] / cmd[i]));
                basketP.push_back(std::round(eurP[i] / cmdP[i]));
            }
            ch::TimeChartData d;
            d.x = weeks();
            d.xTicks = weekTicks();
            d.lineInBands = true;
            ch::Series s = serie("un", "2026", basket);
            s.endDot = true;
            d.series = {s, compare("l'an dernier", basketP)};
            d.yTicks = {0, 200, 400};
            d.valueFormat = eurosF();
            card(*b, "graphes.panier", "Panier moyen", "", 3, 0, 1, 1, tchart("graphes.panier", d), ch::fr::euros(std::round(sum(eur) / sum(cmd))),
                 ch::deltaOf(sum(eur) / sum(cmd), sum(eurP) / sum(cmdP)), "contre " + ch::fr::euros(std::round(sum(eurP) / sum(cmdP))));
        }
        {
            // Par offre : empilees dans l'ordre fixe, les valeurs a cote.
            Rng g{11};
            std::vector<double> pe, pr, en, op;
            for (std::size_t i = 0; i < 13; ++i) {
                const double f = i == 12 ? 0.3 : 1.0;
                pe.push_back(std::round(g.in(520, 980) * f));
                pr.push_back(std::round(g.in(620, 1250) * f));
                en.push_back(i % 4 == 2 ? std::round(g.in(380, 520) * f) : 0.0);
                op.push_back(std::round(g.in(120, 290) * f));
            }
            ch::TimeChartData d;
            d.kind = ch::Kind::Stacked;
            d.x = weeks();
            d.xTicks = weekTicks();
            d.series = {serie("offre:PERSONAL", "PERSONAL", pe), serie("offre:PROFESSIONAL", "PROFESSIONAL", pr), serie("offre:ENTERPRISE", "ENTERPRISE", en),
                        serie("offre:OPTIONS", "Options", op)};
            d.legend = ch::LegendMode::None;   // la legende est a cote, avec les chiffres
            d.valueFormat = eurosF();
            d.partialFrom = 12;
            d.clickHint = "Clic : ces commandes";
            const double tot = sum(pe) + sum(pr) + sum(en) + sum(op);
            const auto row = [&](const char* name, const char* key, const std::vector<double>& v) {
                return ch::SideRow{name, ch::fr::euros(sum(v)), ch::fr::percent(sum(v) * 100.0 / tot), ch::ColorKeys::standard().colorOf(key), false, false};
            };
            std::vector<ch::SideRow> side = {row("PERSONAL", "offre:PERSONAL", pe), row("PROFESSIONAL", "offre:PROFESSIONAL", pr),
                                             row("ENTERPRISE", "offre:ENTERPRISE", en), row("Options", "offre:OPTIONS", op),
                                             ch::SideRow{"D\xC3\xA9" "couverte (l'essai)", "gratuite", "", {0, 0, 0, 0}, false, true}};
            card(*b, "graphes.offres", "Ventes par offre", "montant par semaine", 0, 1, 2, 1, tchart("graphes.offres", d), ch::fr::euros(tot), {},
                 "4 offres et options", side);
        }
        {
            ch::BarListData d;
            d.mode = ch::BarMode::Track;
            d.color = ch::colors::kOptions;
            d.maxValue = 1800;
            d.format = eurosF();
            d.rows = {{"", "Exports avanc\xC3\xA9s", "", 1450, {}, "", "21 cmd"}, {"", "Travail \xC3\xA0 plusieurs", "", 860, {}, "", "10 cmd"},
                      {"", "Imports avanc\xC3\xA9s", "", 290, {}, "", "5 cmd"}};
            d.rowHeight = 34.f;
            d.clickHint = "Clic : ces commandes";
            card(*b, "graphes.options", "Options vendues", "", 2, 1, 1, 1, barList("graphes.options", d), ch::fr::euros(2600), ch::deltaOf(2600, 2200),
                 "36 commandes");
        }
        {
            Rng g{5};
            std::vector<double> on, hand;
            for (int i = 0; i < 13; ++i) {
                on.push_back(i == 12 ? 1 : g.whole(2, 6));
                hand.push_back(i == 12 ? 1 : g.whole(1, 3));
            }
            ch::TimeChartData d;
            d.kind = ch::Kind::Grouped;
            d.x = weeks();
            d.xTicks = weekTicks();
            ch::Series a = serie("un", "en ligne", on), m = serie("deux", "sign\xC3\xA9" "es \xC3\xA0 la main", hand);
            a.legendValue = ch::fr::integer(sum(on));
            m.legendValue = ch::fr::integer(sum(hand));
            d.series = {a, m};
            d.partialFrom = 12;
            d.yTickCount = 2;
            d.tipForward = true;
            card(*b, "graphes.vendues", "Licences vendues", "", 3, 1, 1, 1, tchart("graphes.vendues", d), ch::fr::integer(sum(on) + sum(hand)),
                 ch::deltaOf(sum(on) + sum(hand), 69), ch::fr::percent(sum(on) * 100.0 / (sum(on) + sum(hand))) + " en ligne");
        }
        {
            Rng g{3};
            std::vector<double> nw, pro, exp, rev;
            for (int i = 0; i < 13; ++i) {
                nw.push_back(g.whole(1, 5));
                pro.push_back(g.whole(1, 4));
                exp.push_back(g.whole(0, 3));
                rev.push_back(i % 5 == 3 ? 1 : 0);
            }
            ch::TimeChartData d;
            d.kind = ch::Kind::Diverging;
            d.x = weeks();
            d.xTicks = weekTicks();
            ch::Series e = serie("down:1", "expir\xC3\xA9" "es", exp), r = serie("down:0", "r\xC3\xA9voqu\xC3\xA9" "es", rev);
            e.below = r.below = true;
            ch::Series n = serie("up:0", "nouvelles", nw), p = serie("up:1", "prolong\xC3\xA9" "es", pro);
            n.legendValue = ch::fr::integer(sum(nw));
            p.legendValue = ch::fr::integer(sum(pro));
            e.legendValue = ch::fr::integer(sum(exp));
            r.legendValue = ch::fr::integer(sum(rev));
            d.series = {n, p, e, r};
            d.yFormat = [](double v) { return ch::fr::integer(v); };
            d.tipForward = true;
            d.clickHint = "Clic : ces licences";
            const double net = sum(nw) - sum(exp) - sum(rev);
            card(*b, "graphes.mouvements", "Mouvements des licences", "", 0, 2, 1, 1, tchart("graphes.mouvements", d), ch::fr::signedInteger(net) + " net",
                 {}, "entr\xC3\xA9" "es \xE2\x88\x92 sorties");
        }
        {
            Rng g{9};
            std::vector<double> soon(13, ch::kNone), later(13, ch::kNone);
            for (int i = 0; i < 13; ++i) (i < 4 ? soon : later)[static_cast<std::size_t>(i)] = g.whole(0, 5);
            ch::TimeChartData d;
            d.kind = ch::Kind::Stacked;
            d.x = ch::weeklyAxis(ch::fr::day(2026, 11, 9), 13);
            d.xTicks = {{0, 0}, {4, 0}, {8, 0}, {12, 0}};
            ch::Series a = serie("un", "dans les 30 jours", soon), l = serie("autres", "ensuite", later);
            a.legendValue = ch::fr::integer(sum(soon));
            l.legendValue = ch::fr::integer(sum(later));
            d.series = {a, l};
            d.yTickCount = 2;
            d.clickHint = "Clic : ces licences";
            card(*b, "graphes.echeances", "\xC3\x89" "ch\xC3\xA9" "ances", "les 90 jours \xC3\xA0 venir", 1, 2, 1, 1, tchart("graphes.echeances", d),
                 ch::fr::integer(sum(soon) + sum(later)), {}, ch::fr::euros(11960) + " \xC3\xA0 renouveler");
        }
        {
            const std::vector<double> h = {5.2, 4.8, 5.6, 4.1, 4.4, 3.9, 4.2, 3.6, 3.8, 3.3, 3.5, 3.1, 3.7};
            const std::vector<double> hp = {6.1, 5.9, 6.3, 5.5, 5.8, 5.2, 5.6, 5.4, 5.1, 5.6, 5.3, 5.0, 5.2};
            ch::TimeChartData d;
            d.x = weeks();
            d.xTicks = weekTicks();
            ch::Series s = serie("un", "2026", h);
            s.endDot = true;
            d.series = {s, compare("l'an dernier", hp)};
            d.yTicks = {0, 4, 8};
            d.yFormat = [](double v) { return ch::fr::integer(v) + ch::fr::kNbsp + "h"; };
            d.valueFormat = [](double v) { return ch::fr::hours(v); };
            card(*b, "graphes.delai", "D\xC3\xA9lai de r\xC3\xA9ponse", "m\xC3\xA9" "dian", 2, 2, 1, 1, tchart("graphes.delai", d), ch::fr::hours(3.67), ch::deltaOf(3.67, 5.2, false),
                 "moyen " + ch::fr::hours(5.58));
        }
        {
            Rng g{21};
            std::vector<double> v;
            int capped = 0;
            for (int i = 0; i < 90; ++i) {
                const bool we = ch::fr::weekday(ch::fr::day(2026, 8, 13) + i) >= 5;
                double x = we ? g.whole(0, 3) : g.whole(6, 17);
                if (!we && g.next() < 0.08) {
                    x = 20;
                    ++capped;
                }
                v.push_back(x);
            }
            ch::TimeChartData d;
            d.x = ch::dailyAxis(ch::fr::day(2026, 8, 13), 90);
            d.xTicks = {{0, -1}, {30, 0}, {60, 0}, {89, 1}};
            ch::Series s = serie("un", "jetons pris en m\xC3\xAAme temps", v);
            s.area = true;
            s.width = 1.5f;
            d.series = {s};
            d.refs.push_back({20, "plafond", true, {0, 0, 0, 0}});
            d.yTicks = {0, 10, 20};
            d.clickHint = "Clic : les PC de ce jour";
            ch::Delta warn;
            card(*b, "graphes.flottantes", "Flottantes", "le pic du jour", 3, 2, 1, 1, tchart("graphes.flottantes", d), "20 / 20 jetons", warn,
                 std::to_string(capped) + " jours au plafond");
        }
        return b;
    }

    // ================================================= 2. LES LICENCES ET LES PC (08) ==
    std::unique_ptr<Board> buildLicences() {
        auto b = std::make_unique<Board>("graphes.licences", 4, std::vector<float>{1.f, 1.f});
        {
            std::vector<double> a, p;
            Rng g{13};
            double x = 109, y = 96;
            for (int i = 0; i < 13; ++i) {
                x += g.in(0.2, 2.6);
                y += g.in(-0.4, 1.4);
                a.push_back(std::round(x));
                p.push_back(std::round(y));
            }
            ch::TimeChartData d;
            d.x = weeks();
            for (auto& c : d.x) c.tip = "Fin de la s" + c.tip.substr(1);
            d.xTicks = weekTicks();
            ch::Series s = serie("un", "2026", a);
            s.endDot = true;
            d.series = {s, compare("l'an dernier", p)};
            d.yMin = 80;
            d.yTicks = {80, 100, 120, 140};
            d.clickHint = "Clic : les licences actives ce jour-l\xC3\xA0";
            card(*b, "graphes.actives", "Licences actives", "", 0, 0, 1, 1, tchart("graphes.actives", d), ch::fr::integer(a.back()), ch::deltaOf(a.back(), p.back()),
                 "contre " + ch::fr::integer(p.back()));
        }
        {
            Rng g{17};
            std::vector<double> in, done;
            for (int i = 0; i < 13; ++i) {
                in.push_back(g.whole(13, 20));
                done.push_back(in.back() - g.whole(0, 2));
            }
            ch::TimeChartData d;
            d.x = weeks();
            d.xTicks = weekTicks();
            ch::Series a = serie("un", "re\xC3\xA7ues", in), t = serie("deux", "trait\xC3\xA9" "es", done);
            t.legendValue = ch::fr::integer(sum(done));
            t.endDot = true;
            d.series = {a, t};
            d.yMax = 25;
            d.yTickCount = 2;
            d.clickHint = "Clic : ces demandes";
            card(*b, "graphes.demandes", "Demandes", "", 1, 0, 1, 1, tchart("graphes.demandes", d), ch::fr::integer(sum(in)) + " re\xC3\xA7ues",
                 ch::deltaOf(sum(in), 175), "5 en attente");
        }
        {
            std::vector<double> pe, pr, en, de;
            Rng g{23};
            double a = 60, bb = 115, c = 44, e = 12;
            for (int i = 0; i < 13; ++i) {
                a += g.in(-0.5, 2.0);
                bb += g.in(0, 2.6);
                c += g.in(-0.3, 0.8);
                e += g.in(-1, 1);
                pe.push_back(std::round(a));
                pr.push_back(std::round(bb));
                en.push_back(std::round(c));
                de.push_back(std::round(e));
            }
            ch::TimeChartData d;
            d.kind = ch::Kind::Stacked;
            d.x = weeks();
            for (auto& x : d.x) x.tip = "Fin de la s" + x.tip.substr(1);
            d.xTicks = weekTicks();
            d.series = {serie("offre:PERSONAL", "PERSONAL", pe), serie("offre:PROFESSIONAL", "PROFESSIONAL", pr), serie("offre:ENTERPRISE", "ENTERPRISE", en),
                        serie("offre:DECOUVERTE", "D\xC3\xA9" "couverte", de)};
            d.legend = ch::LegendMode::None;
            d.yTicks = {0, 100, 200, 300};
            d.clickHint = "Clic : ces PC (Activations)";
            const double tot = pe.back() + pr.back() + en.back() + de.back();
            std::vector<ch::SideRow> side = {{"PERSONAL", ch::fr::integer(pe.back()), "", ch::colors::kPersonal, false, false},
                                             {"PROFESSIONAL", ch::fr::integer(pr.back()), "", ch::colors::kProfessional, false, false},
                                             {"ENTERPRISE", ch::fr::integer(en.back()), "", ch::colors::kEnterprise, false, false},
                                             {"D\xC3\xA9" "couverte", ch::fr::integer(de.back()), "", ch::colors::kDiscovery, false, false},
                                             {"Tous", ch::fr::integer(tot), "", {0, 0, 0, 0}, false, true}};
            card(*b, "graphes.pc", "PC actifs", "par offre, \xC3\xA0 la fin de chaque semaine", 2, 0, 2, 1, tchart("graphes.pc", d), ch::fr::integer(tot),
                 ch::deltaOf(tot, 247), "contre 247 l'an dernier", side);
        }
        {
            Rng g{29};
            std::vector<double> act, lib, ret;
            for (int i = 0; i < 13; ++i) {
                act.push_back(g.whole(3, 9));
                lib.push_back(g.whole(1, 5));
                ret.push_back(g.whole(0, 2));
            }
            ch::TimeChartData d;
            d.kind = ch::Kind::Diverging;
            d.x = weeks();
            d.xTicks = weekTicks();
            ch::Series a = serie("up:0", "activ\xC3\xA9s", act), l = serie("down:1", "lib\xC3\xA9r\xC3\xA9s", lib), r = serie("down:0", "retir\xC3\xA9s", ret);
            l.below = r.below = true;
            a.legendValue = ch::fr::integer(sum(act));
            l.legendValue = ch::fr::integer(sum(lib));
            r.legendValue = ch::fr::integer(sum(ret));
            d.series = {a, l, r};
            d.tipForward = true;
            d.clickHint = "Clic : ces PC";
            card(*b, "graphes.mvtpc", "Mouvements des PC", "", 0, 1, 1, 1, tchart("graphes.mvtpc", d),
                 ch::fr::signedInteger(sum(act) - sum(lib) - sum(ret)) + " net", {}, "267 " + std::string(ch::fr::kArrow) + " 286");
        }
        {
            Rng g{31};
            std::vector<double> st, conv;
            for (int i = 0; i < 13; ++i) {
                st.push_back(g.whole(1, 4));
                conv.push_back(g.next() < 0.55 ? g.whole(0, 1) : 0);
            }
            ch::TimeChartData d;
            d.kind = ch::Kind::Grouped;
            d.x = weeks();
            d.xTicks = weekTicks();
            ch::Series s = serie("un", "commenc\xC3\xA9" "es", st), c = serie("deux", "converties", conv);
            s.legendValue = ch::fr::integer(sum(st));
            c.legendValue = ch::fr::integer(sum(conv));
            d.series = {s, c};
            d.partialFrom = 12;
            d.yTicks = {0, 2, 4};
            d.tipForward = true;
            d.barMax = 7.f;
            card(*b, "graphes.decouvertes", "D\xC3\xA9" "couvertes", "l'essai", 1, 1, 1, 1, tchart("graphes.decouvertes", d),
                 ch::fr::percent(sum(conv) * 100.0 / sum(st)) + " converties", {"+4 pts", 1, 1}, "");
        }
        {
            const std::vector<double> ref = {0, 0, 0, 190, 0, 0, 0, 0, 0, 490, 0, 0, 29};
            ch::TimeChartData d;
            d.kind = ch::Kind::Bars;
            d.x = weeks();
            d.xTicks = weekTicks();
            d.series = {serie("un", "remboursements", ref)};
            d.yTicks = {0, 250, 500};
            d.valueFormat = eurosF();
            d.partialFrom = 12;
            d.clickHint = "Clic : ces commandes";
            card(*b, "graphes.remboursements", "Remboursements", "", 2, 1, 1, 1, tchart("graphes.remboursements", d), ch::fr::euros(sum(ref)),
                 ch::deltaOf(sum(ref), 682, false), "3 commandes");
        }
        {
            // Un pas d'un mois : 12 mois (dec. 2025 -> nov. 2026), le dernier en cours.
            Rng g{37};
            std::vector<double> m, mp;
            for (int i = 0; i < 12; ++i) {
                m.push_back(std::round(g.in(6200, 9800) * (i == 11 ? 0.35 : 1.0)));
                mp.push_back(std::round(g.in(5600, 8800)));
            }
            ch::TimeChartData d;
            d.kind = ch::Kind::Bars;
            d.x = ch::monthlyAxis(2025, 12, 12);
            d.xTicks = {{0, 0}, {3, 0}, {6, 0}, {9, 0}};
            d.series = {serie("un", "2026", m), compare("l'an dernier", mp)};
            d.yFormat = [](double v) { return v ? ch::fr::compact(v) : std::string("0"); };
            d.valueFormat = eurosF();
            d.partialFrom = 11;
            d.yTickCount = 2;
            card(*b, "graphes.mois", "Montant par mois", "un pas d'un mois", 3, 1, 1, 1, tchart("graphes.mois", d), ch::fr::euros(sum(m)),
                 ch::deltaOf(sum(m), sum(mp)), "sur 12 mois");
        }
        return b;
    }

    // ========================================================= 3. LES MISES A JOUR (09) ==
    std::unique_ptr<Board> buildUpdates() {
        auto b = std::make_unique<Board>("graphes.maj", 4, std::vector<float>{1.25f, 1.f});
        {
            // L'adoption de chaque version, alignees sur leur jour de publication (J0).
            struct Ver {
                const char* name;
                double      top, k;
                int         days;
                const char* pub;
            };
            const Ver vers[] = {{"1.44.0", 77, 0.11, 35, "14/07"}, {"1.45.0", 87, 0.12, 42, "18/08"}, {"1.46.0", 94, 0.15, 36, "29/09"}, {"1.47.0", 92, 0.16, 6, "04/11"}};
            ch::TimeChartData d;
            for (int i = 0; i <= 44; ++i) {
                ch::Category c;
                c.label = i == 0 ? "J0" : "J+" + std::to_string(i);
                c.tip = i == 0 ? "Le jour de la publication" : std::to_string(i) + " jour" + (i > 1 ? "s" : "") + " apr\xC3\xA8s la publication";
                d.x.push_back(c);
            }
            d.xTicks = {{0, -1}, {7, 0}, {14, 0}, {21, 0}, {28, 0}, {35, 0}, {42, 0}};
            for (int v = 0; v < 4; ++v) {
                ch::Series s;
                s.key = std::string("version:") + vers[v].name;
                s.name = vers[v].name;
                s.legendValue = std::string("publi\xC3\xA9" "e le ") + vers[v].pub;
                s.color = ch::colors::ordinal(v, 4);
                for (int i = 0; i <= 44; ++i)
                    s.values.push_back(i <= vers[v].days ? std::round(vers[v].top * (1.0 - std::exp(-vers[v].k * (i + 0.6))) * 10.0) / 10.0 : ch::kNone);
                s.format = [](double x) { return ch::fr::percent(x) + " " + ch::fr::kDot + " " + ch::fr::integer(x * 2.86) + " PC"; };
                if (v < 3) {
                    s.endDot = true;
                    s.endLabel = std::string(vers[v].name) + " " + ch::fr::kDot + " " + ch::fr::percent(s.values[static_cast<std::size_t>(vers[v].days)]);
                } else {
                    s.endDot = true;
                }
                d.series.push_back(s);
            }
            d.notes.push_back({6, d.series[3].values[6], "1.47.0 " + std::string(ch::fr::kDot) + " " + ch::fr::percent(d.series[3].values[6]), ch::Note::Anchor::End, -12.f});
            d.yTicks = {0, 25, 50, 75, 100};
            d.yFormat = pctF();
            d.exampleTip = 6;
            d.legend = ch::LegendMode::Top;
            d.clickHint = "Clic : les PC encore en retard ce jour-l\xC3\xA0";
            card(*b, "graphes.adoption", "L'adoption de chaque version", "les PC qui l'ont install\xC3\xA9" "e, jour apr\xC3\xA8s jour", 0, 0, 2, 1, tchart("graphes.adoption", d));
        }
        {
            // La part des PC a jour, chaque jour (01/07 -> 10/11) : les Studios (une dent de scie) et les Admins (en escalier).
            const Day first = ch::fr::day(2026, 7, 1);
            const int n = 133;
            const int pubs[] = {13, 48, 62, 90, 126};   // 14/07, 18/08, 01/09 (1.45.2), 29/09, 04/11
            const char* names[] = {"1.44.0", "1.45.0", "1.45.2", "1.46.0", "1.47.0"};
            std::vector<double> st, ad;
            for (int i = 0; i < n; ++i) {
                int last = -100;
                for (const int p : pubs)
                    if (i >= p) last = p;
                const double t = i - last;
                st.push_back(last < 0 ? 84.0 : std::round(4.0 + 90.0 * (1.0 - std::exp(-0.1 * t))));
                const double a = last < 0 ? 100.0 : (t < 2 ? 33.3 : t < 9 ? 66.7 : 100.0);
                ad.push_back(std::round(a));
            }
            ch::TimeChartData d;
            d.x = ch::dailyAxis(first, n);
            d.xTicks = {{0, -1}, {31, 0}, {62, 0}, {92, 0}, {123, 0}};
            ch::Series s = serie("un", "Studios", st), a = serie("deux", "Admins", ad);
            s.legendValue = "59 %";
            a.legendValue = "67 %";
            a.step = true;
            a.width = 1.75f;
            a.format = [](double v) { return ch::fr::integer(v * 3.0 / 100.0) + " sur 3"; };
            s.format = pctF();
            d.series = {s, a};
            d.tipForward = true;
            for (int i = 0; i < 5; ++i) d.markers.push_back({static_cast<double>(pubs[i]), names[i], ch::kNone, {0, 0, 0, 0}, false});
            d.yTicks = {0, 25, 50, 75, 100};
            d.yFormat = pctF();
            d.clickHint = "Clic : les PC pas \xC3\xA0 jour ce jour-l\xC3\xA0";
            card(*b, "graphes.ajour", "Les PC \xC3\xA0 jour", "sur la derni\xC3\xA8re version, chaque jour", 2, 0, 2, 1, tchart("graphes.ajour", d));
        }
        {
            ch::BarListData d;
            d.mode = ch::BarMode::Grouped;
            d.partKeys = {"un", "deux"};
            d.partNames = {"m\xC3\xA9" "dian", "moyen"};
            d.format = [](double v) { return ch::fr::decimal(v, 1) + ch::fr::kNbsp + "j"; };
            d.axis = true;
            d.ticks = {0, 5, 10, 15};
            d.rows = {{"", "1.44.0", "", 0, {9, 14}}, {"", "1.45.0", "", 0, {6, 11}}, {"", "1.46.0", "", 0, {4, 8}}, {"", "1.47.0", "en cours", 0, {2, 2.6}}};
            d.clickHint = "Clic : les PC de cette version";
            card(*b, "graphes.delaimaj", "D\xC3\xA9lai avant la mise \xC3\xA0 jour", "", 0, 1, 1, 1, barList("graphes.delaimaj", d));
        }
        {
            ch::BarListData d;
            d.mode = ch::BarMode::Track;
            d.maxValue = 100;
            d.format = pctF();
            const char* v[] = {"1.47.0", "1.46.0", "1.45.2", "1.45.0", "1.44.0", "1.43.2 et avant"};
            const double p[] = {59, 28, 7, 1, 3, 2};
            const char* pc[] = {"168 PC", "81 PC", "19 PC", "4 PC", "8 PC", "6 PC"};
            for (int i = 0; i < 6; ++i) {
                ch::BarRow r;
                r.label = v[i];
                r.value = p[i];
                r.extra = pc[i];
                r.color = ch::colors::ordinal(5 - i, 6);
                d.rows.push_back(r);
            }
            d.rowHeight = 24.f;
            d.clickHint = "Clic : ces PC (Activations)";
            card(*b, "graphes.part", "La part des PC", "par version, aujourd'hui", 1, 1, 1, 1, barList("graphes.part", d));
        }
        {
            // Les versions installees, semaine apres semaine (20 semaines) : empilees, de la plus ancienne a la plus recente.
            const int n = 20;
            const int pub[] = {2, 7, 13, 18};
            std::vector<std::vector<double>> v(5, std::vector<double>(n, 0.0));
            for (int i = 0; i < n; ++i) {
                double share[5] = {0.25, 0.75, 0, 0, 0};
                if (i >= pub[0]) share[0] = 0.12, share[1] = std::min(0.88, 0.2 + 0.12 * (i - pub[0])), share[2] = 0;
                if (i >= pub[1]) share[1] = 0.1, share[2] = std::min(0.78, 0.2 + 0.13 * (i - pub[1]));
                if (i >= pub[2]) share[2] = 0.08, share[3] = std::min(0.8, 0.25 + 0.14 * (i - pub[2]));
                if (i >= pub[3]) share[3] = 0.3, share[4] = std::min(0.6, 0.35 + 0.2 * (i - pub[3]));
                double tot = 0;
                for (const double s : share) tot += s;
                for (int k = 0; k < 5; ++k) v[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)] = std::round(286.0 * share[k] / tot);
            }
            ch::TimeChartData d;
            d.kind = ch::Kind::Stacked;
            d.x = ch::weeklyAxis(ch::fr::day(2026, 6, 29), n);
            d.xTicks = {{0, 0}, {6, 0}, {13, 0}, {19, 0}};
            const char* names[] = {"1.43 et avant", "1.44", "1.45", "1.46", "1.47"};
            for (int k = 0; k < 5; ++k) {
                ch::Series s = serie(std::string("version:") + names[k], names[k], v[static_cast<std::size_t>(k)]);
                s.color = k == 0 ? ch::colors::kCompare : ch::colors::ordinal(k - 1, 4);
                d.series.push_back(s);
            }
            d.markers = {{2, "1.44.0", ch::kNone, {0, 0, 0, 0}, false}, {7, "1.45.0", ch::kNone, {0, 0, 0, 0}, false},
                         {13, "1.46.0", ch::kNone, {0, 0, 0, 0}, false}, {18, "1.47.0", ch::kNone, {0, 0, 0, 0}, false}};
            d.yTickCount = 3;
            d.clickHint = "Clic : ces PC";
            card(*b, "graphes.versions", "Les versions install\xC3\xA9" "es", "chaque semaine, tous les PC", 2, 1, 2, 1, tchart("graphes.versions", d));
        }
        return b;
    }

    // ============================================================ 4. UN CLIENT (10) ==
    std::unique_ptr<Board> buildClient() {
        auto b = std::make_unique<Board>("graphes.client", 4, std::vector<float>{1.15f, 1.f, 1.f});
        using ch::fr::day;
        {
            ch::TimelineData d;
            d.from = day(2024, 1, 1);
            d.to = day(2027, 12, 31);
            d.today = day(2026, 11, 10);
            d.legend = {{"PROFESSIONAL", ch::colors::kProfessional, ch::Swatch::Box, {}}, {"PERSONAL", ch::colors::kPersonal, ch::Swatch::Box, {}},
                        {"D\xC3\xA9" "couverte", ch::colors::kDiscovery, ch::Swatch::Box, {}}, {"Options", ch::colors::kOptions, ch::Swatch::Box, {}},
                        {"aujourd'hui", ch::colors::ink(ui::Theme::xpg()).ink, ch::Swatch::Line, {}}};
            ch::TimelineRow a;
            a.label = "XPG-2024-00031";
            a.sub = "5 PC";
            a.segments = {{day(2024, 3, 11), day(2025, 3, 11), "offre:PROFESSIONAL", "sign\xC3\xA9" "e"}, {day(2025, 3, 11), day(2026, 3, 11), "offre:PROFESSIONAL", "prolong\xC3\xA9" "e"},
                          {day(2026, 3, 11), day(2027, 3, 11), "offre:PROFESSIONAL", "prolong\xC3\xA9" "e"}};
            a.end = "11/03/2027";
            ch::TimelineRow o;
            o.label = "Exports avanc\xC3\xA9s";
            o.sub = "option \xC2\xB7 00031";
            o.segments = {{day(2026, 3, 14), day(2027, 3, 14), "offre:OPTIONS", "ajout\xC3\xA9" "e"}};
            o.end = "14/03/2027";
            ch::TimelineRow c;
            c.label = "XPG-2025-00188";
            c.sub = "2 PC";
            c.segments = {{day(2024, 11, 5), day(2024, 12, 5), "offre:DECOUVERTE", "D\xC3\xA9" "couverte"}, {day(2024, 12, 5), day(2025, 12, 5), "offre:PERSONAL", "sign\xC3\xA9" "e"},
                          {day(2025, 12, 5), day(2026, 12, 5), "offre:PERSONAL", "prolong\xC3\xA9" "e"}};
            c.end = "05/12/2026 \xC2\xB7 dans 25 j";
            c.warn = true;
            ch::TimelineRow n;
            n.label = "XPG-2026-00412";
            n.sub = "3 PC";
            n.segments = {{day(2026, 11, 9), day(2027, 11, 9), "offre:PROFESSIONAL", "sign\xC3\xA9" "e par L\xC3\xA9" "a Essai"}};
            n.lead = "nouvelle";
            n.end = "09/11/2027";
            ch::TimelineRow x;
            x.label = "XPG-2024-00102";
            x.sub = "1 PC";
            x.segments = {{day(2024, 6, 19), day(2025, 6, 19), "offre:PERSONAL", "sign\xC3\xA9" "e"}};
            x.end = "expir\xC3\xA9" "e le 19/06/25";
            x.faded = true;
            d.rows = {a, o, c, n, x};
            d.clickHint = "Clic : la licence";
            auto t = std::make_unique<ch::Timeline>("graphes.frise");
            t->setData(d);
            card(*b, "graphes.frise", "Ses licences et leurs \xC3\xA9" "ch\xC3\xA9" "ances", "chaque p\xC3\xA9riode : sign\xC3\xA9" "e, prolong\xC3\xA9" "e", 0, 0, 3, 1, std::move(t));
        }
        {
            ch::BarListData d;
            d.mode = ch::BarMode::Ranking;
            d.rows = {{"admin:0", "Camille Essai", "la 1re licence, 11 demandes", 23, {}, "", "70 %"},
                      {"admin:1", "L\xC3\xA9" "a Essai", "la nouvelle licence, 4 demandes", 7, {}, "", "21 %"},
                      {"admin:2", "Hugo Essai", "2 demandes, en octobre", 3, {}, "", "9 %"}};
            d.clickHint = "Clic : ses lignes du journal";
            card(*b, "graphes.servi", "Qui l'a servi", "depuis le d\xC3\xA9" "but", 3, 0, 1, 1, barList("graphes.servi", d), "33 actions", {}, "dans le journal");
        }
        {
            // Ses PC actifs a la fin de chaque mois (mars 2024 -> nov. 2026) et les places achetees (en escalier).
            std::vector<double> pc, places;
            const int n = 33;
            for (int i = 0; i < n; ++i) {
                places.push_back(i < 8 ? 5 : i < 20 ? 7 : i < 32 ? 7 : 10);
                pc.push_back(i < 1 ? 3 : i < 3 ? 5 : i < 8 ? 6 : i < 14 ? 7 : i < 19 ? 8 : i < 32 ? 7 : 9);
            }
            ch::TimeChartData d;
            d.x = ch::monthlyAxis(2024, 3, n);
            for (int i = 0; i < n; ++i) {
                int y = 2024 + (2 + i) / 12, m = (2 + i) % 12 + 1;
                d.x[static_cast<std::size_t>(i)].label = (m == 1 || i == 0) ? ch::fr::monthShort(m) + " " + std::to_string(y) : ch::fr::monthShort(m);
            }
            d.xTicks = {{0, -1}, {10, 0}, {22, 0}, {32, 1}};
            ch::Series s = serie("un", "PC actifs", pc), p = serie("places", "places achet\xC3\xA9" "es (ses licences)", places);
            s.step = s.area = s.endDot = true;
            p.step = true;
            p.width = 1.25f;
            p.color = Color::rgb(0x64748B);
            d.series = {s, p};
            d.tipForward = true;
            d.yTicks = {0, 5, 10};
            d.clickHint = "Clic : ses PC ce mois-l\xC3\xA0";
            card(*b, "graphes.sespc", "Ses PC actifs", "\xC3\xA0 la fin de chaque mois", 0, 1, 2, 1, tchart("graphes.sespc", d), "9", {"+2 aujourd'hui", 1, 1}, "sur 10 places");
        }
        {
            const int n = 20;
            const int pub[] = {2, 7, 13, 18};
            std::vector<std::vector<double>> v(5, std::vector<double>(n, 0.0));
            for (int i = 0; i < n; ++i) {
                int c[5] = {2, 7, 0, 0, 0};
                if (i >= pub[0]) c[0] = 1, c[1] = i >= pub[0] + 1 ? 8 : 4, c[2] = 0;
                if (i >= pub[0] && i < pub[0] + 1) c[0] = 5, c[1] = 4;
                if (i >= pub[1]) c[1] = 2, c[2] = 7, c[0] = 0;
                if (i >= pub[2]) c[2] = 1, c[3] = 8, c[1] = 0;
                if (i >= pub[3]) c[3] = 2, c[4] = 7, c[2] = 0;
                for (int k = 0; k < 5; ++k) v[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)] = c[k];
            }
            ch::TimeChartData d;
            d.kind = ch::Kind::Stacked;
            d.x = ch::weeklyAxis(ch::fr::day(2026, 6, 29), n);
            d.xTicks = {{0, 0}, {6, 0}, {13, 0}, {19, 0}};
            const char* names[] = {"1.43 et avant", "1.44", "1.45", "1.46", "1.47"};
            for (int k = 0; k < 5; ++k) {
                ch::Series s = serie(std::string("version:") + names[k], names[k], v[static_cast<std::size_t>(k)]);
                s.color = k == 0 ? ch::colors::kCompare : ch::colors::ordinal(k - 1, 4);
                d.series.push_back(s);
            }
            d.markers = {{2, "1.44.0", ch::kNone, {0, 0, 0, 0}, false}, {7, "1.45.0", ch::kNone, {0, 0, 0, 0}, false},
                         {13, "1.46.0", ch::kNone, {0, 0, 0, 0}, false}, {18, "1.47.0", ch::kNone, {0, 0, 0, 0}, false}};
            d.yTicks = {0, 3, 6, 9};
            d.clickHint = "Clic : ses PC";
            card(*b, "graphes.sesmaj", "Ses mises \xC3\xA0 jour", "la version de ses PC, chaque semaine", 2, 1, 2, 1, tchart("graphes.sesmaj", d), "6 sur 9 \xC3\xA0 jour", {},
                 "ATELIER-PC3 : 1.43.2");
        }
        {
            // Ses achats par trimestre, et le montant cumule (deux graphiques, jamais deux axes).
            std::vector<ch::Category> q;
            std::vector<std::pair<int, int>> qt;
            for (int i = 0; i < 12; ++i) {
                const int y = 2024 + i / 4, t = i % 4 + 1;
                q.push_back({t == 1 ? std::to_string(y) : "T" + std::to_string(t), std::string(t == 1 ? "1er" : t == 2 ? "2e" : t == 3 ? "3e" : "4e") + " trimestre " + std::to_string(y), false});
                if (t == 1) qt.push_back({i, 0});
            }
            const std::vector<double> buy = {490, 120, 0, 490, 120, 0, 0, 0, 120, 0, 1010, 470};
            std::vector<double> cum;
            double a = 0;
            for (const double v : buy) cum.push_back(a += v);
            ch::TimeChartData d;
            d.kind = ch::Kind::Bars;
            d.x = q;
            d.xTicks = qt;
            d.series = {serie("un", "commandes", buy)};
            d.series[0].zeroInTip = true;
            d.yTicks = {0, 500, 1000};
            d.valueFormat = eurosF();
            d.clickHint = "Clic : ses commandes";
            card(*b, "graphes.achats", "Ses achats", "par trimestre", 0, 2, 1, 1, tchart("graphes.achats", d), ch::fr::euros(sum(buy)), {}, "8 commandes");
            ch::TimeChartData c;
            c.x = q;
            c.xTicks = qt;
            c.lineInBands = true;
            ch::Series s = serie("un", "cumul", cum);
            s.step = s.area = s.endDot = true;
            c.series = {s};
            c.yTicks = {0, 1500, 3000};
            c.yFormat = [](double v) { return v ? ch::fr::compact(v) : std::string("0"); };
            c.valueFormat = eurosF();
            c.clickHint = "Clic : ses commandes jusque-l\xC3\xA0";
            card(*b, "graphes.cumulclient", "Le montant cumul\xC3\xA9", "", 1, 2, 1, 1, tchart("graphes.cumulclient", c), ch::fr::euros(cum.back()));
        }
        {
            std::vector<ch::Category> q;
            std::vector<std::pair<int, int>> qt;
            for (int i = 0; i < 12; ++i) {
                const int y = 2024 + i / 4, t = i % 4 + 1;
                q.push_back({t == 1 ? std::to_string(y) : "T" + std::to_string(t), "trimestre " + std::to_string(t) + " de " + std::to_string(y), false});
                if (t == 1) qt.push_back({i, 0});
            }
            ch::TimeChartData d;
            d.kind = ch::Kind::Stacked;
            d.x = q;
            d.xTicks = qt;
            ch::Series c = serie("admin:0", "Camille", {1, 2, 0, 2, 1, 1, 0, 0, 1, 2, 0, 1});
            ch::Series l = serie("admin:1", "L\xC3\xA9" "a", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3});
            ch::Series h = serie("admin:2", "Hugo", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0});
            c.legendValue = "11";
            l.legendValue = "4";
            h.legendValue = "2";
            d.series = {c, l, h};
            d.yTicks = {0, 2, 4, 6};
            d.clickHint = "Clic : ses demandes";
            card(*b, "graphes.sesdemandes", "Ses demandes", "par trimestre, et qui les a trait\xC3\xA9" "es", 2, 2, 2, 1, tchart("graphes.sesdemandes", d), "17", {},
                 "0 en attente \xC2\xB7 r\xC3\xA9ponse en 2 h 50");
        }
        return b;
    }

    // ===================================================== 5. UN ADMIN (02) : LEA ESSAI ==
    // Les chiffres de graphes.py (la maquette), les memes partout.
    static std::vector<double> leaDaily() { return {6, 5, 4, 6, 3, 0, 0, 0, 0, 7, 5, 4, 0, 2, 5, 4, 6, 5, 19, 0, 0, 6, 5, 4, 6, 3, 0, 0, 7, 5}; }
    static std::vector<double> leaDailyBefore() { return {5, 4, 5, 4, 3, 0, 0, 5, 5, 4, 4, 3, 0, 0, 5, 5, 4, 5, 4, 0, 0, 5, 5, 4, 4, 3, 0, 0, 5, 5}; }

    std::unique_ptr<Board> buildAdmin() {
        auto b = std::make_unique<Board>("graphes.admin", 4, std::vector<float>{0.5f, 1.15f, 1.f});
        const Color lea = ch::colors::admin(1);
        b->put(std::make_unique<Tile>("graphes.tuile.actions", "Actions", "117", ch::deltaOf(117, 96), "96 avant", std::vector<double>{0, 0, 0, 0, 9, 18, 22, 25, 24, 18, 39, 24}, lea), 0, 0);
        b->put(std::make_unique<Tile>("graphes.tuile.licences", "Licences sign\xC3\xA9" "es", "41", ch::Delta{"+7", 1, 1}, "34 avant", std::vector<double>{0, 0, 0, 0, 3, 6, 7, 8, 9, 6, 15, 11}, lea), 1, 0);
        b->put(std::make_unique<Tile>("graphes.tuile.demandes", "Demandes trait\xC3\xA9" "es", "34", ch::Delta{"+4", 1, 1}, "85 % accept\xC3\xA9" "es", std::vector<double>{0, 0, 0, 0, 2, 5, 7, 8, 9, 4, 11, 10}, lea), 2, 0);
        b->put(std::make_unique<Tile>("graphes.tuile.montant", "Montant encaiss\xC3\xA9", ch::fr::euros(4870), ch::deltaOf(4870, 3720), "22 commandes",
                                      std::vector<double>{0, 0, 0, 0, 380, 610, 980, 1130, 1240, 690, 1650, 1290}, lea), 3, 0);
        {
            ch::TimeChartData d;
            d.kind = ch::Kind::Bars;
            d.x = ch::dailyAxis(ch::fr::day(2026, 10, 12), 30, true);
            d.x.front().label = "lun. 12/10";
            d.x.back().label = "auj.";
            d.xTicks = {{0, 0}, {7, 0}, {14, 0}, {21, 0}, {29, 0}};
            ch::Series s = serie("admin:1", "cette p\xC3\xA9riode", leaDaily());
            s.legendValue = "117";
            ch::Series p = compare("avant", leaDailyBefore());
            p.legendValue = "96";
            d.series = {s, p};
            d.yTicks = {0, 5, 10, 15, 20};
            d.barFraction = 0.6f;
            d.notes.push_back({18, 19, "19 actions \xC2\xB7 3,4\xC3\x97 sa moyenne", ch::Note::Anchor::End, -6.f});
            d.clickHint = "Clic : ce jour dans son journal";
            card(*b, "graphes.parjour", "Ses actions par jour", "du 12/10 au 10/11 \xC2\xB7 fins de semaine en clair", 0, 1, 2, 1, tchart("graphes.parjour", d));
        }
        {
            const char* types[] = {"Cr\xC3\xA9" "er et signer une licence", "Prolonger une licence", "Ajouter une option", "R\xC3\xA9voquer une licence", "Retirer un PC",
                                   "Modifier les offres", "Cr\xC3\xA9" "er, modifier une option", "Annonces", "Accepter, refuser une demande", "Revoir le code",
                                   "Supprimer un client", "Exporter les clients", "\xC3\x89" "crire aux clients", "Boutique : livrer, revoir"};
            const double v[] = {19, 9, 7, 2, 0, 0, 0, 0, 34, 6, 2, 2, 14, 22};
            const int right[] = {1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1};
            ch::BarListData d;
            d.mode = ch::BarMode::Ranking;
            d.color = lea;
            d.rowHeight = 22.f;
            for (int i = 0; i < 14; ++i) {
                ch::BarRow r;
                r.label = types[i];
                r.value = v[i];
                if (!right[i] && v[i] == 0) {
                    r.off = true;
                    r.note = "pas ce droit";
                } else if (v[i] == 0) {
                    r.warn = true;
                    r.note = "jamais (90 j)";
                }
                if (i == 1) r.tip = "9 avant le 02/11, le jour o\xC3\xB9 ce droit lui a \xC3\xA9t\xC3\xA9 retir\xC3\xA9";
                d.rows.push_back(r);
            }
            d.clickHint = "Clic : ces lignes du journal";
            card(*b, "graphes.partype", "Ses actions par type", "les 13 droits, puis la boutique", 2, 1, 1, 2, barList("graphes.partype", d));
        }
        {
            ch::BarListData d;
            d.mode = ch::BarMode::Share;
            d.partKeys = {"crit:0", "crit:1", "crit:2", "crit:3"};
            d.partNames = {"Info 71", "Moyenne 33", "Haute 11", "Critique 2"};
            d.legend = ch::LegendMode::Top;
            ch::BarRow r;
            r.label = "";
            r.parts = {71, 33, 11, 2};
            r.valueText = "117";
            d.rows = {r};
            d.barThickness = 12.f;
            d.rowHeight = 30.f;
            d.clickHint = "Clic : ces lignes du journal";
            card(*b, "graphes.criticite", "Par criticit\xC3\xA9", "ses 117 actions de la p\xC3\xA9riode", 3, 1, 1, 1, barList("graphes.criticite", d));
        }
        {
            ch::BarListData d;
            d.mode = ch::BarMode::Track;
            d.color = lea;
            d.maxValue = 100;
            d.format = pctF();
            d.rows = {{"", "Actions", "", 44, {}, "", "1re sur 4"}, {"", "Licences sign\xC3\xA9" "es", "", 54, {}, "", "1re"}, {"", "Demandes trait\xC3\xA9" "es", "", 47, {}, "", "1re"},
                      {"", "Montant encaiss\xC3\xA9", "", 56, {}, "", "1re"}};
            d.rowHeight = 30.f;
            card(*b, "graphes.parmi", "Elle parmi les admins", "sa part des 4 admins", 0, 2, 2, 1, barList("graphes.parmi", d));
        }
        {
            ch::BarListData d;
            d.mode = ch::BarMode::Ranking;
            d.color = lea;
            d.rows = {{"offre:PERSONAL", "PERSONAL", "", 15}, {"offre:PROFESSIONAL", "PROFESSIONAL", "", 20}, {"offre:ENTERPRISE", "ENTERPRISE", "", 6}};
            for (auto& r : d.rows) r.key.clear();   // une seule serie : une seule teinte (la sienne)
            d.rowHeight = 26.f;
            card(*b, "graphes.paroffre", "Ses licences par offre", "", 3, 2, 1, 1, barList("graphes.paroffre", d), "41", ch::Delta{"+7", 1, 1}, "34 avant");
        }
        return b;
    }

    // ============================================ 6. SON ACTIVITE, CE QU'ELLE PRODUIT (03, 04) ==
    std::unique_ptr<Board> buildActivity() {
        auto b = std::make_unique<Board>("graphes.activite", 4, std::vector<float>{0.62f, 0.95f, 1.15f});
        const Color lea = ch::colors::admin(1);
        {
            // La carte jour x heure (les chiffres de graphes.py : 117 actions, deux heures inhabituelles).
            std::vector<std::vector<double>> m(7, std::vector<double>(24, 0.0));
            const double tot[5] = {24, 19, 21, 22, 29};
            const double w[24] = {0, 0, 0, 0, 0, 0, 0, 0.3, 1.4, 3.6, 4.4, 3.8, 1.0, 1.2, 3.6, 3.8, 3.0, 1.6, 0.4, 0, 0, 0, 0, 0};
            double sw = 0;
            for (const double x : w) sw += x;
            for (int dd = 0; dd < 5; ++dd) {
                const double t = tot[dd] - (dd == 2 ? 1 : 0);
                double placed = 0;
                std::vector<std::pair<double, int>> rest;
                for (int h = 0; h < 24; ++h) {
                    const double raw = t * w[h] / sw;
                    m[static_cast<std::size_t>(dd)][static_cast<std::size_t>(h)] = std::floor(raw);
                    placed += std::floor(raw);
                    rest.push_back({raw - std::floor(raw), h});
                }
                std::sort(rest.begin(), rest.end(), [](auto a, auto c) { return a.first > c.first; });
                for (int j = 0; j < static_cast<int>(t - placed); ++j) m[static_cast<std::size_t>(dd)][static_cast<std::size_t>(rest[static_cast<std::size_t>(j)].second)] += 1;
            }
            m[2][21] = 1;
            m[6][22] = 2;
            ch::HeatMapData d;
            d.rowLabels = {"lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."};
            for (int h = 0; h < 24; ++h) d.colLabels.push_back(std::to_string(h) + ch::fr::kNbsp + "h");
            d.cells = m;
            d.flagged = {{2, 21}, {6, 22}};
            d.bandFrom = 8.67;
            d.bandTo = 17.83;
            d.bandLabel = "ses heures habituelles : 8 h 40 \xE2\x86\x92 17 h 50";
            d.scaleLabel = "actions par heure :";
            d.flagLabel = "heure inhabituelle pour elle (signal\xC3\xA9" "e)";
            d.valueUnit = "action";
            d.cellTitle = [](int r, int c) {
                static const char* const days[] = {"lundis", "mardis", "mercredis", "jeudis", "vendredis", "samedis", "dimanches"};
                return std::string("Les ") + days[r % 7] + ", de " + std::to_string(c) + ch::fr::kNbsp + "h \xC3\xA0 " + std::to_string(c + 1) + ch::fr::kNbsp + "h";
            };
            d.clickHint = "Clic : ces actions dans son journal";
            auto h = std::make_unique<ch::HeatMap>("graphes.carte");
            h->setData(d);
            card(*b, "graphes.carte", "Quand elle travaille", "ses 117 actions, par jour de la semaine et par heure \xC2\xB7 du 12/10 au 10/11", 0, 0, 2, 2, std::move(h));
        }
        {
            ch::DayStripData d;
            d.first = ch::fr::day(2026, 10, 12);
            d.values = leaDaily();
            d.streakFrom = 21;
            d.streakTo = 29;
            d.streakColor = lea;
            d.flagged = {13};
            d.clickHint = "Clic : ce jour dans son journal";
            auto s = std::make_unique<ch::DayStrip>("graphes.jours");
            s->setData(d);
            card(*b, "graphes.jours", "Ses jours", "un carr\xC3\xA9 par jour, plus fonc\xC3\xA9 = plus d'actions ; la s\xC3\xA9rie en cours soulign\xC3\xA9" "e", 2, 0, 2, 1, std::move(s), "21",
                 ch::Delta{"+2", 1, 1}, "jours actifs sur 30 \xC2\xB7 s\xC3\xA9rie en cours : 7 jours ouvr\xC3\xA9s");
        }
        {
            const double R[30][2] = {{8.7, 17.6}, {8.5, 18.0}, {9.1, 17.2}, {8.6, 17.9}, {8.9, 16.4}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {9.0, 21.05},
                                     {8.8, 17.5}, {8.6, 17.1}, {-1, -1}, {22.2, 22.4}, {8.7, 17.8}, {8.9, 17.4}, {8.4, 18.1}, {8.6, 17.7}, {8.2, 19.7}, {-1, -1},
                                     {-1, -1}, {8.7, 17.9}, {8.8, 17.6}, {8.6, 17.3}, {8.5, 18.0}, {9.0, 16.2}, {-1, -1}, {-1, -1}, {8.6, 17.8}, {8.4, 11.7}};
            ch::TimeChartData d;
            d.kind = ch::Kind::Range;
            d.x = ch::dailyAxis(ch::fr::day(2026, 10, 12), 30);
            d.x.back().label = "auj.";
            d.xTicks = {{0, 0}, {7, 0}, {14, 0}, {21, 0}, {29, 0}};
            ch::Series a = serie("admin:1", "premi\xC3\xA8re \xE2\x86\x92 derni\xC3\xA8re action", {}), e = serie("admin:1", "fin", {});
            for (int i = 0; i < 30; ++i) {
                a.values.push_back(R[i][0] < 0 ? ch::kNone : R[i][0]);
                e.values.push_back(R[i][1] < 0 ? ch::kNone : R[i][1]);
                a.pointColors.push_back(R[i][1] > 20 || R[i][0] > 20 ? ch::colors::kBad : Color{0, 0, 0, 0});
            }
            a.format = [](double v) { return ch::fr::clock(v); };
            d.series = {a, e};
            d.yMin = 6;
            d.yMax = 24;
            d.yTicks = {6, 12, 18, 24};
            d.yFormat = [](double v) { return ch::fr::integer(v) + ch::fr::kNbsp + "h"; };
            d.yBands.push_back({8.67, 17.83, "habituel", {0, 0, 0, 0}});
            d.notes.push_back({13, 22.4, "dim. 25/10, 22 h 14", ch::Note::Anchor::Start, -8.f});
            d.notes.push_back({9, 21.05, "21 h 03", ch::Note::Anchor::Middle, -10.f});
            d.legend = ch::LegendMode::None;
            d.clickHint = "Clic : ce jour dans son journal";
            card(*b, "graphes.journees", "D\xC3\xA9" "but et fin de ses journ\xC3\xA9" "es", "premi\xC3\xA8re \xE2\x86\x92 derni\xC3\xA8re action ; en rouge : inhabituel (une alerte)", 2, 1, 2, 1,
                 tchart("graphes.journees", d));
        }
        {
            const std::vector<double> mine = {0, 0, 0, 0, 3, 6, 7, 8, 9, 6, 15, 11};
            const std::vector<double> others = {14, 12, 15, 13, 11, 9, 10, 9, 8, 9, 10, 8};
            ch::TimeChartData d;
            d.kind = ch::Kind::Stacked;
            d.x = ch::weeklyAxis(ch::fr::day(2026, 8, 17), 12);
            d.xTicks = {{0, 0}, {4, 0}, {8, 0}, {11, 0}};
            d.series = {serie("admin:1", "L\xC3\xA9" "a Essai", mine), serie("autres", "les autres admins", others)};
            d.shades.push_back({0, 3, "pas encore admin", {0, 0, 0, 0}});
            d.yTicks = {0, 10, 20, 30};
            d.barFraction = 0.58f;
            d.clickHint = "Clic : ses licences de la semaine";
            card(*b, "graphes.semaines", "Ses licences, semaine apr\xC3\xA8s semaine", "12 semaines \xC2\xB7 sa part parmi tous les admins", 0, 2, 2, 1, tchart("graphes.semaines", d));
        }
        {
            // Le delai entre la demande et sa reponse : 34 demandes (mediane 1 h 10, moyenne 4 h 05, max 2 j 6 h).
            std::vector<double> s = {0.1, 0.2, 0.25, 0.3, 0.3, 0.4, 0.4, 0.45, 0.45, 0.6, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 0.98, 1.1, 1.23, 1.4, 1.6, 1.8, 1.9,
                                     2.2, 2.6, 3.1, 3.5, 3.9, 4.5, 5.2, 6.8, 9.5, 16.0, 54.0};
            const std::vector<double> edges = {0, 0.5, 1, 2, 4, 8, 24, 48, 1e9};
            const std::vector<std::string> labels = {"< \xC2\xBD h", "\xC2\xBD\xE2\x80\x93" "1 h", "1\xE2\x80\x93" "2 h", "2\xE2\x80\x93" "4 h", "4\xE2\x80\x93" "8 h", "8\xE2\x80\x93" "24 h", "1\xE2\x80\x93" "2 j", "> 2 j"};
            ch::TimeChartData d = ch::makeDistribution(s, edges, labels, lea, "demandes", intF());
            d.yTicks = {0, 5, 10};
            d.clickHint = "Clic : ces demandes";
            card(*b, "graphes.distribution", "Le d\xC3\xA9lai de ses r\xC3\xA9ponses", "34 demandes \xC2\xB7 la m\xC3\xA9" "diane, la moyenne, le max", 2, 2, 1, 1, tchart("graphes.distribution", d));
        }
        {
            ch::BarListData d;
            d.mode = ch::BarMode::Ranking;
            d.color = lea;
            d.format = eurosF();
            d.rows = {{"", "Bureau d'\xC3\xA9tudes Exemple", "", 1470, {}, "", "5 lic."}, {"", "Client d'essai", "", 1190, {}, "", "4 lic."},
                      {"", "Studio Exemple", "", 780, {}, "", "3 lic."}, {"", "Laboratoire d'essai", "", 560, {}, "", "3 lic."},
                      {"", "Usine Exemple Nord", "", 390, {}, "", "2 lic."}};
            d.clickHint = "Clic : la fiche du client";
            card(*b, "graphes.clients", "Ses clients", "les 5 premiers", 3, 2, 1, 1, barList("graphes.clients", d), "27", ch::Delta{"+5", 1, 1}, "clients servis");
        }
        return b;
    }

    // ========================================================== 7. QUI FAIT QUOI (05) ==
    std::unique_ptr<Board> buildWhoDoesWhat() {
        auto b = std::make_unique<Board>("graphes.quifait", 4, std::vector<float>{1.f});
        {
            const char* types[] = {"Cr\xC3\xA9" "er et signer une licence", "Prolonger une licence", "Ajouter une option", "R\xC3\xA9voquer une licence", "Retirer un PC",
                                   "Modifier les offres", "Cr\xC3\xA9" "er, modifier une option", "Annonces", "Accepter, refuser une demande", "Revoir le code",
                                   "Supprimer un client", "Exporter les clients", "\xC3\x89" "crire aux clients", "Boutique : livrer, revoir"};
            const double M[4][14] = {{8, 6, 3, 3, 2, 4, 3, 2, 9, 4, 1, 3, 6, 12},
                                     {19, 9, 7, 2, 0, 0, 0, 0, 34, 6, 2, 2, 14, 22},
                                     {6, 5, 2, 1, 1, 0, 0, 0, 21, 3, 0, 0, 4, 5},
                                     {4, 2, 1, 0, 0, 0, 0, 0, 8, 2, 0, 0, 1, 0}};
            const double prev[14] = {40, 29, 10, 4, 5, 2, 1, 3, 85, 15, 2, 4, 24, 33};
            const char* who[4] = {"Camille", "L\xC3\xA9" "a", "Hugo", "Nina"};
            ch::BarListData d;
            d.mode = ch::BarMode::Share;
            d.partKeys = {"admin:0", "admin:1", "admin:2", "admin:3"};
            d.partNames = {"Camille Essai (toi)", "L\xC3\xA9" "a Essai", "Hugo Essai", "Nina Essai (retir\xC3\xA9" "e le 28/10)"};
            d.rowHeight = 25.f;
            const auto lead = [&](const std::vector<double>& parts) {
                std::size_t best = 0;
                int count = 0;
                double tot = 0;
                for (std::size_t k = 0; k < parts.size(); ++k) {
                    tot += parts[k];
                    if (parts[k] > 0) ++count;
                    if (parts[k] > parts[best]) best = k;
                }
                return count == 1 ? std::string(who[best]) + " seule" : std::string(who[best]) + " " + ch::fr::percent(parts[best] * 100.0 / tot);
            };
            ch::BarRow all;
            all.label = "Toutes (hors r\xC3\xA9serv\xC3\xA9" "es)";
            all.strong = true;
            double prevAll = 0;
            for (int k = 0; k < 4; ++k) {
                double s = 0;
                for (int t = 0; t < 14; ++t) s += M[k][t];
                all.parts.push_back(s);
            }
            for (const double p : prev) prevAll += p;
            all.extra = ch::fr::deltaPercent(sum(all.parts), prevAll) + "   " + lead(all.parts);
            d.rows.push_back(all);
            for (int t = 0; t < 14; ++t) {
                ch::BarRow r;
                r.label = types[t];
                for (int k = 0; k < 4; ++k) r.parts.push_back(M[k][t]);
                const double tot = sum(r.parts);
                const std::string ecart = prev[t] >= 10 ? ch::fr::deltaPercent(tot, prev[t]) : ch::fr::signedInteger(tot - prev[t]);
                r.extra = ecart + "   " + lead(r.parts);
                d.rows.push_back(r);
            }
            d.clickHint = "Clic : ces lignes dans le journal";
            card(*b, "graphes.part2", "La part de chacun, par type d'action", "une barre par type (100 %), les admins dans le m\xC3\xAAme ordre, la m\xC3\xAAme couleur", 0, 0, 2, 1,
                 barList("graphes.partchacun", d));
        }
        {
            // Cette periode et la periode d'avant, par admin : quatre petits graphiques, chacun son echelle.
            auto sm = std::make_unique<ch::SmallMultiples>("graphes.multiples", 2);
            const auto add = [&](const char* title, const std::string& aside, std::vector<double> now, std::vector<double> before, ch::Format f) {
                ch::DumbbellData d;
                const char* names[] = {"Camille", "L\xC3\xA9" "a", "Hugo", "Nina"};
                for (int k = 0; k < 4; ++k) d.rows.push_back({"admin:" + std::to_string(k), names[k], before[static_cast<std::size_t>(k)], now[static_cast<std::size_t>(k)]});
                d.format = std::move(f);
                d.clickHint = "Clic : ses lignes du journal";
                auto c = std::make_unique<ch::Dumbbell>(std::string("graphes.db.") + title);
                c->setData(d);
                ch::Dumbbell& ref = sm->addChart(title, aside, std::move(c));
                wire(ref, title);
            };
            add("Actions", "263 \xC2\xB7 \xE2\x88\x92" "1 %", {80, 117, 48, 18}, {70, 96, 57, 44}, intF());
            add("Licences sign\xC3\xA9" "es", "76 \xC2\xB7 +7 %", {20, 41, 11, 4}, {17, 34, 14, 6}, intF());
            add("Demandes trait\xC3\xA9" "es", "72 \xC2\xB7 \xE2\x88\x92" "15 %", {9, 34, 21, 8}, {12, 30, 26, 17}, intF());
            add("Montant encaiss\xC3\xA9", ch::fr::euros(8690) + " \xC2\xB7 +17 %", {2760, 4870, 1060, 0}, {2310, 3720, 1390, 0},
                [](double v) { return std::fmod(v, 1000.0) != 0.0 ? ch::fr::euros(v) : ch::fr::eurosCompact(v); });
            auto c = std::make_unique<ch::ChartCard>("graphes.multiples.carte", "Cette p\xC3\xA9riode et la p\xC3\xA9riode d'avant", "par admin ; quatre petits graphiques, chacun son \xC3\xA9" "chelle");
            c->setValue("", {}, "");
            c->setChart(std::move(sm));
            b->put(std::move(c), 2, 0, 2, 1);
        }
        return b;
    }

    // ================================================ 8. LES CAS LIMITES, LE THEME SOMBRE ==
    std::unique_ptr<Board> buildEdgeCases() {
        auto b = std::make_unique<Board>("graphes.limites", 4, std::vector<float>{1.f, 1.f});
        {
            ch::TimeChartData d;
            d.kind = ch::Kind::Bars;
            d.x = weeks();
            d.series = {serie("un", "montant", std::vector<double>(13, ch::kNone))};
            card(*b, "graphes.vide", "Vide", "aucune valeur", 0, 0, 1, 1, tchart("graphes.vide", d));
        }
        {
            ch::TimeChartData d;
            d.x = ch::dailyAxis(ch::fr::day(2026, 11, 10), 1);
            ch::Series s = serie("un", "aujourd'hui", {7});
            s.endDot = true;
            d.series = {s};
            card(*b, "graphes.unpoint", "Un seul point", "une courbe d'un jour", 1, 0, 1, 1, tchart("graphes.unpoint", d));
        }
        {
            ch::TimeChartData d;
            d.kind = ch::Kind::Bars;
            d.x = weeks();
            d.xTicks = weekTicks();
            d.series = {serie("un", "remboursements", std::vector<double>(13, 0.0)), compare("l'an dernier", std::vector<double>(13, 0.0))};
            card(*b, "graphes.zero", "Tout \xC3\xA0 z\xC3\xA9ro", "l'axe garde 0 \xE2\x86\x92 1", 2, 0, 1, 1, tchart("graphes.zero", d));
        }
        {
            Rng g{41};
            std::vector<double> v;
            double x = 2.4e11;
            for (int i = 0; i < 13; ++i) v.push_back(x *= g.in(1.0, 1.25));
            ch::TimeChartData d;
            d.x = weeks();
            d.xTicks = weekTicks();
            ch::Series s = serie("un", "octets", v);
            s.area = s.endDot = true;
            d.series = {s};
            card(*b, "graphes.grandes", "Tr\xC3\xA8s grandes valeurs", "des centaines de milliards", 3, 0, 1, 1, tchart("graphes.grandes", d), ch::fr::compact(v.back()));
        }
        {
            Rng g{43};
            std::vector<double> v;
            for (int i = 0; i < 30; ++i) v.push_back(std::round(g.in(-12, 15)));
            ch::TimeChartData d;
            d.x = ch::dailyAxis(ch::fr::day(2026, 10, 12), 30);
            ch::Series s = serie("un", "solde", v);
            s.area = true;
            d.series = {s};
            d.yFormat = [](double x) { return ch::fr::integer(x); };
            card(*b, "graphes.negatives", "Valeurs n\xC3\xA9gatives", "la ligne de 0 en trait plus fort", 0, 1, 1, 1, tchart("graphes.negatives", d));
        }
        {
            Rng g{47};
            std::vector<double> v, p;
            double x = 120, y = 110;
            for (int i = 0; i < 365; ++i) {
                x += g.in(-3, 3.4);
                y += g.in(-3, 3.1);
                v.push_back(std::round(x));
                p.push_back(std::round(y));
            }
            ch::TimeChartData d;
            d.x = ch::dailyAxis(ch::fr::day(2025, 11, 11), 365);
            ch::Series s = serie("un", "PC actifs", v);
            s.endDot = true;
            d.series = {s, compare("l'ann\xC3\xA9" "e d'avant", p)};
            card(*b, "graphes.annee", "365 jours", "une courbe dense, les libell\xC3\xA9s espac\xC3\xA9s seuls", 1, 1, 1, 1, tchart("graphes.annee", d));
        }
        {
            ch::BarListData d;
            d.mode = ch::BarMode::Ranking;
            d.rows = {{"", "Soci\xC3\xA9t\xC3\xA9 d'essai des tr\xC3\xA8s longs noms de clients qui ne tiennent jamais dans la colonne", "", 2450, {}, "", "12 lic."},
                      {"", "Atelier Exemple", "", 2820, {}, "", "3 lic."},
                      {"", "Bureau d'\xC3\xA9tudes Exemple \xE2\x80\x94 agence de la Soci\xC3\xA9t\xC3\xA9 d'essai, Lyon", "", 990, {}, "", "1 lic."}};
            d.format = eurosF();
            card(*b, "graphes.longs", "Libell\xC3\xA9s trop longs", "coup\xC3\xA9s avec \xC2\xAB \xE2\x80\xA6 \xC2\xBB, la valeur reste", 2, 1, 1, 1, barList("graphes.longs", d));
        }
        {
            auto dark = std::make_unique<DarkPreview>("graphes.sombre");
            ch::TimeChartData d;
            d.kind = ch::Kind::Bars;
            d.x = weeks();
            d.xTicks = weekTicks();
            d.series = {serie("un", "2026", {1450, 2180, 1720, 1990, 1610, 2240, 1880, 2050, 2480, 1720, 2520, 2390, 690}),
                        compare("l'an dernier", {1650, 1720, 1810, 1900, 1580, 1990, 1700, 1870, 2050, 1930, 2090, 1880, 1830})};
            d.partialFrom = 12;
            dark->add(d);
            ch::TimeChartData v;
            v.kind = ch::Kind::Stacked;
            v.x = ch::weeklyAxis(ch::fr::day(2026, 6, 29), 12);
            v.xTicks = {{0, 0}, {6, 0}, {11, 0}};
            for (int k = 0; k < 4; ++k) {
                ch::Series s = serie("v" + std::to_string(k), "1.4" + std::to_string(4 + k), {});
                for (int i = 0; i < 12; ++i) s.values.push_back(std::max(0.0, 3.0 - std::fabs(i / 3.0 - k) * 1.5) + (k == 0 ? 1 : 0));
                s.color = ch::colors::ordinal(k, 4);
                v.series.push_back(s);
            }
            v.legend = ch::LegendMode::None;
            dark->add(v);
            b->put(std::move(dark), 3, 1);
        }
        return b;
    }

    std::vector<ui::Button*> tabs_;
    std::vector<Board*>      boards_;
    int                      current_{0};
    core::ConnectionScope    links_;
};

} // namespace

PagePtr makeChartsGalleryPage(ShellHost host) { return std::make_unique<ChartsGalleryPage>(std::move(host)); }

} // namespace license_admin::gui
