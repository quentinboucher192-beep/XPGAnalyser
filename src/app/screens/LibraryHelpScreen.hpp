// =============================================================================
//  app/screens/LibraryHelpScreen.hpp - l'onglet Macros du menu d'aide
// -----------------------------------------------------------------------------
//  CE QUE CET ECRAN EST CENSE RESOUDRE.
//
//  L'aide d'un bloc etait jusqu'ici lisible dans le fichier, et nulle part
//  ailleurs. Ouvrir un .ddt dans un editeur de texte pour savoir a quoi sert
//  Thermal, c'est exactement ce que le menu d'aide existe pour eviter.
//
//  TROIS ONGLETS, ET PAS UN DE PLUS.
//
//    Aide      ce que le bloc fait, mis en page pour etre lu.
//    Modifier  la meme chose, champ par champ, pour etre ecrit.
//    Source    les declarations telles qu'elles sont dans le fichier, parce
//              qu'une doc qu'on ne peut pas confronter a l'original ne se
//              verifie pas.
//
//  L'EDITEUR EST GENERIQUE. Il ne connait pas la liste des champs : il la
//  demande au fichier. Un bloc qui gagne un parametre gagne une ligne dans
//  l'editeur sans qu'on touche a l'editeur, et un bloc venu d'une autre equipe
//  s'edite comme les notres.
//
//  ON ECRIT DANS LE FICHIER DE LA BIBLIOTHEQUE, et on le dit. Enregistrer
//  modifie libs/<categorie>/<Nom>.ddt sur le disque. Le bandeau d'etat le
//  rappelle, le bouton s'appelle "Enregistrer dans libs", et le fichier est
//  relu apres ecriture : une sauvegarde qui n'a pas pris doit se voir tout de
//  suite, pas a la prochaine ouverture.
//
// -----------------------------------------------------------------------------
//  CE QUE LE MODULE help/ AJOUTE ICI, ET SELON QUELLE REGLE.
//
//  Chercher, revenir en arriere, garder une page, faire tourner l'exemple,
//  imprimer, sortir le dossier d'affaire. Tout le CALCUL de ces choses est dans
//  src/help/, qui ne sait pas qu'il existe un ecran ; cette page ne fait que
//  montrer ce qu'il rend. C'est la meme frontiere que partout ailleurs, et
//  c'est ce qui permet a help_test et helpsession_test de tourner sans serveur
//  X, donc en CI, donc a chaque commit.
//
//  LES COMMANDES SONT UNE LISTE, PAS UN MENU. `commands()` rend ce qui est
//  possible maintenant, avec pour chaque entree la RAISON quand ce n'est pas
//  possible. Le menu contextuel de l'hote la recopie dans ses PopupMenu::Item -
//  les champs sont dans le meme ordre, c'est une boucle de trois lignes - et
//  la barre d'outils s'en sert pour griser ses boutons. Une seule liste : un
//  menu qui propose Essayer sur un bloc sans exemple et une barre qui le grise
//  ne peuvent pas diverger s'ils lisent la meme chose.
//
//  CETTE PAGE NE CONNAIT NI LE PROJET NI LE DISQUE DE L'HOTE. Inserer un
//  exemple dans la section ouverte, choisir un chemin d'enregistrement, savoir
//  quelle version du bloc est importee dans le projet : trois choses que seul
//  l'hote sait. Elles arrivent par des std::function que l'hote pose, et
//  chacune non posee grise sa commande EN DISANT POURQUOI, au lieu d'etre
//  absente ou de ne rien faire.
// =============================================================================
#pragma once

#include "../../help/HelpCodes.hpp"
#include "../../help/HelpExport.hpp"
#include "../../help/HelpIndex.hpp"
#include "../../help/HelpSession.hpp"
#include "../../help/HelpTryIt.hpp"
#include "../../help/TryBench.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/Layout.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/HelpArticleView.hpp"
#include "../LibraryHelpModels.hpp"
#include "HelpBench.hpp"
#include "HelpChrome.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace app {

