// =============================================================================
//  tests/topbar8_test.cpp - lot API 8 : le bandeau haut, refondu
// -----------------------------------------------------------------------------
//  1. Trois zones sur 44 px : le projet a gauche, la palette au centre, la
//     simulation et les sorties a droite ; Historique a quitte le bandeau (son
//     action reste joignable, par son libelle aussi).
//  2. Les 10 dernieres actions : la liste d'Annuler, "edit.undoTo:n".
//  3. La cloche : non lues, Tout marquer comme lu ; les taches de fond ; la
//     mini-courbe (40 echantillons au plus).
//  4. Le registre des taches et des avis (bgtasks).
//  5. 1.11 (R111, recette T1-1) : le nom du projet coupe (bandeau serre) finit
//     par « … », et l'infobulle le donne entier.
// =============================================================================
#ifdef NDEBUG
#  undef NDEBUG
#endif

#include "../src/app/BackgroundTasks.hpp"
#include "../src/app/TopBar.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

void testZones() {
    std::printf("1. Trois zones sur 44 px\n");
    app::TopBar bar("barre");
    check(bar.sizeHint().preferred.h == 44.f, "44 px de haut");
    bar.setProject("Armoire_Gaz", "DEV", true);
    bar.setBounds(gfx::Rect{0.f, 0.f, 1600.f, 44.f});
    const auto projet = bar.partRect("projet"), save = bar.partRect("enregistrer"), undo = bar.partRect("annuler");
    const auto palette = bar.partRect("aller"), sim = bar.partRect("simulation"), bell = bar.partRect("cloche");
    const auto ce = bar.partRect("control-expert"), aide = bar.partRect("aide");
    check(!projet.empty() && !save.empty() && save.x >= projet.right() - 0.5f, "Enregistrer colle a la puce projet");
    check(!undo.empty() && undo.x > save.x, "Annuler apres le projet");
    check(!palette.empty() && palette.x > undo.x && palette.right() < sim.x, "la palette au centre, avant la simulation");
    check(!bell.empty() && bell.x > sim.right() - 0.5f && ce.x > bell.x && aide.right() <= 1600.f, "a droite : simulation, cloche, sortie, Aide");
    check(bar.partRect("etat").w > 0.f && bar.partRect("etat").x >= sim.x, "l'etat dans le bloc simulation");
    // Historique : hors du bandeau, joignable
    check(bar.hiddenPartAction("historique") == "edit.history", "Historique : hors du bandeau, son action reste");
    check(bar.actionForLabel("Historique") == "edit.history" && bar.reaches("edit.history"), "Historique par son libelle");
    check(bar.actionForLabel("Enregistrer") == "project.save" && bar.actionForLabel("Fermer le projet") == "file.close",
          "Enregistrer (integre), Fermer le projet (menu)");
    check(bar.actionForLabel("Simuler") == "sim.run" && bar.actionForLabel("Vers Control Expert") == "project.exportSources",
          "les libelles d'avant gardent leur sens");
    // etroit : tout tient
    bar.setBounds(gfx::Rect{0.f, 0.f, 1100.f, 44.f});
    check(bar.partRect("aide").right() <= 1100.f && bar.partRect("aller").w >= 120.f, "1100 px : tout tient, la palette se resserre");
}

void testUndoList() {
    std::printf("2. Annuler : les 10 dernieres actions\n");
    app::TopBar bar("barre");
    std::vector<app::TopBar::UndoItem> items;
    for (int i = 0; i < 14; ++i) items.push_back({"Action " + std::to_string(i), "il y a 1 min"});
    bar.setUndoList(items);
    check(bar.undoList().size() == 10, "10 au plus");
    std::vector<std::string> fired;
    bar.setActionSink([&](core::ActionId id) { fired.emplace_back(id); });
    ui::PopupMenu popup("barre.menu");
    bar.setPopup(&popup);
    bar.openMenu(app::TopBar::Menu::UndoList);
    check(bar.openedMenu() == app::TopBar::Menu::UndoList && popup.isOpen(), "la liste s'ouvre");
    popup.itemChosen->emit(2);
    check(fired.size() == 1 && fired[0] == "edit.undoTo:3", "la 3e : annule les 3 dernieres (" + (fired.empty() ? std::string("rien") : fired[0]) + ")");
    fired.clear();
    bar.openMenu(app::TopBar::Menu::UndoList);
    popup.itemChosen->emit(10);
    check(fired.size() == 1 && fired[0] == "edit.history", "la derniere ligne : l'historique du projet");
}

