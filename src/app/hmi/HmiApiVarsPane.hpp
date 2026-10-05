// =============================================================================
//  app/hmi/HmiApiVarsPane.hpp - les variables de l'automate, en arbre (1.11.1)
// -----------------------------------------------------------------------------
//  Chantier API-V (decision 104) : la liste plate « VARIABLES DU PROGRAMME »
//  (IHM > Configuration) devient un arbre :
//    - d'abord les Globales, puis une branche par unite de programme, ses
//      variables rangees en Publiques, Privees et E/S ;
//    - une instance de DDT ou de DFB se deplie en ses membres, a toute
//      profondeur ; un tableau en ses elements (les premiers, puis
//      « ... et N autres », qui se deplie).
//  Les colonnes : Nom (un cadenas : un nom de l'automate ne se renomme pas
//  ici), Type, Portee, Accessible, Utilisee par l'IHM, Reference, Taille.
//  Le filtre : Toutes, Employees par l'IHM, Accessibles en ecriture, Sans
//  adresse ; la recherche (nom, type, reference) parcourt tout l'arbre et
//  deplie ce qui correspond.
//  Les gestes : double-clic (ou Entree) sur une variable : `insertName` part
//  avec son nom complet (API.Armoires[0].ana.PT1.mes) ; copyName() le copie.
//  Le clic droit : Copier le nom API.…, Inserer dans le script, Emplois.
//  Un clic sur la case « Utilisee par l'IHM » dit ou (la liste des emplois) ;
//  en choisir un : `useChosen` (l'hote y mene). Une variable se glisse hors
//  de l'arbre jusque dans un editeur de script de l'IHM (routeDrag).
//
//  LES DONNEES : des ApiVarNode, que remplit le modele d'API-M
//  (hmi/HmiApiVars.hpp) - ou, dans les essais, des donnees factices.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace app {

// Une ligne de l'arbre, telle que la vue la montre.
struct ApiVarNode {
    enum class Kind : std::uint8_t {
        Group,       // « Globales »
        Unit,        // une unite de programme
        Scope,       // sous une unite : « Publiques », « Privees », « E/S »
        Variable,    // une variable (ou un membre) de type elementaire
        Structure,   // une instance de DDT ou de DFB (ou un element de tableau de DDT) : ses membres
        Array,       // un tableau : ses elements
        More,        // « ... et N autres » (le modele d'API-M) : les elements suivants, a la demande
    };
    enum class Access : std::uint8_t {
        None,        // un dossier, une structure (ses membres le disent)
        ReadWrite,   // une adresse que l'IHM lit et ecrit (%MW, %M, la table d'echanges)
        ReadOnly,    // se lit, ne s'ecrit pas (une entree, une constante...)
        NoAddress,   // sans adresse : lue en simulation seulement
    };
    Kind        kind{Kind::Variable};
    std::string name;           // « Armoires », « [0] », « PT1 », « Publiques »
    std::string path;           // le nom complet : « API.Armoires[0].ana.PT1 » (vide : un dossier)
    std::string type;           // « ARRAY[0..3] OF T_Armoire », « REAL », « unite de programme »
    std::string scope;          // la portee : « Globale », « Publique · Gestion », « Entree · Gestion »
    std::string reference;      // l'adresse : « %MW100 » (vide : aucune)
    Access      access{Access::None};
    std::string accessWhy;      // pourquoi cet acces (l'infobulle) ; vide : la phrase par defaut
    double      bytes{-1};      // la taille en octets (un BOOL : 0,125) ; < 0 : inconnue
    bool        dfb{false};     // une instance de DFB (l'icone)
    std::vector<std::string> uses;      // ou l'IHM l'emploie : « Vue_Accueil/Jauge.value »
    std::vector<ApiVarNode>  children;
    // Le modele d'API-M fabrique les enfants a la demande : `lazy` - il se
    // deplie, ses enfants viennent de HmiApiVarsView::setFetch ; `useCount` -
    // ses emplois et ceux de ce qu'il contient (sans la liste) ; `key` - sa cle
    // stable (vide : le chemin, ou le dossier).
    bool        lazy{false};
    std::size_t useCount{0};
    std::string key;
    // 1.11.1 (R1111-6) : une variable du projet IHM (la bibliotheque de
    // l'editeur de vue les range sous « Variables IHM ») : pas de cadenas, pas
    // de API. devant son nom.
    bool        hmi{false};
};

