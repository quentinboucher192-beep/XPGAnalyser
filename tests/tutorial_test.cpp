// tutorial_test : le moteur des tutoriels (1.11, D2 et D3), sans ecran.
//
//   tutorial_test [<dossier des .tuto>]
//
// Le format (lecteur, problemes avec leur ligne, variantes, frise), la
// verification d'un "A toi", et l'automate du lecteur sur une scene factice :
// surtout, REVENIR EN ARRIERE REMET L'ETAT EXACT (seek == avoir joue). Avec un
// dossier, chaque .tuto se lit sans probleme, se compile pour chaque variante
// et se joue d'un bout a l'autre.

#include "help/Tutorial.hpp"
#include "help/TutorialKeys.hpp"
#include "help/TutorialLaunch.hpp"
#include "help/TutorialPlayer.hpp"
#include "help/TutorialScreen.hpp"
#include "help/SandboxTrash.hpp"
#include "help/TutorialSpot.hpp"
#include "help/TutorialVerify.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_ok = 0;
int g_ko = 0;

void check(bool cond, const char* what, int line) {
    if (cond) { ++g_ok; return; }
    ++g_ko;
    std::printf("ECHEC ligne %d : %s\n", line, what);
}
#define CHECK(c) check((c), #c, __LINE__)

// Une scene factice : un "projet" de cles et de valeurs que chaque geste change.
// Rejouer [0, f] d'un trait doit donner le meme etat qu'en plusieurs morceaux.
struct FakeStage : help::TutorialStage {
    std::map<std::string, std::string> state;
    std::map<std::string, std::string> answers;  // ce que read() rend
    int resets = 0, animated = 0, instant = 0, shown = 0;
    bool interactive = false;

    void reset(const help::CompiledTutorial&) override { state.clear(); ++resets; }
    void advance(const help::Gesture& g, double from, double to, bool anim) override {
        (anim ? animated : instant)++;
        using K = help::GestureKind;
        const bool ends = to >= 1.0 && from < 1.0;
        switch (g.kind) {
        case K::Type: {
            const auto n = static_cast<std::size_t>(std::floor(to * static_cast<double>(g.text.size()) + 1e-9));
            const std::string where = g.target.empty() ? state["focus"] : g.target;
            state["texte:" + where] = g.text.substr(0, n);
            break;
        }
        case K::Click: if (ends) state["focus"] = g.target; break;
        case K::Drag: if (ends) state["poses"] += g.target + ">" + g.target2 + ";"; break;
        case K::Key: if (ends) state["touches"] += g.text + ";"; break;
        case K::Say: state["bulle"] = g.text; break;
        case K::Spot: state["encadre"] = g.target; break;
        case K::Move: if (ends) state["curseur"] = g.target; break;
        case K::Run: if (ends) state["simulation"] = "marche"; break;
        case K::Wait: break;
        case K::Prepare: state["prep"] += (g.args.empty() ? std::string() : g.args[0] + (g.args.size() > 1 ? " " + g.args[1] : std::string())) + ";"; break;
        }
    }
    void showStep(const help::CompiledTutorial& c, std::size_t i) override { ++shown; state["etape"] = c.steps[i].title; }
    void setInteractive(bool on) override { interactive = on; }
    std::optional<std::string> read(std::string_view p) override {
        const auto it = answers.find(std::string(p));
        if (it == answers.end()) return std::nullopt;
        return it->second;
    }
    [[nodiscard]] std::string signature() const {
        std::string s;
        for (const auto& [k, v] : state) s += k + "=" + v + "\n";
        return s;
    }
};

bool hasProblemAt(const std::vector<help::TutorialProblem>& ps, int line) {
    return std::any_of(ps.begin(), ps.end(), [&](const auto& p) { return p.line == line; });
}

// Joue jusqu'a la fin : Espace a chaque fin d'etape. Rend le nombre de ticks.
int playToEnd(help::TutorialPlayer& p, double dt = 33.0) {
    int ticks = 0;
    while (p.state() != help::PlayerState::Finished && ticks < 200000) {
        if (p.state() == help::PlayerState::StepEnd) p.togglePlay();
        p.tick(dt);
        ++ticks;
    }
    return ticks;
}

const char* kSmall =
    "# un petit tutoriel\n"
    "= petit | Un petit tutoriel\n"
    "@sujet sujet-a\n"
    "@sujet sujet-b\n"
    "@bac demo-ihm | Vue_1\n"
    "@variantes a, b\n"
    "@variante b\n"
    "\n"
    "== 1 | Premier pas\n"
    ": La bulle\n"
    ": continue.\n"
    ":: Un autre paragraphe.\n"
    "clic biblio:Bouton\n"
    "texte propriete:Nom \"B_{variante}\"\n"
    "[a] touche Return\n"
    "@atoi Fais-le.\n"
    "@verifier objet[Bouton].nom = B_{variante}\n"
    "@presque objet[Bouton].nom = B | Il manque la variante : {valeur}.\n"
    "\n"
    "== 2 | Seulement pour a [a]\n"
    ": Ici.\n"
    "attendre 1s\n"
    "\n"
    "== 3 | La fin\n"
    ": La fin.\n"
    "[!a] survol vue:10,20\n"
    "glisser biblio:Bouton vue:100,100\n"
    "simuler 30\n";

void testFormatTools() {
    const auto w = help::tutorialWords(R"(texte "propriete:Couleur allum" "a \"b\" c\\d" fin)");
    CHECK(w.size() == 4);
    CHECK(w.size() == 4 && w[1] == "propriete:Couleur allum");
    CHECK(w.size() == 4 && w[2] == "a \"b\" c\\d");
    CHECK(help::tutorialWords(R"(x "")").size() == 2);
    CHECK(help::parseDurationMs("1.5s") == 1500);
    CHECK(help::parseDurationMs("800ms") == 800);
    CHECK(help::parseDurationMs("30") == 1000);
    CHECK(help::parseDurationMs("2,5s") == 2500);
    CHECK(help::parseDurationMs("abc") == -1);
    CHECK(help::readingTimeMs("court") == 1500);
    CHECK(help::readingTimeMs(std::string(100, 'x')) == 4500);
    CHECK(help::readingTimeMs(std::string(1000, 'x')) == 6000);
    CHECK(help::utf8Letters("\xC3\xA9t\xC3\xA9") == 3);
    CHECK(help::formatClock(64000) == "1:04");
    CHECK(help::formatClock(0) == "0:00");
    CHECK(help::isKnownTarget("propriete:Nom"));
    CHECK(help::isKnownTarget("variantes"));
    CHECK(!help::isKnownTarget("bidule:Truc"));
    CHECK(!help::isKnownTarget("propriete:"));
}

void testParseAndCompile() {
    std::vector<help::TutorialProblem> problems;
    const auto t = help::Tutorial::parse(kSmall, &problems);
    CHECK(problems.empty());
    for (const auto& p : problems) std::printf("  ligne %d : %s\n", p.line, p.message.c_str());
    CHECK(t.id == "petit");
    CHECK(t.title == "Un petit tutoriel");
    CHECK(t.topics.size() == 2);
    CHECK(t.sandbox == "demo-ihm" && t.view == "Vue_1");
    CHECK(t.variants.size() == 2 && t.defaultVariant == "b");

    const auto b = t.compile();  // la variante par defaut
    CHECK(b.variant == "b");
    CHECK(b.problems.empty());
    CHECK(b.steps.size() == 2);
    if (b.steps.size() == 2) {
        const auto& s1 = b.steps[0];
        CHECK(s1.bubble == "La bulle continue.\nUn autre paragraphe.");
        CHECK(s1.gestures.size() == 2);
        CHECK(s1.gestures.size() == 2 && s1.gestures[1].text == "B_b");
        // Les durees de la maquette : clic = move 650 + 120 + click 380 ; texte = 260 + 85 par lettre ;
        // 120 ms apres chaque geste ; 1,8 s de lecture en fin d'etape.
        CHECK(s1.gestures.size() == 2 && s1.gestures[0].durationMs == 1150);
        CHECK(s1.gestures.size() == 2 && s1.gestures[1].durationMs == 260 + 85 * 3);
        CHECK(s1.gestures.size() == 2 && s1.gestures[1].startMs == 1150 + 120);
        CHECK(s1.durationMs == std::max(1150 + 120 + 515 + 120 + 1800, help::readingTimeMs(s1.bubble)));
        CHECK(s1.aTry.has_value());
        if (s1.aTry) {
            CHECK(s1.aTry->instruction == "Fais-le.");
            CHECK(s1.aTry->target == "propriete:Nom");  // la cible du dernier geste
            CHECK(s1.aTry->bravo == "Bravo !");
            CHECK(s1.aTry->checks.size() == 1 && s1.aTry->checks[0].value == "B_b");
            CHECK(s1.aTry->almost.size() == 1 && s1.aTry->almost[0].reason == "Il manque la variante : {valeur}.");
        }
        const auto& s3 = b.steps[1];
        CHECK(s3.number == 2 && s3.writtenNumber == 3);  // renumerotee
        CHECK(s3.gestures.size() == 3);
        CHECK(s3.startMs == s1.durationMs);
        CHECK(s3.durationMs == 650 + 120 + 1400 + 120 + 1000 + 120 + 1800);
        CHECK(b.totalMs == s1.durationMs + s3.durationMs);
        CHECK(b.stepAt(0) == 0 && b.stepAt(s3.startMs) == 1 && b.stepAt(1e9) == 1);
    }
    const auto a = t.compile("a");
    CHECK(a.steps.size() == 3);
    CHECK(a.steps.size() == 3 && a.steps[0].gestures.size() == 3);
    CHECK(a.steps.size() == 3 && a.steps[2].gestures.size() == 2);
    const auto z = t.compile("zz");
    CHECK(!z.problems.empty() && z.variant == "b");
}

void testProblems() {
    const char* bad =
        "@verifier x = 1\n"
        "== 1 | \n"
        "clic propriete:Couleur allum\n"
        "clic bidule:Truc\n"
        "sauter haut\n"
        "[zz] dire \"x\"\n"
        "attendre jamais\n"
        "@presque a = b\n";
    std::vector<help::TutorialProblem> problems;
    const auto t = help::Tutorial::parse(bad, &problems);
    for (int line = 1; line <= 8; ++line) {
        const bool found = hasProblemAt(problems, line);
        if (!found) std::printf("  pas de probleme ligne %d\n", line);
        CHECK(found);
    }
    // Une etape sans @atoi mais avec un @verifier, un A toi sans @verifier.
    const char* bad2 =
        "= x | X\n"
        "== 1 | Un\n"
        ": Bulle.\n"
        "clic biblio:Vanne\n"
        "@verifier a = b\n"
        "== 2 | Deux\n"
        "attendre 1s\n"
        "@atoi Fais-le.\n";
    std::vector<help::TutorialProblem> p2;
    const auto t2 = help::Tutorial::parse(bad2, &p2);
    CHECK(p2.empty());
    const auto c2 = t2.compile();
    CHECK(hasProblemAt(c2.problems, 2));  // @verifier sans @atoi
    CHECK(hasProblemAt(c2.problems, 6));  // sans bulle, A toi sans @verifier
    (void)t;
}

