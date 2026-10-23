// tests/crashreport_test.cpp - 1.12.28 : la fenetre du plantage et du blocage, dessinee par l'appli.
//
//   crashreport_test
//
//  Sans ecran :
//    1. le resume d'un rapport (core/CrashGuard : son format) - la raison en clair, ce que
//       l'appli faisait (les portees du fil principal), ses dernieres actions, les documents ;
//       la raison d'un plantage Windows (le nom technique, puis l'explication) ;
//    2. pendant un blocage : les boutons du moment, a leur taille (rien de coupe, rien ne se
//       chevauche), le compteur qui avance, Copier qui le dit, la reprise qui ferme la fenetre ;
//       une fenetre etroite : les outils passent au-dessus de la decision ;
//    3. apres un plantage : Relancer en principal ; au lancement suivant : le dialogue de l'appli
//       dans la pile (MenuManager), Echap le ferme.
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/Widget.hpp"
#include "../src/ui/widgets/CrashReport.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0, checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// Un renderer qui note le texte dessine (8 px par octet, 16 px de haut).
class FakeRenderer final : public gfx::IRenderer {
public:
    explicit FakeRenderer(gfx::Size s) : size_(s) {}
    void beginFrame(gfx::Color) override { texts.clear(); }
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point p, std::string_view s, gfx::FontId, gfx::Color) override {
        texts.push_back({std::string(s), {p.x, p.y, 8.f * static_cast<float>(s.size()), 16.f}});
    }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId) const override {
        return {8.f * static_cast<float>(s.size()), 16.f, 12.f, 4.f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId) const override { return 16.f; }
    [[nodiscard]] gfx::Size surfaceSize() const override { return size_; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId, float maxWidth) const override {
        return std::min(s.size(), static_cast<std::size_t>(std::max(0.f, maxWidth) / 8.f));
    }
    struct Text { std::string s; gfx::Rect at; };
    std::vector<Text> texts;
    [[nodiscard]] bool shows(std::string_view part) const {
        for (const auto& t : texts) if (t.s.find(part) != std::string::npos) return true;
        return false;
    }
private:
    gfx::Size size_;
};

// Un rapport de blocage, au format de core/CrashGuard (raccourci).
const char* kHang =
    "\xEF\xBB\xBFIHM Studio 1.12.28 - rapport de blocage\n"
    "=================================================\n"
    "Genre       : blocage\n"
    "Version     : IHM Studio 1.12.28\n"
    "Syst\xC3\xA8me     : Windows 10.0 (build 22631), 64 bits\n"
    "Heure       : 2026-10-23 10:57:52 (heure locale)\n"
    "Raison      : blocage : la boucle principale ne bat plus depuis 8,0 s\n"
    "Fil         : #1 principal\n"
    "Historique  : 1520 entr\xC3\xA9" "es \xC3\xA9" "crites depuis le lancement\n"
    "\n"
    "Pile d'appels du fil principal\n"
    "------------------------------\n"
    "   0  IHM-Studio.exe+0x32CF1\n"
    "\n"
    "Port\xC3\xA9" "es ouvertes (la pile des appels suivis, par fil)\n"
    "-----------------------------------------------------\n"
    "Fil #1 principal : 2 port\xC3\xA9" "es ouvertes\n"
    "   App::frame   depuis 9,214 s\n"
    "     HmiEditor::rebuild   depuis 9,201 s\n"
    "Fil #6 chien de garde : 0 port\xC3\xA9" "es ouvertes\n"
    "\n"
    "Documents ouverts\n"
    "-----------------\n"
    "Armoire_Gaz.xpgproj\n"
    "\n"
    "Les 2 000 derni\xC3\xA8res entr\xC3\xA9" "es de l'historique interne\n"
    "---------------------------------------------------\n"
    "10:57:31.100  #1 principal    menu            Projet > Ouvrir\n"
    "10:57:35.200  #1 principal    action          IHM > Compiler\n"
    "10:57:36.000  #3 sim          simulation      cycle 120\n"
    "10:57:40.300  #1 principal    fen\xC3\xAAtre         Simulation\n"
    "10:57:43.800  #6 chien de garde chien de garde blocage : la boucle principale ne bat plus depuis 8,0 s\n"
    "\n"
    "(fin du rapport)\n";

