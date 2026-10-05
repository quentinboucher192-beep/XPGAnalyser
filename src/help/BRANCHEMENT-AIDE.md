# Brancher le module `src/help/` dans l'aide

Cinq fichiers nouveaux, aucun fichier existant remplace. Le module ne connait
ni `app/` ni les ecrans : il rend des donnees, et c'est `LibraryHelpPage` qui
les affiche. Tout ce qui suit est du code a coller dans **vos** fichiers.

| fichier | ce qu'il apporte |
|---|---|
| `help/HelpCodes.{hpp,cpp}`   | 24 codes `XPG-nnnn`, leur page, `withCode()` |
| `help/HelpIndex.{hpp,cpp}`   | recherche classee, renvois calcules, glossaire, F1, bandeau de version |
| `help/HelpTryIt.{hpp,cpp}`   | le bouton **Essayer** : l'exemple tourne dans le vrai simulateur |
| `help/HelpSession.{hpp,cpp}` | historique, recents, favoris, insertion de l'exemple, ouvrir le source, visite |
| `help/HelpExport.{hpp,cpp}`  | le dossier d'affaire et l'impression d'une page, en HTML autonome |

Aucun ne depend de SDL. Ils se compilent et se testent sans ecran — c'est ce
qui a permis de trouver les deux defauts notes en fin de document.

---

## 0. Le projet Visual Studio

A ajouter a `XpgAnalyzer.vcxproj` (et aux `CMakeLists.txt` si vous les gardez
a jour) :

```
src\help\HelpCodes.cpp
src\help\HelpIndex.cpp
src\help\HelpTryIt.cpp
src\help\HelpSession.cpp
src\help\HelpExport.cpp
```

Les tests, si vous les branchez :

```
tests\help_test.cpp          help_test <dossier libs>
tests\helpsession_test.cpp   helpsession_test <dossier libs>
```

---

## 1. `LibraryHelpPage` — les membres a ajouter

Dans `app/screens/LibraryHelpScreen.hpp`, a la suite des membres existants :

```cpp
#include "../../help/HelpExport.hpp"
#include "../../help/HelpSession.hpp"
#include "../../help/HelpTryIt.hpp"

    // ...
private:
    void showEntryMenu(ui::NodeId node, gfx::Point at);   // menu contextuel
    void goTo(const help::Target& target);                // le seul chemin d'ouverture
    void runExample();                                    // n°1  Essayer
    void insertExample();                                 // n°13 inserer
    void exportDossier();                                 // n°11 le dossier
    void printCurrentPage();                              // n°12 imprimer
    void openSourceFile();                                // n°17 ouvrir le .ddt
    void startTour();                                     // n°20 la visite
    void refreshNavButtons();

    ui::Button*     back_{nullptr};
    ui::Button*     forward_{nullptr};
    ui::Button*     star_{nullptr};
    ui::Button*     tryIt_{nullptr};
    ui::PopupMenu*  entryMenu_{nullptr};
    ui::NodeId      menuNode_{ui::kInvalidNode};

    help::Navigation             nav_;
    std::shared_ptr<help::TrySession> session_;   // vit tant que la page est ouverte
```

`goTo` est volontairement **le seul** endroit qui change de page. Tout le
reste — l'arbre, la recherche, F1, un renvoi, un favori, Precedent — y passe.
Deux chemins d'ouverture, c'est un historique qui oublie la moitie des pages.

```cpp
void LibraryHelpPage::goTo(const help::Target& target) {
    if (target.kind == help::TargetKind::None) return;
    nav_.go(target);
    showTarget(target);        // selectionne le noeud, remplit l'article
    refreshNavButtons();
}
```

`showTarget` ne touche pas a `nav_` : c'est ce qui permet a Precedent et
Suivant de l'appeler sans repousser une entree dans l'historique.

```cpp
void LibraryHelpPage::refreshNavButtons() {
    if (back_)    back_->setEnabled(nav_.canBack());
    if (forward_) forward_->setEnabled(nav_.canForward());
    if (star_)    star_->setText(nav_.isFavourite(nav_.current()) ? "★" : "☆");
    if (tryIt_) {
        const auto* e = current();
        tryIt_->setEnabled(e != nullptr && help::hasExample(*e));
    }
}
```

