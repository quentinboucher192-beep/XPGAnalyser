// =============================================================================
//  app/hmi/HmiSemanticPane.hpp - 1.23.1 : IHM > Styles > Couleurs semantiques
// -----------------------------------------------------------------------------
//  Le client (05/11) : << dans Styles, un onglet couleurs semantiques (globales),
//  utilisables partout comme des constantes globales, pouvoir en definir soi-meme >>.
//
//  Le deuxieme onglet de la page Styles. A gauche, la liste : celles du projet
//  (une personne), puis les fournies (un cadenas) - le nom a citer (@Alarme), la
//  couleur (une pastille et sa valeur), la description, le nombre de citations.
//  A droite, la fiche de la couleur choisie : son nom, sa couleur (la palette ;
//  une autre couleur semantique aussi, @Autre), sa description, son dossier, ou
//  elle est citee, ce qui est a verifier. Une fournie se lit sans se modifier
//  (<< Dupliquer a partir de... >> en fait une a soi).
//
//  La barre et le clic droit : Nouvelle couleur (Inser), Dupliquer a partir de...
//  (Ctrl+D), Copier << @Nom >> (Ctrl+C), Retirer (Suppr). Chaque changement est
//  une commande (Ctrl+Z) : renommer fait suivre ce qui la cite, retirer y ecrit
//  sa couleur en dur (hmi/HmiSemanticColors.hpp).
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui {
class StatusBar;
}

namespace app {

class HmiToolStrip;

class HmiSemanticColorsPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    enum Action : int { ActAdd = 1, ActDuplicate, ActCopy, ActRemove };

    HmiSemanticColorsPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();

    // Les gestes (les essais, l'arbre du projet, le clic droit) - faux / kNoId : refuse, `why` le dit.
    hmi::Id addColor(const std::string& name = "Couleur", const std::string& value = "#808080", std::string* why = nullptr);
    hmi::Id duplicateFrom(const std::string& name, const std::string& newName = {}, std::string* why = nullptr);
    bool    removeColor(hmi::Id color, std::string* why = nullptr);
    bool    renameColor(hmi::Id color, const std::string& name, std::string* why = nullptr);
    bool    setValue(hmi::Id color, const std::string& value, std::string* why = nullptr);
    // "description", "dossier".
    bool    setField(hmi::Id color, const std::string& key, const std::string& value, std::string* why = nullptr);

    // La couleur choisie : son nom (une fournie aussi) ; "" : aucune.
    [[nodiscard]] std::string selectedName() const;
    // Une couleur du projet ; kNoId : aucune, ou une fournie.
    [[nodiscard]] hmi::Id     selectedColor() const;
    [[nodiscard]] bool        locked() const;          // la couleur choisie est fournie (verrouillee)
    bool select(const std::string& name);              // sans casse, avec ou sans @
    [[nodiscard]] const std::vector<std::string>& shownNames() const noexcept { return order_; }   // la liste, dans l'ordre

    // Le menu du clic droit (les essais le lisent) : ses entrees, leur id = l'action.
    [[nodiscard]] std::vector<ui::PopupMenu::Item> contextItems() const;
    void runAction(int action);

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     table() noexcept { return *table_; }
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *grid_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildProperties();
    void say(std::string text, bool error = false);
    bool fail(std::string text, std::string* why);
    [[nodiscard]] std::string checkValue(const std::string& self, const std::string& value) const;   // "" : bonne
    [[nodiscard]] std::string checkName(const std::string& name, hmi::Id except) const;              // "" : bon

    hmi::DocumentPtr      doc_;
    Apply                 apply_;
    HmiToolStrip*         tools_{nullptr};
    ui::TableView*        table_{nullptr};
    ui::PropertyGrid*     grid_{nullptr};
    ui::StatusBar*        status_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::vector<std::string> order_;
    bool                  refreshing_{false};
    std::string           message_;
    core::ConnectionScope links_;
};

} // namespace app
