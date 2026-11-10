// =============================================================================
//  tests/charts_test.cpp - 1.47.0 : LES GRAPHIQUES DE L'ADMIN (ui/widgets/Charts)
// -----------------------------------------------------------------------------
//  Sans fenetre (un renderer qui note ce qu'on lui demande) :
//   - les formats francais (espaces fines, moins typographique, euros, %, ecarts,
//     durees, dates) et le calendrier ;
//   - les echelles : pas ronds, graduations (vide, un point, tout a zero, tres
//     grandes valeurs, negatives), bandes, point le plus proche, mediane, moyenne ;
//   - les couleurs : la palette de la maquette, la couleur d'une cle stable (un
//     filtre ne repeint pas les survivants), l'ordinale des versions, la
//     sequentielle ;
//   - la geometrie et le survol de chaque graphique (la cible plus grande que la
//     marque : toute la colonne, toute la ligne, la case et son espace), le clic
//     (la page est appelee avec ce qu'on a touche), le clavier ;
//   - la bulle (ce qu'elle dit) ; la distribution d'un delai ; l'echelle commune
//     des petits graphiques ;
//   - le dessin a deux tailles : aucun texte hors du graphique, les libelles de
//     l'axe ne se chevauchent pas, aucun NaN envoye au rendu.
// =============================================================================
#include "../src/ui/widgets/Charts.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ch = ui::charts;
using ch::fr::Day;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("%s %s\n", ok ? "  ok  " : "ECHEC ", what.c_str());
}
void same(const std::string& got, const std::string& want, const std::string& what) {
    check(got == want, what + " : \xC2\xAB " + got + " \xC2\xBB" + (got == want ? "" : " (attendu \xC2\xAB " + want + " \xC2\xBB)"));
}
bool near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

std::size_t glyphs(std::string_view s) {
    std::size_t n = 0;
    for (const char c : s)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}
float advance(gfx::FontId f) { return static_cast<float>(f.v ? f.v : 16) * 0.55f; }

// Ce qu'on demande au rendu : les textes (et leur boite), les formes, les triangles (et s'ils sont finis).
class Recorder final : public gfx::IRenderer {
public:
    struct Text {
        gfx::Rect   box;
        std::string s;
        gfx::Color  c;
    };
    void beginFrame(gfx::Color) override { clear(); }
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect& r, gfx::Color c) override { note(r, c); }
    void strokeRect(const gfx::Rect& r, gfx::Color c, float) override { note(r, c); }
    void fillRoundedRect(const gfx::Rect& r, gfx::Color c, float) override { note(r, c); }
    void line(gfx::Point a, gfx::Point b, gfx::Color, float) override { finite = finite && std::isfinite(a.x + a.y + b.x + b.y); }
    void drawText(gfx::Point at, std::string_view s, gfx::FontId f, gfx::Color c) override {
        finite = finite && std::isfinite(at.x + at.y);
        texts.push_back({{at.x, at.y, measure(s, f).width, lineHeight(f)}, std::string(s), c});
    }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    void fillTriangles(const gfx::Vertex* v, std::size_t n) override {
        triangles += n / 3;
        for (std::size_t i = 0; i < n; ++i) finite = finite && std::isfinite(v[i].pos.x + v[i].pos.y);
    }
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        const float h = static_cast<float>(f.v ? f.v : 16);
        return {static_cast<float>(glyphs(s)) * advance(f), h * 1.25f, h, h * 0.25f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return static_cast<float>(f.v ? f.v : 16) * 1.25f; }
    [[nodiscard]] gfx::Size surfaceSize() const override { return {1373.f, 917.f}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f, float w) const override {
        return std::min(s.size(), static_cast<std::size_t>(std::max(0.f, w / advance(f))));
    }
    void clear() {
        texts.clear();
        shapes.clear();
        triangles = 0;
        finite = true;
    }
    [[nodiscard]] bool shows(const std::string& part) const {
        for (const auto& t : texts)
            if (t.s.find(part) != std::string::npos) return true;
        return false;
    }
    std::vector<Text>                                texts;
    std::vector<std::pair<gfx::Rect, gfx::Color>> shapes;
    std::size_t                                      triangles{0};
    bool                                             finite{true};

private:
    void note(const gfx::Rect& r, gfx::Color c) {
        finite = finite && std::isfinite(r.x + r.y + r.w + r.h);
        shapes.push_back({r, c});
    }
};

void installMetrics() {
    ui::PlatformServices s;
    s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(glyphs(t)) * advance(f); };
    s.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v ? f.v : 16) * 1.25f; };
    s.surfaceSize = [] { return gfx::Size{1373.f, 917.f}; };
    ui::installPlatformServices(std::move(s));
}

const ui::Theme& theme() {
    static const ui::Theme t = ui::Theme::byName("XPG");
    return t;
}

void paint(ui::Widget& w, Recorder& r, const ui::Theme& th = theme()) {
    r.clear();
    w.layout();
    const ui::PaintContext ctx{r, th, {0.f, 0.f, 1373.f, 917.f}, 0.0, nullptr};
    w.render(ctx);
}

// Tout le texte dans les bornes (a 1 px pres) ; les libelles ne se chevauchent pas.
bool textInside(const Recorder& r, const gfx::Rect& b, std::string* bad = nullptr) {
    for (const auto& t : r.texts)
        if (t.box.x < b.x - 1.f || t.box.right() > b.right() + 1.f || t.box.y < b.y - 2.f || t.box.bottom() > b.bottom() + 3.f) {
            if (bad) {
                char at[96];
                std::snprintf(at, sizeof at, " (%.0f, %.0f, %.0f x %.0f)", static_cast<double>(t.box.x), static_cast<double>(t.box.y), static_cast<double>(t.box.w),
                              static_cast<double>(t.box.h));
                *bad = t.s + at;
            }
            return false;
        }
    return true;
}
bool noOverlap(const Recorder& r, std::string* bad = nullptr) {
    for (std::size_t i = 0; i < r.texts.size(); ++i)
        for (std::size_t j = i + 1; j < r.texts.size(); ++j) {
            const gfx::Rect a = r.texts[i].box.inset(1.f, 2.f), b = r.texts[j].box.inset(1.f, 2.f);
            if (!a.intersect(b).empty()) {
                if (bad) *bad = r.texts[i].s + " / " + r.texts[j].s;
                return false;
            }
        }
    return true;
}

ch::Series serie(std::string key, std::string name, std::vector<double> v) {
    ch::Series s;
    s.key = std::move(key);
    s.name = std::move(name);
    s.values = std::move(v);
    return s;
}

ui::MouseMove move(float x, float y) { return ui::MouseMove{{x, y}, {0.f, 0.f}, {}}; }
ui::MouseDown click(float x, float y) { return ui::MouseDown{{x, y}, ui::MouseButton::Left, 1, {}}; }
ui::KeyDown key(ui::Key k) { return ui::KeyDown{k, {}, false, ui::Key::Unknown}; }

