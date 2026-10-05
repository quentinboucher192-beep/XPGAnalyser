// =============================================================================
//  tests/lot8tutorials_test.cpp - Lot API 8 : didacticiels et aide
// -----------------------------------------------------------------------------
//  Les huit didacticiels du lot 8 (app/TutorialsLot8) : leurs cles (uniques,
//  "api-..." - le script "parcours api-..." les trouve -, pas celles du lot 7),
//  leur sorte, leurs etapes ; chacun a sa page dans l'aide generale
//  (ui::buildHelp), comme la page des nouveautes, celle des raccourcis clavier
//  (F5, Maj+F5, F9, F10, F2, Ctrl+K, Ctrl+F) et celles des nouveaux ecrans
//  (F1) ; le menu Aide porte Nouveautes... et Raccourcis clavier ; les
//  parties de la barre du haut que les etapes eclairent existent.
// =============================================================================
#include "../src/app/TopBar.hpp"
#include "../src/app/TutorialsLot8.hpp"
#include "../src/ui/HelpDocument.hpp"

#include <cstdio>
#include <set>
#include <string>

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) ++failures;
    std::printf("  [%s] %s\n", ok ? "ok" : "ECHEC", what.c_str());
}

bool hasAnchor(const ui::HelpDocument& d, const std::string& anchor) {
    for (const auto& t : d.toc())
        if (t.anchor == anchor) return true;
    return false;
}

std::string titleOf(const ui::HelpDocument& d, const std::string& anchor) {
    for (const auto& t : d.toc())
        if (t.anchor == anchor) return t.title;
    return {};
}

// Le mot est-il dans la page de cette ancre ?
bool inSection(const ui::HelpDocument& d, const std::string& word, const std::string& anchor) {
    const std::string title = titleOf(d, anchor);
    for (const auto& h : d.search(word, 500))
        if (h.anchor == anchor || (!title.empty() && h.section == title)) return true;
    return false;
}

void testCatalog() {
    std::printf("1. Les didacticiels du lot 8\n");
    const auto& all = app::lot8::trails();
    check(all.size() == 8, "huit didacticiels : " + std::to_string(all.size()));
    std::set<std::string> keys;
    const std::set<std::string> lot7 = {"api-decouvrir", "api-variable", "api-ihm-table", "api-bloc", "api-ordre", "api-macro"};
    bool prefix = true, kinds = true, filled = true, found = true, fresh = true;
    for (const auto& t : all) {
        const std::string key = t.key;
        keys.insert(key);
        prefix = prefix && key.rfind("api-", 0) == 0;
        kinds = kinds && (std::string(t.kind) == "visite" || std::string(t.kind) == "interactif");
        filled = filled && t.steps >= 4 && t.minutes > 0 && std::string(t.title).size() > 5 && std::string(t.summary).size() > 40;
        found = found && app::lot8::find(key) == &t;
        fresh = fresh && lot7.count(key) == 0;
    }
    check(keys.size() == all.size(), "des cles uniques");
    check(prefix, "chaque cle commence par api- (le script parcours les trouve dans l'onglet API . Didacticiel)");
    check(fresh, "aucune cle du lot 7 reprise");
    check(kinds, "une visite ou un parcours interactif");
    check(filled, "un titre, un resume, au moins quatre etapes et une duree");
    check(found, "find() rend chacun");
    check(app::lot8::find("api-decouvrir") == nullptr && app::lot8::find("") == nullptr, "find() : ni le lot 7 ni une cle vide");
    const char* expected[] = {"api-simuler", "api-deboguer", "api-pause", "api-glisser", "api-theme", "api-renommer", "api-expressions", "api-filtres"};
    bool all8 = true;
    for (const char* k : expected) all8 = all8 && keys.count(k) == 1;
    check(all8, "les huit du sujet : simuler, deboguer, pause, glisser, theme, renommer, expressions, filtres");
}

