// =============================================================================
//  app/hmi/HmiDesignPanes.hpp - concevoir plus vite : les styles nommes,
//                               rechercher / remplacer (lot 12)
// -----------------------------------------------------------------------------
//  IHM > STYLES. Les styles du projet (combien d'objets les citent), et la
//  fiche du style choisi : son nom, sa description, ses valeurs d'apparence.
//  Une valeur changee : les objets qui le citent suivent, sauf ceux qui
//  l'avaient changee eux-memes ; renomme, ils le citent sous son nouveau nom ;
//  retire, ils gardent leurs valeurs. Chaque changement est une commande
//  (Ctrl+Z).
//
//  IHM > RECHERCHER / REMPLACER. Un texte, son remplacant, ou chercher (tout
//  le projet ou une vue), la casse, le mot entier : l'APERCU liste chaque champ
//  touche (ou, avant, apres) avant qu'on remplace. Remplacer tout : une
//  commande. Double-clic sur une ligne : la vue, l'objet choisi.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "HmiFolderTable.hpp"          // lot 21 : les styles ranges en dossiers
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiDesign.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/SearchField.hpp"   // lot API 8 : chercher un style

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui { class StatusBar; }

namespace app {

class HmiStylesPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiStylesPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();
    // Les gestes du volet, pour les boutons, les scripts et les tests.
    hmi::Id addStyle(const std::string& name = "Style");
    hmi::Id duplicateStyle(hmi::Id style);
    bool    removeStyle(hmi::Id style);
    // "nom", "description", ou une propriete d'apparence (fill, fontSize...) ;
    // une valeur vide : le style ne la porte plus.
    bool    setStyleField(hmi::Id style, const std::string& key, const std::string& value, std::string* why = nullptr);
    [[nodiscard]] hmi::Id selectedStyle() const;
    void selectStyle(hmi::Id);
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *grid_; }
    [[nodiscard]] ui::TableView&    table() noexcept { return *table_; }
    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] HmiFolderTable&   folders() noexcept { return *folders_; }     // lot 21
    // Lot API 8 : chercher un style (son nom, sa description, ses valeurs, son
    // dossier) ; "2 sur 9", retenue d'une seance a l'autre.
    [[nodiscard]] ui::SearchField&  search() noexcept { return *search_; }
    [[nodiscard]] std::size_t shownStyles() const noexcept { return shown_; }
protected:
    void onLayout() override;
private:
    void rebuildProperties();
    void say(std::string text, bool error = false);
    hmi::DocumentPtr      doc_;
    Apply                 apply_;
    HmiToolStrip*         tools_{nullptr};
    ui::SearchField*      search_{nullptr};     // lot API 8
    std::size_t           shown_{0};            // lot API 8 : les styles montres
    ui::TableView*        table_{nullptr};
    ui::PropertyGrid*     grid_{nullptr};
    ui::StatusBar*        status_{nullptr};
    std::unique_ptr<HmiFolderTable> folders_;       // lot 21
    std::string           message_;
    core::ConnectionScope links_;
};

class HmiFindPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiFindPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    // Chercher : l'apercu se remplit. Remplacer tout : une commande ; le nombre
    // de champs changes.
    void search();
    std::size_t replaceAll();
    void setFind(const std::string& text);
    void setReplace(const std::string& text);
    void setScopeView(hmi::Id view);            // kNoId : tout le projet
    void setMatchCase(bool on);
    void setWholeWord(bool on);
    [[nodiscard]] const std::vector<hmi::design::FindHit>& hits() const noexcept { return hits_; }
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    // Double-clic sur une ligne : ouvrir la vue (et choisir l'objet).
    const core::SignalPtr<hmi::Id, hmi::Id> openHit = core::Signal<hmi::Id, hmi::Id>::create();
protected:
    void onLayout() override;
private:
    void refreshScopes();
    [[nodiscard]] hmi::design::FindOptions options() const;
    void say(std::string text, bool error = false);
    hmi::DocumentPtr                   doc_;
    Apply                              apply_;
    ui::InputText*                     find_{nullptr};
    ui::InputText*                     replace_{nullptr};
    ui::DropDown*                      scope_{nullptr};
    ui::Checkbox*                      case_{nullptr};
    ui::Checkbox*                      word_{nullptr};
    ui::Button*                        searchButton_{nullptr};
    ui::Button*                        replaceButton_{nullptr};
    ui::TableView*                     table_{nullptr};
    ui::StatusBar*                     status_{nullptr};
    std::vector<hmi::Id>               scopes_;       // kNoId, puis les vues
    std::vector<hmi::design::FindHit>  hits_;
    std::string                        message_;
    core::ConnectionScope              links_;
};

} // namespace app
