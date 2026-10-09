// =============================================================================
//  app/hmi/HmiDeclGrid.hpp - 1.11.18 (refonte des scripts, lot 5) : LES ONGLETS
//  DE DECLARATIONS
// -----------------------------------------------------------------------------
//  Une grille par onglet - Constantes, Variables (Locales d'une fonction ou d'un
//  operateur), Parametres - sur les declarations du modele d'un code (un script
//  general ou de vue, une fonction IHM ou de symbole, un operateur). Les volets des
//  scripts, des fonctions et des operateurs la posent a cote de l'onglet Code :
//
//    +-- [+ Ajouter] [- Supprimer] [Dupliquer] [^] [v] [Utilisations] [Rechercher] --+
//    | Nom        | Type  | Initiale | Stockage  | Visibilite | Utilisations | Doc.  |
//    | Compteur   | INT   | 0        | Conservee | Public     |            3 | ...   |
//    | Seuil      | REAL  | 12.5     | Execution | Prive      |            0 | ...   |
//    +-- Stockage : Execution (remise a chaque execution) . Conservee . Persistante --+
//
//  ECRIRE DANS LA CASE : double-clic, F2 ou Entree ; le Type, le Stockage, le Mode
//  et la Visibilite se choisissent dans une liste (Type : "Autre type..." ouvre un
//  champ libre - ARRAY[1..10] OF REAL, REF_TO T). Chaque geste est une commande
//  (Ctrl+Z) qui valide d'abord (hmi::decledit) : un nom pris, un type inconnu, une
//  constante sans valeur sont refuses et la barre dit pourquoi. Renommer renomme
//  les utilisations dans le code. Une declaration fautive (un nom aussi declare
//  dans un bloc VAR du code...) a son nom en rouge, la raison en infobulle.
//
//  LE CLAVIER : Inser ajoute, Suppr supprime, Ctrl+D duplique, Alt+Haut / Alt+Bas
//  deplacent (l'ordre des parametres est la signature). La recherche filtre les
//  lignes (tous les mots, sans casse ni accents).
//
//  EXCEL : Ctrl+C copie les lignes choisies avec leurs titres (tabulations : Excel
//  les colle en cases) ; Ctrl+V colle un tableau d'Excel - les colonnes reconnues
//  par leur titre (Nom, Type, Valeur, Stockage, Mode, Visibilite, Documentation,
//  en francais ou en anglais, dans n'importe quel ordre), un nom existant mis a
//  jour, un nom nouveau cree ; Ctrl+Maj+V : en nouvelles lignes. Un seul Ctrl+Z
//  pour tout le collage, les cases refusees marquees (app/TablePaste.hpp).
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../TablePaste.hpp"
#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiDeclEdit.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ui { class SearchField; }

namespace app {

class HmiDeclGrid final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiDeclGrid(std::string id, hmi::DocumentPtr doc, Apply apply, hmi::decledit::Tab tab);
    ~HmiDeclGrid() override;

    // Le code montre ; vide : aucun (la grille vide, grisee, `why` dans sa barre -
    // "Cet evenement n'a pas encore de script...").
    void setPlace(std::optional<hmi::decledit::Place> place, std::string why = {});
    [[nodiscard]] const std::optional<hmi::decledit::Place>& place() const noexcept { return place_; }
    // Relit le projet (le volet l'appelle a chaque modification) : la selection et le
    // defilement restent.
    void refresh();

    [[nodiscard]] hmi::decledit::Tab tab() const noexcept { return tab_; }
    // "Constantes", "Variables", "Locales", "Parametres" (accentues) - selon le code montre.
    [[nodiscard]] std::string title() const;
    [[nodiscard]] std::size_t count() const noexcept;        // les declarations de l'onglet
    [[nodiscard]] std::size_t faultCount() const noexcept;   // ... fautives
    // Le code montre accepte-t-il cet onglet ? (Faux : un script C, un parametre de script...)
    [[nodiscard]] bool usable() const;