void testChecks() {
    help::TutorialCheck c;
    c.path = "p";
    c.op = "=";
    c.value = "V_201";
    CHECK(help::evaluateCheck(c, "V_201"));
    CHECK(!help::evaluateCheck(c, "V201"));
    c.op = ">=";
    c.value = "1";
    CHECK(help::evaluateCheck(c, "2") && help::evaluateCheck(c, "1") && !help::evaluateCheck(c, "0"));
    c.op = "<";
    c.value = "10";
    CHECK(help::evaluateCheck(c, "9"));  // en nombres, pas en lettres
    CHECK(!help::evaluateCheck(c, "10.5"));
    c.op = "~";
    c.value = "Pos";
    CHECK(help::evaluateCheck(c, "=V[0].Pos"));
    c.op = "<>";
    c.value = "";
    CHECK(help::evaluateCheck(c, "manuelle") && !help::evaluateCheck(c, ""));

    help::TutorialATry a;
    a.checks.push_back({"nom", "=", "V_201", "", 0});
    a.almost.push_back({"nom", "=", "V201", "Tu as \xC3\xA9" "crit {valeur}.", 0});
    a.bravo = "Bien : {valeur}.";
    std::map<std::string, std::string> values;
    const help::PathReader read = [&](std::string_view p) -> std::optional<std::string> {
        const auto it = values.find(std::string(p));
        if (it == values.end()) return std::nullopt;
        return it->second;
    };
    CHECK(help::evaluateATry(a, read).result == help::CheckOutcome::Result::NotYet);
    values["nom"] = "V201";
    const auto almost = help::evaluateATry(a, read);
    CHECK(almost.result == help::CheckOutcome::Result::Almost);
    CHECK(almost.message == "Tu as \xC3\xA9" "crit V201.");
    values["nom"] = "V_201";
    const auto ok = help::evaluateATry(a, read);
    CHECK(ok.result == help::CheckOutcome::Result::Ok && ok.message == "Bien : V_201.");
}

void testPlayer() {
    const auto t = help::Tutorial::parse(kSmall);
    FakeStage stage;
    help::TutorialPlayer p(t, stage, "a");
    CHECK(p.compiled().steps.size() == 3);
    CHECK(p.progressLabel() == "1 / 3");

    // Lecture : comme la maquette, les etapes s'enchainent (meme celle qui a un A toi).
    p.start(0);
    CHECK(p.state() == help::PlayerState::Playing && stage.resets == 1);
    for (int i = 0; i < 2000 && p.state() == help::PlayerState::Playing; ++i) p.tick(33);
    CHECK(p.state() == help::PlayerState::Finished && p.step() == 2);
    CHECK(stage.state["texte:propriete:Nom"] == "B_a");
    CHECK(stage.state["touches"] == "Return;");
    CHECK(stage.state["poses"] == "biblio:Bouton>vue:100,100;");
    CHECK(stage.animated > 0 && stage.instant == 0);
    CHECK(p.progressLabel() == "3 / 3");
    // Pause apres chaque etape : on s'arrete a la fin de chacune ; Espace continue.
    p.setPauseAfterEachStep(true);
    p.restart();
    for (int i = 0; i < 1000 && p.state() == help::PlayerState::Playing; ++i) p.tick(33);
    CHECK(p.state() == help::PlayerState::StepEnd && p.step() == 0);
    p.togglePlay();
    CHECK(p.state() == help::PlayerState::Playing && p.step() == 1);
    for (int i = 0; i < 1000 && p.state() == help::PlayerState::Playing; ++i) p.tick(33);
    CHECK(p.state() == help::PlayerState::StepEnd && p.step() == 1);
    p.togglePlay();
    for (int i = 0; i < 1000 && p.state() == help::PlayerState::Playing; ++i) p.tick(33);
    CHECK(p.state() == help::PlayerState::Finished && p.step() == 2);  // la derniere : la fin
    p.setPauseAfterEachStep(false);
    // Suivante relance la lecture, meme en pause (comme la maquette).
    p.seek(0, 0);
    p.pause();
    p.next();
    CHECK(p.state() == help::PlayerState::Playing && p.step() == 1);

    // L'ETAT EXACT : a chaque instant, seek() donne le meme etat qu'avoir joue.
    {
        FakeStage played, sought;
        help::TutorialPlayer a(t, played, "a"), b(t, sought, "a");
        a.setSpeed(0.5);
        a.start(0);
        int ticks = 0, compared = 0, same = 0;
        while (a.state() != help::PlayerState::Finished && ticks < 5000) {
            if (a.state() == help::PlayerState::StepEnd) a.togglePlay();
            a.tick(33);
            if (++ticks % 7 == 0) {
                b.seek(a.step(), a.stepTimeMs());
                ++compared;
                if (played.signature() == sought.signature()) ++same;
                else std::printf("  ecart a l'etape %zu, %.1f ms :\n%s--\n%s", a.step(), a.stepTimeMs(),
                                 played.signature().c_str(), sought.signature().c_str());
            }
        }
        CHECK(compared > 20 && same == compared);
        CHECK(sought.animated == 0);  // seek ne fait que rejouer sans animation
    }

    // Precedent : recommence l'etape apres 1,5 s, sinon celle d'avant.
    p.seek(0, 1700);
    p.previous();
    CHECK(p.step() == 0 && p.stepTimeMs() == 0);
    p.seek(2, 100);
    p.previous();
    CHECK(p.step() == 1 && p.stepTimeMs() == 0);
    p.next();
    CHECK(p.step() == 2);
    p.next();
    CHECK(p.state() == help::PlayerState::Finished);
    p.togglePlay();  // Espace a la fin : recommence
    CHECK(p.state() == help::PlayerState::Playing && p.step() == 0 && p.timeMs() == 0);
    p.seekTime(p.compiled().steps[2].startMs + 10);
    CHECK(p.step() == 2 && std::abs(p.stepTimeMs() - 10) < 1e-9);

    // La vitesse.
    p.seek(2, 0);
    p.play();
    p.setSpeed(2);
    p.tick(100);
    CHECK(std::abs(p.stepTimeMs() - 200) < 1e-9);
    p.setSpeed(10);
    CHECK(p.speed() == 4.0);
    p.setSpeed(1);

    // Pause apres chaque etape.
    p.setPauseAfterEachStep(true);
    p.seek(1, 0);
    p.play();
    for (int i = 0; i < 1000 && p.state() == help::PlayerState::Playing; ++i) p.tick(33);
    CHECK(p.state() == help::PlayerState::StepEnd && p.step() == 1);
    p.setPauseAfterEachStep(false);

    // A toi.
    CHECK(!p.startATry());  // l'etape 2 n'en a pas
    p.seek(0, 500);
    const int before = stage.resets;
    CHECK(p.startATry());
    CHECK(p.state() == help::PlayerState::ATry && stage.interactive && stage.resets == before + 1);
    CHECK(p.stepTimeMs() == 0);
    CHECK(p.verify().result == help::CheckOutcome::Result::NotYet);
    stage.answers["objet[Bouton].nom"] = "B";
    const auto almost = p.verify();
    CHECK(almost.result == help::CheckOutcome::Result::Almost);
    CHECK(almost.message == "Il manque la variante : B.");
    CHECK(p.state() == help::PlayerState::ATry);
    stage.answers["objet[Bouton].nom"] = "B_a";
    const auto ok = p.verify();
    CHECK(ok.result == help::CheckOutcome::Result::Ok && ok.message == "Bravo !");
    CHECK(p.state() == help::PlayerState::StepEnd && !stage.interactive);
    // Montre-moi : sort d'A toi et rejoue l'etape.
    CHECK(p.startATry());
    p.showMe();
    CHECK(p.state() == help::PlayerState::Playing && !stage.interactive && p.stepTimeMs() == 0);
    // Echap.
    CHECK(p.startATry());
    p.leaveATry();
    CHECK(p.state() == help::PlayerState::Paused && !stage.interactive);
    // Recommencer l'etape, toujours en A toi.
    CHECK(p.startATry());
    p.retryATry();
    CHECK(p.state() == help::PlayerState::ATry && stage.interactive);
    p.leaveATry();

    // Une autre variante : on recommence.
    p.setVariant("b");
    CHECK(p.compiled().variant == "b" && p.compiled().steps.size() == 2 && p.step() == 0);

    // Tranche 11 (trouve par I111, session 89) : avec des images de 1000/30 ms, il restait a
    // 100 ms un reste d'arrondi (7e-15 ms) qui ne faisait plus avancer stepTime_ ; tick ne
    // rendait plus la main. Sans le garde-fou de tick, cet essai ne finit pas.
    p.setPauseAfterEachStep(false);
    p.restart();
    for (int i = 0; i < 4000 && p.state() == help::PlayerState::Playing; ++i) p.tick(1000.0 / 30.0);
    CHECK(p.state() == help::PlayerState::Finished && p.step() == 1);
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Chaque .tuto du dossier : se lit, se compile pour chaque variante, se joue.
void testWrittenTutorials(const std::filesystem::path& dir) {
    int files = 0;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".tuto") continue;
        ++files;
        const auto text = readFile(e.path());
        std::vector<help::TutorialProblem> problems;
        const auto t = help::Tutorial::parse(text, &problems);
        for (const auto& p : problems)
            std::printf("  %s:%d : %s\n", e.path().filename().string().c_str(), p.line, p.message.c_str());
        CHECK(problems.empty());
        auto variants = t.variants;
        if (variants.empty()) variants.emplace_back();
        for (const auto& v : variants) {
            const auto c = t.compile(v);
            for (const auto& p : c.problems)
                std::printf("  %s [%s] :%d : %s\n", t.id.c_str(), v.c_str(), p.line, p.message.c_str());
            CHECK(c.problems.empty());
            int at = 0;
            bool frise = true, complete = true;
            for (const auto& s : c.steps) {
                frise = frise && s.startMs == at;
                at += s.durationMs;
                complete = complete && !s.title.empty() && !s.bubble.empty() && !s.gestures.empty();
                if (s.aTry) complete = complete && !s.aTry->checks.empty() && !s.aTry->instruction.empty();
            }
            CHECK(frise && at == c.totalMs);
            CHECK(complete);
            // Tranche 13 : chaque `touche` a un nom francais pour le calque.
            bool keysKnown = true;
            for (const auto& s : c.steps)
                for (const auto& g : s.gestures) {
                    if (g.kind != help::GestureKind::Key) continue;
                    bool known = false;
                    (void)help::keyLabel(g.text, &known);
                    if (!known) std::printf("  %s [%s] : touche %s : nom inconnu\n", t.id.c_str(), v.c_str(), g.text.c_str());
                    keysKnown = keysKnown && known;
                }
            CHECK(keysKnown);
            FakeStage stage;
            help::TutorialPlayer p(t, stage, v);
            p.start(0);
            playToEnd(p);
            CHECK(p.state() == help::PlayerState::Finished);
            CHECK(p.step() + 1 == c.steps.size());
            std::printf("  %s [%s] : %zu \xC3\xA9tapes, %s\n", t.id.c_str(), v.c_str(), c.steps.size(),
                        help::formatClock(c.totalMs).c_str());
        }

        if (t.id == "objet-vanne") {
            const std::string reglante = "r\xC3\xA9glante";
            CHECK(t.defaultVariant == reglante && t.variants.size() == 4);
            const auto r = t.compile();
            const auto m = t.compile("manuelle");
            CHECK(r.steps.size() == 7 && m.steps.size() == 7);
            auto hasTarget = [](const help::TutorialStep& s, const std::string& target) {
                return std::any_of(s.gestures.begin(), s.gestures.end(),
                                   [&](const help::Gesture& g) { return g.target == target; });
            };
            if (r.steps.size() == 7 && m.steps.size() == 7) {
                CHECK(hasTarget(r.steps[3], "propriete:Ouverture (%)") && !hasTarget(r.steps[3], "propriete:Valeur"));
                CHECK(hasTarget(m.steps[3], "propriete:Valeur") && !hasTarget(m.steps[3], "propriete:Ouverture (%)"));
                CHECK(hasTarget(r.steps[5], "propriete:En mouvement (expression)") && !hasTarget(r.steps[5], "alarme:Defaut"));
                CHECK(hasTarget(m.steps[5], "alarme:Defaut"));
                CHECK(hasTarget(r.steps[0], "variante:" + reglante));
                CHECK(hasTarget(m.steps[0], "variante:manuelle"));
                CHECK(r.steps[0].aTry && r.steps[0].aTry->onOkVariantFrom == "biblio.variante");
            }
            // A toi de l'etape 1 : le choix de l'utilisateur change la variante jouee.
            FakeStage stage;
            help::TutorialPlayer p(t, stage);
            p.start(0, true);
            CHECK(p.startATry());
            stage.answers["biblio.variante"] = "manuelle";
            const auto ok = p.verify();
            CHECK(ok.result == help::CheckOutcome::Result::Ok);
            CHECK(ok.message == "Bien : la vanne manuelle.");
            CHECK(p.compiled().variant == "manuelle" && p.step() == 0);
            p.next();
            CHECK(p.step() == 1 && p.state() == help::PlayerState::Playing);
        }
    }
    CHECK(!ec && files > 0);
}