// =============================================================== les formats ==
void testFormats() {
    std::printf("-- les formats francais\n");
    const std::string thin = "\xE2\x80\xAF", nb = "\xC2\xA0", minus = "\xE2\x88\x92";
    same(ch::fr::integer(12480), "12" + thin + "480", "un entier, l'espace fine des milliers");
    same(ch::fr::integer(1234567), "1" + thin + "234" + thin + "567", "un million");
    same(ch::fr::integer(-3), minus + "3", "le moins typographique");
    same(ch::fr::integer(0.4), "0", "arrondi a l'unite");
    same(ch::fr::integer(-0.4), "0", "pas de \xC2\xAB \xE2\x88\x92" "0 \xC2\xBB");
    same(ch::fr::euros(12480), "12" + thin + "480" + nb + "\xE2\x82\xAC", "des euros");
    same(ch::fr::eurosCents(490), "490,00" + nb + "\xE2\x82\xAC", "des euros et des centimes");
    same(ch::fr::decimal(2.5), "2,5", "la virgule");
    same(ch::fr::decimal(3.0), "3", "pas de zero de fin");
    same(ch::fr::decimal(2.04, 1), "2", "arrondi a un chiffre");
    same(ch::fr::percent(12), "12" + thin + "%", "un pourcentage");
    same(ch::fr::percent(59.4), "59" + thin + "%", "un pourcentage arrondi");
    same(ch::fr::signedPercent(13), "+13" + thin + "%", "un ecart positif");
    same(ch::fr::signedPercent(-3), minus + "3" + thin + "%", "un ecart negatif");
    same(ch::fr::signedPercent(0.2), "=", "pas d'ecart");
    same(ch::fr::deltaPercent(26020, 23000), "+13" + thin + "%", "l'ecart de 26 020 a 23 000");
    same(ch::fr::deltaPercent(5, 0), "", "un ecart sur rien : rien a dire");
    same(ch::fr::signedInteger(2), "+2", "un ecart en nombre");
    same(ch::fr::signedPoints(4), "+4" + nb + "pts", "des points");
    same(ch::fr::compact(850), "850", "compact, petit");
    same(ch::fr::compact(1500), "1,5" + thin + "k", "compact, des milliers");
    same(ch::fr::compact(85000), "85" + thin + "k", "compact, 85 k");
    same(ch::fr::compact(1.2e6), "1,2" + thin + "M", "compact, des millions");
    same(ch::fr::compact(1.2e12), "1" + thin + "200" + thin + "Md", "compact, tres grand");
    same(ch::fr::eurosCompact(3000), "3" + thin + "k\xE2\x82\xAC", "des k\xE2\x82\xAC");
    same(ch::fr::eurosCompact(850), "850" + nb + "\xE2\x82\xAC", "des euros sous mille");
    same(ch::fr::hours(4.0 + 5.0 / 60.0), "4" + nb + "h" + nb + "05", "une duree en heures");
    same(ch::fr::hours(1.0), "1" + nb + "h", "une heure ronde");
    same(ch::fr::hours(10.0 / 60.0), "10" + nb + "min", "des minutes");
    same(ch::fr::hours(27), "1" + nb + "j" + nb + "3" + nb + "h", "plus d'un jour");
    same(ch::fr::hours(54), "2" + nb + "j" + nb + "6" + nb + "h", "le max de la planche 04");
    same(ch::fr::clock(8.0 + 40.0 / 60.0), "8" + nb + "h" + nb + "40", "une heure du jour");
    same(ch::fr::clock(21.05), "21" + nb + "h" + nb + "03", "21 h 03");
    same(ch::fr::integer(std::nan("")), "\xE2\x80\x94", "pas de valeur : un tiret");
}

void testDates() {
    std::printf("-- le calendrier\n");
    const Day d = ch::fr::day(2026, 11, 10);
    check(ch::fr::weekday(d) == 1, "le 10/11/2026 est un mardi");
    check(ch::fr::day(1970, 1, 1) == 0 && ch::fr::weekday(0) == 3, "le 01/01/1970 : le jour 0, un jeudi");
    same(ch::fr::ddmm(ch::fr::day(2026, 11, 3)), "03/11", "jj/mm");
    same(ch::fr::ddmmyyyy(ch::fr::day(2024, 2, 29)), "29/02/2024", "un 29 fevrier");
    same(ch::fr::dayShort(ch::fr::day(2026, 11, 3)), "3\xC2\xA0nov.", "\xC2\xAB 3 nov. \xC2\xBB");
    same(ch::fr::dayLong(ch::fr::day(2026, 10, 12)), "lun. 12\xC2\xA0oct.", "le jour en clair");
    same(ch::fr::weekRange(ch::fr::day(2026, 10, 26)), "Semaine du 26/10 au 01/11", "une semaine");
    same(ch::fr::monthYear(ch::fr::day(2024, 3, 1)), "mars 2024", "un mois");
    same(ch::fr::monthShort(2), "f\xC3\xA9vr.", "fevrier abrege");
    same(ch::fr::monthShort(12), "d\xC3\xA9" "c.", "decembre abrege");
    bool round = true;
    for (Day x = ch::fr::day(1999, 12, 25); x < ch::fr::day(2031, 1, 10); ++x) {
        int y, m, dd;
        ch::fr::civil(x, y, m, dd);
        round = round && ch::fr::day(y, m, dd) == x && m >= 1 && m <= 12 && dd >= 1 && dd <= 31;
    }
    check(round, "jour -> date -> jour, trente ans");
    const auto w = ch::weeklyAxis(ch::fr::day(2026, 8, 17), 13);
    check(w.size() == 13 && w[0].label == "17/08" && w[12].label == "09/11" && w[10].tip == "Semaine du 26/10 au 01/11", "l'axe des semaines");
    const auto dly = ch::dailyAxis(ch::fr::day(2026, 10, 12), 30, true);
    check(dly[5].shade && dly[6].shade && !dly[7].shade, "l'axe des jours : les fins de semaine en clair");
    const auto mo = ch::monthlyAxis(2025, 12, 3);
    check(mo[0].label == "d\xC3\xA9" "c." && mo[1].label == "janv." && mo[1].tip == "janvier 2026", "l'axe des mois passe l'annee");
}

