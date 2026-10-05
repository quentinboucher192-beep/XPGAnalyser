// =============================================================================
//  app/TypePanes.hpp - lot API 5 : les types derives, les blocs DFB, les unites
// -----------------------------------------------------------------------------
//  Trois onglets de l'API, faits comme les autres (ApiFrame : la barre, la
//  ligne d'aide) :
//
//   * TYPES DERIVES - ceux du projet et ceux de la bibliotheque, en deux
//     dossiers ; chaque type se deplie sur ses champs. « Utilise par » dit qui
//     s'en sert (variables, unites, DFB, autres types) ; la bibliotheque dit
//     s'il y a plus recent. + Type, + Champ, Renommer (les variables et le code
//     suivent), Supprimer, Mettre a jour, Voir les variables.
//   * BLOCS DFB - les blocs du projet (entrees, sorties, E/S, publiques,
//     sections, instances) ; le bloc choisi DESSINE, ses broches et leurs
//     types ; ses instances, globales ou locales a une unite, et les sections
//     qui les appellent (double-clic : la variable). Creer une instance.
//   * UNITES DE PROGRAMME - chaque unite se deplie sur ses sections (langage,
//     lignes) ; son rang dans la tache, ses parametres relies aux variables du
//     projet (l'EffectiveParameter de l'export). + Unite, + Section, Renommer
//     (ses tables d'animation suivent), Supprimer, Ouvrir le code, Voir dans
//     l'ordre.
//
//  Lot API 7 : DEPLIER SANS LIMITE, dans les trois. Un champ, une variable
//  dont le type est un DDT, un tableau, un DFB ou un bloc de la bibliotheque se
//  deplie sur ses membres, et chaque membre a son tour (project/MemberTree) ; un
//  grand tableau arrive par paquets de 100. Seules les lignes depliees sont
//  fabriquees. Les lignes de membres se lisent, elles ne se modifient pas. Un
//  bloc DFB se deplie sur les dossiers de l'arbre de gauche (et ses sections) ;
//  les variables d'une unite sont rangees par portee, un groupe chacune ; « Ouvrir
//  le code » d'un bloc a plusieurs sections fait choisir la section. Les lignes
//  refaites gardent la ligne choisie (et le defilement, si ui::TableView sait le
//  rendre : scrollOffset / setScrollOffset).
//
//  Chaque modification est UNE commande : un Ctrl+Z la reprend.
// =============================================================================
#pragma once

#include "TaskPanes.hpp"

#include "../project/MemberTree.hpp"
#include "../project/TypeUsage.hpp"
#include "../ui/Icons.hpp"
#include "../ui/TextSearch.hpp"      // lot recherche : la recherche (mots, "phrases", -exclus)
#include "../ui/Theme.hpp"
#include "ApiListKit.hpp"
#include "TablePaste.hpp"

#include <set>

namespace ui { class TableView; class PropertyGrid; class PopupMenu; }

namespace app {

class ApiFrame;

// ======================================================== Types derives ====
// Lot API 7 : un champ structure se deplie a son tour. Ses membres (sans
// limite de profondeur) se lisent : Renommer, Supprimer et + Champ sont grises
// quand l'un d'eux est choisi ; « Voir les variables » prend le type qui le porte.
class DerivedTypesPane final : public ui::Widget {
public:
    enum Action : int { AAddType = 1, AAddField, ARename, ADelete, AUpdate, AVariables };
    explicit DerivedTypesPane(std::string id);
    ~DerivedTypesPane() override;
    void setHosts(ApiPaneHosts h);
    void attach(ApiFrame& frame);
    void refresh();
    void runAction(int action);
    bool selectType(std::string_view name);
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] ApiFilterBar& filters() noexcept { return *filters_; }
    [[nodiscard]] std::size_t outdatedCount() const noexcept { return outdated_; }