// Tranche 2 : la section @avant (le bac a sable prepare) et le point d'entree de T2.
void testBeforeAndLaunch() {
    const char* text =
        "= avec-avant | Avec @avant\n"
        "@sujet sujet-avant\n"
        "@variantes a, b\n"
        "@avant\n"
        "vue Vue_Tuto\n"
        "poser Tuyauterie 120,262 Tuyauterie_1\n"
        "[b] poser Cuve 520,200 T_{variante}\n"
        "regler Tuyauterie_1 Longueur 400\n"
        "clic vue:10,10\n"
        "== 1 | Un\n"
        ": Un.\n"
        "clic biblio:Vanne\n"
        "== 2 | Deux\n"
        ": Deux.\n"
        "attendre 1s\n";
    std::vector<help::TutorialProblem> ps;
    const auto t = help::Tutorial::parse(text, &ps);
    CHECK(ps.empty());
    CHECK(t.before.size() == 5);
    const auto a = t.compile("a");
    const auto b = t.compile("b");
    CHECK(a.before.size() == 4 && b.before.size() == 5);
    CHECK(b.before.size() == 5 && b.before[2].kind == help::GestureKind::Prepare && b.before[2].args.size() == 4
          && b.before[2].args[3] == "T_b");
    CHECK(a.totalMs == b.totalMs && a.steps.size() == 2);   // hors de la frise
    // Chaque remise a neuf rejoue @avant, avant l'etape 1, sans animation.
    FakeStage stage;
    help::TutorialPlayer p(t, stage, "b");
    p.start(0, true);
    CHECK(stage.state["prep"] == "vue Vue_Tuto;poser Tuyauterie;poser Cuve;regler Tuyauterie_1;");
    CHECK(stage.state["focus"] == "vue:10,10");
    p.seek(1, 0);
    CHECK(stage.state["prep"] == "vue Vue_Tuto;poser Tuyauterie;poser Cuve;regler Tuyauterie_1;");
    CHECK(stage.state["focus"] == "biblio:Vanne");

    // Les problemes de @avant, avec leur ligne.
    ps.clear();
    (void)help::Tutorial::parse("= x | X\n@avant\n: bulle\n@atoi non\nposer Vanne\nvue\n== 1 | Un\n: u\nvue V\n@avant\nclic biblio:A\n", &ps);
    CHECK(hasProblemAt(ps, 3));   // pas de bulle dans @avant
    CHECK(hasProblemAt(ps, 4));   // @atoi : pas dans @avant
    CHECK(hasProblemAt(ps, 5));   // poser <genre> <x,y> [<nom>]
    CHECK(hasProblemAt(ps, 6));   // vue <nom>
    CHECK(hasProblemAt(ps, 9));   // vue : seulement dans @avant
    CHECK(hasProblemAt(ps, 10));  // @avant apres la premiere etape

    // Tranche 5 : type et variable (le bac a sable declare V : ARRAY[0..3] OF T_VANNE).
    ps.clear();
    const auto tv = help::Tutorial::parse(
        "= tv | TV\n@avant\ntype T_VANNE \"Pos : REAL; Defaut : BOOL\"\nvariable V \"ARRAY[0..3] OF T_VANNE\"\n"
        "== 1 | Un\n: u\nattendre 1s\n", &ps);
    CHECK(ps.empty());
    const auto tvc = tv.compile("");
    CHECK(tvc.before.size() == 2 && tvc.before[0].args.size() == 3 && tvc.before[0].args[2] == "Pos : REAL; Defaut : BOOL"
          && tvc.before[1].args.size() == 3 && tvc.before[1].args[2] == "ARRAY[0..3] OF T_VANNE");
    ps.clear();
    (void)help::Tutorial::parse("= x | X\n@avant\nvariable V\ntype T \"sans membre\"\n== 1 | Un\n: u\nvariable W INT\n", &ps);
    CHECK(hasProblemAt(ps, 3));   // variable <nom> "<type>"
    CHECK(hasProblemAt(ps, 4));   // type <nom> "<membre> : <TYPE>; ..."
    CHECK(hasProblemAt(ps, 7));   // variable : seulement dans @avant

    // Tranche 14 (decision du chef) : action <identifiant>, une action de l'appli (la page des
    // expressions des expr-* de T3). Hors de la frise, rejouee a chaque remise a neuf, une fois.
    ps.clear();
    const auto ta = help::Tutorial::parse(
        "= ta | TA\n@variantes x, y\n@avant\nvue Vue_Tuto\n[y] action help.expressions\n"
        "== 1 | Un\n: u\nclic \"bouton:#   Couleur\"\n== 2 | Deux\n: d\nattendre 1s\n", &ps);
    CHECK(ps.empty());
    const auto tax = ta.compile("x"), tay = ta.compile("y");
    CHECK(tax.before.size() == 1 && tay.before.size() == 2);
    CHECK(tay.before.size() == 2 && tay.before[1].kind == help::GestureKind::Prepare && tay.before[1].args.size() == 2
          && tay.before[1].args[0] == "action" && tay.before[1].args[1] == "help.expressions");
    CHECK(tax.totalMs == tay.totalMs && tay.steps.size() == 2);   // hors de la frise
    {
        FakeStage st;
        help::TutorialPlayer pa(ta, st, "y");
        pa.start(0, true);
        CHECK(st.state["prep"] == "vue Vue_Tuto;action help.expressions;");
        pa.seek(1, 0);   // une remise a neuf : l'action est rejouee, une seule fois
        CHECK(st.state["prep"] == "vue Vue_Tuto;action help.expressions;");
        CHECK(st.state["focus"] == "bouton:#   Couleur");
        CHECK(st.resets == 2);
    }
    ps.clear();
    (void)help::Tutorial::parse(
        "= x | X\n@avant\naction\naction help.expressions en.trop\naction \"help expressions\"\n"
        "== 1 | Un\n: u\naction help.expressions\n", &ps);
    CHECK(hasProblemAt(ps, 3));   // action <identifiant>
    CHECK(hasProblemAt(ps, 4));   // un seul identifiant
    CHECK(hasProblemAt(ps, 5));   // sans espace
    CHECK(hasProblemAt(ps, 8));   // action : seulement dans @avant
    // Tranche 19 : aide <cle> (le centre d'aide ouvert sur ce sujet), les memes regles qu'action.
    ps.clear();
    (void)help::Tutorial::parse(
        "= x | X\n@avant\naide page-signaler\naide\naide page-signaler en.trop\n"
        "== 1 | Un\n: u\naide page-signaler\n", &ps);
    CHECK(!hasProblemAt(ps, 3));  // aide page-signaler : lu
    CHECK(hasProblemAt(ps, 4));   // aide <cle>
    CHECK(hasProblemAt(ps, 5));   // une seule cle
    CHECK(!hasProblemAt(ps, 8));  // 1.11.2 (R1112-3) : aide <cle> aussi dans une etape
    CHECK(ps.size() == 2);        // les lignes 4 et 5 seulement

    // Le point d'entree de T2 : help::startTutorial(cle, variante).
    help::clearTutorials();
    CHECK(help::startTutorial("avec-avant") == help::StartResult::Unknown);
    CHECK(help::registerTutorialText(text) == "avec-avant");
    CHECK(help::registerTutorialText("pas un tutoriel").empty());
    CHECK(help::tutorials().size() == 1);
    CHECK(help::findTutorial("sujet-avant") == help::findTutorial("avec-avant"));
    CHECK(help::startTutorial("avec-avant") == help::StartResult::NoLauncher);
    help::TutorialStart got;
    std::string gotId;
    help::setTutorialLauncher([&](const help::Tutorial& tt, const help::TutorialStart& s) {
        gotId = tt.id;
        got = s;
        return true;
    });
    help::TutorialStart opts;
    opts.step = 9;
    opts.paused = true;
    CHECK(help::startTutorial("sujet-avant", "b", opts) == help::StartResult::Started);
    CHECK(gotId == "avec-avant" && got.variant == "b" && got.step == 1 && got.paused);
    CHECK(help::startTutorial("avec-avant") == help::StartResult::Started && got.variant == "a");
    CHECK(help::startTutorial("avec-avant", "z") == help::StartResult::Broken);

    // 1.11.2 (R1112-4) : « Essayer » sur une etape sans A toi (vues : l'etape 1 n'en a pas) rejouait la
    // demonstration sans un mot. Il ouvre l'A toi le plus proche (apres, sinon avant) et le dit ;
    // sans aucun A toi, il montre les gestes et le dit.
    CHECK(help::registerTutorialText(
              "= essai-loin | E\n@bac Armoire_Gaz\n"
              "== 1 | Un\n: u\nattendre 1s\n"
              "== 2 | Deux\n: d\nattendre 1s\n"
              "== 3 | Trois\n: t\nattendre 1s\n@atoi Fais-le.\n@verifier objet(A).nom = A\n"
              "== 4 | Quatre\n: q\nattendre 1s\n") == "essai-loin");
    CHECK(help::registerTutorialText(
              "= essai-sans | S\n@bac Armoire_Gaz\n== 1 | Un\n: u\nattendre 1s\n== 2 | Deux\n: d\nattendre 1s\n")
          == "essai-sans");
    help::TutorialStart tryOpts;
    tryOpts.aTry = true;
    CHECK(help::startTutorial("essai-loin", {}, tryOpts) == help::StartResult::Started);
    CHECK(got.aTry && got.step == 2);   // l'etape 3, la premiere ou c'est a toi
    CHECK(got.notice.find("\xC3\xA9tape 3") != std::string::npos && got.notice.find("premi\xC3\xA8re") != std::string::npos);
    tryOpts.step = 3;                    // apres le seul A toi : le plus proche est avant
    CHECK(help::startTutorial("essai-loin", {}, tryOpts) == help::StartResult::Started);
    CHECK(got.aTry && got.step == 2 && got.notice.find("plus proche") != std::string::npos);
    tryOpts.step = 2;                    // l'etape a son A toi : rien a dire
    CHECK(help::startTutorial("essai-loin", {}, tryOpts) == help::StartResult::Started);
    CHECK(got.aTry && got.step == 2 && got.notice.empty());
    tryOpts.step = 0;
    CHECK(help::startTutorial("essai-sans", {}, tryOpts) == help::StartResult::Started);
    CHECK(!got.aTry && got.step == 0 && got.notice.find("pas d'\xC3\xA9tape") != std::string::npos);
    CHECK(help::startTutorial("essai-loin") == help::StartResult::Started && got.notice.empty() && !got.aTry);
    {
        const auto c = help::findTutorial("essai-loin")->compile({});
        CHECK(help::nearestATry(c, 0) == std::optional<std::size_t>(2));
        CHECK(help::nearestATry(c, 3) == std::optional<std::size_t>(2));
        CHECK(!help::nearestATry(help::findTutorial("essai-sans")->compile({}), 0));
        CHECK(help::aTryNotice(2, 2).empty());
    }
    help::setTutorialLauncher({});
    help::clearTutorials();
}