void testBellTasks() {
    std::printf("3. La cloche, les taches, la mini-courbe\n");
    app::TopBar bar("barre");
    bar.setBounds(gfx::Rect{0.f, 0.f, 1600.f, 44.f});
    app::TopBar::Notice a;
    a.group = "Projet";
    a.title = "2 expressions impossibles (Compiler)";
    a.button = "Voir la liste";
    a.action = "hmi.compile";
    app::TopBar::Notice b = a;
    b.group = "Simulation";
    b.title = "2 valeurs forc\xC3\xA9" "es";
    bar.setNotices({a, b});
    check(bar.unreadNotices() == 2, "2 non lues");
    std::vector<std::string> fired;
    bar.setActionSink([&](core::ActionId id) { fired.emplace_back(id); });
    bar.trigger("bandeau.notices.read");
    check(bar.unreadNotices() == 0 && fired.empty(), "Tout marquer comme lu : a la barre seule");
    app::TopBar::Notice c = a;
    c.title = "Export termin\xC3\xA9 : alarmes.csv";
    bar.setNotices({a, b, c});
    check(bar.unreadNotices() == 1, "un avis nouveau : 1 non lue");
    ui::PopupMenu popup("barre.menu");
    bar.setPopup(&popup);
    bar.openMenu(app::TopBar::Menu::Notices);
    popup.itemChosen->emit(1);     // 0 : Tout marquer comme lu ; 1 : la premiere ligne
    check(fired.size() == 1 && fired[0] == "hmi.compile", "le bouton d'une ligne : son action");

    const float idle = bar.partRect("taches").w;
    bar.setTasks({{"G\xC3\xA9n\xC3\xA9ration IHM", 0.62f, "hmi.cancel"}});
    check(bar.partRect("taches").w > idle, "une tache tourne : la jauge s'elargit");
    bar.setTasks({});
    check(bar.partRect("taches").w == idle, "plus rien : l'icone seule");

    for (int i = 0; i < 60; ++i) bar.setCycleTime(static_cast<float>(i % 20), 20.f);
    check(bar.cycleTimes().size() == 40, "la mini-courbe : 40 echantillons au plus");
}

void testRegistry() {
    std::printf("4. Le registre des taches et des avis\n");
    const int t = app::bgtasks::begin("Import", "import.cancel");
    app::bgtasks::progress(t, 0.5f);
    auto list = app::bgtasks::list();
    check(list.size() == 1 && list[0].progress == 0.5f && list[0].cancelAction == "import.cancel", "begin, progress");
    {
        app::bgtasks::Scope s("Export");
        check(app::bgtasks::list().size() == 2, "Scope : inscrite");
    }
    check(app::bgtasks::list().size() == 1, "Scope : retiree a la fin");
    app::bgtasks::end(t);
    check(app::bgtasks::list().empty(), "end");
    app::bgtasks::Notice n;
    n.key = "lib";
    n.title = "5 mises a jour";
    app::bgtasks::post(n);
    n.title = "6 mises a jour";
    app::bgtasks::post(n);
    check(app::bgtasks::notices().size() == 1 && app::bgtasks::notices()[0].title == "6 mises a jour", "post : la meme cle remplace");
    app::bgtasks::withdraw("lib");
    check(app::bgtasks::notices().empty(), "withdraw");
}

// 1.11 (R111, T1-1) : un renderer qui note le texte dessine. Il mesure comme la
// mise en place de la barre dans ces essais (ui::measureWidth sans services :
// une avance fixe par caractere), et ne coupe jamais au milieu d'un caractere.
class TextRecorder final : public gfx::IRenderer {
public:
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view s, gfx::FontId, gfx::Color) override { texts.emplace_back(s); }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        return {ui::measureWidth(s, f), 16.f, 12.f, 4.f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId) const override { return 16.f; }
    [[nodiscard]] gfx::Size surfaceSize() const override { return {1600.f, 900.f}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f, float maxWidth) const override {
        std::size_t fit = 0;
        for (std::size_t e = 1; e <= s.size(); ++e) {
            if (e < s.size() && (static_cast<unsigned char>(s[e]) & 0xC0) == 0x80) continue;   // au milieu d'un caractere
            if (ui::measureWidth(s.substr(0, e), f) > maxWidth) break;
            fit = e;
        }
        return fit;
    }

    std::vector<std::string> texts;
};

const std::string kDots = "\xE2\x80\xA6";   // « … »

bool startsWith(const std::string& s, const std::string& head) { return s.compare(0, head.size(), head) == 0; }

// Ce que la puce projet a ecrit du nom : le texte dessine qui en est le debut,
// avec ou sans « … » (le premier trouve ; le nom est ecrit deux fois, en gras).
std::string drawnName(const TextRecorder& rec, const std::string& name) {
    for (const auto& t : rec.texts) {
        if (t == name) return t;
        if (t.size() > kDots.size() && t.compare(t.size() - kDots.size(), kDots.size(), kDots) == 0
            && startsWith(name, t.substr(0, t.size() - kDots.size())))
            return t;
    }
    for (const auto& t : rec.texts)   // le defaut d'avant : un debut du nom, sans rien pour le dire
        if (t.size() >= 3 && t.size() < name.size() && startsWith(name, t)) return t;
    return {};
}

