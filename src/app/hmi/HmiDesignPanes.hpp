// =============================================================================
//  app/hmi/HmiDesignPanes.hpp - concevoir plus vite : les styles nommes,
//                               rechercher / remplacer (lot 12)
// -----------------------------------------------------------------------------
//  IHM > STYLES (1.23.0 : la maquette validee du 05/11 - un style vise des
//  TYPES d'objets). Trois colonnes :
//   - LES STYLES : d'abord les FOURNIS (ISA-101, Sombre, Clair, Contraste
//     eleve : un cadenas, verrouilles), puis ceux DU PROJET (ranges en
//     dossiers) ; chacun sa pastille de couleurs et le nombre d'objets qui le
//     citent. La recherche lit le nom, la description, les types, les valeurs.
//   - LES TYPES D'OBJETS DU STYLE : un genre de la bibliotheque, un symbole du
//     projet (et << Tous les objets >> d'un style d'avant) ; + Ajouter un
//     type... (un choix avec recherche, BIBLIOTHEQUE puis MES SYMBOLES),
//     - Retirer le type choisi.
//   - LES PROPRIETES IMPOSEES au type choisi, chacune avec la case de
//     l'inspecteur (couleur, nombre, bascule, liste, texte) ; + Ajouter une
//     propriete... (n'importe laquelle du type, sa valeur d'objet neuf),
//     - Retirer la propriete. Dessous, l'APERCU : un type au choix, dessine
//     par le vrai painter avec le style (un equipement a etats : en marche, a
//     l'arret, en defaut).
//  Barre : Nouveau, Dupliquer a partir de... (tout style, fourni compris : une
//  copie du projet, a renommer), Renommer, Retirer, Dossier. Un style fourni se
//  lit seulement (un bandeau le dit). Une valeur changee : les objets qui
//  citent le style suivent, sauf ceux qui l'avaient changee eux-memes ;
//  renomme, ils le citent sous son nouveau nom ; retire, ils gardent leurs
//  valeurs. Chaque changement est une commande (Ctrl+Z).
//
//  IHM > RECHERCHER / REMPLACER. Un texte, son remplacant, ou chercher (tout
//  le projet ou une vue), la casse, le mot entier : l'APERCU liste chaque champ
//  touche (ou, avant, apres) avant qu'on remplace. Remplacer tout : une
//  commande. Double-clic sur une ligne : la vue, l'objet choisi.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "HmiFolderTable.hpp"          // lot 21 : les styles ranges en dossiers
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiDeclEdit.hpp"   // 1.14.0 (lot M1) : decledit::globalUsages (Voir les usages d'une variable)
#include "../../hmi/HmiDesign.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/SearchField.hpp"   // lot API 8 : chercher un style

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui { class StatusBar; }