// Section 5 (tranche 9) : un tutoriel pour chaque sujet. Les sujets (T2 : setTopics), le
// deducteur (T3 : setTutorialDeducer), tutorialForTopic (l'ecrit passe devant, le deduit en
// cache), le compteur "Tutoriels prets" (countTutorials) et le lancement par la cle du sujet.
void testTopics() {
    help::clearTutorials();
    help::setTutorialDeducer({});
    std::vector<help::TopicInfo> list(4);
    list[0].key = "ecrit";
    list[0].kind = help::TopicKind::Guide;
    list[1].key = "objet-x";
    list[1].kind = help::TopicKind::Object;
    list[1].name = "X";
    list[2].key = "casse";
    list[2].kind = help::TopicKind::Macro;
    list[3].key = "rien";
    help::setTopics(list);
    CHECK(help::topics().size() == 4 && help::findTopic("objet-x") && help::findTopic("objet-x")->name == "X");
    CHECK(help::findTopic("inconnu") == nullptr);
    CHECK(std::string(help::topicKindName(help::TopicKind::Object)) == "objet");
    CHECK(help::registerTutorialText("= tuto-ecrit | Ecrit\n@sujet ecrit\n== 1 | Un\n: u\nattendre 1s\n") == "tuto-ecrit");
    // Sans deducteur : l'ecrit seul.
    auto c = help::countTutorials();
    CHECK(c.total == 4 && c.ready == 1 && c.written == 1 && c.deduced == 0 && c.missing.size() == 3 && c.broken.empty());
    CHECK(help::tutorialForTopic("objet-x") == nullptr);
    CHECK(help::deducedTutorialText("objet-x").empty());
    int calls = 0;
    help::setTutorialDeducer([&](const help::TopicInfo& t) -> std::string {
        ++calls;
        if (t.kind == help::TopicKind::Object)
            return "= deduit-" + t.key + " | " + t.name + "\n== 1 | Poser\n: pose " + t.name + "\nattendre 1s\n";
        if (t.kind == help::TopicKind::Macro) return "pas un tutoriel";
        if (t.kind == help::TopicKind::Guide) return "= deduit-guide | G\n== 1 | Un\n: u\nattendre 1s\n";
        return {};
    });
    CHECK(help::hasTutorialDeducer());
    const auto* d = help::tutorialForTopic("objet-x");
    CHECK(d && d->id == "deduit-objet-x" && help::isDeducedTutorial(d));
    CHECK(d && std::find(d->topics.begin(), d->topics.end(), "objet-x") != d->topics.end());
    CHECK(help::tutorialForTopic("objet-x") == d && calls == 1);   // en cache
    CHECK(help::findTutorial("deduit-objet-x") == d);
    CHECK(help::deducedTutorialText("objet-x").rfind("= deduit-objet-x", 0) == 0);
    // L'ecrit passe devant le deduit.
    const auto* w = help::tutorialForTopic("ecrit");
    CHECK(w && w->id == "tuto-ecrit" && !help::isDeducedTutorial(w));
    std::string fault;
    CHECK(help::tutorialForTopic("casse", &fault) == nullptr && !fault.empty());
    c = help::countTutorials();
    CHECK(c.total == 4 && c.ready == 2 && c.written == 1 && c.deduced == 1);
    CHECK(c.missing.size() == 1 && c.missing[0] == "rien");
    CHECK(c.broken.size() == 1 && c.broken[0].rfind("casse : ", 0) == 0);
    // Le lanceur de T2 : par la cle du sujet, le deduit part ; sans tutoriel, Unknown.
    std::string gotId;
    help::setTutorialLauncher([&](const help::Tutorial& tt, const help::TutorialStart&) {
        gotId = tt.id;
        return true;
    });
    CHECK(help::startTutorial("objet-x") == help::StartResult::Started && gotId == "deduit-objet-x");
    CHECK(help::startTutorial("rien") == help::StartResult::Unknown);
    // Un nouveau deducteur vide le cache ; l'adresse d'avant reste lisible.
    help::setTutorialDeducer([](const help::TopicInfo&) { return std::string(); });
    CHECK(d->id == "deduit-objet-x" && help::tutorialForTopic("objet-x") == nullptr);
    help::setTutorialLauncher({});
    help::setTutorialDeducer({});
    help::setTopics({});
    help::clearTutorials();
}

// Tranche 19 (decision du chef du 03/10, 12) : les notes de version n'attendent pas de tutoriel ;
// elles sortent du compteur. Les autres pages speciales (Raccourcis, Signaler) y restent : sans
// tutoriel ecrit, elles manquent ; avec, elles sont pretes.
void testNotesOutOfCount() {
    help::clearTutorials();
    help::setTutorialDeducer([](const help::TopicInfo& t) -> std::string {
        if (t.kind == help::TopicKind::Special) return {};   // la regle de T3 : pas de deduit
        return "= deduit-" + t.key + " | D\n== 1 | Un\n: u\nattendre 1s\n";
    });
    std::vector<help::TopicInfo> list(5);
    list[0].key = "notes-1.11";
    list[0].kind = help::TopicKind::Special;
    list[0].name = "1.11";
    list[1].key = "notes-1.10.4";
    list[1].kind = help::TopicKind::Special;
    list[2].key = "page-raccourcis";
    list[2].kind = help::TopicKind::Special;
    list[3].key = "page-signaler";
    list[3].kind = help::TopicKind::Special;
    list[4].key = "notes-guide";   // un sujet du guide dont la cle commence par notes- : il compte
    list[4].kind = help::TopicKind::Guide;
    help::setTopics(list);
    CHECK(!help::tutorialExpected(list[0]) && !help::tutorialExpected(list[1]));
    CHECK(help::tutorialExpected(list[2]) && help::tutorialExpected(list[3]) && help::tutorialExpected(list[4]));
    auto c = help::countTutorials();
    CHECK(c.total == 3 && c.ready == 1 && c.deduced == 1 && c.written == 0);
    CHECK(c.excluded.size() == 2 && c.excluded[0] == "notes-1.11" && c.excluded[1] == "notes-1.10.4");
    CHECK(c.missing.size() == 2 && c.missing[0] == "page-raccourcis" && c.missing[1] == "page-signaler");
    // Un tutoriel ecrit pour Raccourcis : la page est prete ; Signaler manque encore.
    CHECK(help::registerTutorialText("= raccourcis | R\n@sujet page-raccourcis\n== 1 | Un\n: u\nattendre 1s\n") == "raccourcis");
    c = help::countTutorials();
    CHECK(c.total == 3 && c.ready == 2 && c.written == 1 && c.deduced == 1 && c.excluded.size() == 2);
    CHECK(c.missing.size() == 1 && c.missing[0] == "page-signaler" && c.broken.empty());
    help::setTutorialDeducer({});
    help::setTopics({});
    help::clearTutorials();
}

