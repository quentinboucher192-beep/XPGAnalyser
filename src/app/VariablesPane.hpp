// =============================================================================
//  app/VariablesPane.hpp - lot API 5 : les variables, les sous-routines
// -----------------------------------------------------------------------------
//  VARIABLES - les variables globales, presentees comme les variables IHM :
//  en dossiers par genre (instances de DFB, blocs standard, instances de
//  types derives, situees, les autres), des filtres (situees, instances de
//  DFB, pas utilisees, lues par l'IHM), une recherche, et ce qui relie chaque
//  variable au reste : les sections qui la nomment, l'IHM qui la lit, les
//  tables d'animation qui la montrent. « Pas utilisee » ne veut pas dire « a
//  supprimer » : la grille le dit quand l'IHM la lit.
//  + Variable, Renommer (le code et les tables suivent), Supprimer, Ajouter a
//  une table d'animation (une existante ou une nouvelle), Voir les usages,
//  Exporter CSV.
//  Lot API 7 : une variable se deplie sur ses membres, et chacun a son tour,
//  sans limite de profondeur (project/MemberTree) - les champs d'un DDT, les
//  broches, publiques et privees d'une instance de bloc, les elements d'un
//  tableau, par paquets de 100 au-dela de 100. Un membre se lit (son type, ce
//  qu'il est, son commentaire) et s'ajoute a une table d'animation ; il ne se
//  renomme ni ne se supprime. Les filtres et la recherche choisissent les
//  variables : leurs membres les suivent.
//
//  SOUS-ROUTINES - un onglet meme vide : ce que c'est, les trois facons d'en
//  creer (a la main, CreerSR, AppelerSR). Rempli : chaque sous-routine, sa
//  tache, qui l'appelle, ses lignes ; Ouvrir montre le code et les appels.
// =============================================================================
#pragma once

#include "TaskPanes.hpp"
#include "TablePaste.hpp"

#include "../project/MemberTree.hpp"
#include "../project/TypeUsage.hpp"
#include "../ui/TextSearch.hpp"      // lot recherche : la recherche (mots, "phrases", -exclus)
#include "ApiListKit.hpp"

#include <map>
#include <set>

namespace ui { class TableView; class PropertyGrid; class PopupMenu; }

namespace app {

class ApiFrame;
class ApiEmptyState;

// ============================================================ Variables ====
class VariablesPane final : public ui::Widget {
public:
    enum Action : int { AAdd = 1, ARename, ADelete, AAddToTable, AUsages, AExportCsv };
    explicit VariablesPane(std::string id);
    ~VariablesPane() override;
    void setHosts(ApiPaneHosts h);
    void attach(ApiFrame& frame);
    void refresh();
    void runAction(int action);
    bool selectVariable(std::string_view name);
    void search(const std::string& text);
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] ApiFilterBar& filters() noexcept { return *filters_; }
    // Pour les scripts : le menu « Ajouter a une table d'animation » ouvert, et son choix.
    [[nodiscard]] ui::PopupMenu* tableMenu() const noexcept { return menu_; }
    [[nodiscard]] std::size_t unusedCount() const noexcept { return unused_; }