    // Lot API 7 - pour les scripts et les tests : deplier (ou replier) la ligne
    // de cette cle, sans la casse :
    //   "g:projet", "g:bibliotheque"   un dossier (deplie au depart)
    //   "t:<Type>"                     un type, sur ses champs
    //   "m:<Type>.<champ>"             un champ, sur ses membres ; puis chaque membre
    //                                  par sa cle project/MemberTree sous cette racine :
    //                                  "m:ST_Armoire.sorties.V3", "m:ST_Armoire.tab[100..199]"
    //                                  (un paquet), "m:ST_Armoire.grille[2,*]" (une ligne)
    // Deplier montre aussi ce qui est au-dessus (le dossier, le type, les
    // membres parents, les paquets) : un membre cache se deplie d'un appel.
    // Replier ne touche qu'a sa cle. Faux : cle inconnue, ou rien a deplier (un
    // BOOL). Un filtre qui cache le type le cache encore.
    bool setExpanded(std::string_view key, bool open);
    // L'etat retenu, meme sous un parent replie.
    [[nodiscard]] bool isExpanded(std::string_view key) const;
    // Les lignes fabriquees (les depliees seulement).
    [[nodiscard]] std::size_t rowCount() const noexcept { return rows_.size(); }

    // ---- Lot API 8 : renommer un champ (le dialogue qui montre tout) ----
    // Choisir la ligne du champ `field` du type `type` (le type deplie) ; `was` :
    // son nom d'avant un renommage - ce qui etait deplie dessous le suit.
    bool selectField(std::string_view type, std::string_view field, std::string_view was = {});
    // F2 (la table a le clavier) : ce que fait Renommer sur la ligne choisie - le
    // type ou le champ ; faux : rien a renommer (un membre, un dossier).
    bool renameSelected();
    // ---- fin Lot API 8 ----

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;     // lot API 8 : F2

private:
    class Model;
    friend class Model;
    struct Info {
        domain::Index              index{domain::kNoIndex};
        std::string                name, version;
        std::size_t                fields{0};
        project::usage::TypeUse    use;
        apikit::LibState           lib;
    };
    struct Row {
        enum Kind : std::uint8_t { Group, Type, Field, Member } kind{Type};
        std::size_t   info{0};                       // Type, Field, Member : dans infos_
        domain::Index field{domain::kNoIndex};       // Field, Member : la variable du champ (la racine)
        int           group{0};                      // 0 du projet, 1 de la bibliotheque
        std::size_t   count{0};                      // Group : combien de types
        // Lot API 7 : ce que la ligne deplie - un champ (sa racine
        // "<Type>.<champ>"), un membre, un paquet d'un tableau.
        project::members::Node node;
        int           depth{0};                      // Member : 1 sous le champ
        bool          expandable{false}, open{false};
        domain::Index decl{domain::kNoIndex};        // Field, Member : la variable qui le declare (members::declarationOf)
        std::string   comment;                       // Member : le commentaire de sa declaration
        ui::Icon      icon{ui::Icon::Variable};      // Field, Member : sa nature (tableau, structure, bloc, variable)
        ui::Tone      tone{ui::Tone::None};          // Member : son sens (entree, sortie...)
        std::string   key;                           // la ligne a garder choisie : "g:projet", "<Type>", "<Type>.<champ>", "m:<chemin>"
    };
    void rebuildRows();
    // Lot recherche : le type passe-t-il la pastille, la recherche (son nom, ses
    // champs : nom, type, COMMENTAIRE) et les filtres des colonnes (sauf `skipColumn`) ?
    [[nodiscard]] bool keeps(const Info& i, const ui::SearchQuery& query, int skipColumn) const;
    void addMembers(const domain::Project& p, const Row& parent, int depth);
    void toggle(std::size_t row);
    [[nodiscard]] const Row* selectedRow() const;
    [[nodiscard]] const Info* selectedInfo() const;
    void refreshProperties();
    void updateHint();
    void rename(const Info& info, domain::Index field);

    ApiPaneHosts                  hosts_;
    ApiFrame*                     frame_{nullptr};
    ApiFilterBar*                 filters_{nullptr};
    ui::TableView*                table_{nullptr};
    ui::PropertyGrid*             props_{nullptr};
    std::shared_ptr<Model>        model_;
    std::vector<Info>             infos_;
    std::vector<Row>              rows_;
    std::set<int>                 collapsedGroups_;
    std::set<std::string>         expanded_;          // en minuscules : "t:<type>" (ses champs), "m:<chemin>" (un champ, un membre)
    std::string                   keep_;              // la cle de la ligne a garder choisie (Row::key)
    std::size_t                   outdated_{0};
    bool                          syncing_{false};
    core::ConnectionScope         links_;
};

