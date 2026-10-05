// =============================================================================
//  app/hmi/HmiAssetPanes.hpp — IHM > Ressources et IHM > Fichiers externes
// -----------------------------------------------------------------------------
//  RESSOURCES : le gestionnaire. Un tableau (nom, genre, format, dimensions ou
//  duree, poids, utilisations), un apercu (l'image, l'onde d'un son, la police
//  ecrite, l'affiche d'une video), et ce qui cite la ressource. Importer,
//  remplacer, renommer (les objets suivent), supprimer ; montrer et retirer
//  les inutilisees.
//
//  FICHIERS EXTERNES : le tableau des liens (nom, type, chemin, date, taille,
//  etat) et l'apercu du contenu - les lignes d'un CSV, d'une feuille Excel,
//  d'une table SQLite, d'un JSON ou d'un XML tabulaires. Relier prend acte
//  d'un fichier modifie.
//
//  Tout passe par des commandes (hmi::changeProject) : Ctrl+Z reprend un
//  import, un renommage (et la mise a jour des objets qui va avec), un retrait.
//  Les dialogues (chemin a saisir, confirmation) sont a l'hote ; sans hote, les
//  methodes publiques font l'action directement (tests, scripts).
// =============================================================================
#pragma once

#include "HmiImages.hpp"
#include "HmiFolderTable.hpp"          // lot 21 : les ressources rangees en dossiers
#include "HmiPanels.hpp"
#include "../../core/Command.hpp"
#include "../../hmi/HmiAssets.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

// ------------------------------------------------------------------ apercus ---
class HmiResourcePreview final : public ui::Widget {
public:
    explicit HmiResourcePreview(std::string id) : ui::Widget(std::move(id)) {}
    void show(const hmi::Project* project, hmi::Id resource) { project_ = project; resource_ = resource; invalidate(); }
protected:
    void onPaint(const ui::PaintContext&) override;
private:
    const hmi::Project* project_{nullptr};
    hmi::Id             resource_{hmi::kNoId};
};

class HmiResourcesPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiResourcesPane(std::string id, hmi::DocumentPtr doc, Apply apply);

    void refresh();
    [[nodiscard]] hmi::Id selectedResource() const;
    void selectResource(hmi::Id);
    // 0 toutes, 1 images, 2 sons, 3 videos, 4 polices.
    void setKindFilter(int index);
    void setSearch(std::string text);
    void setUnusedOnly(bool on);
    [[nodiscard]] bool unusedOnly() const noexcept { return unusedOnly_; }
    [[nodiscard]] std::size_t rowCount() const noexcept { return order_.size(); }
    [[nodiscard]] HmiFolderTable& folders() noexcept { return *folders_; }     // lot 21 : rangees en dossiers

    // Les actions. Faux (et `why`) quand rien n'est fait. Par identifiant : un
    // dialogue repond apres coup, quand la selection a peut-etre change.
    bool importFile(const std::string& path, const std::string& name = {}, std::string* why = nullptr);
    bool replaceResource(hmi::Id, const std::string& path, std::string* why = nullptr);
    bool renameResource(hmi::Id, const std::string& name, std::string* why = nullptr);
    bool deleteResource(hmi::Id);
    bool replaceSelected(const std::string& path, std::string* why = nullptr) { return replaceResource(selectedResource(), path, why); }
    bool renameSelected(const std::string& name, std::string* why = nullptr) { return renameResource(selectedResource(), name, why); }
    bool deleteSelected() { return deleteResource(selectedResource()); }
    std::size_t removeUnused();
    // Ecouter le son choisi ; encore : l'arreter. Faux (et le message) sans
    // sortie audio ou si ce n'est pas un son.
    bool togglePlay();

    struct Hosts {
        std::function<void()>        importFile;            // demande un chemin
        std::function<void(hmi::Id)> replace, rename, remove;
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    [[nodiscard]] HmiToolStrip&  tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    // Un fichier glisse depuis l'explorateur sur le volet : importe.
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void say(std::string text, bool warning = false);
    hmi::DocumentPtr    doc_;
    Apply               apply_;
    Hosts               hosts_;
    HmiToolStrip*       tools_{nullptr};
    ui::DropDown*       kind_{nullptr};
    ui::InputText*      search_{nullptr};
    ui::Splitter*       split_{nullptr};
    ui::TableView*      table_{nullptr};
    HmiResourcePreview* preview_{nullptr};
    ui::StatusBar*      status_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::vector<hmi::Id> order_;
    std::unique_ptr<HmiFolderTable> folders_;          // lot 21
    int                  kindFilter_{0};
    std::string          searchText_;
    bool                 unusedOnly_{false};
    std::string          message_;
    core::ConnectionScope links_;
};