---

## 2. La barre d'outils

A ajouter dans `buildUi()`, la ou la barre est deja construite :

```cpp
    back_ = toolbar_->addButton("←", "Precedent");
    forward_ = toolbar_->addButton("→", "Suivant");
    star_ = toolbar_->addButton("☆", "Garder cette page");
    toolbar_->addSeparator();
    tryIt_ = toolbar_->addButton("Essayer", "Faire tourner l'exemple (F5)");

    links_ += back_->clicked.connect([this] { showTarget(nav_.back()); refreshNavButtons(); });
    links_ += forward_->clicked.connect([this] { showTarget(nav_.forward()); refreshNavButtons(); });
    links_ += star_->clicked.connect([this] {
        nav_.toggleFavourite(nav_.current());
        refreshNavButtons();
        setStatus(nav_.isFavourite(nav_.current()) ? "Page gardee" : "Page retiree des favoris");
    });
    links_ += tryIt_->clicked.connect([this] { runExample(); });
```

> Adaptez `addButton` a la vraie signature de votre `ToolBar` — c'est la seule
> chose ici que je n'ai pas pu verifier contre votre code.

**La connexion doit etre gardee.** `core::Signal::connect()` rend une
`Connection` qui se deconnecte en se detruisant : un `connect(...)` dont on
jette le resultat ne fait rien, et ne dit pas qu'il ne fait rien. C'est
exactement le defaut qui a coute une soiree sur le glisser-deposer de l'ordre
d'execution.

---

## 3. Le menu contextuel de l'arbre

```cpp
enum {
    ActionOuvrirSource = 1, ActionCopierExemple, ActionInsererExemple,
    ActionFavori, ActionExporter, ActionImprimer, ActionEssayer, ActionVisite,
};

void LibraryHelpPage::showEntryMenu(ui::NodeId node, gfx::Point at) {
    if (!entryMenu_) return;
    menuNode_ = node;
    const project::CatalogEntry* e = treeModel_->entry(node);

    std::vector<ui::PopupMenu::Item> items;
    if (e != nullptr) {
        const bool exemple = help::hasExample(*e);
        const std::string pourquoi = exemple ? "" : "ce bloc n'a pas d'exemple";

        items.push_back({"Essayer l'exemple", "F5", pourquoi, ui::Icon::Play,
                         exemple, false, ActionEssayer});
        items.push_back({"Copier l'exemple", "Ctrl+C", pourquoi, ui::Icon::Copy,
                         exemple, false, ActionCopierExemple});
        items.push_back({"Inserer dans la section ouverte", "", pourquoi, ui::Icon::Paste,
                         exemple, false, ActionInsererExemple});
        items.push_back({"", "", "", ui::Icon::None, true, true, -1});
        items.push_back({"Ouvrir le fichier source", "", e->path.empty()
                             ? "cette entree n'a pas de fichier" : "",
                         ui::Icon::Document, !e->path.empty(), false, ActionOuvrirSource});
        items.push_back({nav_.isFavourite(nav_.current()) ? "Retirer des favoris"
                                                          : "Garder cette page",
                         "", "", ui::Icon::Star, true, false, ActionFavori});
        items.push_back({"", "", "", ui::Icon::None, true, true, -1});
    }
    items.push_back({"Imprimer cette page...", "Ctrl+P", "", ui::Icon::Print,
                     true, false, ActionImprimer});
    items.push_back({"Exporter le dossier d'aide...", "", "", ui::Icon::Export,
                     true, false, ActionExporter});
    items.push_back({"Revoir la visite guidee", "", "", ui::Icon::Help,
                     true, false, ActionVisite});

    entryMenu_->setItems(std::move(items));
    const auto surface = root().bounds();
    entryMenu_->openAt(at, {surface.w, surface.h});
}
```

La raison (`"ce bloc n'a pas d'exemple"`) est le troisieme champ de `Item` :
une entree grisee sans raison est une entree dont on ne sait pas quoi faire.
Comme dans `showExplorerMenu`, tout est juge **maintenant**, contre le modele
tel qu'il est.