// ============================================================ Blocs DFB ====
// Lot API 7 : un bloc se deplie sur ses dossiers (ceux de l'arbre de gauche,
// les non vides, avec leur compte) : Entrees, Sorties, Entrees / sorties,
// Variables publiques, Variables privees - leurs variables (type := valeur
// initiale, commentaire), qui se deplient sur leurs membres - et Sections
// (langage, lignes ; double-clic : le code). Une ligne choisie sous un bloc
// garde ce bloc pour le dessin, les instances et les proprietes.
// « Ouvrir le code » : la section choisie ; un bloc a une section l'ouvre ; a
// plusieurs, un menu les propose sous le bouton (ou sous la ligne, au
// double-clic) ; sans section, la barre d'etat le dit.
class DfbBlockView;

class DfbPane final : public ui::Widget {
public:
    enum Action : int { AAddBlock = 1, ARename, ADelete, AOpenCode, AUpdate, AInstance };
    explicit DfbPane(std::string id);
    ~DfbPane() override;
    void setHosts(ApiPaneHosts h);
    void attach(ApiFrame& frame);
    void refresh();
    void runAction(int action);
    bool selectBlock(std::string_view name);
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] ui::TableView& instances() noexcept { return *instances_; }
    [[nodiscard]] std::string currentBlock() const;

    // Lot API 7 - pour les scripts et les tests : deplier (ou replier) par cle,
    // sans la casse :
    //   "b:<Bloc>"                  un bloc, sur ses dossiers
    //   "f:<Bloc>/<dossier>"        un dossier : entrees, sorties, es, publiques,
    //                               privees, sections (replies au depart)
    //   "m:<Bloc>.<variable>"       une variable, sur ses membres ; puis chaque
    //                               membre par sa cle project/MemberTree
    //                               ("m:DFB_Vanne.Tempo.Q", "m:DFB_Vanne.Tab[0..99]")
    // Deplier montre aussi ce qui est au-dessus (le bloc, le dossier, les membres
    // parents) : un membre cache se deplie d'un appel. Replier ne touche qu'a sa
    // cle. Faux : pas de tel bloc, dossier vide, membre inconnu, ou rien a deplier.
    bool setExpanded(std::string_view key, bool open);
    // L'etat retenu, meme sous un parent replie.
    [[nodiscard]] bool isExpanded(std::string_view key) const;
    [[nodiscard]] std::size_t rowCount() const noexcept { return rows_.size(); }
    // Le menu des sections de « Ouvrir le code » (tests, scripts : choisir "<section>").
    [[nodiscard]] ui::PopupMenu* sectionMenu() const noexcept { return menu_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    class Model;
    class InstanceModel;
    friend class Model;
    friend class InstanceModel;
    struct Info {
        domain::Index                           pou{domain::kNoIndex};
        std::string                             name, version;
        project::usage::Interface               itf;
        std::size_t                             sections{0};
        std::vector<project::usage::Instance>   instances;
        apikit::LibState                        lib;
    };
    struct Row {
        enum Kind : std::uint8_t { Block, Group, Variable, Member, Section } kind{Block};
        std::size_t   info{0};                       // le bloc qui la porte (dans infos_)
        int           folder{-1};                    // Group, Variable, Member, Section : le dossier
        std::size_t   count{0};                      // Group : combien dedans
        domain::Index variable{domain::kNoIndex};    // Variable
        domain::Index section{domain::kNoIndex};     // Section
        project::members::Node node;                 // Variable (sa racine "<Bloc>.<variable>"), Member
        int           depth{0};                      // Member : 1 sous la variable
        bool          expandable{false}, open{false};
        domain::Index decl{domain::kNoIndex};        // Variable, Member : la variable qui le declare (members::declarationOf)
        std::string   comment;                       // Member : le commentaire de sa declaration
        ui::Icon      icon{ui::Icon::Variable};      // Variable, Member : sa nature (tableau, structure, bloc, variable)
        ui::Tone      tone{ui::Tone::None};          // Member : son sens (entree, sortie...)
        std::string   key;                           // "b:<Bloc>", "f:<Bloc>/<dossier>", "v:<Bloc>/<var>", "m:<chemin>", "s:<Bloc>/<section>"
    };
    [[nodiscard]] const Row* selectedRow() const;
    [[nodiscard]] std::size_t selected() const;     // le bloc de la ligne choisie (dans infos_)
    void rebuildRows();
    void addMembers(const domain::Project& p, const Row& parent, int depth);
    void toggle(std::size_t row);
    void refreshDetails();
    // « Ouvrir le code » : `fromTable` (double-clic, Entree) - le menu sous la ligne.
    void openCode(bool fromTable);

    ApiPaneHosts                  hosts_;
    ApiFrame*                     frame_{nullptr};
    ui::TableView*                table_{nullptr};
    ui::TableView*                instances_{nullptr};
    DfbBlockView*                 block_{nullptr};
    ui::PropertyGrid*             props_{nullptr};
    ui::PopupMenu*                menu_{nullptr};
    std::vector<domain::Index>    menuSections_;      // les sections du menu ouvert (l'id d'une entree : son rang)
    std::shared_ptr<Model>        model_;
    std::shared_ptr<InstanceModel> instanceModel_;
    std::vector<Info>             infos_;
    std::vector<Row>              rows_;
    std::set<std::string>         expanded_;          // en minuscules : "b:", "f:", "m:" (voir setExpanded)
    std::size_t                   outdated_{0};
    std::size_t                   shownBlock_{static_cast<std::size_t>(-1)};   // le bloc des instances montrees
    std::string                   keep_;              // la cle de la ligne a garder choisie (Row::key ; un nom seul : un bloc)
    bool                          syncing_{false};
    gfx::Rect                     cardBlock_{}, cardInstances_{};
    core::ConnectionScope         links_;
};