    // Les gestes (les boutons, le clavier, les essais). Faux, et lastMessage() : refuse.
    bool add();
    bool removeSelected();
    bool duplicateSelected();
    bool moveSelected(int delta);
    // Ecrire la case `column` de la ligne `row` (une ligne du modele, dans l'ordre du code).
    bool setCell(std::size_t row, hmi::decledit::Column column, const std::string& text);
    // 1.11.19 (lot 6) : "Choisir un type..." dans la case Type - le selecteur de types (par son
    // hote, app::typepicker) ; le type choisi va a la declaration (retrouvee par son identifiant :
    // la grille a pu changer entre-temps). Sans hote : la barre le dit.
    void pickType(std::size_t row);
    bool setTypeOf(hmi::Id declaration, const std::string& type);
    [[nodiscard]] std::vector<std::size_t> selectedRows() const;
    void selectRows(const std::vector<std::size_t>& rows);
    // Choisir la declaration `name` (sans casse) ; faux : pas dans cet onglet.
    bool selectName(const std::string& name);
    // La colonne de la table qui montre `column` (-1 : pas dans cet onglet).
    [[nodiscard]] int tableColumnOf(hmi::decledit::Column column) const;
    // La colonne Utilisations (-1 : aucune).
    [[nodiscard]] int usesColumn() const noexcept { return usesColumn_; }

    [[nodiscard]] ui::TableView&     table() noexcept { return *table_; }
    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::StatusBar&     hintBar() noexcept { return *hint_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

    // Aller aux utilisations du nom choisi (le volet montre l'onglet Code et la suivante).
    const core::SignalPtr<const std::string&> usesRequested = core::Signal<const std::string&>::create();
    // Un message pour la barre d'etat du volet (texte, avertissement).
    const core::SignalPtr<const std::string&, bool> message = core::Signal<const std::string&, bool>::create();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    class Rows;
    // Un geste dans une commande : `edit` valide puis modifie (faux et `why` : rien).
    bool change(const std::string& label, const std::function<bool(hmi::Project&, std::string*)>& edit);
    void say(std::string text, bool warning);
    void rebuildColumns();
    void applySearch();
    [[nodiscard]] std::string noun(bool plural) const;          // "la constante", "les locales"...
    [[nodiscard]] bool feminine() const noexcept { return tab_ != hmi::decledit::Tab::Parameters; }   // ajoutee / ajoute
    [[nodiscard]] std::string ownerLabel() const;               // "Compter", "Vanne.Ouvrir"...
    [[nodiscard]] paste::Target pasteTarget(const ui::TableView::PasteRequest&);

    hmi::DocumentPtr       doc_;
    Apply                  apply_;
    hmi::decledit::Tab     tab_;
    std::optional<hmi::decledit::Place> place_;
    std::string            placeWhy_;
    hmi::decl::Role        role_{hmi::decl::Role::Script};
    bool                   haveColumns_{false};
    HmiToolStrip*          tools_{nullptr};
    ui::SearchField*       search_{nullptr};
    ui::TableView*         table_{nullptr};
    ui::StatusBar*         hint_{nullptr};
    std::shared_ptr<Rows>  rows_;
    std::vector<int>       columns_;           // par colonne de la table : la Column, ou -1 (Utilisations)
    int                    usesColumn_{-1};
    std::string            message_;
    paste::Binding         paste_;
    std::shared_ptr<int>   alive_{std::make_shared<int>(0)};   // le selecteur repond apres coup : la grille vit-elle encore ?
    core::ConnectionScope  links_;
};

// L'ANCIEN FORMAT, AU-DESSUS DU CODE : un bandeau quand le code montre declare encore
// ses variables dans son texte (VAR ... END_VAR) - "Migrer ce code" les passe dans les
// onglets (hmi::migrate, une commande). Cache sinon.
class HmiDeclBanner final : public ui::Widget {
public:
    explicit HmiDeclBanner(std::string id);
    void setText(std::string text);                 // vide : le bandeau se cache
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] bool shown() const noexcept { return !text_.empty(); }
    [[nodiscard]] ui::Button& button() noexcept { return *button_; }
    const core::SignalPtr<> migrate = core::Signal<>::create();
    static constexpr float kHeight = 30.f;
protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
private:
    std::string           text_;
    ui::Button*           button_{nullptr};
    core::ConnectionScope links_;
};

