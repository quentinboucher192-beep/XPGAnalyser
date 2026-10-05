// =============================================================================
//  app/hmi/HmiDuplicateDialog.hpp - "Dupliquer..." (1.10.2, chantier D)
// -----------------------------------------------------------------------------
//  Ctrl+D, le clic droit, la barre d'outils : N copies de la selection, chaque
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
// =============================================================================
#pragma once

#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiDuplicate.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui { class TabControl; class ScrollablePanel; }

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
    bool fitSpacing();                                   // "Ajuster l'espacement"
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
    void rebuildGrid();
    void refresh();                 // revalider, le bouton, la note, l'apercu
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
    // L'ecran.
    class Body;
    class CopiesPage;
    class LayoutPage;
    class Grid;
    class Preview;
    Body*                                body_{nullptr};
    ui::TabControl*                      tabs_{nullptr};
    CopiesPage*                          copiesPage_{nullptr};
    LayoutPage*                          layoutPage_{nullptr};
    ui::ScrollablePanel*                 gridPanel_{nullptr};
    Grid*                                grid_{nullptr};
    ui::InputText*                       copiesField_{nullptr};
    ui::DropDown*                        fillColumn_{nullptr};
    ui::InputText*                       fillPattern_{nullptr};
    ui::InputText*                       fillFrom_{nullptr};
    ui::DropDown*                        fillArray_{nullptr};    // 1.10.4 : Remplir > Tableau...
    std::vector<ui::Checkbox*>           follow_;
    std::vector<bool>                    followed_;      // 1.11 (REP-4) : le reglage de chaque case "Suivre"
    std::vector<ui::Checkbox*>           keepBoxes_;
    ui::Checkbox*                        replaceOriginal_{nullptr};
    std::shared_ptr<ui::RadioGroup>      axis_;
    ui::InputText*                       columnsField_{nullptr};
    ui::InputText*                       spacingField_{nullptr};
    ui::Button*                          fit_{nullptr};
    ui::Button*                          ok_{nullptr};
    core::ConnectionScope                links_;
    core::ConnectionScope                gridLinks_;
};

} // namespace app
