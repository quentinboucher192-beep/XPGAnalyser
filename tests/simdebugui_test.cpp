// tests/simdebugui_test.cpp - lot API 8 : Simulation > Debogage, ce qui se verifie sans ecran.
//
//   simdebugui_test [MAST.XPG]
//
//  1. Les nombres en francais (1 243 ; 0,042 ms ; 12,5 ms ; 31 % ; < 0,1 %).
//  2. Un point d'arret tape (SFC_PurgeA 42, SFC_PurgeA:42, « ligne 42 »,
//     « l.42 », une condition apres « si ») ; ce qui est refuse, et pourquoi.
//  3. La pile, niveau par niveau (la tache, l'unite, la section, l'instance,
//     la section du bloc) ; ce qu'un clic montre.
//  4. L'etat en clair et « pourquoi ici ».
//  5. Le temps du cycle : la plus lente en tete, les pourcentages, le resume ;
//     la trace (l'ordre de MAST, la premiere section de chaque entree).
//  6. Les noms d'une ligne de ST (les valeurs a droite du code).
//  7. Les espions : depuis quand ; qui a ecrit.
//  8. L'editeur de code : la colonne des points d'arret (un clic, Maj+clic, F9,
//     Ctrl+F9), le repli toujours a sa place, le curseur au meme endroit sans
//     la colonne ; l'endroit ou cliquer pour une ligne.
//  9. Sur le projet d'essai : chaque section retrouvee par son nom de simulateur.
#include "../src/app/SimDebugText.hpp"
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/CrossReference.hpp"   // 1.11.2 (D25) : les lignes qui ecrivent, comme le panneau
#include "../src/sim/Runtime.hpp"          // Lot API 8 (2e partie) : la condition lue maintenant
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace sd = app::simdebug;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string joined(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : " | ") + s;
    return out;
}

// Les metriques de la police de secours de SDL (8 x 8, a l'echelle entiere) :
// une face de 16 px avance de 16 px par glyphe, comme dans fold_test.
class RecordingRenderer final : public gfx::IRenderer {
public:
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    void drawText(gfx::Point, std::string_view, gfx::FontId, gfx::Color) override {}
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        const float adv = 8.f * scaleFor(f);
        std::size_t glyphs = 0;
        for (const char c : s) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++glyphs;
        return {static_cast<float>(glyphs) * adv, adv, adv * 0.8f, adv * 0.2f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return 8.f * scaleFor(f); }
    [[nodiscard]] gfx::Size surfaceSize() const override { return {1536.f, 1024.f}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f, float maxWidth) const override {
        const float adv = 8.f * scaleFor(f);
        if (maxWidth <= 0.f || adv <= 0.f) return 0;
        return std::min<std::size_t>(s.size(), static_cast<std::size_t>(maxWidth / adv));
    }
private:
    static float scaleFor(gfx::FontId f) { return std::max(1.f, std::round(static_cast<float>(f.v ? f.v : 16) / 8.f)); }
};

} // namespace