// --------------------------------------------------------- les commandes ----
// Les champs sont dans l'ordre de ui::PopupMenu::Item : l'hote recopie sans
// reflechir, et le jour ou Item gagne un champ, c'est ici qu'on l'ajoute.
struct HelpCommand {
    std::string label;
    std::string shortcut;
    std::string reason;      // vide = disponible ; sinon POURQUOI ca ne l'est pas
    ui::Icon    icon{ui::Icon::None};
    bool        enabled{true};
    bool        separator{false};
    int         id{0};
};

enum HelpCommandId : int {
    CmdNone = 0,
    CmdBack, CmdForward, CmdFavourite,
    CmdTry, CmdCopyExample, CmdInsertExample,
    CmdOpenSource, CmdPrintPage, CmdExportDossier, CmdTour, CmdDiagnostics,
};

// Lot macros 1 : CE QUE MONTRE UNE PAGE. L'aide avait un seul ecran pour
// toute la bibliotheque ; elle a maintenant un onglet Macros (rangees dans les
// dossiers de l'onglet Macros) et un onglet Blocs DFB / DDT (les dossiers de
// libs/). All est l'ancienne page, gardee pour les tests et les hotes.
enum class HelpScope : std::uint8_t { All, Blocks, Macros };

// La page complete. C'est un widget, pas un ecran : il se pose tel quel dans
// le TabControl d'un menu d'aide existant, et il sert aussi de racine a
// l'ecran autonome plus bas. Une seule implementation pour les deux, parce que
// deux versions de la meme page divergent des la premiere correction.
class LibraryHelpPage final : public ui::DockLayout {
public:
    explicit LibraryHelpPage(std::string libsRoot, std::string id = "help.macros",
                             HelpScope scope = HelpScope::All);

    [[nodiscard]] HelpScope scope() const noexcept { return scope_; }
    // Une entree de l'autre onglet (un lien " Voir aussi " d'une macro vers un
    // bloc...) : l'hote change d'onglet. Non posee : la page dit ou elle est.
    void setOutOfScope(std::function<void(const help::Target&)> f) { outOfScope_ = std::move(f); }
    // Le lien " Lancer " d'une macro : l'hote ouvre son formulaire. Non pose :
    // pas de lien.
    void setMacroLauncher(std::function<void(const std::string&)> f) { launchMacro_ = std::move(f); }
    // Le formulaire d'une macro (ses lignes #!), nul si ce n'en est pas une.
    [[nodiscard]] const project::macro::MacroSpec* macroSpec(const std::string& name) const;

    // Relit libs/ depuis le disque et reconstruit l'arbre.
    void reload();

    // Ce qui est selectionne, pour les tests et pour l'ecran hote.
    [[nodiscard]] const project::CatalogEntry* current() const;
    [[nodiscard]] std::size_t entryCount() const;

    // --- ce que l'hote branche ----------------------------------------------
    // Inserer l'exemple dans la section ouverte. Rend false si ca n'a pas pu se
    // faire. Non posee : la commande est grisee, et elle dit pourquoi.
    void setExampleInserter(std::function<bool(const project::CatalogEntry&)> f) {
        insertExample_ = std::move(f);
    }
    // Le nom de la section ouverte, pour le libelle ("Inserer dans Securites").
    void setSectionNameSource(std::function<std::string()> f) {
        sectionName_ = std::move(f);
    }
    // La version de ce bloc TELLE QU'IMPORTEE DANS LE PROJET. C'est la seule
    // facon de savoir que le projet et libs/ ont diverge, et SharedLibrary est
    // le seul a la connaitre.
    void setProjectVersionSource(std::function<std::string(const std::string&)> f) {
        projectVersion_ = std::move(f);
    }
    // Ou ecrire un export. Non posee, le fichier part a cote de libs/ et le
    // bandeau dit ou : mieux vaut un fichier ecrit quelque part de previsible
    // qu'un bouton qui ne fait rien faute de boite de dialogue.
    void setSavePathChooser(std::function<std::string(const std::string&)> f) {
        chooseSavePath_ = std::move(f);
    }
    // De quoi remplir la couverture du dossier.
    void setDossierInfo(help::DossierInfo info) { dossier_ = std::move(info); }

