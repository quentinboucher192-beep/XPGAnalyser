// tests/simperf_test.cpp - 1.12.17 : la Vue d'ensemble de Simulation dans le temps, sans ecran.
//
//   simperf_test
//
//  LES PERFORMANCES DANS LE TEMPS : cinq minutes de marche jouees seconde par seconde (le
//  PerfHistory rempli comme le moteur le fait : dix cycles par seconde, un depassement a la
//  200e seconde), la periode (1 min, 10 min, depuis le lancement), les quatre cases, ou passe
//  le temps (les postes du cycle), Remettre a zero, Exporter en CSV, le point orange de la
//  courbe qui mene a sa lenteur. LES LENTEURS : les filtres, une lenteur ouverte (ce qu'il y
//  avait dedans, en retrait), Aller a, Vider (et sa confirmation), les seuils (pour le projet),
//  au-dela de 14 le lien vers le Journal. Arretee : la derniere marche.
#include "../src/app/SimCenter.hpp"
#include "../src/core/Edition.hpp"
#include "../src/ui/Theme.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace app;
namespace ss = app::simstatus;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// Un renderer qui retient les textes (8 px par caractere).
struct Recorder final : gfx::IRenderer {
    std::vector<std::string> texts;
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view t, gfx::FontId, gfx::Color) override { texts.emplace_back(t); }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    gfx::TextMetrics measure(std::string_view t, gfx::FontId) const override { return {8.f * static_cast<float>(t.size()), 16.f, 12.f, 4.f}; }
    float lineHeight(gfx::FontId) const override { return 16.f; }
    gfx::Size surfaceSize() const override { return {2000.f, 1000.f}; }
    float dpiScale() const override { return 1.f; }
    std::size_t fitCharacters(std::string_view t, gfx::FontId, float maxWidth) const override {
        return std::min(t.size(), static_cast<std::size_t>(std::max(0.f, maxWidth) / 8.f));
    }
    gfx::TextureId createImage(const std::uint8_t*, int, int) override { return {1}; }
    void drawImage(gfx::TextureId, const gfx::Rect&, float, bool, bool, gfx::Color) override {}
    bool wrote(std::string_view part) const {
        for (const auto& t : texts)
            if (t.find(part) != std::string::npos) return true;
        return false;
    }
};

bool hasKey(const std::vector<std::string>& keys, const std::string& k) { return std::find(keys.begin(), keys.end(), k) != keys.end(); }

hmi::SlowEntry slowOf(hmi::SlowKind kind, double at, double ms, double usual, std::string what, std::string where) {
    hmi::SlowEntry e;
    e.kind = kind;
    e.at = at;
    e.ms = ms;
    e.usual = usual;
    e.what = std::move(what);
    e.where = std::move(where);
    e.clock = "14:0" + std::to_string(static_cast<int>(at) / 60) + ":10.204";
    return e;
}