// Les textes de la colonne Accessible.
[[nodiscard]] const char* apiAccessLabel(ApiVarNode::Access) noexcept;

class HmiApiVarsView final : public ui::Widget {
public:
    enum Column : std::size_t { Name = 0, Type, Scope, Access, Usage, Reference, Size, ColumnCount };
    enum class Filter : std::uint8_t { All, Used, Writable, NoAddress };
    // Un tableau montre ses premiers elements, puis « ... et N autres ».
    static constexpr std::size_t kFirstElements = 8;

    explicit HmiApiVarsView(std::string id);
    ~HmiApiVarsView() override;

    // L'arbre (les Globales, puis les unites). Ce qui est deplie, choisi et le
    // defilement restent quand les memes chemins reviennent.
    void setNodes(std::vector<ApiVarNode> roots);
    [[nodiscard]] const std::vector<ApiVarNode>& nodes() const noexcept { return roots_; }
    // Les enfants d'un noeud `lazy`, demandes la premiere fois qu'il se deplie
    // (le modele d'API-M : Model::children) ; retenus jusqu'au prochain setNodes.
    using Fetch = std::function<std::vector<ApiVarNode>(const ApiVarNode&)>;
    void setFetch(Fetch f) { fetch_ = std::move(f); }
    // La recherche dans tout l'arbre, meme ce qui n'est pas encore fabrique :
    // les chemins qui correspondent (Model::search) ; la vue demande leurs
    // ancetres, puis filtre comme d'habitude. Sans : ce qui est deja connu.
    using SearchSource = std::function<std::vector<std::string>(std::string_view)>;
    void setSearchSource(SearchSource s) { searchSource_ = std::move(s); }
    // Les filtres sur ce qui n'est pas encore deplie : les chemins qui passent
    // le filtre sous des noeuds pas encore fabriques (Accessibles en ecriture :
    // les lignes de la table des adresses) ; la vue demande leurs ancetres. Un
    // noeud pas encore deplie passe « Sans adresse » s'il est lui-meme sans
    // adresse (ses membres le sont, sauf ceux que la table nomme).
    using FilterSource = std::function<std::vector<std::string>(Filter)>;
    void setFilterSource(FilterSource s) { filterSource_ = std::move(s); }

    void setFilter(Filter f);
    [[nodiscard]] Filter filter() const noexcept { return filter_; }
    void setSearch(std::string text);
    [[nodiscard]] const std::string& search() const noexcept { return search_; }

    // Deplier / replier une ligne par sa cle (le chemin API.… ; un dossier :
    // « #Globales », « #Gestion », « #Gestion/Publiques »).
    void setOpen(const std::string& key, bool open);
    [[nodiscard]] bool isOpen(const std::string& key) const;
    void toggleLine(std::size_t line);

    // Les lignes montrees (dossiers compris, sans ce que cachent les dossiers replies).
    [[nodiscard]] std::size_t lineCount() const noexcept { return lines_.size(); }
    [[nodiscard]] std::string lineText(std::size_t line, std::size_t column) const;
    [[nodiscard]] std::string lineKey(std::size_t line) const;
    [[nodiscard]] int lineDepth(std::size_t line) const;
    [[nodiscard]] int findLine(const std::string& key) const;     // -1 : absente
    // Les variables (feuilles) qui passent le filtre et la recherche, depliees ou non.
    [[nodiscard]] std::size_t matchCount() const noexcept { return matches_; }