    // --- les commandes -------------------------------------------------------
    [[nodiscard]] std::vector<HelpCommand> commands() const;
    void runCommand(int id);

    // --- naviguer ------------------------------------------------------------
    // LE SEUL CHEMIN D'OUVERTURE. F1, un resultat de recherche, un favori, un
    // clic dans l'arbre : tout passe par la, sinon l'historique oublie la
    // moitie des pages - et un historique a trous est pire qu'aucun.
    void goTo(const help::Target& target);
    [[nodiscard]] help::Target targetForNode(ui::NodeId) const;
    [[nodiscard]] const help::Navigation& navigation() const noexcept { return nav_; }

    // Ce qui traverse la fermeture de l'application. L'hote les range dans
    // app::Settings (setList / getList) : le module d'aide n'a pas a connaitre
    // le format du fichier de reglages.
    [[nodiscard]] std::vector<std::string> savedRecents() const { return nav_.saveRecents(); }
    [[nodiscard]] std::vector<std::string> savedFavourites() const {
        return nav_.saveFavourites();
    }
    void restoreNavigation(const std::vector<std::string>& recents,
                           const std::vector<std::string>& favourites);

    void startTour();

    // LE POULS DU BANC D'ESSAI. Un widget n'a pas d'horloge : c'est l'ecran
    // hote qui lui passe le temps de l'image, depuis son Update(). Sans cet
    // appel le banc ne tourne pas, et "Marche" ne fait rien.
    void tick(double deltaSeconds);

    // --- seams de test -------------------------------------------------------
    // Ce que font les clics, sans clics. Le reste du code base fait pareil
    // (MultiLineText::caretLineForTest) : une interface sans machine a
    // evenements est une interface qu'on ne verifie qu'a la main, donc qu'on ne
    // verifie pas. Ces fonctions n'ajoutent aucun chemin : elles appellent
    // exactement ce que les signaux appellent.
    void selectEntryForTest(std::size_t index);
    void selectTabForTest(std::size_t index);
    void selectFieldForTest(std::size_t index);
    void typeForTest(std::string text);
    bool saveForTest();
    void revertForTest() { revert(); }
    void searchForTest(const std::string& term) { runSearch(term); }
    bool forceForTest(const std::string& name, const std::string& value);
    // Le banc, vu du dehors. Ces trois-la existent pour que le bout a bout se
    // verifie sans ecran : le banc lui-meme est teste a part, ce qui se verifie
    // ici est qu'il est BRANCHE.
    [[nodiscard]] std::uint32_t benchScansForTest() const;
    void                        benchPlayForTest();
    [[nodiscard]] BenchPane*    benchForTest() const noexcept { return benchPane_; }
    // La palette et les liens de l'article, par le meme chemin que le clavier
    // et la souris.
    void openPaletteForTest() { openPalette(); }
    [[nodiscard]] CommandPalette* paletteForTest() const noexcept { return palette_; }
    void linkForTest(const std::string& target) { onLink(target); }
    [[nodiscard]] bool saveVisibleForTest() const noexcept;
    [[nodiscard]] bool benchLiveForTest() const noexcept {
        return tabs_ != nullptr && tabs_->tab(benchTab_) != nullptr && tabs_->tab(benchTab_)->live;
    }
    [[nodiscard]] std::size_t currentTabForTest() const noexcept;
    [[nodiscard]] bool dirtyForTest() const noexcept { return dirty_; }
    [[nodiscard]] const project::LibraryHelp& draftForTest() const noexcept { return draft_; }
    [[nodiscard]] std::string fieldLabelForTest(std::size_t index) const;
    [[nodiscard]] std::size_t fieldCountForTest() const noexcept { return fields_.size(); }
    [[nodiscard]] const std::string& statusForTest() const noexcept { return lastStatus_; }
    [[nodiscard]] const ui::HelpArticle& articleForTest() const noexcept;

protected:
    ui::EventResult onEvent(const ui::InputEvent&) override;
    void            onLayout() override;

private:
    void buildUi();
    void selectNode(ui::NodeId);
    void selectField(ui::RowIndex);
    void applyFilter(const std::string& term);
    void onDraftEdited(const std::string& text);
    void refreshPreview();
    bool save();
    void revert();
    void setStatus(std::string message, bool error = false);
    void refreshTabTitles();