bool validUtf8(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const std::size_t n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (std::size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

void testProjectName() {
    std::printf("5. Le nom du projet coupe : « … », et l'infobulle le donne entier (T1-1)\n");
    const ui::Theme theme = ui::Theme::dark();
    const auto paint = [&](app::TopBar& bar) {
        TextRecorder rec;
        const ui::PaintContext ctx{rec, theme, bar.bounds(), 0.0};
        bar.render(ctx);
        return rec;
    };
    const auto overName = [](const app::TopBar& bar) {   // sur le nom, pas sur l'icone (40 px)
        const auto r = bar.partRect("projet");
        return gfx::Point{r.x + 47.f + 6.f, r.y + 10.f};
    };

    // Le bandeau serre (comme Armoire_Gaz DEV modifie en 1600 x 900) : 130 px pour la puce projet.
    {
        app::TopBar bar("barre");
        const std::string name = "Armoire_Gaz";
        bar.setProject(name, "DEV", true);
        bar.setBounds(gfx::Rect{0.f, 0.f, 1000.f, 44.f});
        const auto pr = bar.partRect("projet");
        check(pr.w > 0.f && pr.w <= 130.f, "1000 px : le mode serre, la puce projet a 130 px au plus (" + std::to_string(static_cast<int>(pr.w)) + ")");
        const auto rec = paint(bar);
        const auto shown = drawnName(rec, name);
        check(shown.size() > kDots.size() && shown.compare(shown.size() - kDots.size(), kDots.size(), kDots) == 0,
              "le nom coupe finit par \xC2\xAB \xE2\x80\xA6 \xC2\xBB (" + (shown.empty() ? std::string("rien") : shown) + ")");
        bool word = false;
        for (const auto& t : rec.texts) word = word || t == "modifi\xC3\xA9";
        check(!word, "\xC2\xAB modifi\xC3\xA9 \xC2\xBB ne passe plus sous Enregistrer : le point orange seul");
        check(ui::measureWidth(shown, gfx::FontId{15}) <= pr.w - 67.f + 0.5f, "\xC2\xAB \xE2\x80\xA6 \xC2\xBB compris, il tient avant le chevron");
        const auto tip = bar.liveTooltip(overName(bar));
        check(startsWith(tip, name + "\n"), "l'infobulle du nom : d'abord le nom entier (" + tip.substr(0, tip.find('\n')) + ")");
        check(tip.find("Le projet : ouvrir") != std::string::npos, "puis ce que la puce fait, comme avant");
        const auto icon = bar.liveTooltip({pr.x + 21.f, pr.y + 17.f});
        check(startsWith(icon, "L'ic\xC3\xB4ne du projet"), "sur l'icone : l'infobulle de l'icone, comme avant");
    }
    // Un nom accentue : la coupe ne tombe pas au milieu d'un caractere.
    {
        app::TopBar bar("barre");
        const std::string name = "\xC3\x89\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9_Pupitre";   // « Ééééé_Pupitre »
        bar.setProject(name, "DEV", true);
        bar.setBounds(gfx::Rect{0.f, 0.f, 1000.f, 44.f});
        const auto shown = drawnName(paint(bar), name);
        check(!shown.empty() && shown != name && validUtf8(shown), "accents : coupe sur un caractere entier (" + shown + ")");
    }
    // A l'aise : le nom entier, sans « … », et l'infobulle d'avant.
    {
        app::TopBar bar("barre");
        const std::string name = "Armoire";
        bar.setProject(name, "NEW", false);
        bar.setBounds(gfx::Rect{0.f, 0.f, 1600.f, 44.f});
        const auto shown = drawnName(paint(bar), name);
        check(shown == name, "1600 px, un nom court : ecrit entier (" + shown + ")");
        const auto tip = bar.liveTooltip(overName(bar));
        check(startsWith(tip, "Le projet : ouvrir"), "le nom entier se lit : l'infobulle ne le repete pas");
    }
    // A l'aise et modifie : le mot « modifié » est la, comme avant.
    {
        app::TopBar bar("barre");
        bar.setProject("Armoire_Gaz", "DEV", true);
        bar.setBounds(gfx::Rect{0.f, 0.f, 1600.f, 44.f});
        const auto rec = paint(bar);
        bool word = false;
        for (const auto& t : rec.texts) word = word || t == "modifi\xC3\xA9";
        check(bar.partRect("projet").w > 130.f && word, "1600 px, modifie : \xC2\xAB modifi\xC3\xA9 \xC2\xBB ecrit, comme avant");
    }
}

} // namespace

int main() {
    testZones();
    testUndoList();
    testBellTasks();
    testRegistry();
    testProjectName();
    std::printf("\n%d echec(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