    // Lot API 7 : DEPLIER. Par la cle de la ligne ("armoires", "armoires[0].sorties",
    // "tempon[100..149]", "Grille[2,*]") ou le chemin montre ("tempon[100 ... 149]",
    // "Grille[2, 5]"), sans la casse ni les espaces. Une ligne pas encore montree
    // se trouve aussi : sa variable est montree (les filtres qui la cachent
    // leves), ses parents deplies, puis elle. Faux : pas de variable globale a
    // ce chemin, ou rien dessous (un BOOL).
    bool setExpanded(std::string_view path, bool open);
    // Sa cle est-elle retenue depliee (meme cachee sous un parent replie) ?
    [[nodiscard]] bool isExpanded(std::string_view path) const;
    // Montrer un membre ("armoires[0].sorties.V3", "tempon[104]") : sa variable
    // (les filtres qui la cachent sont leves), les lignes qui y menent
    // depliees, lui choisi. Faux : pas de variable globale ni de membre a ce
    // chemin. Un nom de variable seul la choisit, comme selectVariable.
    bool revealMember(std::string_view path);
    // Le chemin de la ligne choisie : une variable, un membre ("armoires[0].ana",
    // un paquet "tempon[100 ... 149]") ; vide : aucune ligne, ou plusieurs.
    [[nodiscard]] std::string selectedPath() const;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    class Model;
    friend class Model;
    struct Info {
        domain::Index          index{domain::kNoIndex};
        std::string            name, type, address, comment, where;
        std::size_t            sections{0};
        project::usage::Genre  genre{project::usage::Genre::Other};
        const ApiHmiRead*      hmi{nullptr};
        bool                   structure{false};     // lot API 7 : son type a des membres
    };
    struct Row {
        bool        group{false};        // un titre de genre
        int         genre{0};
        std::size_t info{0};             // la variable ; pour un membre, celle de sa racine
        std::size_t count{0};
        // Lot API 7 : un membre deplie, en retrait sous sa variable. `pack` : un
        // paquet de 100, une ligne d'un tableau a plusieurs dimensions - il
        // range des elements et n'a pas de valeur a lui.
        bool        member{false};
        bool        pack{false};
        bool        structure{false};    // la ligne se deplie (une variable ou un membre)
        bool        open{false};         // ... et elle est depliee
        int         depth{0};
        domain::Index decl{domain::kNoIndex};   // la variable qui declare le membre (son commentaire)
        std::string note;                // un membre : ce qu'il est, son commentaire (colonne Commentaire)
        std::string comment;             // ... le commentaire seul (la grille)
        ui::Icon    icon{ui::Icon::Variable};
        ui::Tone    tone{ui::Tone::None};
        project::members::Node node;     // la racine d'une variable qui se deplie ; le membre
    };
    void rebuildRows();
    // La variable passe-t-elle les filtres (la pastille, la recherche) ? Lot
    // recherche : et ceux des colonnes, sauf `skipColumn` (-1 : tous).
    [[nodiscard]] bool passes(const Info& i, int skipColumn = -1) const;
    // La montrer : les filtres qui la cachent leves, son genre deplie.
    void showVariable(const Info& i);
    // Lot API 7 : les membres d'une ligne depliee, a sa suite (et les leurs).
    void addMembers(const domain::Project& p, const Row& parent, int depth);
    // Lot API 7 : RETROUVER UNE LIGNE, montree ou non (voir setExpanded pour
    // `path`) : sa variable globale, son noeud, et les cles (en minuscules) des
    // lignes a deplier pour l'atteindre, sa variable d'abord.
    bool locate(const domain::Project& p, std::string_view path, const Info*& info, project::members::Node& node,
                std::vector<std::string>& chain) const;
    // La cle d'une ligne (le nom d'une variable, la cle d'un membre) : l'etat
    // deplie et la ligne choisie se retiennent par elle.
    [[nodiscard]] std::string keyOf(const Row& r) const;
    [[nodiscard]] std::string selectionKey() const;
    [[nodiscard]] std::vector<std::size_t> selectedRows() const;
    // Ce que "Ajouter a une table d'animation" ajoute : les variables et les
    // membres choisis (un paquet n'est pas une variable).
    [[nodiscard]] std::vector<std::string> selectedPaths() const;
    // La variable des usages : celle de la ligne choisie, ou la racine d'un membre.
    [[nodiscard]] const Info* usageInfo() const;
    [[nodiscard]] std::vector<const Info*> selectedInfos() const;
    void refreshProperties();
    void updateHint();
    void openTableMenu();
    [[nodiscard]] std::vector<std::string> tablesOf(const std::string& name) const;
    // Lot API 6 : coller depuis Excel (Ctrl+V, le clic droit) - un seul Ctrl+Z.
    [[nodiscard]] paste::Target pasteTarget(const ui::TableView::PasteRequest& rq);
    paste::Binding                      paste_;
    bool                          pasteFresh_{false};   // le bandeau du collage survit au rafraichissement qui le suit

    ApiPaneHosts                        hosts_;
    ApiFrame*                           frame_{nullptr};
    ApiFilterBar*                       filters_{nullptr};
    ui::TableView*                      table_{nullptr};
    ui::PropertyGrid*                   props_{nullptr};
    ui::PopupMenu*                      menu_{nullptr};
    std::shared_ptr<Model>              model_;
    std::vector<Info>                   infos_;
    std::vector<Row>                    rows_;
    std::map<std::string, ApiHmiRead>   reads_;
    std::set<int>                       collapsed_;
    std::set<std::string>               expanded_;     // lot API 7 : les cles depliees, en minuscules
    // Lot API 7 : les membres deja calcules, par cle du parent (en minuscules) :
    // une lettre tapee dans la recherche, une fleche cliquee refont les lignes
    // sans recalculer chaque niveau deplie. refresh() les oublie.
    std::map<std::string, std::vector<Row>> memberRows_;
    std::string                         keep_;
    std::size_t                         unused_{0}, readByHmi_{0};
    ui::SearchQuery                     query_;        // lot recherche : la recherche tapee, lue une fois
    bool                                syncing_{false};
    core::ConnectionScope               links_;
};

// ======================================================== Sous-routines ====
class SubroutinesPane final : public ui::Widget {
public:
    enum Action : int { AAdd = 1, ARename, ADelete, AOpen, ACreateSr, ACallSr, AHelp };
    explicit SubroutinesPane(std::string id, std::string projectName);
    ~SubroutinesPane() override;
    void setHosts(ApiPaneHosts h);
    void attach(ApiFrame& frame);
    void refresh();
    void runAction(int action);
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] ApiEmptyState* emptyState() noexcept { return empty_; }
    [[nodiscard]] std::size_t count() const noexcept { return list_.size(); }
    // Lot API 8 : chercher (le nom, la tache, qui l'appelle, le commentaire en
    // tete de son code) ; retenu d'une seance a l'autre. Les sous-routines montrees.
    [[nodiscard]] ApiFilterBar& filters() noexcept { return *filters_; }
    [[nodiscard]] std::size_t shownCount() const;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    class Model;
    friend class Model;
    struct Info {
        domain::Index            section{domain::kNoIndex};
        std::string              name, task;
        std::vector<std::string> callers;
        std::size_t              lines{0};
        std::string              comment;     // lot API 8 : le commentaire en tete du code
    };
    [[nodiscard]] std::size_t selected() const;
    void applySearch();                        // lot API 8 : la recherche, sur la table

    ApiPaneHosts                hosts_;
    ApiFrame*                   frame_{nullptr};
    ApiEmptyState*              empty_{nullptr};
    ui::TableView*              table_{nullptr};
    ApiFilterBar*               filters_{nullptr};     // lot API 8
    std::shared_ptr<Model>      model_;
    std::vector<Info>           list_;
    std::string                 projectName_, keep_;
    core::ConnectionScope       links_;
};

} // namespace app
