// =============================================================================
//  app/hmi/HmiItemDuplicateDialog.hpp - « Dupliquer... » d'un element (1.12.16)
// -----------------------------------------------------------------------------
//  « dans Courbe (contenu en general, actions aussi) pouvoir dupliquer une
//  plume, une action - en gardant le meme principe que dupliquer un symbole » :
//  une plume d'une courbe, une ligne du Contenu (un etat, un choix, une ligne
//  d'un tableau), une action, un raccourci - N copies :
//    - les indices suivis (Cuves[0].Niveau -> Cuves[1].Niveau...), chacun a sa
//      case « Suivre » (decochee d'office quand les copies sortiraient de ses
//      bornes connues) ; les morceaux a indices d'un repere decales ($V[1]$) ;
//    - le numero au bout d'un nom suivi (Plume 1 -> Plume 2, F2 -> F3) ;
//    - une couleur prise dans la palette ;
//    - le tableau des copies : une ligne par copie (l'original en tete, en
//      gris), une colonne par champ ; chaque case se corrige (l'aide a la saisie
//      dans une expression) et garde sa correction quand le nombre change.
//  « Dupliquer (N copies) » rend les copies (DialogResult::payload, parse) ;
//  l'appelant les insere apres l'original en UNE commande (Ctrl+Z retire tout).
//  Le moteur est hmi::dup (itemIndices, itemCopies), sans ecran.
// =============================================================================
#pragma once

#include "../../hmi/HmiDuplicate.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ui { class ScrollablePanel; }

namespace app {

class HmiItemDuplicateDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string                      id{"dialog.hmiItemDuplicate"};
        std::string                      title{"Dupliquer"};   // "Dupliquer la plume 2"
        std::string                      what;                 // la ligne du haut : "Plume 2 de Trend_1 : Cuves[0].Niveau"
        std::vector<hmi::dup::ItemField> fields;               // les colonnes (l'original)
        int                              copies{1};
        hmi::dup::BoundsFn               bounds;               // les bornes des tableaux (Suivre, les avertissements)
        ui::InputText::Assist            assist;               // l'aide a la saisie d'une case Expression ; vide : aucune
    };
    static constexpr int kMaxCopies = 99;

    explicit HmiItemDuplicateDialog(Spec spec);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return spec_.title; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // ---- ce que ferait la souris (scripts, essais) --------------------------
    [[nodiscard]] const std::vector<hmi::dup::IndexInfo>& indices() const noexcept { return indices_; }
    [[nodiscard]] bool following(std::size_t index) const noexcept { return index < follow_.size() && follow_[index]; }
    void setFollow(std::size_t index, bool on);
    void setCopies(int copies);
    [[nodiscard]] int copies() const noexcept { return copies_; }
    // Une case d'une copie (1..copies ; 0 : l'original, en lecture) ; faux : pas de telle case.
    bool setCell(int row, std::size_t column, std::string value);
    [[nodiscard]] std::string cell(int row, std::size_t column) const;
    [[nodiscard]] const std::vector<std::vector<std::string>>& rows() const noexcept { return rows_; }   // les copies
    [[nodiscard]] std::vector<std::string> warnings() const;   // un indice qui sort de ses bornes, une case vide
    [[nodiscard]] std::string confirmLabel() const;             // "Dupliquer (3 copies)"
    [[nodiscard]] bool canConfirm() const;
    bool confirm();                                             // « Dupliquer »
    void cancel();

    // Les copies, dans la reponse (DialogResult::payload) et relues.
    [[nodiscard]] static std::string encode(const std::vector<std::vector<std::string>>& rows);
    [[nodiscard]] static std::vector<std::vector<std::string>> parse(const std::string& payload);

protected:
    core::Status buildUi() override;

private:
    class Body;
    class Grid;
    void recompute();      // les cases non corrigees suivent le nombre de copies et les cases Suivre
    void rebuildCells();   // les cases du tableau (le nombre de lignes a change)
    void refresh();        // le bouton, les avertissements
    void finish(bool ok);

    Spec                                  spec_;
    std::vector<std::size_t>              columns_;   // les champs montres (Spec::fields, ItemField::shown)
    std::vector<hmi::dup::IndexInfo>      indices_;
    std::vector<bool>                     follow_;
    int                                   copies_{1};
    std::vector<std::vector<std::string>> rows_;      // [copie - 1][champ]
    std::vector<std::vector<bool>>        edited_;    // [copie - 1][champ] : corrigee a la main (gardee)
    Body*                                 body_{nullptr};
    Grid*                                 grid_{nullptr};
    ui::ScrollablePanel*                  gridPanel_{nullptr};
    ui::InputText*                        copiesField_{nullptr};
    std::vector<ui::Checkbox*>            followBoxes_;
    ui::Button*                           ok_{nullptr};
    bool                                  syncing_{false};
    bool                                  done_{false};
    core::ConnectionScope                 links_;
    core::ConnectionScope                 cellLinks_;
};

} // namespace app
