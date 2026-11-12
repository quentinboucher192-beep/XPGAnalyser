// =============================================================================
//  app/hmi/HmiDuplicateDialog.hpp - "Dupliquer..." (1.10.2, chantier D)
// -----------------------------------------------------------------------------
//  Ctrl+Maj+D, le clic droit, la barre d'outils : N copies de la selection, chaque
//  repere ($Vanne$) remplace par la valeur de sa colonne, les indices des
//  tableaux suivis (V[0] -> V[1]...), posees sur X, sur Y ou en grille. Deux
//  onglets, comme la maquette :
//    "Les copies et leurs reperes" : la selection, les reperes et leurs
//      endroits, les indices (Suivre / Garder), le tableau des copies (une
//      ligne par copie, l'original en tete, l'aide a la saisie dans chaque
//      case), les remplissages (serie V1{n}, liste collee d'Excel, vider),
//      l'avant / apres de la ligne choisie ;
//    "La disposition" : X, Y ou grille (colonnes), l'espacement, le pas
//      mesure, l'apercu en direct, le depassement et le chevauchement,
//      "Ajuster l'espacement".
//  Le moteur est hmi::dup (sans ecran) ; "Dupliquer" rend le plan a l'appelant,
//  qui le joue en UNE commande (Ctrl+Z retire tout).
//
//  1.50.0 (maquette validee, planche 04 : « Dupliquer... qui tient 500 reperes ») :
//  « Les copies et leurs reperes » range les copies par des COMPTEURS (une suite n,
//  ou une grille i x j (x k), j variant le plus vite), donne UNE REGLE PAR PROFONDEUR
//  (Armoires[.] = i, Departs[.] = j, Voies[.] fixe ; les adresses %MW(1000 + 10 * i + j),
//  les textes 'Depart {i+1}.{j+1}', les noms 'Depart_{i}_{j}' - le calcul des formules
//  d'adresses, hmi::dup::computeRows), montre les reperes en LISTE groupee, comptee,
//  repliable, cherchable, filtrable et virtuelle (un repere « a part » a sa regle ; les
//  Remplir d'avant passent dans son clic droit), l'APERCU (les 5 premieres et les 3
//  dernieres copies, les totaux, l'avant / apres de la ligne choisie) et les CONFLITS
//  (hors des bornes : bloque ; adresse ou nom deja pris : avertit). « Tableau (Excel) »
//  garde la grille d'avant : la premiere colonne figee, le defilement, seules les cases
//  visibles se dessinent.
// =============================================================================
#pragma once

#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiDuplicate.hpp"
#include "../../hmi/HmiDuplicateRules.hpp"   // 1.50.0 : les compteurs, les regles par profondeur
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ui { class TabControl; class TableView; }