// Les tutoriels embarques (src/help/TutorialTexts.cpp, genere) : chacun se lit sans probleme, et,
// quand on a le dossier des sources, il est le fichier octet pour octet (sinon : regenerer).
void testEmbedded(const std::filesystem::path* dir) {
    const auto& all = help::embeddedTutorials();
    CHECK(all.size() >= 3);
    help::clearTutorials();
    std::vector<std::string> errors;
    CHECK(help::registerEmbeddedTutorials(&errors) == static_cast<int>(all.size()));
    for (const auto& e : errors) std::printf("  %s\n", e.c_str());
    CHECK(errors.empty());
    for (const char* id : {"objet-vanne", "script-rampe", "expression-couleur"}) {
        const auto* t = help::findTutorial(id);
        CHECK(t != nullptr);
        if (t) CHECK(t->compile().problems.empty() && t->compile().steps.size() == 7);
    }
    CHECK(help::findTutorial("scripts") == help::findTutorial("script-rampe"));   // par son @sujet
    // Tranche 6 : "Nouveau script ST" est un dialogue (pas d'inspecteur) : champ:1, bouton:Creer,
    // puis la ligne du script dans la liste.
    if (const auto* t = help::findTutorial("script-rampe")) {
        const auto c = t->compile();
        auto has = [&](const std::string& target) {
            return !c.steps.empty() && std::any_of(c.steps[0].gestures.begin(), c.steps[0].gestures.end(),
                                                   [&](const help::Gesture& g) { return g.target == target; });
        };
        CHECK(has("champ:1") && has("bouton:Cr\xC3\xA9" "er") && has("ligne:Rampe_Vanne") && !has("propriete:Nom"));
    }
    // Tranche 21 (lot final) : l'A toi de l'etape 4 ("Lance Compiler") etait deja vrai avant F7 :
    // script(Rampe_Vanne).erreurs compte les fautes soulignees pendant la frappe (celle de l'etape
    // 3). compiler.resultats n'a de valeur qu'apres Compiler : faux avant ses gestes, juste apres.
    if (const auto* t = help::findTutorial("script-rampe")) {
        const auto c = t->compile();
        const help::TutorialATry* a = c.steps.size() == 7 && c.steps[3].aTry ? &*c.steps[3].aTry : nullptr;
        CHECK(a != nullptr);
        if (a) {
            CHECK(a->checks.size() == 2 && a->checks[0].path == "compiler.resultats" && a->checks[0].op == ">="
                  && a->checks[0].value == "1" && a->checks[1].path == "script(Rampe_Vanne).erreurs");
            std::map<std::string, std::string> v{{"script(Rampe_Vanne).erreurs", "1"}, {"compiler.resultats", ""}};
            const help::PathReader read = [&](std::string_view p) -> std::optional<std::string> {
                const auto it = v.find(std::string(p));
                if (it == v.end()) return std::nullopt;
                return it->second;
            };
            // La fin de l'etape 3 : la faute soulignee, Compiler pas encore lance ("").
            CHECK(help::aTryAlreadyTrue(*a, read).empty());
            CHECK(help::evaluateATry(*a, read).result == help::CheckOutcome::Result::NotYet);
            // Apres F7 : une ligne de resultats, la faute toujours la.
            v["compiler.resultats"] = "1";
            CHECK(help::evaluateATry(*a, read).result == help::CheckOutcome::Result::Ok);
            // Compiler lance sur un script deja corrige : pas la faute que le bravo annonce.
            v["compiler.resultats"] = "0";
            v["script(Rampe_Vanne).erreurs"] = "0";
            CHECK(help::evaluateATry(*a, read).result == help::CheckOutcome::Result::NotYet);
        }
    }
    {
        std::vector<help::TutorialProblem> pb;
        const auto t = help::Tutorial::parse("= t | T\n== 1 | A\n: B\nclic champ:2\nencadrer \"ligne:Rampe Vanne\"\n", &pb);
        CHECK(pb.empty() && t.compile().problems.empty());
    }
    help::clearTutorials();
    if (!dir) return;
    std::size_t files = 0;
    for (const auto& f : std::filesystem::directory_iterator(*dir)) {
        if (f.path().extension() != ".tuto") continue;
        ++files;
        std::ifstream in(f.path(), std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const auto name = f.path().filename().string();
        const auto it = std::find_if(all.begin(), all.end(), [&](const auto& e) { return name == e.file; });
        const bool same = it != all.end() && ss.str() == it->text;
        if (!same)
            std::printf("  %s : TutorialTexts.cpp n'est pas a jour (python3 tools/tutoriels/generer_tutoriels.py "
                        "--cpp src/help/TutorialTexts.cpp)\n", name.c_str());
        CHECK(same);
    }
    CHECK(files == all.size());
}

// Tranche 13 : la touche dessinee porte le nom francais de l'appli.
// Tranche 16 (l'idee de T3, acceptee par le chef) : un "A toi" DEJA VRAI AVANT SES GESTES ne
// verifie rien. help::aTryAlreadyTrue le dit, avec ses conditions et leur ligne ; le verificateur
// (tutoriel-avant, app/ScriptRunner) le lit sur l'etat de la fin de l'etape d'avant.
const char* kAlreadyTrue =
    "= avant | Un A toi deja vrai\n"                              // 1
    "@sujet avant\n"                                               // 2
    "\n"                                                           // 3
    "== 1 | Choisis le type\n"                                     // 4
    ": Clique **Couleur** : la page met le premier exemple.\n"     // 5
    "clic \"bouton:#   Couleur\"\n"                                // 6
    "\n"                                                           // 7
    "== 2 | Le premier exemple\n"                                  // 8
    ": Il est dans le champ d'essai.\n"                            // 9
    "encadrer champ:essai\n"                                       // 10
    "@atoi V\xC3\xA9rifie le champ d'essai.\n"                     // 11
    "@verifier essai.convient = oui\n"                             // 12
    "\n"                                                           // 13
    "== 3 | Un autre exemple\n"                                    // 14
    ": \xC3\x89" "cris-le.\n"                                      // 15
    "texte champ:essai \"=16#FF\"\n"                               // 16
    "@atoi \xC3\x89" "cris =16#FF dans le champ d'essai.\n"        // 17
    "@verifier essai.texte = \"=16#FF\"\n"                         // 18
    "@verifier essai.convient = oui\n"                             // 19
    "@presque essai.texte = \"=16#ff\" | Les majuscules : {valeur}.\n"  // 20
    "\n"                                                           // 21
    "== 4 | Un seuil\n"                                            // 22
    ": Au-del\xC3\xA0 de 80.\n"                                    // 23
    "texte champ:seuil \"85\"\n"                                   // 24
    "@atoi Mets 85.\n"                                             // 25
    "@verifier seuil > 80\n"                                       // 26
    "@verifier essai.texte ~ 16#\n";                               // 27

void testATryAlreadyTrue() {
    std::vector<help::TutorialProblem> problems;
    const auto t = help::Tutorial::parse(kAlreadyTrue, &problems);
    CHECK(problems.empty());
    for (const auto& p : problems) std::printf("  ligne %d : %s\n", p.line, p.message.c_str());
    const auto c = t.compile();
    CHECK(c.steps.size() == 4);
    if (c.steps.size() != 4) return;
    CHECK(!c.steps[0].aTry && c.steps[1].aTry && c.steps[2].aTry && c.steps[3].aTry);
    if (!c.steps[1].aTry || !c.steps[2].aTry || !c.steps[3].aTry) return;
    CHECK(c.steps[1].aTry->checks.size() == 1 && c.steps[1].aTry->checks[0].line == 12);
    CHECK(c.steps[2].aTry->checks.size() == 2 && c.steps[2].aTry->checks[0].line == 18 && c.steps[2].aTry->checks[1].line == 19);
    CHECK(c.steps[2].aTry->checks.size() == 2 && c.steps[2].aTry->checks[0].value == "=16#FF");

    std::map<std::string, std::string> values;
    const help::PathReader read = [&](std::string_view p) -> std::optional<std::string> {
        const auto it = values.find(std::string(p));
        if (it == values.end()) return std::nullopt;
        return it->second;
    };
    // Avant les gestes de l'etape 2 (la fin de l'etape 1) : la page a mis le premier exemple, qui
    // convient. L'A toi de l'etape 2 est deja vrai : c'etait l'etape 3 des expr-* de tutos-v4.
    values["essai.convient"] = "oui";
    values["essai.texte"] = "=SEL(V[0].Pos > 80, '#E53935', '#43A047')";
    CHECK(help::aTryAlreadyTrue(*c.steps[1].aTry, read) == "essai.convient = oui (ligne 12)");
    // L'etape 3 demande un AUTRE exemple (tutos-v5) : faux avant ses gestes, rien a signaler.
    CHECK(help::aTryAlreadyTrue(*c.steps[2].aTry, read).empty());
    // ... mais deja vrai si une etape d'avant l'a deja ecrit : les deux conditions, et leur ligne.
    values["essai.texte"] = "=16#FF";
    CHECK(help::aTryAlreadyTrue(*c.steps[2].aTry, read) == "essai.texte = =16#FF (ligne 18) et essai.convient = oui (ligne 19)");
    // Une erreur reconnue (@presque) n'est pas un A toi juste : rien a signaler.
    values["essai.texte"] = "=16#ff";
    CHECK(help::evaluateATry(*c.steps[2].aTry, read).result == help::CheckOutcome::Result::Almost);
    CHECK(help::aTryAlreadyTrue(*c.steps[2].aTry, read).empty());
    // Une condition fausse ou illisible : rien a signaler.
    values["essai.convient"] = "non";
    values["essai.texte"] = "=16#FF";
    CHECK(help::aTryAlreadyTrue(*c.steps[2].aTry, read).empty());
    CHECK(help::aTryAlreadyTrue(*c.steps[3].aTry, read).empty());   // seuil illisible
    // Hors "=", la valeur lue est redite (elle dit pourquoi c'est vrai).
    values["seuil"] = "85";
    CHECK(help::aTryAlreadyTrue(*c.steps[3].aTry, read) ==
          "seuil > 80, lu \xC2\xAB 85 \xC2\xBB (ligne 26) et essai.texte ~ 16#, lu \xC2\xAB =16#FF \xC2\xBB (ligne 27)");
    values["seuil"] = "80";
    CHECK(help::aTryAlreadyTrue(*c.steps[3].aTry, read).empty());
    // Un A toi sans @verifier n'est jamais juste : rien a signaler. Sans lecteur non plus.
    help::TutorialATry none;
    none.instruction = "Regarde.";
    CHECK(help::aTryAlreadyTrue(none, read).empty());
    CHECK(help::aTryAlreadyTrue(*c.steps[1].aTry, help::PathReader{}).empty());

    // La condition nommee (la faute de tutoriel-verifier et l'avertissement), et la valeur lue.
    const auto& ck = c.steps[1].aTry->checks[0];
    CHECK(help::checkText(ck, std::string("non")) == "essai.convient = oui, lu \xC2\xAB non \xC2\xBB (ligne 12)");
    CHECK(help::checkText(ck, std::nullopt) == "essai.convient = oui, lu \xC2\xAB (illisible) \xC2\xBB (ligne 12)");
    CHECK(help::checkText(ck, std::string("oui"), false) == "essai.convient = oui (ligne 12)");
    CHECK(help::shortReadValue(std::string("a\nb\tc\rd")) == "a b c d");
    CHECK(help::shortReadValue(std::string(80, 'x')) == std::string(80, 'x'));
    CHECK(help::shortReadValue(std::string(100, 'x')) == std::string(77, 'x') + "\xE2\x80\xA6");
    // Coupee entre deux caracteres : le "e" accentue (2 octets, 76 et 77) n'est pas coupe en deux.
    CHECK(help::shortReadValue(std::string(76, 'x') + "\xC3\xA9" + std::string(30, 'y')) == std::string(76, 'x') + "\xE2\x80\xA6");

    // L'etat ou tutoriel-avant lit l'A toi de l'etape n (la fin de l'etape n-1) est celui ou "A toi"
    // met l'utilisateur (startATry : seek(n, 0)) : la scene factice a la meme signature, sauf la bulle.
    FakeStage a;
    FakeStage b;
    help::TutorialPlayer pa(t, a);
    help::TutorialPlayer pb(t, b);
    for (std::size_t n = 1; n < c.steps.size(); ++n) {
        pa.seek(n - 1, 9999999);
        pb.seek(n, 0);
        CHECK(pa.step() == n - 1 && pa.stepTimeMs() >= static_cast<double>(c.steps[n - 1].durationMs));
        CHECK(pb.step() == n && pb.stepTimeMs() <= 0.0);
        a.state.erase("etape");
        b.state.erase("etape");
        CHECK(a.signature() == b.signature());
        // "A toi" (startATry) remet la scene au meme etat.
        CHECK(pb.startATry());
        b.state.erase("etape");
        CHECK(a.signature() == b.signature());
    }
}

void testKeyLabels() {
    bool known = false;
    CHECK(help::keyLabel("Return", &known) == "Entr\xC3\xA9" "e" && known);
    CHECK(help::keyLabel("entree") == "Entr\xC3\xA9" "e");
    CHECK(help::keyLabel("Escape") == "\xC3\x89" "chap");
    CHECK(help::keyLabel("echap") == "\xC3\x89" "chap");
    CHECK(help::keyLabel("Delete") == "Suppr");
    CHECK(help::keyLabel("suppr") == "Suppr");
    CHECK(help::keyLabel("Tab") == "Tab");
    CHECK(help::keyLabel("maj+tab") == "Maj+Tab");
    CHECK(help::keyLabel("ctrl+maj+Tab") == "Ctrl+Maj+Tab");
    CHECK(help::keyLabel("space") == "Espace");
    CHECK(help::keyLabel("ctrl+espace") == "Ctrl+Espace");
    CHECK(help::keyLabel("left") == "\xE2\x86\x90");
    CHECK(help::keyLabel("droite") == "\xE2\x86\x92");
    CHECK(help::keyLabel("Up") == "\xE2\x86\x91");
    CHECK(help::keyLabel("maj+bas") == "Maj+\xE2\x86\x93");
    CHECK(help::keyLabel("home") == "D\xC3\xA9" "but");
    CHECK(help::keyLabel("maj+end") == "Maj+Fin");
    CHECK(help::keyLabel("backspace") == "Retour arri\xC3\xA8re");
    CHECK(help::keyLabel("pageup") == "Pg pr\xC3\xA9" "c");
    CHECK(help::keyLabel("ctrl+a") == "Ctrl+A");
    CHECK(help::keyLabel("shift+alt+z") == "Maj+Alt+Z");
    CHECK(help::keyLabel("F8", &known) == "F8" && known);
    CHECK(help::keyLabel("f10") == "F10");
    CHECK(help::keyLabel("1") == "1");
    // Inconnu : rendu tel quel, et dit.
    CHECK(help::keyLabel("Bidule", &known) == "Bidule" && !known);
    CHECK(help::keyLabel("hyper+a", &known) == "hyper+a" && !known);
    CHECK(help::keyLabel("f13", &known) == "f13" && !known);
    CHECK(help::keyLabel("ctrl", &known) == "ctrl" && !known);  // un modificateur seul
    CHECK(help::keyLabel("", &known).empty() && !known);
}

} // namespace