// ============================================================= les echelles ==
void testScales() {
    std::printf("-- les echelles\n");
    check(ch::niceStep(0.7) == 1 && ch::niceStep(1.3) == 2 && ch::niceStep(2.2) == 2.5 && ch::niceStep(3) == 5 && ch::niceStep(7) == 10, "des pas ronds (1, 2, 2,5, 5)");
    check(near(ch::niceStep(0.03), 0.05) && ch::niceStep(0) == 1 && ch::niceStep(-4) == 1 && ch::niceStep(std::nan("")) == 1, "un pas petit, nul, negatif, NaN");
    const auto a = ch::niceTicks(0, 2520, 3);
    check(a == std::vector<double>({0, 1000, 2000, 3000}), "0 -> 2 520 : 0, 1 000, 2 000, 3 000");
    const auto z = ch::niceTicks(0, 0, 3);
    check(z == std::vector<double>({0, 1}), "tout a zero : 0 et 1");
    const auto b = ch::niceTicks(80, 135, 3);
    check(b == std::vector<double>({80, 100, 120, 140}), "80 -> 135 : 80, 100, 120, 140");
    const auto n = ch::niceTicks(-12, 15, 3);
    check(n.front() <= -12 && n.back() >= 15 && std::find(n.begin(), n.end(), 0.0) != n.end(), "negatives : 0 dans les graduations");
    const auto h = ch::niceTicks(0, 1.2e12, 3);
    bool fin = h.size() >= 2 && h.size() <= 8 && h.back() >= 1.2e12;
    for (const double t : h) fin = fin && std::isfinite(t);
    check(fin, "tres grand (1,2e12) : peu de graduations, finies");
    check(ch::niceTicks(5, 5, 3).size() >= 2, "un seul point : un axe quand meme");
    const auto ht = ch::hourTicks(9.5, 3);
    check(ht.front() == 0 && ht.back() >= 9.5 && ht.size() <= 5, "des heures : 0, 4, 8, 12");
    const ch::Linear l{0, 100, 200, 0};
    check(near(l.map(50), 100) && near(l.invert(50), 75), "une echelle lineaire (vers le haut) et son inverse");
    const ch::Band bd{10, 0, 100};
    check(bd.indexAt(0) == 0 && bd.indexAt(99.9f) == 9 && bd.indexAt(100) == -1 && bd.indexAt(-1) == -1 && near(bd.center(2), 25), "des bandes");
    check(ch::nearestIndex(49, 0, 100, 3) == 1 && ch::nearestIndex(-50, 0, 100, 3) == 0 && ch::nearestIndex(500, 0, 100, 3) == 2 && ch::nearestIndex(5, 0, 100, 0) == -1,
          "le point le plus proche");
    check(near(ch::median({3, 1, 2}), 2) && near(ch::median({4, 1, 3, 2}), 2.5) && std::isnan(ch::median({})), "la mediane");
    check(near(ch::mean({1, 2, 3, std::nan("")}), 2) && std::isnan(ch::mean({})), "la moyenne (NaN ignores)");
}

// ============================================================== les couleurs ==
void testColors() {
    std::printf("-- les couleurs\n");
    const auto eq = [](gfx::Color a, gfx::Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; };
    check(eq(ch::colors::admin(0), gfx::Color::rgb(0x6D4AFF)) && eq(ch::colors::admin(1), gfx::Color::rgb(0x0B7FA3)) &&
              eq(ch::colors::admin(2), gfx::Color::rgb(0xEA580C)) && eq(ch::colors::admin(3), gfx::Color::rgb(0xDB2777)),
          "les admins : Camille, Lea, Hugo, Nina (extra.css)");
    check(eq(ch::colors::offer("PERSONAL"), gfx::Color::rgb(0x1BAF7A)) && eq(ch::colors::offer("professional"), gfx::Color::rgb(0x6250D6)) &&
              eq(ch::colors::offer("D\xC3\xA9" "couverte"), gfx::Color::rgb(0xA3ADBF)),
          "les offres (casse et accents ignores)");
    check(eq(ch::colors::criticality(3), gfx::Color::rgb(0xDC2626)) && eq(ch::colors::criticality(9), gfx::Color::rgb(0xDC2626)), "la criticite");
    const auto& keys = ch::ColorKeys::standard();
    check(eq(keys.colorOf("admin:lea"), ch::colors::kAdmins[1]) && eq(keys.colorOf("admin:1"), ch::colors::kAdmins[1]) &&
              eq(keys.colorOf("offre:ENTERPRISE"), ch::colors::kEnterprise) && eq(keys.colorOf("cmp"), ch::colors::kCompare),
          "la table commune");
    check(eq(keys.colorOf("une cle inconnue"), keys.colorOf("une cle inconnue")) && eq(ch::ColorKeys().colorOf("une cle inconnue"), keys.colorOf("une cle inconnue")),
          "une cle inconnue : toujours la meme couleur");
    ch::ColorKeys k({"camille", "lea", "hugo", "nina"}, {});
    const gfx::Color hugo = k.colorOf("hugo");
    check(eq(hugo, ch::colors::kAdmins[2]), "le domaine : Hugo est le troisieme");
    // Un filtre (Lea seule montree) : la couleur ne depend pas de ce qui est montre - de deux graphiques, l'un avec
    // Camille, Lea, Hugo et Nina, l'autre avec Hugo seul : Hugo garde sa couleur.
    ch::TimeChart all, filtered;
    ch::TimeChartData d1, d2;
    d1.x = ch::labelAxis({"a", "b"});
    d2.x = d1.x;
    for (const char* who : {"admin:0", "admin:1", "admin:2", "admin:3"}) d1.series.push_back(serie(who, who, {1, 2}));
    d2.series.push_back(serie("admin:2", "Hugo", {1, 2}));
    all.setData(d1);
    filtered.setData(d2);
    check(eq(all.seriesColor(2), filtered.seriesColor(0)) && eq(filtered.seriesColor(0), ch::colors::kAdmins[2]), "filtrer ne repeint pas : Hugo reste orange");
    ch::TimeChartData d3;
    d3.x = d1.x;
    d3.series = {serie("", "une", {1, 2}), serie("", "deux", {2, 1}), serie("", "avant", {1, 1})};
    d3.series[2].role = ch::Role::Compare;
    ch::TimeChart sans;
    sans.setData(d3);
    check(eq(sans.seriesColor(0), ch::colors::kOne) && eq(sans.seriesColor(1), ch::colors::kTwo) && eq(sans.seriesColor(2), ch::colors::kCompare),
          "sans cle : une mesure bleue, la seconde orange, la comparaison grise");
    check(eq(ch::colors::ordinal(3, 4), ch::colors::kOrdinal[3]) && eq(ch::colors::ordinal(0, 4), ch::colors::kOrdinal[0]) &&
              eq(ch::colors::ordinal(1, 2), ch::colors::kOrdinal[3]) && eq(ch::colors::ordinal(0, 6), ch::colors::kCompare) &&
              eq(ch::colors::ordinal(5, 6), ch::colors::kOrdinal[3]),
          "l'ordinale : la plus recente la plus foncee, au-dela de 4 les anciennes en gris");
    check(ch::colors::sequentialStep(0, 10) == 0 && ch::colors::sequentialStep(10, 10) == 6 && ch::colors::sequentialStep(0.1, 10) == 1 &&
              ch::colors::sequentialStep(5, 0) == 0 && ch::colors::sequentialStep(50, 10) == 6,
          "la sequentielle : 0, 1..6");
    const ch::colors::Ink light = ch::colors::ink(theme());
    const ch::colors::Ink dark = ch::colors::ink(ui::Theme::dark());
    check(eq(light.ink, gfx::Color::rgb(0x0F172A)) && eq(light.faint, gfx::Color::rgb(0x94A3B8)) && eq(light.grid, gfx::Color::rgb(0xEEF1F5)), "clair : les gris de la maquette");
    check(dark.dark && !light.dark, "sombre : reconnu");
    const gfx::Color deep = ch::colors::onTheme(ch::colors::kOrdinal[3], dark);
    check(!eq(deep, ch::colors::kOrdinal[3]) && eq(ch::colors::onTheme(ch::colors::kOrdinal[3], light), ch::colors::kOrdinal[3]), "sombre : une teinte trop sombre eclaircie");
}

