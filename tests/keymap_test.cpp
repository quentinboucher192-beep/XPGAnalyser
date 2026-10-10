// =============================================================================
//  tests/keymap_test.cpp - 1.12.2 : les raccourcis de Visual Studio dans les
//  editeurs de code (ui/KeyMap, ui/widgets/CodeCommands.cpp)
// -----------------------------------------------------------------------------
//  1. la table : l'ecriture des touches, les accords, les deux profils ;
//  2. l'attente de la seconde touche (Ctrl+K puis Ctrl+C), l'accord inconnu, la
//     lettre d'un accord qui ne s'ecrit pas ;
//  3. les commandes de l'editeur, chacune sur un vrai MultiLineText : commenter,
//     dupliquer, deplacer, couper la ligne et la recoller au-dessus, indenter un
//     bloc, mettre en forme, aller au bout du bloc, chercher et remplacer, aller a
//     la ligne, les signets, les fautes (F8, Ctrl+.), les commandes de l'ecran ;
//  4. une commande = un pas d'annulation ; la vue ne saute plus a chaque frappe ;
//  5. la page d'aide : chaque touche des editeurs ecrite dans help/Shortcuts.cpp
//     est dans la table du profil Visual Studio.
// =============================================================================
#include "../src/core/Command.hpp"
#include "../src/help/Shortcuts.hpp"
#include "../src/ui/KeyMap.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ui;

namespace {

int gFailures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        ++gFailures;
        std::printf("ECHEC : %s\n", what.c_str());
    }
}
void same(const std::string& got, const std::string& want, const std::string& what) {
    if (got != want) {
        ++gFailures;
        std::printf("ECHEC : %s\n  obtenu : [%s]\n  attendu : [%s]\n", what.c_str(), got.c_str(), want.c_str());
    }
}

class FakeRenderer final : public gfx::IRenderer {
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
        const float adv = 8.f * scale(f);
        std::size_t g = 0;
        for (char c : s) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++g;
        return {static_cast<float>(g) * adv, adv, adv * 0.8f, adv * 0.2f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return 8.f * scale(f); }
    [[nodiscard]] gfx::Size surfaceSize() const override { return {1536.f, 1024.f}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f, float w) const override {
        const float adv = 8.f * scale(f);
        return w <= 0.f ? 0 : std::min<std::size_t>(s.size(), static_cast<std::size_t>(w / adv));
    }
private:
    static float scale(gfx::FontId f) { return std::max(1.f, std::round(static_cast<float>(f.v ? f.v : 16) / 8.f)); }
};

std::string gClipboard;

KeyMods ctrl() { return KeyMods{ true, false, false, false }; }
KeyMods ctrlShift() { return KeyMods{ true, true, false, false }; }
KeyMods shift() { return KeyMods{ false, true, false, false }; }
KeyMods alt() { return KeyMods{ false, false, true, false }; }

EventResult press(MultiLineText& v, Key k, KeyMods m = {}) { return v.dispatch(KeyDown{ k, m, false }); }
void type(MultiLineText& v, std::string_view text) {
    for (const char c : text) (void)v.dispatch(TextInput{ std::string(1, c) });
}
// Une touche qui ecrit (sans Ctrl) : son KeyDown, puis son TextInput, comme SDL.
void typeKey(MultiLineText& v, Key k, char c) {
    (void)v.dispatch(KeyDown{ k, {}, false });
    (void)v.dispatch(TextInput{ std::string(1, c) });
}

// Une commande du texte pour la pile : elle fusionne avec la precedente (comme la
// frappe d'un volet), sauf quand la pile est scellee.
class TextEdit final : public core::ICommand {
public:
    core::Status execute() override { return core::ok(); }
    core::Status undo() override { return core::ok(); }
    [[nodiscard]] std::string label() const override { return "Texte"; }
    [[nodiscard]] bool mergeableWith(const core::ICommand& o) const override { return dynamic_cast<const TextEdit*>(&o) != nullptr; }
    void mergeFrom(const core::ICommand&) override {}
};

}   // namespace