const char* kCrashReason =
    "Genre       : plantage\n"
    "Raison      : EXCEPTION_ACCESS_VIOLATION (acc\xC3\xA8s m\xC3\xA9moire invalide), code 0xC0000005, adresse 0x7FF6 (IHM-Studio.exe+0x1234), lecture de 0x0\n";

// Les boutons de la vue : chacun a la taille de son texte, dans le panneau, sans chevauchement.
void checkButtons(const ui::CrashReportView& v, const std::string& label) {
    const auto panel = v.panel();
    bool fit = true, inside = true, apart = true;
    const auto& bs = v.buttons();
    for (std::size_t i = 0; i < bs.size(); ++i) {
        const auto r = bs[i]->bounds();
        if (r.w + 0.5f < 8.f * static_cast<float>(bs[i]->text().size()) + 32.f) fit = false;
        if (r.x < panel.x || r.right() > panel.right() + 0.5f || r.y < panel.y || r.bottom() > panel.bottom() + 0.5f) inside = false;
        for (std::size_t j = i + 1; j < bs.size(); ++j) {
            const auto o = bs[j]->bounds();
            if (r.x < o.right() && o.x < r.right() && r.y < o.bottom() && o.y < r.bottom()) apart = false;
        }
    }
    check(fit, label + " : chaque bouton a la taille de son texte (rien de coupe)");
    check(inside, label + " : les boutons sont dans le panneau");
    check(apart, label + " : les boutons ne se chevauchent pas");
}

} // namespace