namespace app {

class HmiDuplicateDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        hmi::DocumentPtr     doc;
        hmi::Id              view{hmi::kNoId};
        std::vector<hmi::Id> selection;
        int                  copies{3};
        // L'aide a la saisie d'une case (app::fieldAssist) ; vide : aucune.
        ui::InputText::Assist assist;
        // Un chemin existe-t-il (app::describe) ; vide : les valeurs ne se verifient pas.
        hmi::dup::PathExists exists;
        // "Dupliquer" : le plan, a jouer en une commande.
        std::function<void(const hmi::dup::Plan&)> apply;
    };

    explicit HmiDuplicateDialog(Spec spec);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // ---- pour les scripts et les essais (ce que ferait la souris) ----------
    [[nodiscard]] const hmi::dup::Scan&       scanned() const noexcept { return scan_; }
    [[nodiscard]] const hmi::dup::Plan&       plan() const noexcept { return plan_; }
    [[nodiscard]] const hmi::dup::Validation& validation() const noexcept { return check_; }
    // Les colonnes du tableau : "Nom", puis "$Vanne$"...
    [[nodiscard]] std::vector<std::string> columns() const;
    // 1.11.1 (R1111-7) : la largeur de chaque colonne du tableau dans `width`
    // pixels (sans les numeros de ligne), a la mesure de son en-tete et de ses
    // cases : un repere long ($API.Tempon_TOR16_I4_OUT[1]$) prend la place qu'il
    // lui faut ; ce qui reste se partage ; quand tout ne tient pas, les plus
    // larges cedent d'abord (jamais sous `minimum`).
    [[nodiscard]] std::vector<float> gridColumns(float width) const;
    [[nodiscard]] static std::vector<float> fitColumns(const std::vector<float>& need, float width, float minimum);
    // L'en-tete de la colonne `c` dans `width` pixels : "$V[1].Ouv$  variable" ;
    // sinon le repere seul ; sinon coupe au milieu ("$API.Tem…OUT[1]$").
    [[nodiscard]] std::string gridHeader(std::size_t c, float width) const;
    void setCopies(int copies);
    // Une case (ligne 0 : l'original) ; faux : pas de telle case.
    bool setCell(int row, std::string_view column, std::string value);
    [[nodiscard]] std::string cell(int row, std::string_view column) const;
    // Remplir une colonne, a partir de la ligne 1 (les copies) ; `fromOriginal` : la ligne 0 aussi.
    bool fillSeries(std::string_view column, std::string_view pattern, std::string_view from, bool fromOriginal = false);
    bool fillList(std::string_view column, std::string_view pasted, bool fromOriginal = false);
    // 1.10.4 : Remplir > Tableau... - la colonne recoit V[i], V[i+1]... depuis
    // l'indice de l'original (celui que l'objet suit, sinon la borne basse), ligne 0 comprise.
    bool fillArray(std::string_view column, std::string_view array);
    [[nodiscard]] std::vector<std::string> arrays() const;   // ce que propose Tableau... ("V (0..63)")
    // 1.10.4 : la note du preremplissage ("$Vanne$ est prerempli avec V[0], V[1]...") ; vide : aucun.
    [[nodiscard]] const std::string& note() const noexcept { return note_; }
    bool clearColumn(std::string_view column);
    void setKeep(std::string_view marker, bool keep);   // "garder le repere" (une case vide ne bloque pas)
    void setFollow(std::size_t index, bool follow);      // un indice : Suivre / Garder
    // 1.11 (REP-4, decision 72) : la case "Suivre V[0]" d'un indice hors repere - cochee
    // par defaut si aucun repere ne porte d'indice, decochee des qu'un repere en porte un.
    [[nodiscard]] bool following(std::size_t index) const noexcept { return index < followed_.size() && followed_[index]; }
    void setReplaceOriginal(bool on);
    void setLayout(const hmi::dup::Layout& layout);
    [[nodiscard]] const hmi::dup::Layout& layout() const noexcept { return plan_.layout; }
    bool fitSpacing();                                   // "Ajuster l'espacement" (1.12.16 : en X, en Y, ou chacun)
    [[nodiscard]] std::vector<int> overflow() const;     // les copies hors de la vue
    [[nodiscard]] std::vector<int> overlap() const;      // celles qui chevauchent un objet
    void showTab(std::size_t index);
    [[nodiscard]] std::size_t currentTab() const;
    void chooseRow(int row);                             // l'avant / apres montre
    [[nodiscard]] int chosenRow() const noexcept { return chosen_; }
    // "15 avant / apres" de la ligne : "text : $Vanne$ -> V102".
    [[nodiscard]] std::vector<std::string> beforeAfter(int row) const;
    [[nodiscard]] bool        canConfirm() const;
    [[nodiscard]] std::string confirmLabel() const;      // "Dupliquer (7 copies)"
    [[nodiscard]] std::string blockingText() const;      // pourquoi c'est gris
    bool confirm();                                      // "Dupliquer"

    // ---- 1.50.0 (planche 04) : les compteurs, les regles, la liste, l'apercu, les conflits ----
    [[nodiscard]] const hmi::dup::Model& model() const noexcept { return model_; }
    [[nodiscard]] const hmi::dup::Rules& rules() const noexcept { return rules_; }
    // « Une grille : i x j » (1 a 3 compteurs) ; setCopies : « Une suite : n ». Changer de sorte remet les regles
    // des profondeurs a celles d'office de la nouvelle sorte (i, j, k par profondeur ; o + n) ; les reperes a part restent.
    void setGrid(std::vector<hmi::dup::Range> ranges);
    void prefillFromBounds();                            // « Preremplir depuis les bornes »
    void setCounterK(bool on);                           // « + un compteur k »
    // La regle d'un groupe (sa cle, son libelle ou son dernier morceau : "Departs[\xC2\xB7]", "Adresses", "Noms des objets").
    bool setGroupRule(std::string_view group, hmi::dup::Rule rule);
    bool setVaries(std::string_view group, bool varies); // la colonne « Varie ? »
    [[nodiscard]] const hmi::dup::Group* findGroup(std::string_view group) const;
    // Un repere « a part » (`marker` : "$Nom$" ou "Nom") ; resetOwn : « Comme sa profondeur ».
    bool setOwnRule(std::string_view marker, hmi::dup::Own own);
    bool resetOwn(std::string_view marker);
    // La regle a part ecrite : une formule (un repere a indices : pour la profondeur `group`, vide : la plus grande).
    bool setOwnFormula(std::string_view marker, std::string formula, std::string_view group = {});
    // ---- la liste des reperes (virtuelle) ----
    enum class ListFilter : std::uint8_t { All, Indices, Addresses, Texts, Own, Conflicts };
    enum class Grouping : std::uint8_t { Depth, Object, None };
    struct ListRow {
        bool        group{false};
        std::string key;             // un groupe : sa cle ; un repere : markerKey
        std::size_t marker{0};       // un repere : son rang dans scanned().markers
        std::string text, where, rule, last;
        std::size_t count{0};        // un groupe : ses reperes montres
        bool        expanded{true};
        bool        own{false};      // a part
        int         conflict{0};     // 0 aucun, 1 avertissement, 2 faute
    };
    void setSearch(std::string text);
    void setListFilter(ListFilter f);
    void setGrouping(Grouping g);
    void expandAll(bool expanded);                       // « Tout deplier » / « Tout replier »
    void toggleGroup(std::string_view key);
    [[nodiscard]] const std::vector<ListRow>& listRows() const noexcept { return listRows_; }
    [[nodiscard]] std::size_t listCount(ListFilter f) const;
    // ---- l'apercu ----
    struct Stats {
        std::size_t copies{0}, places{0}, markers{0}, replacements{0}, conflicts{0}, faults{0};
    };
    [[nodiscard]] Stats stats() const;
    // Les lignes de l'apercu : les 5 premieres, -1 (l'ecart), les 3 dernieres ; « Tout voir » : toutes.
    [[nodiscard]] std::vector<int> previewRows() const;
    void showAllPreview(bool all);
    // ---- les conflits (les fautes d'abord) ----
    struct Conflict {
        bool        blocks{false};
        std::string text;
        int         row{0};          // la premiere ligne en cause (0 : l'original)
        std::string column;
    };
    [[nodiscard]] const std::vector<Conflict>& conflicts() const noexcept { return conflicts_; }
    // ---- pour les essais : l'ecran ----
    [[nodiscard]] gfx::Rect panelRect() const;           // le cadre du dialogue
    [[nodiscard]] ui::TableView* depthTable() const noexcept { return depthTable_; }
    [[nodiscard]] ui::TableView* markerList() const noexcept { return listTable_; }
    [[nodiscard]] ui::TableView* previewTable() const noexcept { return previewTable_; }
    // « Tableau (Excel) » : les cases dessinees a la derniere image (lignes x colonnes), la colonne figee, le defilement.
    struct SheetStats {
        std::size_t rows{0}, columns{0}, drawnCells{0}, frozenColumns{0};
        float       scrollX{0}, scrollY{0};
    };
    [[nodiscard]] SheetStats sheetStats() const;
    void scrollSheet(float x, float y);

    // 1.11 (R111) : "veux-tu dire" sur un membre de l'automate. `path` ne mene a
    // rien (Armoires[0].sortie.V8) : le debut qui existe reste (Armoires[0]) ;
    // chaque morceau qui n'existe pas prend le membre le plus proche parmi ceux
    // que l'aide a la saisie propose apres le point (les membres de la DDT) ; un
    // premier morceau inconnu, la variable la plus proche de `variables`. Seuls
    // les chemins qui existent sont proposes (Armoires[0].sorties.V8), 3 au plus.
    [[nodiscard]] static std::vector<std::string> memberSuggestions(std::string_view path, const hmi::dup::PathExists& exists,
                                                                    const ui::InputText::Assist& assist,
                                                                    const std::vector<std::string>& variables = {});