    // La ligne choisie : son nom complet (vide : un dossier, ou rien).
    [[nodiscard]] std::string selectedName() const;
    bool selectName(const std::string& path);      // ses dossiers se deplient ; faux : absente
    std::string copyName();                        // « Copier le nom API.… » : le nom copie (vide : rien)

    // Double-clic (ou Entree) sur une ligne qui a un nom : ce nom (API.…).
    const core::SignalPtr<const std::string&> insertName = core::Signal<const std::string&>::create();

    // OU L'IHM L'EMPLOIE. Un clic sur la case « Utilisee par l'IHM » d'une
    // variable employee (ou le menu du clic droit, Emplois) ouvre la liste des
    // endroits ; en choisir un emet useChosen(chemin, rang dans ApiVarNode::uses).
    const core::SignalPtr<const std::string&, std::size_t> useChosen =
        core::Signal<const std::string&, std::size_t>::create();
    bool showUses(std::size_t line);                 // faux : rien d'employe sur cette ligne
    [[nodiscard]] ui::PopupMenu& usesMenu() noexcept { return *usesMenu_; }
    // Le clic droit, en tete du menu de la table : Copier le nom API.…,
    // Inserer dans le script montre (un editeur de script de l'IHM a l'ecran),
    // Emplois (un sous-menu : chacun y mene, comme la liste).

    // GLISSER une variable hors de l'arbre. La table ne recoit plus la souris
    // une fois dehors : l'ecran passe chaque evenement a routeDrag, qui depose
    // le nom dans l'editeur de script de l'IHM sous la souris (au point du
    // lacher) et remet la table au repos. Vrai : l'evenement est pris (le lacher).
    static bool routeDrag(const ui::InputEvent& ev);
    // Le nom de la variable tiree (vide : aucun glisser en cours).
    [[nodiscard]] std::string draggedName() const;
    // Inserer un nom dans le premier editeur de script de l'IHM montre (sous
    // `root`), a son curseur ; faux : aucun n'est montre.
    static bool insertInShownScript(ui::Widget& root, const std::string& name);

    // 1.11.1 (R1111-6) : LA BIBLIOTHEQUE DE L'EDITEUR DE VUE (une colonne
    // etroite) : le filtre au-dessus de la recherche, des colonnes plus
    // etroites ; un clic sur une variable la choisit (`picked`), pour la poser
    // dans la vue (un clic dans la vue, ou glissee jusqu'a elle).
    void setCompact(bool compact);
    [[nodiscard]] bool compact() const noexcept { return compact_; }
    // La colonne montree c -> la colonne de l'arbre (Column) : etroite, Accessible vient juste apres Nom.
    [[nodiscard]] std::size_t logicalColumn(std::size_t c) const noexcept {
        static constexpr std::size_t kCompact[ColumnCount] = {Name, Access, Type, Scope, Usage, Reference, Size};
        return compact_ && c < ColumnCount ? kCompact[c] : c;
    }
    [[nodiscard]] const ApiVarNode* selectedNode() const;      // nullptr : un dossier, ou rien
    const core::SignalPtr<const ApiVarNode&> picked = core::Signal<const ApiVarNode&>::create();
    // Les enfants d'un noeud (demandes au modele s'il le faut).
    [[nodiscard]] const std::vector<ApiVarNode>& childrenOf(const ApiVarNode& n) const { return kids(n, keyOf(n, {})); }
    // Le rectangle de la ligne d'un nom, si elle est montree (les essais, les sessions).
    [[nodiscard]] bool nameRect(const std::string& path, gfx::Rect& out) const;

    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] ui::DropDown&  filterBox() noexcept { return *filterBox_; }
    [[nodiscard]] ui::InputText& searchBox() noexcept { return *searchBox_; }

protected:
    void onLayout() override;