// =========================================================== le TimeChart ==
ch::TimeChartData weeksBars() {
    ch::TimeChartData d;
    d.kind = ch::Kind::Bars;
    d.x = ch::weeklyAxis(ch::fr::day(2026, 8, 17), 13);
    d.series = {serie("un", "2026", {1450, 2180, 1720, 1990, 1610, 2240, 1880, 2050, 2480, 1720, 2520, 2390, 690})};
    ch::Series c = serie("cmp", "l'an dernier", {1650, 1720, 1810, 1900, 1580, 1990, 1700, 1870, 2050, 1930, 2090, 1880, 1830});
    c.role = ch::Role::Compare;
    d.series.push_back(c);
    d.valueFormat = [](double v) { return ch::fr::euros(v); };
    d.partialFrom = 12;
    d.clickHint = "Clic : ces commandes";
    d.tipExtra = [](int i) { return "extra " + std::to_string(i); };
    return d;
}

void testTimeChart() {
    std::printf("-- TimeChart : geometrie, survol, clic, clavier, bulle\n");
    ch::TimeChart c("essai.montant");
    c.setBounds({100, 50, 400, 220});
    c.setData(weeksBars());
    const auto f = c.frame();
    check(f.plot.x > 100 && f.plot.right() <= 500 && f.plot.y > 50 && f.plot.bottom() < 270, "le trace dans le graphique");
    check(f.ticks.front() == 0 && f.ticks.back() >= 2520 && f.legendHeight > 0, "les graduations couvrent les valeurs ; la legende des 2 series");
    check(near(f.yAt(f.lo), f.plot.bottom(), 0.01) && near(f.yAt(f.hi), f.plot.y, 0.01), "y : le bas et le haut");
    bool inc = true;
    for (int i = 1; i < 13; ++i) inc = inc && f.xAt(i) > f.xAt(i - 1);
    check(inc && f.band, "x : des bandes, de gauche a droite");
    check(c.barWidth(f) <= 24.f, "une colonne de 24 px au plus");
    // La cible : toute la colonne (au-dessus de la barre aussi).
    const float x3 = f.xAt(3);
    check(c.itemAt({x3, f.plot.y + 2.f}).first == 3, "survol : le haut de la colonne 3 (au-dessus de la barre) est la colonne 3");
    check(c.itemAt({x3 + f.slot() * 0.45f, f.plot.bottom() - 1.f}).first == 3, "survol : le bord de la bande");
    check(c.itemAt({f.plot.x - 30.f, f.plot.y + 10.f}).first == -1, "survol : hors du trace, rien");
    const ch::Pick p = c.pickAt({x3, f.plot.bottom() - 2.f});
    check(p.index == 3 && p.series == 0 && p.key == "un" && near(p.value, 1990), "clic sur la barre 3 : l'indice, la serie, sa valeur");
    const ch::Pick pc = c.pickAt({f.xAt(5), f.yAt(1990)});
    check(pc.index == 5 && pc.series == 1, "clic sur la comparaison (a 12 px) : la courbe passe avant la barre");
    // La bulle.
    const ch::Tip t = c.tipFor(10, -1);
    check(t.title == "Semaine du 26/10 au 01/11" && t.rows.size() == 2 && t.rows[0].name == "2026" && t.rows[1].name == "l'an dernier" && t.rows[1].line &&
              !t.rows[0].line,
          "la bulle : la semaine, la periode puis la comparaison (un trait)");
    check(t.rows[0].value == ch::fr::euros(2520) && t.extra == "extra 10" && t.click == "Clic : ces commandes", "la bulle : la valeur en euros, la ligne en plus, le clic");
    check(c.tipFor(12, -1).title.find("en cours") != std::string::npos, "la periode en cours est dite");
    check(c.tipFor(99, -1).empty() && c.tipFor(-1, -1).empty(), "la bulle hors des donnees : rien");
    // Le clic par le vrai chemin (dispatch).
    std::vector<ch::Pick> got;
    c.setOnPick([&](const ch::Pick& k) { got.push_back(k); });
    (void)c.dispatch(move(f.xAt(7), f.plot.y + 20.f));
    check(c.activeIndex() == 7, "le survol suit la souris");
    const auto res = c.dispatch(click(f.xAt(7), f.plot.bottom() - 2.f));
    check(res == ui::EventResult::Consumed && got.size() == 1 && got[0].index == 7, "le clic appelle la page (l'indice 7)");
    (void)c.dispatch(click(f.plot.x - 40.f, f.plot.y));
    check(got.size() == 1, "un clic hors du trace n'appelle pas la page");
    // Le clavier : focus, fleches, Entree.
    c.takeFocus();
    (void)c.dispatch(key(ui::Key::Right));
    (void)c.dispatch(key(ui::Key::Right));
    check(c.activeIndex() == 9, "le clavier : deux fois droite depuis 7 -> 9");
    (void)c.dispatch(key(ui::Key::End));
    (void)c.dispatch(key(ui::Key::Return));
    check(got.size() == 2 && got[1].index == 12 && got[1].series == 0, "Fin puis Entree : la derniere semaine, la serie principale");
    (void)c.dispatch(key(ui::Key::Home));
    check(c.activeIndex() == 0, "Debut");
    check(c.requestsOverlayPass(), "la bulle passe au-dessus de tout (le focus clavier)");
    (void)c.dispatch(key(ui::Key::Escape));
    check(c.activeIndex() == -1, "Echap efface");

    // Une courbe : le point le plus proche, la serie la plus proche.
    ch::TimeChartData l;
    l.x = ch::dailyAxis(ch::fr::day(2026, 10, 12), 30);
    l.series = {serie("un", "re\xC3\xA7ues", std::vector<double>(30, 10.0)), serie("deux", "trait\xC3\xA9" "es", std::vector<double>(30, 20.0))};
    ch::TimeChart lc;
    lc.setBounds({0, 0, 500, 200});
    lc.setData(l);
    const auto lf = lc.frame();
    check(!lf.band && near(lf.xAt(0), lf.plot.x) && near(lf.xAt(29), lf.plot.right(), 0.01), "une courbe : des points aux bords");
    check(lc.itemAt({lf.xAt(4) + 3.f, lf.plot.y + 5.f}).first == 4, "une courbe : le jour le plus proche, n'importe ou dans le trace");
    check(lc.pickAt({lf.xAt(4), lf.yAt(20) + 3.f}).series == 1 && lc.pickAt({lf.xAt(4), lf.yAt(10) - 4.f}).series == 0, "une courbe : la serie la plus proche");

    // Empilees : le morceau sous la souris.
    ch::TimeChartData s;
    s.kind = ch::Kind::Stacked;
    s.x = ch::labelAxis({"a", "b", "c"});
    s.series = {serie("offre:PERSONAL", "PERSONAL", {10, 10, 10}), serie("offre:PROFESSIONAL", "PROFESSIONAL", {10, 20, 5})};
    ch::TimeChart sc;
    sc.setBounds({0, 0, 300, 200});
    sc.setData(s);
    const auto sf = sc.frame();
    check(sc.pickAt({sf.xAt(1), sf.yAt(5)}).series == 0 && sc.pickAt({sf.xAt(1), sf.yAt(25)}).series == 1, "empilees : le morceau du bas, celui du haut");
    check(sc.tipFor(1, -1).rows.front().name == "PROFESSIONAL", "empilees : la bulle du haut vers le bas");

    // Divergentes : le 0 au milieu, des graduations negatives.
    ch::TimeChartData v;
    v.kind = ch::Kind::Diverging;
    v.x = ch::labelAxis({"a", "b"});
    ch::Series out = serie("down:0", "sorties", {3, 4});
    out.below = true;
    v.series = {serie("up:0", "entr\xC3\xA9" "es", {5, 8}), out};
    ch::TimeChart vc;
    vc.setBounds({0, 0, 300, 200});
    vc.setData(v);
    const auto vf = vc.frame();
    check(vf.lo < 0 && vf.hi >= 8 && std::find(vf.ticks.begin(), vf.ticks.end(), 0.0) != vf.ticks.end(), "divergentes : le 0 dans l'axe, les sorties dessous");
    check(vc.pickAt({vf.xAt(1), vf.yAt(-2)}).series == 1, "divergentes : clic sur une sortie");
    check(vc.tipFor(1, -1).rows.back().value == "\xE2\x88\x92" "4", "divergentes : la sortie dite negative");

    // Les cas limites.
    ch::TimeChart e;
    e.setBounds({0, 0, 300, 160});
    ch::TimeChartData ed;
    e.setData(ed);
    check(e.frame().n == 0 && e.itemAt({150, 80}).first == -1 && e.tipFor(0, -1).empty(), "vide : rien a survoler");
    ch::TimeChartData one;
    one.x = ch::labelAxis({"auj."});
    one.series = {serie("un", "x", {7})};
    e.setData(one);
    const auto of = e.frame();
    check(of.n == 1 && near(of.xAt(0), of.plot.x + of.plot.w * 0.5f, 0.01) && e.itemAt({of.plot.x + 3.f, of.plot.y + 5.f}).first == 0, "un seul point : au milieu, survolable");
    ch::TimeChartData zero;
    zero.kind = ch::Kind::Bars;
    zero.x = ch::labelAxis({"a", "b", "c"});
    zero.series = {serie("un", "x", {0, 0, 0})};
    e.setData(zero);
    check(e.frame().ticks == std::vector<double>({0, 1}), "tout a zero : l'axe 0 -> 1");
    ch::TimeChartData huge;
    huge.x = ch::labelAxis({"a", "b", "c"});
    huge.series = {serie("un", "x", {2e11, 9e11, 1.4e12})};
    e.setData(huge);
    const auto hf = e.frame();
    check(hf.hi >= 1.4e12 && hf.ticks.size() <= 8 && hf.plot.w > 100.f, "tres grandes valeurs : un axe lisible, le trace garde sa place");
    ch::TimeChartData nan;
    nan.x = ch::labelAxis({"a", "b", "c", "d"});
    nan.series = {serie("un", "x", {1, std::nan(""), 3, 4})};
    e.setData(nan);
    check(e.tipFor(1, -1).rows.empty() && e.tipFor(2, -1).rows.size() == 1, "un trou (NaN) : rien dans la bulle a cet endroit");
}