namespace app {

// 1.23.0 : LE CHOIX AVEC RECHERCHE de l'onglet Styles (+ Ajouter un type..., + Ajouter une propriete...) : une liste
// deroulee sous son bouton - un titre, un champ de recherche, des lignes rangees en groupes (BIBLIOTHEQUE, MES
// SYMBOLES). Haut / Bas, Entree, Echap (le champ rend la main) ; un clic ailleurs la ferme. `chosen` : la valeur de la
// ligne choisie (la liste est deja fermee : le volet peut en rouvrir une).
class HmiStyleChooser final : public ui::Widget {
public:
    struct Entry {
        std::string value;    // ce que le choix rend ("Pump", "symbole:Sym_Moteur", "colorOn")
        std::string label;    // ce qu'on lit ("Pompe")
        std::string group;    // son groupe ("BIBLIOTHEQUE") ; les lignes d'un meme groupe se suivent
        std::string detail;   // a droite, en gris (la valeur d'un objet neuf) ; vide : rien
    };
    explicit HmiStyleChooser(std::string id);
    // Ouvert sous `anchor` (au-dessus s'il n'y a pas la place), garde dans `area` ; le champ de recherche prend la main.
    void open(std::string title, std::vector<Entry> entries, gfx::Rect anchor, gfx::Rect area);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    void setSearch(const std::string& text);
    [[nodiscard]] const std::string& searchText() const;
    // Ce qui est montre, dans l'ordre : les groupes ("# BIBLIOTHEQUE") et les lignes (leur libelle) ; les valeurs seules.
    [[nodiscard]] std::vector<std::string> shownLines() const;
    [[nodiscard]] std::vector<std::string> shownValues() const;
    [[nodiscard]] std::string highlighted() const;              // la valeur en surbrillance ("" : aucune)
    void moveHighlight(int delta);
    bool choose(std::string_view value);                        // comme un clic sur sa ligne ; faux : pas montree
    bool chooseHighlighted();                                   // Entree
    [[nodiscard]] bool rowRect(std::string_view value, gfx::Rect& out) const;   // sa ligne a l'ecran (essais, scripts)
    [[nodiscard]] ui::InputText& field() noexcept { return *field_; }
    [[nodiscard]] gfx::Rect eventBounds() const override;       // ouverte : toute la zone (un clic ailleurs la ferme)
    const core::SignalPtr<const std::string&> chosen = core::Signal<const std::string&>::create();
protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    struct Line { bool group{false}; std::size_t entry{0}; };
    void filter();
    [[nodiscard]] gfx::Rect listRect() const;
    [[nodiscard]] int lineAt(gfx::Point) const;
    void reveal(int line);
    std::string        title_;
    std::vector<Entry> entries_;
    std::vector<Line>  lines_;
    int                hot_{-1};
    float              scroll_{0.f};
    bool               open_{false};
    gfx::Rect          area_{};
    ui::InputText*     field_{nullptr};
    core::ConnectionScope links_;
};

class HmiStylesPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    enum : int { ActNew = 1, ActDuplicate, ActRename, ActRemove, ActFolder };   // la barre
    HmiStylesPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();

    // ---- les styles (une commande chacun ; les gestes des boutons, des scripts et des essais) ----
    hmi::Id addStyle(const std::string& name = "Style");     // un style vide, au nom libre (jamais celui d'un fourni)
    hmi::Id duplicateStyle(hmi::Id style);                    // un style du projet
    // 1.23.0 : DUPLIQUER A PARTIR DE - n'importe quel style par son nom, fourni compris : une copie du projet, modifiable
    // (nom unique ; `newName` vide : "<nom> (copie)").
    hmi::Id duplicateFrom(const std::string& name, const std::string& newName = {});
    bool    removeStyle(hmi::Id style);
    bool    renameStyle(hmi::Id style, const std::string& name, std::string* why = nullptr);
    // "nom", "description", ou une propriete (fill, fontSize...) : pour le type choisi dans le volet (le style etant le
    // style choisi), sinon le premier type du style qui la porte, sinon tous les objets ; une valeur vide : le style ne
    // la porte plus.
    bool    setStyleField(hmi::Id style, const std::string& key, const std::string& value, std::string* why = nullptr);

    // ---- 1.23.0 : les types du style choisi et ce qu'ils imposent (une commande chacun ; les objets qui citent le
    // style suivent - styles::propagate) ----
    bool addType(const std::string& type, std::string* why = nullptr);
    bool removeType(const std::string& type, std::string* why = nullptr);
    bool addProperty(const std::string& key, std::string* why = nullptr);    // au type choisi, sa valeur d'objet neuf
    bool removeProperty(const std::string& key, std::string* why = nullptr);
    // La valeur imposee a un type (un style du projet) ; la case de la propriete la valide selon son genre.
    bool setTypeValue(hmi::Id style, const std::string& type, const std::string& key, const std::string& value,
                      std::string* why = nullptr);

    // ---- le choix ----
    [[nodiscard]] hmi::Id selectedStyle() const;               // un style du projet ; kNoId : aucun, ou un fourni
    [[nodiscard]] const hmi::Style* currentStyle() const;      // le style choisi, fourni compris ; nul : aucun
    [[nodiscard]] bool locked() const;                         // le style choisi est un style fourni (verrouille)
    void selectStyle(hmi::Id);
    bool selectBuiltin(std::string_view name);
    [[nodiscard]] const std::string& selectedType() const noexcept { return type_; }
    void selectType(const std::string& type);
    [[nodiscard]] const std::string& selectedProperty() const noexcept { return prop_; }
    void selectProperty(const std::string& key);
    [[nodiscard]] std::vector<std::string> shownTypes() const { return typeRows_; }   // la colonne des types, dans l'ordre
    // Le libelle d'une propriete dans la liste des proprietes imposees (celui de l'inspecteur).
    [[nodiscard]] static std::string propertyName(std::string_view type, std::string_view key);

