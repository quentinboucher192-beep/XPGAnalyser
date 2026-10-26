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
//    | Nom | Type | Unite | Initiale | Equipement | Adresse | Acces | Place | Qualite| grille
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
#include "../../hmi/HmiDesign.hpp"   // 1.12.18 : FindHit (Voir les usages)
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/FilterMemory.hpp"   // lot API 8 : la recherche retenue
#include "../../ui/widgets/PathBrowse.hpp"     // 1.11.16 : exporter, importer les valeurs remanentes
#include "../../hmi/HmiRetain.hpp"             // 1.11.16 : la remanence d'exploitation
#include "../../sim/Rich.hpp"                  // 1.12.14 : la valeur initiale deballee

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace app {

class HmiOperatorsPane;   // 1.10 (chantier S2) : HmiOperatorPanes.hpp
class HmiMethodsPane;     // 1.13.0 (J1b) : HmiMethodPanes.hpp
class HmiMethodDialog;
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
    // 1.13.0 (lot J6b) : L'UNITE PHYSIQUE (la colonne Unite, apres le Type) - vide : sans unite ; sinon un
    // symbole du catalogue (degc -> degC), seulement sur un type numerique (refuse sinon, et pourquoi).
    // Un type change qui n'en prend plus la retire. pickUnit : le selecteur d'unites (par son hote).
    bool setUnit(hmi::Id, const std::string& unit, std::string* why = nullptr);
    void pickUnit(hmi::Id);
    bool setDescription(hmi::Id, const std::string& text);
    bool setFolder(hmi::Id, const std::string& folder, std::string* why = nullptr);
    // "" : delier. Lier sans adresse : la prochaine libre de l'equipement.
    bool setEquipment(hmi::Id, const std::string& equipment, std::string* why = nullptr);
    bool setAddress(hmi::Id, const std::string& address, std::string* why = nullptr);
    bool setReadOnly(hmi::Id, bool readOnly, std::string* why = nullptr);
    bool setPackBools(hmi::Id, bool pack);
    // ---- 1.11.16 : LA REMANENCE D'EXPLOITATION ----
    //  La case « Remanente » d'une variable de l'IHM (non liee) : sur le poste
    //  d'exploitation, sa valeur est gardee a chaque changement et rendue au
    //  lancement suivant (hmi::retain) - jamais par la simulation de l'editeur.
    //  Les commandes agissent sur le stockage du poste (retainFile :
    //  <projet>/ihm/historique/remanence_exploitation.txt), refusees pendant que le
    //  poste tourne (il y ecrit lui-meme). Faux : refuse, `why` dit pourquoi.
    bool setRetain(hmi::Id, bool on, std::string* why = nullptr);
    [[nodiscard]] std::string retainFile() const;
    // Reinitialiser : oublier les valeurs gardees d'une variable (kNoId : toutes) ; le
    // poste reprendra la valeur initiale. `count` : combien de valeurs sont parties.
    bool resetRetained(hmi::Id variable, std::size_t* count = nullptr, std::string* why = nullptr);
    // Exporter (.csv : pour Excel ; sinon le format du poste) ; importer l'un ou l'autre
    // (les valeurs remplacent celles des memes variables) - `report` : le compte rendu.
    bool exportRetained(const std::string& path, std::string* why = nullptr);
    bool importRetained(const std::string& path, std::string* report = nullptr);
    // Verifier l'integrite du stockage : lisible, complet, ce qu'il garde et sa copie.
    [[nodiscard]] std::string retainIntegrity() const;
    // Les valeurs gardees, relues quand le fichier change (nul : rien de lisible).
    [[nodiscard]] const hmi::retain::Store* retainStore() const;
    // L'adresse corrigee d'un membre ("Heures", "Vannes[2].Position") ; vide : la place calculee.
    bool setMemberAddress(hmi::Id, const std::string& relPath, const std::string& address, std::string* why = nullptr);
    // 1.12.16 : L'ADRESSE CALCULEE d'un membre (sa place dans la suite logique, sans sa correction) - %MW503
    // pour Vannes[3] parti de %MW500, meme corrige en %MW510 ; un membre compose : son depart. Vide : aucune.
    [[nodiscard]] std::string computedMemberAddress(hmi::Id, const std::string& relPath) const;
    // 1.12.16 : retirer les corrections (revenir aux adresses calculees) de ces membres ; vide : de tous les
    // membres de la variable. Une commande (un Ctrl+Z). Faux : rien a retirer (`why`).
    bool clearMemberAddresses(hmi::Id, const std::vector<std::string>& relPaths, std::string* why = nullptr);
    // 1.12.18 (planches 11 et 12) : « Modifier l'adresse dans toutes les cases... » - la formule d'un motif
    // ("[*].Retour" = "%MW(1000 + 10 * i)", "[*]" = le depart de chaque case) ; les adresses par case qu'il
    // couvre sont retirees. Une commande (un Ctrl+Z). Faux : l'apercu (hmi::addr::preview) refuse (`why`).
    bool applyAddressFormula(hmi::Id, const std::string& pattern, const std::string& formula, std::string* why = nullptr);
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
        std::function<void()>                                      newType;          // + Type : l'onglet Types IHM
        std::function<hmi::comm::Link*(const std::string&)>        link;             // en marche : la liaison d'un equipement
        // ---- 1.10 (chantier O) : la fenetre graphique temporaire ----
        // L'ecran la pose dans une fenetre a elle et la rend (vide : le volet la
        // garde, trends() - les essais) ; ce qui la nourrit (la simulation).
        std::function<HmiQuickTrend*(std::unique_ptr<HmiQuickTrend>)> showTrend;
        HmiQuickTrend::Source                                          trendSource;
        // ---- 1.11.16 : la remanence d'exploitation ----
        //  retainFile : le stockage du poste (vide : projet jamais enregistre) ;
        //  stationRunning : le poste tourne (il ecrit le stockage) ; currentValue :
        //  la valeur d'une variable dans la simulation de l'editeur (vide :
        //  arretee) ; confirm : une question (titre, texte, bouton ; rappel si oui) ;
        //  askPath : un chemin, avec le bouton ... ; report : un compte rendu.
        std::function<std::string()>                                   retainFile;
        std::function<bool()>                                          stationRunning;
        std::function<std::string(const std::string&)>                 currentValue;
        std::function<void(const std::string&, const std::string&, const std::string&, std::function<void()>)> confirm;
        std::function<void(const std::string&, const std::string&, const std::string&, ui::PathBrowse,
                           std::function<void(const std::string&)>)>   askPath;
        std::function<void(const std::string&, const std::string&, bool)> report;
        // 1.12.18 : « Voir les usages » - aller a un emploi (la vue, l'objet ; kNoId : la vue elle-meme).
        std::function<void(hmi::Id view, hmi::Id object)>               openUse;
        // 1.12.18 (planche 11) : « Modifier l'adresse dans toutes les cases... » - la fenetre (l'hote).
        std::function<void(hmi::Id variable, const std::string& member)> editAddresses;
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
        // 1.12.14 : Detail - une ligne du deballage d'un type a taille (une propriete publique, un element,
        // une entree d'une MAP et ses deux cotes), lue seulement.
        enum class Kind : std::uint8_t { Folder, Variable, Member, More, Detail } kind{Kind::Variable};
        std::string key;               // le dossier ("Ligne/Convoyeur"), ou le chemin ("Four1.Vannes[2]")
        hmi::Id     var{hmi::kNoId};
        std::string rel;               // un membre : relatif a la variable ("Vannes[2]")
        std::string type;
        int         depth{0};
        int         expander{-1};      // -1 : rien a deplier ; 0 : replie ; 1 : deplie
        std::vector<std::string> cells;
        int         tone{0};           // qualite : 1 bonne, 2 ancienne, 3 mauvaise
        bool        editableAddress{false};
        bool        property{false};   // 1.12.14 : une propriete publique (Count, Length...)
    };
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }
    // Lot 21 : le depot d'un glisser (lignes du modele) : ranger dans un dossier,
    // a la racine, ou reordonner entre deux variables.
    void dropRows(const std::vector<ui::RowIndex>& from, ui::RowIndex to, ui::TreeView::DropWhere where);

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    std::vector<hmi::design::FindHit> usesOfMenu_;   // 1.12.18 : les emplois que montre le menu (Voir les usages)
    void rebuildProperties();
    void say(std::string text, bool warning = false);
    void apply(core::CommandPtr cmd);
    bool change(hmi::Id, std::string label, const std::function<void(hmi::Variable&)>& edit);
    void emitFolder(const std::string& folder, int depth);
    void emitVariable(const hmi::Variable&, int depth);
    using Leaves = std::map<std::string, hmi::Variable, std::less<>>;   // cle : le chemin en majuscules
    void emitMembers(const hmi::Variable&, const Leaves&, const std::string& path, const std::string& rel, const std::string& type, int depth);
    // 1.12.14 : LE DEBALLAGE A TOUTE PROFONDEUR - les proprietes publiques (Count, Length...), les elements
    // d'un type a taille (ceux de la valeur initiale, sinon un element type qui se deplie a son tour),
    // first et second d'une entree de MAP ; et les proprietes d'un tableau ou d'une structure IHM.
    void emitRich(const hmi::Variable&, const std::string& path, const std::string& type, const sim::ObjRef& value, int depth);
    void emitDetail(const hmi::Variable&, const std::string& key, const std::string& name, const std::string& type, const std::string& initial,
                    const std::string& access, const std::string& what, const sim::ObjRef& value, const std::string& under, int depth);
    void emitEntry(const hmi::Variable&, const std::string& path, const std::string& key, const std::string& keyType,
                   const std::string& valueType, const sim::ObjRef& value, int depth);
    void emitAggregateProperties(const hmi::Variable&, const std::string& path, const std::string& type, int depth);
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
    std::shared_ptr<int>  alive_{std::make_shared<int>(0)};   // 1.11.19 : le selecteur de types repond apres coup
    core::ConnectionScope links_;
    hmi::Id           naming_{hmi::kNoId}; // lot API 8 : la variable qui vient d'etre creee (son nom s'ecrit sur place)
    ui::PopupMenu*    menu_{nullptr};      // 1.10 : le menu du clic droit
    std::vector<std::unique_ptr<HmiQuickTrend>> trends_;   // 1.10 : les fenetres que l'ecran n'a pas prises
    int               trendCount_{0};
    // ---- Lot API 8 : les filtres retenus - la recherche tapee, d'une seance a l'autre ----
    ui::SearchMemory  searchMemory_;
    // ---- fin Lot API 8 ----
    // 1.11.16 : le stockage du poste relu (son empreinte : date et taille du fichier et de sa copie).
    [[nodiscard]] bool retainBusy(std::string* why) const;
    void askRetainExport();
    void askRetainImport();
    mutable std::optional<hmi::retain::Store> retainCache_;
    mutable std::string retainStamp_, retainNote_;
    mutable bool        retainReadable_{false};
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
    bool duplicateMember(hmi::Id, std::size_t index);   // 1.12.18 : la copie juste apres (Niveau_copie)
    // 1.13.0 (lot J6b) : la colonne Unite d'un membre (apres le Type) - setMember la controle (un nombre seulement,
    // un symbole du catalogue) ; pickMemberUnit : le selecteur d'unites (par son hote).
    void pickMemberUnit(hmi::Id type, std::size_t index);

    [[nodiscard]] hmi::Id selectedType() const;
    [[nodiscard]] int     selectedMember() const;
    [[nodiscard]] HmiOperatorsPane* operators() noexcept { return operators_; }   // 1.10 (chantier S2)
    // ---- 1.13.0 (J1b) : L'ECRAN DES OBJETS ----
    //  La section Methodes (sous les membres, au-dessus des Operateurs) ; la colonne Visibilite des membres
    //  (Publique / Privee, cadenas ; clic droit Rendre prive / Rendre public) ; les membres herites montres
    //  grises "(herite de T_Base)" a la suite ; dans les proprietes du type, "Herite de... (EXTENDS)" (les
    //  structures, sans cycle) et la categorie "Implemente (IMPLEMENTS)" (une case par interface) ; Nouveau
    //  type > Interface... (I_Interface, une signature d'exemple) ; la page d'une interface : ses signatures,
    //  l'apercu de sa declaration et "Types qui l'implementent". Chaque geste : une commande (Ctrl+Z).
    [[nodiscard]] HmiMethodsPane*  methods() noexcept { return methods_; }
    [[nodiscard]] HmiMethodDialog* methodDialog() noexcept { return methodDialog_; }
    bool setMemberPrivate(hmi::Id type, std::size_t index, bool isPrivate, std::string* why = nullptr);
    // "" ou "(aucun)" : plus de parent. Refuse : un type inconnu, hors structure, un cycle.
    bool setTypeBase(hmi::Id type, const std::string& base, std::string* why = nullptr);
    // Implementer (ou plus) l'interface `iface` (son nom complet).
    bool setTypeImplements(hmi::Id type, const std::string& iface, bool on, std::string* why = nullptr);
    hmi::Id addInterfaceType(const std::string& name, std::string* why = nullptr);
    [[nodiscard]] bool showsInterface() const;
    void selectType(hmi::Id);
    void selectMember(int index);

    struct Hosts {
        std::function<void(const std::string&)> showVariable;                                         // Utilise par : la variable
        // 1.11.2 (decision 174) : Exporter (les elements coches, celui-ci d'avance) et Importer... (tout paquet).
        std::function<void(hmi::Id)>            exportItems;
        std::function<void()>                   importAny;
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }

    // ---- 1.10 (chantier O) : les membres <-> Excel ----
    //  Ctrl+C (et le clic droit, et l'outil Copier) : les membres choisis en
    //  tableau, avec une ligne de titres (Nom, Type, Valeur initiale,
    //  Description, Decalage, Taille ; 1.13.0 : Unite). Ctrl+V : un tableau d'Excel, avec ou
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

    // ---- 1.12.20 (lot T) : le type rappel nomme (T_Filtre : CALLBACK (x : REAL) : BOOL) ----
    //  << Nouveau type v >> › Rappel... : un type T_Rappel, a renommer, sa signature a changer. Un
    //  rappel choisi montre sa page a la place des membres : la signature (sa case s'edite), ses
    //  entrees, son retour, et les fonctions du projet qui conviennent (ou pourquoi pas).
    hmi::Id addCallbackType(const std::string& name, const std::string& signature, std::string* why = nullptr);
    bool setCallbackSignature(hmi::Id, const std::string& text, std::string* why = nullptr);
    // 1.12.20 (lot G) : les generiques d'une structure ("T", "T : ANY_NUM, U") ; vide : une structure ordinaire.
    bool setTypeGenerics(hmi::Id, const std::string& text, std::string* why = nullptr);
    // 1.13.0 (lot J8) : l'espace de noms du type ("" ou "(racine)" : la racine) - les textes qui le nomment suivent.
    bool setTypeNamespace(hmi::Id, const std::string& ns, std::string* why = nullptr);
    [[nodiscard]] bool showsCallback() const noexcept { return callbackPage_; }

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
    void refreshCallbackPage(const hmi::HmiType&);   // 1.12.20 (lot T)
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
    HmiMethodsPane*   methods_{nullptr};     // 1.13.0 (J1b) : la section Methodes du type
    HmiMethodDialog*  methodDialog_{nullptr};   // 1.13.0 (J1b) : la fenetre "Ajouter une methode..."
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
    bool                callbackPage_{false};   // 1.12.20 (lot T) : la page d'un rappel a la place des membres
    hmi::Id           current_{hmi::kNoId};
    int               member_{-1};
    std::string       message_;
    bool              syncing_{false};
    std::shared_ptr<int>  alive_{std::make_shared<int>(0)};   // 1.11.19 : le selecteur de types repond apres coup
    core::ConnectionScope links_;
};

} // namespace app