int main() {
    ui::PlatformServices services;
    services.measureWidth = [](std::string_view s, gfx::FontId) { return 8.f * static_cast<float>(s.size()); };
    services.lineHeight = [](gfx::FontId) { return 16.f; };
    std::string clipboard;
    services.clipboardSet = [&clipboard](std::string_view s) { clipboard = std::string(s); };
    services.clipboardGet = [&clipboard] { return clipboard; };
    ui::installPlatformServices(std::move(services));
    const ui::Theme theme = ui::Theme::byName("Light");

    std::printf("1. le resume d'un rapport\n");
    {
        const auto s = ui::crashreport::summarize(kHang);
        check(s.kind == "blocage", "le genre : blocage");
        check(s.reason == "La boucle principale ne bat plus depuis 8,0 s", "la raison en clair (sans \xC2\xAB blocage : \xC2\xBB) : " + s.reason);
        check(s.doing.size() == 2 && s.doing[0] == "App::frame  (depuis 9,214 s)" && s.doing[1] == "HmiEditor::rebuild  (depuis 9,201 s)",
              "ce que l'appli faisait : les deux port\xC3\xA9" "es du fil principal, pas celles du chien de garde");
        check(s.recent.size() == 3 && s.recent[0] == "10:57:40  Simulation" && s.recent[1] == "10:57:35  IHM > Compiler"
              && s.recent[2] == "10:57:31  Projet > Ouvrir",
              "ses derni\xC3\xA8res actions, la plus r\xC3\xA9" "cente d'abord (ni la simulation d'un autre fil, ni le chien de garde)");
        check(s.documents.size() == 1 && s.documents[0] == "Armoire_Gaz.xpgproj", "les documents ouverts");
        check(s.version == "IHM Studio 1.12.28" && s.when.rfind("2026-10-23 10:57:52", 0) == 0, "la version et l'heure");
        check(std::fabs(ui::crashreport::stalledSeconds(s.reason) - 8.0) < 1e-9, "l'arr\xC3\xAAt lu dans la raison : 8,0 s");
        const auto c = ui::crashreport::summarize(kCrashReason);
        check(c.reason == "Acc\xC3\xA8s m\xC3\xA9moire invalide", "un plantage Windows : l'explication en clair (" + c.reason + ")");
        check(c.detail.rfind("EXCEPTION_ACCESS_VIOLATION", 0) == 0, "et le technique a part (le code, l'adresse)");
        const auto x = ui::crashreport::summarize("Raison      : exception C++ non attrap\xC3\xA9" "e : vector::_M_range_check\n");
        check(x.reason == "Une exception C++ non attrap\xC3\xA9" "e" && x.detail == "vector::_M_range_check", "une exception C++ : sa phrase et son message");
    }

    std::printf("2. pendant un blocage\n");
    {
        bool waited = false, killed = false, closed = false;
        ui::CrashReportSetup setup;
        setup.mode = ui::crashreport::Mode::Hang;
        setup.product = "IHM Studio";
        setup.path = "C:\\Users\\Q\\AppData\\Roaming\\XpgAnalyzer\\IHM\\crashs\\blocage-2026-10-23_10-57-52.txt";
        setup.text = kHang;
        setup.openFolder = [] { return std::string("Le dossier des rapports s'ouvre."); };
        setup.copyReport = [&clipboard] { clipboard = "rapport"; return std::string("Le rapport est copi\xC3\xA9 dans le presse-papiers."); };
        setup.signal = [] { return std::string("Le signalement est pr\xC3\xAAt."); };
        setup.wait = [&waited] { waited = true; };
        setup.closeApp = [&killed] { killed = true; };
        setup.close = [&closed] { closed = true; };
        ui::CrashReportView view(setup, false);
        view.setBounds({0, 0, 760, 520});
        view.layout();
        check(view.headline() == "IHM Studio ne r\xC3\xA9pond plus", "le titre : " + view.headline());
        check(view.button("rapport.attendre") && view.button("rapport.fermer-appli") && view.button("rapport.dossier")
              && view.button("rapport.copier") && view.button("rapport.signaler"),
              "les boutons : Attendre, Fermer IHM Studio, Ouvrir le dossier, Copier le rapport, Signaler le probl\xC3\xA8me");
        checkButtons(view, "760 px");
        const auto* wait = view.button("rapport.attendre");
        const auto* kill = view.button("rapport.fermer-appli");
        check(wait && kill && wait->bounds().x > kill->bounds().x, "Attendre (le principal) est le dernier, \xC3\xA0 droite");
        view.tick(100.0);
        check(view.subline().rfind("Depuis 8 s", 0) == 0, "le compteur part de l'arr\xC3\xAAt du rapport : " + view.subline());
        view.tick(105.4);
        check(view.subline().rfind("Depuis 13 s", 0) == 0, "puis il avance : " + view.subline());
        FakeRenderer r({760, 520});
        const ui::PaintContext ctx{r, theme, {0, 0, 760, 520}, 105.4, nullptr};
        r.beginFrame({});
        view.render(ctx);
        check(r.shows("IHM Studio ne r\xC3\xA9pond plus") && r.shows("CE QUI S'EST PASS\xC3\x89") && r.shows("La boucle principale ne bat plus")
              && r.shows("HmiEditor::rebuild") && r.shows("blocage-2026-10-23_10-57-52.txt"),
              "dessin\xC3\xA9 : le titre, ce qui s'est pass\xC3\xA9, ce que l'appli faisait (la port\xC3\xA9" "e la plus fine), le rapport");
        bool within = true;
        for (const auto& t : r.texts) if (t.at.right() > 760.f + 0.5f) within = false;
        check(within, "aucun texte ne d\xC3\xA9" "borde de la fen\xC3\xAAtre (tout est repli\xC3\xA9)");
        view.button("rapport.copier")->clicked->emit();
        check(clipboard == "rapport" && view.note().find("copi\xC3\xA9") != std::string::npos, "Copier le rapport : le presse-papiers, et la note le dit");
        view.button("rapport.fermer-appli")->clicked->emit();
        check(killed, "Fermer IHM Studio : l'appli bloqu\xC3\xA9" "e est arr\xC3\xAAt\xC3\xA9" "e");
        (void)view.dispatch(ui::KeyDown{ui::Key::Escape, {}, false});
        check(waited && !closed, "\xC3\x89" "chap : attendre (l'appli continue)");
        view.setRecovered(14.0);
        check(view.headline() == "IHM Studio est reparti" && !view.button("rapport.attendre")->enabled(),
              "la boucle repart : la fen\xC3\xAAtre le dit, Attendre et Fermer s'\xC3\xA9teignent");
        view.tick(106.0);
        check(!closed, "elle reste un instant");
        view.tick(108.2);
        check(closed, "puis se ferme toute seule");
        // Etroite : les outils au-dessus de la decision.
        ui::CrashReportView narrow(setup, false);
        narrow.setBounds({0, 0, 560, 440});
        narrow.layout();
        checkButtons(narrow, "560 px");
        check(narrow.button("rapport.dossier")->bounds().y < narrow.button("rapport.attendre")->bounds().y,
              "560 px : les outils passent au-dessus de la d\xC3\xA9" "cision");
    }

    std::printf("3. apres un plantage, au lancement suivant\n");
    {
        bool restarted = false;
        ui::CrashReportSetup setup;
        setup.mode = ui::crashreport::Mode::Crash;
        setup.text = kCrashReason;
        setup.when = "23/10/2026 \xC3\xA0 11:02";
        setup.restart = [&restarted] { restarted = true; };
        setup.close = [] {};
        ui::CrashReportView view(setup, false);
        view.setBounds({0, 0, 760, 520});
        view.layout();
        check(view.headline() == "IHM Studio s'est arr\xC3\xAAt\xC3\xA9" && view.subline().find("le 23/10/2026") != std::string::npos,
              "le titre et la date : " + view.subline());
        (void)view.dispatch(ui::KeyDown{ui::Key::Return, {}, false});
        check(restarted, "Entr\xC3\xA9" "e : Relancer IHM Studio (le principal)");

        menu::MenuManager menus{menu::MenuFactory{}};
        ui::CrashReportSetup pending;
        pending.mode = ui::crashreport::Mode::Pending;
        pending.text = kHang;
        pending.when = "23/10/2026 \xC3\xA0 10:57";
        pending.copyReport = [] { return std::string("copi\xC3\xA9"); };
        bool closedByUser = false;
        pending.close = [&closedByUser] { closedByUser = true; };
        menus.ShowDialog(std::make_unique<ui::CrashReportDialog>(pending), {});
        menus.applyPending();
        auto* dialog = dynamic_cast<ui::CrashReportDialog*>(menus.top());
        check(dialog && dialog->view(), "le dialogue de l'appli est dans la pile (plus de bo\xC3\xAEte native)");
        if (dialog && dialog->view()) {
            const menu::FrameContext fc{1.0 / 60, 1.0, &theme, {1600, 900}, 1.f};
            menus.Update(fc);
            check(dialog->view()->headline() == "Un blocage s'est produit", "au lancement suivant : \xC2\xAB Un blocage s'est produit \xC2\xBB");
            check(dialog->view()->subline().rfind("Le 23/10/2026", 0) == 0, "et quand : " + dialog->view()->subline());
            check(dialog->view()->panel().w <= 760.f && dialog->view()->panel().w >= 560.f, "un dialogue centr\xC3\xA9, \xC3\xA0 sa taille");
            checkButtons(*dialog->view(), "dialogue");
            (void)menus.HandleEvent(ui::KeyDown{ui::Key::Escape, {}, false});
            menus.applyPending();
            check(closedByUser && dynamic_cast<ui::CrashReportDialog*>(menus.top()) == nullptr, "\xC3\x89" "chap le ferme");
        }
    }

    std::printf("crashreport_test : %d v\xC3\xA9rifications, %d \xC3\xA9" "checs\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