    // ---- les choix avec recherche ----
    void openTypeChooser();
    void openPropertyChooser();
    [[nodiscard]] HmiStyleChooser& chooser() noexcept { return *chooser_; }
    // Ce que chacun propose : les types qu'on peut ajouter au style choisi (pas ceux qu'il a), les proprietes du type
    // choisi qu'il n'impose pas encore.
    [[nodiscard]] std::vector<HmiStyleChooser::Entry> typeChoices() const;
    [[nodiscard]] std::vector<HmiStyleChooser::Entry> propertyChoices() const;

    // ---- l'apercu ----
    // Les objets d'apercu d'un style pour un type : un objet neuf fait par le style (styles::previewObject) ; un
    // equipement a etats en trois exemplaires (en marche, a l'arret, en defaut ; deux sans defaut), poses cote a cote.
    struct PreviewItem { hmi::Object object; std::string caption; };
    [[nodiscard]] static std::vector<PreviewItem> previewItems(const hmi::Project&, const hmi::Style&, std::string_view type);
    [[nodiscard]] const std::string& previewType() const noexcept { return previewType_; }
    void setPreviewType(const std::string& type);
    [[nodiscard]] std::vector<std::string> previewTypeChoices() const;   // la liste Type : (ceux du style en tete)
    [[nodiscard]] ui::Widget&   preview() noexcept { return *preview_; }
    [[nodiscard]] ui::DropDown& previewTypes() noexcept { return *previewTypes_; }

    // ---- les widgets (essais, scripts) ----
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *grid_; }
    [[nodiscard]] ui::TableView&    table() noexcept { return *table_; }          // les styles du projet
    [[nodiscard]] ui::TableView&    builtinTable() noexcept { return *builtins_; }   // 1.23.0 : les styles fournis
    [[nodiscard]] ui::TableView&    typesTable() noexcept { return *types_; }       // 1.23.0 : les types du style
    [[nodiscard]] ui::Button&       addTypeButton() noexcept { return *addType_; }
    [[nodiscard]] ui::Button&       removeTypeButton() noexcept { return *removeType_; }
    [[nodiscard]] ui::Button&       addPropertyButton() noexcept { return *addProp_; }
    [[nodiscard]] ui::Button&       removePropertyButton() noexcept { return *removeProp_; }
    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] HmiFolderTable&   folders() noexcept { return *folders_; }     // lot 21
    // Le bandeau d'un style fourni ("" : aucun).
    [[nodiscard]] std::string lockedBanner() const;
    // Lot API 8 : chercher un style (son nom, sa description, ses types, ses valeurs, son dossier) ; "2 sur 9",
    // retenue d'une seance a l'autre.
    [[nodiscard]] ui::SearchField&  search() noexcept { return *search_; }
    [[nodiscard]] std::size_t shownStyles() const noexcept { return shown_; }
protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
private:
    void rebuildBuiltins();
    void styleChanged();
    void rebuildTypes();
    void rebuildProperties();
    void rebuildPreviewTypes();
    void updateButtons();
    void beginRename(hmi::Id style);
    void say(std::string text, bool error = false);
    [[nodiscard]] bool matchesSearch(const hmi::Style&) const;
    [[nodiscard]] const hmi::Style* editable(std::string* why) const;   // le style choisi, s'il se modifie (sinon : pourquoi)
    bool dropKey(hmi::Id style, const std::string& type, const std::string& key, std::string* why);   // ne plus imposer
    hmi::DocumentPtr      doc_;
    Apply                 apply_;
    ui::Widget*           preview_{nullptr};    // 1.12.27
    ui::DropDown*         previewTypes_{nullptr};   // 1.23.0 : Type :
    HmiToolStrip*         tools_{nullptr};
    ui::SearchField*      search_{nullptr};     // lot API 8
    std::size_t           shown_{0};            // lot API 8 : les styles montres
    ui::TableView*        builtins_{nullptr};   // 1.23.0
    ui::TableView*        table_{nullptr};
    ui::TableView*        types_{nullptr};      // 1.23.0
    ui::Button*           addType_{nullptr};
    ui::Button*           removeType_{nullptr};
    ui::Button*           addProp_{nullptr};
    ui::Button*           removeProp_{nullptr};
    ui::PropertyGrid*     grid_{nullptr};
    ui::StatusBar*        status_{nullptr};
    HmiStyleChooser*      chooser_{nullptr};    // 1.23.0
    int                   chooserFor_{0};       // 1 : un type ; 2 : une propriete
    std::unique_ptr<HmiFolderTable> folders_;       // lot 21
    std::vector<std::string> builtinRows_;      // les noms des styles fournis montres
    std::vector<std::string> typeRows_;         // les types montres
    std::string           builtin_;             // le style fourni choisi ("" : un style du projet, ou aucun)
    std::string           styleKey_;            // le style dont le volet montre les types (pour garder le type choisi)
    std::string           type_, prop_, previewType_;
    // La mise en page (onLayout) : les trois colonnes, leurs titres, le bandeau d'un style fourni.
    gfx::Rect             colStyles_{}, colTypes_{}, colProps_{}, banner_{};
    std::string           message_;
    core::ConnectionScope links_;
};

class HmiFindPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiFindPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    // Chercher : l'apercu se remplit. Remplacer tout : une commande ; le nombre
    // de champs changes.
    void search();
    std::size_t replaceAll();
    void setFind(const std::string& text);
    void setReplace(const std::string& text);
    void setScopeView(hmi::Id view);            // kNoId : tout le projet
    void setMatchCase(bool on);
    void setWholeWord(bool on);
    [[nodiscard]] const std::vector<hmi::design::FindHit>& hits() const noexcept { return hits_; }
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    // Double-clic sur une ligne : ouvrir la vue (et choisir l'objet).
    const core::SignalPtr<hmi::Id, hmi::Id> openHit = core::Signal<hmi::Id, hmi::Id>::create();
    // 1.14.0 (lot M1) : VOIR LES USAGES d'une variable IHM (le clic droit de l'arbre du projet) - le champ prend son nom
    // et la liste montre ses utilisations, lues comme la colonne Utilisations les lit (hmi::decledit::globalUsages : la
    // racine et ses acces, Cuves[i].Niveau ; ni une chaine, ni un commentaire, ni le membre homonyme d'une autre variable,
    // ni un code qui declare le meme nom) - pas les occurrences du texte. Double-clic : y aller (openUsage). Taper un autre
    // texte revient a la recherche.
    void showUsages(const std::string& name);
    [[nodiscard]] bool showingUsages() const noexcept { return !usagesOf_.empty(); }
    [[nodiscard]] const std::vector<hmi::decledit::Usage>& usages() const noexcept { return usages_; }
    const core::SignalPtr<const hmi::decledit::Usage&> openUsage = core::Signal<const hmi::decledit::Usage&>::create();
protected:
    void onLayout() override;
private:
    void refreshScopes();
    [[nodiscard]] hmi::design::FindOptions options() const;
    void say(std::string text, bool error = false);
    hmi::DocumentPtr                   doc_;
    Apply                              apply_;
    ui::InputText*                     find_{nullptr};
    ui::InputText*                     replace_{nullptr};
    ui::DropDown*                      scope_{nullptr};
    ui::Checkbox*                      case_{nullptr};
    ui::Checkbox*                      word_{nullptr};
    ui::Button*                        searchButton_{nullptr};
    ui::Button*                        replaceButton_{nullptr};
    ui::TableView*                     table_{nullptr};
    ui::StatusBar*                     status_{nullptr};
    std::vector<hmi::Id>               scopes_;       // kNoId, puis les vues
    std::vector<hmi::design::FindHit>  hits_;
    std::string                        usagesOf_;      // 1.14.0 : le nom dont la liste montre les usages ("" : une recherche)
    std::vector<hmi::decledit::Usage>  usages_;
    void listUsages();
    std::string                        message_;
    core::ConnectionScope              links_;
};

} // namespace app