private:
    friend class ApiVarsRows;            // le modele de la table lit les lignes
    friend class ApiVarsTable;           // la table : le clic sur une case, le clic droit
    void cellClicked(std::size_t line, std::size_t column);
    [[nodiscard]] std::vector<ui::PopupMenu::Item> menuItems(std::size_t line);   // retient la ligne (menuChosen)
    void menuChosen(int id);
    struct Line {
        const ApiVarNode* node{nullptr};   // nullptr : la ligne « ... et N autres »
        std::string key;
        int         depth{0};
        bool        open{false}, expandable{false};
        std::size_t more{0};               // « ... et N autres »
        std::size_t uses{0};               // les emplois, ceux de dessous compris
    };
    void rebuild();
    void walk(const ApiVarNode& n, const std::string& parentKey, int depth, bool underMatch);
    [[nodiscard]] bool keepFilter(const ApiVarNode& n) const;
    [[nodiscard]] bool keepSearch(const ApiVarNode& n) const;
    [[nodiscard]] bool selfMatches(const ApiVarNode& n) const;
    [[nodiscard]] static std::string keyOf(const ApiVarNode& n, const std::string& parentKey);
    [[nodiscard]] bool narrowing() const noexcept { return filter_ != Filter::All || !search_.empty(); }

    std::vector<ApiVarNode> roots_;
    std::vector<Line>       lines_;
    std::set<std::string>   open_;          // deplies a la main (sans filtre ni recherche)
    std::set<std::string>   closedNarrow_;  // replies pendant ce filtre / cette recherche
    std::set<std::string>   allShown_;      // les tableaux montres en entier
    std::map<const ApiVarNode*, std::size_t> usesUnder_, leavesUnder_;   // poses par setNodes
    // Les enfants des noeuds `lazy` deja demandes, par cle (std::map : les
    // noeuds ne bougent pas, les lignes gardent leurs pointeurs).
    [[nodiscard]] const std::vector<ApiVarNode>& kids(const ApiVarNode& n, const std::string& key) const;
    Fetch fetch_;
    SearchSource searchSource_;
    FilterSource filterSource_;
    ui::PopupMenu* usesMenu_{nullptr};
    std::string    usesPath_;               // la variable dont la liste des emplois est ouverte
    std::size_t    menuLine_{0};            // la ligne du clic droit
    void prefetchFor(const std::vector<std::string>& paths);
    mutable std::map<std::string, std::vector<ApiVarNode>> fetched_;
    Filter      filter_{Filter::All};
    std::string search_;
    std::size_t matches_{0};
    bool        seeded_{false};             // les dossiers du haut ouverts une fois
    bool        compact_{false};            // R1111-6 : la bibliotheque de l'editeur de vue
    ui::DropDown*  filterBox_{nullptr};
    ui::InputText* searchBox_{nullptr};
    ui::TableView* table_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    core::ConnectionScope links_;
};

// Pour les essais et la maquette : un petit automate factice (deux globales
// structurees, un tableau de 64 vannes, deux unites et leurs portees).
[[nodiscard]] std::vector<ApiVarNode> apiVarsSample();

} // namespace app

namespace hmi { struct Project; }
namespace hmi::apivars { struct Node; class Model; }

namespace app {
// Le branchement sur le modele d'API-M (hmi/HmiApiVars.hpp) : un noeud du
// modele, tel que la vue le montre (ses enfants : `lazy`, par Model::children).
[[nodiscard]] ApiVarNode apiVarNodeOf(const hmi::apivars::Node& n);

// 1.11.1 (R1111-6) : une vue branchee sur le modele d'API-M, sans hote : les
// racines (Globales, unites ; puis `extra`, apres elles), les enfants a la
// demande (Model::children), la recherche (Model::search), le filtre
// Accessibles en ecriture (la table des adresses du projet), les emplois.
// Rend le nombre de variables (les globales et celles des unites).
std::size_t bindApiModel(HmiApiVarsView& view, std::shared_ptr<const hmi::apivars::Model> model, const hmi::Project& project,
                         std::vector<ApiVarNode> extra = {});

} // namespace app