void vueEnsemble() {
    std::printf("1.12.17 : la Vue d'ensemble - les performances dans le temps, les lenteurs\n");
    core::setEdition(core::Edition::Ihm);
    // Cinq minutes de marche : dix cycles par seconde, 12 a 14 ms ; a la 200e seconde, un
    // cycle de 180 ms (la periode : 100 ms) et une lenteur.
    hmi::PerfHistory perf;
    perf.reset(0);
    perf.setPeriod(100);
    for (int s = 0; s < 300; ++s)
        for (int k = 0; k < 10; ++k) {
            const double now = s + k / 10.0;
            const double ms = s == 200 && k == 5 ? 180.0 : 12.0 + k % 3;
            perf.cycle(now, ms, 100);
            perf.post("script:1", "Script Chauffer (Vue_Four, \xC3\xA0 chaque cycle)", 4.0);
            perf.post("alarmes", "Alarmes", 1.0);
            perf.screen(3.0, 1.0, 40);
            if (s == 200 && k == 5) perf.slow();
            if (k == 0) perf.write();
        }
    perf.advance(300.0);
    hmi::SlowLog slow;
    auto clic = slowOf(hmi::SlowKind::Action, 200.5, 312, 40, "Clic sur Btn_Regler \xE2\x86\x92 ouvrir Pop_Reglage", "Vue_Four");
    clic.inside = {{"Ouvrir Pop_Reglage", 290, 0}, {"Script d'ouverture Init_Reglage", 270, 1}};
    clic.viewId = 3;
    clic.objectId = 7;
    clic.user = "jdupont";
    clic.view = "Vue_Four";
    clic.runs = 42;
    clic.slowRuns = 1;
    slow.add(clic);
    auto cyc = slowOf(hmi::SlowKind::Cycle, 200.5, 180, 0, "Cycle IHM : 180 ms pour une p\xC3\xA9riode de 100 ms", "IHM");
    cyc.period = 100;
    slow.add(cyc);
    slow.add(slowOf(hmi::SlowKind::Equipment, 250.2, 140, 20, "Lecture de 12 variables, r\xC3\xA9ponse lente", "Automate_1"));
    const auto clicSerial = slow.entries().front().serial;

    hmi::SlowRules rules;
    bool running = true;
    int resets = 0, clears = 0;
    std::vector<std::string> csvs, goes;
    std::vector<std::uint64_t> gone;
    SimCenterHosts hosts;
    hosts.perfView = [&] {
        SimPerfView v;
        v.perf = &perf;
        v.slow = &slow;
        v.rules = rules;
        v.now = 300.0;
        v.running = running;
        return v;
    };
    hosts.resetPerf = [&] { ++resets; };
    hosts.clearSlow = [&] {
        ++clears;
        slow.clear();
    };
    hosts.setSlowRules = [&](const hmi::SlowRules& r) { rules = r; };
    hosts.exportCsv = [&](const std::string& w) { csvs.push_back(w); };
    hosts.goSlow = [&](std::uint64_t s) { gone.push_back(s); };
    hosts.go = [&](const std::string& k) { goes.push_back(k); };

    auto model = std::make_shared<SimCenterModel>();
    ss::Snapshot snap;
    snap.project = true;
    snap.hmi.exists = true;
    snap.hmi.running = true;
    snap.hmi.view = "Vue_Four";
    model->report = ss::compute(snap);
    SimOverviewPane pane("ensemble", model, hosts);
    pane.setBounds({0, 0, 1400, 900});
    Recorder rec;
    const auto theme = ui::Theme::dark();
    const auto paint = [&] {
        rec.texts.clear();
        pane.layout();
        pane.render(ui::PaintContext{rec, theme, {0, 0, 1400, 900}, 0.0, nullptr});
    };
    paint();
    auto keys = pane.partKeys();
    check(rec.wrote("PERFORMANCES") && rec.wrote("LENTEURS"), "les deux blocs : PERFORMANCES et LENTEURS");
    check(rec.wrote("CYCLE IHM, EN MOYENNE") && rec.wrote("CHARGE DE LA P\xC3\x89RIODE (100 ms)") && rec.wrote("D\xC3\x89PASSEMENTS"),
          "les cases : le cycle, la charge de la p\xC3\xA9riode, les d\xC3\xA9passements");
    check(rec.wrote("LENTEURS (10 min)") && rec.wrote("10 min \xC2\xB7 3000 cycles"), "10 min par d\xC3\xA9" "faut : 3000 cycles");
    check(rec.wrote("Script Chauffer (Vue_Four, \xC3\xA0 chaque cycle)") && rec.wrote("Alarmes") && rec.wrote("O\xC3\x99 PASSE LE TEMPS"),
          "o\xC3\xB9 passe le temps : le script, les alarmes");
    check(rec.wrote("Dessin de la vue") && rec.wrote("3,0 ms") && rec.wrote("40 expressions"), "le dessin (3,0 ms), les expressions (40)");
    check(hasKey(keys, "perf:periode:60") && hasKey(keys, "perf:periode:600") && hasKey(keys, "perf:periode:3600") && hasKey(keys, "perf:periode:-1"),
          "les quatre p\xC3\xA9riodes");
    check(hasKey(keys, "perf:lenteur:200"), "la courbe : un point orange \xC3\xA0 la 200e seconde");

    // La periode : 1 min (la 200e seconde n'y est plus), puis depuis le lancement.
    check(pane.activate("perf:periode:60") && pane.perfPeriod() == 60, "1 min");
    paint();
    keys = pane.partKeys();
    check(rec.wrote("1 min \xC2\xB7 600 cycles") && rec.wrote("LENTEURS (1 min)"), "1 min : 600 cycles");
    check(!hasKey(keys, "perf:lenteur:200"), "1 min : le point de la 200e seconde n'est plus sur la courbe");
    check(pane.activate("perf:periode:-1") && pane.perfPeriod() < 0, "depuis le lancement");
    paint();
    check(rec.wrote("depuis le lancement \xC2\xB7 3000 cycles"), "depuis le lancement : 3000 cycles");
    (void)pane.activate("perf:periode:600");
    paint();

    // Remettre a zero, les exports.
    check(pane.activate("perf:zero") && resets == 1, "Remettre \xC3\xA0 z\xC3\xA9ro : l'h\xC3\xB4te repart d'une page blanche");
    check(pane.activate("perf:csv") && pane.activate("lenteurs:csv") && csvs == std::vector<std::string>{"performances", "lenteurs"},
          "Exporter en CSV : les performances, les lenteurs");

    // Les lenteurs : la plus recente d'abord, les filtres.
    check(rec.wrote("3 gard\xC3\xA9" "es") && rec.wrote("Tout \xC2\xB7 3") && rec.wrote("Actions \xC2\xB7 1") && rec.wrote("Cycles \xC2\xB7 1")
              && rec.wrote("\xC3\x89quipements \xC2\xB7 1"),
          "3 gard\xC3\xA9" "es ; Tout 3, Actions 1, Cycles 1, \xC3\x89quipements 1");
    check(rec.wrote("312 ms") && rec.wrote("40 ms \xC2\xB7 \xC3\x97" "7,8") && rec.wrote("p\xC3\xA9riode 100 ms"),
          "la dur\xC3\xA9" "e, son temps habituel (\xC3\x97" "7,8) ; un cycle : sa p\xC3\xA9riode");
    check(pane.activate("lenteurs:filtre:4") && pane.slowFilter() == 4, "le filtre Cycles");
    paint();
    keys = pane.partKeys();
    check(!hasKey(keys, "lenteur:" + std::to_string(clicSerial)) && hasKey(keys, "lenteur:" + std::to_string(clicSerial + 1)),
          "Cycles : le clic n'est plus montr\xC3\xA9, le cycle oui");
    (void)pane.activate("lenteurs:filtre:-1");
    paint();

    // Ouvrir une lenteur : ce qu'il y avait dedans, en retrait ; qui, ou ; Aller a.
    check(pane.activate("lenteur:" + std::to_string(clicSerial)) && pane.openedSlow() == clicSerial, "un clic ouvre la lenteur");
    paint();
    check(rec.wrote("Ouvrir Pop_Reglage") && rec.wrote("Script d'ouverture Init_Reglage") && rec.wrote("290 ms") && rec.wrote("270 ms"),
          "dedans : ouvrir la popup (290 ms), son script d'ouverture (270 ms)");
    check(rec.wrote("Utilisateur : jdupont") && rec.wrote("vue : Vue_Four") && rec.wrote("1 fois plus lente sur 42"),
          "qui, sur quelle vue, combien de fois plus lente");
    check(pane.activate("lenteur:" + std::to_string(clicSerial) + ":aller") && gone == std::vector<std::uint64_t>{clicSerial},
          "Aller \xC3\xA0 : l'h\xC3\xB4te ouvre l'objet");
    check(pane.activate("lenteur:" + std::to_string(clicSerial)) && pane.openedSlow() == 0, "un second clic la referme");

    // Le point orange de la courbe : la lenteur de cette seconde, ouverte, le bloc a l'ecran.
    pane.scrollTo(0);
    paint();
    check(pane.activate("perf:lenteur:200") && pane.openedSlow() == clicSerial, "le point orange ouvre la lenteur de la 200e seconde");
    paint();
    check(pane.scrollOffset() > 0.f, "et descend jusqu'au journal des lenteurs");

    // Les seuils : un clic, le suivant - pour le projet.
    check(pane.activate("lenteurs:seuil-ms") && rules.absoluteMs == 100, "le seuil : 50 ms \xE2\x86\x92 100 ms");
    check(pane.activate("lenteurs:seuil-fois") && rules.factor == 10, "le rapport : 5 \xE2\x86\x92 10 fois");
    paint();
    check(rec.wrote("100 ms") && rec.wrote("10 \xC3\x97"), "les seuils montr\xC3\xA9s : 100 ms, 10 \xC3\x97");

    // Vider : une confirmation d'abord ; Annuler garde tout ; Vider vide.
    check(pane.activate("lenteurs:vider") && pane.clearAsked() && clears == 0, "Vider\xE2\x80\xA6 : la confirmation d'abord");
    paint();
    check(rec.wrote("Vider les 3 lenteurs ?"), "Vider les 3 lenteurs ?");
    check(pane.activate("lenteurs:vider-non") && !pane.clearAsked() && clears == 0 && slow.entries().size() == 3, "Annuler : rien n'est vid\xC3\xA9");
    (void)pane.activate("lenteurs:vider");
    paint();
    check(pane.activate("lenteurs:vider-oui") && clears == 1 && slow.entries().empty() && pane.openedSlow() == 0, "Vider : le journal repart vide");
    paint();
    check(rec.wrote("Aucune lenteur : tout a r\xC3\xA9pondu \xC3\xA0 temps."), "vide : aucune lenteur");

    // Au-dela de 14 : le lien vers le Journal (filtre Lenteurs).
    for (int i = 0; i < 20; ++i) slow.add(slowOf(hmi::SlowKind::Script, 100.0 + i, 60.0 + i, 0, "Script Calcul_" + std::to_string(i), "Vue_Four"));
    paint();
    check(rec.wrote("et 6 de plus : le Journal, filtre Lenteurs"), "20 lenteurs : 14 ici, et 6 de plus dans le Journal");
    check(pane.activate("lenteurs:journal") && !goes.empty() && goes.back() == "journal-lenteurs", "le lien : le Journal, filtre Lenteurs");
    check(pane.activate("perf:lenteurs") == true, "la case Lenteurs m\xC3\xA8ne au journal (plus bas)");

    // Arretee : la derniere marche, dite.
    running = false;
    paint();
    check(rec.wrote("la derni\xC3\xA8re marche (arr\xC3\xAAt\xC3\xA9" "e)"), "arr\xC3\xAAt\xC3\xA9" "e : la derni\xC3\xA8re marche");

    // Sans mesure : la phrase d'attente.
    hmi::PerfHistory empty;
    hosts.perfView = [&] {
        SimPerfView v;
        v.perf = &empty;
        return v;
    };
    SimOverviewPane fresh("ensemble2", model, hosts);
    fresh.setBounds({0, 0, 1400, 900});
    rec.texts.clear();
    fresh.layout();
    fresh.render(ui::PaintContext{rec, theme, {0, 0, 1400, 900}, 0.0, nullptr});
    check(rec.wrote("Pas encore de mesure"), "pas encore de mesure : d\xC3\xA9marrez l'IHM");
    core::setEdition(core::Edition::Both);
}

} // namespace

int main() {
    vueEnsemble();
    std::printf("\n%s (%d \xC3\xA9" "chec%s)\n", failures ? "\xC3\x89" "CHEC" : "OK", failures, failures > 1 ? "s" : "");
    return failures ? 1 : 0;
}
