// =============================================================================
//  ui/widgets/TableFilters.hpp - la fenetre du filtre d'une colonne (lot recherche)
// -----------------------------------------------------------------------------
//  UN CLIC SUR L'ENTONNOIR D'UN TITRE DE COLONNE (TableView::setColumnFiltersEnabled) :
//
//    +------------------------------------------------+
//    | Filtrer : Commentaire                        x |
//    | [contient] [commence par] [=] [different de]   |
//    | [vide] [non vide] [entre]                      |
//    | [ pompe______________________ ]                |
//    |------------------------------------------------|
//    | Valeurs         [ chercher dans la liste__ ]   |
//    | [x] (Tout)                                 251 |
//    | [x] BOOL                                   120 |
//    | [ ] INT                                     80 |
//    |------------------------------------------------|
//    |                        [Effacer]  [Appliquer]  |
//    +------------------------------------------------+
//
//  La condition et la liste s'ajoutent (ET). La liste : les valeurs distinctes
//  de la colonne et combien de lignes chacune (200 au plus ; sa recherche les
//  resserre, "(Tout)" coche ou decoche celles montrees). "entre" n'est propose
//  que pour une colonne de nombres. Entree applique, Echap ferme, Tab passe
//  d'un champ a l'autre. Dessinee au-dessus de tout (requestsOverlayPass),
//  elle recoit les clics de toute sa surface (eventBounds) ; un clic ailleurs
//  (le clavier part) la ferme sans rien changer.
// =============================================================================
#pragma once

#include "DataViews.hpp"

#include <string>
#include <utility>
#include <vector>

namespace ui {

class ColumnFilterPopup final : public Widget {
public:
    struct Spec {
        std::size_t column{0};
        std::string title;                                         // le titre de la colonne
        ColumnFilter current;                                      // ce qui est applique (inactif : rien)
        std::vector<std::pair<std::string, std::size_t>> values;   // les valeurs distinctes, et combien
        bool        more{false};                                   // d'autres valeurs que celles-ci (200 au plus)
        bool        numeric{false};                                // une colonne de nombres : "entre"
        gfx::Rect   anchor{};                                      // sous quoi s'ouvrir (le titre)
    };

    explicit ColumnFilterPopup(std::string id = {});

    void open(Spec spec);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    [[nodiscard]] std::size_t column() const noexcept { return spec_.column; }
    [[nodiscard]] const Spec& spec() const noexcept { return spec_; }

    // ---- ce que la fenetre ferait, et les gestes sans la souris (scripts, tests) ----
    [[nodiscard]] ColumnFilter draft() const;
    [[nodiscard]] ColumnFilter::Op op() const noexcept { return op_; }
    void setOp(ColumnFilter::Op op);
    void setValue(const std::string& text, bool second = false);
    void setListSearch(const std::string& text);
    bool toggleValue(std::string_view value);          // faux : pas dans la liste
    void setAllShown(bool checked);                    // "(Tout)" : les valeurs montrees
    void apply();                                      // Appliquer
    void clearFilter();                                // Effacer

    // Appliquer (le filtre, peut-etre inactif), Effacer (la colonne), Echap ou la croix.
    const core::SignalPtr<const ColumnFilter&> applied = core::Signal<const ColumnFilter&>::create();
    const core::SignalPtr<std::size_t>         cleared = core::Signal<std::size_t>::create();
    const core::SignalPtr<>                    dismissed = core::Signal<>::create();

    [[nodiscard]] gfx::Rect popupRect() const;
    [[nodiscard]] gfx::Rect opRect(ColumnFilter::Op op) const;   // vide : pas proposee
    [[nodiscard]] gfx::Rect fieldRect(int field) const;          // 0 la valeur, 1 la seconde borne, 2 la recherche de la liste
    [[nodiscard]] gfx::Rect listRowRect(std::size_t shownIndex) const;   // 0 : "(Tout)" ; vide hors de vue
    [[nodiscard]] gfx::Rect applyRect() const;
    [[nodiscard]] gfx::Rect clearRect() const;
    [[nodiscard]] gfx::Rect closeRect() const;
    [[nodiscard]] std::size_t shownCount() const noexcept { return shown_.size(); }

    [[nodiscard]] bool      requestsOverlayPass() const override { return open_; }
    [[nodiscard]] gfx::Rect eventBounds() const override;

protected:
    void        onPaintOverlay(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;
    void        onFocusChanged(bool gained) override;

private:
    struct Item { std::string value; std::size_t count{0}; bool checked{true}; };
    struct Chip { ColumnFilter::Op op; gfx::Rect rect; };
    [[nodiscard]] std::vector<Chip> chips() const;               // les conditions, a leur place
    [[nodiscard]] std::vector<ColumnFilter::Op> ops() const;     // celles proposees
    [[nodiscard]] float chipsBottom() const;
    [[nodiscard]] gfx::Rect listRect() const;
    [[nodiscard]] int  visibleRows() const noexcept { return 8; }
    [[nodiscard]] bool needsValue() const noexcept;
    [[nodiscard]] std::string& fieldText(int field);
    void refilter();                                             // shown_ d'apres la recherche de la liste
    void typeInto(const std::string& utf8);
    void backspace();
    void nextField(bool backwards);

    Spec                     spec_;
    bool                     open_{false};
    ColumnFilter::Op         op_{ColumnFilter::Op::Contains};
    std::string              value_, value2_, listSearch_;
    int                      field_{0};                          // le champ qui a le clavier
    bool                     replaceOnType_{false};              // tout choisi : la frappe remplace
    std::vector<Item>        items_;
    std::vector<std::size_t> shown_;                             // les valeurs montrees (indices dans items_)
    float                    scroll_{0.f};                       // en lignes
    int                      hoverRow_{-1};
    int                      hoverChip_{-1};
};

} // namespace ui