    // --- le module help/ ------------------------------------------------------
    // Montre une cible SANS toucher a l'historique : c'est ce qui permet a
    // Precedent et Suivant de s'en servir sans repousser la page qu'ils
    // viennent de depiler.
    void showTarget(const help::Target&);
    void showEntry(const project::CatalogEntry&, const std::string& subject = {});
    void showArticle(ui::HelpArticle, bool transient);
    void runSearch(const std::string& term);
    void runExample();
    void stepExample();
    void copyExample();
    void insertExampleIntoSection();
    void openSourceFile();
    void exportDossier();
    void printCurrentPage();
    void showDiagnostics();
    void refreshCommands();
    void refreshPlaces();
    // La barre suit l'etat : Enregistrer et Annuler n'apparaissent que quand il
    // y a quelque chose a enregistrer.
    void refreshDirty();
    // Ctrl+F : chercher partout, aller n'importe ou.
    void openPalette();
    // Un lien de l'article (fil d'Ariane, puce, bouton Copier...).
    void onLink(const std::string& target);
    // La page d'un dossier, ou de toute la bibliotheque (`category` vide).
    void showCategory(const std::string& category);
    [[nodiscard]] std::size_t indexOfEntry(std::string_view name) const;
    [[nodiscard]] std::string writeExport(const std::string& suggested,
                                          const std::string& html);
    // Ecrire `html` a `path` et l'ouvrir ; rend le chemin ("" : echec, dit).
    [[nodiscard]] std::string writeExportTo(std::string path, const std::string& html);

    std::string libsRoot_;
    HelpScope   scope_{HelpScope::All};
    // Lot macros 1 : les formulaires des macros (par nom), et les noms que
    // l'autre onglet montre (true : une macro).
    std::map<std::string, project::macro::MacroSpec> specs_;
    std::map<std::string, bool>                      elsewhere_;
    std::function<void(const help::Target&)>         outOfScope_;
    std::function<void(const std::string&)>          launchMacro_;
    [[nodiscard]] std::string rootLabel() const;

    HelpBar*              toolbar_{nullptr};
    ui::InputText*        filter_{nullptr};
    ui::TreeView*         tree_{nullptr};
    ui::TabControl*       tabs_{nullptr};
    ui::HelpArticleView*  article_{nullptr};
    ui::ListView*         fieldList_{nullptr};
    ui::GroupBox*         editorBox_{nullptr};
    ui::MultiLineText*    editor_{nullptr};
    ui::GroupBox*         previewBox_{nullptr};
    ui::MultiLineText*    preview_{nullptr};
    ui::MultiLineText*    source_{nullptr};
    ui::StatusBar*        status_{nullptr};

    // Les boutons dont l'etat change : on garde leur adresse plutot que de les
    // rechercher par identifiant a chaque rafraichissement.
    PillButton*           back_{nullptr};
    PillButton*           forward_{nullptr};
    PillButton*           star_{nullptr};
    PillButton*           tryIt_{nullptr};
    PillButton*           save_{nullptr};
    PillButton*           revert_{nullptr};
    CommandPalette*       palette_{nullptr};

    // La liste "ce que j'ai garde / ce que je viens de lire", sous l'arbre.
    ui::ListView*         places_{nullptr};
    std::shared_ptr<ui::IListModel> placesModel_;
    std::vector<help::Target>       placeTargets_;

