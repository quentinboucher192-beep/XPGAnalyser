// =============================================================================
//  app/LibraryHelpModels.hpp - la bibliotheque vue par l'aide
// -----------------------------------------------------------------------------
//  Le seam habituel : rien dans ui/ ne sait ce qu'est un DFB, rien dans
//  project/ ne sait qu'il existe un arbre. Ce fichier fait la traduction.
//
//  L'IDEE CENTRALE EST LA LISTE DE CHAMPS.
//
//  Un editeur d'aide pourrait avoir une case "Resume", une case "Utilisation",
//  une case par parametre... et il faudrait le rouvrir chaque fois qu'un bloc
//  gagne un parametre. On fait l'inverse : on demande au fichier de quoi il est
//  fait, et on en deduit la liste des choses a documenter. Ajouter un parametre
//  a un .ddt ajoute une ligne dans l'editeur, sans toucher a l'editeur.
//
//  C'est aussi ce qui rend l'editeur utilisable sur les bibliotheques qu'on n'a
//  pas ecrites : un bloc venu d'ailleurs expose ses champs comme les autres.
// =============================================================================
#pragma once

#include "../project/LibraryCatalog.hpp"
#include "../project/MacroSpec.hpp"
#include "../ui/widgets/DataViews.hpp"
#include "../ui/widgets/HelpArticleView.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace app {

// --------------------------------------------------------------- les champs --
// Une chose documentable, quelle qu'elle soit.
struct HelpField {
    enum class Kind : std::uint8_t {
        Summary, Usage, Example, Since, Author, See, Param, Fault, Unknown,
    };
    Kind        kind{Kind::Summary};
    std::string key;      // le nom du parametre, ou le code du defaut
    std::string label;    // ce qui s'affiche dans la liste
    std::string hint;     // le type et la portee, ou ce que dit la table
    bool        multiline{true};
};

// Tous les champs d'une entree, dans l'ordre ou on les remplit : ce que c'est,
// comment s'en sert, puis les parametres, puis les defauts.
[[nodiscard]] std::vector<HelpField> helpFields(const project::CatalogEntry&);

[[nodiscard]] std::string readField(const project::LibraryHelp&, const HelpField&);

// Recompose une prose pre-coupee : une ligne qui ne finit pas par une
// ponctuation forte est la suite de la suivante. Expose pour etre verifiable.
[[nodiscard]] std::string reflow(std::string_view texte);

// L'inverse, pour l'editeur : coupe a `colonnes` sans couper de mot.
//
// POURQUOI L'EDITEUR COUPE LE TEXTE. Le MultiLineText de ce projet expose
// setWordWrap() mais ne l'implemente pas : une prose tapee d'un seul tenant
// s'ecrit sur une ligne unique, que l'on edite en defilant horizontalement.
// Plutot que de reecrire l'editeur de texte du projet pour une fenetre
// d'aide, on ecrit des lignes courtes - ce que fait deja, a la main, tout
// fichier de bibliotheque - et reflow() les recompose a l'affichage. Le
// fichier reste lisible dans un editeur de texte, ce qui est la moitie de
// l'interet de ranger l'aide dedans.
[[nodiscard]] std::string softWrap(std::string_view texte, std::size_t colonnes = 78);
void                      writeField(project::LibraryHelp&, const HelpField&, std::string value);

// ------------------------------------------------------------ les familles ---
// L'indice de famille d'un dossier de libs/ (0 Alarmes, 1 Controle, 2
// Equipement, 3 E/S, 4 Macros, 5 autre), tire du NOM et non du rang : ajouter un
// dossier ne repeint pas les autres.
[[nodiscard]] int familyIndex(std::string_view category) noexcept;
// Le nom d'un dossier pour l'ecran : "Equipment" -> "Equipements".
[[nodiscard]] std::string categoryLabel(std::string_view category);
// Le genre, en toutes lettres : "Bloc fonction derive".
[[nodiscard]] std::string_view kindName(project::CatalogKind) noexcept;

// ------------------------------------------------------------------ la prose ---
// Ce qu'une aide ecrite dans un fichier contient sans le dire : des
// paragraphes, des listes, des tableaux alignes a l'espace. Expose pour etre
// verifiable, et pour l'export qui veut la meme lecture.
struct ProsePart {
    enum class Kind : std::uint8_t { Paragraph, Item, Table };
    Kind        kind{Kind::Paragraph};
    std::string label;   // Item : "0", "1", "-"
    std::string term;    // Item : le terme defini ("ordre fixe"), s'il y en a un
    std::string text;    // Paragraph : recompose par reflow ; Item : la definition ;
                         // Table : lignes separees par \n, cellules par \t
};
[[nodiscard]] std::vector<ProsePart> structureProse(std::string_view raw);

// --------------------------------------------------------------- l'article ---
// Lot macros 1 : ce que la page sait et que le fichier ne dit pas.
struct ArticleContext {
    std::string rootLabel;                              // vide : "Bibliotheques"
    bool        byFolder{false};                        // le fil d'Ariane suit `folder`, pas la categorie
    std::string folder;                                 // une macro : son dossier ("A/B" ; "" : la racine)
    const project::macro::MacroSpec* spec{nullptr};     // une macro : son formulaire
    bool        canLaunch{false};                       // le lien " Lancer " (run:<nom>)
};