// ------------------------------------------------------------ fichiers externes ---
class HmiFilePreview final : public ui::Widget {
public:
    explicit HmiFilePreview(std::string id);
    void show(const hmi::ExternalFile* file);
    [[nodiscard]] const hmi::ExternalData* data() const noexcept { return data_.get(); }
    // ---- Lot API 8 : glisser de fichiers, 2e partie ----
    //  Un document (PDF, DOCX, ZIP...) : son nom, sa taille, et le bouton
    //  "Ouvrir avec le programme du systeme" (le fichier present).
    [[nodiscard]] bool documentShown() const noexcept { return !openPath_.empty(); }
    bool openDocument(std::string* why = nullptr);
    // ---- fin Lot API 8 : glisser de fichiers, 2e partie ----
protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
private:
    std::shared_ptr<const hmi::ExternalData> data_;
    std::string                              title_;
    ui::TableView*                           grid_{nullptr};
    std::string                              openPath_;          // lot API 8 : le document montre (chemin resolu)
    ui::Button*                              open_{nullptr};
    core::ConnectionScope                    links_;
};

class HmiFilesPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiFilesPane(std::string id, hmi::DocumentPtr doc, Apply apply);

    void refresh();
    [[nodiscard]] hmi::Id selectedFile() const;
    void selectFile(hmi::Id);
    [[nodiscard]] std::size_t rowCount() const noexcept { return order_.size(); }

    bool link(hmi::ExternalKind, const std::string& path, const std::string& name = {}, const std::string& part = {},
              std::string* why = nullptr);
    bool relinkFile(hmi::Id);
    bool removeFile(hmi::Id);
    bool setPart(hmi::Id, const std::string& part);
    bool relinkSelected() { return relinkFile(selectedFile()); }
    bool removeSelected() { return removeFile(selectedFile()); }
    bool setPartOfSelected(const std::string& part) { return setPart(selectedFile(), part); }
    // Lot API 8 : le document choisi, ouvert avec le programme du systeme.
    bool openSelected(std::string* why = nullptr) { return preview_->openDocument(why); }

    struct Hosts {
        std::function<void()>        linkFile, linkDatabase;
        std::function<void(hmi::Id)> remove, choosePart;
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    [[nodiscard]] HmiToolStrip&   tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&  table() noexcept { return *table_; }
    [[nodiscard]] HmiFilePreview& preview() noexcept { return *preview_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    // Un fichier glisse sur le volet : lie, son type tire de l'extension.
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void say(std::string text, bool warning = false);
    hmi::DocumentPtr doc_;
    Apply            apply_;
    Hosts            hosts_;
    HmiToolStrip*    tools_{nullptr};
    ui::Splitter*    split_{nullptr};
    ui::TableView*   table_{nullptr};
    HmiFilePreview*  preview_{nullptr};
    ui::StatusBar*   status_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::vector<hmi::Id> order_;
    std::string      message_;
    core::ConnectionScope links_;
};

// Choisir une ligne de modele dans un TableView (et prevenir ceux qui suivent
// sa selection), sans clic.
// Le rang dans le modele (un indice de liste : std::size_t, converti ici une fois).
void hmiSelectModelRow(ui::TableView&, std::size_t modelRow);

} // namespace app