int main(int argc, char** argv) {
    const std::string nnbsp = "\xE2\x80\xAF";   // l'espace fine insecable des milliers
    const std::string nbsp = "\xC2\xA0";        // l'espace insecable avant %

    std::printf("1. Les nombres\n");
    {
        check(sd::grouped(1243) == "1" + nnbsp + "243" && sd::grouped(12) == "12" && sd::grouped(1000000) == "1" + nnbsp + "000" + nnbsp + "000",
              "1 243 ; 12 ; 1 000 000");
        check(sd::millis(42) == "0,042 ms" && sd::millis(420) == "0,42 ms" && sd::millis(1250) == "1,25 ms" && sd::millis(2000) == "2 ms",
              "0,042 ms ; 0,42 ms ; 1,25 ms ; 2 ms");
        check(sd::millis(12500) == "12,5 ms" && sd::millis(1204000) == "1" + nnbsp + "204 ms" && sd::millis(0) == "0 ms" && sd::millis(-5) == "0 ms",
              "12,5 ms ; 1 204 ms ; 0 ms (et un negatif)");
        check(sd::percent(31.2) == "31" + nbsp + "%" && sd::percent(4.5) == "4,5" + nbsp + "%" && sd::percent(0.05) == "< 0,1" + nbsp + "%"
                  && sd::percent(0.0) == "0" + nbsp + "%",
              "31 % ; 4,5 % ; < 0,1 % ; 0 %");
        check(sd::plural(1, "cycle", "cycles") == "1 cycle" && sd::plural(39, "cycle", "cycles") == "39 cycles", "1 cycle ; 39 cycles");
    }

    std::printf("2. Un point d'arr\xC3\xAAt tap\xC3\xA9\n");
    {
        sd::TypedBreakpoint b;
        std::string why;
        check(sd::parseBreakpoint("SFC_PurgeA 42", b) && b.section == "SFC_PurgeA" && b.line == 42 && b.condition.empty(), "SFC_PurgeA 42");
        check(sd::parseBreakpoint("SFC_PurgeA:42", b) && b.section == "SFC_PurgeA" && b.line == 42, "SFC_PurgeA:42");
        check(sd::parseBreakpoint("  SFC_PurgeA, ligne 42 ", b) && b.section == "SFC_PurgeA" && b.line == 42, "SFC_PurgeA, ligne 42");
        check(sd::parseBreakpoint("SFC_PurgeA l.42", b) && b.section == "SFC_PurgeA" && b.line == 42, "SFC_PurgeA l.42");
        check(sd::parseBreakpoint("Total 12", b) && b.section == "Total" && b.line == 12, "Total 12 : le l final reste au nom");
        check(sd::parseBreakpoint("DFB_X.Main 7 si Armoires[0].etat = 3", b) && b.section == "DFB_X.Main" && b.line == 7
                  && b.condition == "Armoires[0].etat = 3",
              "DFB_X.Main 7 si Armoires[0].etat = 3 : la section d'un bloc, une condition");
        // Le refus d'abord, sa raison ensuite : l'ordre des arguments d'un appel n'est pas fixe.
        const auto refused = [&](const char* text, const char* part, const std::string& what) {
            why.clear();
            const bool no = !sd::parseBreakpoint(text, b, &why);
            check(no && contains(why, part), what + " : refus\xC3\xA9 (" + why + ")");
        };
        refused("SFC_PurgeA", "num\xC3\xA9ro", "sans ligne");
        refused("42", "section", "sans section");
        refused("SFC Purge 3", "pas un nom", "un nom avec une espace");
        refused("SFC_PurgeA ligne 0", "commencent", "la ligne 0");
    }

    std::printf("3. La pile\n");
    {
        using K = sd::StackLevel::Kind;
        const std::vector<std::string> stack = {"MAST", "Logigrammes_A", "SFC_PurgeA", "Gc_PurgeA (DFB_GRAFCETENGINE)", "Main"};
        const auto levels = sd::stackLevels(stack, "DFB_GRAFCETENGINE.Main");
        check(levels.size() == 5, "cinq niveaux");
        if (levels.size() == 5) {
            check(levels[0].kind == K::Task && levels[1].kind == K::Unit && levels[2].kind == K::Section && levels[3].kind == K::Instance
                      && levels[4].kind == K::BlockSection,
                  "t\xC3\xA2" "che, unit\xC3\xA9, section, instance, section du bloc");
            check(levels[2].section == "SFC_PurgeA", "la section : un clic montre SFC_PurgeA");
            check(levels[3].name == "Gc_PurgeA" && levels[3].type == "DFB_GRAFCETENGINE" && levels[3].section == "DFB_GRAFCETENGINE.Main",
                  "l'instance : son nom, son bloc ; un clic montre DFB_GRAFCETENGINE.Main");
            check(levels[4].section == "DFB_GRAFCETENGINE.Main" && levels[4].instance == "Gc_PurgeA", "la section du bloc, pour Gc_PurgeA");
            check(contains(sd::levelTip(levels[3]), "Gc_PurgeA") && contains(sd::levelTip(levels[0]), "MAST"), "les infobulles les nomment");
        }
        const auto nested = sd::stackLevels({"MAST", "SFC_X", "a (T1)", "Corps", "b (T2)", "Main"}, "T2.Main");
        check(nested.size() == 6 && nested[1].kind == K::Section && nested[4].kind == K::Instance && nested[4].instance == "a.b"
                  && nested[5].section == "T2.Main" && nested[5].instance == "a.b",
              "une instance dans un bloc : a.b, T2.Main");
        const auto plain = sd::stackLevels({"MAST", "SFC_X"}, "SFC_X");
        check(plain.size() == 2 && plain[0].kind == K::Task && plain[1].kind == K::Section, "une section de t\xC3\xA2" "che : t\xC3\xA2" "che, section");
        const auto none = sd::stackLevels({}, "SFC_X");
        check(none.size() == 1 && none[0].kind == K::Section && none[0].section == "SFC_X", "sans pile : la section du passage");
    }

    std::printf("4. L'\xC3\xA9tat en clair, pourquoi ici\n");
    {
        using S = app::SimulationHost::State;
        app::SimBreakpoint bp;
        bp.id = 7;
        bp.section = "SFC_PurgeA";
        bp.line = 42;
        bp.condition = "Armoires[0].etat = 3";
        bp.hits = 5;
        app::SimBreakHit hit;
        hit.id = 7;
        hit.section = "SFC_PurgeA";
        hit.line = 42;
        hit.scan = 1243;
        hit.stack = {"MAST", "Logigrammes_A", "SFC_PurgeA"};
        sd::StateFacts f;
        f.state = S::Paused;
        f.attached = true;
        f.cycle = 1243;
        f.hit = &hit;
        f.breakpoint = &bp;
        f.number = 2;
        f.active = 3;
        f.total = 3;
        const auto sentence = sd::stateSentence(f);
        check(sentence == "En pause au point d'arr\xC3\xAAt 2 : SFC_PurgeA, ligne 42, cycle 1" + nnbsp
                              + "243 \xE2\x80\x94 la condition Armoires[0].etat = 3 est vraie",
              "en pause au point d'arr\xC3\xAAt 2 : " + sentence);
        const auto why = sd::whyHere(f);
        check(why.size() >= 3 && why[0] == "Le point d'arr\xC3\xAAt 2 est pos\xC3\xA9 sur SFC_PurgeA, ligne 42. Il l'arr\xC3\xAAte pour la 5e fois.",
              "pourquoi ici, 1 : " + (why.empty() ? std::string() : why[0]));
        check(contains(joined(why), "\xC2\xAB Armoires[0].etat = 3 \xC2\xBB est vraie"), "pourquoi ici : la condition, entre guillemets");
        // La maquette : "C'est vrai : ... vaut ..." - un nom de la condition lu par la ligne (mot entier).
        hit.values = {{"etat", "9"}, {"Armoires[0].etat", "3"}, {"x", "1"}};
        check(contains(joined(sd::whyHere(f)), "C'est vrai : Armoires[0].etat vaut 3."), "pourquoi ici : ce que vaut le nom de la condition");
        check(!contains(joined(sd::whyHere(f)), "etat vaut 9"), "pourquoi ici : un nom partiel ne compte pas");
        hit.values.clear();
        check(contains(joined(why), "Continuer (F5)") && contains(joined(why), "Section suivante (F10)"), "pourquoi ici : ce que font les boutons");
        hit.stack = {"MAST", "Logigrammes_A", "SFC_PurgeA", "Gc_PurgeA (DFB_GRAFCETENGINE)", "Main"};
        check(contains(joined(sd::whyHere(f)), "pour l'instance Gc_PurgeA, appel\xC3\xA9" "e depuis SFC_PurgeA"), "pourquoi ici : l'instance et d'o\xC3\xB9 elle est appel\xC3\xA9" "e");
        bp.condition.clear();
        check(sd::stateSentence(f) == "En pause au point d'arr\xC3\xAAt 2 : SFC_PurgeA, ligne 42, cycle 1" + nnbsp + "243", "sans condition : la phrase s'arr\xC3\xAAte au cycle");
        check(contains(joined(sd::whyHere(f)), "pas de condition"), "pourquoi ici : pas de condition");
        check(contains(joined(sd::whyHere(f)), "donne-lui une condition"), "pourquoi ici : le conseil de la maquette (une condition)");
        check(contains(joined(sd::whyHere(f)), "reprendra ici, pas au cycle 0"), "pourquoi ici : modifier le projet reprend au m\xC3\xAAme cycle");

        sd::StateFacts run;
        run.state = S::Running;
        run.attached = true;
        run.cycle = 1243;
        run.active = 3;
        check(sd::stateSentence(run) == "En marche, cycle 1" + nnbsp + "243 \xE2\x80\x94 3 points d'arr\xC3\xAAt actifs", "en marche : " + sd::stateSentence(run));
        run.active = 0;
        check(contains(sd::stateSentence(run), "aucun point d'arr\xC3\xAAt"), "en marche sans point d'arr\xC3\xAAt : le dire");
        sd::StateFacts stopped;
        stopped.attached = true;
        stopped.active = 2;
        check(contains(sd::stateSentence(stopped), "Arr\xC3\xAAt\xC3\xA9" "e") && contains(sd::stateSentence(stopped), "2 points d'arr\xC3\xAAt l'attendent"),
              "arr\xC3\xAAt\xC3\xA9" "e : " + sd::stateSentence(stopped));
        sd::StateFacts step;
        step.state = S::Paused;
        step.attached = true;
        step.cycle = 12;
        step.gesture = sd::LastGesture::StepSection;
        step.nextSection = "SFC_PurgeA";
        check(sd::stateSentence(step) == "En pause apr\xC3\xA8s une section, cycle 12 \xE2\x80\x94 la suivante : SFC_PurgeA", "pas \xC3\xA0 pas : " + sd::stateSentence(step));
        check(contains(joined(sd::whyHere(step)), "La prochaine section \xC3\xA0 s'ex\xC3\xA9" "cuter : SFC_PurgeA."), "pas \xC3\xA0 pas : la prochaine section");
        sd::StateFacts halted;
        halted.state = S::Halted;
        halted.attached = true;
        halted.cycle = 3;
        halted.haltMessage = "division par z\xC3\xA9ro";
        check(sd::stateSentence(halted) == "Halte au cycle 3 : division par z\xC3\xA9ro", "halte : " + sd::stateSentence(halted));
    }

    std::printf("5. Le temps du cycle, la trace\n");
    {
        std::vector<app::SimSectionTime> t = {
            {"Init", "Init", 10, 100}, {"Logigrammes_A", "SFC_PurgeA", 900, 3000}, {"Logigrammes_A", "SFC_PompageA", 800, 3000}, {"Fin", "Fin", 1, 0}};
        const auto bars = sd::timeBars(t, 20);
        check(bars.size() == 4 && bars[0].section == "SFC_PurgeA" && bars[1].section == "SFC_PompageA" && bars[2].section == "Init" && bars[3].section == "Fin",
              "la plus lente en t\xC3\xAAte ; \xC3\xA0 \xC3\xA9galit\xC3\xA9, l'ordre de MAST");
        check(bars.size() == 4 && std::fabs(bars[0].ofPeriod - 15.0) < 1e-9 && std::fabs(bars[0].ofCycle - 3000.0 * 100.0 / 6100.0) < 1e-9,
              "3 ms : 15 % de 20 ms, 49 % du cycle");
        check(bars.size() == 4 && bars[0].label == "Logigrammes_A \xE2\x80\xBA SFC_PurgeA" && bars[2].label == "Init", "le libell\xC3\xA9 : l'unit\xC3\xA9 \xE2\x80\xBA la section ; une section seule");
        const auto summary = sd::cycleSummary(t, 20);
        check(summary == "Le cycle : 6,1 ms sur 20 ms (31" + nbsp + "%) \xC2\xB7 la plus lente : Logigrammes_A \xE2\x80\xBA SFC_PurgeA, 3 ms", "r\xC3\xA9sum\xC3\xA9 : " + summary);
        std::vector<app::SimSectionTime> slow = {{"A", "A", 1, 26000}};
        check(contains(sd::cycleSummary(slow, 20), "plus long que la p\xC3\xA9riode"), "un cycle plus long que la p\xC3\xA9riode : le dire");
        check(sd::cycleSummary({}, 20).empty(), "pas de temps : pas de r\xC3\xA9sum\xC3\xA9");
        const auto trace = sd::traceRows(t);
        check(trace.size() == 4 && trace[0].rank == 1 && trace[0].firstOfEntry && trace[1].firstOfEntry && !trace[2].firstOfEntry && trace[3].firstOfEntry,
              "la trace : l'ordre de MAST, la premi\xC3\xA8re section de chaque entr\xC3\xA9" "e");
    }

    std::printf("6. Les noms d'une ligne\n");
    {
        bool comment = false;
        auto names = sd::lineSymbols("IF Armoires[0].etat = 3 AND NOT bVanne THEN (* x y *)", comment);
        check(joined(names) == "Armoires[0].etat | bVanne" && !comment, "IF Armoires[0].etat = 3 AND NOT bVanne : " + joined(names));
        names = sd::lineSymbols("x[i + 1] := TON_1.Q OR f(y, z);", comment);
        check(joined(names) == "i | TON_1.Q | y | z", "un indice calcul\xC3\xA9, un appel : " + joined(names));
        names = sd::lineSymbols("Tempo(IN := bStart, PT := T#2s);", comment);
        check(joined(names) == "bStart", "les param\xC3\xA8tres d'un appel, un litt\xC3\xA9ral de dur\xC3\xA9" "e : " + joined(names));
        names = sd::lineSymbols("%MW10 := 16#FF; s := 'abc(* pas un commentaire';", comment);
        check(joined(names) == "%MW10 | s" && !comment, "une adresse, une cha\xC3\xAEne : " + joined(names));
        names = sd::lineSymbols("a := 1; (* d\xC3\xA9" "but", comment);
        check(joined(names) == "a" && comment, "un commentaire ouvert");
        names = sd::lineSymbols("fin *) b := a + 1; // c := 2", comment);
        check(joined(names) == "b | a" && !comment, "le commentaire se ferme ; // : la fin de la ligne");
        names = sd::lineSymbols("a := a + 1;", comment);
        check(joined(names) == "a", "sans doublon");
        names = sd::lineSymbols("Grille[2, 5] := REAL_TO_INT(Mes.val);", comment);
        check(joined(names) == "Grille[2,5] | Mes.val", "une case \xC3\xA0 deux indices, une conversion : " + joined(names));
    }

    std::printf("7. Les espions, qui a \xC3\xA9" "crit\n");
    {
        check(sd::sinceText(false, 0, 10, 20) == "inchang\xC3\xA9" "e depuis l'ajout", "jamais chang\xC3\xA9" "e");
        check(sd::sinceText(true, 1204, 1243, 20) == "chang\xC3\xA9" "e au cycle 1" + nnbsp + "204 (il y a 39 cycles, 0,8 s)",
              "chang\xC3\xA9" "e au cycle 1 204 : " + sd::sinceText(true, 1204, 1243, 20));
        check(sd::sinceText(true, 5, 5, 20) == "vient de changer (ce cycle)", "ce cycle");
        check(contains(sd::sinceText(true, 0, 10000, 20), "3 min 20 s"), "10 000 cycles de 20 ms : 3 min 20 s");
        app::SimLastWrite w;
        w.section = "SFC_PurgeA";
        w.line = 42;
        w.scan = 1243;
        check(sd::writeText(w, 1243) == "SFC_PurgeA, ligne 42, au cycle 1" + nnbsp + "243 (ce cycle)", "qui a \xC3\xA9" "crit : ce cycle");
        check(sd::writeText(w, 1245) == "SFC_PurgeA, ligne 42, au cycle 1" + nnbsp + "243 (il y a 2 cycles)", "qui a \xC3\xA9" "crit : il y a 2 cycles");
    }

    std::printf("8. L'\xC3\xA9" "diteur : la colonne des points d'arr\xC3\xAAt\n");
    {
        using namespace ui;
        RecordingRenderer renderer;
        PlatformServices services;
        services.measureWidth = [&](std::string_view t, gfx::FontId f) { return renderer.measure(t, f).width; };
        services.lineHeight = [&](gfx::FontId f) { return renderer.lineHeight(f); };
        services.clipboardGet = [] { return std::string{}; };
        services.clipboardSet = [](std::string_view) {};
        installPlatformServices(std::move(services));
        const Theme theme = Theme::dark();
        const std::string st = "IF a THEN\n  b := 1;\n  c := 2;\nEND_IF;\n";

        // Sans la colonne : rien ne bouge (la marge, le curseur).
        MultiLineText plain("plain");
        plain.setLanguage(Language::StructuredText);
        plain.setText(st);
        plain.setBounds({0.f, 0.f, 800.f, 300.f});
        plain.layout();
        plain.render(PaintContext{renderer, theme, {0, 0, 800, 300}, 0.0, nullptr});
        const float plainGutter = plain.gutterForTest();
        gfx::Point unused{};
        check(!plain.breakpointMarginPoint(1, unused), "sans la colonne : pas d'endroit o\xC3\xB9 cliquer");

        MultiLineText code("code");
        code.setLanguage(Language::StructuredText);
        code.setText(st);
        code.setBreakpointGutter(true);
        code.setBounds({0.f, 0.f, 800.f, 300.f});
        code.layout();
        code.render(PaintContext{renderer, theme, {0, 0, 800, 300}, 0.0, nullptr});
        check(std::fabs(code.gutterForTest() - (plainGutter + 18.f)) < 0.01f, "la colonne ajoute 18 px \xC3\xA0 la marge");

        std::vector<std::size_t> toggled, enabled;
        auto c1 = code.breakpointToggled->connect([&](std::size_t l) { toggled.push_back(l); });
        auto c2 = code.breakpointEnableToggled->connect([&](std::size_t l) { enabled.push_back(l); });
        gfx::Point at{};
        check(code.breakpointMarginPoint(1, at), "la ligne 2 : un endroit o\xC3\xB9 cliquer");
        code.dispatch(MouseDown{at, MouseButton::Left, 1, {}});
        code.dispatch(MouseUp{at, MouseButton::Left, {}});
        check(toggled.size() == 1 && toggled[0] == 1, "un clic dans la colonne : la ligne 2 demand\xC3\xA9" "e");
        check(code.caretLineForTest() == 0, "le curseur ne bouge pas");
        KeyMods shift;
        shift.shift = true;
        code.dispatch(MouseDown{at, MouseButton::Left, 1, shift});
        check(enabled.size() == 1 && enabled[0] == 1 && toggled.size() == 1, "Maj+clic : activer / d\xC3\xA9sactiver");
        // Lot API 8 (2e partie) : le clic droit dans la colonne demande la condition de la ligne.
        std::vector<std::size_t> asked;
        auto c3 = code.breakpointConditionRequested->connect([&](std::size_t l) { asked.push_back(l); });
        code.dispatch(MouseDown{at, MouseButton::Right, 1, {}});
        code.dispatch(MouseUp{at, MouseButton::Right, {}});
        check(asked.size() == 1 && asked[0] == 1 && toggled.size() == 1 && enabled.size() == 1 && code.caretLineForTest() == 0,
              "clic droit dans la colonne : la condition de la ligne 2 demand\xC3\xA9" "e (ni pose, ni curseur)");

        // Le repli reste dans sa colonne (a droite des numeros).
        const float lh = renderer.lineHeight(gfx::FontId{16});
        const auto inner = code.contentRectForTest();
        const float foldX = inner.x + code.gutterForTest() - 9.f;
        code.dispatch(MouseDown{{foldX, inner.y + 0.5f * lh}, MouseButton::Left, 1, {}});
        check(code.isFolded(0) && toggled.size() == 1, "un clic sur le repli replie toujours (pas de point d'arr\xC3\xAAt)");
        code.dispatch(MouseDown{{foldX, inner.y + 0.5f * lh}, MouseButton::Left, 1, {}});
        check(!code.isFolded(0), "et d\xC3\xA9plie");

        // Le texte : le curseur sous la souris, la colonne comprise.
        const float advance = renderer.measure("M", gfx::FontId{16}).width;
        code.dispatch(MouseDown{{inner.x + code.gutterForTest() + 4.2f * advance, inner.y + 1.5f * lh}, MouseButton::Left, 1, {}});
        check(code.caretLineForTest() == 1 && code.caretColumnForTest() == 4, "le curseur tombe sous la souris, colonne comprise");
        code.dispatch(MouseUp{{inner.x + code.gutterForTest() + 4.2f * advance, inner.y + 1.5f * lh}, MouseButton::Left, {}});

        // F9 et Ctrl+F9 : la ligne du curseur (l'editeur a le focus).
        code.dispatch(KeyDown{Key::F9, {}, false});
        KeyMods ctrl;
        ctrl.ctrl = true;
        code.dispatch(KeyDown{Key::F9, ctrl, false});
        check(toggled.size() == 2 && toggled[1] == 1 && enabled.size() == 2 && enabled[1] == 1, "F9 : la ligne du curseur ; Ctrl+F9 : l'activer");
        plain.dispatch(MouseDown{{1.f, 1.f}, MouseButton::Left, 1, {}});
        check(plain.dispatch(KeyDown{Key::F9, {}, false}) == EventResult::Ignored, "sans la colonne, F9 passe (l'\xC3\xA9" "cran ouvre la simulation)");

        // Ce qu'on lui donne, il le garde (et l'infobulle de la colonne le dit).
        code.setBreakpoints({{1, true, false}, {3, false, true}});
        code.setExecutionLine(1);
        code.setLineNotes({{1, "b = 1", Tone::None}});
        check(code.breakpoints().size() == 2 && code.executionLine() == 1 && code.lineNotes().size() == 1, "les points, la ligne d'arr\xC3\xAAt, les valeurs");
        code.render(PaintContext{renderer, theme, {0, 0, 800, 300}, 0.0, nullptr});
        check(code.hasTooltip() && contains(code.liveTooltip(at), "clic pour l'enlever"), "l'infobulle de la colonne : " + code.liveTooltip(at));
        gfx::Point at4{};
        check(code.breakpointMarginPoint(3, at4) && contains(code.liveTooltip(at4), "r\xC3\xA9" "activer"), "un point d\xC3\xA9sactiv\xC3\xA9 : le r\xC3\xA9" "activer");
        code.dispatch(MouseDown{at, MouseButton::Left, 2, {}});
        check(toggled.size() == 3, "un double clic dans la colonne : une demande de plus, rien d'autre");
    }

    std::printf("9. Sur le projet d'essai\n");
    if (argc < 2) {
        std::printf("       (sans MAST.XPG : rien d'essay\xC3\xA9 ici)\n");
    } else {
        core::EventBus bus;
        importer::ProjectImporter importer(bus);
        auto imported = importer.importFile(argv[1]);
        check(static_cast<bool>(imported), "le projet d'essai se lit");
        if (imported) {
            const auto& p = *imported->project;
            std::size_t found = 0, blocks = 0, unique = 0;
            for (domain::Index i = 0; i < p.sections.size(); ++i) {
                const auto key = sd::sectionKey(p, i);
                std::size_t same = 0;
                for (domain::Index j = 0; j < p.sections.size(); ++j)
                    if (sd::sameSection(sd::sectionKey(p, j), key)) ++same;
                if (same != 1) continue;
                ++unique;
                if (key.find('.') != std::string::npos) ++blocks;
                if (sd::findSection(p, key) == i) ++found;
            }
            check(unique > 0 && found == unique, "chaque section retrouv\xC3\xA9" "e par son nom de simulateur (" + std::to_string(found) + " sur "
                                                    + std::to_string(unique) + ", dont " + std::to_string(blocks) + " dans des blocs)");
            check(blocks > 0, "les sections des blocs s'\xC3\xA9" "crivent BLOC.Section");
            check(sd::findSection(p, "pas_une_section_42") == domain::kNoIndex, "un nom inconnu : aucune");

            // Lot API 8 (2e partie) : la condition lue dans la simulation, maintenant.
            sim::Runtime rt(imported->project);
            rt.setContinueOnUnknownCalls(true);
            rt.setScanLimits(200000, 0);
            if (rt.prepare().has_value()) {
                std::string flag;
                rt.forEachSlot([&](const std::string& name, const sim::Value& v) {
                    if (flag.empty() && v.type() == sim::Type::Bool && name.find_first_of(".[%") == std::string::npos) flag = name;
                });
                check(!flag.empty(), "une variable BOOL simple pour essayer une condition : " + flag);
                if (!flag.empty()) {
                    sim::Value v;
                    (void)rt.get(flag, v);
                    const std::string lit = v.isTruthy() ? "TRUE" : "FALSE";
                    const auto yes = sd::conditionPreview(&rt, {""}, flag + " = " + lit);
                    check(yes == "Maintenant, c'est vrai (" + flag + " = " + lit + ").", "en direct : " + yes);
                    const auto no = sd::conditionPreview(&rt, {""}, "NOT (" + flag + " = " + lit + ")");
                    check(contains(no, "Maintenant, c'est faux"), "NOT (...) : faux, et pas un appel : " + no);
                }
                const auto unknown = sd::conditionPreview(&rt, {""}, "pas_une_variable_42 > 3");
                check(contains(unknown, "Maintenant, elle ne se lit pas"), "un nom inconnu : " + unknown);
            }
        }
    }

    std::printf("10. La condition par clic droit, l'espion tap\xC3\xA9, \xC2\xAB Mettre la condition \xC2\xBB (lot API 8, 2e partie)\n");
    {
        check(sd::stLiteral("12,5") == "12.5" && sd::stLiteral("1" + nnbsp + "243") == "1243" && sd::stLiteral("-3") == "-3",
              "une valeur montr\xC3\xA9" "e en litt\xC3\xA9ral ST : 12.5 ; 1243 ; -3");
        check(sd::stLiteral("TRUE") == "TRUE" && sd::stLiteral("false") == "FALSE" && sd::stLiteral("T#1s500ms") == "T#1s500ms"
                  && sd::stLiteral("'abc'") == "'abc'",
              "TRUE ; FALSE ; T#1s500ms ; 'abc'");
        check(sd::stLiteral("4 (forc\xC3\xA9" "e)") == "4" && sd::stLiteral("?").empty() && sd::stLiteral("(structure ou tableau)").empty()
                  && sd::stLiteral("abc").empty() && sd::stLiteral("").empty(),
              "forc\xC3\xA9" "e : la valeur seule ; ?, une structure, un mot : rien");
        const std::vector<std::pair<std::string, std::string>> values = {{"x", "3"}, {"s", "?"}, {"b", "TRUE"}, {"r", "4,5"}};
        const auto ideas = sd::conditionIdeas(values);
        check(joined(ideas) == "x = 3 | x > 3 | b = TRUE | b = FALSE | r = 4.5 | r > 4.5", "les id\xC3\xA9" "es : " + joined(ideas));
        check(sd::conditionIdeas(values, 2).size() == 2 && sd::conditionIdeas({}).empty(), "au plus 2 ; aucune valeur : aucune id\xC3\xA9" "e");
        check(sd::suggestedCondition(values) == "x = 3" && sd::suggestedCondition({{"s", "?"}, {"etat", "6"}}) == "etat = 6"
                  && sd::suggestedCondition({{"s", "?"}}).empty(),
              "Mettre la condition : la premi\xC3\xA8re valeur utilisable (etat = 6), sinon rien");
        check(sd::suggestedCondition({{"b", "TRUE"}, {"r", "4,5"}, {"etat", "6"}}) == "etat = 6"
                  && sd::suggestedCondition({{"b", "TRUE"}, {"r", "4,5"}}) == "b = TRUE",
              "un entier d'abord (un \xC3\xA9tat), sinon la premi\xC3\xA8re valeur");
        check(sd::conditionTitle(2, "SFC_PurgeA", 42) == "Condition du point d'arr\xC3\xAAt 2 : SFC_PurgeA, ligne 42", "le titre du dialogue");
        const std::vector<std::string> names = {"Armoires", "Bidule", "armoire_b", "Arm", "Unite.compteur", "Armoires"};
        check(joined(sd::namesStartingWith(names, "arm")) == "Arm | armoire_b | Armoires", "les noms qui commencent pareil (sans doublon) : "
                                                                                              + joined(sd::namesStartingWith(names, "arm")));
        check(sd::namesStartingWith(names, "").empty() && sd::namesStartingWith(names, "zz").empty() && sd::namesStartingWith(names, "a", 1).size() == 1
                  && joined(sd::namesStartingWith(names, "unite.")) == "Unite.compteur",
              "rien tap\xC3\xA9 : rien ; au plus 1 ; Unite.compteur");
        check(sd::unknownName(" Toto ") == "\xC2\xAB Toto \xC2\xBB n'existe pas dans le projet.", "un nom inconnu : " + sd::unknownName(" Toto "));
        bool call = true;
        const auto found = sd::conditionNames("Armoires[0].etat = 3 AND NOT b OR T#1s > t (* c *) AND s = 'x y' AND 16#FF > n", call);
        std::vector<std::string> list;
        for (const auto& f : found) list.push_back(f.name);
        check(joined(list) == "Armoires[0].etat | b | t | s | n" && !call, "les noms d'une condition : " + joined(list));
        check(found.size() == 5 && found[0].from == 0 && found[0].to == 16, "o\xC3\xB9 ils sont (pour les compl\xC3\xA9ter de l'unit\xC3\xA9)");
        (void)sd::conditionNames("ABS(x) > 3", call);
        check(call, "ABS(x) : un appel");
        check(sd::conditionPreview(nullptr, {""}, "  ") == "Sans condition : la simulation s'arr\xC3\xAAtera \xC3\xA0 chaque cycle.",
              "vide : sans condition");
        check(contains(sd::conditionPreview(nullptr, {""}, "x >"), "Elle ne se lit pas"), "illisible : " + sd::conditionPreview(nullptr, {""}, "x >"));
        check(contains(sd::conditionPreview(nullptr, {""}, "ABS(x) > 3"), "appelle une fonction"), "un appel : pas essay\xC3\xA9 ici");
        check(contains(sd::conditionPreview(nullptr, {""}, "x > 3"), "pas pr\xC3\xAAte"), "sans simulation : lue au premier passage");
    }

    // 1.11.2 (API-M, D25, decision 143) : « Qui a ecrit ? » dit, a cote d'une ligne
    // du code qui ecrit la variable, que sa section n'a pas tourne au dernier cycle.
    std::printf("11. Qui a \xC3\xA9" "crit ? : la ligne d'une section qui n'a pas tourn\xC3\xA9 (1.11.2, D25)\n");
    const std::string off = "section inactive (condition fausse : configuree)";
    {
        const std::vector<app::SimSectionTime> t = {
            {"Init", "Init", 10, 100, true, ""},
            {"Logigrammes_A", "Init", 5, 50, true, ""},
            {"Logigrammes_A", "SFC_ChangementA", 0, 0, false, "configuree"},
            {"Logigrammes_A", "Matrice", 40, 300, true, "configuree"},
            {"SFC_Seule", "SFC_Seule", 0, 0, false, "Mode = 3"}};
        check(sd::inactiveSectionText(t, "Logigrammes_A", "SFC_ChangementA") == off, "condition fausse : " + off);
        check(sd::inactiveSectionText(t, "logigrammes_a", " sfc_changementa ") == off, "l'unit\xC3\xA9 et la section sans casse");
        check(sd::inactiveSectionText(t, "Logigrammes_A", "Matrice").empty(), "condition vraie : elle a tourn\xC3\xA9, rien \xC3\xA0 dire");
        check(sd::inactiveSectionText(t, "Logigrammes_A", "Init").empty() && sd::inactiveSectionText(t, "", "Init").empty(),
              "Init, sans condition (celle de l'unit\xC3\xA9, celle de MAST) : rien");
        check(sd::inactiveSectionText(t, "", "SFC_Seule") == "section inactive (condition fausse : Mode = 3)"
                  && sd::inactiveSectionText(t, "Gestion_armoires", "SFC_Seule") == "section inactive (condition fausse : Mode = 3)",
              "une section de t\xC3\xA2" "che, ou d'une unit\xC3\xA9 sans rang (l'entr\xC3\xA9" "e porte le nom de la section) : sa condition");
        check(sd::inactiveSectionText(t, "Logigrammes_A", "Absente").empty() && sd::inactiveSectionText({}, "Logigrammes_A", "SFC_ChangementA").empty(),
              "pas dans la trace (une section de bloc), pas encore de cycle : rien");
        const std::vector<app::SimSectionTime> two = {{"Init", "Init", 1, 1, true, ""}, {"Init", "Init", 0, 0, false, "x"}};
        check(sd::inactiveSectionText(two, "U", "Init").empty(), "deux sections du m\xC3\xAAme nom qui ne disent pas la m\xC3\xAAme chose : rien");
    }
    if (argc >= 2) {
        // Sur MAST.XPG (Armoire_Gaz) : la ligne 1 de SFC_ChangementA (Logigrammes_A) ecrit
        // Gc_ChangementA.Runtime.GC_MAX_STEPS ; non configuree, sa section ne tourne pas.
        core::EventBus bus;
        importer::ProjectImporter importer(bus);
        auto imported = importer.importFile(argv[1]);
        check(static_cast<bool>(imported), "D25 : le projet d'essai se lit");
        if (imported) {
            const auto& p = *imported->project;
            const project::Reference* site = nullptr;
            const auto refs = project::crossReference(p, "Gc_ChangementA");
            for (const auto& r : refs.writes)
                if (!site && r.line == 1 && r.section < p.sections.size() && sd::sectionKey(p, r.section) == "SFC_ChangementA") site = &r;
            check(site != nullptr, "D25 : la lecture du code trouve SFC_ChangementA, ligne 1 (" + std::to_string(refs.writes.size()) + " \xC3\xA9" "critures)");
            std::string unit;
            if (site && p.sections[site->section].owner < p.pous.size()
                && p.pous[p.sections[site->section].owner].kind == domain::PouKind::ProgramUnit)
                unit = std::string(p.strings.text(p.pous[p.sections[site->section].owner].name));
            check(unit == "Logigrammes_A", "D25 : sa section est dans l'unit\xC3\xA9 Logigrammes_A (" + unit + ")");
            sim::Runtime rt(imported->project);
            rt.setScanLimits(2000000, 0);
            check(rt.prepare().has_value(), "D25 : simulateur pr\xC3\xAAt");   // comme grafcet_editor_test (simulation)
            const auto cycles = [&rt](int n) {
                bool halted = false;
                for (int i = 0; i < n && !halted; ++i) halted = rt.step(20).halted;
                return !halted;
            };
            const auto times = [&rt] {
                std::vector<app::SimSectionTime> out;
                for (auto& s : rt.sectionTimes())
                    out.push_back(app::SimSectionTime{std::move(s.entry), std::move(s.section), s.statements, s.micros, s.active, std::move(s.condition)});
                return out;
            };
            check(cycles(20), "D25 : non configur\xC3\xA9" "e, 20 cycles sans halte");
            const auto before = times();
            check(sd::inactiveSectionText(before, unit, "SFC_ChangementA") == off,
                  "D25 : non configur\xC3\xA9" "e, la ligne de SFC_ChangementA le dit : " + sd::inactiveSectionText(before, unit, "SFC_ChangementA"));
            check(sd::inactiveSectionText(before, unit, "Init").empty(), "D25 : Init de l'unit\xC3\xA9 (sans condition) a tourn\xC3\xA9 : rien");
            // Configuree (la page 151 validee : ici forcee, comme grafcet_editor_test) : Init de
            // l'unite recopie configuree, la section tourne, la ligne ne dit plus rien.
            check(rt.force("ConfigArmoireUtilisee.configuree", sim::Value::boolean(true)), "D25 : forcer ConfigArmoireUtilisee.configuree");
            check(cycles(3), "D25 : configur\xC3\xA9" "e, 3 cycles sans halte");
            check(sd::inactiveSectionText(times(), unit, "SFC_ChangementA").empty(), "D25 : configur\xC3\xA9" "e, SFC_ChangementA a tourn\xC3\xA9 : rien");
        }
    }

    std::printf(failures ? "ECHEC : %d \xC3\xA9" "chec(s)\n" : "simdebugui_test : tout est bon\n", failures);
    return failures ? 1 : 0;
}