    // Le banc d'essai, detenu par le TabControl.
    BenchPane*            benchPane_{nullptr};
    std::size_t           benchTab_{0};

    std::shared_ptr<LibraryHelpTreeModel> treeModel_;
    std::shared_ptr<HelpFieldListModel>   fieldModel_;

    ui::NodeId            currentNode_{ui::kInvalidNode};
    std::vector<HelpField> fields_;
    ui::RowIndex          currentField_{0};

    // Le brouillon. On n'ecrit jamais directement dans l'entree : tant qu'on
    // n'a pas enregistre, le fichier et l'affichage disent encore ce qu'il y a
    // sur le disque, et Annuler redevient possible.
    project::LibraryHelp  draft_;
    bool                  dirty_{false};
    std::string           lastStatus_;

    // Vrai pendant qu'on remplit la zone de texte par programme. Sans ce
    // verrou, remplir l'editeur en changeant de champ declencherait
    // textChanged, qui ecrirait le texte de l'ancien champ dans le nouveau.
    bool                  loading_{false};

    help::Navigation                  nav_;
    std::shared_ptr<help::TrySession> session_;   // vit tant qu'on lit la page
    // Vrai quand l'article affiche n'est PAS l'aide de l'entree selectionnee :
    // un resultat de recherche, un essai, une page de diagnostic, la visite.
    // Echap revient alors a l'aide plutot que de ne rien faire.
    bool                  transient_{false};
    std::string           lastSearch_;

    help::DossierInfo                                 dossier_;
    std::function<bool(const project::CatalogEntry&)> insertExample_;
    std::function<std::string()>                      sectionName_;
    std::function<std::string(const std::string&)>    projectVersion_;
    std::function<std::string(const std::string&)>    chooseSavePath_;

    core::ConnectionScope links_;
    // Exporter le dossier d'aide : l'explorateur ("Enregistrer sous") repond
    // plus tard - la reponse n'en garde qu'une reference faible.
    std::shared_ptr<char> alive_{std::make_shared<char>('\0')};
};

// L'ecran autonome : l'onglet Macros ("help.macros") ou l'onglet Blocs DFB /
// DDT ("help.blocs") du menu d'aide, sous la barre des quatre onglets.
class LibraryHelpScreen final : public menu::WidgetMenu {
public:
    explicit LibraryHelpScreen(std::string libsRoot, HelpScope scope = HelpScope::Macros);
    [[nodiscard]] std::string title() const override {
        return scope_ == HelpScope::Blocks ? "Aide des blocs" : "Aide des macros";
    }

    // La page, pour que l'hote y pose ses seams apres construction.
    [[nodiscard]] LibraryHelpPage* page() const noexcept { return page_; }
    [[nodiscard]] HelpTabStrip*    tabs() const noexcept { return tabs_; }

protected:
    core::Status buildUi() override;
    void         onEnter() override;
    void         Update(const menu::FrameContext&) override;

private:
    std::string      libsRoot_;
    HelpScope        scope_{HelpScope::Macros};
    LibraryHelpPage* page_{nullptr};
    HelpTabStrip*    tabs_{nullptr};
    core::ConnectionScope links_;
};

// Lot macros 1 : L'ONGLET D'UNE PAGE D'AIDE. Une macro de libs/Macros ->
// "help.macros" ; un bloc, un type, un code de diagnostic, un mot du glossaire
// -> "help.blocs" ; rien de precis -> "help" (l'aide generale).
[[nodiscard]] std::string helpMenuFor(const help::Target& target, const std::string& libsRoot);

// Branche la barre des onglets sur le gestionnaire d'ecrans : un onglet
// REMPLACE l'ecran d'aide, Fermer le depile.
void connectHelpTabs(HelpTabStrip& tabs, menu::MenuManager& menus, core::ConnectionScope& links);

} // namespace app