int main() {
    FakeRenderer metrics;
    installPlatformServices(PlatformServices{
        [&](std::string_view s, gfx::FontId f) { return metrics.measure(s, f).width; },
        [&](gfx::FontId f) { return metrics.lineHeight(f); },
        [] { return gClipboard; },
        [](std::string_view s) { gClipboard = std::string(s); },
    });
    const Theme theme = Theme::dark();
    const auto paint = [&](MultiLineText& v) {
        v.setBounds({ 0.f, 0.f, 900.f, 400.f });
        v.layout();
        v.render(PaintContext{ metrics, theme, { 0, 0, 900, 400 }, 0.0, nullptr });
    };
    const auto editor = [&](MultiLineText& v, std::string text) {
        v.setLanguage(Language::StructuredText);
        v.setReadOnly(false);
        v.setTabInsertsSpaces(4);
        v.setCommandKeys(true);
        v.setText(std::move(text));
        paint(v);
        v.takeFocus();
    };
    namespace km = keymap;

    // ---- 1. la table ---------------------------------------------------------------
    {
        const auto c = km::parse("Ctrl+K, Ctrl+C");
        check(c && c->twoStrokes() && c->first.key == Key::K && c->first.ctrl && c->second.key == Key::C, "Ctrl+K, Ctrl+C : deux appuis");
        same(c ? km::label(*c) : "", "Ctrl+K, Ctrl+C", "l'ecriture d'un accord");
        const auto d = km::parse("ctrl+shift+l");
        same(d ? km::label(*d) : "", "Ctrl+Maj+L", "ctrl+shift+l se lit Ctrl+Maj+L");
        const auto e = km::parse("Alt+Haut");
        same(e ? km::label(*e) : "", "Alt+\xE2\x86\x91", "Alt+Haut : la fleche");
        const auto f = km::parse("Ctrl+,");
        check(f && f->first.key == Key::Comma && !f->twoStrokes(), "Ctrl+, : la virgule est la touche");
        const auto g = km::parse("Ctrl+K Ctrl+K");
        check(g && g->twoStrokes() && g->second.key == Key::K, "Ctrl+K Ctrl+K : deux appuis, separes par une espace");
        check(!km::parse("Ctrl+Truc"), "une touche inconnue ne se lit pas");
        // Chaque liaison du profil : son ecriture se relit a l'identique.
        bool roundTrip = true;
        for (const auto& b : km::bindings(km::Profile::VisualStudio)) {
            const auto again = km::parse(km::label(b.chord));
            roundTrip = roundTrip && again && *again == b.chord && km::command(b.command) != nullptr;
        }
        check(roundTrip, "le profil Visual Studio : chaque accord se relit, chaque commande existe");
        check(km::bindings(km::Profile::VisualStudio).size() >= 55, "le profil Visual Studio : pres de soixante raccourcis");
        // Le profil classique : pas d'accords ; F12, F8, Ctrl+W gardent leur sens d'avant.
        bool noChord = true;
        for (const auto& b : km::bindings(km::Profile::Classic)) noChord = noChord && !b.chord.twoStrokes();
        check(noChord, "le profil classique n'a pas d'accords");
        const auto stroke = [](std::string_view s) { return km::parse(s)->first; };
        check(km::single(km::Profile::Classic, stroke("F12")).empty() && km::single(km::Profile::Classic, stroke("F8")).empty()
                  && km::single(km::Profile::Classic, stroke("Ctrl+W")).empty() && !km::startsChord(km::Profile::Classic, stroke("Ctrl+K")),
              "classique : F12 (capture), F8 (simulation), Ctrl+W (fermer), Ctrl+K (Aller a) restent a l'appli");
        check(km::single(km::Profile::Classic, stroke("Ctrl+D")) == "dupliquer", "classique : Ctrl+D duplique quand meme");
        check(km::single(km::Profile::VisualStudio, stroke("F12")) == "allerDefinition" && km::startsChord(km::Profile::VisualStudio, stroke("Ctrl+K")),
              "Visual Studio : F12 la definition, Ctrl+K un accord");
        check(km::profileFromKey("vs") == km::Profile::VisualStudio && km::profileFromKey("classique") == km::Profile::Classic
                  && !km::profileFromKey("emacs"),
              "les cles des reglages");
    }
    // ---- 2. l'attente de la seconde touche -------------------------------------------
    {
        km::Resolver r;
        const auto p = km::Profile::VisualStudio;
        auto a = r.feed(p, KeyDown{ Key::K, ctrl(), false });
        check(a.outcome == km::Resolver::Outcome::Pending && a.chord == "Ctrl+K" && r.pending(), "Ctrl+K : l'attente");
        auto rep = r.feed(p, KeyDown{ Key::K, ctrl(), true });
        check(rep.outcome == km::Resolver::Outcome::Pending, "Ctrl+K tenu (repetition) : toujours l'attente");
        auto none = r.feed(p, KeyDown{ Key::Unknown, ctrl(), false });
        check(none.outcome == km::Resolver::Outcome::None && r.pending(), "Ctrl seul : l'attente continue");
        auto b = r.feed(p, KeyDown{ Key::C, ctrl(), false });
        check(b.outcome == km::Resolver::Outcome::Command && b.command == "commenter" && !r.pending(), "Ctrl+K, Ctrl+C : commenter");
        (void)r.feed(p, KeyDown{ Key::K, ctrl(), false });
        auto u = r.feed(p, KeyDown{ Key::Q, ctrl(), false });
        check(u.outcome == km::Resolver::Outcome::Unknown && u.chord == "Ctrl+K, Ctrl+Q", "Ctrl+K, Ctrl+Q : inconnu");
        same(km::unknownMessage(u.chord), "La combinaison de touches (Ctrl+K, Ctrl+Q) n'est pas une commande.", "la phrase de Visual Studio");
        same(km::pendingMessage("Ctrl+K"), "(Ctrl+K) a \xC3\xA9t\xC3\xA9 appuy\xC3\xA9. Attente de la seconde touche de l'accord\xE2\x80\xA6",
             "la phrase de l'attente");
        auto s = r.feed(p, KeyDown{ Key::D, ctrl(), false });
        check(s.outcome == km::Resolver::Outcome::Command && s.command == "dupliquer", "Ctrl+D : une touche seule");
    }

    // ---- 3. les commandes sur un editeur ----------------------------------------------
    std::vector<std::string> notices;
    MultiLineText::setNoticeSink([&](const std::string& t, Tone) { notices.push_back(t); });
    {
        MultiLineText v("c1");
        editor(v, "IF a THEN\n    b := 1;\nEND_IF");
        v.goToLine(1);
        press(v, Key::K, ctrl());
        check(v.chordPending() && !notices.empty() && notices.back().find("(Ctrl+K)") == 0, "Ctrl+K : l'editeur attend et le dit");
        press(v, Key::C, ctrl());
        same(v.text(), "IF a THEN\n    // b := 1;\nEND_IF", "Ctrl+K, Ctrl+C commente la ligne a son retrait");
        press(v, Key::K, ctrl());
        press(v, Key::U, ctrl());
        same(v.text(), "IF a THEN\n    b := 1;\nEND_IF", "Ctrl+K, Ctrl+U la decommente");
        press(v, Key::Slash, ctrl());
        same(v.text(), "IF a THEN\n    // b := 1;\nEND_IF", "Ctrl+/ bascule");
        press(v, Key::Colon, ctrl());
        same(v.text(), "IF a THEN\n    b := 1;\nEND_IF", "Ctrl+: (AZERTY) bascule aussi");
        // La seconde touche sans Ctrl : un accord inconnu, sa lettre ne s'ecrit pas.
        press(v, Key::K, ctrl());
        typeKey(v, Key::C, 'c');
        same(v.text(), "IF a THEN\n    b := 1;\nEND_IF", "Ctrl+K puis C : rien ne s'ecrit");
        check(v.lastNotice().find("n'est pas une commande") != std::string::npos, "Ctrl+K puis C : la combinaison inconnue est dite");
        // Une lettre ordinaire s'ecrit toujours.
        press(v, Key::End);
        typeKey(v, Key::X, 'x');
        same(v.text(), "IF a THEN\n    b := 1;x\nEND_IF", "une lettre tapee apres reste ecrite");
    }
    {
        MultiLineText v("c2");
        editor(v, "un\ndeux\ntrois");
        v.goToLine(1);
        press(v, Key::D, ctrl());
        same(v.text(), "un\ndeux\ndeux\ntrois", "Ctrl+D duplique la ligne");
        check(v.caretLine() == 2, "Ctrl+D : le curseur sur la copie");
        press(v, Key::Down, alt());
        same(v.text(), "un\ndeux\ntrois\ndeux", "Alt+Bas descend la ligne");
        press(v, Key::Up, alt());
        press(v, Key::Up, alt());
        same(v.text(), "un\ndeux\ndeux\ntrois", "Alt+Haut la remonte");
        v.goToLine(0);
        press(v, Key::L, ctrl());
        same(v.text(), "deux\ndeux\ntrois", "Ctrl+L coupe la ligne");
        same(gClipboard, "un\n", "Ctrl+L : la ligne et sa fin dans le presse-papiers");
        v.goToLine(2);
        press(v, Key::V, ctrl());
        same(v.text(), "deux\ndeux\nun\ntrois", "Ctrl+V d'une ligne entiere : collee au-dessus de la ligne du curseur");
        v.goToLine(3);
        press(v, Key::C, ctrl());
        same(gClipboard, "trois\n", "Ctrl+C sans selection : la ligne (et non tout le texte)");
        press(v, Key::L, ctrlShift());
        same(v.text(), "deux\ndeux\nun", "Ctrl+Maj+L supprime la ligne");
        press(v, Key::Return, ctrl());
        same(v.text(), "deux\ndeux\n\nun", "Ctrl+Entree ouvre une ligne au-dessus");
    }
    {
        MultiLineText v("c3");
        editor(v, "a := 1;\nb := 2;\nc := 3;");
        press(v, Key::A, ctrl());
        press(v, Key::Tab);
        same(v.text(), "    a := 1;\n    b := 2;\n    c := 3;", "Tab sur plusieurs lignes les indente (la selection n'est plus remplacee)");
        press(v, Key::Tab, shift());
        same(v.text(), "a := 1;\nb := 2;\nc := 3;", "Maj+Tab les desindente");
        v.goToLine(1);
        press(v, Key::U, ctrlShift());
        same(v.text(), "a := 1;\nB := 2;\nc := 3;", "Ctrl+Maj+U sans selection : le mot en MAJUSCULES");
        press(v, Key::Home);
        press(v, Key::Right, ctrl());
        press(v, Key::Backspace, ctrl());
        same(v.text(), "a := 1;\n:= 2;\nc := 3;", "Ctrl+Retour efface le mot d'avant (Ctrl+fleche saute un mot)");
    }
    {
        MultiLineText v("c4");
        editor(v, "IF a THEN\nFOR i := 0 TO 3 DO\nx := i;\nEND_FOR\nELSE\ny := 2;\nEND_IF");
        press(v, Key::K, ctrl());
        press(v, Key::D, ctrl());
        same(v.text(), "IF a THEN\n    FOR i := 0 TO 3 DO\n        x := i;\n    END_FOR\nELSE\n    y := 2;\nEND_IF",
             "Ctrl+K, Ctrl+D met en forme : la profondeur des blocs, ELSE et END_x un cran a gauche");
        v.goToLine(0);
        press(v, Key::RightBracket, ctrl());
        check(v.caretLine() == 6, "Ctrl+] : de IF a son END_IF");
        press(v, Key::RightBracket, ctrl());
        check(v.caretLine() == 0, "Ctrl+] : et retour");
        v.goToLine(1);
        press(v, Key::Dollar, ctrl());
        check(v.caretLine() == 3, "Ctrl+$ (AZERTY) : de FOR a END_FOR");
        press(v, Key::Minus, ctrl());
        check(v.caretLine() == 1, "Ctrl+- revient ou l'on etait");
        // Entourer de (Ctrl+K, Ctrl+S) : la liste, puis Entree sur le premier (IF).
        v.goToLine(5);
        press(v, Key::K, ctrl());
        press(v, Key::S, ctrl());
        check(v.completionOpen(), "Ctrl+K, Ctrl+S : la liste des blocs");
        press(v, Key::Return);
        same(v.text(), "IF a THEN\n    FOR i := 0 TO 3 DO\n        x := i;\n    END_FOR\nELSE\n    IF condition THEN\n        y := 2;\n    END_IF\nEND_IF",
             "Entourer de IF : la ligne dedans, un cran de plus");
        same(v.selectedText(), "condition", "Entourer : le mot a remplacer est choisi");
    }
    {
        MultiLineText v("c5");
        editor(v, "Ecart := 1;\nEcartMax := Ecart;\nIF Ecart > 2 THEN\nEND_IF");
        press(v, Key::F, ctrl());
        check(v.barMode() == MultiLineText::BarMode::Find && v.barHasKeyboard(), "Ctrl+F : la barre de recherche, avec le clavier");
        type(v, "Ecart");
        check(v.matchCount() == 4, "4 trouvailles de Ecart (EcartMax compris)");
        v.setFindOptions(false, true);
        check(v.matchCount() == 3, "mot entier : 3");
        press(v, Key::Return);
        press(v, Key::H, ctrl());
        check(v.barMode() == MultiLineText::BarMode::Replace, "Ctrl+H : remplacer");
        press(v, Key::Tab);
        type(v, "Ecart_Mesure");
        press(v, Key::A, alt());
        same(v.text(), "Ecart_Mesure := 1;\nEcartMax := Ecart_Mesure;\nIF Ecart_Mesure > 2 THEN\nEND_IF", "Alt+A : tout remplacer (mot entier)");
        press(v, Key::Escape);
        check(v.barMode() == MultiLineText::BarMode::None, "Echap ferme la barre");
        press(v, Key::G, ctrl());
        check(v.barMode() == MultiLineText::BarMode::GoToLine, "Ctrl+G : aller a la ligne");
        press(v, Key::A, ctrl());
        type(v, "3");
        press(v, Key::Return);
        check(v.caretLine() == 2 && v.barMode() == MultiLineText::BarMode::None, "Ctrl+G, 3, Entree : la ligne 3");
    }
    {
        MultiLineText v("c6");
        editor(v, "a := 1;\nb := Fours.Etat;\nc := 3;\nd := 4;");
        MultiLineText::Squiggle s;
        s.line = 1;
        s.column = 5;
        s.length = 10;
        s.message = "Fours.Etat : membre inconnu : veux-tu dire Fours.EtatMoteur ?";
        s.fix = "Fours.EtatMoteur";
        v.setSquiggles({ s });
        v.goToLine(0);
        press(v, Key::F8);
        check(v.caretLine() == 1 && v.selectedText() == "Fours.Etat", "F8 : la faute suivante, choisie");
        press(v, Key::Period, ctrl());
        same(v.text(), "a := 1;\nb := Fours.EtatMoteur;\nc := 3;\nd := 4;", "Ctrl+. : le nom propose remplace le nom souligne");
        check(v.squiggles().empty(), "Ctrl+. : la faute corrigee ne se montre plus");
        // Les signets.
        v.goToLine(3);
        press(v, Key::K, ctrl());
        press(v, Key::K, ctrl());
        v.goToLine(0);
        press(v, Key::K, ctrl());
        press(v, Key::N, ctrl());
        check(v.caretLine() == 3 && v.bookmarks() == std::vector<std::size_t>{ 3 }, "Ctrl+K, Ctrl+K pose un signet ; Ctrl+K, Ctrl+N y va");
        v.goToLine(0);
        press(v, Key::Return, ctrlShift());     // une ligne de plus au-dessus du signet
        check(v.bookmarks() == std::vector<std::size_t>{ 4 }, "le signet suit sa ligne quand on en insere une au-dessus");
    }
    // ---- les commandes de l'ecran ----
    {
        MultiLineText v("c7");
        editor(v, "x := Pression;");
        std::vector<std::string> asked;
        bool answer = true;
        MultiLineText::setHostCommandHandler([&](MultiLineText&, std::string_view c) { asked.emplace_back(c); return answer; });
        v.goToLine(0);
        press(v, Key::End);
        press(v, Key::Left);
        check(v.claimsKey(KeyDown{ Key::F12, {}, false }) && v.claimsKey(KeyDown{ Key::K, ctrl(), false }), "l'editeur garde F12 et Ctrl+K");
        check(!v.claimsKey(KeyDown{ Key::S, ctrl(), false }), "Ctrl+S reste a l'ecran (enregistrer)");
        check(press(v, Key::F12) == EventResult::Consumed && !asked.empty() && asked.back() == "allerDefinition", "F12 : l'ecran va a la definition");
        press(v, Key::T, ctrl());
        check(asked.back() == "allerA", "Ctrl+T : Aller a");
        answer = false;
        check(press(v, Key::B, ctrlShift()) == EventResult::Ignored, "une commande que personne ne fait : la touche suit son chemin");
        check(press(v, Key::F7, ctrl()) == EventResult::Ignored, "Ctrl+F7 : le volet le prend (compiler)");
        MultiLineText::setHostCommandHandler({});
        km::setCurrent(km::Profile::Classic);
        check(!v.claimsKey(KeyDown{ Key::K, ctrl(), false }) && !v.claimsKey(KeyDown{ Key::F12, {}, false }),
              "profil classique : Ctrl+K et F12 restent a l'appli");
        km::setCurrent(km::Profile::VisualStudio);
        MultiLineText plain("plain");
        plain.setText("a");
        plain.setReadOnly(false);
        paint(plain);
        plain.takeFocus();
        check(!plain.claimsKey(KeyDown{ Key::K, ctrl(), false }), "sans setCommandKeys : rien ne change");
    }

    // ---- 4. une commande = un pas ; la vue ne saute pas ----------------------------------
    {
        core::CommandStack stack;
        core::CommandGroupScope::registerStack(&stack);
        MultiLineText v("c8");
        editor(v, "ligne");
        auto link = v.textChanged->connect([&](const std::string&) { (void)stack.push(std::make_unique<TextEdit>()); });
        press(v, Key::End);
        type(v, "ab");
        check(stack.done().size() == 1, "deux lettres : un seul pas (la rafale)");
        press(v, Key::D, ctrl());
        check(stack.done().size() == 2, "Ctrl+D : son propre pas");
        type(v, "c");
        check(stack.done().size() == 3, "la frappe d'apres : un nouveau pas, pas fondu dans Ctrl+D");
        core::CommandGroupScope::registerStack(nullptr);
    }
    {
        MultiLineText v("c9");
        std::string text;
        for (int i = 0; i < 120; ++i) text += "ligne " + std::to_string(i) + "\n";
        editor(v, text);
        v.goToLine(80);
        paint(v);
        const auto first = v.firstVisibleLine();
        type(v, "x");
        paint(v);
        check(v.firstVisibleLine() == first, "taper ne fait plus sauter la vue (avant : le curseur filait en bas de l'editeur)");
        std::string undone = v.text();
        undone.erase(undone.find("xligne 80"), 1);
        v.reloadText(undone);
        check(v.firstVisibleLine() == first && v.caretLine() == 80, "reloadText (annuler) : la vue reste, le curseur au changement");
    }
    MultiLineText::setNoticeSink({});

    // ---- 5. la page d'aide contre la table -----------------------------------------
    {
        bool allKnown = true;
        std::string missing;
        // Les touches des editeurs qui ne sont pas des commandes du profil : le comportement de base.
        const std::vector<std::string> base = { "F7", "Ctrl+Espace", "Tab", "Entr\xC3\xA9" "e", "\xE2\x86\x91", "\xE2\x86\x93", "Ctrl+Z", "Maj+Tab",
                                                "Ctrl+\xE2\x86\x90", "Ctrl+\xE2\x86\x92" };
        for (const auto* s : help::keys::ofContext(help::keys::Context::Scripts)) {
            for (const auto& alt : help::keys::alternatives(s->keys)) {
                if (std::find(base.begin(), base.end(), alt) != base.end()) continue;
                const auto chord = km::parse(alt);
                bool bound = false;
                if (chord)
                    for (const auto& b : km::bindings(km::Profile::VisualStudio)) bound = bound || b.chord == *chord;
                if (!bound) { allKnown = false; missing += " [" + alt + "]"; }
            }
        }
        check(allKnown, "la page d'aide des editeurs : chaque touche est dans le profil Visual Studio -" + missing);
        check(help::keys::keyCaps("Ctrl+K, Ctrl+C") == std::vector<std::string>{ "Ctrl", "K", ",", "Ctrl", "C" },
              "la page dessine un accord : ses deux appuis, une virgule entre");
    }

    if (gFailures) {
        std::printf("keymap_test : %d echec(s)\n", gFailures);
        return 1;
    }
    std::printf("keymap_test : tout est bon\n");
    return 0;
}