protected:
    core::Status buildUi() override;

private:
    void rebuildGrid();             // 1.50.0 : le tableau (Excel) relit ses colonnes
    void writeCell(int row, int column, std::string value);   // une case, sans revalider
    void refresh();                 // revalider, le bouton, la note, l'apercu
    // 1.50.0 : les lignes d'apres les regles ; `countersChanged` : une case vide d'un repere a ses cases se remplit
    // comme avant (son morceau decale, la colonne preremplie), un nom tape vide prend la serie.
    void recompute(bool countersChanged);
    void setCounters(const hmi::dup::Counters& c);
    void markCells(std::size_t marker, std::string how);
    void buildList();
    void buildConflicts();
    void syncWidgets();             // les tableaux, la bande des compteurs
    void openOwnEditor(int mode);   // 1 : Formule... ; 2 : Serie (V1{n})
    [[nodiscard]] std::vector<std::size_t> selectedMarkers() const;
    [[nodiscard]] std::string ruleText(std::size_t marker) const;
    [[nodiscard]] int conflictOf(int row, const std::string& column) const;
    void validate();                // 1.11 (R111) : dup::validate, puis "veux-tu dire" sur un membre
    void finish(bool ok);
    [[nodiscard]] int columnIndex(std::string_view column) const;
    [[nodiscard]] const hmi::View* view() const;

    Spec                                 spec_;
    hmi::dup::Scan                       scan_;
    hmi::dup::Plan                       plan_;
    hmi::dup::Validation                 check_;
    std::vector<std::string>             keep_;          // markerKey des colonnes "garder le repere"
    std::vector<std::string>             candidates_;    // pour "veux-tu dire"
    std::optional<hmi::dup::Prefill>     prefilled_;     // 1.10.4 : la colonne preremplie (V[0], V[1]...)
    std::string                          note_;          // 1.10.4 : ce que la note en dit
    int                                  chosen_{1};
    bool                                 done_{false};
    bool                                 syncing_{false};
    // 1.50.0 : les regles.
    hmi::dup::Model                      model_;
    hmi::dup::Rules                      rules_;
    std::vector<hmi::dup::RuleError>     ruleErrors_;
    std::vector<std::string>             ruleKeep_;      // les reperes que les regles gardent (fixe, Garder)
    std::map<std::string, std::string>   addressOwners_; // les adresses du projet -> leur variable
    std::vector<hmi::dup::CellCheck>     warnings_;      // les adresses deja prises
    std::vector<Conflict>                conflicts_;
    std::map<std::pair<int, std::string>, int> flags_;   // (ligne, markerKey ou "NOM") -> 1 avertit, 2 bloque
    std::string                          search_;
    ListFilter                           filter_{ListFilter::All};
    Grouping                             grouping_{Grouping::Depth};
    std::set<std::string>                collapsed_;
    std::vector<ListRow>                 listRows_;
    bool                                 previewAll_{false};
    std::vector<std::size_t>             editing_;       // les reperes de la regle a part en cours d'ecriture
    int                                  editMode_{0};
    // L'ecran.
    class Body;
    class CopiesPage;
    class LayoutPage;
    class Preview;
    class Sheet;
    class Band;
    class DepthModel;
    class ListModel;
    class PreviewModel;
    class ConflictModel;
    hmi::dup::BoundsFn                   bounds_;        // 1.50.0 : les bornes, relevees une fois (la fenetre est modale)
    std::size_t                          replaced_{0};   // 1.50.0 : les remplacements du plan (l'apercu)
    std::vector<int>                     previewIndex_;  // les lignes de l'apercu (-1 : l'ecart)
    std::vector<std::size_t>             samples_;       // les reperes montres en colonnes dans l'apercu
    std::shared_ptr<DepthModel>          depthModel_;
    std::shared_ptr<ListModel>           listModel_;
    std::shared_ptr<PreviewModel>        previewModel_;
    std::shared_ptr<ConflictModel>       conflictModel_;
    Body*                                body_{nullptr};
    ui::TabControl*                      tabs_{nullptr};
    CopiesPage*                          copiesPage_{nullptr};
    LayoutPage*                          layoutPage_{nullptr};
    Sheet*                               sheet_{nullptr};       // 1.50.0 : « Tableau (Excel) », virtuel
    Band*                                band_{nullptr};
    ui::TableView*                       depthTable_{nullptr};
    ui::TableView*                       listTable_{nullptr};
    ui::TableView*                       previewTable_{nullptr};
    ui::TableView*                       conflictTable_{nullptr};
    ui::InputText*                       searchField_{nullptr};
    ui::InputText*                       ownField_{nullptr};
    ui::InputText*                       ownFrom_{nullptr};
    ui::DropDown*                        ownDepth_{nullptr};
    std::vector<ui::Button*>             ownButtons_;
    std::vector<ui::Button*>             modeButtons_;          // Une suite / Une grille
    std::vector<ui::Button*>             filterButtons_;
    std::vector<ui::Button*>             groupButtons_;
    std::vector<ui::Button*>             foldButtons_;          // Tout deplier / Tout replier
    std::vector<ui::InputText*>          rangeFields_;          // i de, a ; j de, a ; k de, a
    ui::Button*                          kButton_{nullptr};
    ui::Button*                          boundsButton_{nullptr};
    ui::InputText*                       copiesField_{nullptr};
    std::vector<bool>                    followed_;      // 1.11 (REP-4) : le reglage de chaque case "Suivre" (1.50.0 : la regle de sa profondeur)
    ui::Checkbox*                        replaceOriginal_{nullptr};
    std::shared_ptr<ui::RadioGroup>      axis_;
    ui::InputText*                       columnsField_{nullptr};
    ui::InputText*                       spacingField_{nullptr};    // l'espacement en X
    ui::InputText*                       spacingYField_{nullptr};   // 1.12.16 : l'espacement en Y
    ui::Button*                          fit_{nullptr};
    ui::Button*                          ok_{nullptr};
    core::ConnectionScope                links_;
    core::ConnectionScope                gridLinks_;
};

} // namespace app
