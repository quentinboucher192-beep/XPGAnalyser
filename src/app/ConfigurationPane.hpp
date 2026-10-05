// =============================================================================
//  app/ConfigurationPane.hpp - lot API 4 : l'automate, ses racks, ses voies
// -----------------------------------------------------------------------------
//  L'onglet Configuration de l'API, en cinq sous-onglets - les memes que les
//  cinq entrees de Configuration dans l'arbre, avec leur nombre :
//
//   * Processeur        la reference, la famille, la memoire (la grille d'avant) ;
//   * Racks et modules  les racks dessines (RackView) avec, par module, les voies
//                       que le programme emploie (« 14/16 ») ; le module choisi a
//                       droite ; Rack, Module, Remplacer, Supprimer, Importer le .XHW ;
//   * Voies et adresses chaque %I / %Q / %IW / %QW du code face au module de son
//                       emplacement (project/IoCheck) : sans module, dans le mauvais
//                       sens, voie en trop - et que faire ;
//   * Reseau            les ports de communication que le .XHW declare ;
//   * Plan memoire      les %MW des variables situees, un carre par mot, les
//                       chevauchements, la plus grande place libre.
//
//  Chaque modification du materiel est une commande (Ctrl+Z la reprend).
// =============================================================================
#pragma once

#include "TaskPanes.hpp"      // ApiPaneHosts

#include "../project/IoCheck.hpp"
#include "../ui/Widget.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ui { class TableView; class PropertyGrid; class TabControl; }

namespace app {

class ApiFrame;
class RackView;

class ConfigurationPane final : public ui::Widget {
public:
    enum Tab : int { TProcessor = 0, TRacks, TChannels, TNetwork, TMemory, TCount };
    enum Action : int {
        AAddRack = 1, AAddModule, AReplace, ARemove, AImportXhw, ACheck,
        AFaultyOnly, AShowModule, AFindGap, AOverlapsOnly,
        // Lot API 5 : les trois zones du plan memoire, leurs bornes de lecture.
        AZoneBits, AZoneWords, AZoneConstants, ABounds, AWholeZone,
    };

    explicit ConfigurationPane(std::string id);
    ~ConfigurationPane() override;
    // `hosts.request` recoit "create.rack", "create.module", "import-xhw".
    void setHosts(ApiPaneHosts h);
    void attach(ApiFrame& frame);
    void refresh();
    void runAction(int action);

    void showTab(int tab);
    [[nodiscard]] int currentTab() const;
    bool selectModule(std::uint16_t rack, std::int16_t slot);
    bool selectAddress(std::string_view text);
    [[nodiscard]] const project::io::Report& report() const noexcept { return report_; }
    [[nodiscard]] const project::io::Memory& memory() const noexcept { return memory_; }
    // Lot API 5 : %M, %MW, %KW (indice : domain::MemoryZone) ; la zone montree.
    [[nodiscard]] const std::vector<project::io::ZoneMap>& zones() const noexcept { return zones_; }
    [[nodiscard]] std::size_t currentZone() const noexcept { return zone_; }
    void showZone(std::size_t zone);
    [[nodiscard]] ui::TabControl& tabs() noexcept { return *tabs_; }
    [[nodiscard]] RackView& racks() noexcept { return *rackView_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    class ChannelsModel;
    class NetworkModel;
    class MemoryMapView;
    friend class ChannelsModel;
    friend class NetworkModel;
    friend class MemoryMapView;

    void refreshProperties();
    void rebuildChannelRows();
    void setWindow(std::size_t zone, domain::MemoryWindow window);
    void updateHint();
    [[nodiscard]] const domain::Module* moduleAt(std::uint16_t rack, std::int16_t slot) const;

    ApiPaneHosts                          hosts_;
    ApiFrame*                             frame_{nullptr};
    ui::TabControl*                       tabs_{nullptr};
    ui::PropertyGrid*                     props_{nullptr};
    ui::PropertyGrid*                     processor_{nullptr};
    RackView*                             rackView_{nullptr};
    ui::TableView*                        channels_{nullptr};
    ui::TableView*                        network_{nullptr};
    MemoryMapView*                        memoryView_{nullptr};
    std::shared_ptr<ChannelsModel>        channelsModel_;
    std::shared_ptr<NetworkModel>         networkModel_;
    project::io::Report                   report_;
    project::io::Memory                   memory_;
    std::vector<project::io::ZoneMap>     zones_;
    std::size_t                           zone_{1};          // %MW d'abord
    std::vector<project::io::Port>        ports_;
    std::vector<std::size_t>              channelRows_;      // indices dans report_.addresses
    bool                                  faultyOnly_{false};
    bool                                  overlapsOnly_{false};
    bool                                  showGap_{false};
    int                                   rack_{-1};
    int                                   slot_{-32768};
    std::uint32_t                         word_{0};
    bool                                  hasWord_{false};
    bool                                  syncing_{false};
    core::ConnectionScope                 links_;
};

} // namespace app