// Tranche 23 (R111-15, decision 43) : L'ENCADRE SUIT SA CIBLE a chaque image. Sur api-ordre, le
// clic sur API > Ordre d'execution ajoute "Recents" (deux lignes) en tete de l'arbre : la ligne
// descend, et l'encadre, reste a l'ancienne place, entourait Blocs DFB (R145_08). Ici, un arbre
// factice dont la ligne visee bouge : l'encadre la retrouve elle-meme a chaque image.
void testSpotFollows() {
    const std::string target = "arbre:API/Ordre d'ex\xC3\xA9" "cution";
    constexpr float kRow = 22.f;
    int row = 9;              // le rang de la ligne visee (Blocs DFB est au rang 9 apres "Recents")
    bool inView = true;
    float height = kRow;      // 0 : pas encore placee
    int finds = 0;
    const help::TutorialSpot::Finder tree = [&](const std::string& t) -> std::optional<gfx::Rect> {
        ++finds;
        if (t != target || !inView) return std::nullopt;
        return gfx::Rect{0.f, 100.f + static_cast<float>(row) * kRow, 240.f, height};
    };
    auto at = [&](int r) { return 100.f + static_cast<float>(r) * kRow; };

    help::TutorialSpot spot;
    spot.follow(tree);
    CHECK(!spot.rect() && !spot.following() && finds == 0);   // rien a suivre

    spot.show(target, gfx::Rect{0.f, at(row), 240.f, kRow}, false);
    CHECK(spot.rect() && spot.rect()->y == at(9) && spot.target() == target);
    spot.follow(tree);
    CHECK(spot.rect() && spot.rect()->y == at(9) && finds == 1);
    // Le clic a ouvert l'onglet : "Recents" et sa ligne apparaissent en tete, la cible descend de 2.
    row += 2;
    spot.follow(tree);
    CHECK(spot.rect() && spot.rect()->y == at(11));            // la cible elle-meme...
    CHECK(spot.rect() && spot.rect()->y != at(9));             // ... pas l'ancienne place (Blocs DFB)
    // Elle remonte (un dossier se replie au-dessus) : l'encadre la suit encore, image apres image.
    for (int r = 11; r >= 4; --r) {
        row = r;
        spot.follow(tree);
        CHECK(spot.rect() && spot.rect()->y == at(r));
    }
    // L'arbre defile, la ligne sort de la vue : l'encadre s'efface, mais la cible reste suivie...
    inView = false;
    spot.follow(tree);
    CHECK(!spot.rect() && spot.following());
    // ... et il revient avec elle, a sa nouvelle place.
    inView = true;
    row = 6;
    spot.follow(tree);
    CHECK(spot.rect() && spot.rect()->y == at(6));
    // Une cible de taille nulle n'est pas encore placee (tranche 13) : pas d'encadre au coin.
    height = 0.f;
    spot.follow(tree);
    CHECK(!spot.rect() && spot.following());
    height = kRow;
    spot.follow(tree);
    CHECK(spot.rect() && spot.rect()->y == at(6));
    // Une nouvelle etape (ou Recommencer) efface l'encadre : il ne revient pas, meme si la cible est la.
    spot.clear();
    const int before = finds;
    spot.follow(tree);
    CHECK(!spot.rect() && !spot.following() && finds == before);

    // Une cible fugace (la liste de l'aide a la saisie, tranche 11) : elle bouge, l'encadre la suit ;
    // elle se ferme, il s'efface pour de bon, meme si une autre liste s'ouvre ensuite.
    bool open = true;
    float listY = 300.f;
    const help::TutorialSpot::Finder list = [&](const std::string& t) -> std::optional<gfx::Rect> {
        if (t != "aide-saisie" || !open) return std::nullopt;
        return gfx::Rect{40.f, listY, 180.f, 120.f};
    };
    spot.show("aide-saisie", gfx::Rect{40.f, listY, 180.f, 120.f}, true);
    listY = 320.f;
    spot.follow(list);
    CHECK(spot.rect() && spot.rect()->y == 320.f);
    open = false;
    spot.follow(list);
    CHECK(!spot.rect() && !spot.following());
    open = true;
    spot.follow(list);
    CHECK(!spot.rect());

    // Sans recherche branchee (les essais sans appli) : l'encadre s'efface, rien ne plante.
    spot.show(target, gfx::Rect{0.f, at(3), 240.f, kRow}, false);
    spot.follow(nullptr);
    CHECK(!spot.rect() && spot.following());
}

// Tranche 23 (decision 44) : L'ANCIEN BAC S'EFFACE SANS QU'ON L'ATTENDE. discard() libere le dossier
// tout de suite (une copie neuve peut y aller), et l'efface dans un fil a part ; finishDiscards() l'attend.
// 1.11.1 (T1, R111-14 / R111-24) : le bac a sable d'un tutoriel, reconnu par son dossier (l'ecran
// d'analyse et le bandeau y sautent leurs deux ver::compare apres chaque remise en place).
void testSandboxContains() {
    const std::filesystem::path root = "/tmp/xpg-bac-a-sable";
    CHECK(help::sandbox::contains("/tmp/xpg-bac-a-sable/objet-vanne-1/Armoire_Gaz", root));
    CHECK(help::sandbox::contains("/tmp/xpg-bac-a-sable", root));
    CHECK(help::sandbox::contains("/tmp/xpg-bac-a-sable/", root));
    CHECK(help::sandbox::contains("/tmp/./xpg-bac-a-sable/expr-reel-0/Armoire_Gaz/../Armoire_Gaz", root));
    CHECK(!help::sandbox::contains("/tmp/xpg-bac-a-sable-autre/Armoire_Gaz", root));
    CHECK(!help::sandbox::contains("/home/client/Projets/Armoire_Gaz", root));
    CHECK(!help::sandbox::contains("/tmp", root));
    CHECK(!help::sandbox::contains("", root));
    CHECK(!help::sandbox::contains("/tmp/xpg-bac-a-sable/x", std::filesystem::path()));
}

