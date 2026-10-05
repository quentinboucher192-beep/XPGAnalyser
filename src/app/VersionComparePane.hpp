// =============================================================================
//  app/VersionComparePane.hpp - comparer deux versions (lot 21)
// -----------------------------------------------------------------------------
//  A GAUCHE, LES DIFFERENCES RANGEES PAR ELEMENT : l'API puis l'IHM, par
//  categorie (Sections, Variables globales ; Vues, Variables IHM, Alarmes,
//  Scripts, Ressources...). Les fichiers identiques ne sont pas listes.
//  A DROITE, L'ELEMENT CHOISI :
//    - un texte (une section ST, un script, une fonction, les variables
//      globales) : cote a cote, les lignes retirees en rouge, ajoutees en vert,
//      modifiees face a face ; ou "unifie" ;
//    - une vue : ses deux vignettes, les objets changes entoures (ajoutes en
//      vert, retires en rouge), et en clair ce qui a change ;
//    - le reste : ce qui a change, en clair.
//  "Restaurer cet element depuis V4" : cette section, cette vue, ce script
//  revient, en une commande (Ctrl+Z l'annule) ; l'ecran le fait (Hosts).
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../hmi/HmiVersions.hpp"
#include "../ui/Widget.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ui {
class TableView;
class StatusBar;
class DropDown;
}

namespace app {

class HmiToolStrip;

class VersionComparePane final : public ui::Widget {
public:
    enum Action : int { APrev = 1, ANext, ASide, AUnified, ARestore };
    struct Hosts {
        // Restaurer un element depuis la version `from` : une commande de l'ecran.
        std::function<bool(const hmi::ver::Element&, int from, std::string* why)> restoreElement;
        // Les versions choisies ont change : le titre de l'onglet suit.
        std::function<void(const std::string& title)> retitle;
    };

    VersionComparePane(std::string id, std::string folder, int a, int b);
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    void compare(int a, int b);
    // Choisir l'element de cette cle (vide : le premier).
    void showElement(const std::string& key);
    void step(int delta);                  // difference suivante (+1) ou precedente (-1)
    void setUnified(bool);
    bool restoreCurrent(std::string* why = nullptr);
    [[nodiscard]] bool unified() const noexcept { return unified_; }
    [[nodiscard]] int  a() const noexcept { return a_; }
    [[nodiscard]] int  b() const noexcept { return b_; }
    [[nodiscard]] std::string title() const;       // "Comparer V4 <-> V5"
    [[nodiscard]] static std::string titleFor(int a, int b);
    [[nodiscard]] const hmi::ver::Comparison&           comparison() const noexcept { return comp_; }
    [[nodiscard]] const hmi::ver::Element*              current() const;
    [[nodiscard]] const std::vector<hmi::ver::DiffLine>& diff() const noexcept { return diff_; }
    [[nodiscard]] const std::vector<std::string>&        leftLines() const noexcept { return left_; }
    [[nodiscard]] const std::vector<std::string>&        rightLines() const noexcept { return right_; }
    [[nodiscard]] const std::vector<hmi::ver::ObjectChange>& viewChanges() const noexcept { return viewChanges_; }
    [[nodiscard]] ui::TableView& elements() noexcept { return *list_; }
    [[nodiscard]] HmiToolStrip&  tools() noexcept { return *tools_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] static bool restorable(const hmi::ver::Element&);

protected:
    void onLayout() override;

private:
    friend class CompareBody;
    void rebuildList();
    void showCurrent();
    void loadProjects();
    void say(std::string text, bool error = false);
    [[nodiscard]] std::string versionLabel(int n) const;

    std::string                          folder_;
    Hosts                                hosts_;
    hmi::ver::Store                      store_;
    hmi::ver::Comparison                 comp_;
    int                                  a_{0}, b_{0};
    std::vector<int>                     rowElement_;    // ligne de la liste -> element (-1 : un titre)
    int                                  current_{-1};
    std::vector<std::string>             left_, right_;
    std::vector<hmi::ver::DiffLine>      diff_;
    std::vector<hmi::ver::ObjectChange>  viewChanges_;
    std::vector<std::string>             details_;       // ce qui a change, en clair (le reste)
    std::shared_ptr<hmi::Project>        pa_, pb_;       // les IHM des deux cotes (a la demande)
    bool                                 loaded_{false};
    bool                                 unified_{false};
    bool                                 filling_{false};
    float                                scroll_{0.f};   // en lignes
    HmiToolStrip*                        tools_{nullptr};
    ui::DropDown*                        boxA_{nullptr};
    ui::DropDown*                        boxB_{nullptr};
    ui::TableView*                       list_{nullptr};
    ui::Widget*                          body_{nullptr};
    ui::StatusBar*                       status_{nullptr};
    std::string                          message_;
    core::ConnectionScope                links_;
};

} // namespace app