void testHelp() {
    std::printf("2. L'aide generale\n");
    const auto d = ui::buildHelp();
    bool anchors = true;
    std::string missing;
    for (const auto& a : app::lot8::allAnchors())
        if (!hasAnchor(d, a)) { anchors = false; missing += " " + a; }
    check(anchors, "chaque page citee par le lot 8 existe" + (missing.empty() ? std::string() : " ; manque :" + missing));
    // 1.11, decision 12 : le titre sans nom de chantier ; l'ancre ne change pas.
    check(titleOf(d, app::lot8::kNewsAnchor) == "Nouveaut\xC3\xA9s : la simulation, le d\xC3\xA9" "bogage, les fichiers",
          "la page des nouveautes");
    check(titleOf(d, app::lot8::kShortcutsAnchor) == "Raccourcis clavier", "la page des raccourcis clavier");
    bool keysOk = true;
    std::string absent;
    for (const char* k : {"F5", "Maj+F5", "F9", "F10", "F2", "Ctrl+K", "Ctrl+F", "Ctrl+Z", "F1"})
        if (!inSection(d, k, app::lot8::kShortcutsAnchor)) { keysOk = false; absent += std::string(" ") + k; }
    check(keysOk, "les raccourcis du lot 8 sont dans la page" + (absent.empty() ? std::string() : " ; absents :" + absent));
    bool titled = true;
    for (const auto& t : app::lot8::trails()) {
        titled = titled && inSection(d, t.title, app::lot8::kNewsAnchor) && hasAnchor(d, t.anchor);
        if (!inSection(d, t.title, app::lot8::kNewsAnchor)) std::printf("       pas dans les nouveautes : %s\n", t.title);
    }
    check(titled, "chaque didacticiel est cite dans les nouveautes, et a sa page");
    bool screens = true;
    for (const char* k : {"ensemble", "automate", "debogage", "forcages", "courbes", "journal", "glisser", "themes", "renommer", "compiler"})
        screens = screens && !app::lot8::helpAnchorFor(k).empty() && hasAnchor(d, app::lot8::helpAnchorFor(k));
    check(screens, "F1 : chaque nouvel ecran a sa page");
    check(app::lot8::helpAnchorFor("inconnu").empty(), "un ecran inconnu : pas de page");
    check(!inSection(d, "F9 ouvre l'\xC3\xA9" "cran de simulation", "nouveautes"), "l'ancien F9 (l'ecran de simulation) n'est plus decrit");

    app::lot8::setPendingHelpAnchor("raccourcis");
    const auto first = app::lot8::takePendingHelpAnchor();
    const auto second = app::lot8::takePendingHelpAnchor();
    check(first == "raccourcis" && second.empty(), "la page demandee : prise une fois");
}

void testMenu() {
    std::printf("3. Le menu Aide et la barre du haut\n");
    app::TopBar bar("barre");
    // 1.10 (R2) : plus d'entree "Nouveautes du lot 8" ; une seule entree Nouveautes... (la
    // fenetre des nouveautes, help.news), dont la ligne Versions precedentes ouvre l'onglet
    // API . Nouveautes de la 1.8.0 (help.lot8, que la fenetre et les scripts gardent).
    // 1.11 (T2, decision 22) : aucun nom de chantier dans un libelle.
    check(bar.reaches("help.news") && bar.reaches(app::lot8::kShortcutsAction), "Aide : Nouveautes..., Raccourcis clavier");
    check(bar.actionForLabel("Nouveaut\xC3\xA9s\xE2\x80\xA6") == "help.news", "Nouveautes... par son libelle");
    check(bar.actionForLabel("Nouveaut\xC3\xA9s du lot 8").empty(), "plus de libelle Nouveautes du lot 8 (un nom de chantier)");
    check(bar.actionForLabel("Raccourcis clavier") == app::lot8::kShortcutsAction, "Raccourcis clavier par son libelle");
    check(bar.reaches("help.apiTutorial"), "le didacticiel de l'API reste");
    bar.setBounds(gfx::Rect{0.f, 0.f, 1900.f, 44.f});
    bool parts = true;
    for (const char* p : {"simuler", "arreter", "cycle", "affichage", "aide"}) {
        const bool ok = !bar.partRect(p).empty();
        if (!ok) std::printf("       partie introuvable : %s\n", p);
        parts = parts && ok;
    }
    check(parts, "les parties de la barre que les etapes eclairent existent (Simuler, Arreter, Un cycle, Affichage, Aide)");
}

} // namespace

int main() {
    testCatalog();
    testHelp();
    testMenu();
    std::printf("\n%d verifications, %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