void testSandboxDiscard() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / ("tutorial_test-bacs-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto fill = [&](const fs::path& slot) {
        for (int d = 0; d < 6; ++d) {
            const auto dir = slot / "Armoire_Gaz" / ("dossier" + std::to_string(d));
            fs::create_directories(dir, ec);
            for (int f = 0; f < 40; ++f) std::ofstream(dir / ("f" + std::to_string(f) + ".txt")) << "contenu " << f;
        }
    };
    auto trashes = [&] {
        int n = 0;
        for (const auto& e : fs::directory_iterator(root, ec))
            if (e.path().filename().string().rfind(help::sandbox::kTrashPrefix, 0) == 0) ++n;
        return n;
    };
    const fs::path slot = root / "deduit-dossier-0";
    fill(slot);
    CHECK(fs::exists(slot / "Armoire_Gaz" / "dossier5" / "f39.txt"));
    CHECK(help::sandbox::discard(slot));
    CHECK(!fs::exists(slot));                                   // libre tout de suite...
    CHECK(fs::create_directories(slot / "Armoire_Gaz", ec));     // ... une copie neuve y va
    help::sandbox::finishDiscards();
    CHECK(trashes() == 0);                                      // l'ancien est efface
    CHECK(fs::exists(slot / "Armoire_Gaz"));                    // le neuf n'est pas touche
    // Deux remises de suite : la seconde n'attend pas la premiere (1.11.1 : la file du fil) ; rien ne reste.
    fill(slot);
    CHECK(help::sandbox::discard(slot));
    fill(slot);
    CHECK(help::sandbox::discard(slot));
    CHECK(!fs::exists(slot));
    help::sandbox::finishDiscards();
    CHECK(trashes() == 0);
    // Un dossier qui n'existe pas : rien a faire.
    CHECK(help::sandbox::discard(root / "absent"));
    help::sandbox::finishDiscards();
    help::sandbox::finishDiscards();                            // deux fois : sans effet
    CHECK(trashes() == 0);
    // 1.11.1 (tranche 31) : le vieux bac d'un autre tutoriel s'efface dans le fil, sans etre renomme ;
    // pose deux fois dans la file, il n'y est qu'une ; trois remises de suite, puis tout s'efface.
    const fs::path vieux = root / "autre-tuto-0";
    fill(vieux);
    help::sandbox::discardLater(vieux);
    help::sandbox::discardLater(vieux);
    for (int r = 0; r < 3; ++r) {
        fill(slot);
        CHECK(help::sandbox::discard(slot));
        CHECK(!fs::exists(slot));
    }
    help::sandbox::finishDiscards();
    CHECK(!fs::exists(vieux));
    CHECK(!help::sandbox::pending(vieux));
    CHECK(trashes() == 0);
    help::sandbox::discardLater(root / "absent-aussi");          // n'existe pas : sans erreur
    help::sandbox::finishDiscards();
    CHECK(!help::sandbox::pending(root / "absent-aussi"));
    fs::remove_all(root, ec);
}

// Tranche 24 (decision 53, R111-12 : les tutoriels de Demarrer, le premier chapitre du client) : la
// cible barre:<partie> (barre:? : le "?" de la barre du haut, qui ouvre le menu Aide) et les chemins
// de l'ecran centre.ouvert et onglet.courant (help::screen, TutorialScreen.hpp ; TutorialApp.cpp
// les lit de la pile des menus et de l'onglet courant, UiDriver trouve barre: par TopBar::partRect).
const char* kStartChapter =
    "= menu-aide | Le menu Aide et le centre d'aide\n"            // 1
    "@sujet menu-aide\n"                                           // 2
    "\n"                                                           // 3
    "== 1 | Le \"?\" de la barre du haut\n"                        // 4
    ": Le **?**, en haut \xC3\xA0 droite, ouvre le menu Aide.\n"   // 5
    "clic barre:?\n"                                               // 6
    "encadrer menu:Aide\n"                                         // 7
    "\n"                                                           // 8
    "== 2 | Le centre d'aide\n"                                    // 9
    ": **Aide** (F1) ouvre le centre d'aide.\n"                    // 10
    "touche F1\n"                                                  // 11
    "@atoi Ouvre le centre d'aide (F1).\n"                         // 12
    "@verifier centre.ouvert = oui\n"                              // 13
    "\n"                                                           // 14
    "== 3 | Le tableau de bord\n"                                  // 15
    ": Il s'ouvre dans l'arbre.\n"                                 // 16
    "clic arbre:API\n"                                             // 17
    "@atoi Ouvre le tableau de bord de l'API.\n"                   // 18
    "@verifier onglet.courant ~ Tableau de bord\n"                 // 19
    "@verifier centre.ouvert = non\n";                             // 20

void testScreenPaths() {
    // La cible : un genre connu, lu sans probleme ; vide, elle reste inconnue.
    CHECK(help::isKnownTarget("barre:?"));
    CHECK(help::isKnownTarget("barre:aide"));
    CHECK(!help::isKnownTarget("barre:"));
    // 1.11.2 (T1, R1112-3) : une macro de la liste de l'onglet Macros, par son nom ou son chemin.
    CHECK(help::isKnownTarget("macro:ImportES"));
    CHECK(help::isKnownTarget("macro:Importer depuis un CSV/ImportES"));
    CHECK(!help::isKnownTarget("macro:"));
    CHECK(help::screen::topBarPart("?") == "aide");
    CHECK(help::screen::topBarPart("Aide") == "aide");
    CHECK(help::screen::topBarPart("nouveau") == "nouveau");
    std::vector<help::TutorialProblem> problems;
    const auto t = help::Tutorial::parse(kStartChapter, &problems);
    CHECK(problems.empty());
    for (const auto& p : problems) std::printf("  ligne %d : %s\n", p.line, p.message.c_str());
    const auto c = t.compile();
    CHECK(c.steps.size() == 3);
    if (c.steps.size() != 3) return;
    CHECK(!c.steps[0].gestures.empty() && c.steps[0].gestures[0].kind == help::GestureKind::Click
          && c.steps[0].gestures[0].target == "barre:?");
    CHECK(c.steps[1].aTry && c.steps[2].aTry);
    if (!c.steps[1].aTry || !c.steps[2].aTry) return;
    // L'"A toi" de l'etape 2 cible par defaut le dernier geste (aucun : F1 n'a pas de cible).
    CHECK(c.steps[1].aTry->checks.size() == 1 && c.steps[1].aTry->checks[0].path == "centre.ouvert");

    // Les chemins de l'ecran, lus comme la scene les lit (TutorialApp.cpp).
    help::screen::State st;
    const help::PathReader read = [&](std::string_view p) { return help::screen::read(p, st); };
    // Une remise a neuf rouvre le bac : la pile n'a que l'ecran d'analyse, pas encore d'onglet.
    st.menus = {"analysis"};
    CHECK(read("centre.ouvert") == std::optional<std::string>("non"));
    CHECK(read("onglet.courant") == std::optional<std::string>(""));
    CHECK(!read("objet(V_201).valveType"));          // pas un chemin de l'ecran : la scene cherche ailleurs
    // Avant les gestes de l'etape 2 : faux, donc pas un "A toi" deja vrai (rien a signaler).
    CHECK(help::evaluateATry(*c.steps[1].aTry, read).result != help::CheckOutcome::Result::Ok);
    CHECK(help::aTryAlreadyTrue(*c.steps[1].aTry, read).empty());
    // F1 : le centre d'aide par-dessus l'ecran d'analyse (PushMenu("help")) : Bravo.
    st.menus = {"analysis", "help"};
    CHECK(read("centre.ouvert") == std::optional<std::string>("oui"));
    CHECK(help::evaluateATry(*c.steps[1].aTry, read).result == help::CheckOutcome::Result::Ok);
    // Ses chapitres aussi (Aide > L'IHM, les macros, les blocs) ; la page des expressions n'en est pas un.
    for (const char* id : {"help.hmi", "help.macros", "help.blocs"}) {
        st.menus = {"analysis", id};
        CHECK(read("centre.ouvert") == std::optional<std::string>("oui"));
    }
    st.menus = {"analysis", "help.expressions"};
    CHECK(read("centre.ouvert") == std::optional<std::string>("non"));
    // Quitter le centre : faux de nouveau.
    st.menus = {"analysis"};
    CHECK(help::evaluateATry(*c.steps[1].aTry, read).result != help::CheckOutcome::Result::Ok);
    // L'etape 3 : l'onglet du tableau de bord, le centre ferme.
    CHECK(help::evaluateATry(*c.steps[2].aTry, read).result != help::CheckOutcome::Result::Ok);
    st.tab = "Tableau de bord";
    CHECK(read("onglet.courant") == std::optional<std::string>("Tableau de bord"));
    CHECK(help::evaluateATry(*c.steps[2].aTry, read).result == help::CheckOutcome::Result::Ok);
    st.menus = {"analysis", "help"};
    CHECK(help::evaluateATry(*c.steps[2].aTry, read).result != help::CheckOutcome::Result::Ok);
    st.menus = {"analysis"};
    st.tab = "Vue_Tuto";
    CHECK(help::evaluateATry(*c.steps[2].aTry, read).result != help::CheckOutcome::Result::Ok);
}