// ================================================ Unites de programme ====
// Lot API 6 : une unite depliee montre aussi SES VARIABLES - ses parametres
// (entree, sortie, entree-sortie, et la variable du projet que chacun recoit)
// puis ses locales, avec leur type, leur valeur initiale, leur commentaire.
// Coller depuis Excel sur une ligne de l'unite cree ou met a jour ses
// variables (Nom, Type, Portee, Reliee a, Valeur initiale, Commentaire), en un
// seul Ctrl+Z.
// Lot API 7 : les variables sont RANGEES PAR PORTEE, un groupe par portee non
// vide, chacun avec son compte et replie a part : Entrees, Sorties, Entrees /
// sorties, Variables publiques, Variables privees (les dossiers de l'arbre).
// Chaque variable se deplie sur ses membres, sans limite ("Unite.var.membre" :
// le nom que la simulation connait). Coller sur le titre d'un groupe cree les
// variables nouvelles avec la portee du groupe (une colonne Portee collee
// l'emporte) ; « + Variable » aussi. Ailleurs, rien ne change.
class UnitsPane final : public ui::Widget {
public:
    // 1.8.0 : Comparer (les sections choisies), Exporter (le programme lisible), Icone.
    enum Action : int { AAddUnit = 1, AAddSection, ARename, ADelete, AOpenCode, AOrder, AAddVariable, ACompare, AExport, AIcon };
    explicit UnitsPane(std::string id);
    ~UnitsPane() override;
    void setHosts(ApiPaneHosts h);
    void attach(ApiFrame& frame);
    void refresh();
    void runAction(int action);
    bool selectUnit(std::string_view name);
    bool selectSection(std::string_view name);
    // Une variable (parametre ou locale) de l'unite : l'unite et son groupe se deplient.
    bool selectVariable(std::string_view unit, std::string_view name);
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }

    // Lot API 7 - pour les scripts et les tests : deplier (ou replier) par cle,
    // sans la casse :
    //   "u:<Unite>"                 une unite, sur ses sections et ses groupes
    //   "g:<Unite>/<groupe>"        un groupe : entrees, sorties, es, publiques,
    //                               privees (deplies au depart)
    //   "m:<Unite>.<variable>"      une variable, sur ses membres ; puis chaque
    //                               membre par sa cle project/MemberTree
    //                               ("m:Gestion.Tempo.Q", "m:Gestion.Tab[100..199]")
    // Deplier montre aussi ce qui est au-dessus (l'unite, le groupe, les membres
    // parents) : un membre cache se deplie d'un appel. Replier ne touche qu'a sa
    // cle. Faux : pas de telle unite, groupe vide, membre inconnu, ou rien a deplier.
    bool setExpanded(std::string_view key, bool open);
    // L'etat retenu, meme sous un parent replie ; un groupe qui n'existe pas : faux.
    [[nodiscard]] bool isExpanded(std::string_view key) const;
    [[nodiscard]] std::size_t rowCount() const noexcept { return rows_.size(); }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    class Model;
    friend class Model;
    struct Row {
        enum Kind : std::uint8_t { Unit, Section, Group, Variable, Member } kind{Unit};
        domain::Index pou{domain::kNoIndex};             // l'unite qui la porte (toutes)
        domain::Index section{domain::kNoIndex};         // Section
        domain::Index variable{domain::kNoIndex};        // Variable
        int           group{-1};                         // Group, Variable, Member : la portee (le groupe)
        std::size_t   count{0};                          // Group : combien de variables
        project::members::Node node;                     // Variable (sa racine "<Unite>.<var>"), Member
        int           depth{0};                          // Member : 1 sous la variable
        bool          expandable{false}, open{false};
        domain::Index decl{domain::kNoIndex};            // Variable, Member : la variable qui le declare (members::declarationOf)
        std::string   comment;                           // Member : le commentaire de sa declaration
        ui::Icon      icon{ui::Icon::Variable};          // Variable, Member : sa nature (tableau, structure, bloc, variable)
        ui::Tone      tone{ui::Tone::None};              // Member : son sens (entree, sortie...)
        std::string   key;                               // "u:<unite>", "s:<section>", "g:<unite>/<groupe>", "v:<unite>/<var>", "m:<chemin>"
        [[nodiscard]] bool isSection() const noexcept { return kind == Section; }
        [[nodiscard]] bool isVariable() const noexcept { return kind == Variable; }
    };
    [[nodiscard]] const Row* selectedRow() const;
    [[nodiscard]] std::vector<domain::Index> selectedSections() const;   // 1.8.0 : les sections choisies (Ctrl+clic)
    void rebuildRows();
    void addMembers(const domain::Project& p, const Row& parent, int depth);
    void refreshProperties();
    [[nodiscard]] std::size_t rankOf(domain::Index pou) const;
    [[nodiscard]] paste::Target pasteTarget(const ui::TableView::PasteRequest& rq);
    // Une unite renommee : ce qui etait deplie ou replie la suit.
    void renameUnitState(const std::string& before, const std::string& after);

    ApiPaneHosts                  hosts_;
    ApiFrame*                     frame_{nullptr};
    ui::TableView*                table_{nullptr};
    ui::PropertyGrid*             props_{nullptr};
    std::shared_ptr<Model>        model_;
    std::vector<Row>              rows_;
    std::vector<project::api::Entry> entries_;
    std::string                   task_;
    std::set<std::string>         expanded_;     // en minuscules : "u:<unite>" (depliee), "m:<chemin>" (une variable, un membre)
    std::set<std::string>         varsFolded_;   // en minuscules : "g:<unite>/<groupe>", les groupes replies
    bool                          firstFill_{true};
    std::string                   keep_;      // "u:<unite>", "s:<section>", "g:<unite>/<groupe>", "v:<unite>/<variable>", "m:<chemin>"
    bool                          syncing_{false};
    paste::Binding                paste_;
    bool                          pasteFresh_{false};   // le bandeau du collage survit au rafraichissement qui le suit
    core::ConnectionScope         links_;
};

} // namespace app