// ============================================================ les autres ==
void testOthers() {
    std::printf("-- BarList, HeatMap, DayStrip, Dumbbell, Timeline, distribution, petits graphiques\n");
    // BarList : la part de chacun.
    ch::BarListData d;
    d.mode = ch::BarMode::Share;
    d.partKeys = {"admin:0", "admin:1", "admin:2", "admin:3"};
    d.partNames = {"Camille", "L\xC3\xA9" "a", "Hugo", "Nina"};
    ch::BarRow r1;
    r1.label = "Cr\xC3\xA9" "er et signer une licence";
    r1.parts = {8, 19, 6, 4};
    ch::BarRow r2;
    r2.label = "Annonces";
    r2.parts = {2, 0, 0, 0};
    d.rows = {r1, r2};
    ch::BarList bl("essai.part");
    bl.setBounds({0, 0, 500, 200});
    bl.setData(d);
    const auto bf = bl.frame();
    check(bf.rowTop.size() == 2 && bf.x1 > bf.x0 && bf.legendHeight > 0, "la part de chacun : deux lignes, la legende des admins");
    const float y1 = bf.rowTop[0] + bf.rowHeight[0] * 0.5f;
    check(bl.itemAt({bf.x0 + 2.f, y1}).second == 0 && bl.itemAt({bf.x1 - 2.f, y1}).second == 3, "la part : Camille a gauche, Nina a droite");
    check(bl.itemAt({4.f, y1}).first == 0 && bl.itemAt({4.f, y1}).second == -1, "la ligne entiere est la cible (le libelle aussi)");
    const ch::Tip bt = bl.tipFor(0, -1);
    check(bt.title == "Cr\xC3\xA9" "er et signer une licence" && bt.rows.size() == 4 && bt.rows[1].name.find("51") != std::string::npos, "la bulle : chacun et sa part (Lea 51 %)");
    check(bl.tipFor(1, -1).rows.size() == 1, "une seule personne : une ligne (les zeros ne sont pas dits)");
    check(bl.pickAt({bf.x0 + 2.f, y1}).key == "admin:0", "le clic : la cle de l'admin touche");
    ch::BarListData off;
    off.mode = ch::BarMode::Ranking;
    ch::BarRow n1;
    n1.label = "Modifier les offres";
    n1.off = true;
    n1.note = "pas ce droit";
    off.rows = {n1};
    bl.setData(off);
    check(bl.tipFor(0, -1).rows.front().value == "\xE2\x80\x94", "un droit qu'il n'a pas : \xC2\xAB \xE2\x80\x94 \xC2\xBB");
    check(bl.preferredHeight() > 20.f, "la hauteur voulue d'une liste");
    // Trop etroit pour le libelle : il passe au-dessus de la barre (planche 08, << Options vendues >>).
    ch::BarListData longs;
    longs.mode = ch::BarMode::Track;
    ch::BarRow lr;
    lr.label = "Exports avanc\xC3\xA9s pour les tr\xC3\xA8s grands ateliers";
    lr.value = 1450;
    lr.extra = "21 cmd";
    longs.rows = {lr};
    bl.setData(longs);
    bl.setBounds({0, 0, 600, 80});
    check(!bl.frame().stacked, "large : le libelle a gauche de la barre");
    bl.setBounds({0, 0, 200, 80});
    const auto narrow = bl.frame();
    check(narrow.stacked && narrow.x0 == 0.f && narrow.x1 == 200.f, "etroit : le libelle au-dessus, la barre sur toute la largeur");
    longs.labelsAbove = true;
    bl.setData(longs);
    bl.setBounds({0, 0, 600, 80});
    check(bl.frame().stacked, "labelsAbove : au-dessus meme au large");
    bl.setBounds({0, 0, 500, 200});
    ch::BarListData empty;
    bl.setData(empty);
    check(bl.itemAt({10, 10}).first == -1, "une liste vide : rien a survoler");

    // HeatMap : la case et son espace, sans trou.
    ch::HeatMapData h;
    h.rowLabels = {"lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."};
    for (int i = 0; i < 24; ++i) h.colLabels.push_back(std::to_string(i));
    h.cells.assign(7, std::vector<double>(24, 0.0));
    h.cells[2][21] = 1;
    h.cells[0][9] = 5;
    h.flagged = {{2, 21}};
    h.valueUnit = "action";
    ch::HeatMap hm("essai.carte");
    hm.setBounds({0, 0, 700, 260});
    hm.setData(h);
    const auto hf = hm.frame();
    const gfx::Rect c = hf.cell(2, 21);
    check(hm.itemAt({c.x + c.w * 0.5f, c.y + c.h * 0.5f}) == std::pair<int, int>(21, 2), "la carte : la case mer. 21 h");
    check(hm.itemAt({c.right() + hf.gap * 0.5f - 0.1f, c.y + 1.f}).first == 21 && hm.itemAt({c.right() + hf.gap * 0.5f + 0.1f, c.y + 1.f}).first == 22,
          "la carte : l'espace entre deux cases est partage (pas de trou)");
    check(hm.itemCount() == 24 && hm.subCount() == 7, "24 heures, 7 jours");
    const ch::Tip ht = hm.tipFor(21, 2);
    check(ht.rows.size() == 1 && ht.rows[0].value == "1 action" && !ht.extra.empty(), "la bulle : \xC2\xAB 1 action \xC2\xBB, signalee");
    check(hm.tipFor(9, 0).rows[0].value == "5 actions", "le pluriel : \xC2\xAB 5 actions \xC2\xBB");
    hm.takeFocus();
    (void)hm.dispatch(key(ui::Key::Right));
    (void)hm.dispatch(key(ui::Key::Down));
    check(hm.activeIndex() == 0 && hm.activeSub() == 1, "le clavier : les fleches vont de case en case (colonnes, rangees)");

    // DayStrip.
    ch::DayStripData ds;
    ds.first = ch::fr::day(2026, 10, 12);
    ds.values = {6, 5, 4, 0, 3};
    ds.streakFrom = 3;
    ds.streakTo = 4;
    ch::DayStrip strip("essai.jours");
    strip.setBounds({0, 0, 300, 60});
    strip.setData(ds);
    const gfx::Rect s2 = strip.cellRect(2);
    check(strip.itemAt({s2.x + 1.f, s2.y + 1.f}).first == 2, "les jours : le carre du 14/10");
    check(strip.tipFor(0, -1).title == "lun. 12\xC2\xA0oct." && strip.tipFor(3, -1).rows[0].value == "aucune action", "les jours : la date, \xC2\xAB aucune action \xC2\xBB");
    Recorder rs;
    paint(strip, rs);
    const bool dates = rs.shows("12/10");
    strip.setBounds({0, 0, 300, 24});
    paint(strip, rs);
    check(dates && rs.texts.empty() && strip.cellRect(0).bottom() <= 24.f, "les jours : les dates s'il y a la place, sinon les carres seuls");

    // Dumbbell.
    ch::DumbbellData db;
    db.rows = {{"admin:0", "Camille", 70, 80}, {"admin:1", "L\xC3\xA9" "a", 96, 117}};
    ch::Dumbbell dumb("essai.avant");
    dumb.setBounds({0, 0, 300, 90});
    dumb.setData(db);
    const auto df = dumb.frame();
    check(dumb.itemAt({df.x0, df.top + df.rowH * 1.5f}).first == 1, "avant -> maintenant : la ligne de Lea");
    check(dumb.tipFor(1, -1).extra.find("+22") != std::string::npos, "avant -> maintenant : l'ecart (+22 %)");

    // Timeline.
    ch::TimelineData td;
    td.from = ch::fr::day(2024, 1, 1);
    td.to = ch::fr::day(2027, 12, 31);
    td.today = ch::fr::day(2026, 11, 10);
    ch::TimelineRow tr;
    tr.label = "XPG-2024-00031";
    tr.segments = {{ch::fr::day(2024, 3, 11), ch::fr::day(2025, 3, 11), "offre:PROFESSIONAL", "sign\xC3\xA9" "e"}};
    td.rows = {tr, tr};
    ch::Timeline tl("essai.frise");
    tl.setBounds({0, 0, 600, 120});
    tl.setData(td);
    const auto tf = tl.frame();
    const float tx = tf.xAt(static_cast<double>(td.today));
    check(tx > tf.x0 && tx < tf.x1, "la frise : aujourd'hui dans l'axe");
    const float sx = tf.xAt(static_cast<double>(ch::fr::day(2024, 9, 1)));
    check(tl.itemAt({sx, tf.top + tf.rowH * 1.5f}) == std::pair<int, int>(1, 0), "la frise : la ligne 2, son morceau");

    // La distribution d'un delai.
    const std::vector<double> samples = {0.2, 0.4, 0.6, 1.1, 1.5, 3.0, 6.0, 54.0};
    const auto dd = ch::makeDistribution(samples, {0, 0.5, 1, 2, 4, 8, 24, 48, 1e9}, {"a", "b", "c", "d", "e", "f", "g", "h"}, ch::colors::kOne, "demandes");
    double total = 0;
    for (const double x : dd.series[0].values) total += x;
    check(dd.series[0].values.size() == 8 && near(total, 8) && near(dd.series[0].values[7], 1), "la distribution : chaque delai dans sa tranche");
    check(dd.markers.size() == 3 && dd.markers[0].label == "m\xC3\xA9" "diane 1\xC2\xA0h\xC2\xA0" "18" && dd.markers[2].label == "max 2\xC2\xA0j\xC2\xA0" "6\xC2\xA0h",
          "la distribution : la mediane (1 h 18), la moyenne, le max (2 j 6 h)");
    check(dd.markers[0].at >= 1.5 && dd.markers[0].at <= 2.5 && dd.markers[2].at >= 6.5, "les reperes tombent dans leur tranche");
    check(ch::binPosition(0.75, {0, 0.5, 1}) > 0.5 && ch::binPosition(0.75, {0, 0.5, 1}) < 1.5, "la place d'une valeur dans les tranches");

    // Les petits graphiques : chacun son echelle, ou la meme.
    ch::SmallMultiples sm("essai.multiples", 2);
    ch::TimeChartData a;
    a.kind = ch::Kind::Bars;
    a.x = ch::labelAxis({"a", "b"});
    a.series = {serie("un", "x", {3, 5})};
    ch::TimeChartData bdat = a;
    bdat.series[0].values = {40, 90};
    auto* c1 = &sm.addChart("petit", "", std::make_unique<ch::TimeChart>("m1"));
    auto* c2 = &sm.addChart("grand", "", std::make_unique<ch::TimeChart>("m2"));
    c1->setData(a);
    c2->setData(bdat);
    sm.setBounds({0, 0, 600, 200});
    sm.layout();
    check(c1->frame().hi < c2->frame().hi, "chacun son echelle (planche 05)");
    sm.setSharedScale(true);
    sm.layout();
    check(near(c1->frame().hi, c2->frame().hi), "la meme echelle quand on la demande");
    check(sm.cellCount() == 2 && sm.cellRect(1).x > sm.cellRect(0).right(), "deux cellules cote a cote");

    // L'ecart d'une carte.
    const ch::Delta up = ch::deltaOf(26020, 23000);
    const ch::Delta late = ch::deltaOf(3.67, 5.2, false);
    check(up.sense == 1 && up.direction == 1 && late.sense == 1 && late.direction == -1, "l'ecart : monter est bien (le montant), descendre est bien (le delai)");
    check(ch::deltaOf(709, 682, false).sense == -1 && ch::deltaOf(5, 0).text.empty(), "l'ecart : plus de remboursements est mal ; sur zero, rien");
}