// 1.11.2 (T1, decision 141 : les macros ; le clic sur API > Macros laisse l'onglet « API ») : arbre.choisi, le
// chemin de la ligne choisie de l'arbre, et arbre.choisi(<chemin>), la ligne que vise arbre:<chemin>. Les lignes
// visibles sont rejouees comme l'explorateur les donne (TutorialApp.cpp : leur profondeur et leur libelle).
void testTreeChoice() {
    using help::screen::TreeRow;
    // Les compteurs tombent ; un nom qui finit par une parenthese sans nombre reste.
    CHECK(help::screen::treeLabel("Macros  [31]") == "Macros");
    CHECK(help::screen::treeLabel("Types d\xC3\xA9riv\xC3\xA9s (12)") == "Types d\xC3\xA9riv\xC3\xA9s");
    CHECK(help::screen::treeLabel("API") == "API");
    CHECK(help::screen::treeLabel("Pompe (P1)") == "Pompe (P1)");
    CHECK(help::screen::treeLabel("Vue_1[2]") == "Vue_1[2]");
    // L'arbre a l'ouverture du bac (la racine cachee) : les epingles, API deplie, IHM deplie.
    const std::vector<TreeRow> rows = {
        {0, "\xC3\x89pingl\xC3\xA9s"}, {1, "Macros"},
        {0, "API"}, {1, "Configuration"}, {1, "Types d\xC3\xA9riv\xC3\xA9s (12)"}, {1, "Blocs DFB (5)"}, {1, "Macros  [31]"},
        {2, "CREER_VANNE"},
        {0, "IHM"}, {1, "Vues"}, {2, "Vue_Tuto"}, {1, "Programmation g\xC3\xA9n\xC3\xA9rale"}, {2, "Variables IHM"},
        {0, "Simulation"}, {1, "Automate"},
    };
    const auto path = [&](std::size_t at) {
        std::string s;
        for (const auto& p : help::screen::treePath(rows, at)) s += (s.empty() ? "" : "/") + p;
        return s;
    };
    CHECK(path(6) == "API/Macros");
    CHECK(path(7) == "API/Macros/CREER_VANNE");
    CHECK(path(1) == "\xC3\x89pingl\xC3\xA9s/Macros");        // l'epingle n'est pas la ligne de l'API
    CHECK(path(12) == "IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Variables IHM");
    CHECK(path(2) == "API");
    CHECK(path(99).empty());

    // Lus comme la scene les lit : aucune ligne choisie, puis le clic sur API > Macros.
    help::screen::State st;
    const help::PathReader read = [&](std::string_view p) { return help::screen::read(p, st); };
    CHECK(help::screen::isTreePath("arbre.choisi") && help::screen::isTreePath("arbre.choisi(API/Macros)"));
    CHECK(!help::screen::isTreePath("onglet.courant"));
    CHECK(read("arbre.choisi") == std::optional<std::string>(""));
    CHECK(read("arbre.choisi(API/Macros)") == std::optional<std::string>("non"));
    st.tree = help::screen::treePath(rows, 6);
    CHECK(read("arbre.choisi") == std::optional<std::string>("API/Macros"));
    CHECK(read("arbre.choisi(API/Macros)") == std::optional<std::string>("oui"));
    CHECK(read("arbre.choisi(api/macros)") == std::optional<std::string>("oui"));      // sans la casse, comme la cible
    CHECK(read("arbre.choisi(Macros)") == std::optional<std::string>("oui"));
    CHECK(read("arbre.choisi(API/Types d\xC3\xA9riv\xC3\xA9s)") == std::optional<std::string>("non"));
    CHECK(read("arbre.choisi(API)") == std::optional<std::string>("non"));             // le dossier API n'est pas choisi
    CHECK(read("arbre.choisi(IHM/Macros)") == std::optional<std::string>("non"));
    CHECK(!read("arbre.choisi("));                                                     // pas un chemin de l'ecran
    // Une macro sous Macros : ce n'est plus le dossier.
    st.tree = help::screen::treePath(rows, 7);
    CHECK(read("arbre.choisi(API/Macros)") == std::optional<std::string>("non"));
    CHECK(read("arbre.choisi(API/Macros/CREER)") == std::optional<std::string>("oui"));
    // L'epingle Macros n'est pas API > Macros.
    st.tree = help::screen::treePath(rows, 1);
    CHECK(read("arbre.choisi(API/Macros)") == std::optional<std::string>("non"));
    // Le debut d'un libelle suffit, comme pour la cible (arbre:IHM/Programmation/Variables IHM).
    st.tree = help::screen::treePath(rows, 12);
    CHECK(read("arbre.choisi(IHM/Programmation/Variables IHM)") == std::optional<std::string>("oui"));
    CHECK(read("arbre.choisi(IHM/Variables IHM)") == std::optional<std::string>("oui"));
    CHECK(read("arbre.choisi(API/Variables IHM)") == std::optional<std::string>("non"));

    // Un "A toi" de macro, comme le deduit l'ecrirait : faux avant le clic (l'onglet API ne bouge pas), juste apres.
    const char* text =
        "= arbre-macros | Les macros\n"
        "@bac Armoire_Gaz\n"
        "\n== 1 | Ouvre Macros dans l'arbre de l'automate\n"
        ": Les macros de la biblioth\xC3\xA8que.\n"
        "clic \"arbre:API/Macros\"\n"
        "encadrer \"arbre:API/Macros\"\n"
        "@atoi Ouvre Macros dans l'arbre de l'automate (API \xE2\x80\xBA Macros).\n"
        "@cible \"arbre:API/Macros\"\n"
        "@verifier arbre.choisi(API/Macros) = oui\n";
    std::vector<help::TutorialProblem> problems;
    const auto t = help::Tutorial::parse(text, &problems);
    CHECK(problems.empty());
    for (const auto& p : problems) std::printf("  ligne %d : %s\n", p.line, p.message.c_str());
    const auto c = t.compile();
    CHECK(c.steps.size() == 1 && c.steps[0].aTry);
    if (c.steps.size() != 1 || !c.steps[0].aTry) return;
    CHECK(c.steps[0].aTry->checks.size() == 1 && c.steps[0].aTry->checks[0].path == "arbre.choisi(API/Macros)");
    st.tab = "API";
    st.tree = help::screen::treePath(rows, 2);   // le bac s'ouvre sur l'onglet API, la ligne API choisie
    CHECK(help::evaluateATry(*c.steps[0].aTry, read).result != help::CheckOutcome::Result::Ok);
    CHECK(help::aTryAlreadyTrue(*c.steps[0].aTry, read).empty());
    st.tree = help::screen::treePath(rows, 6);   // le clic : l'onglet reste « API », la ligne Macros est choisie
    CHECK(read("onglet.courant") == std::optional<std::string>("API"));
    CHECK(help::evaluateATry(*c.steps[0].aTry, read).result == help::CheckOutcome::Result::Ok);
}

// 1.11.2 (le LISEZ-MOI de la 1.11.0 : « dans un tutoriel d'objet, l'A toi ne te montre pas la tuile a
// glisser ») : TutorialATry::show, ce que l'A toi amene a l'ecran et encadre a son debut. Par defaut, la
// premiere tuile des gestes de l'etape (biblio:, variante:, {variante} remplace) ; @montrer la choisit ;
// sans tuile, rien (l'A toi d'avant). La scene reelle (TutorialStageApp::setInteractive) l'encadre apres
// la remise en place ; ici, le format et sa compilation.
void testATryShowsTile() {
    const char* text =
        "= tuile | Les tuiles\n"
        "@bac demo-ihm | Vue_1\n"
        "@variantes a, b\n"
        "@variante b\n"
        "== 1 | Pose la pompe\n"
        ": Dans la biblioth\xC3\xA8que, la tuile **Pompe** : glisse-la dans la vue.\n"
        "clic biblio:Pompe\n"
        "glisser biblio:Pompe vue:400,262\n"
        "@atoi Glisse Pompe dans la vue.\n"
        "@cible vue:400,262\n"
        "@verifier objets[Pompe] >= 1\n"
        "== 2 | Choisis la variante\n"
        ": La tuile **Vanne**, puis sa variante.\n"
        "clic biblio:Vanne\n"
        "encadrer variantes\n"
        "clic \"variante:{variante}\"\n"
        "glisser \"variante:{variante}\" vue:400,262\n"
        "@atoi Glisse la vanne dans la vue.\n"
        "@cible vue:400,262\n"
        "@verifier objets[Vanne] >= 1\n"
        "== 3 | Pose la variante\n"
        ": La variante choisie, sur la tuyauterie.\n"
        "encadrer vue:Tuyauterie_1\n"
        "glisser variante:{variante} vue:400,262\n"
        "@atoi Glisse la vanne sur la tuyauterie.\n"
        "@verifier objets[Vanne] >= 2\n"
        "== 4 | Nomme-la\n"
        ": Le **Nom**.\n"
        "clic propriete:Nom\n"
        "texte propriete:Nom \"V_1\"\n"
        "@atoi Nomme-la V_1.\n"
        "@verifier objet[Vanne].nom = V_1\n"
        "== 5 | Le choix\n"
        ": Les variantes, en haut.\n"
        "clic biblio:Vanne\n"
        "@atoi Choisis une variante.\n"
        "@montrer variantes\n"
        "@verifier biblio.variante <> \"\"\n"
        "== 6 | Sans A toi\n"
        ": Une tuile, sans A toi.\n"
        "clic biblio:Cuve\n";
    std::vector<help::TutorialProblem> problems;
    const auto t = help::Tutorial::parse(text, &problems);
    CHECK(problems.empty());
    for (const auto& p : problems) std::printf("  ligne %d : %s\n", p.line, p.message.c_str());
    const auto c = t.compile();
    CHECK(c.problems.empty() && c.steps.size() == 6);
    if (c.steps.size() == 6) {
        const auto show = [&](std::size_t i) { return c.steps[i].aTry ? c.steps[i].aTry->show : std::string("(pas d'A toi)"); };
        CHECK(show(0) == "biblio:Pompe");          // la tuile, pas la cible de l'A toi (vue:400,262)
        CHECK(c.steps[0].aTry && c.steps[0].aTry->target == "vue:400,262");
        CHECK(show(1) == "biblio:Vanne");          // la premiere tuile, avant la variante
        CHECK(show(2) == "variante:b");            // {variante} remplace
        CHECK(show(3).empty());                    // aucune tuile : rien a montrer
        CHECK(show(4) == "variantes");             // @montrer
        CHECK(!c.steps[5].aTry);
    }
    CHECK(t.compile("a").steps.size() == 6 && t.compile("a").steps[2].aTry && t.compile("a").steps[2].aTry->show == "variante:a");
    // @montrer : une cible connue, dans une etape.
    std::vector<help::TutorialProblem> bad;
    (void)help::Tutorial::parse("= x | X\n== 1 | A\n: a\nclic biblio:Pompe\n@atoi A.\n@montrer bidule:Truc\n@verifier a = 1\n", &bad);
    CHECK(bad.size() == 1 && bad[0].line == 6 && bad[0].message == "@montrer : une cible connue");
    // La Vanne ecrite : l'A toi de l'etape 1 montre sa tuile, celui de l'etape 2 la variante a glisser.
    help::clearTutorials();   // testEmbedded vide le registre a sa fin
    (void)help::registerEmbeddedTutorials();
    const auto* v = help::findTutorial("objet-vanne");
    CHECK(v != nullptr);
    if (v) {
        const auto r = v->compile("r\xC3\xA9glante");
        CHECK(r.steps.size() == 7 && r.steps[0].aTry && r.steps[0].aTry->show == "biblio:Vanne");
        CHECK(r.steps.size() == 7 && r.steps[1].aTry && r.steps[1].aTry->show == "variante:r\xC3\xA9glante");
    }
    help::clearTutorials();
}

int main(int argc, char** argv) {
    testScreenPaths();
    testTreeChoice();
    testSandboxDiscard();
    testSandboxContains();
    testSpotFollows();
    testKeyLabels();
    testATryAlreadyTrue();
    testFormatTools();
    testParseAndCompile();
    testProblems();
    testChecks();
    testPlayer();
    testBeforeAndLaunch();
    testTopics();
    testNotesOutOfCount();
    if (argc > 1) testWrittenTutorials(argv[1]);
    const std::filesystem::path dir = argc > 1 ? argv[1] : "";
    testEmbedded(argc > 1 ? &dir : nullptr);
    testATryShowsTile();
    std::printf("tutorial_test : %d/%d\n", g_ok, g_ko);
    return g_ko == 0 ? 0 : 1;
}