Le `PopupMenu` doit vivre dans un `OverlayHost`, comme dans
`MainAnalysisScreen::buildUi` :

```cpp
    auto host = std::make_unique<ui::OverlayHost>("help.host");
    host->setContent(std::move(contenu));
    auto popup = std::make_unique<ui::PopupMenu>("help.entryMenu");
    entryMenu_ = popup.get();
    host->addOverlay(std::move(popup));
```

---

## 4. F1, depuis partout

Dans `MainAnalysisScreen::onEvent` (ou la ou vous traitez deja les touches) :

```cpp
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k != nullptr && k->key == ui::Key::F1) {
        help::Target cible;

        // 1. le mot sous le curseur, quand une section est ouverte
        if (auto* editeur = currentSectionEditor(); editeur != nullptr) {
            const std::string ligne = editeur->lineForTest(editeur->caretLineForTest());
            cible = help::targetForWord(library_, help::wordAt(ligne, editeur->caretColumn()));
        }
        // 2. sinon la ligne de diagnostic selectionnee
        if (cible.kind == help::TargetKind::None && diagnosticsSelected())
            cible = help::targetForDiagnostic(selectedDiagnosticText());
        // 3. sinon le noeud de l'arbre
        if (cible.kind == help::TargetKind::None)
            cible = targetForExplorerNode(explorer_->currentNode());

        openHelp(cible);          // ouvre l'onglet d'aide et appelle goTo
        return ui::EventResult::Consumed;
    }
```

L'ordre est le sujet : le curseur est le contexte le plus precis, l'arbre le
moins. `targetForWord` fait deja le tri entre un nom de bloc, un parametre et
un mot du glossaire — `Val` est les trois a la fois, et c'est lui qui tranche.

Dans `LibraryHelpPage::onEvent`, F1 sans contexte ouvre la table des codes :

```cpp
    if (k->key == ui::Key::F1) { showArticle(help::indexArticle()); return ui::EventResult::Consumed; }
    if (k->key == ui::Key::F5) { runExample(); return ui::EventResult::Consumed; }
```

---

## 5. Le bandeau de version

Dans `buildHelpArticle`, juste apres le `Subtitle` :

```cpp
    const std::string notice =
        help::versionNotice(entry.name, entry.version, versionDansLeProjet);
    if (!notice.empty())
        article.blocks.push_back({ui::HelpBlockKind::Note, notice});
```

`versionDansLeProjet` est la version de l'item **importe dans le projet
ouvert** (`SharedLibrary` la connait), pas celle du fichier de `libs/`. Le
bandeau ne sert qu'a dire que les deux ont diverge. Le cas `1.00 -> 1.01` de
`DFB_IO_DIG16` est nomme explicitement, parce que c'est le defaut de `Count`
qui vous a coute une journee et qu'un message general ne l'aurait pas evite.

---

## 6. Essayer, et inserer

```cpp
void LibraryHelpPage::runExample() {
    const auto* e = current();
    if (e == nullptr || !help::hasExample(*e)) return;

    auto built = help::TrySession::build(*e, treeModel_->entries());
    if (!built) { setStatus(help::withCode(built.error().message), true); return; }

    session_ = *built;
    const help::TryReport r = session_->step();
    showTryReport(r);      // une ligne de l'exemple, ses variables a droite
    setStatus(r.ran ? "L'exemple a tourne" : help::withCode(r.failure), !r.ran);
}
```

`TryReport::notes` porte ce qui a ete **suppose** — « `AvancerBande` n'a pas
de type deduit, declare en BOOL ». Montrez-les : une hypothese muette est pire
qu'un exemple qui ne tourne pas. `force(nom, valeur)` puis `step()` relance
avec une entree forcee, ce qui est la moitie de l'interet du bouton.

L'insertion passe par une commande, donc Ctrl+Z la reprend :

```cpp
void LibraryHelpPage::insertExample() {
    const auto* e = current();
    auto document = app_.document();
    if (e == nullptr || !document || sectionOuverte_ == domain::kNoIndex) return;

    const auto& section = document->project().sections[sectionOuverte_];
    std::string corps = help::bodyWithExample(section.body, *e, ligneDuCurseur_);
    app_.apply(std::make_unique<project::SetSectionBodyCommand>(
        document, sectionOuverte_, std::move(corps)));
    setStatus("Exemple insere - Ctrl+Z pour annuler");
}
```