// =================================================== le dessin a deux tailles ==
void testPaint() {
    std::printf("-- le dessin : dans ses bornes, sans chevauchement, sans NaN (grand et petit)\n");
    Recorder r;
    struct Case {
        std::string                 name;
        std::function<std::unique_ptr<ui::Widget>()> make;
    };
    std::vector<Case> cases;
    cases.push_back({"barres et comparaison", [] {
                         auto c = std::make_unique<ch::TimeChart>("p.barres");
                         auto d = weeksBars();
                         d.xTicks = {{0, 0}, {4, 0}, {8, 0}, {12, 0}};
                         c->setData(d);
                         return std::unique_ptr<ui::Widget>(std::move(c));
                     }});
    cases.push_back({"courbe dense (365 jours), libelles automatiques", [] {
                         auto c = std::make_unique<ch::TimeChart>("p.annee");
                         ch::TimeChartData d;
                         d.x = ch::dailyAxis(ch::fr::day(2025, 11, 11), 365);
                         std::vector<double> v;
                         for (int i = 0; i < 365; ++i) v.push_back(100 + 20 * std::sin(i / 20.0));
                         ch::Series s = serie("un", "PC actifs", v);
                         s.area = s.endDot = true;
                         d.series = {s};
                         c->setData(d);
                         return std::unique_ptr<ui::Widget>(std::move(c));
                     }});
    cases.push_back({"reperes qui se chevauchent", [] {
                         auto c = std::make_unique<ch::TimeChart>("p.reperes");
                         ch::TimeChartData d;
                         d.x = ch::dailyAxis(ch::fr::day(2026, 7, 1), 133);
                         d.series = {serie("un", "Studios", std::vector<double>(133, 50.0))};
                         for (const int at : {13, 48, 62, 90, 126}) d.markers.push_back({static_cast<double>(at), "1.45." + std::to_string(at), ch::kNone, {0, 0, 0, 0}, false});
                         c->setData(d);
                         return std::unique_ptr<ui::Widget>(std::move(c));
                     }});
    cases.push_back({"distribution", [] {
                         auto c = std::make_unique<ch::TimeChart>("p.distribution");
                         c->setData(ch::makeDistribution({0.2, 0.4, 0.6, 1.1, 1.5, 3.0, 6.0, 54.0}, {0, 0.5, 1, 2, 4, 8, 24, 48, 1e9},
                                                         {"< \xC2\xBD h", "\xC2\xBD\xE2\x80\x93" "1 h", "1\xE2\x80\x93" "2 h", "2\xE2\x80\x93" "4 h", "4\xE2\x80\x93" "8 h", "8\xE2\x80\x93" "24 h", "1\xE2\x80\x93" "2 j", "> 2 j"}, ch::colors::kOne, "demandes"));
                         return std::unique_ptr<ui::Widget>(std::move(c));
                     }});
    cases.push_back({"plages (debut, fin)", [] {
                         auto c = std::make_unique<ch::TimeChart>("p.plages");
                         ch::TimeChartData d;
                         d.kind = ch::Kind::Range;
                         d.x = ch::dailyAxis(ch::fr::day(2026, 10, 12), 30);
                         d.series = {serie("admin:1", "debut", std::vector<double>(30, 8.5)), serie("admin:1", "fin", std::vector<double>(30, 17.5))};
                         d.series[0].values[5] = std::nan("");
                         d.yMin = 6;
                         d.yMax = 24;
                         c->setData(d);
                         return std::unique_ptr<ui::Widget>(std::move(c));
                     }});
    cases.push_back({"vide", [] {
                         auto c = std::make_unique<ch::TimeChart>("p.vide");
                         ch::TimeChartData d;
                         d.x = ch::labelAxis({"a", "b"});
                         d.series = {serie("un", "x", {std::nan(""), std::nan("")})};
                         c->setData(d);
                         return std::unique_ptr<ui::Widget>(std::move(c));
                     }});
    cases.push_back({"la part de chacun", [] {
                         auto c = std::make_unique<ch::BarList>("p.part");
                         ch::BarListData d;
                         d.mode = ch::BarMode::Share;
                         d.partKeys = {"admin:0", "admin:1", "admin:2", "admin:3"};
                         d.partNames = {"Camille Essai (toi)", "L\xC3\xA9" "a Essai", "Hugo Essai", "Nina Essai"};
                         for (int i = 0; i < 5; ++i) {
                             ch::BarRow row;
                             row.label = "Accepter, refuser une demande";
                             row.parts = {9, 34, 21, 8};
                             row.extra = "\xE2\x88\x92" "15 %   L\xC3\xA9" "a 47 %";
                             d.rows.push_back(row);
                         }
                         c->setData(d);
                         return std::unique_ptr<ui::Widget>(std::move(c));
                     }});
    cases.push_back({"libelles trop longs", [] {
                         auto c = std::make_unique<ch::BarList>("p.longs");
                         ch::BarListData d;
                         ch::BarRow row;
                         row.label = "Soci\xC3\xA9t\xC3\xA9 d'essai des tr\xC3\xA8s longs noms de clients qui ne tiennent jamais dans la colonne";
                         row.value = 2450;
                         row.extra = "12 lic.";
                         d.rows = {row, row};
                         d.format = [](double v) { return ch::fr::euros(v); };
                         c->setData(d);
                         return std::unique_ptr<ui::Widget>(std::move(c));
                     }});
    cases.push_back({"la carte jour x heure", [] {
                         auto c = std::make_unique<ch::HeatMap>("p.carte");
                         ch::HeatMapData h;
                         h.rowLabels = {"lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."};
                         for (int i = 0; i < 24; ++i) h.colLabels.push_back(std::to_string(i) + " h");
                         h.cells.assign(7, std::vector<double>(24, 1.0));
                         h.bandFrom = 8.67;
                         h.bandTo = 17.83;
                         h.bandLabel = "ses heures habituelles";
                         h.flagLabel = "inhabituelle";
                         c->setData(h);
                         return std::unique_ptr<ui::Widget>(std::move(c));
                     }});
    cases.push_back({"la frise", [] {
                         auto c = std::make_unique<ch::Timeline>("p.frise");
                         ch::TimelineData td;
                         td.from = ch::fr::day(2024, 1, 1);
                         td.to = ch::fr::day(2027, 12, 31);
                         td.today = ch::fr::day(2026, 11, 10);
                         ch::TimelineRow tr;
                         tr.label = "XPG-2025-00188";
                         tr.sub = "2 PC";
                         tr.segments = {{ch::fr::day(2024, 12, 5), ch::fr::day(2026, 12, 5), "offre:PERSONAL", ""}};
                         tr.end = "05/12/2026 \xC2\xB7 dans 25 j";
                         tr.warn = true;
                         td.rows = {tr};
                         c->setData(td);
                         return std::unique_ptr<ui::Widget>(std::move(c));
                     }});
    cases.push_back({"la carte d'un graphique", [] {
                         auto card = std::make_unique<ch::ChartCard>("p.carte2", "Montant encaiss\xC3\xA9", "par semaine");
                         card->setValue(ch::fr::euros(26020), ch::deltaOf(26020, 23000), "contre 23 000 \xE2\x82\xAC l'an dernier");
                         card->setSide({{"PERSONAL", ch::fr::euros(9880), "38 %", ch::colors::kPersonal, false, false}});
                         auto c = std::make_unique<ch::TimeChart>("p.carte2.g");
                         c->setData(weeksBars());
                         card->setChart(std::move(c));
                         return std::unique_ptr<ui::Widget>(std::move(card));
                     }});
    for (const auto& cs : cases) {
        for (const gfx::Rect box : {gfx::Rect{40, 30, 520, 240}, gfx::Rect{40, 30, 230, 130}}) {
            auto w = cs.make();
            w->setBounds(box);
            paint(*w, r);
            std::string bad;
            const std::string size = box.w > 300 ? " (grand)" : " (petit)";
            const bool in = textInside(r, box, &bad);
            check(in, cs.name + size + " : le texte dans ses bornes" + (bad.empty() ? "" : " - \xC2\xAB " + bad + " \xC2\xBB"));
            bad.clear();
            const bool apart = noOverlap(r, &bad);
            check(apart, cs.name + size + " : aucun texte sur un autre" + (bad.empty() ? "" : " - \xC2\xAB " + bad + " \xC2\xBB"));
            check(r.finite, cs.name + size + " : rien de NaN envoye au rendu");
        }
    }
    // Une courbe : des triangles (le trait anticrenele) ; le theme sombre dessine aussi.
    ch::TimeChart lc("p.courbe");
    ch::TimeChartData d;
    d.x = ch::labelAxis({"a", "b", "c"});
    d.series = {serie("un", "x", {1, 3, 2})};
    lc.setData(d);
    lc.setBounds({0, 0, 300, 150});
    paint(lc, r);
    check(r.triangles > 0, "une courbe : un trait lisse (des triangles)");
    paint(lc, r, ui::Theme::dark());
    check(r.triangles > 0 && r.finite, "le theme sombre : la meme courbe");
    // Le texte en couleur de texte, jamais de la couleur de la serie.
    bool inkOnly = true;
    for (const auto& t : r.texts) inkOnly = inkOnly && !(t.c.r == ch::colors::kOne.r && t.c.g == ch::colors::kOne.g && t.c.b == ch::colors::kOne.b);
    check(inkOnly, "le texte n'est jamais de la couleur de la serie");
    // La bulle : sa taille, sa place (a droite, a gauche si elle ne tient pas).
    ch::Tip t;
    t.title = "Semaine du 26/10 au 01/11";
    t.rows = {{"2 520 \xE2\x82\xAC", "2026", ch::colors::kOne, false}, {"2 090 \xE2\x82\xAC", "l'an dernier", ch::colors::kCompare, true}};
    t.click = "Clic : ces commandes";
    const gfx::Size ts = ch::tipSize(t);
    check(ts.w >= 150.f && ts.h > 60.f, "la bulle a une taille");
    const gfx::Rect right = ch::placeTip(t, {100, 100, 20, 100}, {0, 0, 1373, 917});
    const gfx::Rect left = ch::placeTip(t, {1300, 100, 20, 100}, {0, 0, 1373, 917});
    check(right.x >= 120 && left.right() <= 1300 && left.x >= 0, "la bulle : a droite du point, a gauche pres du bord");
    const gfx::Rect low = ch::placeTip(t, {100, 900, 20, 10}, {0, 0, 1373, 917});
    check(low.bottom() <= 917, "la bulle reste dans la fenetre (en bas)");
    paint(lc, r);
    Recorder tr;
    ch::paintTip(tr, theme(), t, right);
    check(tr.shows("Semaine du 26/10 au 01/11") && tr.shows("Clic : ces commandes") && textInside(tr, right), "la bulle dessinee : tout dans son cadre");
}

} // namespace

int main() {
    installMetrics();
    testFormats();
    testDates();
    testScales();
    testColors();
    testTimeChart();
    testOthers();
    testPaint();
    std::printf("\n%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
