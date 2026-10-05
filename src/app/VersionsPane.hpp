// =============================================================================
//  app/VersionsPane.hpp - le volet Versions (lot 21)
// -----------------------------------------------------------------------------
//  EN HAUT, la barre : Creer une version (Ctrl+Alt+S), Comparer, Restaurer,
//  Exporter (.zip), Extraire dans un dossier, Supprimer ; a droite, la version
//  automatique (jamais, a chaque enregistrement, au plus une par heure).
//  DESSOUS, LA FRISE : les versions dans le temps (livree en vert, validee en
//  bleu, automatiques en petit), le travail en cours au bout.
//  AU MILIEU, LE TABLEAU : le travail en cours, puis la plus recente d'abord ;
//  et "CE QUI CHANGE" pour la ligne choisie, element par element (double clic :
//  la comparaison). A DROITE, LA FICHE de la version choisie (nom, etat,
//  commentaire se changent la).
//
//  Le volet ne fait rien lui-meme qui demande un dialogue : il le demande a
//  l'ecran (Hosts). Tout le reste passe par hmi::ver, sur le disque.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../hmi/HmiVersions.hpp"
#include "../ui/Widget.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ui {
class TableView;
class PropertyGrid;
class StatusBar;
class DropDown;
}

namespace app {

class HmiToolStrip;

class VersionsPane final : public ui::Widget {
public:
    enum Action : int { ACreate = 1, ACompare, ARestore, AExport, AExtract, ADelete };
    struct Hosts {
        std::function<void()>                                     create;
        std::function<void(int)>                                  restore, exportZip, extract, remove;
        std::function<void(int, int, const std::string&)>         compare;     // a, b (0 : en cours), l'element a montrer
        std::function<std::size_t()>                              unsaved;     // modifications non enregistrees
        std::function<void()>                                     changed;     // l'index a change (l'arbre suit)
        // Lot API 6 : "V4 en cours . DEV", "V3 validee . FINISH" - la premiere ligne et la frise.
        std::function<std::string(const hmi::ver::Store&)>        workingTitle;
    };

    VersionsPane(std::string id, std::string folder);
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    void setFolder(std::string folder);
    [[nodiscard]] const std::string& folder() const noexcept { return folder_; }
    // Relit versions/ et compare le disque a la derniere version.
    void refresh();
    // 0 : le travail en cours ; un numero de version.
    void selectVersion(int number);
    [[nodiscard]] int  selectedVersion() const;       // -1 : rien
    [[nodiscard]] const hmi::ver::Store&      store() const noexcept { return store_; }
    [[nodiscard]] const hmi::ver::Comparison& shownChanges() const noexcept { return shown_; }
    [[nodiscard]] const hmi::ver::Comparison& workChanges() const noexcept { return work_; }
    [[nodiscard]] std::pair<int, int>         shownPair() const noexcept { return {shown_.a, shown_.b}; }
    // Les gestes de la fiche, pour les scripts et les tests.
    bool setInfo(int number, const std::string& name, hmi::ver::State state, const std::string& comment);
    bool setAutoMode(hmi::ver::AutoMode);
    [[nodiscard]] ui::TableView&    table() noexcept { return *table_; }
    [[nodiscard]] ui::TableView&    changes() noexcept { return *changes_; }
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *grid_; }
    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] ui::DropDown&     autoBox() noexcept { return *auto_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    void say(std::string text, bool error = false);
    // La frise : le point d'une version (0 : le travail en cours), pour les tests.
    [[nodiscard]] gfx::Rect dotRect(int number) const;
    // Lot API 6 : la frise defile. La bande des versions, si un point s'y voit,
    // le defilement (0 : la plus ancienne a gauche), et le geste de la molette.
    [[nodiscard]] gfx::Rect timelineStrip() const;
    [[nodiscard]] bool      dotVisible(int number) const;
    [[nodiscard]] float     timelineScroll() const;
    [[nodiscard]] bool      timelineScrolls() const;
    void                    scrollTimeline(float dx);

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    friend class VersionTimeline;
    void rebuildTable();
    void rebuildChanges();
    void rebuildProperties();
    void updateStatus();

    std::string             folder_;
    Hosts                   hosts_;
    hmi::ver::Store         store_;
    hmi::ver::Comparison    work_;          // la derniere version -> le disque
    hmi::ver::Comparison    shown_;         // ce que montre "Ce qui change"
    std::vector<int>        rows_;          // ligne du tableau -> numero (0 : en cours)
    HmiToolStrip*           tools_{nullptr};
    ui::DropDown*           auto_{nullptr};
    ui::Widget*             timeline_{nullptr};
    ui::TableView*          table_{nullptr};
    ui::TableView*          changes_{nullptr};
    ui::PropertyGrid*       grid_{nullptr};
    ui::StatusBar*          status_{nullptr};
    std::string             message_;
    std::string             changesTitle_;
    float                   changesTitleY_{0.f};
    bool                    filling_{false};
    core::ConnectionScope   links_;
};

} // namespace app