// LES ONGLETS D'UN EDITEUR DE CODE : "Code" (la page donnee : l'editeur, sa barre et le
// bandeau de l'ancien format), puis une grille par onglet de declarations du code montre
// (un script : Constantes, Variables ; une fonction : Parametres, Locales, Constantes ; un
// operateur : Locales, Constantes). Leurs titres comptent les declarations (rouge : une
// fautive). Les volets des scripts, des fonctions et des operateurs s'en servent.
class HmiCodeTabs final : public ui::TabControl {
public:
    HmiCodeTabs(std::string id, hmi::DocumentPtr doc, HmiDeclGrid::Apply apply, ui::WidgetPtr codePage,
                const std::vector<hmi::decledit::Tab>& tabs, HmiDeclBanner* banner = nullptr);
    // Le code montre (vide : aucun, `why` dans les grilles) : les grilles, leurs titres, le bandeau.
    void setPlace(std::optional<hmi::decledit::Place> place, std::string why = {});
    // La grille d'un onglet (nulle : cet editeur ne l'a pas) ; son indice d'onglet (0 : Code).
    [[nodiscard]] HmiDeclGrid* grid(hmi::decledit::Tab tab) const noexcept;
    [[nodiscard]] std::size_t  indexOf(hmi::decledit::Tab tab) const noexcept;
    void showCode() { setCurrentIndex(0); }
    [[nodiscard]] bool codeShown() const noexcept { return currentIndex() == 0; }
    // Montrer une declaration : son onglet, sa ligne choisie. Faux : inconnue.
    bool showDeclaration(const std::string& name);
    [[nodiscard]] HmiDeclBanner* banner() const noexcept { return banner_; }

    // Des grilles : un message pour la barre du volet ; les utilisations d'un nom. Du
    // bandeau : "Migrer ce code".
    const core::SignalPtr<const std::string&, bool> message = core::Signal<const std::string&, bool>::create();
    const core::SignalPtr<const std::string&>       usesRequested = core::Signal<const std::string&>::create();
    const core::SignalPtr<>                         migrateRequested = core::Signal<>::create();

private:
    hmi::DocumentPtr doc_;
    std::vector<std::pair<hmi::decledit::Tab, HmiDeclGrid*>> grids_;
    HmiDeclBanner*        banner_{nullptr};
    core::ConnectionScope links_;
};

// LES UTILISATIONS DANS L'EDITEUR : la suivante apres le curseur (la premiere apres la
// derniere), selectionnee ; `said` : "Utilisation 2 sur 5 de Compteur (ligne 12)" ou
// pourquoi il n'y en a pas. Les volets des scripts, des fonctions et des operateurs.
bool selectNextUse(ui::MultiLineText& editor, const std::string& name, std::string* said);
// Le nom d'une declaration dans le message d'une faute ("declaration << Max >> : ...") ;
// vide : la faute n'est pas celle d'une declaration.
[[nodiscard]] std::string declarationNamed(std::string_view message);

// Le texte du bandeau d'un code (vide : rien a migrer) : ce que la migration ferait, ou
// pourquoi elle ne le peut pas. `place` : le code ; un code C ou C++ n'en a jamais.
[[nodiscard]] std::string declBannerText(const hmi::Project&, const hmi::decledit::Place& place);
// "Migrer ce code" : la migration de ce seul code (une commande, Ctrl+Z la reprend).
// Faux et `why` : rien a migrer, ou le code ne peut pas l'etre (la raison du plan).
bool migrateOne(const hmi::DocumentPtr& doc, const std::function<void(core::CommandPtr)>& apply, const hmi::decledit::Place& place,
                std::string* report);

} // namespace app
