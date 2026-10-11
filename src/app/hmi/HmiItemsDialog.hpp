// =============================================================================
//  app/hmi/HmiItemsDialog.hpp - 1.12.7 : LE TABLEAU DES ELEMENTS D'UNE VALEUR
// -----------------------------------------------------------------------------
//  La valeur d'une liste, d'un vecteur, d'un tableau (une ou deux dimensions),
//  d'un dictionnaire ou d'un tuple (une constante, une valeur initiale) : une
//  ligne par element, au lieu du texte « un par ligne » d'avant.
//
//    LIST, VECTOR      #  | Valeur               (Ajouter, Supprimer, Monter, Descendre)
//    ARRAY[1..4]       Indice | Valeur           (les cases du type, ni plus ni moins)
//    ARRAY[1..2, 1..3] Ligne | [.., 1] | [.., 2] | [.., 3]   (une ligne par premiere dimension)
//    MAP[STRING] OF T  Cle | Valeur               (une cle STRING s'ecrit sans apostrophes)
//    TUPLE(INT, STRING) Element | Valeur          (Item1 : INT, Item2 : STRING)
//
//  Une case vide : une liste, une MAP - la ligne ne compte pas ; un tableau - les cases
//  qui suivent la derniere ecrite gardent la valeur de leur type, une case vide avant
//  prend la valeur nulle de son element (0, 0.0, FALSE, '', T#0s). Appliquer rend le
//  litteral ([..], (..)) ; la grille le verifie (Ctrl+Z le reprend).
// =============================================================================
#pragma once

#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiItemsDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string id{"dialog.hmiItems"};
        std::string title;          // « Elements de Seuils »
        std::string type;           // LIST OF REAL, MAP[STRING] OF INT, ARRAY[1..4] OF REAL, TUPLE(INT, STRING)
        std::string value;          // la valeur actuelle ([..], (..), une valeur seule, ou vide)
    };
    explicit HmiItemsDialog(Spec spec);
    ~HmiItemsDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return spec_.title; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // Le modele, sans ecran (les essais, la grille) : les lignes lues d'une valeur, le litteral rendu.
    class Model;
    [[nodiscard]] static std::shared_ptr<Model> modelOf(const std::string& type, const std::string& value);
    [[nodiscard]] static std::string literalOf(const Model&);
    [[nodiscard]] std::string literal() const;
    [[nodiscard]] Model& model() noexcept { return *model_; }
    [[nodiscard]] const ui::TableView* table() const noexcept { return table_; }   // les essais : ses colonnes
    // Les gestes (les boutons ; publics pour les essais). Faux : refuse (une taille fixe...).
    bool addRow();
    bool removeRow(std::size_t row);
    bool moveRow(std::size_t row, int delta);
    bool setCell(std::size_t row, std::size_t column, const std::string& text);

protected:
    core::Status buildUi() override;

private:
    void finish(bool ok);
    [[nodiscard]] std::size_t selected() const;

    Spec                   spec_;
    std::shared_ptr<Model> model_;
    ui::TableView*         table_{nullptr};
    ui::Widget*            body_{nullptr};
    bool                   done_{false};
    core::ConnectionScope  links_;
};

// Le modele des lignes (pour les essais : ses colonnes, ses cases).
class HmiItemsDialog::Model final : public ui::ITableModel {
public:
    enum class Mode : std::uint8_t { List, Array, Matrix, Map, Tuple };
    Mode                     mode{Mode::List};
    std::string              element;                  // le type des elements (List, Array, Matrix) ou des valeurs (Map)
    bool                     textKeys{true};           // une MAP a cle STRING (sinon : entiere)
    std::vector<std::string> labels;                   // Array : l'indice de chaque case ; Tuple : « Item1 : INT » ; Matrix : chaque ligne
    std::vector<std::string> columnLabels;             // Matrix : « [.., 1] »...
    std::vector<std::string> types;                    // Tuple : le type de chaque element
    std::vector<std::string> keys;                     // Map
    std::vector<std::string> values;                   // une case par ligne (Matrix : ligne par ligne)
    std::size_t              columns2{0};              // Matrix : les cases d'une ligne

    [[nodiscard]] bool resizable() const noexcept { return mode == Mode::List || mode == Mode::Map; }
    [[nodiscard]] std::size_t rowCount() const override;
    [[nodiscard]] std::size_t columnCount() const override;
    [[nodiscard]] std::string headerText(std::size_t c) const override;
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override;
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override;
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override;
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override;
};

} // namespace app
