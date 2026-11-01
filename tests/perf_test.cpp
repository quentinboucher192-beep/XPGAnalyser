// =============================================================================
//  tests/perf_test.cpp - 1.22.2 : CE QUI CONSOMME (core/Perf, app/PerfPane)
// -----------------------------------------------------------------------------
//  Sans ecran, l'horloge a la main (setTestClock) :
//    1. les nombres (ms, Mo, %) ;
//    2. les portees : le temps propre, le temps avec ce qu'elle appelle, une
//       partie qui s'appelle elle-meme comptee une fois, le reste de l'image
//       (<< (hors mesure) >>), XPG_MESURE, un autre fil ne compte pas ;
//    3. les etapes a la suite (Lap) ;
//    4. les fenetres de 10 s, de 60 s, depuis le lancement ; la courbe ; la
//       remise a zero ;
//    5. le journal des saccades : le seuil, les trois parties, la borne de 200,
//       une longue dans l'historique interne (CallTrail) ;
//    6. le rapport texte ; les travaux de fond ; le processus ;
//    7. l'onglet Performances dessine (un renderer d'essai) : le haut, le
//       resume, les deux tableaux et leurs lignes, les puces, la recherche,
//       Copier le rapport, Remettre a zero, la mise en page etroite.
// =============================================================================
#include "../src/app/PerfPane.hpp"
#include "../src/core/CallTrail.hpp"
#include "../src/core/Perf.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/Widget.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace pf = core::perf;

