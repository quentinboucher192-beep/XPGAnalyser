// =============================================================================
//  app/hmi/HmiFolderTable.hpp - une liste de l'IHM rangee en dossiers (lot 21)
// -----------------------------------------------------------------------------
//  CE QUE LES VARIABLES IHM FAISAIENT SEULES (lot 16), POUR LES AUTRES LISTES :
//  les vues, les popups, les ecrans modeles, les en-tetes, les pieds de page,
//  les symboles, les scripts generaux, les types IHM, les styles, les
//  ressources. La table devient un arbre : une ligne par dossier (sa fleche,
//  combien il contient), ses elements dessous, dans l'ordre du projet.
//
//  LE GLISSER-DEPOSER, d'une ou de plusieurs lignes (Ctrl+clic, Maj+clic) :
//   - sur un dossier : elles y sont rangees ; sous les lignes : a la racine ;
//   - entre deux lignes : elles s'y placent (l'ordre du projet change) et
//     prennent le dossier de leur voisine ;
//   - un dossier glisse sur un autre y va, avec tout ce qu'il contient.
//  Une commande par geste : Ctrl+Z le defait d'un coup.
//
//  Le volet garde ses colonnes et ses cases : il donne, pour chaque element
//  montre, ses cases et son style ; l'aide fait le reste (les lignes de
//  dossiers, la selection gardee d'une reconstruction a l'autre, les dossiers
//  replies, le renommage d'un dossier dans sa case).
// =============================================================================
#pragma once

#include "../../core/Command.hpp"
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiFolders.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace app {

class FolderRowsModel;

class HmiFolderTable {
public:
    struct Row {
        bool        folder{false};
        hmi::Id     id{hmi::kNoId};    // un element
        std::string path;              // le dossier (sa ligne), ou celui de l'element
        int         depth{0};
    };
    using Apply = std::function<void(core::CommandPtr)>;
    using Cells = std::function<std::vector<std::string>(hmi::Id)>;
    using Style = std::function<ui::CellStyle(hmi::Id, std::size_t column)>;

    HmiFolderTable(ui::TableView& table, hmi::DocumentPtr doc, Apply apply);
    ~HmiFolderTable();
    HmiFolderTable(const HmiFolderTable&) = delete;
    HmiFolderTable& operator=(const HmiFolderTable&) = delete;

    // La table, reconstruite : `list` (vide : une liste melee - pas de dossiers,
    // ni de glisser), les elements montres (dans l'ordre du projet), leurs cases.
    void rebuild(std::optional<hmi::fold::List> list, const std::vector<hmi::Id>& shown, std::vector<std::string> headers,
                 const Cells& cells, const Style& style);
    // Une case d'un element qui s'edite dans la table (double-clic, F2) : le nom
    // d'un type, par exemple. Rend vrai si la valeur est prise.
    void setItemEditing(std::function<bool(hmi::Id, std::size_t)> editable,
                        std::function<bool(hmi::Id, std::size_t, const std::string&)> commit) {
        itemEditable_ = std::move(editable);
        itemCommit_ = std::move(commit);
    }
    // La ligne (du modele) d'un element ; -1 : pas montre.
    [[nodiscard]] int rowOfItem(hmi::Id) const noexcept;
    [[nodiscard]] std::optional<hmi::fold::List> list() const noexcept { return list_; }
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }
    [[nodiscard]] const Row* row(ui::RowIndex modelRow) const noexcept { return modelRow < rows_.size() ? &rows_[modelRow] : nullptr; }
    [[nodiscard]] hmi::Id itemAt(ui::RowIndex modelRow) const noexcept;
    [[nodiscard]] std::vector<hmi::Id> selectedItems() const;
    [[nodiscard]] hmi::Id selectedItem() const;              // le premier element choisi (kNoId : aucun)
    [[nodiscard]] std::string selectedFolder() const;        // une ligne de dossier choisie ("" : aucune)
    [[nodiscard]] std::string targetFolder() const;          // ou va un nouvel element : ce dossier, ou celui de l'element choisi
    [[nodiscard]] std::size_t itemCount() const noexcept { return items_; }
    [[nodiscard]] std::size_t folderCount() const noexcept { return rows_.size() - items_; }
    void selectItem(hmi::Id id);                             // ses dossiers se deplient
    void selectFolder(const std::string& path);
    [[nodiscard]] bool collapsed(const std::string& path) const;
    void setCollapsed(const std::string& path, bool on);

    // ---- les gestes (une commande chacun ; faux : refuse, `message` dit pourquoi)
    std::string newFolder();                                 // "Nouveau dossier" dans targetFolder() ; rend son chemin
    bool renameFolder(const std::string& path, const std::string& name);   // `name` : le dernier nom
    bool deleteFolder(const std::string& path);              // ses elements montent d'un cran
    bool moveItems(const std::vector<hmi::Id>& ids, const std::string& folder);
    bool drop(const std::vector<ui::RowIndex>& rows, ui::RowIndex to, ui::TreeView::DropWhere where);

    // Ce qui s'est passe, en clair (la barre d'etat du volet).
    const core::SignalPtr<const std::string&> message = core::Signal<const std::string&>::create();
    // Un dossier replie ou deplie : le volet se reconstruit (rien n'a change dans le projet).
    const core::SignalPtr<> relayout = core::Signal<>::create();

private:
    [[nodiscard]] static std::string upper(const std::string&);
    ui::TableView&                   table_;
    hmi::DocumentPtr                 doc_;
    Apply                            apply_;
    std::optional<hmi::fold::List>   list_;
    std::vector<Row>                 rows_;
    std::size_t                      items_{0};
    std::set<std::string>            collapsed_;      // en majuscules
    std::shared_ptr<FolderRowsModel> model_;
    std::string                      editAfter_;      // un dossier cree : son nom se tape tout de suite
    std::function<bool(hmi::Id, std::size_t)>                     itemEditable_;
    std::function<bool(hmi::Id, std::size_t, const std::string&)> itemCommit_;
    core::ConnectionScope            links_;
};

} // namespace app
