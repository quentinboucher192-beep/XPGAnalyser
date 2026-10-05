// =============================================================================
//  app/hmi/HmiVariablePanes.hpp - les variables IHM et les types IHM (lot 16)
// -----------------------------------------------------------------------------
//  DEUX ONGLETS DE LA PROGRAMMATION GENERALE.
//
//  VARIABLES IHM : un tableau en arbre.
//    - LES DOSSIERS rangent les variables comme les filtres de Visual Studio :
//      sans effet sur leur nom (Vitesse reste Vitesse dans Ligne/Convoyeur).
//      Un dossier se cree, se renomme (double-clic), s'imbrique ; une variable
//      change de dossier par la grille (Dossier) ou en la glissant dans l'arbre.
//    - UNE STRUCTURE OU UN TABLEAU se deplie : ses membres, ses cases, et leur
//      adresse quand la variable est liee.
//    - LES COLONNES DE LA LIAISON (Equipement, Adresse, Acces) s'editent dans la
//      case : double-clic ou F2. Ce sont les memes donnees que Equipements >
//      Variables liees : une modification ici se voit la-bas, et l'inverse.
//      L'adresse d'un membre se corrige dans sa ligne (vide : la place calculee).
//
//    +------------------------------ outils -------------------------------+
//    | Dossier [v]  Rechercher...   [x] deplier  [ ] seulement les liees     |
//    | Nom | Type | Initiale | Equipement | Adresse | Acces | Place | Qualite| grille
//    +------------------------------ etat ---------------------------------+
//
//  TYPES IHM : les structures (un tableau des types, un tableau de leurs
//  membres, la grille), comme les DDT de l'automate.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "HmiFolderTable.hpp"          // lot 21 : les types ranges en dossiers
#include "HmiQuickTrend.hpp"           // 1.10 (chantier O) : la fenetre graphique temporaire
#include "../TablePaste.hpp"
#include "../../core/Command.hpp"
#include "../../hmi/HmiComm.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/FilterMemory.hpp"   // lot API 8 : la recherche retenue

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace app {

class HmiOperatorsPane;   // 1.10 (chantier S2) : HmiOperatorPanes.hpp
class HmiEnumValuesPane;  // 1.10 (chantier U) : HmiEnumPanes.hpp
class HmiEnumCreatePanel;
class HmiEnumDialog;

class HmiVariablesPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiVariablesPane(std::string id, hmi::DocumentPtr doc, Apply apply);

    void refresh();

    // ---- les actions (Ctrl+Z les reprend) ; faux / kNoId et `why` si refuse ----
    hmi::Id addVariable(const std::string& name, const std::string& type, const std::string& folder = {}, std::string* why = nullptr);
    bool setName(hmi::Id, const std::string& name, std::string* why = nullptr);
    bool setType(hmi::Id, const std::string& type, std::string* why = nullptr);
    bool setInitial(hmi::Id, const std::string& initial, std::string* why = nullptr);
    bool setDescription(hmi::Id, const std::string& text);
    bool setFolder(hmi::Id, const std::string& folder, std::string* why = nullptr);
    // "" : delier. Lier sans adresse : la prochaine libre de l'equipement.
    bool setEquipment(hmi::Id, const std::string& equipment, std::string* why = nullptr);
    bool setAddress(hmi::Id, const std::string& address, std::string* why = nullptr);
    bool setReadOnly(hmi::Id, bool readOnly, std::string* why = nullptr);
    bool setPackBools(hmi::Id, bool pack);
    // L'adresse corrigee d'un membre ("Heures", "Vannes[2].Position") ; vide : la place calculee.
    bool setMemberAddress(hmi::Id, const std::string& relPath, const std::string& address, std::string* why = nullptr);
    // 1.11.8 : des membres INTERNES (gardes dans l'IHM : l'equipement ne les lit ni ne les ecrit) ou
    // ATTRIBUES a l'equipement de la structure ; `allElements` : le meme membre dans toutes les cases
    // du tableau ([0].NOM -> [*].NOM). Une seule commande (un Ctrl+Z).
    bool setMembersInternal(hmi::Id, const std::vector<std::string>& relPaths, bool internal, bool allElements = false,
                            std::string* why = nullptr);
    // 1.11.8 : « Recalculer la place memoire » - les mots des membres internes sont rendus, la suite se
    // resserre ; `on` faux : la place d'origine (les membres internes gardent la leur).
    bool recalculatePlace(hmi::Id, bool on = true, std::string* why = nullptr);
    // 1.11.8 : les membres choisis (chemins relatifs), d'une meme variable.
    [[nodiscard]] std::vector<std::string> selectedMemberPaths() const;
    hmi::Id duplicateVariable(hmi::Id);
    bool deleteVariable(hmi::Id);
    bool addFolder(const std::string& path, std::string* why = nullptr);
    bool renameFolder(const std::string& path, const std::string& newLeaf, std::string* why = nullptr);
    bool deleteFolder(const std::string& path);                 // ses variables montent d'un cran

    // ---- la selection et l'affichage ----
    [[nodiscard]] hmi::Id     selectedVariable() const;          // aussi pour un membre choisi
    [[nodiscard]] std::string selectedFolder() const;            // "" : pas un dossier
    [[nodiscard]] std::string selectedPath() const;              // un membre : "Four1.Vannes[2]"
    void selectVariable(hmi::Id);
    void selectFolder(const std::string& path);
    void selectPath(const std::string& path);
    void setExpanded(const std::string& path, bool on);         // un dossier ("Ligne") ou une variable ("Four1.Vannes")
    [[nodiscard]] bool expanded(const std::string& path) const;
    void setFolderFilter(const std::string& folder);            // "" : tous ; "/" : la racine seule
    void setSearch(const std::string& text);
    void setExpandAll(bool on);
    void setBoundOnly(bool on);
    // La ligne du modele de la table pour ce chemin (dossier, variable, membre) ; -1 : aucune.
    [[nodiscard]] int rowOf(const std::string& key) const;

    // ---- Lot API 8 : renommer une variable, en voyant tout ce qui suit ----
    // Ce que font F2 et le double-clic sur son nom (`typed` vide), et un nom tape
    // dans la case Nom ou la grille (`typed` : le nouveau nom, deja ecrit dans le
    // dialogue) : le dialogue Renommer de l'ecran d'analyse s'ouvre. Faux : pas
    // d'ecran pour l'ouvrir (un essai), ou rien a renommer - le volet renomme
    // alors sur place, comme avant. Les dossiers se renomment toujours sur place.
    bool renameInDialog(hmi::Id id, const std::string& typed = {});
    // F2 sur la ligne choisie (pour les scripts) : vrai si le dialogue s'ouvre.
    bool renameSelectedInDialog();
    // ---- fin Lot API 8 ----

    struct Hosts {
        std::function<void(hmi::Id)>                               removeVariable;   // confirmer
        std::function<void(const std::string&)>                    showEquipment;    // Voir dans Equipements
        std::function<void(const std::string&, std::function<void(const std::string&)>)> arrayType;   // Tableau... (type actuel, rappel)
        std::function<void()>                                      newType;          // + Type : l'onglet Types IHM
        std::function<hmi::comm::Link*(const std::string&)>        link;             // en marche : la liaison d'un equipement
        // ---- 1.10 (chantier O) : la fenetre graphique temporaire ----
        // L'ecran la pose dans une fenetre a elle et la rend (vide : le volet la
        // garde, trends() - les essais) ; ce qui la nourrit (la simulation).
        std::function<HmiQuickTrend*(std::unique_ptr<HmiQuickTrend>)> showTrend;
        HmiQuickTrend::Source                                          trendSource;
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }

    // ---- 1.10 (chantier O) : le clic droit, "Ouvrir une visualisation graphique" ----
    //  Le clic droit sur une ligne (choisie si elle ne l'etait pas ; Ctrl/Maj+clic
    //  pour plusieurs) ouvre un menu : Ouvrir une visualisation graphique, Copier
    //  le nom, Renommer, Dupliquer, Supprimer, Copier / Coller (Excel).
    //  trendPaths : ce que la selection trace - une variable numerique ou BOOL, un
    //  membre ; une structure ou un tableau : ses membres numeriques et BOOL (24 au
    //  plus) ; un texte : rien.
    [[nodiscard]] std::vector<std::string> trendPaths() const;
    // La fenetre graphique sur la selection (nulle : rien a tracer, le volet le dit).
    HmiQuickTrend* openTrend();
    void openContextMenu(gfx::Point at);
    [[nodiscard]] ui::PopupMenu* contextMenu() const noexcept { return menu_; }
    void runMenu(int action);                                    // une entree du menu (les scripts)
    [[nodiscard]] const std::vector<std::unique_ptr<HmiQuickTrend>>& trends() const noexcept { return trends_; }
    // ---- fin 1.10 ----

    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&    table() noexcept { return *table_; }
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *props_; }
    [[nodiscard]] ui::DropDown&     folderFilter() noexcept { return *folderBox_; }
    [[nodiscard]] ui::InputText&    search() noexcept { return *search_; }
    [[nodiscard]] ui::Checkbox&     expandAllBox() noexcept { return *expandAll_; }
    [[nodiscard]] ui::Checkbox&     boundOnlyBox() noexcept { return *boundOnly_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

    // Une ligne du tableau (exposee pour le modele et les essais).
    struct Row {
        enum class Kind : std::uint8_t { Folder, Variable, Member, More } kind{Kind::Variable};
        std::string key;               // le dossier ("Ligne/Convoyeur"), ou le chemin ("Four1.Vannes[2]")
        hmi::Id     var{hmi::kNoId};
        std::string rel;               // un membre : relatif a la variable ("Vannes[2]")
        std::string type;
        int         depth{0};
        int         expander{-1};      // -1 : rien a deplier ; 0 : replie ; 1 : deplie
        std::vector<std::string> cells;
        int         tone{0};           // qualite : 1 bonne, 2 ancienne, 3 mauvaise
        bool        editableAddress{false};
    };
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }
    // Lot 21 : le depot d'un glisser (lignes du modele) : ranger dans un dossier,
    // a la racine, ou reordonner entre deux variables.
    void dropRows(const std::vector<ui::RowIndex>& from, ui::RowIndex to, ui::TreeView::DropWhere where);

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildProperties();
    void say(std::string text, bool warning = false);
    void apply(core::CommandPtr cmd);
    bool change(hmi::Id, std::string label, const std::function<void(hmi::Variable&)>& edit);
    void emitFolder(const std::string& folder, int depth);
    void emitVariable(const hmi::Variable&, int depth);
    using Leaves = std::map<std::string, hmi::Variable, std::less<>>;   // cle : le chemin en majuscules
    void emitMembers(const hmi::Variable&, const Leaves&, const std::string& path, const std::string& rel, const std::string& type, int depth);
    void qualityOf(const hmi::Variable& owner, const std::string& path, bool leaf, std::string& text, int& tone) const;
    // Lot recherche : la recherche (mots, "phrases", -exclus : le nom, le type,
    // l'equipement, l'adresse, la DESCRIPTION, le dossier) et les filtres des
    // colonnes (sauf `skipColumn`) ; le texte d'une variable dans une colonne.
    [[nodiscard]] bool passes(const hmi::Variable&, int skipColumn = -1) const;
    [[nodiscard]] std::string cellOf(const hmi::Variable&, std::size_t column) const;
    // Lot 20 : coller depuis Excel - la cible (les colonnes et les actions du volet).
    [[nodiscard]] paste::Target pasteTarget(const ui::TableView::PasteRequest&);
    [[nodiscard]] bool cellEditable(std::size_t row, std::size_t col) const;
    [[nodiscard]] std::vector<std::string> choicesFor(std::size_t row, std::size_t col) const;
    bool commitCell(std::size_t row, std::size_t col, const std::string& text);
    [[nodiscard]] std::string newFolderParent() const;

    hmi::DocumentPtr  doc_;
    Apply             apply_;
    Hosts             hosts_;
    HmiToolStrip*     tools_{nullptr};
    ui::DropDown*     folderBox_{nullptr};
    ui::InputText*    search_{nullptr};
    ui::Checkbox*     expandAll_{nullptr};
    ui::Checkbox*     boundOnly_{nullptr};
    ui::Splitter*     split_{nullptr};
    ui::TableView*    table_{nullptr};
    ui::PropertyGrid* props_{nullptr};
    ui::StatusBar*    status_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::vector<Row>  rows_;
    std::vector<std::string> folderChoices_;
    std::set<std::string> collapsed_;      // les dossiers replies (majuscules)
    std::set<std::string> open_;           // les variables et membres deplies (majuscules)
    std::string       filterFolder_, searchText_;
    ui::SearchQuery   query_;              // lot recherche : searchText_, lue une fois
    bool              expandAllOn_{false}, boundOnlyOn_{false};
    std::string       selectedKey_;
    std::string       message_;
    bool              syncing_{false};
    paste::Binding    paste_;              // lot 20 : coller depuis Excel
    core::ConnectionScope links_;
    hmi::Id           naming_{hmi::kNoId}; // lot API 8 : la variable qui vient d'etre creee (son nom s'ecrit sur place)
    ui::PopupMenu*    menu_{nullptr};      // 1.10 : le menu du clic droit
    std::vector<std::unique_ptr<HmiQuickTrend>> trends_;   // 1.10 : les fenetres que l'ecran n'a pas prises
    int               trendCount_{0};
    // ---- Lot API 8 : les filtres retenus - la recherche tapee, d'une seance a l'autre ----
    ui::SearchMemory  searchMemory_;
    // ---- fin Lot API 8 ----
};

class HmiTypesPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiTypesPane(std::string id, hmi::DocumentPtr doc, Apply apply);

    void refresh();

    hmi::Id addType(const std::string& name, std::string* why = nullptr);
    bool renameType(hmi::Id, const std::string& name, std::string* why = nullptr);
    bool setTypeDescription(hmi::Id, const std::string& text);
    bool deleteType(hmi::Id, std::string* why = nullptr);
    hmi::Id duplicateType(hmi::Id);
    bool addMember(hmi::Id, const std::string& name, const std::string& type, std::string* why = nullptr);
    bool setMember(hmi::Id, std::size_t index, const hmi::TypeMember&, std::string* why = nullptr);
    bool removeMember(hmi::Id, std::size_t index);
    bool moveMember(hmi::Id, std::size_t index, int delta);

    [[nodiscard]] hmi::Id selectedType() const;
    [[nodiscard]] int     selectedMember() const;
    [[nodiscard]] HmiOperatorsPane* operators() noexcept { return operators_; }   // 1.10 (chantier S2)
    void selectType(hmi::Id);
    void selectMember(int index);

    struct Hosts {
        std::function<void(const std::string&, std::function<void(const std::string&)>)> arrayType;   // Tableau...
        std::function<void(const std::string&)> showVariable;                                         // Utilise par : la variable
        // 1.11.2 (decision 174) : Exporter (les elements coches, celui-ci d'avance) et Importer... (tout paquet).
        std::function<void(hmi::Id)>            exportItems;
        std::function<void()>                   importAny;
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }

    // ---- 1.10 (chantier O) : les membres <-> Excel ----
    //  Ctrl+C (et le clic droit, et l'outil Copier) : les membres choisis en
    //  tableau, avec une ligne de titres (Nom, Type, Valeur initiale,
    //  Description, Decalage, Taille). Ctrl+V : un tableau d'Excel, avec ou
    //  sans titres (sans : Nom, Type, Valeur initiale, Description, dans cet
    //  ordre ; les autres colonnes ignorees) -> un APERCU sous la table (ajoutes,
    //  remplaces, refuses et pourquoi : nom invalide, nom en double, type
    //  inconnu...), puis Appliquer : UNE commande (Ctrl+Z), ou Annuler.
    //  Un nom qui existe : le membre est remplace a sa place (une case vide garde
    //  sa valeur) ; un nom nouveau : ajoute a la fin.
    struct MemberPaste {
        struct Refusal { std::size_t line{0}; std::string name, why; };
        hmi::Id                  type{hmi::kNoId};
        std::vector<hmi::TypeMember> result;      // les membres du type apres le collage
        std::vector<std::string> added, replaced;
        std::vector<Refusal>     refused;
        bool                     titles{false};    // la premiere ligne etait des titres
        std::vector<std::string> ignored;          // les titres inconnus
        std::string              error;            // rien a coller, et pourquoi
        [[nodiscard]] std::string summary() const;  // "3 membres ajoutes, 1 remplace, 2 refuses"
    };
    [[nodiscard]] MemberPaste planMemberPaste(hmi::Id type, std::string_view text) const;
    bool applyMemberPaste(const MemberPaste&, std::string* why = nullptr);
    [[nodiscard]] std::string copyMembersText() const;        // ce que Ctrl+C met dans le presse-papiers
    std::size_t copyMembers();                                // rend le nombre de membres copies
    bool pasteMembers(std::string_view text);                 // l'apercu (faux : rien a coller)
    [[nodiscard]] const MemberPaste* pendingPaste() const noexcept { return pending_ ? &*pending_ : nullptr; }
    bool confirmPaste();                                      // Appliquer
    void cancelPaste();                                       // Annuler
    [[nodiscard]] ui::Widget* pastePreview() const noexcept { return preview_; }
    void openMemberMenu(gfx::Point at);
    [[nodiscard]] ui::PopupMenu* memberMenu() const noexcept { return menu_; }
    // ---- fin 1.10 ----

    // ---- 1.10 (chantier U) : le type enumeration (decision 15, maquette scene 14) ----
    //  << Nouveau type v >> ouvre un menu : Structure... (comme avant) ou
    //  Enumeration... (la fenetre << Nouvelle enumeration >>). Une enumeration
    //  choisie montre ses valeurs (HmiEnumValuesPane) a la place des membres ;
    //  la section Operateurs de S2 (toString / fromString) reste dessous.
    void openNewTypeMenu();
    [[nodiscard]] ui::PopupMenu* newTypeMenu() const noexcept { return newMenu_; }
    void openEnumCreation();
    [[nodiscard]] HmiEnumCreatePanel* enumCreation() const noexcept { return create_; }
    hmi::Id createEnumerationFromPanel(std::string* why = nullptr);   // << Creer l'enumeration >> (une commande)
    [[nodiscard]] HmiEnumValuesPane* enumValues() const noexcept { return enums_; }
    [[nodiscard]] bool showsEnumeration() const;
    // ---- fin 1.10 (chantier U) ----

    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&    typeTable() noexcept { return *types_; }
    [[nodiscard]] HmiFolderTable&   folders() noexcept { return *folders_; }     // lot 21
    [[nodiscard]] ui::TableView&    memberTable() noexcept { return *members_; }
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *props_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void refreshMembers();
    void rebuildProperties();
    void say(std::string text, bool warning = false);
    bool change(hmi::Id, std::string label, const std::function<bool(hmi::HmiType&, std::string*)>& edit, std::string* why);
    bool commitMemberCell(std::size_t row, std::size_t col, const std::string& text);
    void showPreview();                       // 1.10 : l'apercu du collage (ou le cacher)

    hmi::DocumentPtr  doc_;
    Apply             apply_;
    Hosts             hosts_;
    HmiToolStrip*     tools_{nullptr};
    ui::Splitter*     split_{nullptr};
    ui::TableView*    types_{nullptr};
    ui::TableView*    members_{nullptr};
    ui::PropertyGrid* props_{nullptr};
    HmiOperatorsPane* operators_{nullptr};   // 1.10 (chantier S2) : la section Operateurs du type
    ui::StatusBar*    status_{nullptr};
    std::shared_ptr<ui::ITableModel> typeModel_, memberModel_;
    std::vector<hmi::Id> typeOrder_;
    std::unique_ptr<HmiFolderTable> folders_;   // lot 21 : la liste des types rangee en dossiers
    std::optional<MemberPaste> pending_;      // 1.10 : l'apercu du collage
    std::string       pendingText_;           // 1.10 : son texte (refait si le projet change)
    ui::Widget*       preview_{nullptr};
    ui::PopupMenu*    menu_{nullptr};
    // 1.10 (chantier U) : le type enumeration
    HmiEnumValuesPane*  enums_{nullptr};
    HmiEnumCreatePanel* create_{nullptr};
    HmiEnumDialog*      enumDialog_{nullptr};
    ui::PopupMenu*      newMenu_{nullptr};
    ui::Widget*         membersPanel_{nullptr};
    hmi::Id           current_{hmi::kNoId};
    int               member_{-1};
    std::string       message_;
    bool              syncing_{false};
    core::ConnectionScope links_;
};

} // namespace app