// Le document mis en page par HelpArticleView. Une macro dont on donne le
// formulaire (`context.spec`) dit en plus ce qu'elle lit, ce qu'elle produit,
// les macros qu'elle enchaine, et ce qu'elle demande, champ par champ.
[[nodiscard]] ui::HelpArticle buildHelpArticle(const project::CatalogEntry&,
                                               const ArticleContext& context = {});

// La page d'un dossier (ou de toute la bibliotheque si `category` est vide) :
// ce qu'il contient, en puces cliquables colorees par leur couverture.
[[nodiscard]] ui::HelpArticle buildLibraryOverview(const std::vector<project::CatalogEntry>&,
                                                   std::string_view category = {},
                                                   std::string_view rootLabel = {});

// Lot macros 1 : la page d'un dossier de macros ("" : toutes). `folders` donne
// le dossier de chaque entree (meme ordre) ; les sous-dossiers ont leur titre.
[[nodiscard]] ui::HelpArticle buildFolderOverview(const std::vector<project::CatalogEntry>&,
                                                  const std::vector<std::string>& folders,
                                                  std::string_view folder, std::string_view rootLabel);

// Le texte d'une entree sans son aide, pour l'onglet Source.
[[nodiscard]] std::string sourceOf(const project::CatalogEntry&);

// ------------------------------------------------------------------ l'arbre --
// Lot macros 1 : l'arbre d'un onglet de l'aide.
struct TreeOptions {
    std::string              rootLabel;     // vide : "Bibliotheques"
    // Les dossiers de l'onglet Macros : un chemin par entree ("A/B" ; "" : a la
    // racine), dans l'ordre des entrees. Vide : un dossier par categorie de libs/.
    std::vector<std::string> folders;
    std::vector<std::string> folderOrder;   // l'ordre des dossiers, parents d'abord
};

class LibraryHelpTreeModel final : public ui::ITreeModel {
public:
    explicit LibraryHelpTreeModel(std::vector<project::CatalogEntry> entries, TreeOptions options = {});

    [[nodiscard]] ui::NodeId    root() const override;
    [[nodiscard]] std::size_t   childCount(ui::NodeId) const override;
    [[nodiscard]] ui::NodeId    childAt(ui::NodeId, std::size_t) const override;
    [[nodiscard]] bool          hasChildren(ui::NodeId) const override;
    [[nodiscard]] std::string   text(ui::NodeId) const override;
    [[nodiscard]] ui::CellStyle style(ui::NodeId) const override;

    // L'entree derriere un noeud, ou nullptr si le noeud est un dossier.
    [[nodiscard]] const project::CatalogEntry* entry(ui::NodeId) const;
    // Le dossier derriere un noeud : son nom pour une categorie, "" pour la
    // racine, rien pour une entree.
    [[nodiscard]] std::optional<std::string> folderOf(ui::NodeId) const;
    [[nodiscard]] project::CatalogEntry*       mutableEntry(ui::NodeId);
    [[nodiscard]] ui::NodeId nodeForEntry(std::size_t index) const;
    [[nodiscard]] const std::vector<project::CatalogEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] std::size_t entryCount() const noexcept { return entries_.size(); }

    // Le filtre de la barre de recherche. Un noeud correspond quand son nom, sa
    // categorie ou son resume contient le terme ; TreeView garde les ancetres
    // des correspondances, donc les dossiers restent visibles.
    [[nodiscard]] bool matches(ui::NodeId, std::string_view term) const;

    // Le dossier d'une entree (mode dossiers : "A/B" ; sinon sa categorie).
    [[nodiscard]] std::string folderOfEntry(std::size_t index) const;
    [[nodiscard]] const std::vector<std::string>& entryFolders() const noexcept { return entryFolder_; }
    [[nodiscard]] bool byFolder() const noexcept { return byFolder_; }
    [[nodiscard]] const std::string& rootLabel() const noexcept { return rootLabel_; }
    // Le noeud d'un dossier (son chemin), kInvalidNode s'il n'y en a pas.
    [[nodiscard]] ui::NodeId nodeForFolder(std::string_view path) const;

private:
    struct Folder {
        std::string              path;       // "A/B" ; en mode categories : la categorie
        std::string              label;      // ce que l'arbre affiche
        int                      parent{-1};
        std::vector<std::size_t> subfolders;
        std::vector<std::size_t> items;
        int                      family{5};
        std::size_t              deepCount{0};
    };
    std::size_t ensureFolder(const std::string& path);

    std::vector<project::CatalogEntry>  entries_;
    std::vector<Folder>                 folders_;
    std::vector<std::size_t>            rootFolders_, rootItems_;
    std::vector<std::string>            entryFolder_;
    std::string                         rootLabel_;
    bool                                byFolder_{false};
};

// ------------------------------------------------------- la liste de champs --
// Le brouillon est detenu par l'ecran : le modele n'en garde qu'un pointeur,
// parce que le meme brouillon est edite par la zone de texte a cote.
class HelpFieldListModel final : public ui::IListModel {
public:
    void setFields(std::vector<HelpField> fields, const project::LibraryHelp* draft);

    [[nodiscard]] std::size_t rowCount() const override { return fields_.size(); }
    [[nodiscard]] std::string text(ui::RowIndex) const override;
    [[nodiscard]] ui::CellStyle style(ui::RowIndex) const override;

    [[nodiscard]] const HelpField* at(ui::RowIndex) const;

private:
    std::vector<HelpField>       fields_;
    const project::LibraryHelp*  draft_{nullptr};
};

} // namespace app