namespace {

int g_ok = 0, g_fail = 0;
std::string g_clipboard;
std::int64_t g_clock = 1'000'000'000;   // l'horloge des essais, en ns

void check(bool cond, const std::string& what) {
    if (cond) {
        ++g_ok;
    } else {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

bool has(const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; }
bool nearly(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
void advanceMs(double ms) { g_clock += static_cast<std::int64_t>(std::llround(ms * 1e6)); }
std::int64_t ms(double v) { return static_cast<std::int64_t>(std::llround(v * 1e6)); }

const pf::PartStat* find(const pf::Window& w, std::string_view name) {
    for (const auto& p : w.parts)
        if (name == p.name) return &p;
    return nullptr;
}

// Une partie mesuree `millis` ms (l'horloge avance dedans).
void work(pf::Part p, double millis) {
    pf::Scope s(p);
    advanceMs(millis);
}

// Un renderer qui retient les textes.
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
    void fillTriangles(const gfx::Vertex*, std::size_t) override {}
    gfx::TextMetrics measure(std::string_view t, gfx::FontId f) const override {
        return {static_cast<float>(t.size()) * f.v * 0.5f, static_cast<float>(f.v), 0.8f * f.v, 0.2f * f.v};
    }
    float lineHeight(gfx::FontId f) const override { return static_cast<float>(f.v); }
    gfx::Size surfaceSize() const override { return {1600.f, 900.f}; }
    float dpiScale() const override { return 1.f; }
    std::size_t fitCharacters(std::string_view t, gfx::FontId f, float w) const override {
        return std::min(t.size(), static_cast<std::size_t>(std::max(0.f, w / (f.v * 0.5f))));
    }
    [[nodiscard]] bool saw(std::string_view what) const {
        return std::any_of(texts.begin(), texts.end(), [&](const std::string& t) { return t.find(what) != std::string::npos; });
    }
};

// ------------------------------------------------------------------ 1. les nombres ----
void testFormat() {
    std::printf("1. les nombres\n");
    check(pf::formatMs(0.42) == "0,42 ms", "0,42 ms (" + pf::formatMs(0.42) + ")");
    check(pf::formatMs(3.0) == "3,00 ms", "3,00 ms (" + pf::formatMs(3.0) + ")");
    check(pf::formatMs(12.34) == "12,3 ms", "12,3 ms (" + pf::formatMs(12.34) + ")");
    check(pf::formatMs(312.0) == "312 ms", "312 ms (" + pf::formatMs(312.0) + ")");
    check(pf::formatMs(1234.0) == "1,23 s", "1,23 s (" + pf::formatMs(1234.0) + ")");
    check(pf::formatMs(0.0) == "0 ms", "0 ms");
    check(pf::formatBytes(412ull * 1024 * 1024) == "412 Mo", "412 Mo (" + pf::formatBytes(412ull * 1024 * 1024) + ")");
    check(pf::formatBytes(3ull * 512 * 1024 * 1024) == "1,5 Go", "1,5 Go (" + pf::formatBytes(3ull * 512 * 1024 * 1024) + ")");
    check(pf::formatPercent(0.421) == "42,1 %", "42,1 % (" + pf::formatPercent(0.421) + ")");
    check(pf::windowLabel(10) == "10 s" && pf::windowLabel(0) == "depuis le lancement", "les fen\xC3\xAAtres : 10 s, depuis le lancement");
}

// ------------------------------------------------------------------ 2. les portees ----
void testScopes() {
    std::printf("2. les portees\n");
    pf::resetForTests();
    const pf::Part a = pf::part("essai.a");
    const pf::Part b = pf::part("essai.b");
    const std::string sameText = "essai.a";
    check(a != pf::kNoPart && b != pf::kNoPart && a != b && a != pf::kUnmeasured && pf::part("essai.a") == a && pf::part(sameText.c_str()) == a,
          "une partie : un num\xC3\xA9ro, le m\xC3\xAAme pour le m\xC3\xAAme nom");
    check(std::string(pf::partName(a)) == "essai.a" && std::string(pf::partName(pf::kUnmeasured)) == "(hors mesure)" && pf::partCount() >= 3,
          "leurs noms ; la partie 0 : (hors mesure)");

    pf::frameEnd();   // la premiere image : l'origine, rien ne compte
    check(pf::window(0).frames == 0, "la premi\xC3\xA8re fin d'image : l'origine seulement");
    // Une image de 10 ms : a 5 ms dont b 2 ms dedans ; b 1 ms seul ; 4 ms hors mesure.
    {
        pf::Scope sa(a);
        advanceMs(3);
        work(b, 2);
    }
    work(b, 1);
    advanceMs(4);
    pf::frameEnd();
    {
        const auto w = pf::window(10);
        const auto* pa = find(w, "essai.a");
        const auto* pb = find(w, "essai.b");
        const auto* rest = find(w, "(hors mesure)");
        check(w.frames == 1 && w.frameNs == ms(10) && w.worstFrameNs == ms(10), "une image de 10 ms");
        check(pa && pa->selfNs == ms(3) && pa->inclNs == ms(5) && pa->calls == 1, "a : 3 ms propres, 5 ms avec b");
        check(pb && pb->selfNs == ms(3) && pb->inclNs == ms(3) && pb->calls == 2, "b : 3 ms en 2 appels");
        check(rest && rest->selfNs == ms(4) && rest->calls == 1, "le reste de l'image : 4 ms hors mesure");
        check(!w.parts.empty() && w.parts.front().part == pf::kUnmeasured && w.parts.size() == 3 && w.parts[1].part == a && w.parts[2].part == b,
              "la plus gourmande d'abord (\xC3\xA0 \xC3\xA9galit\xC3\xA9 : par nom)");
        check(nearly(w.averageFrameMs(), 10.0) && nearly(w.fps(), 100.0), "100 images/s, 10 ms par image");
    }
    // Une partie qui s'appelle elle-meme : son temps avec ce qu'elle appelle n'est compte qu'une fois.
    {
        pf::Scope outer(a);
        advanceMs(1);
        work(a, 2);
    }
    pf::frameEnd();
    {
        const auto w = pf::window(0);
        const auto* pa = find(w, "essai.a");
        check(pa && pa->selfNs == ms(6) && pa->inclNs == ms(8) && pa->calls == 3 && pa->worstSelfNs == ms(3) && pa->worstInclNs == ms(5),
              "a dans a : 3 ms propres, 3 ms avec (pas 5) ; les pires : 3 et 5 ms");
    }
    // XPG_MESURE garde le numero de sa partie.
    for (int k = 0; k < 2; ++k) {
        XPG_MESURE("essai.macro");
        advanceMs(1);
    }
    pf::frameEnd();
    {
        const auto* m = find(pf::window(0), "essai.macro");
        check(m && m->calls == 2 && m->selfNs == ms(2), "XPG_MESURE : 2 appels, 2 ms");
    }
    // Un autre fil : rien ne compte (aucune lecture de l'horloge, aucun cumul).
    const auto before = find(pf::window(0), "essai.b")->calls;
    std::thread other([b] {
        pf::Scope s(b);
        check(!pf::onUiThread(), "un autre fil n'est pas celui de l'interface");
    });
    other.join();
    advanceMs(1);
    pf::frameEnd();
    check(find(pf::window(0), "essai.b")->calls == before && pf::onUiThread(), "une port\xC3\xA9" "e sur un autre fil ne compte pas");
}

// ------------------------------------------------------------------ 3. les etapes ----
void testLap() {
    std::printf("3. les \xC3\xA9tapes \xC3\xA0 la suite\n");
    pf::resetForTests();
    pf::frameEnd();
    const pf::Part outer = pf::part("essai.dehors");
    {
        pf::Scope s(outer);
        pf::Lap lap;
        lap.to(XPG_PARTIE("essai.etape1"));
        advanceMs(2);
        lap.to(XPG_PARTIE("essai.etape2"));
        advanceMs(3);
        lap.stop();
        advanceMs(1);
    }
    pf::frameEnd();
    const auto w = pf::window(0);
    const auto* e1 = find(w, "essai.etape1");
    const auto* e2 = find(w, "essai.etape2");
    const auto* o = find(w, "essai.dehors");
    check(e1 && e1->selfNs == ms(2) && e2 && e2->selfNs == ms(3), "les \xC3\xA9tapes : 2 ms, puis 3 ms");
    check(o && o->selfNs == ms(1) && o->inclNs == ms(6), "ce qui les contient : 1 ms propre, 6 ms avec elles");
}

// ------------------------------------------------------------------ 4. les fenetres ----
void testWindows() {
    std::printf("4. les fen\xC3\xAAtres de 10 s, de 60 s, depuis le lancement\n");
    pf::resetForTests();
    pf::setStutterThresholdMs(1000);    // pas de saccade ici
    g_clock = 100'000'000'000;
    pf::frameEnd();                    // l'origine : la seconde 0
    const pf::Part x = pf::part("essai.x");
    // 70 s a 25 images par seconde (40 ms chacune) : x prend 2 ms, puis 8 ms les 10 dernieres secondes.
    for (int j = 1; j <= 1750; ++j) {
        const double cost = j > 1500 ? 8.0 : 2.0;
        work(x, cost);
        advanceMs(40.0 - cost);
        pf::frameEnd();
    }
    const auto w10 = pf::window(10);
    const auto w60 = pf::window(60);
    const auto all = pf::window(0);
    const auto* x10 = find(w10, "essai.x");
    const auto* x60 = find(w60, "essai.x");
    const auto* xAll = find(all, "essai.x");
    check(all.frames == 1750 && xAll && xAll->calls == 1750 && nearly(all.span, 70.0, 1e-6), "depuis le lancement : 1 750 images, 70 s");
    check(w10.frames > 200 && w10.frames <= 276 && nearly(w10.fps(), 25.0, 1e-6) && nearly(w10.averageFrameMs(), 40.0, 1e-6),
          "10 s : 25 images/s de 40 ms (" + std::to_string(w10.frames) + " images)");
    check(x10 && nearly(static_cast<double>(x10->selfNs) / static_cast<double>(w10.frames) / 1e6, 8.0, 1e-6),
          "10 s : x prend 8 ms par image (les 10 derni\xC3\xA8res secondes)");
    const double avg60 = x60 ? static_cast<double>(x60->selfNs) / static_cast<double>(w60.frames) / 1e6 : 0.0;
    check(w60.frames > w10.frames && w60.frames < all.frames && avg60 > 2.5 && avg60 < 3.5,
          "60 s : 2 ms puis 8 ms, en moyenne ~3 ms (" + std::to_string(avg60) + ")");
    check(nearly(w10.span, 10.0) && nearly(w60.span, 60.0), "les fen\xC3\xAAtres couvrent 10 s et 60 s");
    const auto line = pf::timeline(60);
    const bool steady = std::all_of(line.begin(), line.end() - 1, [](const pf::Second& s) {
        return s.frames == 25 && nearly(s.averageMs, 40.0, 1e-6) && nearly(s.worstMs, 40.0, 1e-6);
    });
    check(line.size() == 60 && line.back().ago == 0 && line.front().ago == 59 && steady, "la courbe : 60 secondes de 25 images de 40 ms");
    // Une seconde sans image (l'appli figee) : un trou dans la courbe, la longue image dans la suivante.
    advanceMs(2500.0);
    pf::frameEnd();
    const auto after = pf::timeline(5);
    check(after.size() == 5 && after.back().frames == 1 && nearly(after.back().worstMs, 2500.0, 1e-6) && after[3].frames == 0,
          "une image de 2,5 s : un trou, puis sa barre");
    pf::reset();
    check(pf::window(0).frames == 0 && pf::window(10).frames == 0 && pf::window(10).parts.empty() && pf::stutterCount() == 0,
          "Remettre \xC3\xA0 z\xC3\xA9ro : tout repart de maintenant");
    work(x, 5);
    advanceMs(15);
    pf::frameEnd();
    check(pf::window(10).frames == 1 && find(pf::window(10), "essai.x") && find(pf::window(10), "essai.x")->calls == 1, "... et compte de nouveau");
    pf::setStutterThresholdMs(pf::kDefaultThresholdMs);
}

// ------------------------------------------------------------------ 5. les saccades ----
void testStutters() {
    std::printf("5. le journal des saccades\n");
    pf::resetForTests();
    core::trail::resetForTests();
    g_clock = 500'000'000'000;
    pf::frameEnd();
    check(pf::stutterThresholdMs() == 50, "le seuil d'origine : 50 ms");
    const pf::Part big = pf::part("essai.gros");
    const pf::Part mid = pf::part("essai.moyen");
    const pf::Part petit = pf::part("essai.petit");
    const pf::Part tiny = pf::part("essai.minus");
    advanceMs(30);
    pf::frameEnd();
    check(pf::stutterCount() == 0 && pf::stutters().empty(), "une image de 30 ms n'est pas une saccade");
    work(big, 40);
    work(mid, 15);
    work(petit, 3);
    work(tiny, 1);
    advanceMs(1);
    pf::frameEnd();
    {
        const auto list = pf::stutters();
        check(list.size() == 1 && list[0].serial == 1 && list[0].frameNs == ms(60) && list[0].count == 3 && list[0].top[0] == big
                  && list[0].top[1] == mid && list[0].top[2] == petit && list[0].topNs[0] == ms(40),
              "une image de 60 ms : ses trois parties, la plus gourmande d'abord");
        check(!list.empty() && pf::stutterParts(list[0]) == "essai.gros 40,0 ms ; essai.moyen 15,0 ms ; essai.petit 3,00 ms",
              "ce qui l'a prise : " + (list.empty() ? std::string{} : pf::stutterParts(list[0])));
        check(!list.empty() && pf::stutterClock(list[0]).size() == 12, "son heure : hh:mm:ss.mmm");
    }
    pf::setStutterThresholdMs(100);
    work(big, 60);
    pf::frameEnd();
    check(pf::stutterCount() == 1 && pf::stutterThresholdMs() == 100, "seuil 100 ms : une image de 60 ms n'entre pas");
    // La borne : les 200 dernieres.
    pf::setStutterThresholdMs(33);
    for (int k = 0; k < 250; ++k) {
        advanceMs(40);
        pf::frameEnd();
    }
    {
        const auto list = pf::stutters();
        check(list.size() == pf::kStutterCapacity && pf::stutterCount() == 251 && list.front().serial == 52 && list.back().serial == 251,
              "200 gard\xC3\xA9" "es au plus, les plus anciennes tombent (" + std::to_string(list.size()) + ")");
        check(pf::recentStutters(60) == pf::kStutterCapacity, "la pastille : les saccades de la derni\xC3\xA8re minute");
    }
    // Une longue (au moins 250 ms) : une ligne dans l'historique interne.
    work(big, 300);
    pf::frameEnd();
    bool noted = false;
    for (const auto& e : core::trail::recent(50))
        if (e.kind == core::trail::Kind::Watchdog && std::strstr(e.text, "saccade de 300 ms") && std::strstr(e.text, "essai.gros 300 ms")) noted = true;
    check(noted, "une saccade de 300 ms va dans l'historique interne (chien de garde)");
    check(pf::stutters().back().frameNs == ms(300), "et dans le journal");
}

// ------------------------------------------------------------------ 6. le rapport, les travaux, le processus ----
void testReport() {
    std::printf("6. le rapport, les travaux de fond, le processus\n");
    const std::string r = pf::report(60);
    check(has(r, "rapport de performances") && has(r, "Syst\xC3\xA8me : ") && has(r, "Cadence (60 s)") && has(r, "Saccades : seuil 33 ms"),
          "le rapport : la configuration, la cadence, le seuil");
    check(has(r, "Les 20 parties qui consomment le plus (60 s") && has(r, "essai.gros") && has(r, "(hors mesure)"), "... les parties");
    const auto at = r.find("derni\xC3\xA8res saccades");
    std::size_t lines = 0;
    if (at != std::string::npos)
        for (std::size_t p = r.find('\n', at); p != std::string::npos && p + 1 < r.size(); p = r.find('\n', p + 1)) ++lines;
    check(at != std::string::npos && lines == 20 && has(r.substr(at), "300 ms"), "... les 20 derni\xC3\xA8res saccades, la plus r\xC3\xA9" "cente d'abord");
    // Un travail de fond : en cours, puis fini avec sa duree.
    std::atomic<bool> started{false}, go{false};
    std::thread t([&] {
        XPG_TRAVAIL("essai.travail");
        started = true;
        while (!go) std::this_thread::yield();
    });
    while (!started) std::this_thread::yield();
    const auto running = pf::runningJobs();
    check(std::any_of(running.begin(), running.end(), [](const pf::JobInfo& j) { return std::string(j.name) == "essai.travail" && j.endedAgo < 0.0; }),
          "un travail de fond en cours");
    check(has(pf::report(60), "essai.travail (depuis"), "... il est dans le rapport");
    go = true;
    t.join();
    const auto runningAfter = pf::runningJobs();
    const auto done = pf::finishedJobs(5);
    check(std::none_of(runningAfter.begin(), runningAfter.end(), [](const pf::JobInfo& j) { return std::string(j.name) == "essai.travail"; })
              && !done.empty() && std::string(done.front().name) == "essai.travail" && done.front().seconds >= 0.0 && done.front().endedAgo >= 0.0,
          "fini : parmi les derniers finis, avec sa dur\xC3\xA9" "e");
    const auto ps = pf::processStats();
    check(ps.cores > 0 && ps.cpuKnown && ps.cpuSeconds >= 0.0, "le processus : les processeurs, le temps processeur");
#if defined(__linux__) || defined(_WIN32)
    check(ps.memoryKnown && ps.workingSet > 0, "le processus : sa m\xC3\xA9moire");
#endif
}

// ------------------------------------------------------------------ 7. l'onglet ----
void testPane() {
    std::printf("7. l'onglet Performances\n");
    const auto theme = ui::Theme::dark();
    app::PerfPane pane("perf");
    pane.setBounds({0, 0, 1400, 320});
    pane.layout();
    Recorder rec;
    pane.render(ui::PaintContext{rec, theme, {0, 0, 1400, 320}, 0.0, nullptr});
    check(rec.saw("Fen\xC3\xAAtre") && rec.saw("10 s") && rec.saw("60 s") && rec.saw("Depuis le lancement") && rec.saw("33 ms") && rec.saw("250 ms")
              && rec.saw("Remettre \xC3\xA0 z\xC3\xA9ro") && rec.saw("Copier le rapport"),
          "le haut : la fen\xC3\xAAtre, le seuil, les boutons");
    check(rec.saw("images/s") && rec.saw("M\xC3\xA9moire") && rec.saw("Travaux de fond"), "le r\xC3\xA9sum\xC3\xA9 : la cadence, la m\xC3\xA9moire, les travaux de fond");
    check(rec.saw("images/s (") && rec.saw("60 derni\xC3\xA8res secondes"), "la courbe : le temps d'une image et la cadence (60 s)");
    check(rec.saw("Ce qui consomme") && rec.saw("Saccades d'au moins 33 ms"), "les titres des deux tableaux");
    auto& parts = pane.partsTable();
    auto& list = pane.stuttersTable();
    check(parts.model() && parts.model()->rowCount() >= 4 && rec.saw("essai.gros") && rec.saw("(hors mesure)") && rec.saw("Partie")
              && parts.model()->headerText(3) == "Pire image",
          "ce qui consomme : une ligne par partie, dessin\xC3\xA9" "e");
    check(list.model() && list.model()->rowCount() == pf::kStutterCapacity && list.visibleRowCount() == pf::kStutterCapacity
              && list.viewRow(0) == pf::kStutterCapacity - 1 && rec.saw("essai.gros 300 ms"),
          "les saccades : 200, la plus r\xC3\xA9" "cente en haut");
    check(parts.model() && parts.model()->rowCount() > 0 && !parts.model()->rowTooltip(0).empty()
              && has(parts.model()->rowTooltip(0), "Temps propre"),
          "l'infobulle d'une partie : son temps propre, avec ce qu'elle appelle, ses appels");
    // Copier une ligne (le clic droit des tableaux de l'appli : Copier).
    parts.selectModelRows({0}, false);
    check(parts.copySelection(true) == 1 && has(g_clipboard, "Partie") && has(g_clipboard, "Cumul"), "Copier une ligne (avec les titres)");
    // Les puces : la fenetre, le seuil.
    pane.trigger(app::PerfPane::kWindow10);
    check(pane.windowSeconds() == 10 && pane.shownWindow().seconds == 10, "la fen\xC3\xAAtre : 10 s");
    gfx::Rect r;
    check(pane.actionRect(app::PerfPane::kWindowAll, r), "la puce Depuis le lancement");
    (void)pane.dispatch(ui::MouseDown{{r.x + 3.f, r.y + r.h / 2.f}, ui::MouseButton::Left, 1, {}});
    check(pane.windowSeconds() == 0, "un clic : depuis le lancement");
    pane.trigger(app::PerfPane::kThreshold250);
    check(pf::stutterThresholdMs() == 250 && list.model()->rowCount() == 1, "seuil 250 ms : une saccade (" + std::to_string(list.model()->rowCount()) + ")");
    pane.trigger(app::PerfPane::kThreshold33);
    // La recherche du panneau : les parties dont le nom la contient.
    pane.setSearch("GROS");
    check(parts.model()->rowCount() == 1 && parts.model()->cellText(0, 0) == "essai.gros", "la recherche : essai.gros seule");
    pane.setSearch("");
    check(parts.model()->rowCount() >= 4, "la recherche vid\xC3\xA9" "e : toutes");
    // La courbe : l'infobulle d'une seconde.
    const gfx::Rect chart = pane.chartRect();
    check(chart.w > 60.f && chart.h > 40.f && has(pane.liveTooltip({chart.right() - 8.f, chart.y + chart.h * 0.5f}), "image"),
          "la courbe : la seconde sous la souris");
    // Copier le rapport.
    std::string said;
    core::ConnectionScope links;
    links += pane.said->connect([&](const std::string& s) { said = s; });
    g_clipboard.clear();
    pane.trigger(app::PerfPane::kCopy);
    check(has(g_clipboard, "rapport de performances") && has(g_clipboard, "essai.gros") && has(said, "copi\xC3\xA9"), "Copier le rapport");
    check(has(pane.statusText(), "saccade"), "la barre d'\xC3\xA9tat du panneau");
    // L'onglet n'est pas a l'ecran : rien n'est relu.
    check(!pane.tick(false), "cach\xC3\xA9 : rien n'est relu");
    check(pane.tick(true), "montr\xC3\xA9 : relu tout de suite");
    // Remettre a zero.
    pane.trigger(app::PerfPane::kReset);
    check(list.model()->rowCount() == 0 && pf::stutterCount() == 0 && has(said, "z\xC3\xA9ro"), "Remettre \xC3\xA0 z\xC3\xA9ro : le journal vide");
    // Etroit (le panneau a droite de l'editeur) : l'un sous l'autre.
    pane.setBounds({0, 0, 500, 700});
    pane.layout();
    check(parts.bounds().w <= 500.f && list.bounds().w <= 500.f && parts.bounds().y > pane.chartRect().y && list.bounds().y > parts.bounds().y
              && parts.bounds().h > 40.f && list.bounds().h > 40.f,
          "\xC3\xA9troit : la courbe, ce qui consomme, les saccades, l'un sous l'autre");
    gfx::Rect copy;
    check(pane.actionRect(app::PerfPane::kCopy, copy) && copy.right() <= 500.f, "\xC3\xA9troit : les boutons restent dans le volet");
}

} // namespace

int main() {
    {
        ui::PlatformServices s;
        s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(t.size()) * f.v * 0.5f; };
        s.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v); };
        s.clipboardSet = [](std::string_view t) { g_clipboard = std::string(t); };
        s.clipboardGet = [] { return g_clipboard; };
        ui::installPlatformServices(std::move(s));
    }
    pf::attachUiThread();
    pf::setTestClock(&g_clock);
    testFormat();
    testScopes();
    testLap();
    testWindows();
    testStutters();
    testReport();
    testPane();
    pf::setTestClock(nullptr);
    std::printf("perf_test : %d r\xC3\xA9ussis, %d \xC3\xA9" "checs\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
