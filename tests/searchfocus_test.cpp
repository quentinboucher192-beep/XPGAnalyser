// =============================================================================
//  tests/searchfocus_test.cpp - lot API 8 (finitions) : Ctrl+F dans les volets
// -----------------------------------------------------------------------------
//  Sans ecran. Ctrl+F (MainAnalysisScreen::handleShortcut, une fenetre
//  detachee) passe par app::focusSearchField : le PREMIER champ de recherche
//  MONTRE de l'onglet (la barre des listes de l'API, ui::SearchField, un champ
//  dont l'id dit qu'il cherche), le curseur dedans, son texte choisi (la frappe
//  le remplace). Echap dans ce champ l'efface (le curseur reste) ; vide, il le
//  quitte. Un champ ordinaire garde son Echap d'avant (il le quitte, sans rien
//  effacer).
// =============================================================================
#include "../src/app/ApiListKit.hpp"
#include "../src/app/SearchFocus.hpp"
#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/widgets/SearchField.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace ui;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

} // namespace

int main() {
    std::printf("Ctrl+F : le champ de recherche montre de l'onglet\n");
    Widget root("onglet");
    root.setBounds({0, 0, 1000, 700});
    auto& other = static_cast<InputText&>(root.addChild(std::make_unique<InputText>("volet.nom")));
    other.setBounds({10, 10, 200, 28});
    auto& hidden = static_cast<app::ApiFilterBar&>(root.addChild(std::make_unique<app::ApiFilterBar>("volet.cache", "Rechercher")));
    hidden.setBounds({10, 50, 600, 28});
    hidden.layout();
    hidden.setVisibility(Visibility::Collapsed);
    auto& bar = static_cast<app::ApiFilterBar&>(root.addChild(std::make_unique<app::ApiFilterBar>("volet.filtres", "Rechercher")));
    bar.setBounds({10, 90, 600, 28});
    bar.layout();
    auto& field = static_cast<SearchField&>(root.addChild(std::make_unique<SearchField>("volet2.search", "Rechercher")));
    field.setBounds({10, 130, 600, 28});
    field.layout();

    bar.setSearch("pompe vanne");
    check(app::visibleSearchField(root) == &bar.field(), "le premier champ de recherche montre (ni le cache, ni un champ ordinaire)");
    check(app::focusSearchField(root) && bar.field().focused(), "Ctrl+F : le curseur dedans");
    (void)root.dispatch(TextInput{"x"});
    check(bar.search() == "x", "son texte etait choisi : la frappe le remplace");
    bar.setSearch("gaz");
    (void)app::focusSearchField(root);
    (void)root.dispatch(KeyDown{Key::Escape, {}, false});
    check(bar.search().empty() && bar.field().focused(), "Echap : la recherche effacee, le curseur reste");
    (void)root.dispatch(KeyDown{Key::Escape, {}, false});
    check(!bar.field().focused(), "Echap encore (vide) : il quitte le champ");

    other.setText("abc");
    check(other.focusAndSelectAll() && other.focused(), "un champ ordinaire prend le curseur");
    (void)root.dispatch(KeyDown{Key::Escape, {}, false});
    check(other.text() == "abc" && !other.focused(), "un champ ordinaire : Echap le quitte sans l'effacer (comme avant)");

    bar.setVisibility(Visibility::Collapsed);
    check(app::visibleSearchField(root) == &field.field(), "la barre cachee : le champ ui::SearchField suivant");
    field.setText("recette");
    check(app::focusSearchField(root) && field.field().focused() && !bar.field().focused(), "ui::SearchField : le curseur dedans, et plus ailleurs");
    (void)root.dispatch(KeyDown{Key::Escape, {}, false});
    check(field.text().empty(), "ui::SearchField : Echap l'efface");
    field.setVisibility(Visibility::Collapsed);
    check(!app::focusSearchField(root), "aucun champ de recherche montre : rien (l'ecran le dit)");

    // Un champ reconnu a son id (les volets de l'IHM, l'editeur).
    auto& byId = static_cast<InputText&>(root.addChild(std::make_unique<InputText>("hmi.editor.objectsSearch")));
    byId.setBounds({10, 170, 300, 28});
    byId.setText("Pompe");
    check(app::focusSearchField(root) && byId.focused() && byId.escapeClears(), "un champ ...Search : le curseur dedans, Echap l'effacera");
    check(app::looksLikeSearchId("x.chercher") && app::looksLikeSearchId("x.recherche") && app::looksLikeSearchId("x.find")
              && app::looksLikeSearchId("x.filter") && !app::looksLikeSearchId("volet.nom"),
          "les ids : .chercher, .recherche, .find, .filter oui ; .nom non");

    std::printf("%d controle(s), %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