---

## 7. Le dossier, et l'impression

```cpp
void LibraryHelpPage::exportDossier() {
    help::DossierInfo info;
    info.affaire  = app_.project() ? app_.project()->name : "";
    info.projet   = app_.documentPath();
    info.libsRoot = libsRoot_;

    const std::string html = help::renderDossierHtml(treeModel_->entries(), info);
    const std::string chemin = chooseSavePath(help::suggestedFileName(info));
    if (chemin.empty()) return;

    const std::string erreur = help::writeHtmlFile(chemin, html);
    setStatus(erreur.empty() ? "Dossier ecrit : " + chemin : erreur, !erreur.empty());
}
```

`printCurrentPage()` est le meme geste avec `renderArticleHtml(article_->article(),
current()->name)` : c'est l'article **tel qu'il est affiche** qui part dans le
fichier, donc l'impression ne peut pas diverger de l'ecran. Le fichier
s'ouvre, et c'est le navigateur qui imprime — la feuille `@media print` lui
donne ses sauts de page.

Le document est **autonome** : ni script, ni image, ni lien vers l'exterieur.
Un test le verifie, parce que c'est la seule propriete qui compte le jour ou
on l'ouvre sur le poste d'un client.

---

## 8. Recents, favoris et visite : la persistance

Dans le chargement des reglages :

```cpp
    nav_.restore(settings_.getList("help.recents"), settings_.getList("help.favourites"));
```

et a la fermeture :

```cpp
    settings_.setList("help.recents",    nav_.saveRecents());
    settings_.setList("help.favourites", nav_.saveFavourites());
```

Un favori qui ne designe plus rien — bloc renomme, parametre supprime — n'est
pas efface en silence : `nav_.stale(bibliotheque)` les rend, et l'ecran les
montre grises. Un favori disparu sans explication passe pour un bug de
l'application.

La visite se lance une fois :

```cpp
    if (!settings_.getBool(std::string(help::kTourSeenKey), false)) {
        startTour();
        settings_.set(std::string(help::kTourSeenKey), true);
    }
```

`help::tour()` rend cinq etapes ; `anchor` vaut `"tree"`, `"search"`,
`"article"`, `"try"`, `"f1"` — a vous de faire correspondre ces cinq noms aux
widgets a entourer.

---

## Deux defauts trouves en ecrivant tout ca

**Les renvois ecrits ne fonctionnaient pas.** `#! see = DFB_IO_DIG16,
ST_IO_Ana` est **une** ligne, et `backlinks()` la comparait entiere a un nom.
Elle ne rendait donc jamais vrai des qu'un fichier cite deux voisins — c'est a
dire presque partout dans `libs/`. Les renvois structurels (« parametre
`Chan` ») couvraient le trou, et rien ne se voyait. Corrige par
`help::seeNames()`, une seule decoupe partagee par le calcul des renvois et les
liens du dossier.

**Quatre exemples de la bibliotheque ne tournaient pas**, et le bouton Essayer
est ce qui l'a dit :

| bloc | ce qui n'allait pas | corrige en |
|---|---|---|
| `AlmDecode` | `AlmHist[AlmStat.HistHead - 1]` vaut `AlmHist[-1]` tant que rien n'est tombe | un evenement `Evt` deja en main, et la regle du `HistHead - 1` expliquee dans `usage` |
| `DFB_EQ_CONVEYOR` | `Sequence.AvancerBande` : `Sequence` n'existe nulle part | `AvancerBande`, avec `(* votre sequence *)` |
| `DFB_EQ_CYLINDER` | idem `Sequence.SortirPoussoir` | `SortirPoussoir` |
| `DFB_EQ_MOTOR` | idem `Sequence.DemarrerVentilateur` | `DemarrerVentilateur` |

La bibliotheque livree ici est corrigee : **27 exemples sur 27 tournent**.
C'est un chiffre a garder — le jour ou il baisse, un exemple est devenu faux,
et vous le saurez ce jour-la plutot que deux ans apres.
